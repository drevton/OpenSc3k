#ifndef SC3_WINDOW_API_H
#define SC3_WINDOW_API_H


#include "common/api.h"
#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"

// Hash growth ends at the largest prime representable by the 32-bit bucket count.
enum WnHashCapacity : u32 {
  kWnMaximumHashCapacity = 4294967291,
};

// Logical theme image identities are distinct from the installed artwork resource instance IDs registered under them.
enum WnThemeImageId : u32 {
  kWnThemeHorizontalSliderImageId = 2,
  kWnThemeSliderImageId = 3,
  kWnThemeScrollbarImageId = 4,
  kWnThemeSpinnerImageId = 9,
  kWnThemeComboArrowImageId = 10,
  kWnThemeGrayTextureImageId = 12,
  kWnThemePopupBackgroundImageId = 13,
  kWnThemeMessageBoxButtonImageId = 14,
  kWnThemeCheckBoxImageId = 15,
  kWnThemeDialogFrameImageId = 0xe2b14589,
};

// Shared theme resource IDs used for dialog backgrounds, inset panels, borders and text.
enum WnThemeColorId : u32 {
  kWnThemeBackgroundColorId = 0x62e56a2b,
  kWnThemePanelFillColorId = 0x62e56a2c,
  kWnThemeBorderColorId = 0x62e56a2d,
  kWnThemeTextColorId = 0x62e56a2e,
  kWnThemeMutedTextColorId = 0x62e56a2f,
  kWnThemeOutlineMediumColorId = 0x62e56a30,
  kWnThemeOutlineDarkColorId = 0x62e56a31,
  kWnThemeOutlineLightColorId = 0x62e56a32,
  kWnThemeSecondaryMutedColorId = 0x62e56a33,
  kWnThemeFontColorId = 0x430743b8,
};

// Standard provider palette entries and the application UI transparency/background keys.
enum WnPaletteColorId : u32 {
  kWnColorUiTransparency = 0x00000000,
  kWnColorUiBackground = 0x00000001,
  kWnColorUiWhite = 0x00000002,
  kWnColorBlack = 0x00000003,
  kWnColorDarkRed = 0x00000004,
  kWnColorDarkGreen = 0x00000005,
  kWnColorOlive = 0x00000006,
  kWnColorNavy = 0x00000007,
  kWnColorPurple = 0x00000008,
  kWnColorTeal = 0x00000009,
  kWnColorGray = 0x0000000a,
  kWnColorSilver = 0x0000000b,
  kWnColorRed = 0x0000000c,
  kWnColorLime = 0x0000000d,
  kWnColorYellow = 0x0000000e,
  kWnColorBlue = 0x0000000f,
  kWnColorCyan = 0x00000010,
  kWnColorWhite = 0x00000011,
  kWnColorCharcoal = 0x00000012,
};

// Text supplied by the window-resource provider when a caption must be shortened.
inline constexpr u32 kWnEllipsisTextId = 0x5377be31;

// Provider font slots registered by the application theme; aliases remain separate resource keys.
enum WnFontId : u32 {
  kWnFontDefault = 0x00000000,
  kWnFontButton = 0x00000001,
  kWnFontEditor = 0x00000002,
  kWnFontCaption = 0x00000003,
  kWnFontUiSystem = 0x0000000b,
  kWnFontUiTitle = 0x0000000c,
  kWnFontUiHeading = 0x0000000d,
  kWnFontUiMain = 0x0000000e,
  kWnFontUiSmall = 0x0000000f,
  kWnFontUiSerif = 0x00000010,
  kWnFontUiFileDialog = 0x00000011,
  kWnFontUiFileDialogAlternate = 0x000003e9,
  kWnFontTooltip = 0xa2cae9df,
  kWnFontUiHeadingAlias = 0xe2b14587,
  kWnFontUiSmallAlias = 0x55eb9f67,
};

// Controls resolve this reserved color through their current font or palette defaults.
inline constexpr u32 kWnDefaultColor = 0xdeadbeef;
inline constexpr u32 kWnModalFailed = 0x80000000;
inline constexpr u32 kWnModalQuit = 0x80000001;
inline constexpr u32 kWnKeyRepeatModifier = 0x40000000;
inline constexpr u32 kWnKeyWithoutRepeatMask = 0xbfffffff;

// Game key values use the virtual-key transport translated by WnSdlKey, including accelerator ranges.
inline constexpr i32 kWnKeyBackspace = 8;
inline constexpr i32 kWnKeyTab = 9;
inline constexpr i32 kWnKeyReturn = 13;
inline constexpr i32 kWnKeyShift = 16;
inline constexpr i32 kWnKeyControl = 17;
inline constexpr i32 kWnKeyAlt = 18;
inline constexpr i32 kWnKeyPause = 19;
inline constexpr i32 kWnKeyCapsLock = 20;
inline constexpr i32 kWnKeyEscape = 27;
inline constexpr i32 kWnKeySpace = 32;
inline constexpr i32 kWnKeyPageUp = 33;
inline constexpr i32 kWnKeyPageDown = 34;
inline constexpr i32 kWnKeyEnd = 35;
inline constexpr i32 kWnKeyHome = 36;
inline constexpr i32 kWnKeyLeft = 37;
inline constexpr i32 kWnKeyUp = 38;
inline constexpr i32 kWnKeyRight = 39;
inline constexpr i32 kWnKeyDown = 40;
inline constexpr i32 kWnKeyPrintScreen = 44;
inline constexpr i32 kWnKeyInsert = 45;
inline constexpr i32 kWnKeyDelete = 46;
inline constexpr i32 kWnKey0 = 48;
inline constexpr i32 kWnKey9 = 57;
inline constexpr i32 kWnKeyA = 65;
inline constexpr i32 kWnKeyC = 67;
inline constexpr i32 kWnKeyK = 75;
inline constexpr i32 kWnKeyR = 82;
inline constexpr i32 kWnKeyV = 86;
inline constexpr i32 kWnKeyY = 89;
inline constexpr i32 kWnKeyZ = 90;
inline constexpr i32 kWnKeyNumpad0 = 96;
inline constexpr i32 kWnKeyNumpad1 = 97;
inline constexpr i32 kWnKeyNumpadMultiply = 106;
inline constexpr i32 kWnKeyNumpadAdd = 107;
inline constexpr i32 kWnKeyNumpadSubtract = 109;
inline constexpr i32 kWnKeyNumpadDecimal = 110;
inline constexpr i32 kWnKeyNumpadDivide = 111;
inline constexpr i32 kWnKeyF1 = 112;
inline constexpr i32 kWnKeyF4 = 115;
inline constexpr i32 kWnKeyNumLock = 144;
inline constexpr i32 kWnKeyScrollLock = 145;

// Modifier bits shared by keyboard and mouse messages; mouse-button state occupies separate bits.
enum WnInputModifier : i32 {
  kWnModifierMouseButtonsMask = 0x30,
  kWnModifierShift = 1,
  kWnModifierControl = 2,
  kWnModifierAlt = 4,
  kWnModifierShiftControlMask = 3,
  kWnModifierKeyboardMask = 7,
  kWnModifierMouseLeft = 16,
  kWnModifierMouseRight = 32,
};

// Shared window state bits, established by the root getters, drag handlers and private-buffer operations.
inline constexpr u32 kWnFlagVisible = 0x00000001;
inline constexpr u32 kWnFlagEnabled = 0x00000002;
inline constexpr u32 kWnFlagDraggable = 0x00000100;
inline constexpr u32 kWnFlagDragging = 0x00000400;
inline constexpr u32 kWnFlagReorderable = 0x00000800;
inline constexpr u32 kWnFlagModal = 0x00001000;
inline constexpr u32 kWnFlagInitialized = 0x00004000;
inline constexpr u32 kWnFlagPrivateBuffer = 0x00010000;
inline constexpr u32 kWnFlagTransparentBuffer = 0x00020000;
inline constexpr u32 kWnFlagClearTransparentBuffer = 0x00040000;
inline constexpr u32 kWnFlagHitTestTransparency = 0x00080000;
inline constexpr u32 kWnFlagPrivateBufferVideoMemory = 0x00100000;
inline constexpr u32 kWnDefaultFlags = kWnFlagVisible | kWnFlagEnabled | kWnFlagDraggable | kWnFlagReorderable;

// Original zero-reference class constructors. Missing bodies remain explicit dependencies rather than partial catalog entries.
ScUnknown *WnConstructResources();
ScUnknown *WnConstructFlatRect();
ScUnknown *WnConstructLabel();
ScUnknown *WnConstructEditor();
ScUnknown *WnConstructMessageBox();
ScUnknown *WnConstructList();
ScUnknown *WnConstructTextList();
ScUnknown *WnConstructIconList();
ScUnknown *WnConstructCombo();
ScUnknown *WnConstructImageButton();
ScUnknown *WnConstructBitmap();
ScUnknown *WnConstructSlider();
ScUnknown *WnConstructScrollbar();
ScUnknown *WnConstructTextEdit();
ScUnknown *WnConstructOptionGroup();
ScUnknown *WnConstructSpinner();
ScUnknown *WnConstructOutline();
ScUnknown *WnConstructTicker();
ScUnknown *WnConstructKeyAccelerator();
ScUnknown *WnConstructWindowService();

class WnWindow;

// Original E2CFBCD6 cIGZWinCtrlList publishes only its primary window and minimum-size operation after Unknown.
class WnControlList : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool GetMinMaxInfo(ScRect *limits) = 0;
};

class WnWindow;
class FtFont;
class GxBuffer;
class ScStringView;

// Retail enum3C2D/field list3C2C supplies the editor's signed32 font-color roles and count sentinel.
enum class WnFontColor : i32 {
  Foreground = 0,
  Background = 1,
  DisabledForeground = 2,
  DisabledBackground = 3,
  HilitedForeground = 4,
  HilitedBackground = 5,
  Count = 6
};

inline constexpr u32 kWnEditorBackgroundFromForeground = 0x40000000;
inline constexpr u32 kWnEditorHighlightTextColor = UINT32_MAX;
inline constexpr u32 kWnInvalidIndex = UINT32_MAX;

// Recovered231A1C57 editor interface, distinct from the21335C59 line-input control.
// Overloads follow reverse declaration order for all64 installed slots; the retail audio difference remains explicit.
class WnEditorControl : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual WnWindow *AsIGZWin() = 0;
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual void GetTextArea(ScRect *output) = 0;
  virtual void SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(u32 color) = 0;
  virtual bool SetFontColor(WnFontColor index, u32 color) = 0;
  virtual bool GetFontColor(WnFontColor index, u32 *color) const = 0;
  virtual bool ClearFontColor(WnFontColor index) = 0;
  virtual void SetCaretColor(u32 color) = 0;
  virtual void SetFillColor(u32 color) = 0;
  virtual u32 GetHighlightFillColor() = 0;
  virtual void SetHighlightFillColor(u32 color) = 0;
  virtual bool ScrollLineUp() = 0;
  virtual bool ScrollLineDown() = 0;
  virtual bool ScrollToStart() = 0;
  virtual bool ScrollToEnd() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool ScrollPageDown() = 0;
  virtual u32 GetMaximumTextToAllow() = 0;
  virtual void SetMaximumTextToAllow(u32 value) = 0;
  virtual u32 GetFirstVisibleLine() = 0;
  virtual u32 GetLineCount() = 0;
  virtual u32 GetLineHeight() = 0;
  virtual void SetOutlineUse(bool enabled, u32 color) = 0;
  virtual u32 GetTextGutterX() = 0;
  virtual void SetTextGutterX(u32 value) = 0;
  virtual u32 GetTextGutterY() = 0;
  virtual void SetTextGutterY(u32 value) = 0;
  virtual bool ResizeWindowForExactLineHeights() = 0;
  virtual const char *GetText() = 0;
  virtual void GetText(ScStringView *output) = 0;
  virtual void GetText(char *output, u32 *length) = 0;
  virtual u32 GetTextLength() = 0;
  virtual bool SetText(const char *bytes, u32 count) = 0;
  virtual bool SetText(const ScStringView &value) = 0;
  virtual u32 InsertText(const char *bytes, u32 count, u32 index) = 0;
  virtual u32 InsertText(const char *bytes, u32 count, u32 line, u32 column) = 0;
  virtual bool RemoveChar(u32 index, bool move, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 index, u32 count, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *removed) = 0;
  virtual u32 FindText(const char *bytes, u32 count, u32 position, bool forward) = 0;
  virtual bool SetInsertMode(bool enabled) = 0;
  virtual bool SetInsertionIndex(u32 index) = 0;
  virtual bool SetInsertionLineAndColumn(u32 line, u32 column) = 0;
  virtual void IncrementInsertionPosition(i32 delta) = 0;
  virtual bool SetCaretPeriod(u32 milliseconds) = 0;
  virtual void SetFlag(i32 flags) = 0;
  virtual void SetFlags(i32 flags) = 0;
  virtual void ClearFlag(i32 flags) = 0;
  virtual bool IsFlagSet(i32 flags) = 0;
  virtual i32 GetFlags() = 0;
  virtual void SetMaxUndoLevelCount(u32 count) = 0;
  virtual bool Undo() = 0;
  virtual bool Redo() = 0;
  virtual bool SetScrollBarImageVertical(GxBuffer *image, const ScRect &area) = 0;
  virtual bool SetScrollBarImageHorizontal(GxBuffer *image, const ScRect &area) = 0;
  virtual bool SetSound(i32 index, u32 identifier) = 0;
  // Unlimited propagates a raw sound-device byte; the retail bool PlaySoundA(long) declaration differs.
  virtual u8 PlaySound(u32 index) = 0;
};

class WnWindow;

// Original A2CBB912 control-list-item view supplies width and height layout after its primary-window getter.
class WnListItem : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool GetMinMaxInfo(ScRect *limits) = 0;
  virtual bool LayoutW(i32 width) = 0;
  virtual bool LayoutH(i32 height) = 0;
};

class ScDirector;
class ScFramework;
class ScCom;
class ScMessageServer;
class WnWindowManager;
class WnResources;
class GxGraphics;
class FtFontSystem;
class AuSoundDevice;
class ScLanguageManager;

// Actual module and borrowed cached-service getters reached by the reconstructed window classes.
ScDirector *WnGetDirector();
ScFramework *WnGetFramework();
ScCom *WnGetCom();
ScMessageServer *WnGetMessageServer();
WnWindowManager *WnGetWindowManager();
WnResources *WnGetUiResources();
GxGraphics *WnGetGraphics();
FtFontSystem *WnGetFontManager();
AuSoundDevice *WnGetSoundDevice();
ScLanguageManager *WnGetLanguageManager();

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cGZWndProcHookData.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// Native storage for the UI provider's unsigned-key maps. Bucket/chain traversal and rehash order are game-observable
// during resource callbacks; no allocator/comparator representation or STL machinery is reproduced.
template <class T> class WnResourceMap {
public:
  struct Node {
    Node *next;
    u32 identifier;
    T value;
  };

  Node **buckets;
  u32 capacity, count;

  // Source map construction requests100 buckets, selecting the first prime193 and zeroing only bucket heads.
  WnResourceMap() : buckets(new Node *[193]{}), capacity(193), count(0) {
  }

  // Raw map destruction drops storage only. The owning provider performs its separate value-release phases first.
  ~WnResourceMap() {
    Clear();
    delete[] buckets;
  }

  // Each embedded map has one owner; copying the bucket allocation is not an ownership operation.
  WnResourceMap(const WnResourceMap &) = delete;

  // Each embedded map has one owner; assignment cannot duplicate its bucket allocation.
  WnResourceMap &operator=(const WnResourceMap &) = delete;

  // Look up the original modulo-hashed unsigned key without growing or inserting.
  Node *Find(u32 identifier) const {
    for (Node *node = buckets[identifier % capacity]; node; node = node->next)
      if (node->identifier == identifier)
        return node;
    return nullptr;
  }

  // Source subscript operations perform growth even on hits, then insert a zero value on a miss.
  Node *At(u32 identifier) {
    Reserve(count + 1);
    if (Node *node = Find(identifier))
      return node;
    u32 bucket = identifier % capacity;
    Node *node = new Node{buckets[bucket], identifier, T{}};
    buckets[bucket] = node;
    ++count;
    return node;
  }

  // Retain a reentrant insertion rather than overwriting it. Callers already performed any required resource AddRef.
  void Insert(u32 identifier, T value) {
    Reserve(count + 1);
    if (Find(identifier))
      return;
    u32 bucket = identifier % capacity;
    buckets[bucket] = new Node{buckets[bucket], identifier, value};
    ++count;
  }

  // Erase the captured source iterator after its callbacks. Source-defined execution requires that iterator remain live.
  void Erase(Node *node) {
    Node **slot = &buckets[node->identifier % capacity];
    while (*slot != node)
      slot = &(*slot)->next;
    *slot = node->next;
    delete node;
    --count;
  }

  // Return the first occupied bucket in the source traversal order.
  Node *First() const {
    for (u32 i = 0; i < capacity; ++i)
      if (buckets[i])
        return buckets[i];
    return nullptr;
  }

  // Follow the live chain, then scan subsequent buckets using the current capacity after possible callback rehashing.
  Node *Next(Node *node) const {
    if (node->next)
      return node->next;
    for (u32 i = node->identifier % capacity + 1; i < capacity; ++i)
      if (buckets[i])
        return buckets[i];
    return nullptr;
  }

  // Drop all nodes without invoking resource methods, preserving the allocation for later reuse.
  void Clear() {
    for (u32 i = 0; i < capacity; ++i)
      while (Node *node = buckets[i]) {
        buckets[i] = node->next;
        delete node;
      }
    count = 0;
  }

private:
  // Move old heads into new heads in ascending old-bucket order, retaining source chain reversal at prime growth.
  void Reserve(u32 requested) {
    static const u32 kHashPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613,
        393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189,
        805306457, 1610612741, 3221225473, kWnMaximumHashCapacity};
    if (requested <= capacity)
      return;
    u32 next_capacity = kWnMaximumHashCapacity;
    for (u32 prime : kHashPrimes)
      if (prime >= requested) {
        next_capacity = prime;
        break;
      }
    if (next_capacity <= capacity)
      return;
    Node **next = new Node *[next_capacity] {};
    for (u32 i = 0; i < capacity; ++i)
      while (Node *node = buckets[i]) {
        buckets[i] = node->next;
        u32 bucket = node->identifier % next_capacity;
        node->next = next[bucket];
        next[bucket] = node;
      }
    delete[] buckets;
    buckets = next;
    capacity = next_capacity;
  }
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class WnWindow;
class WnWindowManager;
class WnKeyboard;
class GxBuffer;
class GxPaletteView;
class ScString;
class ScStringView;
class WnCursor;
class WnCursorManager;
class WnAccelerator;
class WnAcceleratorResources;
class ScVariant;
class WnResources;
class WnResourceClient;
class WnButton;
class WnLabelControl;
class WnBitmapControl;
class WnOptionControl;
class WnScrollbarControl;
class WnSliderControl;
class WnSpinnerControl;
class WnTextControl;
class FtFont;

// Native representation of the proven Unknown+DoMessage ABI prefix shared by windows and message targets.
// The SDK declares cIGZWin from Unknown directly; this prefix base does not assert its historical C++ inheritance.
// The SDK's two buffer getters are corrected to actual pointer returns.
class WnWindow : public ScMessageTarget {
public:
  virtual bool DoMessage(ScMessage *message) = 0;
  virtual WnWindowManager *GetWindowManager() = 0;
  virtual bool SetWindowManager(WnWindowManager *owner) = 0;
  virtual WnKeyboard *GetKeyboard() = 0;
  virtual bool SetKeyboard(WnKeyboard *keyboard) = 0;
  virtual WnWindow *GetMainWindow() = 0;
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual WnWindow *GetParentWin() const = 0;
  virtual bool SetParentWin(WnWindow *parent) = 0;
  virtual bool ChildAdd(WnWindow *child) = 0;
  virtual bool ChildRemove(WnWindow *child) = 0;
  virtual bool ChildDelete(WnWindow *child) = 0;
  virtual bool ChildDeleteAbsolute(WnWindow *child) = 0;
  virtual bool ChildDeleteAll() = 0;
  virtual bool ChildExists(WnWindow *child) = 0;
  virtual bool ChildExists(u32 identifier) = 0;
  virtual bool PullToFront() = 0;
  virtual bool SendToBack() = 0;
  virtual bool ChildToFront(WnWindow *child) = 0;
  virtual bool ChildToFront(u32 identifier) = 0;
  virtual bool ChildToBack(WnWindow *child) = 0;
  virtual bool IsWinInParentChain(WnWindow *window) = 0;
  virtual bool IsWinInChildChain(WnWindow *window) = 0;
  virtual u32 GetChildCount() const = 0;
  virtual bool EnumChildren(u32, bool (*)(WnWindow *, u32, void *, void *), void *) = 0;
  virtual bool SortChildren(bool (*)(WnWindow *, WnWindow *, void *), void *, bool) = 0;
  virtual WnWindow *GetChildWindowFromID(u32 identifier) = 0;
  virtual WnWindow *GetChildWindowFromIDRecursive(u32 identifier) = 0;
  virtual bool GetChildAs(u32 identifier, u32 iid, void **out) = 0;
  virtual bool GetChildAsRecursive(u32 identifier, u32 iid, void **out) = 0;
  virtual WnWindow *GetWindowFromPoint(i32 x, i32 y) = 0;
  virtual WnWindow *GetChildWindowFromPoint(i32 x, i32 y) = 0;
  virtual i32 GetW() const = 0;
  virtual i32 GetH() const = 0;
  virtual i32 GetL() const = 0;
  virtual i32 GetT() const = 0;
  virtual i32 GetR() const = 0;
  virtual i32 GetB() const = 0;
  // Original const-reference views remain borrowed; pointer spelling preserves the read-only result.
  virtual const ScRect *GetArea() const = 0;
  virtual void GetArea(ScRect *rectangle) const = 0;
  virtual const ScRect *GetAreaAbsolute() const = 0;
  virtual void GetAreaAbsolute(ScRect *rectangle) const = 0;
  virtual void SetW(i32 width) = 0;
  virtual void SetH(i32 height) = 0;
  virtual void SetSize(i32 width, i32 height) = 0;
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual void SetArea(const ScRect &rectangle) = 0;
  virtual void GZWinMoveTo(i32 x, i32 y) = 0;
  virtual void GZWinOffset(i32 x, i32 y) = 0;
  virtual bool FitRectToWindow(ScRect *rectangle, i32 mode) = 0;
  virtual void ScreenToWindowCoordinates(i32 *x, i32 *y) const = 0;
  virtual void WindowToScreenCoordinates(i32 *x, i32 *y) const = 0;
  virtual bool WindowToWindowCoordinates(WnWindow *window, i32 *x, i32 *y) const = 0;
  virtual bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const = 0;
  virtual u32 GetID() const = 0;
  virtual void SetID(u32 identifier) = 0;
  virtual bool GetFlag(u32 flag) const = 0;
  virtual u32 SetFlag(u32 flag, bool enabled) = 0;
  virtual void ShowWindow() = 0;
  virtual void HideWindow() = 0;
  virtual bool IsVisible() const = 0;
  virtual bool IsEnabled() const = 0;
  virtual const ScStringView *GetCaption() const = 0;
  virtual void SetCaption(const ScStringView &text) = 0;
  virtual WnAccelerator *GetKeyboardAccelerator() const = 0;
  virtual void SetKeyboardAccelerator(WnAccelerator *accelerator) = 0;
  virtual bool AccelerateKeyboardMsg(ScMessage *message) = 0;
  virtual u32 GetKeyEquivalent() = 0;
  virtual void SetKeyEquivalent(u32 key) = 0;
  virtual bool CheckKeyEquivalent(u32 key, u32 modifiers) = 0;
  virtual u32 MakeKeyEquivalent(u32 key, u32 modifiers) = 0;
  virtual WnWindow *IsChildKeyEquivalent(u32 key, u32 modifiers) = 0;
  virtual bool ProcessCursorMessage(ScMessage *message) = 0;
  virtual void UpdateCursor() = 0;
  virtual void SetCursor(WnCursor *cursor, bool activate) = 0;
  virtual void SetNotificationTarget(WnWindow *window) = 0;
  virtual WnWindow *GetNotificationTarget() const = 0;
  virtual bool GZPaint() = 0;
  virtual bool Plot() = 0;
  virtual void CalcAbsoluteArea() = 0;
  virtual void InvalidateSelf() = 0;
  virtual void InvalidateSelfAndParents() = 0;
  virtual GxBuffer *GetBufferToDrawTo() = 0;
  virtual void SetBufferToDrawTo() = 0;
  virtual void SetBufferToDrawToRecursive() = 0;
  virtual void SetAreaToDrawTo() = 0;
  virtual void SetAreaToDrawToRecursive() = 0;
  virtual GxBuffer *GetPrivateBuffer() = 0;
  // Installed GZWIND 1001EFDF returns a success byte; SDK intptr_t is incorrect here.
  virtual bool PrivateBuffer(bool enabled) = 0;
  virtual void SetFillColor(u32 color) = 0;
  virtual u32 GetFillColor() const = 0;
  virtual u32 MakeFillColor(u8 red, u8 green, u8 blue) = 0;
  virtual void SetFillColor(u8 red, u8 green, u8 blue) = 0;
  virtual void GetFillColor(u8 *red, u8 *green, u8 *blue) const = 0;
  virtual bool SetParam(u32 key, ScVariant *value) = 0;
  virtual bool GetParam(u32 key, ScVariant **value) const = 0;
  virtual bool EnumParams(bool (*callback)(WnWindow *, u32, ScVariant *, void *)) = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  typedef u32 WnFlags;
};

// Original manager interface order follows the installed 59-slot table at GZWIND 1002DB80.
// Message payload words retain pointer width on native hosts, as in ScMessage.
class WnWindowManager : public ScUnknown {
public:
  virtual WnWindow *GetMainWindow() = 0;
  virtual bool SetMainWin(WnWindow *window) = 0;
  virtual bool GetMainWinAs(u32 iid, void **out) const = 0;
  virtual GxCanvas *GetCanvas() = 0;
  virtual bool AddWindowToValidList(WnWindow *window) = 0;
  virtual bool RemoveWindowFromValidList(WnWindow *window) = 0;
  virtual void CleanUpWindowReferences(WnWindow *window) = 0;
  virtual bool DestroyWindow(WnWindow *window) = 0;
  virtual bool IsWindowValid(WnWindow *window) = 0;
  virtual WnWindow *GetWindowFromPoint(i32 x, i32 y) = 0;
  virtual void InvalidateAllWindows() = 0;
  virtual void Plot() = 0;
  virtual void PauseDrawing() = 0;
  virtual void ResumeDrawing() = 0;
  virtual bool GZWinMsgPost(u32 type, uptr first, uptr second) = 0;
  virtual bool GZWinMsgPost_Mouse(u32 type, i32 x, i32 y, u32 modifiers) = 0;
  virtual bool GZWinMsgPost_Keyboard(u32 type, u32 key, u32 modifiers) = 0;
  virtual bool SendMessageToTarget(WnWindow *window, ScMessage *message) = 0;
  virtual bool SendMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) = 0;
  virtual void PostMessageToTarget(WnWindow *window, const ScMessage &message) = 0;
  virtual void PostMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) = 0;
  virtual void ProcessPostedMessages() = 0;
  virtual bool ProcessMouseMessage(ScMessage *message) = 0;
  virtual bool ProcessKeyboardMessage(ScMessage *message) = 0;
  virtual u32 FlushInputMessageQueue() = 0;
  virtual WnWindow *GetCapture() = 0;
  virtual bool SetCapture(WnWindow *window) = 0;
  virtual bool ReleaseCapture(WnWindow *window) = 0;
  virtual i32 GZShowCursor(bool visible) = 0;
  virtual WnCursorManager *GetCursorManager() = 0;
  virtual void GetCursorScreenPosition(i32 *x, i32 *y) = 0;
  virtual void GetCursorRelativePosition(WnWindow *window, i32 *x, i32 *y) = 0;
  virtual bool SetCurrentMouseWin(WnWindow *window) = 0;
  virtual WnWindow *GetFocus() = 0;
  virtual WnWindow *SetFocus(WnWindow *window) = 0;
  virtual WnKeyboard *GetKeyboard() = 0;
  virtual u32 DoModalWin(WnWindow *window) = 0;
  virtual u32 DoModalWin(WnWindow *parent, WnWindow *window) = 0;
  virtual bool IsModal() const = 0;
  virtual u32 GetModalNestCount() const = 0;
  virtual u32 GZMsgBox(
      const ScStringView &message, const ScStringView &title, u32 options, bool sound, u32 sound_id) = 0;
  virtual bool SetGZMsgBoxCMDID(u32 identifier, u32 priority, bool forced) = 0;
  virtual bool GetGZMsgBoxCMDID(u32 *identifier, u32 *priority, bool *forced) const = 0;
  virtual bool EnumRegWins(void (*callback)(u32, u32, u32, u32, bool)) = 0;
  virtual bool AddRegWin(u32 identifier, u32 first_class, u32 second_class, u32 priority, bool forced) = 0;
  virtual bool RemoveRegWin(u32 identifier) = 0;
  virtual bool RemoveAllRegWins() = 0;
  virtual bool GetRegWinInfo(
      u32 identifier, u32 *first_class, u32 *second_class, u32 *priority, bool *forced) const = 0;
  virtual bool CreateRegWins() = 0;
  virtual bool DebugIsFocusGadgetVisible() const = 0;
  virtual void DebugSetFocusGadgetVisible(bool visible) = 0;
  virtual bool DebugIsCaptureGadgetVisible() const = 0;
  virtual void DebugSetCaptureGadgetVisible(bool visible) = 0;
  virtual bool DebugIsInvalidateGadgetVisible() const = 0;
  virtual void DebugSetInvalidateGadgetVisible(bool visible) = 0;
  virtual void DebugDumpWindowList() = 0;
};

// Installed accelerator interface; the later SDK adds operations absent from this original vtable.
class WnAccelerator : public ScUnknown {
public:
  virtual bool Init(WnAcceleratorResources *resources) = 0;
  virtual bool Shutdown() = 0;
  virtual bool RegisterAccelerator(u32 modifiers, u32 key, const ScMessage &message) = 0;
  virtual bool RegisterAccelerator(u32 modifiers, u32 key, u32 command) = 0;
  virtual bool UnregisterAccelerator(u32 key) = 0;
  virtual bool ProcessAccelerator(const ScMessage &message) = 0;
  // Preserve deletion through the source accelerator's final virtual slot.
  virtual ~WnAccelerator() = default;
};

// Resource interface42E3EA4B registers its keys on the supplied actual accelerator.
class WnAcceleratorResources : public ScUnknown {
public:
  virtual bool RegisterResources(WnAccelerator *accelerator) = 0;
};

#ifdef LoadCursor
#undef LoadCursor
#endif
class WnCursor : public ScUnknown {
public:
  virtual bool LoadCursor(ScStringView *name) = 0;
  virtual void SetCursor() = 0;
  virtual void GetHotSpot(i32 *x, i32 *y) = 0;
};

// Original cursor-manager interface has 12 slots; pointer selection is a concrete WnCursors extension.
class WnCursorManager : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual bool SetCursor(u32 identifier) = 0;
  virtual bool PushAutoCursor(u32 identifier) = 0;
  virtual bool PopAutoCursor() = 0;
  virtual bool AddCursor(WnCursor *cursor, u32 identifier) = 0;
  virtual bool AddCursor(const ScStringView &name, u32 identifier) = 0;
  virtual WnCursor *GetIGZCursor(u32 identifier) = 0;
  virtual bool RemoveCursor(u32 identifier) = 0;
};

// Original control-manager table has 37 slots; each factory belongs to the concrete provider.
class WnResources : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  // The provider exchanges the common font interface, including non-bitmap implementations.
  virtual FtFont *GetFont(i32 identifier) = 0;
  virtual void SetFont(i32 identifier, FtFont *font) = 0;
  virtual GxPaletteView *SystemPalette(i32 identifier) = 0;
  virtual void SetSystemPalette(i32 identifier, GxPaletteView *palette) = 0;
  virtual GxBuffer *GetImage(i32 identifier) = 0;
  virtual void SetImage(i32 identifier, GxBuffer *image) = 0;
  virtual WnButton *CreateButton(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnButton *CreateFixedButton(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnButton *CreateToggleButton(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnButton *CreateFixedToggleButton(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnButton *CreateCheckBox(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnButton *CreateRadioButton(
      u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) = 0;
  virtual WnLabelControl *CreateLabel(u32 identifier, const ScStringView &caption) = 0;
  virtual WnOptionControl *CreateOptGrp(u32 identifier, const ScStringView &caption, const ScRect *rectangle) = 0;
  virtual WnBitmapControl *CreateWinBMP(u32 identifier, GxBuffer *image, bool fit) = 0;
  virtual WnSliderControl *CreateSlider(u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) = 0;
  virtual WnScrollbarControl *CreateScrollbar(u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) = 0;
  virtual WnSpinnerControl *CreateSpinner(u32 identifier, i32 type, i32 minimum, i32 maximum, i32 step, i32 value) = 0;
  virtual WnTextControl *CreateTextEdit(u32 identifier, u8 editable) = 0;
  virtual bool RegisterObject(WnResourceClient *client) = 0;
  virtual bool RemoveObject(WnResourceClient *client) = 0;
  virtual bool CreateObject(u32 kind, u32 iid, void **out) = 0;
  virtual void SetVolume(i32 volume) = 0;
  virtual i32 GetVolume() = 0;
  virtual u32 GetSoundId(i32 identifier) = 0;
  virtual void SetSoundId(i32 identifier, u32 sound) = 0;
  virtual u32 GetColor(i32 identifier) = 0;
  virtual void GetColor(i32 identifier, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void SetColor(i32 identifier, u32 color) = 0;
  virtual void SetColor(i32 identifier, u8 red, u8 green, u8 blue) = 0;
  virtual const ScStringView *GetText(i32 identifier) = 0;
  virtual void SetText(i32 identifier, const ScStringView *text) = 0;
};

#define WN_ROOT_CLASS WnRootWidget
#define WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR
#include "window/wn_root_widget_decl.inc"
#undef WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR
#undef WN_ROOT_CLASS

#define WN_ROOT_CLASS WnToolsRootWidget
#include "window/wn_root_widget_decl.inc"
#undef WN_ROOT_CLASS

#define WN_ROOT_CLASS WnSpriteRootWidget
#include "window/wn_root_widget_decl.inc"
#undef WN_ROOT_CLASS

#define WN_ROOT_CLASS WnArchitectRootWidget
#include "window/wn_root_widget_decl.inc"
#undef WN_ROOT_CLASS

#endif
