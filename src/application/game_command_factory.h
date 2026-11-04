#ifndef SC3_APPLICATION_GAME_COMMAND_FACTORY_H
#define SC3_APPLICATION_GAME_COMMAND_FACTORY_H

#include "application/main_dialogs.h"
#include "application/reference_count.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "ui/commands.h"

// Executable's own copy of the common chained command factory, with executable director accounting.
namespace ApRuntime {
class ScGameCommandFactory : public ScCommandFactory {
public:
  ScGameCommandFactory();
  virtual ~ScGameCommandFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 command, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual bool CreateCommand(u32 command, u32 iid, void **out) = 0;

protected:
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// Original command descriptors contain the signed type and independent unsigned flags.
struct ScCommandParameter {
  i32 type;
  u32 flags;
};

// Executable copy of the real common command; derived commands supply the original Execute operation.
class ScGameCommand : public UiCommandInterface, public ScRefCount {
public:
  ScGameCommand();
  ~ScGameCommand() override;
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

protected:
  ScArray<ScCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};
} // namespace ApRuntime

// Original grid command and its distinct sprite-view-filter command factory.
class ApGridToggleCommand : public ApRuntime::ScGameCommand {
public:
  ~ApGridToggleCommand() override;
  bool Execute() override;
};

class ApSpriteFilterCommandFactory : public ApRuntime::ScGameCommandFactory {
public:
  ~ApSpriteFilterCommandFactory() override;
  bool CreateCommand(u32 command, u32 iid, void **out) override;
};

// Real executable command factory dispatches its original products.
class ApCommandFactory : public ApRuntime::ScGameCommandFactory {
public:
  ~ApCommandFactory() override;
  bool CreateCommand(u32 command, u32 iid, void **out) override;
};

// Original command products own no fields beyond the executable command base.
class ApMainMenuCommand : public ApRuntime::ScGameCommand {
public:
  ~ApMainMenuCommand() override;
  bool Execute() override;
};

// Original emergency warning command adds its city point and consumed-input count after the common command bases.
class ApEmergencyWarningCommand : public ApRuntime::ScGameCommand {
public:
  ApEmergencyWarningCommand();
  ~ApEmergencyWarningCommand() override;
  bool Execute() override;

private:
  static void InputParameterCallback(i32 type, u32 flags, void *context);
  ScPoint cell_coord;
  u32 arguments_found;
};

// Original emergency dispatch commands combine the point-selection and game-command identities.
class ApDispatchFirefightersCommand : public ApRuntime::ScPointSelectionHandler, public ApRuntime::ScGameCommand {
public:
  ApDispatchFirefightersCommand();
  ~ApDispatchFirefightersCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
};

class ApDispatchPoliceCommand : public ApRuntime::ScPointSelectionHandler, public ApRuntime::ScGameCommand {
public:
  ApDispatchPoliceCommand();
  ~ApDispatchPoliceCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
};

class ApDispatchPlaneCommand : public ApRuntime::ScPointSelectionHandler, public ApRuntime::ScGameCommand {
public:
  ApDispatchPlaneCommand();
  ~ApDispatchPlaneCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool ExecuteOnMouseUp(const CtCoord &point) override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
};

// The original toolbar command has no extra fields and its Execute body returns false.
class ApToolbarCommand : public ApRuntime::ScGameCommand {
public:
  ~ApToolbarCommand() override;
  bool Execute() override;
};

// These original command products own the common command state and implement their actual dispatch methods.
class ApFireDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApFireDisasterCommand();
  bool Execute() override;
};

class ApTornadoDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApTornadoDisasterCommand();
  bool Execute() override;
};

class ApEarthquakeDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApEarthquakeDisasterCommand();
  bool Execute() override;
};

class ApRiotDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApRiotDisasterCommand();
  bool Execute() override;
};

class ApUfoDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApUfoDisasterCommand();
  bool Execute() override;
};

class ApToxicCloudDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApToxicCloudDisasterCommand();
  bool Execute() override;
};

class ApWhirlpoolDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApWhirlpoolDisasterCommand();
  bool Execute() override;
};

class ApLocustsDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApLocustsDisasterCommand();
  bool Execute() override;
};

class ApSpaceDebrisDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApSpaceDebrisDisasterCommand();
  bool Execute() override;
};

class ApSmiteDisasterCommand : public ApRuntime::ScGameCommand {
public:
  ApSmiteDisasterCommand();
  bool Execute() override;
};

class ApParadeCommand : public ApRuntime::ScGameCommand {
public:
  ApParadeCommand();
  ~ApParadeCommand() override;
  bool Execute() override;
};

class ApQuitCommand : public ApRuntime::ScGameCommand {
public:
  ApQuitCommand();
  bool Execute() override;
};

#endif
