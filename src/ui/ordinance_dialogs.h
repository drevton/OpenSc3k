#ifndef SC3_UI_ORDINANCE_DIALOGS_H
#define SC3_UI_ORDINANCE_DIALOGS_H

#include "city/api.h"
#include "common/sc3_unknown.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"

class WnWindow;

// Original cISC3WinAdvOrd is the six-slot ordinance-group view, distinct from its control-list-item view.
// SetAssociatedAdvisorID retains its observed byte result while the private nominal bool/BYTE spelling is unresolved.
class UiAdvisorOrdinanceControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 SetAssociatedAdvisorID(u32 identifier) = 0;
  virtual i32 GetValue() const = 0;
};

class AdAdvisor;

// The ordinance callback retains its last rectangle across separate window instances.
// Its real ordinance-list and advisor-group children provide population, scrolling and minimum-size negotiation.
class UiOrdinanceDialog : public UiWindowCallback {
public:
  UiOrdinanceDialog();
  virtual ~UiOrdinanceDialog();
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
  ScRect area;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
  u8 OnUpdateOrdinanceTotal(WnWindow *window);
  u8 OnGetMinMaxInfo(WnWindow *window, ScRect *out, const ScRect *current);
  u8 OnUserAreaChanged(WnWindow *window, const ScRect *rectangle);
  u8 FillOrdinanceList(WnWindow *list);
  u8 AddAdvisorOrdinances(WnWindow *list, AdAdvisor *advisor);
  static bool EnumOrdinanceControls(WnWindow *window, u32 iid, void *view, void *context);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiOrdinanceDialog) == 28);

class WnScrollbarControl;

// Original cSC3WinOrdList owns the ordinance rows' scrolling and placement independently of the ordinance callback.
class UiOrdinanceList : public UiBaseWindow, public WnControlList {
public:
  UiOrdinanceList();
  ~UiOrdinanceList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool GetMinMaxInfo(ScRect *limits) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool ChildAdd(WnWindow *child) override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  virtual u8 ScrollTo(i32 x, i32 y);
  virtual u8 SetGutters(u8 horizontal, u8 vertical);
  virtual u8 GetGutters(i32 *horizontal, i32 *vertical);
  virtual u8 SetPageSizeV(u16 size);
  virtual u8 SetLineSizeV(u16 size);
  virtual u8 SetPageSizeH(u16 size);
  virtual u8 SetLineSizeH(u16 size);

protected:
  struct UiListExtent {
    i32 width, height;
  };

  u32 list_flags;
  i32 virtual_width, virtual_height, scroll_x, scroll_y, maximum_y;
  u32 unknown192;
  u16 page_v, page_h, line_v, line_h;
  u8 gutter_x, gutter_y;
  u16 unknown206;
  u32 unknown208;
  WnScrollbarControl *vertical_scroll, *horizontal_scroll;
  u8 CalcMinVirtualSize(i32 *width, i32 *height);
  static bool EnumMinVirtualSize(WnWindow *window, u32 iid, void *view, void *context);
  bool OnLayout(i32 *width, i32 *height);
  u8 PlaceControls(i32 width, i32 height);
  u8 OnVirtualSizeChanged(i32 width, i32 height);
  u8 UpdateScrollBarVisibility(i32 width, i32 height);
  u8 CreateScrollBar(u32 id, WnScrollbarControl **out);
  u8 OnScrollH(const ScMessage &message);
  u8 OnScrollV(const ScMessage &message);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiOrdinanceList) == 220);

class WnButton;
class WnLabelControl;

// Original cSC3WinAdvOrd publishes list-item and ordinance views after its primary window.
// The borrowed resource font and retained checkbox/label rows follow the original initialization and shutdown order.
class UiAdvisorOrdinance : public UiBaseWindow, public WnListItem, public UiAdvisorOrdinanceControl {
public:
  UiAdvisorOrdinance();
  ~UiAdvisorOrdinance() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u8 SetAssociatedAdvisorID(u32 identifier) override;
  i32 GetValue() const override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool GetMinMaxInfo(ScRect *limits) override;
  bool LayoutW(i32 width) override;
  bool LayoutH(i32 height) override;

protected:
  struct UiOrdinanceLine {
    u32 identifier;
    i32 cost;
    WnButton *checkbox;
    WnLabelControl *label;
  };

  struct UiOrdinanceNode {
    UiOrdinanceNode *next, *previous;
    UiOrdinanceLine *line;
  };

  u32 associated_advisor;
  UiOrdinanceNode *lines;
  FtFont *font;
  i32 line_height;
  u8 RequeryOrdinanceLines();
  u8 AddOrdinance(CtOrdinanceLayer::CtOrdinance *ordinance);
  u8 RemoveAllOrdinanceLines();
  u8 RequeryOrdinanceCosts();
  WnButton *FindOrdinanceCheckbox(u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiAdvisorOrdinance) == 188);

#endif
