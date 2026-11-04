// Shared bodies retain GZWIND address markers. Other module copies retain distinct service bindings; their
// individual binary parity is not established by these markers.

// Construct the circular child list. Its sentinel has no window payload.
WN_ROOT_CLASS::WnChildren::WnChildren() {
  head = new WnChild;
  head->next = head;
  head->previous = head;
}

// Retire list storage without releasing window references; the owner performs child deletion before this phase.
WN_ROOT_CLASS::WnChildren::~WnChildren() {
  WnChild *node = head->next;
  while (node != head) {
    WnChild *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

#if defined(WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR)
// Construct the base window and register it with the supplied owner after all member defaults are installed.
WN_ROOT_CLASS::WN_ROOT_CLASS(WnWindowManager *owner, WnKeyboard *keyboard)
    : owner(owner), keyboard(keyboard), references(0), identifier(0), absolute_area{}, draw_area{},
      children_locked(false), parent(nullptr), notification_target(nullptr), caption(), private_buffer(nullptr),
      draw_buffer(nullptr), dirty(true), cursor(nullptr), accelerator(nullptr), fill_color(0), child_focus(nullptr),
      key_equivalent(0), mouse_down_x(0), mouse_down_y(0), area{}, buffer_area{}, flags(kWnDefaultFlags) {
  if (owner)
    owner->AddWindowToValidList(this);
}

#endif

// Release the accelerator and delete children before owner cleanup. Embedded caption and list members retire afterward.
WN_ROOT_CLASS::~WN_ROOT_CLASS() {
  if (accelerator)
    accelerator->Release();
  accelerator = nullptr;
  ChildDeleteAll();
  Shutdown();
  owner->CleanUpWindowReferences(this);
  owner->RemoveWindowFromValidList(this);
}

// Publish the identical primary prefix for Unknown, message-target and window IDs before live AddRef. Failed queries
// clear the output slot; the native message-target prefix adds no second polymorphic subobject.
bool WN_ROOT_CLASS::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_ScMessageTarget && iid != IID_WnWindow))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnWindow *>(this);
  AddRef();
  return 1;
}

// Increment the unsigned reference count and return its new value.
u32 WN_ROOT_CLASS::AddRef() {
  return ++references;
}

// Delete through the real virtual destructor when the unsigned count reaches zero.
u32 WN_ROOT_CLASS::Release() {
  if (--references)
    return references;
  delete this;
  return 0;
}

// Return the borrowed owner without acquiring a reference.
WnWindowManager *WN_ROOT_CLASS::GetWindowManager() {
  return owner;
}

// Forward owner replacement to the original field setter; registration is a separate operation.
bool WN_ROOT_CLASS::SetWindowManager(WnWindowManager *window_manager) {
  return SetWindowManagerInternal(window_manager);
}

// Return the borrowed keyboard supplied to the constructor.
WnKeyboard *WN_ROOT_CLASS::GetKeyboard() {
  return keyboard;
}

// Replace the borrowed keyboard and report whether it is nonnull.
bool WN_ROOT_CLASS::SetKeyboard(WnKeyboard *value) {
  keyboard = value;
  return keyboard != nullptr;
}

// Ask the live owner for its main window.
WnWindow *WN_ROOT_CLASS::GetMainWindow() {
  return owner->GetMainWindow();
}

// Publish initialized state through the live flag override before private-buffer preparation and buffer selection.
bool WN_ROOT_CLASS::Init() {
  SetFlag(kWnFlagInitialized, true);
  if ((flags & (kWnFlagVisible | kWnFlagPrivateBuffer)) == (kWnFlagVisible | kWnFlagPrivateBuffer))
    PrivateBuffer(true);
  SetBufferToDrawToRecursive();
  return 1;
}

// Hide and delete children, retire the private buffer, then clear initialized state through the live flag override.
bool WN_ROOT_CLASS::Shutdown() {
  HideWindow();
  ChildDeleteAll();
  PrivateBuffer(false);
  SetFlag(kWnFlagInitialized, false);
  return 1;
}

// Return the borrowed parent window.
WnWindow *WN_ROOT_CLASS::GetParentWin() const {
  return parent;
}

// Follow the parent with the notification target only while the target still aliases the previous parent.
bool WN_ROOT_CLASS::SetParentWin(WnWindow *window) {
  if (!notification_target || notification_target == parent)
    notification_target = window;
  parent = window;
  return 1;
}

// Preserve the original advisory child-list lock byte. Nested locking does not create a second lock depth.
bool WN_ROOT_CLASS::SetChildListLock(bool locked) {
  if (locked && children_locked)
    return 0;
  children_locked = locked;
  return 1;
}

// Forward attachment through the original absolute child operation without an additional virtual dispatch.
bool WN_ROOT_CLASS::ChildAdd(WnWindow *child) {
  return WN_ROOT_CLASS::ChildAddAbsolute(child);
}

// Initialize a permitted new child before inserting it at the front and retaining it.
bool WN_ROOT_CLASS::ChildAddAbsolute(WnWindow *child) {
  if (!IsWindowValid(child))
    return 0;
  SetChildListLock(true);
  WnChild *node = children.head->next;
  while (node != children.head && node->window != child)
    node = node->next;
  SetChildListLock(false);
  if (node != children.head)
    return 0;
  child->SetParentWin(this);
  if (!child->Init()) {
    child->Shutdown();
    child->SetParentWin(nullptr);
    return 0;
  }
  SetChildListLock(true);
  node = new WnChild;
  node->window = child;
  node->next = children.head->next;
  node->previous = children.head;
  node->next->previous = node;
  children.head->next = node;
  SetChildListLock(false);
  child->AddRef();
  child->CalcAbsoluteArea();
  return 1;
}

// Forward detachment through the original absolute operation, preserving its captured validity result.
bool WN_ROOT_CLASS::ChildRemove(WnWindow *child) {
  return WN_ROOT_CLASS::ChildRemoveAbsolute(child);
}

// Remove the matching node after owner cleanup, parent clearing and release when the owner still recognizes the child.
bool WN_ROOT_CLASS::ChildRemoveAbsolute(WnWindow *child) {
  bool allowed = IsWindowValid(child);
  SetChildListLock(true);
  WnChild *node = children.head->next;
  while (node != children.head && node->window != child)
    node = node->next;
  SetChildListLock(false);
  if (node == children.head)
    return 0;
  if (allowed) {
    owner->CleanUpWindowReferences(child);
    child->SetParentWin(nullptr);
    child->Release();
  }
  SetChildListLock(true);
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
  SetChildListLock(false);
  return 1;
}

// Keep the child alive across shutdown and removal, then invoke owner deletion before releasing the temporary reference.
bool WN_ROOT_CLASS::ChildDeleteAbsolute(WnWindow *child) {
  bool closed = false;
  if (owner->IsWindowValid(child)) {
    child->AddRef();
    closed = child->Shutdown();
  }
  bool detached = WN_ROOT_CLASS::ChildRemoveAbsolute(child);
  owner->DestroyWindow(child);
  child->Release();
  return closed && detached;
}

// Ask the owning manager to delete the child through its normal validation and deferred-removal policy.
// Only the manager invokes absolute deletion after establishing a valid target and parent.
bool WN_ROOT_CLASS::ChildDelete(WnWindow *child) {
  return owner->DestroyWindow(child);
}

// Repeatedly delete the live first child through its owner, accumulating false results without snapshotting the list.
bool WN_ROOT_CLASS::ChildDeleteAll() {
  bool failed = false;
  while (children.head->next != children.head)
    failed |= owner->DestroyWindow(children.head->next->window) == 0;
  return !failed;
}

// Check owner permission before searching the child list under its advisory lock.
bool WN_ROOT_CLASS::ChildExists(WnWindow *child) {
  if (!owner->IsWindowValid(child))
    return 0;
  SetChildListLock(true);
  WnChild *node = children.head->next;
  while (node != children.head && node->window != child)
    node = node->next;
  SetChildListLock(false);
  return node != children.head;
}

// Search child identifiers in forward list order and stop at the first match.
bool WN_ROOT_CLASS::ChildExists(u32 value) {
  SetChildListLock(true);
  WnChild *node = children.head->next;
  while (node != children.head && node->window->GetID() != value)
    node = node->next;
  SetChildListLock(false);
  return node != children.head;
}

// Ask the current parent to bring this window to the front.
bool WN_ROOT_CLASS::PullToFront() {
  return parent ? parent->ChildToFront(this) : 0;
}

// Ask the current parent to send this window to the back.
bool WN_ROOT_CLASS::SendToBack() {
  return parent ? parent->ChildToBack(this) : 0;
}

// Resolve the identifier through the live lookup method before forwarding to the pointer overload.
bool WN_ROOT_CLASS::ChildToFront(u32 value) {
  return ChildToFront(GetChildWindowFromID(value));
}

// Raise the parent first, then move a permitted reorderable child to the list front without reference changes.
bool WN_ROOT_CLASS::ChildToFront(WnWindow *child) {
  if (!owner->IsWindowValid(child) || !ChildExists(child))
    return 0;
  if (GetParentWin())
    GetParentWin()->ChildToFront(this);
  if (!child->GetFlag(kWnFlagReorderable))
    return 0;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head;) {
    WnChild *next = node->next;
    if (node->window == child) {
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
    }
    node = next;
  }
  WnChild *node = new WnChild;
  node->window = child;
  node->next = children.head->next;
  node->previous = children.head;
  node->next->previous = node;
  children.head->next = node;
  SetChildListLock(false);
  return 1;
}

// Move a permitted reorderable child to the list tail without changing its ownership references.
bool WN_ROOT_CLASS::ChildToBack(WnWindow *child) {
  if (!owner->IsWindowValid(child) || !ChildExists(child) || !child->GetFlag(kWnFlagReorderable))
    return 0;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head;) {
    WnChild *next = node->next;
    if (node->window == child) {
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
    }
    node = next;
  }
  WnChild *node = new WnChild;
  node->window = child;
  node->next = children.head;
  node->previous = children.head->previous;
  node->previous->next = node;
  children.head->previous = node;
  SetChildListLock(false);
  return 1;
}

// Walk borrowed parent pointers, excluding this window itself.
bool WN_ROOT_CLASS::IsWinInParentChain(WnWindow *window) {
  for (WnWindow *ancestor = GetParentWin(); ancestor; ancestor = ancestor->GetParentWin())
    if (ancestor == window)
      return 1;
  return 0;
}

// Search children recursively in list order; direct identity matches precede each child's recursive search.
bool WN_ROOT_CLASS::IsWinInChildChain(WnWindow *window) {
  bool found = false;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head; node = node->next) {
    if (node->window == window) {
      found = 1;
      break;
    }
    found = node->window->IsWinInChildChain(window);
    if (found)
      break;
  }
  SetChildListLock(false);
  return found;
}

// Count the live child nodes; the original list does not maintain a separate size field.
u32 WN_ROOT_CLASS::GetChildCount() const {
  u32 count = 0;
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    ++count;
  return count;
}

// Return the borrowed first matching child, with no query or retain.
WnWindow *WN_ROOT_CLASS::GetChildWindowFromID(u32 value) {
  if (!WN_ROOT_CLASS::GetChildCount())
    return nullptr;
  WnWindow *found = nullptr;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    if (node->window->GetID() == value) {
      found = node->window;
      break;
    }
  SetChildListLock(false);
  return found;
}

// Search descendants before this window itself. This source ordering permits a descendant with the same identifier to win.
WnWindow *WN_ROOT_CLASS::GetChildWindowFromIDRecursive(u32 value) {
  WnWindow *found = nullptr;
  if (WN_ROOT_CLASS::GetChildCount()) {
    SetChildListLock(true);
    for (WnChild *node = children.head->next; node != children.head; node = node->next) {
      found = node->window->GetChildWindowFromIDRecursive(value);
      if (found)
        break;
    }
    SetChildListLock(false);
  }
  if (!found && GetID() == value)
    return this;
  return found;
}

// Query the directly resolved child through its live interface, preserving its own publication/reference behavior.
bool WN_ROOT_CLASS::GetChildAs(u32 value, u32 iid, void **out) {
  WnWindow *child = GetChildWindowFromID(value);
  return child && child->QueryInterface(iid, out);
}

// Query the recursively resolved window through its live interface.
bool WN_ROOT_CLASS::GetChildAsRecursive(u32 value, u32 iid, void **out) {
  WnWindow *child = GetChildWindowFromIDRecursive(value);
  return child && child->QueryInterface(iid, out);
}

// Enumerate the live forward list. Non-window interfaces are queried and their temporary reference is released afterward.
bool WN_ROOT_CLASS::EnumChildren(u32 iid, bool (*callback)(WnWindow *, u32, void *, void *), void *context) {
  if (!callback || !WN_ROOT_CLASS::GetChildCount())
    return 0;
  u32 failures = 0;
  for (WnChild *node = children.head->next; node != children.head; node = node->next) {
    WnWindow *child = node->window;
    if (iid == IID_WnWindow) {
      u32 id = child->GetID();
      failures += callback(this, id, child, context) == 0;
    } else {
      void *view;
      if (child->QueryInterface(iid, &view)) {
        u32 id = child->GetID();
        failures += callback(this, id, view, context) == 0;
        child->Release();
      }
    }
  }
  return failures == 0;
}

// Compare unsigned window identifiers, acquiring the first identifier before calling the second getter.
bool WN_ROOT_CLASS::LessfuncId(WnWindow *first, WnWindow *second, void *) {
  u32 identifier = first->GetID();
  return identifier < second->GetID();
}

// Reinsert out-of-order children at the first earlier comparison match, restarting after each move.
bool WN_ROOT_CLASS::SortChildren(bool (*compare)(WnWindow *, WnWindow *, void *), void *context, bool all) {
  if (WN_ROOT_CLASS::GetChildCount() < 2)
    return 0;
  for (;;) {
    WnChild *previous = children.head->next, *node = previous->next;
    bool moved = false;
    for (; node != children.head; previous = node, node = node->next) {
      WnWindow *window = node->window;
      if (!all && window->GetFlag(kWnFlagReorderable))
        continue;
      bool before =
          compare ? compare(window, previous->window, context) : LessfuncId(window, previous->window, context);
      if (!before)
        continue;
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
      WnChild *position = children.head->next;
      while (position != children.head) {
        before =
            compare ? compare(window, position->window, context) : LessfuncId(window, position->window, context);
        if (before)
          break;
        position = position->next;
      }
      WnChild *fresh = new WnChild;
      fresh->window = window;
      fresh->next = position;
      fresh->previous = position->previous;
      fresh->previous->next = fresh;
      position->previous = fresh;
      moved = true;
      break;
    }
    if (!moved)
      return 1;
  }
}

// Return the signed width of the stored area.
i32 WN_ROOT_CLASS::GetW() const {
  return (i32)((u32)area.right - (u32)area.left);
}

// Return the signed height of the stored area.
i32 WN_ROOT_CLASS::GetH() const {
  return (i32)((u32)area.bottom - (u32)area.top);
}

// Read the stored left edge.
i32 WN_ROOT_CLASS::GetL() const {
  return area.left;
}

// Read the stored top edge.
i32 WN_ROOT_CLASS::GetT() const {
  return area.top;
}

// Read the stored right edge.
i32 WN_ROOT_CLASS::GetR() const {
  return area.right;
}

// Read the stored bottom edge.
i32 WN_ROOT_CLASS::GetB() const {
  return area.bottom;
}

// Borrow the original read-only local rectangle without copying it or permitting mutation through this view.
const ScRect *WN_ROOT_CLASS::GetArea() const {
  return &area;
}

// Copy the stored area into the genuine output pointer.
void WN_ROOT_CLASS::GetArea(ScRect *rectangle) const {
  *rectangle = area;
}

// Borrow the original read-only absolute rectangle without changing ownership or recomputing its bounds.
const ScRect *WN_ROOT_CLASS::GetAreaAbsolute() const {
  return &absolute_area;
}

// Copy the stored absolute area into the genuine output reference.
void WN_ROOT_CLASS::GetAreaAbsolute(ScRect *rectangle) const {
  *rectangle = absolute_area;
}

// Change width through the live four-scalar area overload.
void WN_ROOT_CLASS::SetW(i32 width) {
  SetArea(area.left, area.top, (i32)((u32)area.left + (u32)width), area.bottom);
}

// Change height through the live four-scalar area overload.
void WN_ROOT_CLASS::SetH(i32 height) {
  SetArea(area.left, area.top, area.right, (i32)((u32)area.top + (u32)height));
}

// Preserve the origin while changing both extents through the live scalar overload.
void WN_ROOT_CLASS::SetSize(i32 width, i32 height) {
  SetArea(area.left, area.top, (i32)((u32)area.left + (u32)width), (i32)((u32)area.top + (u32)height));
}

// Expand the point's two extents through the live scalar size setter.
// This retains a derived window's size handling rather than writing the stored area directly.
void WN_ROOT_CLASS::SetSize(const ScPoint &size) {
  SetSize(size.x, size.y);
}

// Center the current extent within the supplied rectangle, rounding signed half differences toward zero.
// Submit all four computed edges through the live area setter so derived layout behavior is retained.
void WN_ROOT_CLASS::CenterWindowInRect(const ScRect &rectangle) {
  i32 horizontal = (i32)((u32)rectangle.right - (u32)area.right - (u32)rectangle.left + (u32)area.left);
  i32 vertical = (i32)((u32)area.top - (u32)area.bottom + (u32)rectangle.bottom - (u32)rectangle.top);
  i32 left = (i32)((u32)rectangle.left + (u32)(horizontal / 2));
  i32 top = (i32)((u32)rectangle.top + (u32)(vertical / 2));
  SetArea(left, top, (i32)((u32)left + (u32)area.right - (u32)area.left),
      (i32)((u32)top + (u32)area.bottom - (u32)area.top));
}

// Ignore a null rectangle; otherwise compute the same centered area without dispatching the reference overload.
// The original pointer overload calls the live area setter directly.
void WN_ROOT_CLASS::CenterWindowInRect(ScRect *rectangle) {
  if (!rectangle)
    return;
  i32 horizontal = (i32)((u32)rectangle->right - (u32)area.right - (u32)rectangle->left + (u32)area.left);
  i32 vertical = (i32)((u32)area.top - (u32)area.bottom + (u32)rectangle->bottom - (u32)rectangle->top);
  i32 left = (i32)((u32)rectangle->left + (u32)(horizontal / 2));
  i32 top = (i32)((u32)rectangle->top + (u32)(vertical / 2));
  SetArea(left, top, (i32)((u32)left + (u32)area.right - (u32)area.left),
      (i32)((u32)top + (u32)area.bottom - (u32)area.top));
}

// Expand the rectangle through the live four-scalar overload.
void WN_ROOT_CLASS::SetArea(const ScRect &rectangle) {
  SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
}

// Store all edges before optional private-buffer resizing and descendant coordinate updates.
void WN_ROOT_CLASS::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  bool resized = (u32)right - (u32)left != (u32)area.right - (u32)area.left ||
      (u32)bottom - (u32)top != (u32)area.bottom - (u32)area.top;
  area = ScRect{left, top, right, bottom};
  if ((flags & kWnFlagPrivateBuffer) && resized)
    PrivateBuffer(true);
  SetAreaToDrawToRecursive();
  CalcAbsoluteArea();
}

// Move the window while preserving its current extents.
void WN_ROOT_CLASS::GZWinMoveTo(i32 x, i32 y) {
  SetArea(x, y, (i32)((u32)x + (u32)area.right - (u32)area.left),
      (i32)((u32)y + (u32)area.bottom - (u32)area.top));
}

// Shift all four edges through the live area overload.
void WN_ROOT_CLASS::GZWinOffset(i32 x, i32 y) {
  SetArea((i32)((u32)x + (u32)area.left), (i32)((u32)y + (u32)area.top),
      (i32)((u32)x + (u32)area.right), (i32)((u32)y + (u32)area.bottom));
}

// Clamp the supplied rectangle against local extents using the original margin adjustment order.
bool WN_ROOT_CLASS::FitRectToWindow(ScRect *rectangle, i32 margin) {
  i32 width = (i32)((u32)area.right - (u32)area.left);
  i32 height = (i32)((u32)area.bottom - (u32)area.top);
  if (rectangle->left < 0)
    rectangle->left = margin;
  if (rectangle->top < 0)
    rectangle->top = margin;
  if (rectangle->right > width) {
    rectangle->left = (i32)((u32)rectangle->left + (u32)width - (u32)rectangle->right - (u32)margin);
    rectangle->right = (i32)((u32)width - (u32)margin);
  }
  if (rectangle->bottom > height) {
    rectangle->top = (i32)((u32)rectangle->top + (u32)height - (u32)rectangle->bottom - (u32)margin);
    rectangle->bottom = (i32)((u32)height - (u32)margin);
  }
  return (i32)((u32)rectangle->right + (u32)margin - (u32)rectangle->left) <= width &&
      (i32)((u32)margin + (u32)rectangle->bottom - (u32)rectangle->top) <= height;
}

// Add this window and ancestor origins, stopping before the parentless root's origin.
void WN_ROOT_CLASS::WindowToScreenCoordinates(i32 *x, i32 *y) const {
  const WnWindow *window = this;
  while (window->GetParentWin()) {
    i32 left = window->GetL();
    *x = (i32)((u32)*x + (u32)left);
    i32 top = window->GetT();
    *y = (i32)((u32)*y + (u32)top);
    window = window->GetParentWin();
  }
}

// Compute the source origin through the live inverse conversion before subtracting it.
void WN_ROOT_CLASS::ScreenToWindowCoordinates(i32 *x, i32 *y) const {
  i32 origin_x = 0, origin_y = 0;
  WindowToScreenCoordinates(&origin_x, &origin_y);
  *x = (i32)((u32)*x - (u32)origin_x);
  *y = (i32)((u32)*y - (u32)origin_y);
}

// Convert through screen coordinates only when the destination window is nonnull.
bool WN_ROOT_CLASS::WindowToWindowCoordinates(WnWindow *window, i32 *x, i32 *y) const {
  if (!window)
    return 0;
  WindowToScreenCoordinates(x, y);
  window->ScreenToWindowCoordinates(x, y);
  return 1;
}

// Return the identifier without a reference or traversal operation.
u32 WN_ROOT_CLASS::GetID() const {
  return identifier;
}

// Replace the stored identifier.
void WN_ROOT_CLASS::SetID(u32 value) {
  identifier = value;
}

// Test whether any bit of the supplied flag mask is set.
bool WN_ROOT_CLASS::GetFlag(u32 mask) const {
  return (flags & mask) != 0;
}

// Store flags before their mask-specific buffer, capture and focus effects.
u32 WN_ROOT_CLASS::SetFlag(u32 mask, bool enabled) {
  flags = enabled ? flags | mask : flags & ~mask;
  if (mask == kWnFlagVisible) {
    if (GetFlag(kWnFlagPrivateBuffer))
      PrivateBuffer(enabled);
    if (enabled)
      return flags;
  } else if (mask == kWnFlagEnabled) {
    if (enabled)
      return flags;
    if (owner->GetCapture() == this)
      owner->ReleaseCapture(this);
  } else if (mask == kWnFlagPrivateBuffer) {
    if (!enabled)
      PrivateBuffer(false);
    else if (IsVisible())
      PrivateBuffer(true);
    return flags;
  } else if (mask == kWnFlagTransparentBuffer) {
    if (GetFlag(kWnFlagPrivateBuffer)) {
      SetPrivateBufferTransparency(enabled);
      InvalidateSelfAndParents();
    }
    return flags;
  } else
    return flags;
  if (GetFocus() == this)
    SetFocusTo(notification_target);
  return flags;
}

// Set the visible flag through the live flag operation.
// The genuine visibility command returns no value, including when an override leaves a byte in the return register.
void WN_ROOT_CLASS::ShowWindow() {
  SetFlag(kWnFlagVisible, true);
}

// Clear the visible flag through the live flag operation.
// Discard the flag operation's result as required by the original void visibility command.
void WN_ROOT_CLASS::HideWindow() {
  SetFlag(kWnFlagVisible, false);
}

// Read visibility through the live flag getter.
bool WN_ROOT_CLASS::IsVisible() const {
  return GetFlag(kWnFlagVisible);
}

// Read enablement through the live flag getter.
bool WN_ROOT_CLASS::IsEnabled() const {
  return GetFlag(kWnFlagEnabled);
}

// Return the embedded caption as the original borrowed read-only interface.
const ScStringView *WN_ROOT_CLASS::GetCaption() const {
  return &caption;
}

// Copy through the original temporary before assigning the embedded caption.
void WN_ROOT_CLASS::SetCaption(const ScStringView &text) {
  u32 length = text.Strlen();
  const char *bytes = text.Data();
  ScString temporary(bytes, length);
  caption = temporary;
}

// Read the stored key equivalent.
u32 WN_ROOT_CLASS::GetKeyEquivalent() {
  return key_equivalent;
}

// Replace the stored key equivalent.
void WN_ROOT_CLASS::SetKeyEquivalent(u32 value) {
  key_equivalent = value;
}

// Compose through the live virtual first, then compare the key equivalent it may have changed.
bool WN_ROOT_CLASS::CheckKeyEquivalent(u32 key, u32 modifiers) {
  u32 composed = MakeKeyEquivalent(key, modifiers);
  return key_equivalent == composed;
}

// Pack the low key word and modifier word into their original positions.
u32 WN_ROOT_CLASS::MakeKeyEquivalent(u32 key, u32 modifiers) {
  return (key & 0xFFFF) | (modifiers << 16);
}

// Return the borrowed notification target.
WnWindow *WN_ROOT_CLASS::GetNotificationTarget() const {
  return notification_target;
}

// The base paint hook has no pixels of its own and explicitly returns true.
bool WN_ROOT_CLASS::GZPaint() {
  return 1;
}

// Accumulate ancestor origins and then recompute each child's absolute area in forward list order.
void WN_ROOT_CLASS::CalcAbsoluteArea() {
  ScRect rectangle = *GetArea();
  WnWindow *ancestor = GetParentWin();
  while (ancestor) {
    i32 y = ancestor->GetT(), x = ancestor->GetL();
    rectangle.left = (i32)((u32)rectangle.left + (u32)x);
    rectangle.right = (i32)((u32)rectangle.right + (u32)x);
    rectangle.top = (i32)((u32)rectangle.top + (u32)y);
    rectangle.bottom = (i32)((u32)rectangle.bottom + (u32)y);
    ancestor = ancestor->GetParentWin();
  }
  absolute_area = rectangle;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    node->window->CalcAbsoluteArea();
  SetChildListLock(false);
}

// Mark only this window dirty.
void WN_ROOT_CLASS::InvalidateSelf() {
  dirty = true;
}

// Invoke the live local invalidation hook, then each ancestor's local hook.
void WN_ROOT_CLASS::InvalidateSelfAndParents() {
  InvalidateSelf();
  InvalidateAllParents();
}

// Return the selected drawing buffer without acquiring a reference.
GxBuffer *WN_ROOT_CLASS::GetBufferToDrawTo() {
  return draw_buffer;
}

// Return this window's private buffer without acquiring a reference.
GxBuffer *WN_ROOT_CLASS::GetPrivateBuffer() {
  return private_buffer;
}

// Publish local drawing bounds before querying ancestors; private buffers keep a separate compositing rectangle.
// Ancestor callbacks observe the initial bounds, and each origin uses the original wrapping coordinate arithmetic.
void WN_ROOT_CLASS::SetAreaToDrawTo() {
  if (private_buffer) {
    draw_area = {0, 0, (i32)((u32)area.right - (u32)area.left), (i32)((u32)area.bottom - (u32)area.top)};
    RecalcBltArea();
  } else {
    draw_area = area;
    ScRect rectangle = area;
    WnWindow *ancestor = GetParentWin();
    while (ancestor && !ancestor->GetPrivateBuffer()) {
      i32 y = ancestor->GetT(), x = ancestor->GetL();
      rectangle.left = (i32)((u32)rectangle.left + (u32)x);
      rectangle.right = (i32)((u32)rectangle.right + (u32)x);
      rectangle.top = (i32)((u32)rectangle.top + (u32)y);
      rectangle.bottom = (i32)((u32)rectangle.bottom + (u32)y);
      ancestor = ancestor->GetParentWin();
    }
    draw_area = rectangle;
  }
}

// Recalculate this window before descending through the live forward child cursor.
void WN_ROOT_CLASS::SetAreaToDrawToRecursive() {
  SetAreaToDrawTo();
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    if (node->window)
      node->window->SetAreaToDrawToRecursive();
}

// Select this window's drawing buffer before refreshing each descendant in forward list order.
void WN_ROOT_CLASS::SetBufferToDrawToRecursive() {
  SetBufferToDrawTo();
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    node->window->SetBufferToDrawToRecursive();
}

// The base window has no per-key variant properties; leave the caller's output untouched.
bool WN_ROOT_CLASS::GetParam(u32, ScVariant **) const {
  return 0;
}

// The base window rejects variant property assignment without retaining the supplied object.
bool WN_ROOT_CLASS::SetParam(u32, ScVariant *) {
  return 0;
}

// The base window has no variant properties to enumerate.
bool WN_ROOT_CLASS::EnumParams(bool (*)(WnWindow *, u32, ScVariant *, void *)) {
  return 0;
}

// Modules with recovered local constructors reuse the common window methods without a second default constructor.
#if !defined(WN_ROOT_EXTERNAL_CONSTRUCTOR)
// Resolve the manager and keyboard through the original separate cached-service acquisitions before registering.
WN_ROOT_CLASS::WN_ROOT_CLASS()
    : references(0), identifier(0), absolute_area{}, draw_area{}, children_locked(false), parent(nullptr),
      notification_target(nullptr), caption(), private_buffer(nullptr), draw_buffer(nullptr), dirty(true),
      cursor(nullptr), accelerator(nullptr), fill_color(0), child_focus(nullptr), key_equivalent(0), mouse_down_x(0),
      mouse_down_y(0), area{}, buffer_area{}, flags(kWnDefaultFlags) {
  owner = WN_ROOT_MANAGER();
  if (owner)
    keyboard = WN_ROOT_MANAGER()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}
#endif

// BABLD supplies its original module-local canvas fallback under the same explicit target switch.
#if !defined(WN_ROOT_CUSTOM_BUFFER)
// Select the first private buffer in this window's parent chain, falling back to the current manager's canvas.
void WN_ROOT_CLASS::SetBufferToDrawTo() {
  draw_buffer = GetPrivateBuffer();
  if (!draw_buffer) {
    WnWindow *ancestor = GetParentWin();
    while (ancestor) {
      GxBuffer *buffer = ancestor->GetPrivateBuffer();
      if (buffer) {
        draw_buffer = buffer;
        break;
      }
      ancestor = ancestor->GetParentWin();
    }
    if (!draw_buffer) {
      GxCanvas *canvas = WN_ROOT_MANAGER()->GetCanvas();
      if (canvas)
        draw_buffer = canvas->GetMainBufferSurface();
    }
  }
  SetAreaToDrawToRecursive();
}
#endif

// Forward screen-point lookup to the actual owner.
WnWindow *WN_ROOT_CLASS::GetWindowFromPoint(i32 x, i32 y) {
  return owner->GetWindowFromPoint(x, y);
}

// Search visible descendants in forward order before testing this window's screen-space area.
WnWindow *WN_ROOT_CLASS::GetChildWindowFromPoint(i32 x, i32 y) {
  WnWindow *found = nullptr;
  if (WN_ROOT_CLASS::GetChildCount()) {
    SetChildListLock(true);
    for (WnChild *node = children.head->next; node != children.head; node = node->next) {
      if (!node->window->GetFlag(kWnFlagVisible))
        continue;
      found = node->window->GetChildWindowFromPoint(x, y);
      if (found)
        break;
    }
    SetChildListLock(false);
  }
  if (!found && IsPointInWindowScreenCoordinates(x, y))
    return this;
  return found;
}

// Return the borrowed accelerator interface.
WnAccelerator *WN_ROOT_CLASS::GetKeyboardAccelerator() const {
  return accelerator;
}

// Forward to the current accelerator when present, otherwise return the source false result.
bool WN_ROOT_CLASS::AccelerateKeyboardMsg(ScMessage *message) {
  return accelerator ? accelerator->ProcessAccelerator(*message) : 0;
}

// Propagate eligible private-buffer changes and then move enabled children using their current coordinates.
bool WN_ROOT_CLASS::ChildPrivateBuffers(bool enabled) {
  bool failed = false;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head; node = node->next) {
    WnWindow *child = node->window;
    if (child->GetFlag(kWnFlagPrivateBuffer)) {
      bool resize = !enabled || child->IsVisible() != 0;
      if (resize && !child->PrivateBuffer(enabled))
        failed = true;
    }
    if (enabled) {
      i32 y = child->GetT(), x = child->GetL();
      child->GZWinMoveTo(x, y);
    }
  }
  SetChildListLock(false);
  return !failed;
}

// Use UI color zero as the private buffer's key, or reset the key when disabling it.
bool WN_ROOT_CLASS::SetPrivateBufferTransparency(bool enabled) {
  if (!private_buffer)
    return 0;
  if (enabled) {
    WnResources *resources = WN_ROOT_RESOURCES();
    if (!resources)
      return 0;
    u32 key = resources->GetColor(0);
    private_buffer->SetTransparency(key);
  } else
    private_buffer->RemoveTransparency();
  return 1;
}

// Acquire the module graphics service before creating or resizing the private buffer.
// Preserve early unchanged-size success and the original child-buffer and invalidation tail ordering.
bool WN_ROOT_CLASS::PrivateBuffer(bool enabled) {
  bool result = false;
  if (enabled) {
    GxGraphics *graphics = WN_ROOT_GRAPHICS();
    if (!graphics)
      return 0;
    i32 width = (i32)((u32)area.right - (u32)area.left), height;
    bool usable = width > 0;
    if (usable) {
      height = (i32)((u32)area.bottom - (u32)area.top);
      usable = height > 0;
    }
    if (!usable) {
      if (private_buffer) {
        private_buffer->Shutdown();
        private_buffer->Release();
        private_buffer = nullptr;
      }
    } else {
      if (!private_buffer)
        graphics->CreateBuffer(&private_buffer);
      if (private_buffer) {
        if (private_buffer->IsReady()) {
          u32 buffer_width = private_buffer->Width();
          if ((u32)area.right - (u32)area.left == buffer_width) {
            u32 buffer_height = private_buffer->Height();
            if ((u32)area.bottom - (u32)area.top == buffer_height)
              return 1;
          }
          private_buffer->Shutdown();
        }
        u32 mode = GetFlag(kWnFlagPrivateBufferVideoMemory) != 0;
        GxColorType format = graphics->GetScreenColorType();
        width = (i32)((u32)area.right - (u32)area.left);
        height = (i32)((u32)area.bottom - (u32)area.top);
        if (private_buffer->Init(width, height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
                static_cast<GxVramRequest>(mode), false)) {
          flags |= kWnFlagPrivateBuffer;
          if (GetFlag(kWnFlagTransparentBuffer))
            SetPrivateBufferTransparency(true);
          SetArea(area.left, area.top, area.right, area.bottom);
          result = 1;
        } else {
          private_buffer->Release();
          private_buffer = nullptr;
        }
      }
    }
  } else {
    if (private_buffer) {
      if (private_buffer->IsReady())
        private_buffer->Shutdown();
      private_buffer->Release();
      private_buffer = nullptr;
    }
    result = 1;
  }
  SetBufferToDrawToRecursive();
  ChildPrivateBuffers(enabled);
  InvalidateSelfAndParents();
  return result;
}

// Store the three-byte logical RGB color; conversion to the current raster format is deferred.
void WN_ROOT_CLASS::SetFillColor(u8 red, u8 green, u8 blue) {
  fill_color = ((u32)red << 16) | ((u32)green << 8) | blue;
}

// Unpack a graphics-format pixel through the module service before storing its logical RGB bytes.
// Dispatch the RGB setter after conversion so derived windows retain their original override behavior.
void WN_ROOT_CLASS::SetFillColor(u32 pixel) {
  u8 red, green, blue;
  WN_ROOT_GRAPHICS()->Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  SetFillColor(red, green, blue);
}

// Extract the three stored logical RGB bytes into the original output pointers.
void WN_ROOT_CLASS::GetFillColor(u8 *red, u8 *green, u8 *blue) const {
  *red = (u8)(fill_color >> 16);
  *green = (u8)(fill_color >> 8);
  *blue = (u8)fill_color;
}

// Convert the stored logical RGB bytes through the module graphics service.
// Return the converted low word through the original32-bit pixel result.
u32 WN_ROOT_CLASS::GetFillColor() const {
  return WN_ROOT_GRAPHICS()->ConvertRGBValueToNative16Bit(
      (u8)(fill_color >> 16), (u8)(fill_color >> 8), (u8)fill_color);
}

// Clear the drawable area to the private buffer key while holding its actual write lock.
bool WN_ROOT_CLASS::FillPrivateBufferWithTransparentColor() {
  if (!private_buffer || !private_buffer->Lock(GxLockFlags::Fill))
    return 0;
  u32 key;
  if (private_buffer->GetTransparentColor(&key))
    private_buffer->Fill(key, draw_area, 0);
  private_buffer->Unlock(GxLockFlags::Fill);
  return 1;
}

// Test a keyed private-buffer pixel under its read lock and return the source Boolean result.
bool WN_ROOT_CLASS::IsPointTransparent(i32 x, i32 y) const {
  if (!private_buffer || !private_buffer->GetTransparency() || !private_buffer->Lock(GxLockFlags::GetSetPixel))
    return 0;
  bool transparent = private_buffer->IsPixelTransparent(x, y);
  private_buffer->Unlock(GxLockFlags::GetSetPixel);
  return transparent;
}

// Apply the screen-space half-open bounds test before optional per-pixel transparency testing.
bool WN_ROOT_CLASS::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  bool inside =
      x >= absolute_area.left && y >= absolute_area.top && x < absolute_area.right && y < absolute_area.bottom;
  if (GetFlag(kWnFlagHitTestTransparency) && inside) {
    ScreenToWindowCoordinates(&x, &y);
    if (private_buffer)
      return IsPointTransparent(x, y) == 0;
  }
  return inside;
}

// Paint dirty children back-to-front, then composite this window's private buffer onto its parent's drawing target.
bool WN_ROOT_CLASS::Plot() {
  u32 current_flags = flags;
  if (!(current_flags & kWnFlagVisible))
    return 0;
  if (dirty) {
    if (private_buffer) {
      if (parent)
        dirty = false;
      if ((current_flags & kWnFlagTransparentBuffer) && (current_flags & kWnFlagClearTransparentBuffer))
        FillPrivateBufferWithTransparentColor();
    }
    GZPaint();
    u32 count = WN_ROOT_CLASS::GetChildCount();
    WnChild *node = children.head;
    while (count--) {
      node = node->previous;
      node->window->Plot();
    }
  }
  if (private_buffer) {
    GxBuffer *target;
    if (parent && parent->GetBufferToDrawTo())
      target = parent->GetBufferToDrawTo();
    else
      target = WN_ROOT_MANAGER()->GetCanvas()->GetMainBufferSurface();
    if (target->Lock(GxLockFlags::Blt)) {
      target->Blt(private_buffer, draw_area, buffer_area, 0);
      target->Unlock(GxLockFlags::Blt);
    }
  }
  return 1;
}

// Make a raster-format color using the selected drawing buffer, falling back to the module graphics service.
// An unavailable fallback service supplies the original zero pixel result.
u32 WN_ROOT_CLASS::MakeFillColor(u8 red, u8 green, u8 blue) {
  if (draw_buffer)
    return draw_buffer->ConvertRGBValueToNative16Bit(red, green, blue);
  GxGraphics *graphics = WN_ROOT_GRAPHICS();
  return graphics ? graphics->ConvertRGBValueToNative16Bit(red, green, blue) : 0;
}

// Dispatch the original message types through actual leaf virtuals, narrowing scalar payloads only at consumption.
bool WN_ROOT_CLASS::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_ChildDelete:
    return GZOnChildDelete(reinterpret_cast<WnWindow *>(message->data1), (u32)message->data2);
  case kWnMsg_Command:
    return OnCommand((u32)message->data1, (u32)message->data2);
  case kWnMsg_Character:
    return GZOnCharacter((char)message->data1);
  case kWnMsg_KeyDown:
  case kWnMsg_KeyUp: {
    bool has_accelerator = accelerator != nullptr;
    bool result = message->type == kWnMsg_KeyDown ? OnKeyDown((u32)message->data1, (u32)message->data2)
                                                  : OnKeyUp((u32)message->data1, (u32)message->data2);
    if (!has_accelerator)
      return result;
    return result ? 1 : AccelerateKeyboardMsg(message);
  }
  case kWnMsg_MouseDownL:
    return OnMouseDownL((i16)message->data1, (i16)message->data2, (u32)message->data3);
  case kWnMsg_MouseDownR:
    return OnMouseDownR((i16)message->data1, (i16)message->data2, (u32)message->data3);
  case kWnMsg_MouseUpL:
    return OnMouseUpL((i16)message->data1, (i16)message->data2, (u32)message->data3);
  case kWnMsg_MouseUpR:
    return OnMouseUpR((i16)message->data1, (i16)message->data2, (u32)message->data3);
  case kWnMsg_MouseMove:
    return OnMouseMove((i16)message->data1, (i16)message->data2, (u32)message->data3);
  case kWnMsg_MouseWheel:
    return GZOnMouseWheel(
        (i32)(u32)message->data1, (i32)(u32)message->data2, (u16)((u32)message->data3 >> 16), (i16)message->data3);
  case kWnMsg_KillFocus:
    return GZOnKillFocus(reinterpret_cast<WnWindow *>(message->data1));
  case kWnMsg_SetFocus:
    return GZOnSetFocus(reinterpret_cast<WnWindow *>(message->data1));
  case kWnMsg_CaptureChanged:
    return GZOnCaptureChanged(reinterpret_cast<WnWindow *>(message->data1));
  case kWnMsg_MouseEnter:
    return GZOnMouseEnter(reinterpret_cast<WnWindow *>(message->data1));
  case kWnMsg_MouseExit:
    return GZOnMouseExit(reinterpret_cast<WnWindow *>(message->data1));
  default:
    return 0;
  }
}

// On 64-bit hosts, native-width message fields still require temporary coordinate outputs.
// Route visible hit children first, then convert the same message to local coordinates before owner hover and leaf dispatch.
bool WN_ROOT_CLASS::ProcessCursorMessage(ScMessage *message) {
  ScMsgCode type = message->type;
  for (WnChild *node = children.head->next; node != children.head; node = node->next) {
    WnWindow *child = node->window;
    if (child->IsVisible() &&
        child->IsPointInWindowScreenCoordinates((i32)(u32)message->data1, (i32)(u32)message->data2)) {
      if (type == kWnMsg_MouseMove || child->IsEnabled())
        return child->ProcessCursorMessage(message);
    }
  }
  if (!IsVisible())
    return 0;
  if (IsEnabled()) {
    if (type == kWnMsg_MouseDownL || type == kWnMsg_MouseDownR) {
      PullToFront();
      owner->SetFocus(this);
    }
    i32 x = (i32)(u32)message->data1, y = (i32)(u32)message->data2;
    ScreenToWindowCoordinates(&x, &y);
    message->data1 = (u32)x;
    message->data2 = (u32)y;
    if (type == kWnMsg_MouseMove)
      UpdateCursor();
  } else {
    if (type != kWnMsg_MouseMove)
      return 0;
    i32 x = (i32)(u32)message->data1, y = (i32)(u32)message->data2;
    ScreenToWindowCoordinates(&x, &y);
    message->data1 = (u32)x;
    message->data2 = (u32)y;
  }
  owner->SetCurrentMouseWin(this);
  return DoMessage(message);
}

// Redirect focus to the permitted notification target when this window is hidden; the event remains unhandled.
bool WN_ROOT_CLASS::GZOnSetFocus(WnWindow *) {
  if (!GetFlag(kWnFlagVisible))
    SetFocusTo(notification_target);
  return 0;
}

// The base window leaves command notifications unhandled.
bool WN_ROOT_CLASS::OnCommand(u32, u32) {
  return 0;
}

// The base window has no character handler.
bool WN_ROOT_CLASS::GZOnCharacter(char) {
  return 0;
}

// The base key-down leaf returns false before accelerator fallback.
bool WN_ROOT_CLASS::OnKeyDown(u32, u32) {
  return 0;
}

// The base key-up leaf returns false before accelerator fallback.
bool WN_ROOT_CLASS::OnKeyUp(u32, u32) {
  return 0;
}

// Leave focus-loss notification unhandled without changing focus or window state.
bool WN_ROOT_CLASS::GZOnKillFocus(WnWindow *) {
  return 0;
}

// The base window has no left-button-down leaf behavior.
bool WN_ROOT_CLASS::OnMouseDownL(i32, i32, u32) {
  return 0;
}

// The base window has no right-button-down leaf behavior.
bool WN_ROOT_CLASS::OnMouseDownR(i32, i32, u32) {
  return 0;
}

// The base window has no left-button-up leaf behavior.
bool WN_ROOT_CLASS::OnMouseUpL(i32, i32, u32) {
  return 0;
}

// The base window has no right-button-up leaf behavior.
bool WN_ROOT_CLASS::OnMouseUpR(i32, i32, u32) {
  return 0;
}

// The base window has no mouse-move leaf behavior.
bool WN_ROOT_CLASS::OnMouseMove(i32, i32, u32) {
  return 0;
}

// The base window has no mouse-wheel leaf behavior.
bool WN_ROOT_CLASS::GZOnMouseWheel(i32, i32, u32, i32) {
  return 0;
}

// Leave capture-change notification unhandled without changing the capture owner.
bool WN_ROOT_CLASS::GZOnCaptureChanged(WnWindow *) {
  return 0;
}

// Leave mouse-entry notification unhandled without changing cursor or hover state.
bool WN_ROOT_CLASS::GZOnMouseEnter(WnWindow *) {
  return 0;
}

// Leave mouse-exit notification unhandled without changing cursor or hover state.
bool WN_ROOT_CLASS::GZOnMouseExit(WnWindow *) {
  return 0;
}

// Return the first matching child, not a predicate. Caller100224C0 uses the complete result as a window interface.
WnWindow *WN_ROOT_CLASS::IsChildKeyEquivalent(u32 key, u32 modifiers) {
  WnWindow *found = nullptr;
  SetChildListLock(true);
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    if (node->window->CheckKeyEquivalent(key, modifiers)) {
      found = node->window;
      break;
    }
  SetChildListLock(false);
  return found;
}

// Lazily borrow the owner's default cursor, then activate the live cursor without acquiring a reference.
void WN_ROOT_CLASS::UpdateCursor() {
  if (!cursor)
    cursor = owner->GetCursorManager()->GetIGZCursor(0);
  cursor->SetCursor();
}

// Replace the borrowed cursor and optionally dispatch the live cursor-update virtual method.
void WN_ROOT_CLASS::SetCursor(WnCursor *value, bool active) {
  cursor = value;
  if (active)
    UpdateCursor();
}

// Replace the borrowed target used by later notifications. Register residue does not define a predicate result.
void WN_ROOT_CLASS::SetNotificationTarget(WnWindow *window) {
  notification_target = window;
}

// Release the previous accelerator before storing and retaining the supplied interface.
void WN_ROOT_CLASS::SetKeyboardAccelerator(WnAccelerator *value) {
  if (accelerator)
    accelerator->Release();
  accelerator = value;
  if (value)
    value->AddRef();
}

// Queue the original scalar message only when the owner recognizes its target window.
bool WN_ROOT_CLASS::PostMsg(WnWindow *window, u32 type, uptr first, uptr second, uptr third) {
  if (!owner->IsWindowValid(window))
    return false;
  owner->PostMessageToTarget(window, type, first, second, third);
  return true;
}

// Deliver a stack message synchronously after the same original target-validity test.
bool WN_ROOT_CLASS::SendMsg(WnWindow *window, u32 type, uptr first, uptr second, uptr third) {
  if (!owner->IsWindowValid(window))
    return false;
  ScMessage message{type, first, second, third};
  return window->DoMessage(&message);
}

// Test local half-open bounds before the original optional color-key transparency test.
bool WN_ROOT_CLASS::IsPointInWindow(i32 x, i32 y) const {
  bool inside = y >= 0 && x >= 0 && y < (i32)((u32)area.bottom - (u32)area.top) &&
      x < (i32)((u32)area.right - (u32)area.left);
  if (GetFlag(kWnFlagHitTestTransparency) && inside && private_buffer)
    return !IsPointTransparent(x, y);
  return inside;
}

// Test the stored half-open window area directly in its parent's coordinate space.
// This predicate does not consult the local color-key transparency path.
bool WN_ROOT_CLASS::IsPointInWindowParentCoordinates(i32 x, i32 y) const {
  return x >= area.left && y >= area.top && x < area.right && y < area.bottom;
}

// Give this window focus through its live manager and forward the manager's actual window result.
// The returned window is borrowed; this helper does not query or retain another interface.
WnWindow *WN_ROOT_CLASS::SetFocus() {
  return owner->SetFocus(this);
}

// Validate the requested target before assigning focus through the live manager.
// Ignore the manager's window result and return whether validation allowed the operation.
bool WN_ROOT_CLASS::SetFocusTo(WnWindow *window) {
  if (!IsWindowValid(window))
    return false;
  owner->SetFocus(window);
  return true;
}

// Borrow the manager's currently focused window without changing focus or reference counts.
WnWindow *WN_ROOT_CLASS::GetFocus() {
  return owner->GetFocus();
}

// Request capture for this window through its actual manager operation.
// Return the manager's genuine Boolean result without adding a validity test.
bool WN_ROOT_CLASS::GZSetCapture() {
  return owner->SetCapture(this);
}

// Ask the live manager to release capture for this exact window.
// Preserve the original receiver argument and return the manager's Boolean result.
bool WN_ROOT_CLASS::GZReleaseCapture() {
  return owner->ReleaseCapture(this);
}

// Ask this window's manager whether the supplied window is in its live validity set.
// The original const helper does not retain or otherwise inspect the target.
bool WN_ROOT_CLASS::IsWindowValid(WnWindow *window) const {
  return owner->IsWindowValid(window);
}

// Borrow the actual cursor manager from this window's current manager.
WnCursorManager *WN_ROOT_CLASS::GetCursorManager() {
  return owner->GetCursorManager();
}

// Forward the requested child to the live child-deletion virtual and return its Boolean result.
// Preserve the original second DWORD in the member contract even though this body does not consume it.
bool WN_ROOT_CLASS::GZOnChildDelete(WnWindow *child, u32) {
  return ChildDelete(child);
}

// Query the initialized flag through the live flag getter rather than reading the stored word directly.
// Return the genuine Boolean result without changing initialization state.
bool WN_ROOT_CLASS::GetInitFlag() const {
  return GetFlag(kWnFlagInitialized);
}

// Change the initialized flag through the live flag setter with the original Boolean input.
// The helper returns no value, irrespective of the flag setter's register result.
void WN_ROOT_CLASS::SetInitFlag(bool initialized) {
  SetFlag(kWnFlagInitialized, initialized);
}

// Read the original advisory child-list lock without acquiring or releasing it.
bool WN_ROOT_CLASS::GetChildListLock() const {
  return children_locked;
}

// Store both signed mouse-down coordinates in order for later dialog dragging.
// The original helper returns no value and performs no geometry or capture operation.
void WN_ROOT_CLASS::SetMouseDownOffset(i32 x, i32 y) {
  mouse_down_x = x;
  mouse_down_y = y;
}

// Invalidate successive live parents, loading each next parent only after the current callback returns.
void WN_ROOT_CLASS::InvalidateAllParents() {
  for (WnWindow *ancestor = GetParentWin(); ancestor; ancestor = ancestor->GetParentWin())
    ancestor->InvalidateSelf();
}

// Build the private buffer's compositing bounds relative to the first ancestor that owns a private buffer.
// Capture the first parent before the area getter, then query each vertical origin before its horizontal origin.
bool WN_ROOT_CLASS::RecalcBltArea() {
  if (!private_buffer)
    return false;
  WnWindow *ancestor = GetParentWin();
  GetArea(&buffer_area);
  while (ancestor && !ancestor->GetPrivateBuffer()) {
    i32 y = ancestor->GetT(), x = ancestor->GetL();
    buffer_area.left = (i32)((u32)buffer_area.left + (u32)x);
    buffer_area.right = (i32)((u32)buffer_area.right + (u32)x);
    buffer_area.top = (i32)((u32)buffer_area.top + (u32)y);
    buffer_area.bottom = (i32)((u32)buffer_area.bottom + (u32)y);
    ancestor = ancestor->GetParentWin();
  }
  return true;
}

// Replace only the borrowed manager field and report whether the newly stored pointer is nonnull.
// Internal distinguishes the original snake_case helper from its public PascalCase forwarding method.
bool WN_ROOT_CLASS::SetWindowManagerInternal(WnWindowManager *window_manager) {
  owner = window_manager;
  return window_manager != nullptr;
}

// Test whether this exact window owns the manager's current capture, without changing capture or references.
bool WN_ROOT_CLASS::GZAmCapturing() {
  return owner->GetCapture() == this;
}
