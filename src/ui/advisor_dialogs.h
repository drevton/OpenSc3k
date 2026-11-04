#ifndef SC3_UI_ADVISOR_DIALOGS_H
#define SC3_UI_ADVISOR_DIALOGS_H

#include <SDL3/SDL_atomic.h>

#include "application/window_controls.h"
#include "common/api.h"
#include "resources/resource.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"

// Advisor-family windows and their portrait, topic, ordinance and retirement controls.
enum UiAdvisorControlId : u32 {
  kUiAdvisorMeetingWindowId = 0x62be5dd2,
  kUiAdvisorMainWindowId = 0x62be5df0,
  kUiAdvisorMainNameId = 0x62be5df1,
  kUiAdvisorMainCaptionId = 0x62be5df2,
  kUiAdvisorMainPortraitId = 0x62be5df3,
  kUiAdvisorMainPanelId = 0x62be5df4,
  kUiAdvisorTopicListId = 0x62be5df5,
  kUiAdvisorCategoryPanelId = 0x62be5df6,
  kUiAdvisorCategoryComboId = 0x62be5df7,
  kUiAdvisorOrdinanceWindowId = 0x62be6000,
  kUiAdvisorOrdinanceCaptionId = 0x62be6001,
  kUiAdvisorOrdinanceBodyId = 0x62be6002,
  kUiAdvisorOrdinancePortraitId = 0x62be6003,
  kUiAdvisorOrdinanceFooterId = 0x62be6004,
  kUiAdvisorOrdinanceBodyPanelId = 0x62be6005,
  kUiAdvisorOrdinanceAcceptId = 0x62be6006,
  kUiAdvisorOrdinanceLeftBorderId = 0x62be6007,
  kUiAdvisorOrdinanceTopBorderId = 0x62be6008,
  kUiAdvisorOrdinanceRightBorderId = 0x62be6009,
  kUiAdvisorOrdinanceBottomBorderId = 0x62be600a,
  kUiAdvisorTopicWindowId = 0x62be5000,
  kUiAdvisorTopicNameId = 0x62be5001,
  kUiAdvisorTopicHeadingId = 0x62be5002,
  kUiAdvisorTopicBodyId = 0x62be5003,
  kUiAdvisorTopicPortraitId = 0x62be5004,
  kUiAdvisorTopicPicturePanelId = 0x62be5005,
  kUiAdvisorTopicIllustrationId = 0x62be5006,
  kUiAdvisorTopicBackPanelId = 0x62be5007,
  kUiAdvisorTopicBackId = 0x62be5009,
  kUiAdvisorTopicBodyPanelId = 0x62be500a,
  kUiAdvisorTopicIllustrationFrameId = 0x62be500b,
  kUiAdvisorTopicFirstChoiceId = 0x62be500c,
  kUiAdvisorTopicSecondChoiceId = 0x62be500d,
  kUiAdvisorTopicThirdChoiceId = 0x62be500e,
  kUiAdvisorTopicAlternatePicturePanelId = 0x62be500f,
  kUiAdvisorPetitionerWindowId = 0x62be5f00,
  kUiAdvisorRetiredWindowId = 0xdeaddead,
};

// The topic choice result is the command ID minus the original base word.
inline constexpr u32 kUiAdvisorTopicChoiceIdBase = 0x62be500b;

// Advisor window request payload bits and the category/topic words they leave available.
enum UiAdvisorRequestFlag : u32 {
  kUiAdvisorModeless = 0x80000000,
  kUiAdvisorNoBackButton = 0x40000000,
  kUiAdvisorCategoryMask = 0x7fffffff,
  kUiAdvisorTopicMask = 0x3fffffff,
};

// Local advisor behavior and button paint state use independent flag namespaces.
inline constexpr u32 kUiAdvisorBehaviorModal = 0x40000000;
inline constexpr u32 kUiAdvisorButtonPainting = 0x80000000;

// Original cISC3WinAdvBtn secondary interface; its byte-valued private results remain nominally unresolved.
class UiAdvisorButtonInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 SetAdvImage(i32 type, GxBuffer *image, const ScRect *rectangle, i32 frames) = 0;
  virtual u8 SetOutlineColors(u32 first, u32 second) = 0;
};

// Keep the source's four-byte animation word while isolating platform-specific atomic access.
struct UiAnimationOffset {
  i32 Read();
  void Write(i32 offset);
  SDL_AtomicInt value;
};

// SIMUI's advisor button owns its image, outline colors and callback timer.
class UiAdvisorButton : public UiBaseWindow, public UiAdvisorButtonInterface {
public:
  UiAdvisorButton();
  ~UiAdvisorButton() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u8 SetAdvImage(i32 type, GxBuffer *image, const ScRect *rectangle, i32 frames) override;
  u8 SetOutlineColors(u32 first, u32 second) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  virtual u8 SetAdvBtnFlag(i32 mask, bool enabled);
  virtual i32 GetAdvBtnFlag(i32 mask);

protected:
  u32 unknown168, button_flags;
  GxBuffer *image;
  i16 frames, image_type;
  ScRect source;
  u32 outline_first, outline_second;
  UiAnimationOffset animation_offset;
  u32 pressed_color, normal_color;
  ScCallbackTimer timer;
  u8 ShowTip();
  u8 HideTip();
  // The animation body's nominal private return remains unresolved; only its effects are used by the timer.
  void AnimateEffects();
  static void AnimateCallback(uptr value);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorButton) == 252);

class UiMeetMenu;
class UiAdvisorMain;
class UiAdvisorTopic;
class UiPetitionDialog;
class UiOrdinanceDialog;

// Retail CodeView cISC3AdvDlgMgr adds initialization and shutdown after the message-target prefix.
class UiAdvisorManager : public ScMessageTarget {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
};

// The original advisor-dialog manager owns five independent dialog objects and a message subscription state.
// Private modal operations retain recovered effects; their original nominal return declarations remain unresolved.
class UiAdvisorDialogManager : public UiAdvisorManager {
public:
  UiAdvisorDialogManager();
  virtual ~UiAdvisorDialogManager();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;

protected:
  // Original private slot order28..64. Callers discard results; void records effects, not nominal-return proof.
  virtual void ModalAdvisorTopic(u32 advisor, u32 topic);
  virtual void ModalAdvisorBriefing(u32 advisor, u32 topic);
  virtual void ModalAdvisorOrdinance(u32 advisor, u32 topic);
  virtual void ModalAdvisorTopicList(u32 advisor);
  virtual void ModalAdvisorBriefingList(u32 advisor);
  virtual void ModalAdvisorOrdinanceList(u32 advisor);
  virtual void ModalPetitioner(u32 identifier, u32 flags);
  virtual void ModalOrdinanceDialog();
  virtual void ModalMeetDialog();
  virtual void ModalAdvisorEndGame(u32 advisor, u32 topic);
  i32 references;
  bool active, dialog_state;
  UiMeetMenu *meet;
  UiAdvisorMain *advisor_main;
  UiAdvisorTopic *advisor_topic;
  UiPetitionDialog *petitioner;
  UiOrdinanceDialog *ordinance;
  u8 OnMeetDialog();
  u8 OnAdvisorDialog(u32 advisor, u32 flags);
  u8 OnAdvisorTopic(u32 advisor, u32 flags);
  u8 OnAdvisorBriefing(u32 advisor, u32 flags);
  u8 OnAdvisorOrdinance(u32 advisor, u32 flags);
  u8 OnPetitioner(u32 identifier, u32 flags);
  u8 OnOrdinanceDialog();
  u8 RemoveAdvisors();
  u8 RemoveOrdinances();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorDialogManager) == 32);

class AdAdvisor;
class AdHeaderRecord;
class ApListViewControl;

// Original cSC3AdvDlgMain publishes separate callback and message-target views.
// Its three retained header arrays supply the advice, ordinance and help pages.
class UiAdvisorMain : public UiWindowCallback, public ScMessageTarget {
public:
  UiAdvisorMain();
  virtual ~UiAdvisorMain();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool DoMessage(ScMessage *message) override;
  u8 WinExists() const;
  u8 CreateWin(WnWindow **out, bool modal);
  u8 KillWin(WnWindow *window);
  u8 SetCurrentAdvisorID(u32 identifier);
  u8 SetCurrentTopic(u32 identifier);
  u8 GetWinArea(ScRect *out) const;

protected:
  i32 references;
  u32 behavior_flags, current_topic;
  u8 window_exists;
  ScRect area;
  u32 current_advisor;
  u32 advice_count;
  AdHeaderRecord **advice;
  u32 ordinance_count;
  AdHeaderRecord **ordinances;
  u32 help_count;
  AdHeaderRecord **help;
  u32 advisor_mood;
  u8 OnAdviceChanged();
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 GetTopicHeaders(AdAdvisor *advisor);
  u8 KillTopicHeaders();
  u8 FillTopicListView(ApListViewControl *list, u32 topic);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorMain) == 72);

class ScStringView;

// Original cSC3AdvMsgBox borrows its text and anchor rectangle for the duration of a synchronous modal window.
class UiAdvisorMessageBox : public UiWindowCallback {
public:
  UiAdvisorMessageBox();
  virtual ~UiAdvisorMessageBox();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 WinExists() const;
  u8 CreateWin(WnWindow **out);
  u8 KillWin(WnWindow *window);
  u8 SetMessageString(ScStringView *text);
  u8 SetTitleString(ScStringView *text);
  u8 SetPicKey(const RsKey &key, i32 frames);
  u8 DoAdvMsgBox(
      ScStringView *message, ScStringView *title, const RsKey &key, i32 frames, bool left, const ScRect *anchor);

protected:
  i32 references;
  u8 window_exists;
  ScStringView *message_text, *title_text;
  const ScRect *anchor;
  bool picture_left;
  RsKey picture;
  i32 picture_frames;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorMessageBox) == 44);

// The topic callback owns its selection, saved window rectangle and two independent presentation flags.
// Initialization builds the selected topic, portrait, editor and ordinary or end-game actions.
class UiAdvisorTopic : public UiWindowCallback {
public:
  UiAdvisorTopic();
  virtual ~UiAdvisorTopic();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 WinExists() const;
  u8 CreateWin(WnWindow **out, bool modal);
  u8 KillWin(WnWindow *window, u32 result);
  u8 GetWinArea(ScRect *out) const;
  u8 SetCurrentAdvisorID(u32 identifier);
  u8 SetCurrentTopicID(u32 identifier);
  u8 SetTopicType(u32 type);
  u8 SetEndGame(bool enabled);
  bool SetParentNoBackButton(bool enabled);

protected:
  i32 references;
  u32 topic_type;
  u8 window_exists;
  ScRect area;
  u32 current_advisor, current_topic;
  bool parent_no_back_button, end_game;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorTopic) == 44);

#endif
