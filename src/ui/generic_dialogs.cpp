#include "ui/generic_dialogs.h"

#include <cassert>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/api.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/terrain_editor.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/text_edit.h"
#include "window/window.h"
#include "window/wn_resource_provider.h"

// ScMessageServer routes this subscribed UI notification to UiMainManagerObject::DoMessage.
// Windows and Linux acknowledge it without action; its request purpose remains unresolved.
enum UiUnresolvedMainMessage : u32 {
  kWnMsg_Unresolved35 = 0x624a8300,
};

// The main controller selects this initial game-menu view after attaching its panel.
enum UiInitialMenuView : u32 {
  kUiInitialGameMenuView = 0xd1001000,
};

// The user-input dialog assigns this window identity after configuring its generic frame.
enum UiUserInputIdentity : u32 {
  kUiUserInputWindowId = 0x454423ff,
};

UiMessageBox::UiMessageBox() : references(0) {
}

UiMessageBox::~UiMessageBox() {
}

// Publish the common callback or dialog identity before retaining, clearing the output.
bool UiMessageBox::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback && iid != IID_UiMessageBox))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiMessageBox::AddRef() {
  return ++references;
}

// Delete through the real virtual destructor when the wrapped DWORD decrement is not positive as a signed value.
u32 UiMessageBox::Release() {
  u32 value = --references;
  if ((i32)value > 0)
    return value;
  delete this;
  return 0;
}

// Route generic-window notifications through the same callback receiver, retaining pointer-valued layout arguments.
bool UiMessageBox::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (!window)
    return false;
  switch (message) {
  case kWnMsg_Command:
    return first == kWnCommandButtonActivated && OnCommand(window, (u32)second) != 0;
  case kWnMsg_KeyDown:
    return OnKey(window, (u32)first, (u32)second) != 0;
  case kWnMsg_Init:
    return Attach(window) != 0;
  case kWnMsg_Shutdown:
    return true;
  case kWnMsg_ClientConstraints:
    return first && GetLimits(window, reinterpret_cast<ScRect *>(first)) != 0;
  case kWnMsg_ClientLayout:
    return first && Layout(window, *reinterpret_cast<const ScRect *>(first)) != 0;
  default:
    return false;
  }
}

// Drop the successful generic-interface query before borrowing its live data, preserving replacement during Release.
UiMessageBoxData *UiMessageBox::GetData(WnWindow *window) {
  UiGenericInterface *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)) || !generic)
    return nullptr;
  generic->Release();
  return reinterpret_cast<UiMessageBoxData *>(generic->GetData());
}

// Post the modal completion word through the borrowed manager when available; the post result is deliberately ignored.
u8 UiMessageBox::End(WnWindow *window, u32 command) {
  WnWindowManager *windows = UiGetWindowManager();
  if (windows)
    windows->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), command);
  return 1;
}

// Translate the frame-close command through current dialog data; the seven direct button commands need no data query.
u8 UiMessageBox::OnCommand(WnWindow *window, u32 command) {
  if (command == kUiFrameCloseButtonId) {
    UiMessageBoxData *data = GetData(window);
    if (!data)
      return 0;
    End(window, data->cancel);
    return 1;
  }
  if (command <= kWnModalButtonIdBase || command > kWnYesButtonId)
    return 0;
  End(window, command);
  return 1;
}

// Resolve the live payload before every key, choosing accept for Enter and cancel for Escape or Alt-F4.
u8 UiMessageBox::OnKey(WnWindow *window, u32 key, u32 modifiers) {
  UiMessageBoxData *data = GetData(window);
  if (!data)
    return 0;
  if (key == kWnKeyReturn)
    End(window, data->accept);
  else {
    if (key != kWnKeyEscape && (key != kWnKeyF4 || !(modifiers & kWnModifierAlt)))
      return 0;
    End(window, data->cancel);
  }
  return 1;
}

// Widen nonempty dialog text until its fitted height is at most half its width, then restore the complete saved area.
u8 UiMessageBox::MeasureText(WnWindow *window, ScRect *out) {
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiMessageBoxBodyId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    out->left = 0;
    out->top = 0;
    WnWindow *text = label->GetWindow();
    if (text->GetCaption()->Strlen()) {
      ScRect saved = *text->GetArea();
      i32 width = 216;
      do {
        width = (i32)((u32)width + 16);
        text->SetW(width);
        label->Fit();
        i32 half_width = text->GetW() / 2;
        if (text->GetH() <= half_width)
          break;
      } while (true);
      out->right = text->GetW();
      out->bottom = text->GetH();
      text->SetArea(saved.left, saved.top, saved.right, saved.bottom);
    } else {
      out->right = 16;
      out->bottom = 16;
    }
    label->Release();
  }
  return 1;
}

u8 UiMessageBox::MeasureButtons(WnWindow *window, ScRect *out) {
  UiMessageBoxData *data = GetData(window);
  if (!data)
    return 0;
  ScRect limits = {0, 0, 0, 0};
  WnButton *button = nullptr;
  out->left = 0;
  out->top = 0;
  i32 width = 100, height = 24;
  for (u32 i = 0; i != 3; ++i) {
    u32 id = data->buttons[i];
    if (id && window->GetChildAs(id, IID_WnButton, reinterpret_cast<void **>(&button))) {
      if (button->GetSizeLimits(&limits)) {
        if (limits.left > width)
          width = limits.left;
        if (limits.top > height)
          height = limits.top;
      }
      button->Release();
      if (i != 2)
        button = nullptr;
    }
  }
  out->right = width > 100 ? width : 100;
  out->bottom = height > 24 ? height : 24;
  return 1;
}

// Publish the parent's available extent, then combine fitted text, optional icon and live button IDs into minimum limits.
u8 UiMessageBox::GetLimits(WnWindow *window, ScRect *out) {
  WnWindow *parent = window->GetParentWin();
  if (!parent)
    return 0;
  out->left = 232;
  out->top = 96;
  out->right = (i32)((u32)parent->GetW() - 16);
  out->bottom = (i32)((u32)parent->GetH() - 16);
  UiMessageBoxData *data = GetData(window);
  if (data) {
    ScRect text = {0, 0, 0, 0}, buttons = {0, 0, 0, 0};
    if (MeasureText(window, &text) && MeasureButtons(window, &buttons)) {
      WnWindow *icon = window->GetChildWindowFromID(kUiMessageBoxIconId);
      i32 icon_width = 0, icon_height = 0;
      if (icon) {
        icon_height = icon->GetH();
        icon_width = icon->GetW();
      }
      u32 count = data->buttons[0] != 0;
      if (data->buttons[1])
        ++count;
      if (data->buttons[2])
        ++count;
      i32 button_width = (i32)(count * ((u32)buttons.right - buttons.left + 8) - 6);
      i32 width = (i32)((u32)text.right - text.left);
      if (icon_width)
        width = (i32)((u32)width + icon_width + 16);
      if (width < button_width)
        width = button_width;
      i32 height = (i32)((u32)text.bottom - text.top);
      if (icon_height > height)
        height = icon_height;
      height = (i32)((u32)height + buttons.bottom - buttons.top + 10);
      out->left = width > 232 ? width : 232;
      out->top = height > 96 ? height : 96;
    }
  }
  return 1;
}

// Place the panel, icon and text before centering existing buttons; a missing button does not advance the next position.
u8 UiMessageBox::Layout(WnWindow *window, const ScRect &client) {
  UiMessageBoxData *data = GetData(window);
  if (!data)
    return 0;
  ScRect buttons = {0, 0, 0, 0};
  MeasureButtons(window, &buttons);
  WnWindow *row = window->GetChildWindowFromID(kUiMessageBoxButtonRowId);
  i32 bottom;
  if (row) {
    row->SetArea(
        client.left, (i32)((u32)client.bottom - buttons.bottom + buttons.top - 2), client.right, client.bottom);
    bottom = (i32)((u32)row->GetT() - 8);
  } else
    bottom = client.bottom;
  ScRect content = {client.left, client.top, client.right, bottom};
  WnWindow *panel = window->GetChildWindowFromID(kUiMessageBoxPanelId);
  if (panel)
    panel->SetArea(content.left, content.top, content.right, bottom);
  WnWindow *icon = window->GetChildWindowFromID(kUiMessageBoxIconId);
  i32 left;
  if (icon) {
    i32 height = icon->GetH();
    i32 offset = (i32)((u32)bottom - content.top - height) / 2;
    icon->GZWinMoveTo((i32)((u32)client.left + 8), (i32)((u32)offset + client.top));
    left = icon->GetR();
  } else
    left = client.left;
  WnWindow *text = window->GetChildWindowFromID(kUiMessageBoxBodyId);
  if (text)
    text->SetArea(left, content.top, (i32)((u32)content.right - 8), content.bottom);
  u32 first = data->buttons[0], count = first != 0;
  if (data->buttons[1])
    ++count;
  if (data->buttons[2])
    ++count;
  i32 width = (i32)((u32)buttons.right - buttons.left);
  i32 height = (i32)((u32)buttons.bottom - buttons.top);
  i32 top = (i32)((u32)client.bottom - buttons.bottom + buttons.top - 1);
  i32 remaining = (i32)((u32)client.right - count * ((u32)width + 8) - client.left + 8);
  i32 x = (i32)((u32)client.left + remaining / 2);
  for (u32 i = 0; i != 3; ++i) {
    u32 id = i ? data->buttons[i] : first;
    if (!id)
      continue;
    WnWindow *button = window->GetChildWindowFromID(id);
    if (button) {
      button->SetSize(width, height);
      button->GZWinMoveTo(x, top);
      if (i != 2)
        x = (i32)((u32)x + width + 8);
    }
  }
  return 1;
}

// Build the live dialog children before sizing and centering, preserving partial attachment when required creation fails.
u8 UiMessageBox::Attach(WnWindow *window) {
  UiMessageBoxData *data = GetData(window);
  if (!data)
    return 0;
  UiGenericInterface *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)) || !generic)
    return 0;
  generic->Release();
  generic->SetStyle(kUiGenericDraggable, true);
  generic->SetStyle(kUiGenericBackButton, false);
  generic->SetStyle(kUiGenericSecondButton, false);
  generic->SetStyle(kUiGenericNoFrame, false);
  window->SetCaption(data->caption);
  window->SetFlag(kWnFlagPrivateBuffer, true);
  CreatePanel(window, kUiMessageBoxPanelId);
  CreatePanel(window, kUiMessageBoxButtonRowId);
  for (u32 i = 0; i != 3; ++i) {
    u32 id = data->buttons[i];
    if (id && !CreateButton(window, id))
      return 0;
  }
  if (!CreateText(window, kUiMessageBoxBodyId, &data->body))
    return 0;
  u32 icon = data->style & 0xf0000;
  if (icon)
    CreateIcon(window, kUiMessageBoxIconId, icon);
  window->SetSize(0, 0);
  WnWindow *parent = window->GetParentWin();
  i32 parent_width = parent->GetW(), width = window->GetW();
  i32 x = (i32)((u32)parent_width - width) / 2;
  i32 parent_height = parent->GetH(), height = window->GetH();
  i32 y = (i32)((u32)parent_height - height) / 2;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    ScMessage query = {kWnMsg_QueryUiClientArea, 0, 0, 0};
    server->MessageSend(query);
    u32 left = (u32)query.data1 >> 16, right = (u32)query.data2 >> 16;
    u32 top = (u16)query.data1, bottom = (u16)query.data2;
    width = window->GetW();
    x = (i32)(left + (i32)(right - left - (u32)width) / 2);
    height = window->GetH();
    y = (i32)(top + (i32)(bottom - top - (u32)height) / 2);
    if (x < 0 || x > parent->GetR()) {
      parent_width = parent->GetW();
      width = window->GetW();
      x = (i32)((u32)parent_width - width) / 2;
    }
    if (y < 0 || y > parent->GetB()) {
      parent_height = parent->GetH();
      height = window->GetH();
      y = (i32)((u32)parent_height - height) / 2;
    }
  }
  window->GZWinMoveTo(x, y);
  return 1;
}

// Configure the body or button-row panel before attaching its freshly obtained primary view, then release the control.
u8 UiMessageBox::CreatePanel(WnWindow *window, u32 identifier) {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  WnFlatControl *panel = nullptr;
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
    return 0;
  panel->GetWindow()->SetID(identifier);
  panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
  if (identifier == kUiMessageBoxPanelId) {
    panel->SetOutlineFlags(32);
    panel->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
  } else {
    panel->SetOutlineFlags(15);
    panel->SetFillColor(resources->GetColor(kWnThemePanelFillColorId));
    panel->SetOutlineColor(resources->GetColor(kWnThemeBorderColorId));
  }
  u8 result = window->ChildAdd(panel->GetWindow());
  panel->Release();
  return result;
}

// Resolve the button font and borrowed caption before loading its image; release the button before the scoped image.
u8 UiMessageBox::CreateButton(WnWindow *window, u32 identifier) {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  FtFont *font = resources->GetFont(kWnFontUiTitle);
  if (!font)
    return 0;
  const ScStringView *text = resources->GetText(identifier);
  if (!text)
    return 0;
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(kRsMessageBoxButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  if (!image.GetBuffer())
    return 0;
  WnButton *button = nullptr;
  if (!UiGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button)))
    return 0;
  WnWindow *primary = button->GetWindow();
  GxBuffer *buffer = image.GetBuffer();
  u32 first = primary->MakeFillColor(128, 128, 128), second = primary->MakeFillColor(64, 64, 64);
  button->SetStyle(kWnButtonPushStretch, buffer, buffer->GetBufferArea());
  button->SetFont(font);
  button->SetFontColor(kWnButtonTextDisabled, first);
  button->SetFontColor(kWnButtonTextDisabledEnd, second);
  button->SetGutters(11, 0, 16, 2);
  button->SetButtonFlag(kWnButtonAutoFit, false);
  primary->SetID(identifier);
  primary->SetCaption(*text);
  u8 result = window->ChildAdd(primary);
  button->Release();
  return result;
}

// Bind the supplied body text and borrowed font before configuring wrapping, gutters and alignment on the label.
u8 UiMessageBox::CreateText(WnWindow *window, u32 identifier, const ScStringView *body) {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  FtFont *font = resources->GetFont(kWnFontUiMain);
  if (!font)
    return 0;
  WnLabelControl *label = nullptr;
  if (!UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label)))
    return 0;
  WnWindow *primary = label->GetWindow();
  primary->SetID(identifier);
  primary->SetCaption(*body);
  primary->SetFlag(kWnFlagPrivateBuffer, true);
  label->SetFont(font);
  label->SetAlignment(kWnLabelCenterMiddle);
  label->SetBackground(false);
  label->SetGutters(2, 2);
  label->SetLabelFlag(kWnLabelWrapText, true);
  u8 result = window->ChildAdd(primary);
  label->Release();
  return result;
}

// Copy the selected atlas quarter into a new locked buffer, then attach a bitmap window retaining that copied image.
u8 UiMessageBox::CreateIcon(WnWindow *window, u32 identifier, u32 kind) {
  u32 tile;
  switch (kind) {
  case 0x10000:
    tile = 1;
    break;
  case 0x20000:
    tile = 2;
    break;
  case 0x30000:
    tile = 3;
    break;
  case 0x40000:
    tile = 0;
    break;
  default:
    return 0;
  }
  GxGraphics *graphics = UiGetGraphics();
  if (!graphics)
    return 0;
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(kRsMessageBoxIconAtlasImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  if (!image.GetBuffer())
    return 0;
  GxBuffer *atlas = image.GetBuffer();
  u32 width = atlas->Width() >> 2, height = atlas->Height();
  GxBuffer *copy = nullptr;
  if (!graphics->CreateBuffer(&copy))
    return 0;
  u8 result = 0;
  if (copy->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false) &&
      copy->Lock(GxLockFlags::Blt)) {
    ScRect source = {(i32)(tile * width), 0, (i32)(width + tile * width), (i32)atlas->Height()};
    copy->Blt(atlas, source, *(copy->GetBufferArea()), nullptr);
    copy->Unlock(GxLockFlags::Blt);
    WnBitmapControl *bitmap = nullptr;
    if (UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
      bitmap->GetWindow()->SetID(identifier);
      bitmap->SetImage(copy);
      WnWindow *primary = bitmap->GetWindow();
      i32 image_height = copy->Height(), image_width = copy->Width();
      primary->SetSize(image_width, image_height);
      result = window->ChildAdd(bitmap->GetWindow());
      bitmap->Release();
    }
  }
  copy->Release();
  return result;
}

// Construct the real message-box callback with no initial external reference and return its primary unknown identity.
ScUnknown *UiCreateClass3370D1A5() {
  return new UiMessageBox;
}

// Allocate the original callback owner at zero references for the message command's private retained-query helper.
UiMessageBox *UiCreateMessageBox() {
  return new UiMessageBox;
}

// Own the modal payload through callback removal, window shutdown and interface release, preserving the modal result.
u8 UiMessageBox::Show(u32 *out, const ScStringView &body, const ScStringView &caption, u32 style) {
  WnWindowManager *windows = UiGetWindowManager();
  if (!windows)
    return 0;
  UiGenericInterface *generic = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  UiMessageBoxData *data = new UiMessageBoxData;
  {
    ScString copy(body);
    data->body = copy;
  }
  {
    ScString copy(caption);
    data->caption = copy;
  }
  data->style = style;
  switch ((u16)style) {
  case 0:
    data->buttons[1] = 0;
    data->accept = kWnOkButtonId;
    data->cancel = kWnOkButtonId;
    data->word52 = kWnOkButtonId;
    data->buttons[0] = kWnOkButtonId;
    data->buttons[2] = 0;
    break;
  case 1:
  case 2:
  case 3: {
    u32 accept = (u16)style == 1 ? kWnOkButtonId : (u16)style == 2 ? kWnRetryButtonId : kWnYesButtonId;
    u32 cancel = (u16)style == 3 ? kWnNoButtonId : kWnCancelButtonId;
    data->accept = accept;
    data->cancel = cancel;
    data->word52 = cancel;
    data->buttons[0] = accept;
    data->buttons[1] = cancel;
    data->buttons[2] = 0;
    break;
  }
  case 4:
    data->accept = kWnYesButtonId;
    data->cancel = kWnCancelButtonId;
    data->word52 = kWnCancelButtonId;
    data->buttons[0] = kWnYesButtonId;
    data->buttons[1] = kWnNoButtonId;
    data->buttons[2] = 0;
    break;
  case 5:
    data->accept = kWnRetryButtonId;
    data->cancel = kWnIgnoreButtonId;
    data->word52 = kWnIgnoreButtonId;
    data->buttons[0] = kWnAbortButtonId;
    data->buttons[1] = kWnRetryButtonId;
    data->buttons[2] = kWnIgnoreButtonId;
    break;
  }
  generic->SetCallback(this);
  generic->SetData(reinterpret_cast<UiWindowData *>(data));
  *out = windows->DoModalWin(generic->GetWindow());
  generic->SetCallback(nullptr);
  generic->GetWindow()->Shutdown();
  generic->Release();
  delete data;
  return 1;
}

// Initialize the source-owned extension fields; resource index and client rectangle stay unset until their real setters.
UiGenericWindow::UiGenericWindow()
    : style(0), word172(0), word176(0), minimum_width(0), minimum_height(0), frame(nullptr), image(nullptr),
      callback(nullptr), data(nullptr), border_x(4), border_y(4) {
  SetStyle(kUiGenericCloseButton, true);
  SetStyle(kUiGenericCloseOnEscapeAltF4, true);
}

UiGenericWindow::~UiGenericWindow() {
  UiGenericWindow::Shutdown();
}

// Publish the two generic-window secondary IIDs; base queries retain the canonical window/message identities.
bool UiGenericWindow::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiGenericInterface && iid != IID_UiGenericBase)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiGenericInterface *>(this);
  AddRef();
  return true;
}

u32 UiGenericWindow::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiGenericWindow::Release() {
  return UiBaseWindow::Release();
}

// Return the borrowed primary view from the generic secondary interface without adding a reference.
WnWindow *UiGenericWindow::GetWindow() {
  return this;
}

bool UiGenericWindow::GetStyle(u32 flag) {
  return (style & flag) != 0;
}

// Retain the new callback before releasing the old one, including replacement with the same actual receiver.
bool UiGenericWindow::SetNotify(UiWindowCallback *value) {
  if (value)
    value->AddRef();
  if (callback)
    callback->Release();
  callback = value;
  return true;
}

bool UiGenericWindow::SetCallback(UiWindowCallback *value) {
  return SetNotify(value);
}

u32 UiGenericWindow::GetClassID() {
  return kUiGenericClassId;
}

u8 UiGenericWindow::GetCallback(UiWindowCallback **out) {
  *out = callback;
  return 1;
}

// Write the resource index before the borrowed image pointer without retaining either output.
u8 UiGenericWindow::GetImage(u32 *index, GxBuffer **out) {
  *index = resource_index;
  *out = image;
  return 1;
}

u8 UiGenericWindow::SetData(UiWindowData *value) {
  data = value;
  return 1;
}

// Return the current borrowed data pointer without changing its lifetime.
UiWindowData *UiGenericWindow::GetData() {
  return data;
}

// Only data index zero exists; forward it through the live setter and preserve that method's raw byte result.
u8 UiGenericWindow::SetIndexedData(u32 index, UiWindowData *value) {
  if (index)
    return 0;
  return SetData(value);
}

// Publish the live virtual getter's borrowed pointer for index zero, leaving the output untouched for other indices.
u8 UiGenericWindow::GetIndexedData(u32 index, UiWindowData **out) {
  if (index)
    return 0;
  *out = GetData();
  return 1;
}

// Store the genuine BYTE border dimensions and return the source's success Boolean.
bool UiGenericWindow::SetBorder(u8 horizontal, u8 vertical) {
  border_x = horizontal;
  border_y = vertical;
  return true;
}

// Update the real style field before forwarding frame flags or rebuilding frame/scroller geometry.
bool UiGenericWindow::SetStyle(u32 flag, u32 enabled) {
  style = (u8)enabled ? style | flag : style & ~flag;
  if (frame &&
      (flag == kUiGenericCloseButton || flag == kUiGenericBackButton || flag == kUiGenericSecondButton ||
          flag == kUiGenericCloseButtonState || flag == kUiGenericBackButtonState ||
          flag == kUiGenericSecondButtonState)) {
    u32 mapped = flag == kUiGenericCloseButton ? 1
        : flag == kUiGenericBackButton         ? 4
        : flag == kUiGenericSecondButton       ? 2
        : flag == kUiGenericSecondButtonState  ? 512
        : flag == kUiGenericBackButtonState    ? 1024
                                               : 256;
    frame->SetStyle(mapped, enabled);
    return true;
  }
  if (flag == kUiGenericNoFrame || flag == kUiGenericScroller) {
    if (flag == kUiGenericNoFrame && (u8)enabled) {
      if (frame) {
        ChildRemove(frame);
        frame->Release();
        minimum_width = 2 * border_x;
        frame = nullptr;
        minimum_height = 2 * border_y;
        WnWindow *scroller = GetChildWindowFromID(kUiGenericScrollerId);
        if (scroller)
          scroller->SetArea(border_x, border_y, border_y + 8, (i32)((u32)GetH() - border_y));
      }
    } else if (flag == kUiGenericNoFrame)
      CreateFrame();
    else if ((u8)enabled)
      AddScroller();
    else
      RemoveScroller();
    i32 height = GetH(), width = GetW();
    SetClientEffects(width, height, &client);
    Relayout();
  }
  return true;
}

// Used-effect phase of the original client conversion: output words retain their source store/read ordering.
// Frame and border fields are read live because callbacks may change them before later stores.
void UiGenericWindow::SetClientEffects(i32 width, i32 height, ScRect *out) {
  out->left = border_x;
  if (style & kUiGenericScroller)
    out->left = (i32)((u32)out->left + border_x + 8);
  out->right = (i32)((u32)width - border_x);
  i32 top = frame ? (i32)((u32)frame->GetB() + border_y) : border_y;
  out->top = top;
  i32 bottom = (i32)((u32)height - border_y);
  top = out->top;
  out->bottom = bottom;
  if (bottom <= top)
    bottom = top;
  out->bottom = bottom;
}

// Convert a client rectangle to its outer window bounds using live border and frame dimensions.
void UiGenericWindow::SetOuterEffects(const ScRect *rectangle, ScRect *out) {
  *out = *rectangle;
  out->left = (i32)((u32)out->left - border_x);
  if (style & kUiGenericScroller)
    out->left = (i32)((u32)out->left - border_x - 8);
  out->right = (i32)((u32)out->right + border_x);
  if (frame)
    out->top = (i32)((u32)out->top - frame->GetH() - 2 * border_y);
  else
    out->top = (i32)((u32)out->top - border_y);
  out->bottom = (i32)((u32)out->bottom + border_y);
}

// Negotiate client limits with the live callback, resizing through the original virtual method when bounds change.
// Otherwise notify the current callback with the actual mutable client rectangle.
bool UiGenericWindow::Relayout() {
  if (!callback)
    return false;
  ScRect rectangle = client, limits = {0, 0, 0xffff, 0xffff};
  if (callback->DoWinMsg(
          this, kWnMsg_ClientConstraints, reinterpret_cast<uptr>(&limits), reinterpret_cast<uptr>(&rectangle), 0)) {
    i32 width = (i32)((u32)rectangle.right - rectangle.left);
    if (width < limits.left)
      rectangle.right = (i32)((u32)rectangle.left + limits.left);
    else if (width > limits.right)
      rectangle.right = (i32)((u32)rectangle.left + limits.right);
    i32 height = (i32)((u32)rectangle.bottom - rectangle.top);
    if (height < limits.top)
      rectangle.bottom = (i32)((u32)rectangle.top + limits.top);
    else if (height > limits.bottom)
      rectangle.bottom = (i32)((u32)rectangle.top + limits.bottom);
    ScRect outer;
    SetOuterEffects(&rectangle, &outer);
    width = (i32)((u32)outer.right - outer.left);
    height = (i32)((u32)outer.bottom - outer.top);
    if (width != GetW() || height != GetH()) {
      SetSize(width, height);
      return true;
    }
  }
  callback->DoWinMsg(this, kWnMsg_ClientLayout, reinterpret_cast<uptr>(&client), 0, 0);
  return true;
}

// Initialize the base before acquiring the provider and optional default image, then prepare frame and child layout.
// Register the live message target, clear capture and select keyboard focus before the current callback's attach notice.
bool UiGenericWindow::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  UiBaseWindow::Init();
  WnResources *resources = UiGetWindowResources();
  if (!resources || !callback)
    return false;
  if (!image)
    SetImage(0, resources->GetImage(kRsGenericWindowImageId));
  if (!GetStyle(kUiGenericNoFrame))
    CreateFrame();
  LayoutChildren();
  i32 height = GetH(), width = GetW();
  SetClientEffects(width, height, &client);
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_Relayout);
  GetWindowManager()->SetCapture(nullptr);
  owner->SetFocus(this);
  if (!callback->DoWinMsg(this, kWnMsg_Init, 0, 0, 0))
    return false;
  Relayout();
  return true;
}

// Release initialized window resources in source order, letting detach callbacks replace or clear the live callback.
// The inactive path deliberately leaves callback ownership alone, as the original constructor/shutdown pairing does.
bool UiGenericWindow::Shutdown() {
  if (!GetFlag(kWnFlagInitialized))
    return true;
  HideWindow();
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_Relayout);
  ScRelease(&frame);
  ScRelease(&image);
  if (callback) {
    callback->DoWinMsg(this, kWnMsg_Shutdown, 0, 0, 0);
    ScRelease(&callback);
  }
  WnWindow *target = nullptr;
  if (UiGetApplication()) {
    SpCityWindowView *view = UiGetCityWindow();
    if (view)
      target = view->AsIGZWin();
  }
  WnWindowManager *manager = owner;
  bool result = UiBaseWindow::Shutdown();
  if (manager && target)
    manager->SetFocus(target);
  return result;
}

// Return the new owner's secondary interface at its initial zero reference count, as the actual class factory does.
UiGenericInterface *UiCreateGenericWindow() {
  return new UiGenericWindow;
}

// Store the root caption before forwarding the same input view to the current frame, if present.
void UiGenericWindow::SetCaption(const ScStringView &text) {
  UiBaseWindow::SetCaption(text);
  if (frame)
    frame->SetCaption(text);
}

// Construct and attach the real bordered frame, forwarding live styles before computing its client minimums.
// An existing scroller is brought forward and resized only after the frame's minimum dimensions are enforced.
bool UiGenericWindow::CreateFrame() {
  if (frame)
    return true;
  u32 background = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
  u32 border = UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
  frame = ScAddRefNotNull(new UiFrame);
  frame->SetID(kUiFrameWindowId);
  frame->GZWinMoveTo(border_x, border_y);
  frame->SetFlag(kWnFlagReorderable, false);
  const u32 style_flags[6] = {16, 64, 32, 256, 1024, 512};
  for (u32 flag : style_flags) {
    bool enabled = GetStyle(flag);
    SetStyle(flag, enabled);
  }
  frame->SetOutlineFlags(15);
  frame->SetSizeMode(5);
  frame->SetFillColor(background);
  frame->SetOutlineColor((u16)border);
  if (!ChildAdd(frame)) {
    frame->Release();
    frame = nullptr;
    return false;
  }
  SetCaption(*GetCaption());
  i32 bottom = (i32)((u32)GetH() - border_y), left = border_x;
  i32 right = (i32)((u32)GetW() - left);
  ScRect rectangle = {left, border_y, right, bottom};
  frame->AutoSize(rectangle);
  frame->GetMinimum(&minimum_width, &minimum_height);
  minimum_width = (i32)((u32)minimum_width + 2 * border_x);
  minimum_height = (i32)((u32)minimum_height + 2 * border_y);
  if (GetW() < minimum_width || GetH() < minimum_height)
    SetSize(minimum_width, minimum_height);
  WnWindow *scroller = GetChildWindowFromID(kUiGenericScrollerId);
  if (scroller) {
    scroller->SetFlag(kWnFlagReorderable, true);
    ChildToFront(scroller);
    scroller->SetFlag(kWnFlagReorderable, false);
    UiFrame *current = frame;
    i32 lower = (i32)((u32)GetH() - border_y), upper = (i32)((u32)current->GetB() - 1);
    scroller->SetArea(border_x, upper, border_x + 8, lower);
  }
  return true;
}

u8 UiSaveWindowPosition(WnWindow *window, i32 kind) {
  NwInternet *internet = UiGetApplication()->GetSC3Internet();
  NwInternetPrefs *preferences = internet ? internet->GetInternetPrefs() : nullptr;
  if (preferences && kind < 15) {
    NwWindowPosition *position = &preferences->windowPositions[kind];
    position->left = window->GetL();
    position->top = window->GetT();
  }
  return 1;
}

static bool UiLayoutChild(WnWindow *window, u32, void *view, void *context) {
  u32 borders = *static_cast<const u32 *>(context), x = borders >> 16, y = borders & 0xffff;
  i32 bottom = (i32)((u32)window->GetH() - y), right = (i32)((u32)window->GetW() - x);
  ScRect rectangle = {(i32)x, (i32)y, right, bottom};
  return static_cast<WnFlatControl *>(view)->AutoSize(rectangle) != 0;
}

i32 UiGenericWindow::LayoutChildren() {
  u32 borders = (u32)border_x << 16 | border_y;
  return EnumChildren(IID_WnFlatControl, UiLayoutChild, &borders) != 0;
}

// Apply minimum outer dimensions and let the current callback negotiate client limits before committing the rectangle.
// Resizing lays out children before storing and notifying the live client rectangle; a pure move skips those effects.
void UiGenericWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - left), height = (i32)((u32)bottom - top);
  if (width <= minimum_width)
    width = minimum_width;
  if (height <= minimum_height)
    height = minimum_height;
  if (width == GetW() && height == GetH()) {
    UiBaseWindow::SetArea(left, top, (i32)((u32)left + width), (i32)((u32)top + height));
    return;
  }
  if (callback) {
    ScRect rectangle, limits = {0, 0, 0xffff, 0xffff};
    SetClientEffects(width, height, &rectangle);
    if (callback->DoWinMsg(
            this, kWnMsg_ClientConstraints, reinterpret_cast<uptr>(&limits), reinterpret_cast<uptr>(&rectangle), 0)) {
      i32 client_width = (i32)((u32)rectangle.right - rectangle.left);
      if (client_width < limits.left)
        rectangle.right = (i32)((u32)rectangle.left + limits.left);
      else if (client_width > limits.right)
        rectangle.right = (i32)((u32)rectangle.left + limits.right);
      i32 client_height = (i32)((u32)rectangle.bottom - rectangle.top);
      if (client_height < limits.top)
        rectangle.bottom = (i32)((u32)rectangle.top + limits.top);
      else if (client_height > limits.bottom)
        rectangle.bottom = (i32)((u32)rectangle.top + limits.bottom);
      ScRect outer;
      SetOuterEffects(&rectangle, &outer);
      width = (i32)((u32)outer.right - outer.left);
      height = (i32)((u32)outer.bottom - outer.top);
    }
  }
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + width), (i32)((u32)top + height));
  LayoutChildren();
  SetClientEffects(width, height, &client);
  if (callback)
    callback->DoWinMsg(this, kWnMsg_ClientLayout, reinterpret_cast<uptr>(&client), 0, 0);
}

void UiGenericWindow::GZWinMoveTo(i32 x, i32 y) {
  SetArea(x, y, (i32)((u32)x + area.right - area.left), (i32)((u32)y + area.bottom - area.top));
}

// Retain the new image before releasing the old one, then store the pointer and its independent source index.
bool UiGenericWindow::SetImage(u32 index, GxBuffer *buffer) {
  if (buffer)
    buffer->AddRef();
  if (image)
    image->Release();
  image = buffer;
  resource_index = index;
  return true;
}

bool UiGenericWindow::GetClientArea(ScRect *out) {
  *out = client;
  return true;
}

// Handle the matching layout notice locally, then let the callback observe input before ordinary window dispatch.
// Mouse callback results are ignored; other handled callback messages stop before base processing.
bool UiGenericWindow::DoMessage(ScMessage *message) {
  ScMsgCode type = message->type;
  if (type == kWnMsg_Relayout) {
    if (message->data1 != identifier)
      return false;
    Relayout();
    return true;
  }
  if (callback) {
    bool handled = callback->DoWinMsg(this, u32(type), message->data1, message->data2, message->data3);
    if ((type < kWnMsg_MouseDownL || type > kWnMsg_MouseWheel) && handled)
      return true;
  }
  return UiBaseWindow::DoMessage(message);
}

// Queue modal close through the window manager or post deletion to the permitted live parent for nonmodal windows.
bool UiGenericWindow::RequestClose() {
  if (GetFlag(kWnFlagModal)) {
    WnWindowManager *manager = GetWindowManager();
    WnWindow *window = GetWindow();
    manager->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), kUiFrameCloseButtonId);
  } else {
    WnWindow *window = GetWindow(), *target = GetParentWin();
    PostMsg(target, kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  }
  return true;
}

// Recognize only the close button's activation notification and retain the original successful close-request result.
bool UiGenericWindow::OnCommand(u32 kind, u32 command) {
  if (kind != kWnCommandButtonActivated || command != kUiFrameCloseButtonId)
    return false;
  RequestClose();
  return true;
}

bool UiGenericWindow::OnKeyDown(u32 key, u32 modifiers) {
  if ((style & kUiGenericCloseOnEscapeAltF4) &&
      (key == kWnKeyEscape || (key == kWnKeyF4 && (modifiers & kWnModifierAlt)))) {
    RequestClose();
    return true;
  }
  return GetFlag(kWnFlagModal) ? false : SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiGenericWindow::OnKeyUp(u32 key, u32 modifiers) {
  return GetFlag(kWnFlagModal) ? false : SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Identify the client, resize edges and optional title frame using the original inclusive signed bounds.
i32 UiGenericWindow::HitTest(i32 x, i32 y) {
  if (x >= client.left && y >= client.top && x <= client.right && y <= client.bottom)
    return 0;
  i32 mode = 0;
  if (style & kUiGenericResizable) {
    if (x < border_x)
      mode = y<border_y ? 6 : y>(i32)((u32)GetH() - border_y) ? 8 : 2;
    else if (x > (i32)((u32)GetW() - border_x))
      mode = y<border_y ? 7 : y>(i32)((u32)GetH() - border_y) ? 9 : 3;
    else if (y < border_y)
      mode = 4;
    else if (y > (i32)((u32)GetH() - border_y))
      mode = 5;
  }
  if ((style & kUiGenericDraggable) && frame) {
    const ScRect *rectangle = frame->GetArea();
    if (x >= rectangle->left && y >= rectangle->top && x <= rectangle->right && y <= rectangle->bottom)
      return 1;
  }
  return mode;
}

// Start a resize or title drag only after capture succeeds; remember the coordinates after that imported call returns.
bool UiGenericWindow::OnMouseDownL(i32 x, i32 y, u32) {
  word172 = HitTest(x, y);
  if (word172) {
    if (owner->SetCapture(this)) {
      mouse_down_x = x;
      mouse_down_y = y;
    } else
      word172 = 0;
  }
  return true;
}

// Clear the active drag mode before releasing capture; a release without an active drag stays unhandled.
bool UiGenericWindow::OnMouseUpL(i32, i32, u32) {
  if (!word172)
    return false;
  word172 = 0;
  owner->ReleaseCapture(this);
  return true;
}

// Apply the active drag's ordered movement and dimension updates, retaining the source's coordinate accumulation rules.
// Without capture, update only the hover mode used by the window's cursor behavior.
bool UiGenericWindow::OnMouseMove(i32 x, i32 y, u32) {
  u32 mode = word172;
  if (!mode) {
    u32 value = (u32)HitTest(x, y);
    if (word176 != value)
      word176 = value;
    return true;
  }
  i32 dx = x - mouse_down_x, dy = y - mouse_down_y;
  switch (mode) {
  case 1:
    GZWinOffset(dx, dy);
    break;
  case 2:
    GZWinOffset(dx, 0);
    SetW((i32)((u32)GetW() - dx));
    break;
  case 3:
    SetW((i32)((u32)GetW() + dx));
    mouse_down_x += dx;
    break;
  case 4:
    GZWinOffset(0, dy);
    SetH((i32)((u32)GetH() - dy));
    break;
  case 5:
    SetH((i32)((u32)GetH() + dy));
    mouse_down_y += dy;
    break;
  case 6: {
    GZWinOffset(dx, dy);
    i32 height = (i32)((u32)GetH() - dy), width = (i32)((u32)GetW() - dx);
    SetSize(width, height);
    break;
  }
  case 7: {
    GZWinOffset(0, dy);
    i32 height = (i32)((u32)GetH() - dy), width = (i32)((u32)GetW() + dx);
    SetSize(width, height);
    mouse_down_x += dx;
    break;
  }
  case 8: {
    GZWinOffset(dx, 0);
    i32 height = (i32)((u32)GetH() + dy), width = (i32)((u32)GetW() - dx);
    SetSize(width, height);
    mouse_down_y += dy;
    break;
  }
  case 9: {
    i32 height = (i32)((u32)GetH() + dy), width = (i32)((u32)GetW() + dx);
    SetSize(width, height);
    mouse_down_x += dx;
    mouse_down_y += dy;
    break;
  }
  }
  return true;
}

// Let the callback supply the Boolean hit-test output before falling back to the root.
bool UiGenericWindow::IsPointInWindow(i32 x, i32 y) const {
  if (callback) {
    bool result = false;
    u32 point = (u32)(i32)(i16)x << 16 | (u32)(i32)(i16)y;
    if (callback->DoWinMsg(
            const_cast<UiGenericWindow *>(this), kWnMsg_HitTest, point, reinterpret_cast<uptr>(&result), 0))
      return result;
  }
  return UiBaseWindow::IsPointInWindow(x, y);
}

bool UiGenericWindow::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  ScreenToWindowCoordinates(&x, &y);
  return IsPointInWindow(x, y);
}

// Paint the selected image, solid fill or bevel before optional highlighted left/top and dark right/bottom edges.
// Primitive returns are discarded; only the source's explicit image, fill and border lock failures return false.
bool UiGenericWindow::GZPaint() {
  if (style & kUiGenericNoPainting)
    return true;
  if (image) {
    if (!(style & kUiGenericNoBackground)) {
      if (!draw_buffer->Lock(GxLockFlags::Blt))
        return false;
      if (resource_index == 0)
        draw_buffer->BltTiled(image, *(image->GetBufferArea()), draw_area, 0, 0, nullptr);
      else if (resource_index == 1)
        draw_buffer->Blt(image, *(image->GetBufferArea()), draw_area, nullptr);
      else if (resource_index == 2) {
        u32 height = image->Height() / 3, width = image->Width() / 3;
        ScRect corner = {0, 0, (i32)width, (i32)height};
        draw_buffer->BltEdge(image, corner, draw_area, static_cast<u8>(1));
      }
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
    if (style & kUiGenericNoBevel)
      return true;
  } else if (style & kUiGenericNoBevel) {
    if (!(style & kUiGenericNoBackground)) {
      if (!draw_buffer->Lock(GxLockFlags::Fill))
        return false;
      u32 color = GetFillColor();
      draw_buffer->Fill(color, draw_area, nullptr);
      draw_buffer->Unlock(GxLockFlags::Fill);
    }
    return true;
  } else if (!(style & kUiGenericNoBackground)) {
    u32 color = GetFillColor();
    draw_buffer->DrawRaisedRect(draw_area, color, true, true);
    return true;
  }
  i32 left = draw_area.left, top = draw_area.top, right = (i32)((u32)draw_area.right - 1),
      bottom = (i32)((u32)draw_area.bottom - 1);
  if (!draw_buffer->Lock(GxLockFlags::Line))
    return false;
  draw_buffer->DrawLine(left, top, left, bottom, static_cast<i32>(0xffff), 1);
  draw_buffer->DrawLine(left, top, right, top, static_cast<i32>(0xffff), 1);
  draw_buffer->DrawLine(right, top, right, bottom, static_cast<i32>(0), 1);
  draw_buffer->DrawLine(left, bottom, right, bottom, static_cast<i32>(0), 1);
  draw_buffer->Unlock(GxLockFlags::Line);
  return true;
}

u8 UiGenericWindow::SetGutters(i8 left, i8 top, i8, i8) {
  return static_cast<UiGenericBase *>(this)->SetGutters(left, top, left, top);
}

// Publish the two live byte gutters twice through four signed DWORD outputs, retaining store/read ordering.
u8 UiGenericWindow::GetGutters(i32 *left, i32 *top, i32 *right, i32 *bottom) const {
  *left = border_x;
  *top = border_y;
  *right = border_x;
  *bottom = border_y;
  return 1;
}

// Publish each stored BYTE border dimension through its original separate DWORD output, in source order.
bool UiGenericWindow::GetBorder(u32 *horizontal, u32 *vertical) {
  *horizontal = border_x;
  *vertical = border_y;
  return true;
}

// Create and configure the registered flat rectangle before attaching its primary window to this generic owner.
// Failed attachment destroys the primary through the live manager before releasing the temporary secondary interface.
bool UiGenericWindow::AddPanel(u32 panel_id, u32 layout, u32 border, const ScRect *rectangle) {
  if (ChildExists(panel_id))
    return false;
  WnFlatControl *panel = nullptr;
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
    return false;
  WnWindow *window = panel->GetWindow();
  window->SetID(panel_id);
  window->SetArea(rectangle->left, rectangle->top, rectangle->right, rectangle->bottom);
  WnResources *resources = UiGetWindowResources();
  u32 background = resources->GetColor(kWnThemePanelFillColorId);
  u16 edge = (u16)resources->GetColor(kWnThemeBorderColorId);
  window->SetFlag(kWnFlagReorderable, false);
  window->SetFillColor(background);
  panel->SetOutlineColor(edge);
  panel->SetSizeMode((u8)layout);
  panel->SetOutlineFlags((u8)border);
  bool result = ChildAdd(window);
  if (!result)
    GetWindowManager()->DestroyWindow(window);
  panel->Release();
  return result;
}

// Place an eight-pixel horizontal divider within the current borders, leaving room for the active scroller style.
bool UiGenericWindow::AddDivider(u32 panel_id, i32 top) {
  i32 left = style & kUiGenericScroller ? border_x + 7 : border_x - 1;
  i32 right = (i32)((u32)GetW() - border_x);
  ScRect rectangle = {left, top, right, (i32)((u32)top + 8)};
  return AddPanel(panel_id, 3, 14, &rectangle);
}

// Find and delete the named child through the original live owner method; a missing child returns false.
bool UiGenericWindow::RemovePanel(u32 panel_id) {
  WnWindow *window = GetChildWindowFromID(panel_id);
  if (!window)
    return false;
  ChildDelete(window);
  return true;
}

// Add the narrow left scroller panel below the frame, then send it and the frame behind ordinary child content.
bool UiGenericWindow::AddScroller() {
  if (ChildExists(kUiGenericScrollerId))
    return true;
  i32 top = frame ? (i32)((u32)frame->GetB() - 1) : border_y;
  i32 bottom = (i32)((u32)GetH() - border_y), left = border_x;
  ScRect rectangle = {left, top, left + 8, bottom};
  if (!AddPanel(kUiGenericScrollerId, 4, 13, &rectangle))
    return false;
  WnWindow *scroller = GetChildWindowFromID(kUiGenericScrollerId);
  if (scroller) {
    scroller->SetFlag(kWnFlagReorderable, true);
    ChildToBack(scroller);
    scroller->SetFlag(kWnFlagReorderable, false);
    if (frame) {
      frame->SetFlag(kWnFlagReorderable, true);
      ChildToBack(frame);
      frame->SetFlag(kWnFlagReorderable, false);
    }
  }
  return true;
}

// Delete the optional scroller by its borrowed primary view; absence is also a successful removal.
bool UiGenericWindow::RemoveScroller() {
  WnWindow *window = GetChildWindowFromID(kUiGenericScrollerId);
  if (window)
    ChildDelete(window);
  return true;
}

// Adjust a native window's independently typed message-target view without an extra game query or reference effect.
static ScMessageTarget *UiWindowTarget(WnWindow *window) {
  return dynamic_cast<ScMessageTarget *>(window);
}

UiMinMaxProcedure::UiMinMaxProcedure() : references(0) {
}

UiMinMaxProcedure::~UiMinMaxProcedure() {
}

// Publish the supported callback identity before the live retain, clearing the output on rejection.
bool UiMinMaxProcedure::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiMinMaxProcedure::AddRef() {
  return ++references;
}

// Destroy through the real virtual destructor when the decremented count is zero or negative as a signed DWORD.
u32 UiMinMaxProcedure::Release() {
  u32 value = --references;
  if ((i32)value > 0)
    return value;
  delete this;
  return 0;
}

// Configure the factory-created generic window, publish its primary identity and release the temporary secondary view.
u8 UiMinMaxProcedure::CreateMinMaxWindow(WnWindow **out) {
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return false;
  window->SetStyle(kUiGenericDraggable, false);
  window->SetStyle(kUiGenericResizable, false);
  window->SetStyle(kUiGenericNoFrame, true);
  window->SetStyle(kUiGenericNoPainting, true);
  window->SetBorder(0, 0);
  window->SetCallback(this);
  bool result = window->QueryInterface(IID_WnWindow, reinterpret_cast<void **>(out));
  window->Release();
  return result;
}

// Route generic-window events to the actual control, synchronous size outputs and message server.
// Pointer-valued event words retain native width; hit testing writes only its genuine Boolean output.
bool UiMinMaxProcedure::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (!window)
    return false;
  UiMinMaxButtonInterface *control = nullptr;
  switch (message) {
  case kWnMsg_Command:
    if (first != 1 || second != kUiMinMaxToggleId)
      return false;
    if (window->GetChildAs(kUiMinMaxToggleId, IID_UiMinMaxButtonInterface, reinterpret_cast<void **>(&control))) {
      ScMessage notice = {kWnMsg_HideUiPanels + (control->GetMinMaxMode() != 0), 0, 0, 0};
      if (UiGetMessageServer())
        UiGetMessageServer()->MessagePost(notice, false);
      control->Release();
    }
    return true;
  case kWnMsg_KeyDown:
    return true;
  case kWnMsg_HideUiPanels:
    return OnUiMaximize(window, 0);
  case kWnMsg_ShowUiPanels:
    return OnUiMaximize(window, 1);
  case kWnMsg_Hide:
    window->HideWindow();
    return true;
  case kWnMsg_Show:
    window->ShowWindow();
    return true;
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  case kWnMsg_ClientConstraints: {
    ScRect *rectangle = reinterpret_cast<ScRect *>(first);
    if (!rectangle)
      return false;
    WnWindow *child = window->GetChildWindowFromID(kUiMinMaxToggleId);
    if (!child)
      return false;
    rectangle->left = child->GetW();
    i32 height = child->GetH(), width = rectangle->left;
    rectangle->top = height;
    rectangle->right = width;
    rectangle->bottom = height;
    return true;
  }
  case kWnMsg_ClientLayout:
    if (first) {
      WnWindow *child = window->GetChildWindowFromID(kUiMinMaxToggleId);
      if (child) {
        const ScRect *rectangle = reinterpret_cast<const ScRect *>(first);
        child->GZWinMoveTo(rectangle->left, rectangle->top);
      }
    }
    return true;
  case kWnMsg_HitTest:
    if (!second)
      return false;
    return OnPointInWindow(window, (i16)((u32)first >> 16), (i16)first, reinterpret_cast<bool *>(second));
  default:
    return false;
  }
}

// Subscribe the real window target to the four controller notices before creating and attaching its toggle control.
// Move the window even when control creation fails; only attachment supplies the successful byte result.
u8 UiMinMaxProcedure::OnInit(WnWindow *window) {
  if (!window)
    return 0;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 kMessages[4] = {kWnMsg_HideUiPanels, kWnMsg_ShowUiPanels, kWnMsg_Hide, kWnMsg_Show};
    for (u32 type : kMessages)
      server->AddNotification(UiWindowTarget(window), type);
  }
  u8 result = 0;
  UiMinMaxButtonInterface *control = nullptr;
  if (UiGetCom()->GetClassObject(
          kUiMinMaxButtonClassId, IID_UiMinMaxButtonInterface, reinterpret_cast<void **>(&control))) {
    control->AsIGZWin()->SetID(kUiMinMaxToggleId);
    result = window->ChildAdd(control->AsIGZWin());
    control->Release();
  }
  window->GZWinMoveTo(16, 16);
  return result;
}

// Remove the four subscriptions through the captured server, preserving the original reversed first pair.
// Shutdown succeeds even when the message service is unavailable and adds no control/window ownership operation.
u8 UiMinMaxProcedure::OnShutdown(WnWindow *window) {
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 kMessages[4] = {kWnMsg_ShowUiPanels, kWnMsg_HideUiPanels, kWnMsg_Hide, kWnMsg_Show};
    for (u32 type : kMessages)
      server->RemoveNotification(UiWindowTarget(window), type);
  }
  return 1;
}

// Query the existing toggle and select its opposite mode from the original zero/nonzero maximize byte.
// A missing window fails; a refused control query still consumes the notice and leaves its published output alone.
u8 UiMinMaxProcedure::OnUiMaximize(WnWindow *window, u8 maximized) {
  if (!window)
    return 0;
  UiMinMaxButtonInterface *control = nullptr;
  if (window->GetChildAs(kUiMinMaxToggleId, IID_UiMinMaxButtonInterface, reinterpret_cast<void **>(&control))) {
    control->SetMinMaxMode(static_cast<UiMinMaxButtonInterface::UiMode>(maximized == 0));
    control->Release();
  }
  return 1;
}

// Convert the separate signed window coordinates to screen coordinates only after acquiring the actual child window.
// Publish the genuine Boolean hit result before releasing that child; query refusal preserves the output.
u8 UiMinMaxProcedure::OnPointInWindow(WnWindow *window, i32 x, i32 y, bool *out) {
  WnWindow *child = nullptr;
  if (!window->GetChildAs(kUiMinMaxToggleId, IID_WnWindow, reinterpret_cast<void **>(&child)))
    return 0;
  window->WindowToScreenCoordinates(&x, &y);
  *out = child->IsPointInWindowScreenCoordinates(x, y);
  child->Release();
  return 1;
}

static u32 UiPackCoordinates(i32 high, i32 low) {
  return ((u32)(i32)(i16)high << 16) | (u32)(i32)(i16)low;
}

UiMainManagerObject::UiMainManagerObject()
    : references(0), active(false), client{}, owner(nullptr), first_visible(false), second_visible(false),
      dialog_visible(false), dialog(nullptr) {
  children = new UiChild;
  children->next = children;
  children->previous = children;
}

// Clear an active controller before retiring its list storage. Inactive destruction adds no window or service cleanup.
UiMainManagerObject::~UiMainManagerObject() {
  if (active)
    Shutdown();
  UiChild *node = children->next;
  while (node != children) {
    UiChild *next = node->next;
    delete node;
    node = next;
  }
  delete children;
}

// Publish the canonical message-target view before the live retain, clearing the output.
bool UiMainManagerObject::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_ScMessageTarget && iid != IID_UiMainManagerObject))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScMessageTarget *>(this);
  AddRef();
  return true;
}

u32 UiMainManagerObject::AddRef() {
  return ++references;
}

// Delete through the real virtual destructor on a zero or signed-negative decremented count.
u32 UiMainManagerObject::Release() {
  u32 value = --references;
  if ((i32)value > 0)
    return value;
  delete this;
  return 0;
}

// Append the actual child identifier at the tail of the game-owned circular list.
void UiMainManagerObject::AppendChild(u32 identifier) {
  UiChild *node = new UiChild;
  node->identifier = identifier;
  node->next = children;
  node->previous = children->previous;
  node->previous->next = node;
  children->previous = node;
}

// Attach the four registered panels and auxiliary control before subscribing and laying out the controller's windows.
// Preserve the original repeated getters and factory output locals; unsuccessful attachments add no child identifier.
bool UiMainManagerObject::Init(WnWindow *window) {
  if (active)
    return true;
  if (!UiGetWindowManager() || !UiGetWindowManager()->IsWindowValid(window))
    return false;
  owner = window;
  ScMessageServer *server = UiGetMessageServer();
  if (!server)
    return false;
  CtCity *city = UiGetCity();
  if (city)
    city->SimHiddenPause();
  WnWindow *view = nullptr;
  if (UiGetCom()->GetClassObject(kUiDateCashWindowClassId, IID_WnWindow, reinterpret_cast<void **>(&view))) {
    view->SetID(kUiMainDateCashPanelId);
    view->HideWindow();
    if (window->ChildAdd(view)) {
      AppendChild(view->GetID());
      view->SetFlag(kWnFlagReorderable, false);
      ScMessage message = {kCtMsg_MonthChanged, 0, 0, 0};
      view->DoMessage(&message);
    }
    view->Release();
  }
  view = nullptr;
  if (UiGetCom()->GetClassObject(kUiRciWindowClassId, IID_WnWindow, reinterpret_cast<void **>(&view))) {
    view->SetID(kUiMainRciPanelId);
    view->HideWindow();
    if (window->ChildAdd(view)) {
      AppendChild(view->GetID());
      view->SetFlag(kWnFlagReorderable, false);
    }
    view->Release();
  }
  view = nullptr;
  UiPanelInterface *panel = nullptr;
  if (UiGetCom()->GetClassObject(
          kUiGameMenuPanelClassId, IID_UiGameMenuPanelInterface, reinterpret_cast<void **>(&panel))) {
    view = panel->GetWindow();
    view->SetID(kUiMainMenuPanelId);
    view->HideWindow();
    if (window->ChildAdd(view)) {
      AppendChild(view->GetID());
      view->SetFlag(kWnFlagReorderable, false);
      panel->SetViewEffects(kUiInitialGameMenuView);
    }
    panel->Release();
  }
  panel = nullptr;
  view = nullptr;
  UiPanelInterface *last = nullptr;
  if (UiGetCom()->GetClassObject(kUiNavigationClassId, IID_UiNavigationInterface, reinterpret_cast<void **>(&last))) {
    view = last->GetWindow();
    view->SetID(kUiMainNavigationPanelId);
    view->HideWindow();
    if (window->ChildAdd(view)) {
      AppendChild(view->GetID());
      view->SetFlag(kWnFlagReorderable, false);
    }
    last->Release();
  }
  last = nullptr;
  view = nullptr;
  UiMinMaxProcedure *auxiliary = new UiMinMaxProcedure;
  auxiliary->AddRef();
  if (auxiliary->CreateMinMaxWindow(&view)) {
    WnWindow *parent = view->GetMainWindow();
    parent->ChildAdd(view);
    view->SetSize(0, 0);
    i32 height = view->GetH(), bottom = parent->GetB();
    i32 width = view->GetW(), right = parent->GetR();
    view->GZWinMoveTo((i32)((u32)right - (u32)width), (i32)((u32)bottom - (u32)height));
    view->Release();
  }
  view = nullptr;
  auxiliary->Release();
  const u32 kSubscriptions[14] = {kApMsgPreCityChange, kApMsgCityReady, kApMsgPostCityChange, kWnMsg_ShowCityTip,
      kWnMsg_QueryUiVisibility, kWnMsg_QueryUiClientArea, kWnMsg_EnterTerrainEditor, kWnMsg_EnterCity,
      kWnMsg_HideUiPanels, kWnMsg_ShowUiPanels, kWnMsg_Hide, kWnMsg_Show, kWnMsg_Unresolved35,
      kWnMsg_QueryCityMapControl};
  for (u32 message : kSubscriptions)
    server->AddNotification(this, message);
  Layout();
  ScMessage message = {kWnMsg_Hide, 0, 0, 0};
  server->MessageSend(message);
  view = owner->GetChildWindowFromID(kUiMainCityViewportId);
  if (view && view->GetWindowManager())
    view->GetWindowManager()->SetFocus(view);
  if (city)
    city->SimHiddenUnpause();
  active = true;
  return true;
}

// Remove tracked children from a valid owner, then unsubscribe, release the dialog and reset only active ownership state.
bool UiMainManagerObject::Shutdown() {
  WnWindowManager *manager = UiGetWindowManager();
  if (manager && manager->IsWindowValid(owner)) {
    while (children->next != children) {
      UiChild *node = children->next;
      owner->ChildDelete(owner->GetChildWindowFromID(node->identifier));
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
    }
  }
  UiChild *node = children->next;
  while (node != children) {
    UiChild *next = node->next;
    delete node;
    node = next;
  }
  children->next = children;
  children->previous = children;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 kSubscriptions[14] = {kWnMsg_ShowCityTip, kApMsgPostCityChange, kApMsgPreCityChange, kApMsgCityReady,
        kWnMsg_EnterTerrainEditor, kWnMsg_EnterCity, kWnMsg_QueryUiVisibility, kWnMsg_QueryUiClientArea,
        kWnMsg_HideUiPanels, kWnMsg_ShowUiPanels, kWnMsg_Hide, kWnMsg_Show, kWnMsg_Unresolved35,
        kWnMsg_QueryCityMapControl};
    for (u32 message : kSubscriptions)
      server->RemoveNotification(this, message);
  }
  ScRelease(&dialog);
  owner = nullptr;
  active = false;
  return true;
}

// Place the two corner panels before stretching the bottom and side panels, retaining the original 480/600 rules.
bool UiMainManagerObject::Layout() {
  WnWindowManager *manager = UiGetWindowManager();
  if (!manager || !manager->IsWindowValid(owner))
    return false;
  ScRect rectangle = *owner->GetArea();
  i32 edge = rectangle.right, lower = rectangle.bottom;
  WnWindow *child = owner->GetChildWindowFromID(kUiMainNavigationPanelId);
  if (child) {
    i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top) == 480 ? 152 : child->GetH();
    child->GZWinMoveTo((i32)((u32)rectangle.right - (u32)child->GetW()), (i32)((u32)rectangle.bottom - (u32)height));
    edge = child->GetL();
    lower = child->GetT();
  }
  child = owner->GetChildWindowFromID(kUiMainRciPanelId);
  if (child) {
    i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top) == 600 ? 80 : child->GetH();
    child->GZWinMoveTo((i32)((u32)edge - (u32)child->GetW()), (i32)((u32)rectangle.bottom - (u32)height));
    edge = child->GetL();
  } else
    edge = rectangle.right;
  child = owner->GetChildWindowFromID(kUiMainDateCashPanelId);
  if (child)
    child->SetArea(rectangle.left, (i32)((u32)rectangle.bottom - (u32)child->GetH()), edge, rectangle.bottom);
  child = owner->GetChildWindowFromID(kUiMainMenuPanelId);
  if (child)
    child->SetArea(
        (i32)((u32)rectangle.right - (u32)child->GetW()), rectangle.top, rectangle.right, (i32)((u32)lower + 6));
  return true;
}

// Start from the owner's live rectangle, then reduce the right and bottom extents for visible side and bottom panels.
void UiMainManagerObject::UpdateExtentsEffects() {
  WnWindowManager *manager = UiGetWindowManager();
  if (!manager || !manager->IsWindowValid(owner))
    return;
  owner->GetAreaAbsolute(&client);
  ScRect rectangle;
  WnWindow *child = owner->GetChildWindowFromID(kUiMainMenuPanelId);
  if (child && child->GetFlag(kWnFlagVisible)) {
    child->GetAreaAbsolute(&rectangle);
    client.right = rectangle.left;
  }
  child = owner->GetChildWindowFromID(kUiMainDateCashPanelId);
  if (child && child->GetFlag(kWnFlagVisible)) {
    child->GetAreaAbsolute(&rectangle);
    client.bottom = rectangle.top;
  }
}

// Change tracked child visibility before refreshing extents and posting the packed rectangle to the live message server.
void UiMainManagerObject::SetVisibility(u32 enabled) {
  if (!owner)
    return;
  for (UiChild *node = children->next; node != children; node = node->next) {
    WnWindow *child = owner->GetChildWindowFromID(node->identifier);
    if (child)
      child->SetFlag(kWnFlagVisible, enabled != 0);
  }
  UpdateExtentsEffects();
  ScMessage message = {kWnMsg_CityViewportChanged, UiPackCoordinates(client.left, client.top),
      UiPackCoordinates(client.right, client.bottom), 0};
  if (UiGetMessageServer())
    UiGetMessageServer()->MessagePost(message, false);
}

// Handle the controller's visibility, dialog and synchronous query messages using its actual fields and live outputs.
// The terrain callback resets at44 before creating its window through the real output argument at16.
bool UiMainManagerObject::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kApMsgCityReady:
  case kWnMsg_Unresolved35:
  case kApMsgPostCityChange:
    return true;
  case kWnMsg_HideUiPanels:
  case kWnMsg_ShowUiPanels:
  case kWnMsg_Hide:
  case kWnMsg_Show: {
    u32 enabled = message->type & 1;
    SetVisibility(enabled);
    if (message->type < kWnMsg_Hide)
      first_visible = enabled != 0;
    else
      second_visible = enabled != 0;
    return true;
  }
  case kApMsgPreCityChange: {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      saved_visibility = second_visible;
      ScMessage notice = {kWnMsg_Hide, 0, 0, 0};
      server->MessageSend(notice);
    }
    return true;
  }
  case kWnMsg_ShowCityTip: {
    ScCommandFactory *factory = nullptr;
    UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
    UiCommand *command = reinterpret_cast<UiCommand *>(message);
    if (factory->GetCommand(kUiTipCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      command->Execute();
      command->Release();
    }
    if (factory)
      factory->Release();
    return true;
  }
  case kWnMsg_EnterTerrainEditor: {
    if (dialog_visible)
      return true;
    ScMessageServer *server = UiGetMessageServer();
    if (!server)
      return true;
    ScMessage notice = {kWnMsg_Hide, 0, 0, 0};
    server->MessageSend(notice);
    if (!dialog &&
        !UiGetCom()->GetClassObject(
            kUiTerrainEditorClassId, IID_UiTerrainEditorInterface, reinterpret_cast<void **>(&dialog)))
      return false;
    dialog->ResetToDefault();
    WnWindow *view = nullptr;
    if (dialog->CreateTerrEditWin(&view)) {
      dialog_visible = view->GetMainWindow()->ChildAdd(view);
      view->Release();
    }
    return true;
  }
  case kWnMsg_EnterCity: {
    WnWindow *view = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiTerrainEditorWindowId);
    if (view)
      UiGetWindowManager()->DestroyWindow(view);
    ScMessage notice = {kWnMsg_Show, 0, 0, 0};
    UiGetMessageServer()->MessagePost(notice, false);
    dialog_visible = false;
    second_visible = true;
    notice.type = kWnMsg_ShowCityTip;
    UiGetMessageServer()->MessagePost(notice, false);
    WnWindow *child = owner->GetChildWindowFromID(kUiMainNavigationPanelId);
    if (child) {
      WnWindow *selection = nullptr;
      if (child->GetChildAs(kUiNavigationMapControlId, IID_ScUnknown, reinterpret_cast<void **>(&selection))) {
        selection->ChildToFront((u32)0);
        selection->Release();
        child->InvalidateSelf();
      }
    }
    return true;
  }
  case kWnMsg_QueryUiVisibility:
    message->data1 = 0;
    if (first_visible)
      message->data1 = 1;
    if (second_visible)
      message->data1 |= 2;
    if (dialog_visible)
      message->data1 |= 4;
    return true;
  case kWnMsg_QueryUiClientArea:
    message->data1 = UiPackCoordinates(client.left, client.top);
    message->data2 = UiPackCoordinates(client.right, client.bottom);
    return true;
  case kWnMsg_QueryCityMapControl: {
    message->data1 = 0;
    WnWindow *child = owner->GetChildWindowFromID(kUiMainNavigationPanelId), *selection = nullptr;
    if (!child || !child->GetChildAs(kUiNavigationMapControlId, IID_ScUnknown, reinterpret_cast<void **>(&selection)))
      return false;
    message->data1 = reinterpret_cast<uptr>(selection);
    selection->Release();
    return true;
  }
  default:
    return false;
  }
}

// Construct the real modal controller with no external reference and return its primary message-target unknown view.
ScUnknown *UiCreateClass43164FB1() {
  return new UiMainManagerObject;
}

UiProgressDialog *UiProgressDialog::current;

UiProgressDialog::UiProgressDialog()
    : window(nullptr), range_start(0), range_end(100), value(0), caption(nullptr), flipping(false), references(0) {
  current = this;
}

// Retire the retained generic view before deleting the copied caption; clear publication after both callbacks finish.
UiProgressDialog::~UiProgressDialog() {
  ScRelease(&window);
  ScDelete(&caption);
  current = nullptr;
}

// Only unknown1 is supported; rejected identities explicitly clear the caller's output.
bool UiProgressDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the owner before the module retain, then return the field again after the callback.
u32 UiProgressDialog::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// A nonzero count removes the module reference before its live decrement; exactzero destroys the callback owner.
u32 UiProgressDialog::Release() {
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

// Route the generic attach/detach notices; consume input packets4 through12 and reject other notices.
bool UiProgressDialog::DoWinMsg(WnWindow *recipient, u32 message, uptr, uptr, uptr) {
  if (message == kWnMsg_Init)
    return Attach(recipient);
  if (message == kWnMsg_Shutdown)
    return Detach(recipient);
  return message >= kWnMsg_Character && message <= kWnMsg_MouseWheel;
}

// Publish a new progress callback before acquiring its generic window and setting the original modeless styles.
bool UiProgressDialog::Open() {
  if (current)
    return false;
  new UiProgressDialog;
  if (!current)
    return false;
  current->AddRef();
  current->window = nullptr;
  ScCom *com = UiGetCom();
  bool result = com->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&current->window));
  if (result && current->window) {
    current->window->SetStyle(kUiGenericDraggable, 0);
    current->window->SetStyle(kUiGenericResizable, 0);
    current->window->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    current->window->SetStyle(kUiGenericCloseButton, 0);
    current->window->SetStyle(kUiGenericBackButton, 0);
    current->window->SetStyle(kUiGenericSecondButton, 0);
    current->window->SetStyle(kUiGenericScroller, 1);
    current->window->GetWindow()->PrivateBuffer(true);
    current->window->SetCallback(current);
    WnWindow *parent = UiGetWindowManager()->GetMainWindow();
    return parent->ChildAdd(current->window->GetWindow());
  }
  return result;
}

// Clear the callback before posting close notice1 to the captured primary's parent through its manager.
// Release the live published owner only after that dispatch, matching possible callback changes to publication.
bool UiProgressDialog::Close() {
  if (!current || !current->window)
    return false;
  WnWindow *primary = current->window->GetWindow();
  if (primary) {
    current->window->SetCallback(nullptr);
    WnWindowManager *window_manager = primary->GetWindowManager();
    WnWindow *parent = primary->GetParentWin();
    window_manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(primary), 0, 0);
  }
  current->Release();
  return true;
}

// Store both independent range endpoints, update the two bar panels, then invalidate, draw windows and draw graphics.
bool UiProgressDialog::SetRange(u32 first, u32 last) {
  if (!current)
    return false;
  current->range_start = first;
  current->range_end = last;
  current->UpdateBar();
  current->window->GetWindow()->InvalidateSelf();
  UiGetWindowManager()->Plot();
  UiGetGraphics()->Draw();
  return true;
}

// Delete the previous owned caption before allocating its replacement, then update the live primary and redraw it.
bool UiProgressDialog::SetCaption(ScStringView *source_text) {
  if (!current)
    return false;
  delete current->caption;
  current->caption = new ScString(*source_text);
  if (current->window) {
    current->window->GetWindow()->SetCaption(*current->caption);
    current->window->GetWindow()->InvalidateSelf();
    UiGetWindowManager()->Plot();
    UiGetGraphics()->Draw();
  }
  return true;
}

// Leave an unchanged value quiet; changed values update the panels and request the same three-stage redraw as range changes.
bool UiProgressDialog::SetValue(u32 next) {
  if (!current)
    return false;
  if (next != current->value) {
    current->value = next;
    current->UpdateBar();
    current->window->GetWindow()->InvalidateSelf();
    UiGetWindowManager()->Plot();
    UiGetGraphics()->Draw();
  }
  return true;
}

// Undo only this callback's flipping adjustment, clear capture and focus, then flush queued input.
bool UiProgressDialog::Detach(WnWindow *) {
  if (!window)
    return false;
  if (flipping) {
    UiGetGraphics()->ScreenBuffer()->DisableFlipping(-1);
    flipping = false;
  }
  UiGetWindowManager()->SetCapture(nullptr);
  UiGetWindowManager()->SetFocus(nullptr);
  UiGetWindowManager()->FlushInputMessageQueue();
  return true;
}

// Center the current primary within its main window using local areas and signed truncating division after DWORD wrapping.
void UiProgressDialog::Center() {
  ScRect parent, own;
  window->GetWindow()->GetMainWindow()->GetArea(&parent);
  window->GetWindow()->GetArea(&own);
  i32 x = (i32)((u32)own.left + (u32)parent.right - (u32)own.right - (u32)parent.left) / 2;
  i32 y = (i32)((u32)parent.bottom + (u32)own.top - (u32)parent.top - (u32)own.bottom) / 2;
  window->GetWindow()->GZWinMoveTo(x, y);
}

// Resolve both live bar children before computing the unsigned progress width; update the filled panel before the remainder.
void UiProgressDialog::UpdateBar() {
  if (!window)
    return;
  WnWindow *filled = window->GetWindow()->GetChildWindowFromID(2);
  WnWindow *empty = window->GetWindow()->GetChildWindowFromID(3);
  u32 right;
  if (range_end == range_start || value < range_start)
    right = 31;
  else if (value <= range_end)
    right = (205u * value - 205u * range_start) / (range_end - range_start) + 31;
  else
    right = 236;
  ScRect rectangle = {31, 68, (i32)right, 84};
  filled->SetArea(rectangle);
  rectangle.left = rectangle.right;
  rectangle.right = 237;
  empty->SetArea(rectangle);
}

// Build the label panel and two progress-bar panels before centering, capturing input and draining posted messages twice.
// Return the final panel factory result after the original flipping check, preserving partially created children on failure.
bool UiProgressDialog::Attach(WnWindow *target) {
  if (!window)
    return false;
  target->SetSize(256, 154);
  if (caption)
    target->SetCaption(*caption);
  UiGenericInterface *generic = nullptr;
  if (target->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect rectangle = {11, 113, 252, 150};
    generic->AddPanel(4, 0, 14, &rectangle);
    generic->Release();
  }
  WnFlatControl *panel = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel))) {
    panel->GetWindow()->SetID(1);
    panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    panel->SetOutlineFlags(32);
    WnResources *resources = UiGetWindowResources();
    WnWindow *primary = panel->GetWindow();
    primary->SetFillColor(resources->GetColor(kWnColorUiBackground));
    ScRect rectangle = {16, 38, 252, 109};
    panel->GetWindow()->SetArea(rectangle);
    target->ChildAdd(panel->GetWindow());
    panel->Release();
    panel = nullptr;
  }
  u32 light = UiGetWindowResources()->GetColor(19);
  u32 dark = UiGetWindowResources()->GetColor(20);
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel))) {
    panel->GetWindow()->SetID(2);
    panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    panel->SetOutlineFlags(11);
    panel->SetOutlineColors(light, light, dark, dark);
    WnWindow *conversion = panel->GetWindow();
    WnWindow *primary = panel->GetWindow();
    primary->SetFillColor(conversion->MakeFillColor(237, 101, 74));
    ScRect rectangle = {30, 68, 31, 84};
    panel->GetWindow()->SetArea(rectangle);
    target->ChildAdd(panel->GetWindow());
    panel->Release();
    panel = nullptr;
  }
  bool result = UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel));
  if (result) {
    panel->GetWindow()->SetID(3);
    panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    panel->SetOutlineFlags(14);
    panel->SetOutlineColors(light, light, dark, dark);
    WnResources *resources = UiGetWindowResources();
    WnWindow *primary = panel->GetWindow();
    primary->SetFillColor(resources->GetColor(kWnColorUiBackground));
    ScRect rectangle = {31, 68, 237, 84};
    panel->GetWindow()->SetArea(rectangle);
    target->ChildAdd(panel->GetWindow());
    panel->Release();
    panel = nullptr;
  }
  Center();
  UiGetWindowManager()->SetCapture(target);
  UiGetWindowManager()->SetFocus(target);
  UiGetWindowManager()->ProcessPostedMessages();
  UiGetWindowManager()->ProcessPostedMessages();
  if (UiGetGraphics()->ScreenBuffer()->IsPageFlipping()) {
    UiGetGraphics()->ScreenBuffer()->DisableFlipping(1);
    flipping = true;
  }
  return result;
}

UiProgressCommand::UiProgressCommand() {
}

// Decode the operation and required integer arguments before dispatching progress creation, update or close.
// Optional caption failure does not reject creation; completed dispatch returns true even when its target refuses it.
bool UiProgressCommand::Execute() {
  u32 operation;
  if (!ReadInteger(&operation))
    return false;
  if (!operation) {
    u32 first, last;
    if (!ReadInteger(&first) || !ReadInteger(&last))
      return false;
    ScString *caption = nullptr;
    if (!ReadString(&caption))
      caption = nullptr;
    OpenRange(first, last, caption);
    if (caption)
      caption->Release();
    return true;
  }
  if (operation == 1) {
    u32 next;
    if (!ReadInteger(&next))
      return false;
    SetValue(next);
  } else if (operation == 2)
    Close();
  return true;
}

// Clear the genuine integer output before popping; consume and release one argument even when its type is rejected.
bool UiProgressCommand::ReadInteger(u32 *out) {
  *out = 0;
  ScVariant *argument;
  if (!PopFrontInput(&argument))
    return false;
  bool result = argument->GetType() == kScValueUint32;
  if (result)
    *out = *argument->AsUint32();
  argument->Release();
  return result;
}

// Read count before the borrowed character pointer, then copy those separate arguments through the real String method.
bool UiProgressCommand::ReadString(ScString **out) {
  *out = nullptr;
  ScVariant *argument;
  if (!PopFrontInput(&argument))
    return false;
  bool result = argument->GetType() == kScValueCharArray;
  if (result) {
    *out = new ScString;
    (*out)->AddRef();
    u32 count = argument->GetCount();
    char *data = argument->RefRZChar();
    (*out)->FromChar(data, count);
  }
  argument->Release();
  return result;
}

// Stop when progress creation refuses; otherwise forward the independent endpoint words and an optional caption.
bool UiProgressCommand::OpenRange(u32 first, u32 last, ScStringView *caption) {
  if (!UiProgressDialog::Open())
    return false;
  UiProgressDialog::SetRange(first, last);
  if (caption)
    UiProgressDialog::SetCaption(caption);
  return true;
}

bool UiProgressCommand::SetValue(u32 next) {
  return UiProgressDialog::SetValue(next);
}

bool UiProgressCommand::Close() {
  return UiProgressDialog::Close();
}

// Build the tip window's caption, background, checkbox, retained label and navigation buttons in the original order.
// Only frame, panel and checkbox creation gate continuation; preference synchronization supplies the final byte result.
u8 UiTipDialog::Attach(WnWindow *recipient) {
  if (!window)
    return 0;
  recipient->SetID(kUiTipDialogWindowId);
  ScString caption;
  {
    UiTextRef resource(kRsTipOfDayTheLlamaSuggestsText, kRsTipOfDayTextGroup);
    caption = resource.GetText();
  }
  recipient->SetCaption(caption);
  ScRect rectangle = {40, 40, 347, 314};
  recipient->SetArea(rectangle);
  CenterEffects();
  if (!CreateFrame())
    return 0;
  WnFlatControl *panel = nullptr;
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
    return 0;
  u32 fill = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
  u32 border = UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
  panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
  panel->GetWindow()->SetArea(11, 234, 303, 270);
  panel->GetWindow()->SetFillColor(fill);
  panel->SetOutlineColor((u16)border);
  panel->SetOutlineFlags(14);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(panel->GetWindow());
  panel->Release();
  ScString checkbox_caption;
  {
    UiTextRef resource(kRsTipOfDayShowTipsAtStartupText, kRsTipOfDayTextGroup);
    checkbox_caption = resource.GetText();
  }
  WnButton *checkbox =
      UiGetWindowResources()->CreateCheckBox(kUiTipStartupCheckboxId, checkbox_caption, nullptr, nullptr, 0);
  if (!checkbox)
    return 0;
  checkbox->SetCaptionStyle(2);
  checkbox->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  parent = window->GetWindow();
  parent->ChildAdd(checkbox->GetWindow());
  checkbox->Fit();
  checkbox->GetWindow()->GZWinMoveTo(24, 215);
  checkbox->Release();
  ScString empty;
  ScRelease(&text);
  text = CreateText(35, 59, 251, 131, &empty, 1);
  ScString next_caption;
  {
    UiTextRef resource(kRsTipOfDayNextTipText, kRsTipOfDayTextGroup);
    next_caption = resource.GetText();
  }
  CreateButton(kUiTipNextButtonId, 18, 235, &next_caption);
  CreateButton(kUiTipCloseButtonId, 257, 235, &empty);
  return LoadPreferences();
}

// Create a retained label under its unsigned position-derived ID, then set flags, alignment, font and geometry in order.
// Geometry sums wrap before signed window calls; the caller owns the label and the parent receives its borrowed primary.
WnLabelControl *UiTipDialog::CreateText(u32 left, u32 top, u32 width, u32 height, ScString *caption, u32 alignment) {
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(left + 1000 * top, *caption);
  if (!label)
    return nullptr;
  label->GetWindow()->SetFlag(kWnFlagReorderable, false);
  label->SetLabelFlag(kWnLabelWrapText, true);
  label->SetAlignment(alignment);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  ScRect rectangle = {(i32)left, (i32)top, (i32)(left + width), (i32)(top + height)};
  label->GetWindow()->SetArea(rectangle);
  label->GetWindow()->SetSize((i32)width, (i32)height);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(label->GetWindow());
  return label;
}

// Attach the flat background before loading and centering its bitmap child; retain the source's failure boundaries.
// The image's resource reference retires before its borrowed buffer is used by the bitmap control.
u8 UiTipDialog::CreateFrame() {
  WnFlatControl *panel = nullptr;
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
    return 0;
  panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
  panel->SetOutlineFlags(32);
  WnResources *resources = UiGetWindowResources();
  WnWindow *primary = panel->GetWindow();
  primary->SetFillColor(resources->GetColor(kWnColorUiBackground));
  ScRect rectangle = {20, 39, 301, 210};
  panel->GetWindow()->SetArea(rectangle);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(panel->GetWindow());
  panel->Release();
  GxBuffer *buffer;
  const GxColorType format(GxColorType::Color::RGB16);
  {
    UiImageRef image(kRsTipDialogFrameImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    buffer = image.GetBuffer();
  }
  if (!buffer)
    return 0;
  rectangle = *buffer->GetBufferArea();
  WnBitmapControl *bitmap = nullptr;
  if (!UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)))
    return 0;
  i32 vertical = (rectangle.top - rectangle.bottom + 171) / 2 + 39;
  rectangle.top += vertical;
  rectangle.bottom += vertical;
  i32 horizontal = (rectangle.left - rectangle.right + 281) / 2 + 20;
  rectangle.left += horizontal;
  rectangle.right += horizontal;
  bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
  bitmap->GetWindow()->SetArea(rectangle);
  bitmap->SetImage(buffer);
  parent = window->GetWindow();
  parent->ChildAdd(bitmap->GetWindow());
  bitmap->Release();
  return 1;
}

// Create a captioned navigation button or the special image-backed button, preserving scoped resource retirement.
// The ordinary branch reads two genuine gutter outputs before fitting; both branches attach and release the primary.
u8 UiTipDialog::CreateButton(u32 identifier, u32 left, u32 top, ScString *caption) {
  WnButton *button;
  if (identifier == kUiTipCloseButtonId) {
    GxBuffer *buffer;
    const GxColorType format(GxColorType::Color::RGB16);
    {
      UiImageRef image(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
      buffer = image.GetBuffer();
    }
    ScString empty;
    WnResources *resources = UiGetWindowResources();
    button = resources->CreateButton(identifier, empty, buffer, buffer->GetBufferArea(), 32);
    if (!button)
      return 0;
    button->SetButtonFlag(kWnButtonShowText, false);
  } else {
    button = UiGetWindowResources()->CreateButton(identifier, *caption, nullptr, nullptr, 32);
    if (!button)
      return 0;
    i32 first, second;
    button->GetGutters(&first, &second);
    button->SetGutters(15, (u8)second);
    button->Fit();
  }
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->GetWindow()->GZWinMoveTo((i32)left, (i32)top);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(button->GetWindow());
  button->Release();
  return 1;
}

#if defined(_M_IX86)
static_assert(sizeof(UiTipDialog) == 24);
#endif

UiTipDialog::UiTipDialog() : references(0), current_tip(0), window(nullptr), text(nullptr) {
}

UiTipDialog::~UiTipDialog() {
  ScRelease(&window);
  ScRelease(&text);
}

// Publish the primary identity for unknown or the tip dialog IID, retaining only after the output is written.
bool UiTipDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiTipDialogInterface);
  HANDLE_QUERY_INTERFACE(IID_UiTipDialogInterface, UiTipDialogInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the shared count before retaining the module, then return the live field after that callback.
u32 UiTipDialog::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove a module hold before decrementing the live nonzero count; destroy the owner whenever the result is zero.
u32 UiTipDialog::Release() {
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

// Reuse an already published generic view, including one published by a failed prior query.
// A successful nonnull acquisition sets the three original style bits before installing the secondary callback.
u8 UiTipDialog::Init() {
  if (window)
    return 1;
  window = nullptr;
  bool acquired = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (acquired && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericCloseButton, 0);
    window->SetStyle(kUiGenericScroller, 1);
    return window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  return acquired;
}

// Borrow the primary window through the retained generic view; initialization is the caller's precondition.
WnWindow *UiTipDialog::GetWindow() const {
  return window->GetWindow();
}

// Detach the retained text control before the generic view, preserving the source's order during release callbacks.
u8 UiTipDialog::Detach(WnWindow *) {
  ScRelease(&text);
  ScRelease(&window);
  return 1;
}

// Center the primary inside the main window's dimensions using separately fetched main, own and moving primaries.
void UiTipDialog::CenterEffects() {
  ScRect main, own;
  window->GetWindow()->GetMainWindow()->GetArea(&main);
  window->GetWindow()->GetArea(&own);
  window->GetWindow()->GZWinMoveTo(
      (own.left + main.right - own.right - main.left) / 2, (main.bottom + own.top - main.top - own.bottom) / 2);
}

// Return the newly constructed primary unknown view at zero references; the class catalog performs the retained query.
ScUnknown *UiCreateClassC2FA5860() {
  return static_cast<UiTipDialogInterface *>(new UiTipDialog);
}

// End a modal loop through the live manager, or capture the primary's manager before posting the modeless parent close.
// The original helper always returns one and does not retain the supplied primary.
u8 SC3_STDCALL UiCloseTipDialog(WnWindow *window) {
  if (window->GetFlag(kWnFlagModal))
    UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  else {
    WnWindowManager *window_manager = window->GetWindowManager();
    WnWindow *parent = window->GetParentWin();
    window_manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  }
  return 1;
}

// Route attachment, detachment, navigation and close commands through the shared Boolean callback.
// Button notifications play the click event before command filtering; Escape and modified F4 close without that event.
bool UiTipDialog::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if ((u32)first != 1)
      return 0;
    if (UiGetBoxX())
      UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    u32 command = (u32)second;
    if (command == kUiTipPreviousButtonId)
      PreviousTipEffects();
    else if (command == kUiTipNextButtonId)
      NextTipEffects();
    else {
      if (command == kUiTipCloseButtonId)
        SavePreferencesEffects();
      else if (command != kUiFrameCloseButtonId)
        return 0;
      UiCloseTipDialog(recipient);
    }
    return 1;
  }
  if (message == kWnMsg_KeyDown) {
    u32 key = (u32)first;
    if (key != 27 && (key != 115 || !(second & 4)))
      return 0;
    return UiCloseTipDialog(recipient);
  }
  if (message == kWnMsg_Init)
    return Attach(recipient);
  if (message == kWnMsg_Shutdown)
    return Detach(recipient);
  return 0;
}

// Capture the enable byte before querying the checkbox, then reload the last tip through a fresh preference lookup.
// A successful query advances the retained label even when it supplies no checkbox; the captured byte is returned intact.
u8 UiTipDialog::LoadPreferences() {
  u8 enabled = UiGetPreferences()->TODPrefs()->enabled;
  WnButton *checkbox = nullptr;
  bool found =
      window->GetWindow()->GetChildAs(kUiTipStartupCheckboxId, IID_WnButton, reinterpret_cast<void **>(&checkbox));
  if (!found)
    return 0;
  if (checkbox) {
    if (enabled)
      checkbox->ToggleOn();
    else
      checkbox->ToggleOff();
    checkbox->Release();
  }
  current_tip = UiGetPreferences()->TODPrefs()->last_tip;
  NextTipEffects();
  return enabled;
}

// Store the current tip before querying the checkbox, then preserve the separately captured preference interface.
void UiTipDialog::SavePreferencesEffects() {
  UiGetPreferences()->TODPrefs()->last_tip = current_tip;
  WnButton *checkbox = nullptr;
  if (window->GetWindow()->GetChildAs(kUiTipStartupCheckboxId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool enabled = checkbox->IsOn();
    preferences->TODPrefs()->enabled = enabled;
    checkbox->Release();
  }
}

// Store the requested resource instance before resolving its independent text copy and updating the retained label.
void UiTipDialog::SetTipEffects(u32 instance) {
  current_tip = instance;
  ScString caption;
  {
    UiTextRef resource(instance, kRsTipOfDayTextGroup);
    caption = resource.GetText();
  }
  text->SetCaption(caption);
}

// Advance through tips1 through57, skipping the missing instance16 and wrapping the upper end back to1.
void UiTipDialog::NextTipEffects() {
  u32 next = current_tip >= 57 ? 1 : current_tip + 1;
  if (next == 16)
    next = 17;
  SetTipEffects(next);
}

// Step backward through the same resource sequence, wrapping0 or1 to57 and skipping instance16.
// The selected resource instance remains an ordinary field shared with the original preference synchronization.
void UiTipDialog::PreviousTipEffects() {
  u32 previous = current_tip <= 1 ? 57 : current_tip - 1;
  if (previous == 16)
    previous = 15;
  SetTipEffects(previous);
}

WnLabelControl *UiCreateInputLabel(WnWindow *window, u32 identifier, i32 left, i32 top, i32 right, i32 bottom,
    const ScStringView &text, u32 alignment);
WnTextControl *UiCreateInputField(WnWindow *window, u32 identifier, i32 left, i32 top, i32 right, i32 bottom);
WnButton *UiCreateInputButton(WnWindow *window, u32 identifier, i32 left, i32 top, u32 image_id, i8 key);

UiUserInput::UiUserInput() : result(0), message_label(nullptr), input(nullptr) {
  SetNotify(static_cast<UiWindowCallback *>(this));
}

UiUserInput::~UiUserInput() {
}

// Publish either private user-input view or callback view and delegate other identities to the generic base.
bool UiUserInput::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiUserInputInterface, UiUserInputInterface);
  return UiGenericWindow::QueryInterface(iid, out);
}

u32 UiUserInput::AddRef() {
  return UiGenericWindow::AddRef();
}

u32 UiUserInput::Release() {
  return UiGenericWindow::Release();
}

u8 UiUserInput::GetResult() {
  return result;
}

// Borrow the original primary window from the user-input view without retaining it.
WnWindow *UiUserInput::AsIGZWin() {
  return this;
}

// Initialize the generic window, attach its label, accept button and text field, then restore its saved position.
bool UiUserInput::Init() {
  if (GetInitFlag())
    return true;
  UiGetWindowResources();
  UiGenericWindow::Init();
  SetStyle(kUiGenericDraggable, 1);
  SetStyle(kUiGenericSecondButton, 0);
  SetStyle(kUiGenericResizable, 0);
  SetStyle(kUiGenericScroller, 0);
  SetStyle(kUiGenericBackButton, 0);
  SetFlag(kWnFlagPrivateBuffer, true);
  SetID(kUiUserInputWindowId);
  UiGetBoxX()->Event(1010, 0, 0, 0, 0);
  SetSize(282, 122);
  WnWindow *vertical = GetParentWin(), *horizontal = GetParentWin();
  i32 top = (i32)((u32)vertical->GetH() - (u32)GetH()) / 2;
  i32 left = (i32)((u32)horizontal->GetW() - (u32)GetW()) / 2;
  GZWinMoveTo(left, top);
  message_label = ScAddRefNotNull(UiCreateInputLabel(this, 0, 6, 36, 276, 58, message_text, 0));
  UiCreateInputButton(this, 100, 232, 84, kRsStandardButtonImageId, 0);
  input = ScAddRefNotNull(UiCreateInputField(this, 1324, 6, 58, 275, 78));
  UiLoadWindowPosition(this, 0);
  return true;
}

void UiUserInput::SetTitleText(const ScStringView &text) {
  SetCaption(text);
}

// Update the live label when present, or store an independent message copy before initialization.
void UiUserInput::SetMessageText(const ScStringView &text) {
  if (message_label)
    message_label->SetCaption(text);
  else {
    ScString copy(text);
    message_text = copy;
  }
}

// Copy the input control's current text, or publish the original empty string if no control exists.
void UiUserInput::GetInputText(ScStringView *out) {
  if (input)
    input->GetText(out);
  else
    out->FromChar("");
}

void UiUserInput::SetInputText(const ScStringView &text) {
  if (input)
    input->SetText(text);
}

// Release the two acquired controls before shutting down the initialized generic window.
bool UiUserInput::Shutdown() {
  if (!GetInitFlag())
    return true;
  ScRelease(&message_label);
  ScRelease(&input);
  return UiGenericWindow::Shutdown();
}

// Play the close cue and persist position before ending a modal window or posting to its parent.
u8 UiUserInput::Close(u32 command) {
  UiGetBoxX()->Event(1011, 0, 0, 0, 0);
  UiSaveWindowPosition(this, 0);
  if (GetFlag(kWnFlagModal))
    owner->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), command);
  else {
    WnWindowManager *manager = owner;
    WnWindow *destination = GetParentWin();
    manager->PostMessageToTarget(
        destination, kWnMsg_ChildDelete, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), 0, 0);
  }
  return 1;
}

// Handle the accept and cancel buttons, recording the selected result before playing the cue and closing.
u8 UiUserInput::OnButtonActivated(u32 button_id) {
  if (button_id == 100)
    result = 1;
  else if (button_id == kUiFrameCloseButtonId)
    result = 0;
  else
    return 0;
  UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  Close(kUiFrameCloseButtonId);
  return 1;
}

// Dispatch button activation and the original Escape, modified Enter and modified F4 close keys.
bool UiUserInput::DoWinMsg(WnWindow *, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command && (u32)first == kWnCommandButtonActivated)
    return OnButtonActivated((u32)second) != 0;
  if (message == kWnMsg_KeyDown &&
      ((u32)first == kWnKeyEscape || (((u32)first == kWnKeyReturn || (u32)first == kWnKeyF4) && ((u32)second & 4)))) {
    result = 0;
    Close(kUiFrameCloseButtonId);
    return true;
  }
  return false;
}

// Create and attach a user-input label, returning its borrowed interface after the local release.
WnLabelControl *UiCreateInputLabel(WnWindow *window, u32 identifier, i32 left, i32 top, i32 right, i32 bottom,
    const ScStringView &text, u32 alignment) {
  WnResources *resources = UiGetWindowResources();
  WnLabelControl *label = resources->CreateLabel(identifier, text);
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  label->SetTextColor(0, resources->GetColor(kWnThemeMutedTextColorId));
  label->SetTextColor(1, resources->GetColor(21));
  label->SetAlignment(alignment);
  label->GetWindow()->SetArea(left, top, right, bottom);
  window->ChildAdd(label->GetWindow());
  label->Release();
  return label;
}

// Create and attach the editable single-line control with the original font and outline colors.
WnTextControl *UiCreateInputField(WnWindow *window, u32 identifier, i32 left, i32 top, i32 right, i32 bottom) {
  WnResources *resources = UiGetWindowResources();
  WnTextControl *input = resources->CreateTextEdit(identifier, 1);
  input->AsIGZWin()->SetID(identifier);
  input->SetFont(resources->GetFont(kWnFontUiSmall));
  input->SetFontColor(resources->GetColor(kWnThemeTextColorId));
  input->AsIGZWin()->SetArea(left, top, right, bottom);
  WnWindow *primary = input->AsIGZWin();
  primary->SetFillColor(resources->GetColor(21));
  input->SetOutlineType(WnInputOutline::ThreeD);
  input->SetOutlineColor(WnInputEdge::LeftTop, resources->GetColor(19));
  input->SetOutlineColor(WnInputEdge::RightBottom, resources->GetColor(20));
  window->ChildAdd(input->AsIGZWin());
  input->Release();
  return input;
}

// Load the requested bitmap before creating and attaching a fixed image button.
WnButton *UiCreateInputButton(WnWindow *window, u32 identifier, i32 left, i32 top, u32 image_id, i8 key) {
  GxBuffer *image;
  {
    const GxColorType format(GxColorType::Color::RGB16);
    UiImageRef reference(image_id, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = reference.GetBuffer();
  }
  if (!image)
    return nullptr;
  WnResources *resources = UiGetWindowResources();
  image->SetTransparency(resources->GetColor(kWnColorUiTransparency));
  WnButton *button;
  {
    ScString caption;
    button = resources->CreateFixedButton(identifier, caption, image, image->GetBufferArea(), (u32)(i32)key);
  }
  if (!button)
    return nullptr;
  WnWindow *primary = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  primary->GZWinMoveTo(left, top);
  window->ChildAdd(primary);
  button->Release();
  return button;
}

// Construct the actual user-input owner and return its primary Unknown without another factory retain.
ScUnknown *UiCreateClass454423FF() {
  return static_cast<WnWindow *>(new UiUserInput);
}

// Restore a stored position only when both coordinates lie inside the current main-window bounds.
// The original accepts kinds below 15; assert the array-index invariant rather than inventing a runtime failure.
u8 UiLoadWindowPosition(WnWindow *window, i32 kind) {
  NwInternet *internet = UiGetApplication()->GetSC3Internet();
  NwInternetPrefs *preferences = internet ? internet->GetInternetPrefs() : nullptr;
  if (preferences && kind < 15) {
    assert(kind >= 0);
    WnWindow *main_window = window->GetMainWindow();
    i32 width = main_window ? main_window->GetW() : 16, height = main_window ? main_window->GetH() : 16;
    NwWindowPosition *position = &preferences->windowPositions[kind];
    if (position->left >= 0 && (u32)position->left <= (u32)width - 8 && position->top >= 0 &&
        (u32)position->top < (u32)height - 8)
      window->GZWinMoveTo(position->left, position->top);
  }
  return 1;
}

UiWindowProcedure::UiWindowProcedure() : references(0) {
}

UiWindowProcedure::~UiWindowProcedure() {
}

// Publish the same callback view for unknown and window-procedure identities, then use the live virtual retain.
// Rejected identities and missing output storage clear the output.
bool UiWindowProcedure::QueryInterface(u32 iid, void **out) {
  if ((iid != IID_ScUnknown && iid != IID_UiWindowCallback) || !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiWindowProcedure::AddRef() {
  UiGetDirector()->AddRef();
  return ++references;
}

// Remove the module reference before decrementing the live count, including the original zero-count wrap behavior.
// Exactly zero destroys through the real virtual destructor and returns zero after destruction.
u32 UiWindowProcedure::Release() {
  UiGetDirector()->RemoveRef();
  u32 count = --references;
  if (!count)
    delete this;
  return count;
}

// Give the pre-handler first refusal, then route control, keyboard, lifecycle and client-rectangle notifications.
// Unhandled keyboard input is forwarded as the original four-word message to the current parent window.
bool UiWindowProcedure::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) {
  if (OnBeforeMessage(window, message, first, second, third))
    return 1;
  switch (message) {
  case kWnMsg_Command:
    return OnCommand(window, (u32)first, second, third);
  case kWnMsg_KeyDown: {
    if (OnKeyDown(window, (u32)first, (u32)second))
      return 1;
    WnWindow *parent = window->GetParentWin();
    if (!parent)
      return 1;
    ScMessage forwarded = {kWnMsg_KeyDown, first, second, 0};
    return parent->DoMessage(&forwarded);
  }
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  case kWnMsg_ClientConstraints:
    if (first && second)
      return OnGetLimits(window, reinterpret_cast<ScRect *>(first), reinterpret_cast<ScRect *>(second));
    break;
  case kWnMsg_ClientLayout:
    if (first)
      return OnClientChanged(window, reinterpret_cast<ScRect *>(first));
    break;
  }
  return 0;
}

u8 UiWindowProcedure::OnBeforeMessage(WnWindow *, u32, uptr, uptr, uptr) {
  return 0;
}

u8 UiWindowProcedure::OnInit(WnWindow *) {
  return 1;
}

u8 UiWindowProcedure::OnShutdown(WnWindow *) {
  return 1;
}

u8 UiWindowProcedure::OnKeyDown(WnWindow *, u32, u32) {
  return 0;
}

u8 UiWindowProcedure::OnGetLimits(WnWindow *, ScRect *, ScRect *) {
  return 0;
}

u8 UiWindowProcedure::OnClientChanged(WnWindow *, ScRect *) {
  return 0;
}

u8 UiWindowProcedure::OnCommand(WnWindow *, u32, uptr, uptr) {
  return 0;
}

// Acquire the scoped provider, create the requested primary control and assign its ID before attaching it.
// Retire the temporary control before the provider, preserving the actual child-attachment reply.
u8 UiWindowProcedure::AddControl(WnWindow *window, u32 kind, u32 identifier) const {
  UiWindowResourcesRef resources;
  u8 result = 0;
  if (resources.provider) {
    WnWindow *control = nullptr;
    if (resources.provider->CreateObject(kind, IID_WnWindow, reinterpret_cast<void **>(&control))) {
      control->SetID(identifier);
      result = window->ChildAdd(control);
      control->Release();
    }
  }
  return result;
}

// Acquire the scoped provider only for a real font output, then retain its borrowed font before publication.
// Failed provider/font lookups leave the caller's output unchanged and retire the scoped provider before returning.
u8 UiWindowProcedure::GetFont(u32 identifier, FtFont **out) const {
  if (!out)
    return 0;
  UiWindowResourcesRef resources;
  if (resources.provider) {
    FtFont *font = resources.provider->GetFont(identifier);
    if (font) {
      font->AddRef();
      *out = font;
      return 1;
    }
  }
  return 0;
}
