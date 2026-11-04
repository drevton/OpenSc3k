#ifndef SC3_COMMON_SERVICES_MODULE_H
#define SC3_COMMON_SERVICES_MODULE_H

#include <vector>

#include "common/api.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"

// Original secondary reference-count base, retaining the actual services module before changing its local count.
class ScServicesObject : public ScUnknown {
public:
  ScServicesObject();
  // Final Release destroys the complete object through this virtual destructor after removing the module reference.
  virtual ~ScServicesObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Concrete field/property portion of the original24-byte system-service base; no framework callback descriptors are stored.
class ScServicesBase : public ScSystemService, public ScServicesObject {
public:
  ScServicesBase(u32 identifier, i32 priority);
  ~ScServicesBase() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool running;
};

class ScDirector;
ScDirector *ScGetServicesDirector();

// Main GZService director owns four service constructors and one independently constructed child director.
class ScServicesDirector : public ScDllDirector {
public:
  ScServicesDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

// Original child director registers its own service and is started before the four main-module services.
class ScServicesChildDirector : public ScDllDirector {
public:
  ScServicesChildDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

ScDirector *ScGetServicesDirector();
ScServicesChildDirector *ScGetServicesChildDirector();
ScFramework *ScServicesFramework();
SC3_C_LINKAGE ScDirector *ScServicesDllGetGZCOMDirector();

// Retail cRZGameCommandFactory record8556 establishes this base, its virtual CreateCommand and its vector storage.
class ScCommandFactoryBase : public ScCommandFactory {
public:
  ScCommandFactoryBase();
  virtual ~ScCommandFactoryBase();
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

// The installed command service owns the factory as its second real base and exposes a third service identity.
class ScCommandServiceRoot : public ScServicesBase, public ScCommandFactoryBase, public ScCommandService {
public:
  ScCommandServiceRoot();
  ~ScCommandServiceRoot() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateCommand(u32 class_id, u32 iid, void **out) override;
  ScCommandFactory *GetClassFactory() override;
};

#endif
