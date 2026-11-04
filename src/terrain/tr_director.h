#ifndef SC3_TERRAIN_TR_DIRECTOR_H
#define SC3_TERRAIN_TR_DIRECTOR_H

#include "common/sc3_array.h"
#include "common/sc3_director.h"

class TrDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct TrClassRegistration {
  TrClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct TrDirectorClasses {
  TrDirectorClasses();
  ~TrDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  TrClassRegistration head;
};

// Recovered director base used by SIMDIRT's module director; children and factory functions are borrowed.
class TrDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  TrDllDirector();
  virtual ~TrDllDirector();
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
  virtual void AddDirector(TrDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  ScArray<TrDllDirector *> children;
  TrDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

// Concrete terrain director registers its four actual class callbacks and the terrain interaction command.
class TrDirector : public TrDllDirector {
public:
  TrDirector();
  ~TrDirector() override;
  u32 GetDirectorID() const override;
  bool AppRegister(ScRegistry *registry) override;
  // Original static callbacks construct the four registered classes without an implicit director argument.
  static ScUnknown *CreateDirtBag();
  static ScUnknown *CreateToolFactory();
  static ScUnknown *CreateGenerator();
  static ScUnknown *CreateNeighborMap();
};

#endif
