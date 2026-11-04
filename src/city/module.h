#ifndef SC3_CITY_MODULE_H
#define SC3_CITY_MODULE_H

#include <set>
#include <vector>

#include "buildings/api.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "resources/resource.h"

void SC3_CDECL BdReadPluginSegment(const ScString &key, const ScString &value, void *owner);
void SC3_CDECL BdReadPluginOccupant(const ScString &key, const ScString &value, void *owner);

// The installed filename set compares the complete byte ranges and then their lengths.
struct BdPluginPathLess {
  bool operator()(const ScString &left, const ScString &right) const;
};

// SIMINIT's plugin-building manager. Its sprite-section callback remains an explicit dependency.
class BdPluginManagerImpl : public BdPluginManager {
public:
  BdPluginManagerImpl();
  virtual ~BdPluginManagerImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(const ScStringView &path) override;
  bool Shutdown() override;
  void CheckForPluginBuildings(bool update) override;

protected:
  friend void SC3_CDECL BdReadPluginOccupant(const ScString &, const ScString &, void *);
  // The private stock return is unconsumed by the scanner; this declaration exposes its recovered effects.
  void ProcessPluginFile(const ScString &path);
  u32 references;
  ScString directory;
  std::set<ScString, BdPluginPathLess> processedFiles;
  std::vector<RsKey> pendingKeys;
  u8 scanFlag;
};

class CtDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct CtClassRegistration {
  CtClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct CtDirectorChildren {
  CtDirectorChildren();
  ~CtDirectorChildren();
  void Append(CtDllDirector *director);
  CtDllDirector **begin, **end, **storageEnd;
};

struct CtDirectorClasses {
  CtDirectorClasses();
  ~CtDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  CtClassRegistration head;
};

// Recovered city-module director base; child directors and registered factory functions are borrowed.
class CtDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  CtDllDirector();
  virtual ~CtDllDirector();
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
  virtual void AddDirector(CtDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  CtDirectorChildren children;
  CtDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class CtDirector : public CtDllDirector {
public:
  CtDirector();
  ~CtDirector() override;
  u32 GetDirectorID() const override;
  bool AppRegister(ScRegistry *registry) override;
};

ScDirector *CtGetDirector();

#endif
