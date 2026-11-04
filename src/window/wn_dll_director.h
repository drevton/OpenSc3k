#ifndef SC3_WINDOW_WN_DLL_DIRECTOR_H
#define SC3_WINDOW_WN_DLL_DIRECTOR_H

#include <map>
#include <vector>

#include "common/sc3_director.h"

// Retail types4FE7/4F54 prove the ordered pair-key factory map; type500F proves the child-pointer vector.
using WnClassKey = std::pair<const u32, u32>;
using WnClassFactory = ScUnknown *(*)();
using WnClassCatalog = std::map<WnClassKey, WnClassFactory>;

// Actual GZWIND director base. Child directors, catalog constructors and initialized COM/framework pointers are borrowed.
class WnDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  WnDllDirector();
  virtual ~WnDllDirector();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  u32 RefCount() override;
  bool InitializeCOM(ScCom *com, ScStringView *path) override;
  bool OnStart(ScCom *com) override;
  void EnumClassObjects(void (*callback)(u32, u32, void *), void *context) override;
  bool GetClassObject(u32 clsid, u32 iid, void **out) override;
  bool CanUnloadNow() override;
  bool OnUnload() override;
  ScFramework *FrameWork() override;
  ScCom *GZCOM() override;
  bool ModulePath(ScStringView *out) override;
  virtual bool AppRegister(ScRegistry *registry);
  virtual void AddDirector(WnDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;

protected:
  void AddCls(u32 clsid, WnClassFactory create, u32 reserved);
  std::vector<WnDllDirector *> children;
  WnClassCatalog classes;
  // The original constructor leaves director_id unwritten; derived GetDirectorID methods return their own constants.
  u32 references, director_id;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

// Concrete GZWIND startup director and the two actual borrowed class-registration children.
class WnDirector : public WnDllDirector {
public:
  WnDirector();
  bool OnStart(ScCom *com) override;
  bool PreFrameWorkInit() override;
  u32 GetDirectorID() const override;
};

class WnClassDirector : public WnDllDirector {
public:
  WnClassDirector();
  u32 GetDirectorID() const override;
};

class WnWindowDirector : public WnDllDirector {
public:
  WnWindowDirector();
  u32 GetDirectorID() const override;
};

WnClassDirector *WnGetClassDirector();
WnWindowDirector *WnGetWindowDirector();

#endif
