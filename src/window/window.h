#ifndef SC3_WINDOW_WINDOW_H
#define SC3_WINDOW_WINDOW_H

#include <atomic>

#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "graphics/api.h"
#include "window/api.h"
#include "window/input.h"
#include "window/text_edit.h"

// Original eight-slot C12CEA13 bitmap interface, corroborated by byte-identical retail flag and alpha bodies.
class WnBitmapControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual void SetImage(GxBuffer *image) = 0;
  virtual void SetAlpha(bool enabled, f32 opacity) = 0;
  virtual bool GetBitmapFlag(i32 mask) const = 0;
  virtual void SetBitmapFlag(i32 mask, bool enabled) = 0;
};

// Bitmap window owns one source image and clips its source extent to the current window dimensions.
class WnBitmap : public WnRootWidget, public WnBitmapControl {
public:
  WnBitmap();
  ~WnBitmap() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  void SetImage(GxBuffer *image) override;
  void SetAlpha(bool enabled, f32 opacity) override;
  bool GetBitmapFlag(i32 mask) const override;
  void SetBitmapFlag(i32 mask, bool enabled) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  GxBuffer *image;
  u8 alpha_enabled;
  f32 opacity;
  ScRect source_area;
  u32 bitmap_flags;
  void SetSourceBufferAreaFromWindowArea(const ScRect &rectangle);
};

// Borrow the actual registry used to enumerate the window configuration categories.
ScRegistry *WnGetRegistry();

// Original C2AFA76F flat-control table has 20 entries, including raw flag/size getters and void color operations.
class WnFlatControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual void SetFillColor(u32 pixel) = 0;
  virtual void SetFillColor(u8 red, u8 green, u8 blue) = 0;
  virtual u32 GetFillColor() const = 0;
  virtual void GetFillColor(u8 *red, u8 *green, u8 *blue) const = 0;
  virtual u8 SetOutlineFlags(u8 flags) = 0;
  virtual u8 GetOutlineFlags() = 0;
  virtual u8 GetOutlineFlag(i32 mask) = 0;
  virtual void SetOutlineColor(u32 pixel) = 0;
  virtual void SetOutlineColor(u8 red, u8 green, u8 blue) = 0;
  virtual void SetOutlineColor(u8 index, u8 red, u8 green, u8 blue) = 0;
  virtual void SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) = 0;
  virtual void GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) = 0;
  virtual void GetOutlineColor(u8 index, u8 *red, u8 *green, u8 *blue) const = 0;
  virtual u8 SetSizeMode(u8 mode) = 0;
  virtual u8 GetSizeMode() = 0;
  virtual u8 AutoSize(const ScRect &rectangle) = 0;
};

// Actual flat rectangle uses the root's geometry and notification ownership, with two raw bytes and four logical RGB words.
class WnFlatRect : public WnRootWidget, public WnFlatControl {
public:
  WnFlatRect();
  ~WnFlatRect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  void SetFillColor(u8 red, u8 green, u8 blue) override;
  void SetFillColor(u32 pixel) override;
  void GetFillColor(u8 *red, u8 *green, u8 *blue) const override;
  u32 GetFillColor() const override;
  u8 SetOutlineFlags(u8 flags) override;
  u8 GetOutlineFlags() override;
  u8 GetOutlineFlag(i32 mask) override;
  void SetOutlineColor(u8 index, u8 red, u8 green, u8 blue) override;
  void SetOutlineColor(u8 red, u8 green, u8 blue) override;
  void SetOutlineColor(u32 pixel) override;
  void SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) override;
  void GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) override;
  void GetOutlineColor(u8 index, u8 *red, u8 *green, u8 *blue) const override;
  u8 SetSizeMode(u8 mode) override;
  u8 GetSizeMode() override;
  u8 AutoSize(const ScRect &rectangle) override;

protected:
  u8 layout, border_flags;
  u32 border_colors[4];
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
};

#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(WnFlatRect) == 188);
#endif

ScUnknown *WnConstructFlatRect();

// The original module-coupled basic object. Its Unknown identity is distinct from derived service interfaces.
class WnObject : public ScUnknown {
public:
  WnObject();
  virtual ~WnObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;

protected:
  u32 references;
};

// The actual module-coupled system-service base shared by the window manager and UI resource provider.
class WnSystemService : public ScSystemService, public WnObject {
public:
  WnSystemService(u32 identifier, i32 priority);
  ~WnSystemService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 elapsed) override;
  bool OnIdle(u32 elapsed) override;
  bool OnInstall() override;

protected:
  u32 service_id;
  i32 service_priority;
  bool running;
};

struct WnTimerJob {
  u32 identifier;
  WnTimerCallback callback;
  WnEditorTimer *context;
  u8 once;
  u32 delay, deadline;
  u8 fixed;
};

struct WnTimerNode {
  WnTimerNode *volatile next, *volatile previous;
  WnTimerJob job;
};

// Own only the actual scheduler's circular job storage; sentinel payload and source generic allocator metadata are unused.
class WnTimerList {
public:
  WnTimerList();
  ~WnTimerList();
  void Append(const WnTimerJob &job);
  void Erase(WnTimerNode *node);
  void Clear();
  WnTimerNode *head;
};

// Original global dispatcher100324A8 is an actual ScThreadTarget with an elapsed timer, worker and recursive list lock.
// Windows uses its original volatile publication; SDL builds use atomic flags and job mutations hold the lock.
class WnTimerQueue : public ScThreadTarget {
public:
  WnTimerQueue();
  ~WnTimerQueue();
  // The original job callback returns a word that the scheduler discards after delivery.
  i32 AddEvent(WnTimerCallback callback, WnEditorTimer *context, u32 delay, bool once, bool fixed);
  bool RemoveEvent(i32 identifier);
  u32 StartFunction(void *argument) override;
  WnTimerList list;
  ScStopwatch elapsed;
  ScThread worker;
  u32 next_id;
  ScLock lock;
  std::atomic<u8> stop, running;
};

extern WnTimerQueue WnTimers;

// Recovered service ownership, input, canvas and registration methods use the actual platform and canvas interfaces.
class WnWindowService : public WnSystemService, public WnWindowManager, public GxFrameListener {
public:
  WnWindowService();
  virtual ~WnWindowService();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Init() override;
  virtual bool Shutdown() override;
  u8 GSInit(GxGraphics *graphics, GxBuffer *buffer) override;
  void GSShutdown(GxGraphics *graphics) override;
  void ForceFullSystemRedraw() override;
  u8 Draw() override;
  u8 ProcessWindowMove() override;
  u8 CompleteSwitchToFullScreenMode() override;
  u8 CompleteSwitchToWindowedMode() override;

  WnWindow *GetMainWindow() override;
  virtual bool SetMainWin(WnWindow *window) override;
  virtual bool GetMainWinAs(u32 iid, void **out) const override;
  GxCanvas *GetCanvas() override;
  bool AddWindowToValidList(WnWindow *window) override;
  bool RemoveWindowFromValidList(WnWindow *window) override;
  void CleanUpWindowReferences(WnWindow *window) override;
  bool DestroyWindow(WnWindow *window) override;
  bool IsWindowValid(WnWindow *window) override;
  WnWindow *GetWindowFromPoint(i32 x, i32 y) override;
  void InvalidateAllWindows() override;
  virtual void Plot() override;
  void PauseDrawing() override;
  void ResumeDrawing() override;
  bool GZWinMsgPost(u32 type, uptr first, uptr second) override;
  virtual bool GZWinMsgPost_Mouse(u32 type, i32 x, i32 y, u32 modifiers) override;
  virtual bool GZWinMsgPost_Keyboard(u32 type, u32 key, u32 modifiers) override;
  bool SendMessageToTarget(WnWindow *window, ScMessage *message) override;
  virtual bool SendMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) override;
  virtual void PostMessageToTarget(WnWindow *window, const ScMessage &message) override;
  void PostMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) override;
  void ProcessPostedMessages() override;
  bool ProcessMouseMessage(ScMessage *message) override;
  bool ProcessKeyboardMessage(ScMessage *message) override;
  u32 FlushInputMessageQueue() override;
  WnWindow *GetCapture() override;
  bool SetCapture(WnWindow *window) override;
  bool ReleaseCapture(WnWindow *window) override;
  WnWindow *GetFocus() override;
  WnWindow *SetFocus(WnWindow *window) override;
  bool SetCurrentMouseWin(WnWindow *window) override;
  WnKeyboard *GetKeyboard() override;
  WnCursorManager *GetCursorManager() override;
  i32 GZShowCursor(bool visible) override;
  virtual void GetCursorScreenPosition(i32 *x, i32 *y) override;
  virtual void GetCursorRelativePosition(WnWindow *window, i32 *x, i32 *y) override;
  u32 DoModalWin(WnWindow *parent, WnWindow *window) override;
  u32 DoModalWin(WnWindow *window) override;
  u32 GZMsgBox(const ScStringView &message, const ScStringView &title, u32 options, bool sound, u32 sound_id) override;
  virtual bool IsModal() const override;
  virtual u32 GetModalNestCount() const override;
  virtual bool SetGZMsgBoxCMDID(u32 identifier, u32 priority, bool forced) override;
  virtual bool GetGZMsgBoxCMDID(u32 *identifier, u32 *priority, bool *forced) const override;
  virtual bool EnumRegWins(void (*callback)(u32, u32, u32, u32, bool)) override;
  virtual bool AddRegWin(u32 identifier, u32 first_class, u32 second_class, u32 priority, bool forced) override;
  virtual bool RemoveRegWin(u32 identifier) override;
  virtual bool RemoveAllRegWins() override;
  virtual bool GetRegWinInfo(u32 identifier, u32 *first_class, u32 *second_class, u32 *priority, bool *forced) const override;
  virtual bool CreateRegWins() override;
  virtual bool DebugIsFocusGadgetVisible() const override;
  virtual void DebugSetFocusGadgetVisible(bool visible) override;
  virtual bool DebugIsCaptureGadgetVisible() const override;
  virtual void DebugSetCaptureGadgetVisible(bool visible) override;
  virtual bool DebugIsInvalidateGadgetVisible() const override;
  virtual void DebugSetInvalidateGadgetVisible(bool visible) override;
  virtual void DebugDumpWindowList() override;

protected:
  struct WnRegisteredWindow {
    WnRegisteredWindow *next;
    u32 identifier, first_class, second_class, priority;
    bool forced;
  };

  struct WnValidWindow {
    WnValidWindow *next;
    WnWindow *window;
  };

  struct WnWindowRemoval {
    WnWindowRemoval *next, *previous;
    WnWindow *window;
  };

  struct WnPostedMessage {
    WnPostedMessage *next, *previous;
    WnWindow *window;
    ScMessage message;
  };

  struct WnValidWindows {
    WnValidWindows();
    ~WnValidWindows();
    WnValidWindow **buckets;
    u32 capacity, count;
  };

  struct WnWindowRemovals {
    WnWindowRemovals();
    ~WnWindowRemovals();
    WnWindowRemoval *head;
  };

  bool initialized;
  WnWindow *capture, *focus, *modal, *hover, *main_window;
  GxCanvas *canvas;
  u32 removal_depth;
  WnValidWindows valid_windows;
  WnWindowRemovals removals;
  ScFramework *framework;
  WnKeyboard keyboard;
  WnPostedMessage *messages;
  WnCursors *cursors;
  u32 mouse_x, mouse_y, mouse_modifiers, message_id, message_priority;
  bool message_forced;
  WnRegisteredWindow *classes;
  u32 modal_depth;

  virtual bool DestroyWindowNow(WnWindow *window);
  virtual bool ProcessWindowRemovals();
  virtual bool IsWindowPendingRemoval(WnWindow *window);
  virtual u32 LockWindowRemovals();
  virtual u32 UnlockWindowRemovals();
  WnValidWindow *FindValidWindow(WnWindow *window) const;
  WnValidWindow *NextValidWindow(WnValidWindow *window) const;
  void GrowValidWindows(u32 count);
  WnRegisteredWindow **FindRegisteredWindow(u32 identifier);
  void EnumRegistryEffects(const char *category, ScRegistry::ScRegistryCallback callback);
  // The configured route consumes the actual command/variant interfaces before falling back to the built-in dialog.
  bool TryMessageBox(i32 *result, const ScStringView &message, const ScStringView &title, u32 options);
};

#endif
