#ifndef SC3_APPLICATION_API_H
#define SC3_APPLICATION_API_H

#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "resources/resource.h"
#include "sprites/api.h"

// Cheat identities registered by the newscast and application services.
enum ApCheatId : u32 {
  kApFundCheatId = 0xc352e7ae,
  kApAdvisorCheatId = 0x352e7c2,
  kApMoreMoneyCheatId = 0x352e7cc,
  kApMaxisCheatId = 0x8352e7d6,
  kAp1234CheatId = 0x4352e7e1,
  kApSc3kCheatId = 0x4352e7ed,
  kApMayorCheatId = 0x4352e7f6,
  kApHelloCheatId = 0xa352e800,
  kApSimonSaysCheatId = 0x3546e10,
  kApSimCityCheatId = 0xe352e809,
  kApMoneyCheatId = 0xa352e814,
  kApTickerCheatId = 0xe352e822,
  kApElectronicArtsCheatId = 0x4352e828,
  kApErtsCheatId = 0x352e873,
  kApWillWrightCheatId = 0x352e87c,
  kApHelpCheatId = 0x352e885,
  kApSimCheatId = 0xe352e88f,
  kApEasterEggCheatId = 0x4352e897,
  kApLlamaCheatId = 0xc352e8a2,
  kApScurkCheatId = 0xc352e8ac,
  kApBatCheatId = 0xc352e8b4,
  kApPorntipsguzzardoCheatId = 0x8352e8bd,
  kApBroccoliCheatId = 0x2352e8c7,
  kApCrashCheatId = 0x8889284a,
};

// Model-message requests consumed by the executable newscast service, including advisor and disaster producers.
enum ApNotificationMsg : u32 {
  kApMsgClearNotifications = 0xa2fbe51a,
  kApMsgCreateNotificationWithTickerInfo = 0xa460ed02,
  kApMsgLockNotificationQueue = 0xe3865c7a,
  kApMsgUnlockNotificationQueue = 0xc3865c83,
  kApMsgCreateNotification = 0x82684130,
  kApMsgDismissNotification = 0x82684131,
};

// Runtime state publication, enumeration and idle notifications retain their actual producer contracts.
enum ApRuntimeMsg : u32 {
  kApMsgPreferencesChanged = 0x231bbf91,
  kApMsgUiStateChanged = 0x11fdb44,
  kApMsgEnumerateUiState = 0x111fdb44,
  kApMsgIdle = 0xba44eb51,
};

// Application-wide city lifetime notifications sent through the shared model-message server.
enum ApCityMsg : u32 {
  kApMsgPreCityChange = 0x311fdae5,
  kApMsgCityReady = 0x511fdae5,
  kApMsgPostCityChange = 0x632aba43,
};

// Disaster clients retain returned notification handles until dismissal, with all ones marking an unused slot.
inline constexpr u32 kApInvalidNotificationId = UINT32_MAX;

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScString;

// Original cIBoxX2405 initialization and shutdown precede signed32 Event at slot20.
// Retail EventNum and four long payloads are32-bit; native uptr payloads preserve transported writable pointers.
class ApBoxX : public ScUnknown {
public:
  virtual bool Init(ScString *) = 0;
  virtual bool Shutdown() = 0;
  virtual i32 Event(i32 event, uptr first, uptr second, uptr third, uptr fourth) = 0;
  virtual void Update(u32) = 0;
};

// Copyright (C) 2024, 2025 Nicholas Hayes
// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtCity;
class CtSchemeManager;
class WnWindow;
class RsImageOptions;
class ApBoxX;
class CtAgentTree;
class ScCheatCodeManager;
class ScStringView;
class ApPreferences;
class OcAttribCache;
class SpViewInterface;
class SpManagerView;

// Original cISC3WinCityView order from retail CodeView24BC and the installed SIMSPR interface.
// MSVC places Init(city, segment) before Init() at16/20; the generic current-view getter is at148.
class SpCityWindowView : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool Init() = 0;
  virtual bool Init(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool Save(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool Shutdown() = 0;
  virtual void FocusCameraOn(u32, u32, bool) = 0;
  virtual void ZoomIn(bool) = 0;
  virtual void ZoomOut(bool) = 0;
  virtual void RotateCameraCW(bool) = 0;
  virtual void RotateCameraCCW(bool) = 0;
  virtual void Translate(f32, f32, bool) = 0;
  virtual void SetEdgeScroll(bool) = 0;
  virtual void SetMouseScroll(i16, i16, bool) = 0;
  virtual void UpdateScroll(i16, i16) = 0;
  virtual void SetCellCursorSize(u32, u32) = 0;
  virtual bool CellCursorVisible() = 0;
  virtual void SetCellCursorVisible(bool) = 0;
  virtual void SetCostDisplay(bool, i32, i32) = 0;
  virtual void EnableCostDisplay(bool) = 0;
  virtual bool SetActiveHandler(SpViewHandler *) = 0; // SIMSPR10048A08 forwards SwitchHandler's Boolean result.
  virtual void SetActiveCursor(u32) = 0;
  virtual bool DefActiveCursor(u32 *) = 0;
  virtual bool DefOnKeyDown(u32, u32) = 0;
  virtual bool DefOnKeyUp(u32, u32) = 0;
  virtual bool DefOnMouseDownL(i32, i32, u32) = 0;
  virtual bool DefOnMouseDownR(i32, i32, u32) = 0;
  virtual bool DefOnMouseUpL(i32, i32, u32) = 0;
  virtual bool DefOnMouseUpR(i32, i32, u32) = 0;
  virtual bool DefOnMouseMove(i32, i32, u32) = 0;
  virtual bool DefOnHotKey(u32, u32, u32) = 0;
  virtual void CameraGoHome() = 0;
  virtual SpManagerView *GetViewManager() = 0;
  virtual UiCityCursor *GetCellCursorMap() const = 0;
  virtual void AnimationPause(bool) = 0;
  virtual SpViewInterface *GetCurrentView() = 0;
  virtual SpIsoView *GetIsoView() = 0;
  virtual bool SetCurrentView(SpViewInterface *) = 0;
  virtual bool SwitchHandler(i32, SpViewHandler *) = 0;
};

class AuRenderServer;
struct ApPerformanceInfo;

// Original cISC3App virtual order from retail CodeView and installed SC3U table4CF59C.
// Mutable reference parameters and returns use explicit pointers; GetInputFileName preserves its const result.
class ApGameInterface : public ScUnknown {
public:
  virtual void SetInputFileName(const ScStringView &name) = 0;
  virtual const ScStringView *GetInputFileName() = 0;
  virtual void SetRestartOnExit(bool restart) = 0;
  virtual CtCity *GetCity() = 0;
  virtual bool CreateCity() = 0;
  virtual bool PostCityInit(i32 first, i32 second, ScStringView *name) = 0;
  virtual bool ShutdownCity() = 0;
  virtual bool PreCityLoad() = 0;
  virtual ScCheatCodeManager *GetCheatCodeManager() = 0;
  virtual AuRadio *GetRadio() = 0;
  virtual ApBoxX *GetBoxX() = 0;
  virtual AuRenderServer *GetSoundRenderServer() = 0;
  virtual ApStateManager *GetAppStateManager() = 0;
  virtual CtAgentTree *GetAppAgentTypeTree() = 0;
  virtual OcAttribCache *GetAppOccupantAttribCache() = 0;
  virtual CtDefinition *GetAppCityDefinition() = 0;
  virtual void GetAppIniFileName(ScStringView *out) = 0;
  virtual void GetAppDataDirectory(ScStringView *out) = 0;
  virtual void GetAppDirectory(ScStringView *out) = 0;
  virtual RsManager *GetResourceManager() = 0;
  virtual RsImageOptions *GetBufferResourceFactory() = 0;
  virtual ApPreferences *GetAppPreferences() = 0;
  virtual ApPerformanceInfo *GetSystemPerformanceInfo() = 0;
  virtual void FixStartupResolutionValuesIfNeeded(u32 *width, u32 *height) = 0;
  virtual bool IsStartupResolutionOK(u32 width, u32 height) = 0;
  virtual UiMainWindow *GetSC3MainWindow() = 0;
  virtual SpCityWindowView *GetISC3WinCityView() = 0;
  virtual void SetWinCityView(SpCityWindowView *window) = 0;
  virtual NwInternet *GetSC3Internet() = 0;
  virtual CtSchemeManager *CitySchemeManager() = 0;
  virtual bool Quit(bool ask_exit, bool ask_save) = 0;
  virtual void ShowProgressBar(u32 total, ScStringView *text) = 0;
  virtual void UpdateProgressBar(u32 position) = 0;
  virtual void HideProgressBar() = 0;
};

class WnWindow;
struct ApNewsMessage;

// Retail cISC3Newscast is the distinct secondary interface of the application's ticker window.
class ApNewscastControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool AddNewscastMessage(ApNewsMessage *message) = 0;
  virtual bool RemoveMessage(u32 identifier) = 0;
  virtual bool ClearAll() = 0;
  virtual bool RegisterMessage(
      u32 group, u32 instance, u8 priority, u32 font_color, u32 background_color, bool flags, u32 max_repeat) = 0;
  virtual bool UnregisterMessage(u32 group, u32 instance) = 0;
};

// Original BldgReplacementMgrSettings36 stores the two filter selections and two complete resource keys.
// Retail CodeView11A8/11A9 establishes each field; padding after readiness remains outside the game data.
struct ApReplacementPrefs {
  u8 ready;
  u32 type_filter, size_filter;
  RsKey selected_building, selected_slide;
};

static_assert(sizeof(ApReplacementPrefs) == 36);
static_assert(offsetof(ApReplacementPrefs, type_filter) == 4);
static_assert(offsetof(ApReplacementPrefs, size_filter) == 8);
static_assert(offsetof(ApReplacementPrefs, selected_building) == 12);
static_assert(offsetof(ApReplacementPrefs, selected_slide) == 24);

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3AppStateManager.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original cISC3AppStateManager virtual order, including the retail-only leading state query.
class ApStateManager {

public:
  virtual u32 GetCurrentUIState() = 0;
  virtual bool SetCurrentUIState(u32) = 0;
  virtual ScMessageTarget *GetCurrentStateTarget() = 0;

  virtual bool RegisterUIState(u32, ScMessageTarget *) = 0;
  virtual bool UnregisterUIState(u32) = 0;
  virtual bool UnregisterAllUIStates() = 0;
  virtual bool EnumerateUIStates(ScMessageTarget *) = 0;

  virtual bool GetFlag(u32) = 0;
  virtual void SetFlag(u32) = 0;
  virtual void ClearAllFlags() = 0;
};

// The application's embedded tip preferences contain a byte enable flag and the last displayed resource instance.
// SC3U408F9F returns this eight-byte aggregate; constructor408A23 initializes only its two genuine fields.
struct ApTipPrefs {
  u8 enabled;
  u32 last_tip;
};

static_assert(sizeof(ApTipPrefs) == 8);
static_assert(offsetof(ApTipPrefs, last_tip) == 4);

class WnWindow;
class FtFont;
class ScStringView;

// Retail cISC3WinListView preserves the installed fifteen-slot list interface.
class ApListViewControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool SetListViewFlag(i32 mask, bool enabled) = 0;
  virtual bool GetListViewFlag(i32 mask) = 0;
  virtual bool AddColumn(i32 column, i32 width, const ScStringView *text) = 0;
  virtual bool AddItem(i32 item, i32 column, const ScStringView *text) = 0;
  virtual bool RemoveItem(i32 item) = 0;
  virtual bool RemoveAllItems() = 0;
  virtual bool SetItemText(i32 item, i32 column, const ScStringView *text) = 0;
  virtual bool GetItemText(i32 item, i32 column, const ScStringView **out) = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual bool EnsureVisibility(i32 item) = 0;
  virtual bool SetSelectedItem(i32 item) = 0;
  virtual i32 GetSelectedItemID() = 0;
  virtual i32 GetItemAt(i32 x, i32 y) = 0;
  virtual bool GetMinMaxInfo(ScRect *out) const = 0;
};

bool ScObscureString(const char *source, char *destination, const char *key);
bool ScUnobscureString(const char *source, char *destination, const char *key);

#endif
