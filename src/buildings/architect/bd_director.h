#ifndef SC3_BUILDINGS_ARCHITECT_BD_DIRECTOR_H
#define SC3_BUILDINGS_ARCHITECT_BD_DIRECTOR_H

#include "common/sc3_array.h"
#include "common/sc3_director.h"

class BdDllDirector;

// Native stable nodes retain the original unsigned class/reserved ordering and first constructor on duplicate keys.
struct BdClassRegistration {
  BdClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct BdDirectorClasses {
  BdDirectorClasses();
  ~BdDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  BdClassRegistration head;
};

// Both primary and hook views are genuine interfaces. PC+36 has no established initialization or meaningful access.
class BdDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  BdDllDirector();
  virtual ~BdDllDirector();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool InitializeCOM(ScCom *com, ScStringView *path) override;
  bool OnStart(ScCom *com) override;
  void EnumClassObjects(void (*callback)(u32, u32, void *), void *context) override;
  bool GetClassObject(u32 clsid, u32 iid, void **out) override;
  bool CanUnloadNow() override;
  bool OnUnload() override;
  u32 RefCount() override;
  u32 RemoveRef() override;
  ScFramework *FrameWork() override;
  ScCom *GZCOM() override;
  bool ModulePath(ScStringView *out) override;
  virtual bool AppRegister(ScRegistry *registry);
  virtual void AddDirector(BdDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  void RegisterClass(u32 clsid, ScUnknown *(*create)(), u32 reserved);

  ScArray<BdDllDirector *> children;
  BdDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class BdDirector : public BdDllDirector {
public:
  BdDirector();
  ~BdDirector() override;
  bool OnStart(ScCom *com) override;
  u32 GetDirectorID() const override;
};

class BdChildDirector : public BdDllDirector {
public:
  BdChildDirector();
  ~BdChildDirector() override;
  u32 GetDirectorID() const override;
};

// The actual manager global holds its primary unknown view; its canonical IID1 view is a distinct interface at PC+20.
extern ScUnknown *BdManagerPrimaryCurrent;
BdChildDirector *BdGetChildDirector();
ScUnknown *BdCreateManager();
ScUnknown *BdCreateClass455969DF();
ScUnknown *BdCreateClassA2FFB5D3();
ScUnknown *BdCreateClassC3023BE2();
ScUnknown *BdCreateImageLoader();
ScUnknown *BdCreateCodec();

#endif
