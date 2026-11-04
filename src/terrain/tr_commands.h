#ifndef SC3_TERRAIN_TR_COMMANDS_H
#define SC3_TERRAIN_TR_COMMANDS_H

#include "geometry/ge_types.h"
#include "sprites/api.h"
#include "ui/commands.h"

class WnWindowManager;
class SpCityWindowView;
class SpViewInterface;
class UiCityCursor;
class EcBudgetLayer;

// Partial primary terrain input receiver, with real fields and the verified common message/input effects.
// Identity and lifetime belong to its concrete command subclasses; the actual canonical view-handler base is retained.
class TrTool : public SpViewHandler {
public:
  TrTool();
  bool DoMessage(ScMessage *message) override;
  // Authentic retail overrides establish Boolean input results; the shared handler retains its original virtual order.
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
  // Original handler suffix slots56/60 precede the concrete deleting destructor at64.
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual ~TrTool();

  // Retail declarations establish these original helpers independently of incidental register values.
  bool Dettach(SpCityWindowView *current_window);
  void ClearStatus();
  void UpdateStatusDisplay();
  void CheckBudget();
  bool Attach(SpCityWindowView *window);
  void SetStatus(i32 status, bool enabled);

protected:
  bool ContainsPoint(i32 x, i32 z, i32 altitude) const;
  WnWindowManager *manager;
  SpCityWindowView *window;
  SpViewInterface *view;
  UiCityCursor *cursor;
  EcBudgetLayer *budget;
  GeBounds bounds;
  u8 panning, cursor_visible, edge_scroll, cost_visible;
  i32 cost;
  u8 invalid, unaffordable, conflict, retiring;
};

// Original drag receiver extends the common tool with its active byte and two actual coordinate triples.
// Concrete terrain commands override stroke callbacks; the original shared defaults remain real no-op successes.
class TrDragTool : public TrTool {
public:
  TrDragTool();
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;

  // Original callbacks return bool and take const input references; damage outputs retain explicit typed pointers.
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(const CtCoord &point);
  virtual bool ExecuteOnMouseDrag(const CtCoord &point);
  virtual bool ExecuteOnMouseMove(const CtCoord &point);
  virtual bool ExecuteCellDraw(const CtCoord &point, GeBounds *damage);
  virtual void ExecuteOnResetState();

protected:
  u8 dragging;
  CtCoord current, previous;
};

// Original rectangle receiver stores an active flag followed by current and previous six-coordinate selections.
// Coordinate callbacks retain their actual point/rectangle distinction and shared original no-effect defaults.
class TrRectTool : public TrTool {
public:
  TrRectTool();
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;

  void OnResetState() override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(const GeBounds &selection);
  virtual bool ExecuteOnMouseDrag(const GeBounds &selection);
  virtual bool ExecuteOnMouseMove(const CtCoord &point);
  virtual bool ExecuteRectDraw(const GeBounds &selection, GeBounds *damage);
  virtual void ExecuteOnResetState();
  u8 dragging;
  GeBounds selection, previous_selection;
};

class ScDirtBag;
class CtZoneLayer;
class ScOccupantManager;
class GeDemolitionView;

// SIMDIRT has its own compiled command lifetime base; its reference operations retain this module director.
class TrCommandOwner {
public:
  TrCommandOwner();
  virtual ~TrCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};

// The local command implementation shares IID6856F7 while owning its actual argument and descriptor sequences.
class TrCommand : public UiCommandInterface, public TrCommandOwner {
public:
  TrCommand();
  ~TrCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool PushBackInput(ScVariant *argument) override;
  bool PopFrontInput(ScVariant **out) override;
  u32 InputQueueSize() override;
  bool EnumInputParameters(void (*callback)(i32, u32, void *), void *context) override;
  bool PushBackOutput(ScVariant *argument) override;
  bool PopFrontOutput(ScVariant **out) override;
  u32 OutputQueueSize() override;
  bool EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) override;
  virtual bool AddInputParameter(i32 type, u32 flags);
  virtual bool AddOutputParameter(i32 type, u32 flags);
  ScArray<UiCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};

// Actual raise command combines the primary drag receiver with its separate command and lifetime interfaces.
// Strokes use the original Boolean callbacks with const coordinates and separate mutable damage outputs.
class TrRaiseTool : public TrDragTool, public TrCommand {
public:
  TrRaiseTool();
  ~TrRaiseTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  bool Execute() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GetCursor(u32 *out) override;

protected:
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool ExecuteOnMouseDrag(const CtCoord &point) override;
  bool ExecuteOnMouseMove(const CtCoord &point) override;
  bool ExecuteCellDraw(const CtCoord &point, GeBounds *damage) override;
  GeBounds extent;
  ScDirtBag *terrain;
  // City acquisition requests IID231C8E63; its generic getter transports this actual same-primary view.
  GeDemolitionView *demolition;
  u32 mode;
  u8 unknown_flag;
};

// Actual lower command combines the primary drag receiver with its separate command and lifetime interfaces.
// Original entries fold with raise where shown; the observed bases do not assert an extra raise-tool inheritance.
// Strokes retain the original Boolean callbacks, const coordinate inputs and separate mutable damage outputs.
class TrLowerTool : public TrDragTool, public TrCommand {
public:
  TrLowerTool();
  ~TrLowerTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  bool Execute() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GetCursor(u32 *out) override;

protected:
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool ExecuteOnMouseDrag(const CtCoord &point) override;
  bool ExecuteOnMouseMove(const CtCoord &point) override;
  bool ExecuteCellDraw(const CtCoord &point, GeBounds *damage) override;
  GeBounds extent;
  ScDirtBag *terrain;
  // City acquisition requests IID231C8E63; its generic getter transports this actual same-primary view.
  GeDemolitionView *demolition;
  u32 mode;
  u8 unknown_flag;
};

// Actual level command combines the primary rectangle receiver with its separate command and lifetime interfaces.
// Original inherited stroke contracts are Boolean, with const inputs and explicit mutable damage outputs.
class TrLevelTool : public TrRectTool, public TrCommand {
public:
  TrLevelTool();
  ~TrLevelTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  bool Execute() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GetCursor(u32 *out) override;

protected:
  bool ExecuteOnMouseUp(const GeBounds &selection) override;
  bool ExecuteOnMouseDrag(const GeBounds &selection) override;
  bool ExecuteOnMouseMove(const CtCoord &point) override;
  bool ExecuteRectDraw(const GeBounds &selection, GeBounds *damage) override;
  GeBounds extent;
  ScDirtBag *terrain;
  // City acquisition requests IID231C8E63; its generic getter transports this actual same-primary view.
  GeDemolitionView *demolition;
  u32 mode;
};

// Actual water command combines the primary drag receiver with its separate command and lifetime interfaces.
// Inherited stroke contracts are Boolean, with const inputs and explicit mutable damage outputs.
class TrWaterTool : public TrDragTool, public TrCommand {
public:
  TrWaterTool();
  ~TrWaterTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;

protected:
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool ExecuteOnMouseDrag(const CtCoord &point) override;
  bool ExecuteOnMouseMove(const CtCoord &point) override;
  bool ExecuteCellDraw(const CtCoord &point, GeBounds *damage) override;
  GeBounds extent;
  ScDirtBag *terrain;
  // City acquisition requests IID231C8E63; its generic getter transports this actual same-primary view.
  GeDemolitionView *demolition;
  CtZoneLayer *zones;
  ScOccupantManager *surface, *underground_one, *underground_two;
};

#endif
