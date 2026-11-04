#ifndef SC3_NETWORK_NW_TOOL_H
#define SC3_NETWORK_NW_TOOL_H

#include "city/api.h"
#include "geometry/ge_types.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "ui/commands.h"

class WnWindowManager;
class SpCityWindowView;
class SpViewInterface;
class UiCityCursor;
class EcBudgetLayer;
class SpSelection;

// The real60-byte handler base owns four retained providers and borrows the window-manager service.
// IUnknown remains abstract; the authentic view-handler suffix precedes the deleting destructor.
class NwTool : public SpViewHandler {
public:
  NwTool();
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
  virtual ~NwTool();
  bool AttachHost(SpCityWindowView *host);
  bool DetachIfRequested(SpCityWindowView *host);
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

// The drag-handler extension adds gesture state while its concrete network command supplies identity and execution.
// Callback methods retain their separate point/selection arguments; default methods implement the actual stock leaves.
class NwDragTool : public NwTool {
public:
  NwDragTool();
  ~NwDragTool() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(SpSelection *selection);
  virtual bool ExecuteOnMouseDrag(SpSelection *selection);
  virtual bool ExecuteOnMouseMove(SpSelection *selection);
  virtual bool ExecuteLineDraw(const CtCoord &first, const CtCoord &last, SpSelection **out);
  virtual void ExecuteOnResetState(); // Original primary +88.
  u8 dragging;
  CtCoord first, last;
  i32 field88, field92, field96;
};

// Actual SIMNTWRK enumerator owner: three coordinate-storage pointers, cursor and reference count.
// The shared sprite selection interface is separate from this concrete producer's extra virtual methods.
class NwSelection : public SpSelection {
public:
  NwSelection();
  NwSelection(const NwSelection &source);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Next(u32 maximum, CtCoord *out, u32 *count) override;
  u8 Skip(u32 count) override;
  u8 Reset() override;
  u8 Clone(SpSelection **out) override;
  virtual bool GetBounds(i32 bounds[6]); // Original +28.
  // Defined short batches retain their extra history item; full batches omit the stock out-of-array read.
  virtual u8 AppendFrom(SpSelection *source); // Original +32; private nominal AL type remains unresolved.
  virtual u8 SortAndCompact();                // Original +36; original ordering with ignored unique end.
  virtual ~NwSelection();                     // Original +40.
  void Append(const CtCoord &position);
  void Clear();
  CtCoord *begin, *end, *storageEnd;
  u32 cursor, references;

private:
  void Reserve(u32 capacity);
  NwSelection &operator=(const NwSelection &) = delete;
};

class NwManager;
class NwDrag;
class ScDirtBag;
class OcIteratorTest;
class ScOccupantManager;

// The network command lifetime base retains this module's director through its own secondary virtual interface.
class NwCommandOwner {
public:
  NwCommandOwner();
  virtual ~NwCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};

// Actual network implementation of command6856F7; four sequences own descriptors and retained argument references.
class NwCommand : public UiCommandInterface, public NwCommandOwner {
public:
  NwCommand();
  ~NwCommand() override;
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

// Actual road command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwRoadTool : public NwDragTool, public NwCommand {
public:
  NwRoadTool();
  ~NwRoadTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  u32 field296;
  ScOccupantManager *target;
};

// Actual highway command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwHighwayTool : public NwDragTool, public NwCommand {
public:
  NwHighwayTool();
  ~NwHighwayTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

// Actual rail command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwRailTool : public NwDragTool, public NwCommand {
public:
  NwRailTool();
  ~NwRailTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

// Actual subway command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwSubwayTool : public NwDragTool, public NwCommand {
public:
  NwSubwayTool();
  ~NwSubwayTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

// Actual power command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwPowerTool : public NwDragTool, public NwCommand {
public:
  NwPowerTool();
  ~NwPowerTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

// Actual pipe command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwPipeTool : public NwDragTool, public NwCommand {
public:
  NwPipeTool();
  ~NwPipeTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

// Actual diagonal command combines the drag handler with independent command/lifetime bases and its retained providers.
class NwDiagonalTool : public NwDragTool, public NwCommand {
public:
  NwDiagonalTool();
  ~NwDiagonalTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  bool ExecuteOnMouseUp(SpSelection *selection) override;
  bool ExecuteOnMouseDrag(SpSelection *selection) override;
  bool ExecuteOnMouseMove(SpSelection *selection) override;
  bool ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) override;

protected:
  NwManager *manager;
  NwDrag *drag;
  ScDirtBag *terrain;
  OcIteratorTest *primary, *secondary, *tertiary;
  ScOccupantManager *target;
};

#endif
