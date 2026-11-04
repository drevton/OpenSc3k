#ifndef SC3_UI_UI_MODULE_H
#define SC3_UI_UI_MODULE_H

#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"

class UiDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct UiClassRegistration {
  UiClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct UiDirectorClasses {
  UiDirectorClasses();
  ~UiDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  UiClassRegistration head;
};

// Copied director base used by SIMUI's module director; children and factory functions are borrowed.
class UiDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  UiDllDirector();
  virtual ~UiDllDirector();
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
  virtual void AddDirector(UiDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  ScArray<UiDllDirector *> children;
  UiDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

// The module's actual chained factory owns retained child factory interfaces and a director-backed reference count.
class UiFactory : public ScCommandFactory {
public:
  UiFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 classId, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~UiFactory();
  virtual bool CreateCommand(u32 classId, u32 iid, void **out) = 0;
  u32 references;
  ScArray<ScCommandFactory *> children;
};

class ScDirector;
class ScFramework;
class ScMessageServer;
class WnWindowManager;
class GxGraphics;
class GxBlendService;
class AuSoundDevice;
class RsManager;
class RsFileSystem;
class ApBoxX;
class ScCom;
class ScCommandFactory;
class CtCity;
class CtAgentTree;
class ApGameInterface;
class ApPreferences;
struct ApPerformanceInfo;
class SpCityWindowView;
class SpViewInterface;
class WnResources;
class ScLanguageManager;
class FtFontSystem;
class OcAttribCache;
class SpImageCache;

// Service getters use the concrete SIMUI director and this DLL's own caches. Unported class factories remain explicit.
ScDirector *UiGetDirector();
ScFramework *UiGetFramework();
ScCom *UiGetCom();
ScCommandFactory *UiGetCommandFactory();
bool UiGetCommand(u32 class_id, u32 iid, void **out);
WnWindowManager *UiGetWindowManager();
GxGraphics *UiGetGraphics();
GxBlendService *UiGetBlendService();
AuSoundDevice *UiGetSoundDevice();
ScMessageServer *UiGetMessageServer();
RsManager *UiGetResourceManager();
RsFileSystem *UiGetFileSystem();
ApBoxX *UiGetBoxX();
ApGameInterface *UiGetApplication();
ApPerformanceInfo *UiGetSystemPerformanceInfo();
ApPreferences *UiGetPreferences();
CtCity *UiGetCity();
SpCityWindowView *UiGetCityWindow();
SpViewInterface *UiGetCurrentView();
WnResources *UiGetWindowResources();
ScLanguageManager *UiGetLanguageManager();
FtFontSystem *UiGetFontManager();
OcAttribCache *UiGetAttribCache();
SpImageCache *UiGetImageCache();
CtAgentTree *UiGetAgentTypeTree();

// Concrete SIMUI director owns the source class catalog and startup registration. Unported factories remain explicit.
class UiDirector : public UiDllDirector {
public:
  UiDirector();
  ~UiDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool PreAppInit() override;

private:
  void ConfigureRegistryEffects(ScRegistry *registry);
};

#endif
