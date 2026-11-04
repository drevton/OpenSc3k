#ifndef SC3_MISC_MODULE_H
#define SC3_MISC_MODULE_H

#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_string.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "ui/commands.h"

// Actual common chained-command factory from SIMMISC, with its module-local reference accounting.
// The child sequence holds retained interfaces; its native project array does not overlay the original vector storage.
class ScGameCommandFactory : public ScCommandFactory {
public:
  ScGameCommandFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 classId, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~ScGameCommandFactory();
  virtual bool CreateCommand(u32 classId, u32 iid, void **out) = 0;
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// Original five-word localized text handle; the retained String interface and actual resource key are separate fields.
class CtMiscTextResource {
public:
  CtMiscTextResource(u32 instance, u32 group);
  virtual ~CtMiscTextResource();
  ScString GetText();
  ScStringView *text;
  RsKey key;
};

class RsManager;

// Actual cSC3Buffer scoped resource wrapper has its own vptr and retained general-buffer pointer.
class ScBuffer {
public:
  ScBuffer(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~ScBuffer();
  GxBuffer *GetBuffer();

private:
  // Full native lookup effects; the private nominal bool/BYTE declaration remains unresolved.
  bool Load(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

class UiCommand;

struct ScCommandParameter {
  i32 type;
  u32 flags;
};

// The command's secondary lifetime base has its own virtual order, separate from the primary unknown interface.
class ScMiscRefCount {
public:
  ScMiscRefCount();
  virtual ~ScMiscRefCount();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
protected:
  u32 references;
};

// Actual command base owns its pending arguments; popping transfers the retained reference to the caller.
class ScGameCommand : public UiCommandInterface, public ScMiscRefCount {
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
  ScArray<ScCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};

// Actual registered SimMisc command-factory subclass adds its command selection operation to the common base.
class CtCommandFactory : public ScGameCommandFactory {
public:
  CtCommandFactory();
  ~CtCommandFactory() override;
  bool CreateCommand(u32 classId, u32 iid, void **out) override;
};

#endif
