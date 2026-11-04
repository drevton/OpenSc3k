#ifndef SC3_GEOMETRY_TOOL_H
#define SC3_GEOMETRY_TOOL_H

#include "geometry/ge_types.h"
#include "geometry/module.h"
#include "resources/resource.h"
#include "sprites/api.h"

class WnWindowManager;
class SpCityWindowView;
class SpViewInterface;
class UiCityCursor;
class EcBudgetLayer;
class SpSelection;
class ScOccupantManager;

// The real60-byte handler base owns four retained providers and borrows the window-manager service.
// IUnknown remains abstract; the authentic view-handler suffix precedes the deleting destructor.
class GeTool : public SpViewHandler {
public:
  GeTool();
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnKeyDown(u32 key, u32 flags) override;
  bool OnKeyUp(u32 key, u32 flags) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseDownR(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpR(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  bool GetCursor(u32 *out) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  virtual ~GeTool();
  bool AttachHost(SpCityWindowView *host);
  bool DetachIfRequested(SpCityWindowView *host);
  // Native effects of private2145F/2146B/214B8; discarded nominal return declarations remain unresolved.
  void ResetCost();
  void ShowCost();
  void CheckCost();
  WnWindowManager *windows;
  SpCityWindowView *window;
  SpViewInterface *view;
  UiCityCursor *cursorMap;
  EcBudgetLayer *budget;
  GeBounds bounds;
  u8 mouseScroll, edgeScroll, cursorVisible, costVisible;
  i32 cost;
  u8 invalid, insufficientFunds, field58, detachAfterInput;
};

// Original88-byte point-gesture extension. The final three sentinel words are retained without invented meanings.
class GeDragTool : public GeTool {
public:
  GeDragTool();
  ~GeDragTool() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  virtual u8 BeginAt(const CtCoord *point);
  virtual u8 Commit(const CtCoord *point);
  virtual u8 PreviewDrag(const CtCoord *point);
  virtual u8 PreviewPoint(const CtCoord *point);
  virtual u8 PaintCursor(const CtCoord *point, GeBounds *affected);
  u8 dragging;
  CtCoord point;
  i32 field76, field80, field84;
};

// Original88-byte hover/release base directly extends GeTool; the drag-tool class is a separate sibling.
// Private callbacks use byte carriers. Original84 remains unrecovered, so this is not a foreign overlay.
class GeHoverTool : public GeTool {
public:
  GeHoverTool();
  ~GeHoverTool() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  virtual u8 BeginAt(const CtCoord *point);
  virtual u8 Commit(const CtCoord *point);
  virtual u8 PreviewPoint(const CtCoord *point);
  virtual u8 PaintCursor(const CtCoord *point, GeBounds *affected);
  virtual u8 FinishRelease(SpCityWindowView *host);
  u8 active;
  CtCoord point;
  i32 field76, field80, field84;
};

// Original140-byte area handler holds three footprint bounds and borrows the current occupant manager at136.
// Private callbacks retain raw byte carriers; original88 remains undeclared rather than receiving a guessed signature.
class GeAreaTool : public GeTool {
public:
  GeAreaTool();
  ~GeAreaTool() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  virtual u8 BeginAt(const GeBounds *area);
  virtual u8 Commit(const GeBounds *area);
  virtual u8 PreviewDrag(const GeBounds *area);
  virtual u8 PreviewHover(const GeBounds *area);
  virtual u8 PaintCursor(const GeBounds *area, GeBounds *affected);
  virtual u8 FinishRelease(SpCityWindowView *host);
  u8 active;
  GeBounds start, current, region;
  ScOccupantManager *occupants;
};

class GeBuildingLayer;

// Original288-byte placement tool combines the88-byte handler and172-byte command before its building provider and key.
// Native command storage changes its size; private nominal byte/Boolean callback declarations remain unresolved.
class GeBuildingTool : public GeDragTool, public GeCommand {
public:
  GeBuildingTool(const RsKey &key, bool singleUse, bool freeOfCharge);
  ~GeBuildingTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  u8 BeginAt(const CtCoord *point) override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
  u8 PaintCursor(const CtCoord *point, GeBounds *affected) override;

protected:
  GeBuildingLayer *buildings;
  RsKey key;
  bool freeOfCharge, singleUse;
  u8 widthMinusOne, depthMinusOne;
  u32 cursorId, soundId;
};

// Original288-byte plumbing specialization reuses the actual placement-tool base without adding fields.
class GePlumbingTool : public GeBuildingTool {
public:
  GePlumbingTool(const RsKey &key, bool singleUse, bool freeOfCharge);
  ~GePlumbingTool() override;
  bool Execute() override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  u8 BeginAt(const CtCoord *point) override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
};

// Original288-byte subway specialization reuses the actual placement-tool base without adding fields.
class GeSubwayTool : public GeBuildingTool {
public:
  GeSubwayTool(const RsKey &key, bool singleUse, bool freeOfCharge);
  ~GeSubwayTool() override;
  bool Execute() override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  u8 BeginAt(const CtCoord *point) override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
};

// Original288-byte shoreline specialization reuses the actual placement-tool base without adding fields.
class GeShoreTool : public GeBuildingTool {
public:
  GeShoreTool(const RsKey &key, bool singleUse, bool freeOfCharge);
  ~GeShoreTool() override;
  bool Execute() override;
  u8 BeginAt(const CtCoord *point) override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
};

// Original324-byte four-building tool independently combines the handler and command before four separate resource keys.
// Its actual inheritance does not pass through GeBuildingTool; native command storage reduces the complete size.
class GeMultiTool : public GeDragTool, public GeCommand {
public:
  GeMultiTool(const RsKey &first, const RsKey &second, const RsKey &third, const RsKey &fourth, bool singleUse,
      bool freeOfCharge);
  ~GeMultiTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  u8 BeginAt(const CtCoord *point) override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
  u8 PaintCursor(const CtCoord *point, GeBounds *affected) override;

protected:
  GeBuildingLayer *buildings;
  RsKey first, second, third, fourth;
  bool freeOfCharge, singleUse;
  u8 widthMinusOne, depthMinusOne;
  u32 cursorId, soundId;
};

class GeDemolitionView;

// Original336-byte tool combines the140-byte area handler and172-byte command before four retained services.
// The zero word328 is uninterpreted; unit price332 is deliberately not initialized before Execute supplies it.
class GeDemolitionTool : public GeAreaTool, public GeCommand {
public:
  GeDemolitionTool();
  ~GeDemolitionTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  u8 Commit(const GeBounds *area) override;
  u8 PreviewDrag(const GeBounds *area) override;
  u8 PreviewHover(const GeBounds *area) override;
  u8 PaintCursor(const GeBounds *area, GeBounds *affected) override;

protected:
  ScOccupantManager *surface, *undergroundOne, *undergroundTwo;
  GeDemolitionView *demolition;
  u32 field328, unitCost;
};

// Original constructor installs the hover handler at0 and command at88 without initializing further fields.
// Dispatcher requests272 bytes; native command storage is smaller than its original library representation.
// Unexplained allocation tail is neither read nor given invented fields.
class GeLocationTool : public GeHoverTool, public GeCommand {
public:
  GeLocationTool();
  ~GeLocationTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  u8 Commit(const CtCoord *point) override;
  u8 PaintCursor(const CtCoord *point, GeBounds *affected) override;
};

#endif
