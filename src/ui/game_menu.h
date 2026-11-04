#ifndef SC3_UI_GAME_MENU_H
#define SC3_UI_GAME_MENU_H

#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "ui/controls.h"

// Menu item identities resolve through MenuItem.INI and its installed caption resources.
enum UiGameMenuItemId : u32 {
  kUiMenuAirportItemId = 0x10003006,
  kUiMenuBusStopItemId = 0x10004004,
  kUiMenuSubwayToRailConnectionItemId = 0x10004009,
  kUiMenuLaySubwayRailItemId = 0x10004201,
  kUiMenuSubwayStationItemId = 0x10004202,
  kUiMenuHighwayItemId = 0x10004301,
  kUiMenuOnRampItemId = 0x10004302,
  kUiMenuBudgetItemId = 0x10008001,
  kUiMenuScenarioStatusItemId = 0x10008003,
  kUiMenuCityOptionsItemId = 0x1000900a,
  kUiMenuGoToDisasterItemId = 0x1000a001,
  kUiMenuDispatchPoliceItemId = 0x1000a003,
  kUiMenuDispatchFirefightersItemId = 0x1000a004,
  kUiMenuDispatchCropDustersItemId = 0x1000a006,
};

// Menu-model notifications carry a menu item ID and, for enable requests, the new Boolean state.
enum UiGameMenuMsg : u32 {
  kWnMsg_GameMenuActivateItem = 0x25f0a91,
  kWnMsg_GameMenuSetItemEnabled = 0x25f0a92,
  kWnMsg_GameMenuSetAllEnabled = 0x25f0a94,
};

// Original cSC3MenuItemInfo object. Its two registry formats remain an explicit initialization dependency.
class UiGameMenuItem : public ScUnknown {
public:
  UiGameMenuItem();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u8 Activate();
  virtual u8 InitFromRegistry(const ScStringView &key, const ScStringView &value);
  virtual u32 GetMenuItemID();
  virtual u8 GetMenuItemName(ScStringView **out);
  virtual u8 GetMenuItemTip(ScStringView **out);
  virtual u8 GetMenuItemVersion();
  virtual u8 GetMenuItemCommandType();
  virtual u8 GetMenuItemCommandData(ScMessage *out);
  virtual u8 SetEnabled(bool enabled);
  virtual ~UiGameMenuItem();

protected:
  u32 identifier, references, words12[4];
  ScString name, tip;
  u8 version, command_type;
  ScMessage command;
  bool enabled;
  u8 InitVersion1(const ScStringView &key, const ScStringView &value);
  u8 InitVersion2(const ScStringView &key, const ScStringView &value);
};

// Original cSC3MenuDesc owns a caption and an optional array of menu item IDs.
class UiGameMenuDescriptor final : public ScUnknown {
public:
  UiGameMenuDescriptor();
  ~UiGameMenuDescriptor();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetMenuDescID();
  u8 GetMenuDescItemIDs(i32 *count, const u32 **items);
  u8 SetMenuDescFlag(i32 mask, bool enabled);
  i32 GetMenuDescFlag(i32 mask);
  u8 InitFromRegistry(const ScStringView &key, const ScStringView &value);
  u8 Shutdown();
  u8 GetMenuDescName(ScStringView *out);
  u8 GetMenuDescColors(u32 *background, u32 *highlight);

protected:
  u32 flags, references;
  ScString name;
  u32 identifier, background_color, highlight_color;
  i32 count;
  u32 *items;
};

// Original cSC3MenuBtnDef stores its own identifier, default item and associated menu description identifier.
class UiGameMenuDefinition : public ScUnknown {
public:
  UiGameMenuDefinition();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 GetMenuBtnDefID();
  virtual u32 GetDefaultMenuItemID();
  virtual u32 GetMenuDescHn();
  virtual u8 InitFromRegistry(const ScStringView &key, const ScStringView &value);
  virtual ~UiGameMenuDefinition();

protected:
  u32 identifier, references, default_item, description;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuItem) == 92);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuDescriptor) == 52);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuDefinition) == 20);

class UiGameMenuButton;
class UiGameMenuManager;

// Original cISC3WinMenuPanel secondary view. The two menu operations remain abstract until their nominal returns are known.
class UiGameMenuPanelInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 AddMenuItems(u32 identifier) = 0;
  virtual u8 ShowSubMenuOfBtn(u32 identifier) = 0;
  virtual u32 GetCurrentSubMenuBtnID() = 0;
};

// Original 192-byte game-menu panel. Concrete subclasses provide their own sizing behavior.
class UiGameMenuPanel : public UiBaseWindow, public UiGameMenuPanelInterface {
public:
  UiGameMenuPanel();
  ~UiGameMenuPanel() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u32 GetCurrentSubMenuBtnID() override;
  u8 AddMenuItems(u32 identifier) override;
  u8 ShowSubMenuOfBtn(u32 identifier) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  u32 SetFlag(u32 flag, bool enabled) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;

  // The original private menu methods expose observed AL values; their source-level bool/byte types remain unresolved.
  virtual u8 AutoSize() = 0;
  virtual u8 OnDrop(const ScRect &area);
  virtual u8 AddMenuItem(u32 identifier);
  virtual u8 AddSubItem(u32 identifier, u32 child);
  virtual u8 SetMenuPanelFlag(i32 mask, bool enabled);
  // The installed body returns a normalized full DWORD; its source-level BOOL spelling remains uncertain.
  virtual i32 GetMenuPanelFlag(i32 mask);
  virtual u32 GetHiliteColor();
  virtual u8 SetHiliteColor(u32 color);
  virtual u8 SetRootMenu(UiGameMenuPanel *panel);
  virtual u8 SetParentMenuBtn(UiGameMenuButton *button);
  virtual UiGameMenuButton *GetParentMenuBtn();
  virtual u8 HideSubMenus(bool all);
  virtual u8 HideCurrentSubMenu();
  virtual u8 SetCurrentSubMenuBtnID(u32 identifier);
  virtual u8 IsDescendent(WnWindow *window);
  virtual u8 UpdateMenuItemCheckBoxes(u32 identifier);
  virtual u8 ShutdownMenu();

protected:
  struct UiMenuNode {
    UiMenuNode *next, *previous;
    UiGameMenuButton *button;
  };

  u32 menu_flags, current_button, level;
  UiMenuNode *buttons;
  UiGameMenuPanel *root_menu;
  UiGameMenuButton *parent_button;
};

// The main panel owns the original shared manager pointer and its art state.
class UiGameMenuPanelMain : public UiGameMenuPanel {
public:
  UiGameMenuPanelMain();
  ~UiGameMenuPanelMain() override;
  static UiGameMenuManager *GetMenuMgr();
  bool Init() override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 AddMenuItems(u32 identifier) override;
  u8 AutoSize() override;
  u8 ShutdownMenu() override;
  virtual u8 SetActiveMenu(i32 identifier, GxBuffer *buffer, i32 x, i32 y);

protected:
  static UiGameMenuManager *manager;
  GxBuffer *art[4];
  ScRect art_rects[4];
  u8 art_flags[4];
  GxBuffer *active_art;
  ScRect active_rect;
  i32 active_x, active_y;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuPanel) == 192);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuPanelMain) == 304);

class GxBuffer;
struct RsKey;

// The private helper's nominal byte/Boolean return and transparency argument types remain unresolved.
u8 UiLoadGameMenuImage(const RsKey &key, GxBuffer **out, bool transparent);

class UiGameMenuItem;
class UiGameMenuDescriptor;
class UiGameMenuDefinition;
class UiGameMenuSet;

// Original cISC3MenuMgr primary interface. Private nominal byte/Boolean returns remain explicitly unresolved.
class UiGameMenuManager : public ScMessageTarget {
public:
  virtual u8 Init() = 0;
  virtual u8 Shutdown() = 0;
  virtual u8 Requery() = 0;
  virtual u32 GetMenuItemCount() = 0;
  virtual u8 GetAllMenuItemIDs(i32 *count, const u32 **items) = 0;
  virtual u8 GetMenuItemInfo(u32 identifier, UiGameMenuItem **out) = 0;
  virtual u8 AddMenuItemInfo(UiGameMenuItem *item) = 0;
  virtual u8 EnableAllItems(bool enabled) = 0;
  virtual u8 AddMenuSet(UiGameMenuSet *set) = 0;
  virtual u8 GetActiveMenuSet(UiGameMenuSet **out) = 0;
  virtual u8 GetSpecificMenuSet(u32 identifier, UiGameMenuSet **out) = 0;
  virtual u8 SetActiveMenuSet(u32 identifier) = 0;
  virtual u8 AddMenuDesc(UiGameMenuDescriptor *description) = 0;
  virtual u8 GetMenuDescItemIDs(u32 identifier, i32 *count, const u32 **items) = 0;
  virtual u8 GetMenuDesc(u32 identifier, UiGameMenuDescriptor **out) = 0;
  virtual u8 GetSpecificMenuDesc(u32 identifier, UiGameMenuDescriptor **out) = 0;
  virtual u8 AddMenuBtnDef(UiGameMenuDefinition *definition) = 0;
  virtual u8 GetMenuBtnDef(u32 identifier, UiGameMenuDefinition **out) = 0;
};

class ScStringView;
class UiGameMenuDescriptor;
class UiGameMenuManager;

// Original cISC3MenuSet order. Private byte returns retain the observed width pending nominal declarations.
class UiGameMenuSet : public ScUnknown {
public:
  virtual u8 InitFromRegistry(const ScStringView &key, const ScStringView &value) = 0;
  virtual u8 Shutdown() = 0;
  virtual u32 GetMenuSetID() = 0;
  virtual u8 GetMenuDesc(u32 identifier, UiGameMenuDescriptor **out) = 0;
  virtual u8 SetMenuMgr(UiGameMenuManager *manager) = 0;
  virtual ~UiGameMenuSet() = default;
};

// Original cSC3WinMenuBtn base. Concrete button art and submenu construction remain separate subclasses.
class UiGameMenuButton : public UiBaseWindow {
public:
  UiGameMenuButton();
  ~UiGameMenuButton() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  u32 SetFlag(u32 flag, bool enabled) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;

  virtual u8 SetMenuBtnFlag(i32 mask, bool enabled);
  virtual i32 GetMenuBtnFlag(i32 mask);
  virtual u8 ShowSubMenu();
  virtual u8 HideSubMenu();
  virtual u8 MenuBtnAddSubItem(u32 identifier);
  virtual u8 AutoSize() = 0;
  virtual u8 SetRootMenu(UiGameMenuPanel *panel);
  virtual u8 SetParentMenu(UiGameMenuPanel *panel);
  virtual UiGameMenuPanel *GetParentMenu() const;
  virtual UiGameMenuPanel *GetChildMenu() const;
  virtual u8 SetCurrentMenuItem(u32 identifier);
  virtual u32 GetCurrentMenuItemID();
  virtual u8 ShutdownButton();

protected:
  u32 menu_flags, level;
  UiGameMenuDefinition *definition;
  UiGameMenuItem *current_item;
  UiGameMenuPanel *root_menu, *parent_menu, *child_menu;
  ScString hint;
  u32 hint_token;
  u8 CreateChildPanel();
  u8 CreateChildItemsEffects(u32 identifier);
  u8 ToggleTipEffects(bool show, bool notify);
  u8 Activate();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuButton) == 216);

// The second-level panel positions its child buttons along its parent button's vertical area.
class UiGameMenuPanelLevel2 : public UiGameMenuPanel {
public:
  UiGameMenuPanelLevel2();
  ~UiGameMenuPanelLevel2() override;
  u8 AutoSize() override;
  u8 OnDrop(const ScRect &anchor) override;
};

// Deeper panels own a separate highlight color and arrange their child buttons vertically.
class UiGameMenuPanelLevelX : public UiGameMenuPanel {
public:
  UiGameMenuPanelLevelX();
  ~UiGameMenuPanelLevelX() override;
  bool Init() override;
  bool GZPaint() override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  u8 AutoSize() override;
  u8 OnDrop(const ScRect &anchor) override;
  u32 GetHiliteColor() override;
  u8 SetHiliteColor(u32 color) override;

private:
  u32 highlight;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuPanelLevel2) == 192);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuPanelLevelX) == 196);

// One original ordered-map entry: an unsigned ID and its retained object pointer.
template <class T> struct UiGameMenuEntry {
  u32 identifier;
  T *object;
};

// SIMUI's menu manager owns its ordered menu collections and their registry lifecycle.
class UiGameMenuManagerImpl : public UiGameMenuManager {
public:
  UiGameMenuManagerImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  u8 Init() override;
  u8 Shutdown() override;
  u8 Requery() override;
  u32 GetMenuItemCount() override;
  u8 GetAllMenuItemIDs(i32 *count, const u32 **items) override;
  u8 GetMenuItemInfo(u32 identifier, UiGameMenuItem **out) override;
  u8 AddMenuItemInfo(UiGameMenuItem *item) override;
  u8 EnableAllItems(bool enabled) override;
  u8 AddMenuSet(UiGameMenuSet *set) override;
  u8 GetActiveMenuSet(UiGameMenuSet **out) override;
  u8 GetSpecificMenuSet(u32 identifier, UiGameMenuSet **out) override;
  u8 SetActiveMenuSet(u32 identifier) override;
  u8 AddMenuDesc(UiGameMenuDescriptor *description) override;
  u8 GetMenuDescItemIDs(u32 identifier, i32 *count, const u32 **items) override;
  u8 GetMenuDesc(u32 identifier, UiGameMenuDescriptor **out) override;
  u8 GetSpecificMenuDesc(u32 identifier, UiGameMenuDescriptor **out) override;
  u8 AddMenuBtnDef(UiGameMenuDefinition *definition) override;
  u8 GetMenuBtnDef(u32 identifier, UiGameMenuDefinition **out) override;
  virtual ~UiGameMenuManagerImpl();

protected:
  static void AddItemFromRegistry(const ScStringView &key, const ScStringView &value, void *context);
  static void AddDescriptorFromRegistry(const ScStringView &key, const ScStringView &value, void *context);
  static void AddSetFromRegistry(const ScStringView &key, const ScStringView &value, void *context);
  static void AddDefinitionFromRegistry(const ScStringView &key, const ScStringView &value, void *context);
  bool initialized;
  u32 references;
  ScArray<UiGameMenuEntry<UiGameMenuItem>> item_map;
  ScArray<UiGameMenuEntry<UiGameMenuDescriptor>> description_map;
  ScArray<UiGameMenuEntry<UiGameMenuSet>> set_map;
  ScArray<UiGameMenuEntry<UiGameMenuDefinition>> definition_map;
  UiGameMenuSet *active_set;
  u32 *cached_item_ids;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuManagerImpl) == 68);

struct ApIniContext;

// One source menu ID and the descriptor ID to which the active set maps it.
struct UiGameMenuMapping {
  u32 source, descriptor;
};

// SIMUI's menu set owns its name, ordered mappings and optional cached mapping arrays.
class UiGameMenuSetImpl : public UiGameMenuSet {
public:
  UiGameMenuSetImpl();
  ~UiGameMenuSetImpl() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 InitFromRegistry(const ScStringView &key, const ScStringView &value) override;
  u8 Shutdown() override;
  u32 GetMenuSetID() override;
  u8 GetMenuDesc(u32 identifier, UiGameMenuDescriptor **out) override;
  u8 SetMenuMgr(UiGameMenuManager *manager) override;

protected:
  u8 AddMapping(const ScStringView &key, const ScStringView &value);
  u8 CreateMappingArrays();
  static void AddMappingFromRegistry(const ScString &key, const ScString &value, ApIniContext *context);
  u32 references;
  UiGameMenuManager *manager;
  bool initialized;
  u32 identifier;
  ScString name;
  ScArray<UiGameMenuMapping> mappings;
  u32 *source_ids, *descriptor_ids;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuSetImpl) == 60);

class GxBuffer;
class FtFont;

// The original level-two art descriptor retains flags, resource IDs and window placement.
struct UiGameMenuButtonLevel2Info {
  u32 flags, level, group, reserved, instance;
  bool transparent;
  i32 width, height, x, y;
};

struct UiGameMenuArtEntry {
  u32 group, instance;
  bool transparent;
};

// The original main-button descriptor contains six art entries and placement fields.
struct UiGameMenuButtonMainInfo {
  u32 flags, level, default_group;
  UiGameMenuArtEntry art[6];
  i32 width, height, x, y, active_x, active_y;
};

// Main buttons retain six art buffers and their associated main panel.
class UiGameMenuButtonMain : public UiGameMenuButton {
public:
  UiGameMenuButtonMain();
  ~UiGameMenuButtonMain() override;
  bool Init() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  u8 ShowSubMenu() override;
  u8 HideSubMenu() override;
  u8 AutoSize() override;
  u8 ShutdownButton() override;
  u8 SetMainPanel(UiGameMenuPanelMain *panel);

protected:
  u8 GetMenuBtnInfoMain(UiGameMenuButtonMainInfo *out);
  GxBuffer *art[6];
  u32 art_state[2];
  UiGameMenuPanelMain *main_panel;
};

// Second-level buttons retain one image buffer containing their state-specific art.
class UiGameMenuButtonLevel2 : public UiGameMenuButton {
public:
  UiGameMenuButtonLevel2();
  ~UiGameMenuButtonLevel2() override;
  bool Init() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  u8 AutoSize() override;
  u8 ShutdownButton() override;

protected:
  u8 GetMenuBtnLevel2Info(UiGameMenuButtonLevel2Info *out);
  GxBuffer *art;
};

// Deeper buttons retain their checkmark art buffer and font.
class UiGameMenuButtonLevelX : public UiGameMenuButton {
public:
  UiGameMenuButtonLevelX();
  ~UiGameMenuButtonLevelX() override;
  bool Init() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  u8 SetMenuBtnFlag(i32 mask, bool enabled) override;
  u8 AutoSize() override;
  u8 ShutdownButton() override;

protected:
  GxBuffer *art;
  i32 image_left, image_top, image_right, image_bottom;
  i32 text_x, text_y, image_x, image_y;
  FtFont *text_resource;
  u32 text_color;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuButtonMain) == 252);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuButtonLevel2) == 220);
static_assert(sizeof(uptr) != 4 || sizeof(UiGameMenuButtonLevelX) == 260);

#endif
