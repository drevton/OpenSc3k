#ifndef SC3_GEOMETRY_MODULE_H
#define SC3_GEOMETRY_MODULE_H

#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "ui/commands.h"

// Original20-byte geometry factory base owns a delegated-factory range and a separate module-retaining reference count.
class GeClassFactory : public ScCommandFactory {
public:
  GeClassFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 classId, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~GeClassFactory();
  // Original24; authentic command-factory declarations establish the Boolean creation contract.
  virtual bool CreateCommand(u32 classId, u32 iid, void **out) = 0;

protected:
  u32 references;
  ScCommandFactory **begin, **end, **storageEnd;
};

// Actual secondary lifetime identity, with deleting destructor then Release/AddRef/RemoveRef/RefCount.
// Sharing retain/release names with Unknown preserves the original compiler-generated multiple-inheritance thunks.
class GeCommandOwner {
public:
  GeCommandOwner();
  virtual ~GeCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};

// Original172-byte command has primary interface6856F7 and lifetime owner+4.
// Simple native sequences replace its four deque bodies; this60-byte native class is not a foreign memory overlay.
class GeCommand : public UiCommandInterface, public GeCommandOwner {
public:
  GeCommand();
  ~GeCommand() override;
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

// Actual SIMGEOM director: the shared 68-byte director receiver and its fourteen module-specific registrations.
class GeDirector : public ScDllDirector {
public:
  GeDirector();
  ~GeDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

#if !defined(SC3_STATIC)
SC3_API ScDirector *GZDllGetGZCOMDirector();
#endif

#endif
