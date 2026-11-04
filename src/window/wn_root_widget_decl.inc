class WN_ROOT_CLASS : public WnWindow {
public:
  WN_ROOT_CLASS();
#if defined(WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR)
  WN_ROOT_CLASS(WnWindowManager *owner, WnKeyboard *keyboard);
#endif
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  WnWindowManager *GetWindowManager() override;
  bool SetWindowManager(WnWindowManager *owner) override;
  WnKeyboard *GetKeyboard() override;
  bool SetKeyboard(WnKeyboard *keyboard) override;
  WnWindow *GetMainWindow() override;
  bool Init() override;
  bool Shutdown() override;
  WnWindow *GetParentWin() const override;
  bool SetParentWin(WnWindow *parent) override;
  bool ChildAdd(WnWindow *child) override;
  bool ChildRemove(WnWindow *child) override;
  bool ChildDelete(WnWindow *child) override;
  bool ChildDeleteAbsolute(WnWindow *child) override;
  bool ChildDeleteAll() override;
  bool ChildExists(WnWindow *child) override;
  bool ChildExists(u32 identifier) override;
  bool PullToFront() override;
  bool SendToBack() override;
  bool ChildToFront(WnWindow *child) override;
  bool ChildToFront(u32 identifier) override;
  bool ChildToBack(WnWindow *child) override;
  bool IsWinInParentChain(WnWindow *window) override;
  bool IsWinInChildChain(WnWindow *window) override;
  u32 GetChildCount() const override;
  bool EnumChildren(u32, bool (*)(WnWindow *, u32, void *, void *), void *) override;
  bool SortChildren(bool (*)(WnWindow *, WnWindow *, void *), void *, bool) override;
  WnWindow *GetChildWindowFromID(u32 identifier) override;
  WnWindow *GetChildWindowFromIDRecursive(u32 identifier) override;
  bool GetChildAs(u32 identifier, u32 iid, void **out) override;
  bool GetChildAsRecursive(u32 identifier, u32 iid, void **out) override;
  WnWindow *GetWindowFromPoint(i32 x, i32 y) override;
  WnWindow *GetChildWindowFromPoint(i32 x, i32 y) override;
  i32 GetW() const override;
  i32 GetH() const override;
  i32 GetL() const override;
  i32 GetT() const override;
  i32 GetR() const override;
  i32 GetB() const override;
  const ScRect *GetArea() const override;
  void GetArea(ScRect *rectangle) const override;
  const ScRect *GetAreaAbsolute() const override;
  void GetAreaAbsolute(ScRect *rectangle) const override;
  void SetW(i32 width) override;
  void SetH(i32 height) override;
  void SetSize(i32 width, i32 height) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  void SetArea(const ScRect &rectangle) override;
  void GZWinMoveTo(i32 x, i32 y) override;
  void GZWinOffset(i32 x, i32 y) override;
  bool FitRectToWindow(ScRect *rectangle, i32 mode) override;
  void ScreenToWindowCoordinates(i32 *x, i32 *y) const override;
  void WindowToScreenCoordinates(i32 *x, i32 *y) const override;
  bool WindowToWindowCoordinates(WnWindow *window, i32 *x, i32 *y) const override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  u32 GetID() const override;
  void SetID(u32 identifier) override;
  bool GetFlag(u32 flag) const override;
  u32 SetFlag(u32 flag, bool enabled) override;
  void ShowWindow() override;
  void HideWindow() override;
  bool IsVisible() const override;
  bool IsEnabled() const override;
  const ScStringView *GetCaption() const override;
  void SetCaption(const ScStringView &text) override;
  WnAccelerator *GetKeyboardAccelerator() const override;
  void SetKeyboardAccelerator(WnAccelerator *accelerator) override;
  bool AccelerateKeyboardMsg(ScMessage *message) override;
  u32 GetKeyEquivalent() override;
  void SetKeyEquivalent(u32 key) override;
  bool CheckKeyEquivalent(u32 key, u32 modifiers) override;
  u32 MakeKeyEquivalent(u32 key, u32 modifiers) override;
  WnWindow *IsChildKeyEquivalent(u32 key, u32 modifiers) override;
  bool ProcessCursorMessage(ScMessage *message) override;
  void UpdateCursor() override;
  void SetCursor(WnCursor *cursor, bool activate) override;
  void SetNotificationTarget(WnWindow *window) override;
  WnWindow *GetNotificationTarget() const override;
  bool GZPaint() override;
  bool Plot() override;
  void CalcAbsoluteArea() override;
  void InvalidateSelf() override;
  void InvalidateSelfAndParents() override;
  GxBuffer *GetBufferToDrawTo() override;
  void SetBufferToDrawTo() override;
  void SetBufferToDrawToRecursive() override;
  void SetAreaToDrawTo() override;
  void SetAreaToDrawToRecursive() override;
  GxBuffer *GetPrivateBuffer() override;
  // Installed GZWIND 1001EFDF returns a success byte; SDK intptr_t is incorrect here.
  bool PrivateBuffer(bool enabled) override;
  void SetFillColor(u32 color) override;
  u32 GetFillColor() const override;
  u32 MakeFillColor(u8 red, u8 green, u8 blue) override;
  void SetFillColor(u8 red, u8 green, u8 blue) override;
  void GetFillColor(u8 *red, u8 *green, u8 *blue) const override;
  bool SetParam(u32 key, ScVariant *value) override;
  bool GetParam(u32 key, ScVariant **value) const override;
  bool EnumParams(bool (*callback)(WnWindow *, u32, ScVariant *, void *)) override;
  virtual void SetSize(const ScPoint &size);
  virtual void CenterWindowInRect(const ScRect &rectangle);
  virtual void CenterWindowInRect(ScRect *rectangle);
  virtual bool IsPointInWindow(i32 x, i32 y) const;
  virtual bool IsPointInWindowParentCoordinates(i32 x, i32 y) const;
  WnWindow *SetFocus();
  bool SetFocusTo(WnWindow *window);
  WnWindow *GetFocus();
  bool GZSetCapture();
  bool GZAmCapturing();
  bool GZReleaseCapture();
  bool IsWindowValid(WnWindow *window) const;
  WnCursorManager *GetCursorManager();
  bool GZOnChildDelete(WnWindow *child, u32 unused);
  void InvalidateAllParents();
  bool PostMsg(WnWindow *window, u32 type, uptr first, uptr second, uptr third);
  bool SendMsg(WnWindow *window, u32 type, uptr first, uptr second, uptr third);

protected:
  struct WnChild {
    WnChild *next, *previous;
    WnWindow *window;
  };

  struct WnChildren {
    WnChildren();
    ~WnChildren();
    WnChild *head;
  };

  WnWindowManager *owner;
  WnKeyboard *keyboard;
  u32 references, identifier;
  ScRect absolute_area;

  // The original rectangle exposes a TopLeft point reference at the same address.
  union {
    ScRect draw_area;
    ScPoint draw_origin;
  };

  WnChildren children;
  bool children_locked;
  WnWindow *parent, *notification_target;
  ScString caption;
  GxBuffer *private_buffer, *draw_buffer;
  bool dirty;
  WnCursor *cursor;
  WnAccelerator *accelerator;
  u32 fill_color;
  WnWindow *child_focus;
  u32 key_equivalent;
  i32 mouse_down_x, mouse_down_y;
  ScRect area, buffer_area;
  u32 flags;

public:
  // Actual primary tables order focus gain before loss, then left/right down before left/right up.
  virtual bool GZOnCharacter(char character);
  virtual bool OnKeyDown(u32 key, u32 modifiers);
  virtual bool OnKeyUp(u32 key, u32 modifiers);
  virtual bool GZOnSetFocus(WnWindow *window);
  virtual bool GZOnKillFocus(WnWindow *window);
  virtual bool OnMouseDownL(i32 x, i32 y, u32 modifiers);
  virtual bool OnMouseDownR(i32 x, i32 y, u32 modifiers);
  virtual bool OnMouseUpL(i32 x, i32 y, u32 modifiers);
  virtual bool OnMouseUpR(i32 x, i32 y, u32 modifiers);
  virtual bool OnMouseMove(i32 x, i32 y, u32 modifiers);
  virtual bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel);
  virtual bool GZOnCaptureChanged(WnWindow *window);
  virtual bool GZOnMouseEnter(WnWindow *window);
  virtual bool GZOnMouseExit(WnWindow *window);
  virtual bool OnCommand(u32 kind, u32 command);

public:
  virtual ~WN_ROOT_CLASS();

protected:
  static bool LessfuncId(WnWindow *first, WnWindow *second, void *context);
  bool ChildAddAbsolute(WnWindow *child);
  bool ChildRemoveAbsolute(WnWindow *child);
  bool SetWindowManagerInternal(WnWindowManager *window_manager);
  bool RecalcBltArea();
  bool SetChildListLock(bool locked);
  bool GetChildListLock() const;
  bool GetInitFlag() const;
  void SetInitFlag(bool initialized);
  void SetMouseDownOffset(i32 x, i32 y);
  bool ChildPrivateBuffers(bool enabled);
  bool SetPrivateBufferTransparency(bool enabled);
  bool FillPrivateBufferWithTransparentColor();
  bool IsPointTransparent(i32 x, i32 y) const;
};
