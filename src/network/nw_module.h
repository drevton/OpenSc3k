#ifndef SC3_NETWORK_NW_MODULE_H
#define SC3_NETWORK_NW_MODULE_H

#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"

class NwDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct NwClassRegistration {
  NwClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct NwDirectorClasses {
  NwDirectorClasses();
  ~NwDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  NwClassRegistration head;
};

// Recovered network-module director base; child directors and registered factory functions are borrowed.
class NwDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  NwDllDirector();
  virtual ~NwDllDirector();
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
  virtual void AddDirector(NwDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  ScArray<NwDllDirector *> children;
  NwDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class NwDirector : public NwDllDirector {
public:
  NwDirector();
  ~NwDirector() override;
  u32 GetDirectorID() const override;
  bool AppRegister(ScRegistry *registry) override;
};

// Return the actual unretained manager primary; its fourth query-string interface is a real base.
ScUnknown *NwCreateManager();
// The attribute creator returns the actual unretained connection-attribute primary.
ScUnknown *NwCreateAttributes();

ScDirector *NwGetDirector();
class ApGameInterface;
class OcAttribCache;
class CtAgentTree;
class RsFileSystem;
class ScMessageServer;
ScFramework *NwGetFrameWork();
ScCom *NwGetCom();
ApGameInterface *NwGetApplication();
CtCity *NwGetCity();
class SpCityWindowView;
SpCityWindowView *NwGetCityWindow();
class SpViewInterface;
SpViewInterface *NwGetCurrentView();
OcAttribCache *NwGetOccupantAttribCache();
CtAgentTree *NwGetAgentTypeTree();
class CtDefinition;
CtDefinition *NwGetCityDefinition();
RsFileSystem *NwGetFileSystem();
ScMessageServer *NwGetMessageServer();
class RsManager;
RsManager *NwGetResourceManager();
class ScLanguageManager;
ScLanguageManager *NwGetLanguageManager();
class WnResources;
WnResources *NwGetWindowResources();
class WnWindowManager;
WnWindowManager *NwGetWindowManager();
// Actual one-time guard and external QueryInterface output slot for the borrowed application view.
extern u8 NwApplicationInitFlags;
extern void *NwApplicationCache;
extern RsFileSystem *NwFileSystemCache;
extern ScMessageServer *NwMessageServerCache;
extern RsManager *NwResourceManagerCache;
extern WnWindowManager *NwWindowManagerCache;
extern ScLanguageManager *NwLanguageManagerCache;
extern WnResources *NwWindowResourcesCache;

// The module's actual chained factory owns retained child factory interfaces and a director-backed reference count.
class NwFactory : public ScCommandFactory {
public:
  NwFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 classId, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~NwFactory();
  virtual bool CreateCommand(u32 classId, u32 iid, void **out) = 0;
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// The original seven-command dispatch remains an explicit dependency until its concrete tool classes are recovered.
class NwCommandFactory : public NwFactory {
public:
  ~NwCommandFactory() override;
  bool CreateCommand(u32 classId, u32 iid, void **out) override;
};

// Concrete state used by the module-owned decoration generator.
class NwRandom {
public:
  explicit NwRandom(u32 seed);
  u32 Next();
  u32 Bounded(u32 bound);
  u32 seed, baseSeed, auxiliary;
};

extern NwRandom *NwDecorationRandom;

// Original transit director adds five registrations to the actual common DLL-director base.
// Its five products retain their actual fields; unrecovered simulation and virtual methods remain explicit dependencies.
class NwTransitDirector : public ScDllDirector {
public:
  NwTransitDirector();
  ~NwTransitDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

// Original registered zero-argument factories return the new primary interface without an additional retain.
// Definitions construct the real products in nw_transit_products.cpp; remaining virtual-method dependencies are explicit.
ScUnknown *NwCreateTransitLayer();      // Factory116F -> constructor12BF, original272-byte product.
ScUnknown *NwCreateRailStation();       // Factory11A4 -> constructorD3A7, original64-byte product.
ScUnknown *NwCreateSubwayRailStation(); // Factory11D6 -> constructorD625, original68-byte product.
ScUnknown *NwCreateSubwayStation();     // Factory1208 -> constructorD8E7, original68-byte product.
ScUnknown *NwCreateBusStop();           // Factory123A -> constructorDBC7, original64-byte product.

ScDirector *NwGetTransitDirector();
ScFramework *NwGetTransitFramework();
class ApGameInterface;
class CtCity;
class ScLanguageManager;
class RsManager;
class OcAttribCache;
class WnResources;
class ScMessageServer;
ApGameInterface *NwGetTransitApplication();
CtCity *NwGetTransitCity();
ScLanguageManager *NwGetTransitLanguageManager();
RsManager *NwGetTransitResourceManager();
OcAttribCache *NwGetTransitAttribCache();
WnResources *NwGetTransitWindowResources();
ScMessageServer *NwGetTransitMessageServer();

#endif
