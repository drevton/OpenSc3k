#ifndef SC3_UI_PETITIONS_DEALS_H
#define SC3_UI_PETITIONS_DEALS_H

#include "advisors/ad_petitions.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "ui/query_dialogs.h"

// Petitioner body/portrait and choice/help/back controls, retained independently of their modal results.
enum UiPetitionControlId : u32 {
  kUiPetitionNameLabelId = 0x62be5f01,
  kUiPetitionBodyEditorId = 0x62be5f02,
  kUiPetitionImpactAnalysisLabelId = 0x62be5f03,
  kUiPetitionChoiceHeadingId = 0x62be5f04,
  kUiPetitionPrimaryActionLabelId = 0x62be5f05,
  kUiPetitionSecondaryActionLabelId = 0x62be5f06,
  kUiPetitionPortraitId = 0x62be5f07,
  kUiPetitionBodyPanelId = 0x62be5f08,
  kUiPetitionActionPanelId = 0x62be5f09,
  kUiPetitionUpperBorderId = 0x62be5f0a,
  kUiPetitionLowerBorderId = 0x62be5f0b,
  kUiPetitionFirstChoiceId = 0x62be5f0c,
  kUiPetitionSecondChoiceId = 0x62be5f0d,
  kUiPetitionHelpId = 0x62be5f0e,
  kUiPetitionDismissId = 0x62be5f0f,
};

class ScString;

// The original business-deals dialog owns a row collection, city-enumerator view and window callback.
// Initialization and list construction preserve the original resource, row and callback ordering.
class UiBusinessDeals : public UiDataRows, public UiDialog62FA574B, public UiWindowCallback {
public:
  UiBusinessDeals();
  ~UiBusinessDeals() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init() override;
  u8 EnumCityItems(CtCity *city, bool enabled) override;
  WnWindow *GetWindow() const override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;

protected:
  UiGenericInterface *window;
  // Private nominal bool/BYTE declarations are not yet established.
  u8 OnInit(WnWindow *receiver);
  u8 OnShutdown(WnWindow *receiver);
  // Both private returns follow the reached Boolean services; their source declarations still need confirmation.
  bool EndDialog(WnWindow *receiver);
  bool DoFinanceBriefing();
  u8 AddText(u32 x, u32 y, u32 width, u32 height, const ScString &text, u32 alignment);
  u8 AddButton(u32 identifier, u32 x, u32 y, u32 image_id);
  u8 FillList();
  void CenterDialogEffects();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiBusinessDeals) == 36);

class AdPetitionerHeader;
class ApListViewControl;

// Original cSC3MeetMenu is a callback owner with its own retained petitioner-header array.
// Initialization builds its advisor portraits and petitioner list through the real window and resource interfaces.
class UiMeetMenu : public UiWindowCallback {
public:
  UiMeetMenu();
  virtual ~UiMeetMenu();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 WinExists() const;
  u8 CreateWin(WnWindow **out);
  u8 KillWin(WnWindow *window);
  u8 GetWinArea(ScRect *out) const;

protected:
  i32 references;
  u8 window_exists;
  i32 advisor_count, advisor_width, advisor_height;
  u32 unclassified[6];
  ScRect area;
  i32 petitioner_count;
  AdPetitionerHeader **petitioners;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
  u8 UpdatePetList();
  u8 KillPetHeaders();
  u8 GetPetHeaders();
  u8 FillPetListView(ApListViewControl *list);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMeetMenu) == 72);

// The original petitioner callback owns a full petition record and its retained text interfaces.
// Initialization retains manager-supplied strings; response actions use a separate synchronous advisor message box.
class UiPetitionDialog : public UiWindowCallback {
public:
  UiPetitionDialog();
  virtual ~UiPetitionDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 WinExists() const;
  u8 CreateWin(WnWindow **out, bool modal);
  u8 KillWin(WnWindow *window);
  u8 GetWinArea(ScRect *out) const;
  u8 SetCurrentPetID(u32 identifier, u32 flags);

protected:
  i32 references;
  u8 window_exists;
  ScRect area;
  u32 current_petitioner, behavior_flags;
  AdPetitionerRecord petition;
  u8 response_active;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiPetitionDialog) == 108);

#endif
