#ifndef SC3_UI_NEIGHBOR_WINDOW_H
#define SC3_UI_NEIGHBOR_WINDOW_H

#include "city/city.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"

// Original cSC3CmdWinNeighbor adds only its modal neighbor-window operation to the command base.
class UiNeighborCommand : public UiCommand {
public:
  bool Execute() override;
};

class WnWindow;
class WnListControl;
class GxBuffer;
class ScStringView;

// Actual IID2302B3E5 primary query sink, distinct from its secondary city-enumerator and window-procedure identities.
// Complete reached0..52 prefix; private bool/BYTE returns, availability spelling and count signedness remain qualified.
class UiNeighborWindow : public ScUnknown {
public:
  virtual u8 SetupWindow() = 0;                         // Original12,518DC; no explicit inputs.
  virtual WnWindow *GetAssociatedIGZWin() const = 0;    // Original16,51C4B; borrowed window.
  virtual u32 GetNeighborCount() const = 0;             // Original20,53CD6; full DWORD.
  virtual u8 SetDealIcon(u32 type, GxBuffer *icon) = 0; // Original24,53EBD; retains actual buffer.
  virtual GxBuffer *GetDealIcon(u32 type) = 0;          // Original28,53F19; borrowed buffer.
  virtual u8 SetSelectedNeighborId(u32 neighbor) = 0;   // Original32,7B83F.
  virtual u32 GetSelectedNeighborId() = 0;              // Original36,53CDA; full DWORD.
  // Linux final b is an input, not a mutable/output reference; PC53F42 does not consume that input.
  virtual u8 GetNeighbor(u32 neighbor, ScStringView *name, u32 *population, u8 unusedInput) = 0;    // Original40.
  virtual u8 SetNeighbor(u32 neighbor, const ScStringView &name, u32 population, u8 available) = 0; // Original44.
  // MSVC reverses same-name virtual overloads: source five/six arguments emits original six48/five52.
  // Mutable String pointers preserve the actual nonconst-reference ABI; the producer copies their contents.
  virtual u8 AddNeighborDeal(u32 neighbor, u8 type, ScStringView *description, ScStringView *rate,
      ScStringView *contribution) = 0; // Original52,54082; forwards category26.
  virtual u8 AddNeighborDeal(u32 neighbor, u8 type, u32 category, ScStringView *description, ScStringView *rate,
      ScStringView *contribution) = 0;                      // Original48,5409F.
  virtual u8 RemoveNeighborDeal(u32 neighbor, u8 type) = 0; // Original56,541CB.
};

class UiNeighborWindowObject;
class CtCity;
class SpViewInterface;
struct UiNeighborDeal;

// A source deal carries its raw type, category and three independently owned String values.
struct UiNeighborDeal {
  UiNeighborDeal(u8 type, u32 category, const ScStringView &description, const ScStringView &rate,
      const ScStringView &contribution);
  u8 type;
  u32 category;
  ScString description, rate, contribution;
};

// The source neighbor record owns its name and a separate deal collection; the latter remains unresolved.
struct UiNeighborRecord {
  UiNeighborRecord(UiNeighborWindowObject *owner, u32 id, const ScStringView &name, u32 population, u8 available);
  ~UiNeighborRecord();
  u8 CreateDealWindow();
  UiNeighborWindowObject *owner;
  u32 id;
  ScString name;
  u32 population;
  ScArray<UiNeighborDeal *> deals;
  WnListControl *deal_window;
  u8 available;
};

struct UiNeighborIconEntry {
  u32 id;
  GxBuffer *icon;
};

struct UiNeighborEntry {
  u32 id;
  UiNeighborRecord *record;
};

// Actual 92-byte cSC3WinNeighbor has callback and city-enumerator identities at offsets4 and8.
// Setup, notifications and deal ownership use the recovered original window and record implementations.
class UiNeighborWindowObject : public UiNeighborWindow, public UiWindowCallback, public CtCityEnumerator {
  friend struct UiNeighborRecord;

public:
  UiNeighborWindowObject();
  virtual ~UiNeighborWindowObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 SetupWindow() override;
  WnWindow *GetAssociatedIGZWin() const override;
  u32 GetNeighborCount() const override;
  u8 SetDealIcon(u32 type, GxBuffer *icon) override;
  GxBuffer *GetDealIcon(u32 type) override;
  u8 SetSelectedNeighborId(u32 neighbor) override;
  u32 GetSelectedNeighborId() override;
  u8 GetNeighbor(u32 neighbor, ScStringView *name, u32 *population, u8 unusedInput) override;
  u8 SetNeighbor(u32 neighbor, const ScStringView &name, u32 population, u8 available) override;
  u8 AddNeighborDeal(
      u32 neighbor, u8 type, ScStringView *description, ScStringView *rate, ScStringView *contribution) override;
  u8 AddNeighborDeal(u32 neighbor, u8 type, u32 category, ScStringView *description, ScStringView *rate,
      ScStringView *contribution) override;
  u8 RemoveNeighborDeal(u32 neighbor, u8 type) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;

protected:
  u32 references;
  CtCity *city;
  SpViewInterface *view;
  UiGenericInterface *window;
  ScUnknown *held[6];
  uptr word52;
  ScArray<UiNeighborIconEntry> icons;
  ScArray<UiNeighborEntry> neighbors;
  u32 selected, word84, word88;
  u8 SetupSolidBackground();
  u8 SetupCompassWindow();
  // Installed callers consume the byte result; its private nominal return spelling remains unresolved.
  u8 SetupCoastNeighborArrows();
  u8 SetupNameEditWindow();
  u8 SetupMeetNeighborWindow();
  u8 SetupMapCityAndNeighborTextWindows();
  u8 SetupDealIconsAndIconWindows();
  u8 SetupMapControlWindow();
  u8 UpdateAllDealIcons();
  u8 UpdateDealIcons(u32 neighbor);
  u8 CreateNeighborBorder(u32 neighbor);
  u8 CreateNeighborSelection(u32 neighbor);
  u8 SetupNationalPopulationWindow();
  u32 GetNationalPopulation();
  u8 SetupNamesAndPops();
  u8 SetupDealWindows();
  u8 EnumCityItems(CtCity *target, u8 opening);
  u8 OnInit(WnWindow *recipient);
  u8 OnShutdown(WnWindow *recipient);
  u8 DoMeetNeighborSelect(WnWindow *recipient);
  u8 UpdateCityOrNeighborText(u32 neighbor, ScStringView *name, i32 population);
  u8 DoMapControlEvent(WnWindow *recipient, u32 message, uptr first, uptr second);
  int TranslateMapControlCoord(i16 x, i16 y);
  u8 DoNeighborSelect(WnWindow *recipient, u32 neighbor);
  u8 SelectNeighbor(WnWindow *recipient, u32 neighbor);
  u8 UpdateNameEditControl(WnWindow *recipient, UiNeighborRecord *record);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiNeighborWindowObject) == 92);

#endif
