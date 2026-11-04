#ifndef SC3_WINDOW_TOOLS_H
#define SC3_WINDOW_TOOLS_H

#include <vector>

#include "common/api.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "ui/commands.h"
#include "window/api.h"

// GZTOOLSD's command-factory base owns the original ordered vector of retained child factories.
class ScToolsFactoryBase : public ScCommandFactory {
public:
  ScToolsFactoryBase();
  virtual ~ScToolsFactoryBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 class_id, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual bool CreateCommand(u32 class_id, u32 iid, void **out) = 0;

private:
  u32 references;
  std::vector<ScCommandFactory *> children;
};

// The module-specific factory produces the original class122 command.
class ScToolsCommandFactory : public ScToolsFactoryBase {
public:
  bool CreateCommand(u32 class_id, u32 iid, void **out) override;
};

// The original command's secondary lifetime identity retains the GZTOOLSD director.
class ScToolsCommandOwner {
public:
  ScToolsCommandOwner();
  virtual ~ScToolsCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};

// The class122 command owns four real descriptor and argument sequences.
class ScToolsCommand : public UiCommandInterface, public ScToolsCommandOwner {
public:
  ScToolsCommand();
  ~ScToolsCommand() override;
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
  bool Execute() override;

private:
  ScArray<UiCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};

class ScDebugDisplay;
class FtFontSystem;
class GxGraphics;
class WnResources;
class WnWindowManager;

ScDebugDisplay *ScGetToolsDisplay();
GxGraphics *ScGetToolsGraphics();
WnResources *ScGetToolsResources();
WnWindowManager *ScGetToolsWindowManager();
FtFontSystem *ScGetToolsFontSystem();

// The GZTOOLSD director registers its own service and window factories and exposes framework hooks.
class ScToolsDirector : public ScDllDirector {
public:
  ScToolsDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool OnUnload() override;
  bool PreFrameWorkInit() override;
};

ScToolsDirector *ScGetToolsDirector();
ScFramework *ScGetToolsFramework();
ScCom *ScGetToolsCom();
SC3_C_LINKAGE ScDirector *ScToolsDllGetGZCOMDirector();

// GZTOOLSD's secondary module identity owns the local reference count.
class ScToolsObject : public ScUnknown {
public:
  ScToolsObject();
  virtual ~ScToolsObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// The actual system-service base retains the separate module identity and service properties.
class ScToolsService : public ScSystemService, public ScToolsObject {
public:
  ScToolsService(u32 identifier, i32 priority);
  ~ScToolsService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 value) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool value) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 value) override;
  bool OnIdle(u32 value) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool running;
};

class ScToolsDisplay;
class ScToolsItem;
class ScToolsMeterItem;

// A debug line window retains its private source item and an independently created drawing buffer.
class WnToolsLineWindow : public WnToolsRootWidget {
public:
  explicit WnToolsLineWindow(ScToolsItem *item);
  ~WnToolsLineWindow() override;
  bool GZPaint() override;
  ScToolsItem *item;
  GxBuffer *surface;
};

// A bar window holds a second reference to the same meter item through its more specific view.
class WnToolsBarWindow : public WnToolsLineWindow {
public:
  explicit WnToolsBarWindow(ScToolsMeterItem *item);
  ~WnToolsBarWindow() override;
  bool GZPaint() override;
  ScToolsMeterItem *meter;
};

struct WnToolsWindowNode {
  WnToolsWindowNode *next, *previous;
  WnToolsLineWindow *window;
};

// Original debug-display window, separate from the public display-service facet.
class WnToolsWindow : public WnToolsRootWidget {
public:
  WnToolsWindow();
  ~WnToolsWindow() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  void ShowWindow() override;
  void HideWindow() override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;

private:
  void ReconcileItems();
  bool active;
  ScToolsDisplay *display;
  ScString debug_name;
  WnToolsWindowNode *head;
};

class ScMessageServer;

// The original private line record retains a borrowed live String and a separately owned text snapshot.
class ScToolsItem {
public:
  explicit ScToolsItem(ScStringView *line);
  virtual u8 AddRef();
  virtual u8 Release();
  virtual ~ScToolsItem();
  bool Update();
  u8 kind, references;
  ScStringView *line;
  ScString snapshot;
};

// Meter records add borrowed float and label arrays, plus the original value and copied-label vectors.
class ScToolsMeterItem : public ScToolsItem {
public:
  ScToolsMeterItem(f32 *values, u32 count);
  ScToolsMeterItem(f32 *values, u32 count, ScStringView **labels, u32 labelCount);
  ~ScToolsMeterItem() override;
  bool UpdateValues();
  f32 *values;
  u32 count;
  ScStringView **labels;
  u32 labelCount;
  f32 *previousBegin, *previousEnd, *previousCapacity;
  std::vector<ScString *> copiedLabels;
};

// The original display service owns a circular list of pointers to its private line and meter records.
struct ScToolsItemNode {
  ScToolsItemNode *next, *previous;
  ScToolsItem *item;
};

// The original debug service publishes line and meter items through its second interface.
class ScToolsDisplay : public ScToolsService, public ScDebugDisplay {
public:
  ScToolsDisplay();
  ~ScToolsDisplay() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void Show() override;
  void Hide() override;
  bool IsVisible() override;
  bool AddLine(ScStringView *line) override;
  bool RemoveLine(ScStringView *line) override;
  bool AddBarGraph(f32 *values, u32 count) override;
  bool AddBarGraph(f32 *values, u32 count, ScStringView **labels, u32 labelCount) override;
  bool RemoveBarGraph(f32 *values) override;
  bool AddStripMeter(f32 *values, u32 count) override;
  bool AddStripMeter(f32 *values, u32 count, ScStringView **labels, u32 labelCount) override;
  bool RemoveStripMeter(f32 *values) override;

private:
  friend class WnToolsWindow;
  ScMessageServer *AcquireMessageServer();
  void AppendItem(ScToolsItem *item);
  bool RemoveItem(u8 kind, const void *identity);
  void NotifyItems(bool post);
  bool visible;
  ScMessageServer *server;
  ScToolsItemNode *head;
  u32 unknown;
};

#endif
