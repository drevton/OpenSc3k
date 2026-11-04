#ifndef SC3_UI_COMMANDS_H
#define SC3_UI_COMMANDS_H

#include "city/city.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "ui/ui_module.h"
#include "window/api.h"

class UiCommand;

struct UiCommandParameter {
  i32 type;
  u32 flags;
};

// The command's secondary lifetime base has its own virtual order, separate from the primary unknown interface.
class UiCommandOwner {
public:
  UiCommandOwner();
  virtual ~UiCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};

// Installed command interface6856f7 exposes retained argument queues and signed-type parameter descriptors.
// Enumeration callbacks and their void context are the actual external COM ABI, not an internal service adapter.
class UiCommandInterface : public ScUnknown {
public:
  virtual bool PushBackInput(ScVariant *argument) = 0;
  virtual bool PopFrontInput(ScVariant **out) = 0;
  virtual u32 InputQueueSize() = 0;
  virtual bool EnumInputParameters(void (*callback)(i32, u32, void *), void *context) = 0;
  virtual bool PushBackOutput(ScVariant *argument) = 0;
  virtual bool PopFrontOutput(ScVariant **out) = 0;
  virtual u32 OutputQueueSize() = 0;
  virtual bool EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) = 0;
  virtual bool Execute() = 0;
};

// Actual command base owns its pending arguments; popping transfers the retained reference to the caller.
class UiCommand : public UiCommandInterface, public UiCommandOwner {
public:
  UiCommand();
  ~UiCommand() override;
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

protected:
  virtual bool AddInputParameter(i32 type, u32 flags);
  virtual bool AddOutputParameter(i32 type, u32 flags);

public:
  ScArray<UiCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};

// The concrete SIMUI command factory adds only its dispatch override to the retained chained-factory base.
class UiCommandFactory : public UiFactory {
public:
  UiCommandFactory();
  ~UiCommandFactory() override;
  bool CreateCommand(u32 classId, u32 iid, void **out) override;
};

class ScCommandService;

// Original scoped receiver owns the acquired command-service interface until its stack scope ends.
class UiCommandServiceRef {
public:
  UiCommandServiceRef();
  ~UiCommandServiceRef();
  ScCommandService *value;
};

// The stock advisor-dialog command adds no state or descriptors; its public execution operation is a source no-op.
class UiAdvisorCommand : public UiCommand {
public:
  UiAdvisorCommand();
  bool Execute() override;
};

class GeAttrib;

// Reached A308D9AD secondary view of chooserE3091435, now supplied by UiCatalogPicker's real class factory.
class UiBuildingPicker : public ScUnknown {
public:
  // Source67413 only stores the category; its private nominal return declaration remains unresolved.
  virtual void SetCategoryEffects(u32 category) = 0;
  virtual u8 Init() = 0;
  virtual WnWindow *GetWindow() const = 0;
  virtual bool HasSelection() const = 0;
  virtual u8 GetSelection(GeAttrib **out) = 0;
};

// The one-integer building command keeps its advertised descriptor and captured category as actual class state.
class UiBuildingCommand : public UiCommand {
public:
  UiBuildingCommand();
  bool Execute() override;
  virtual bool Choose(u32 category);
  static void ReadParameterEffects(i32 type, u32 flags, void *receiver);
  static u16 history;
  u32 requested_type;
};

// The message-box command consumes two byte strings and one integer, then publishes the modal result as an integer.
class UiMessageBoxCommand : public UiCommand {
public:
  UiMessageBoxCommand();
  bool Execute() override;
  bool ReadString(ScString *out);
  bool ReadInteger(u32 *out);
  u8 Show(u32 *out, const ScStringView &body, const ScStringView &caption, u32 style);
};

UiMessageBoxCommand *UiCreateMessageBoxCommand();

class CtCity;

// Original business-deals view: city enumeration at12 precedes initialization16, city-items20 and window24.
class UiDialog62FA574B : public CtCityEnumerator {
public:
  virtual u8 Init() = 0;
  virtual u8 EnumCityItems(CtCity *city, bool enabled) = 0;
  virtual WnWindow *GetWindow() const = 0;
};

// Concrete command124CC has no added fields or descriptors; its private original topic/name remains unresolved.
class UiModalCommand124CC : public UiCommand {
public:
  UiModalCommand124CC();
  bool Execute() override;
};

// Concrete command12576 acquires a temporary window service instead of using the module's borrowed cache.
class UiModalCommand12576 : public UiCommand {
public:
  UiModalCommand12576();
  bool Execute() override;
};

// The source184-byte query command captures a coordinate pair and counts consumed descriptor arguments.
// The command dispatches actual layer queries and owns no city, window or argument references between executions.
class UiQueryCommand : public UiCommand {
public:
  UiQueryCommand();
  bool Execute() override;
  // Three original argument words are retained; the unused trailing parameters' nominal source types remain unresolved.
  virtual bool QueryTile(const ScPoint *tile, u32 first, u32 second);
  ScPoint tile;
  u32 processed_count;
};

void UiCaptureQueryInput(i32 type, u32 flags, void *context);

// The scenario-progress command advertises three integers while consuming only its first queued argument.
class UiScenarioProgressCommand : public UiCommand {
public:
  UiScenarioProgressCommand();
  bool Execute() override;
};

// The city-speed command owns the ordinary command queues and has no additional receiver fields.
class UiSpeedCommand : public UiCommand {
public:
  UiSpeedCommand();
  bool Execute() override;
};

// Reached22FA5865 primary view of the real C2FA5860 tip dialog; its remaining renderer is an explicit dependency.
class UiTipDialogInterface : public ScUnknown {
public:
  virtual u8 Init() = 0;
  virtual WnWindow *GetWindow() const = 0;
};

// The E2FA7B74 tip command has the original command base without additional fields or input descriptors.
class UiTipCommand : public UiCommand {
public:
  UiTipCommand();
  bool Execute() override;
};

#endif
