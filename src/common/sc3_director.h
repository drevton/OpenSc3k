#ifndef SC3_COMMON_SC3_DIRECTOR_H
#define SC3_COMMON_SC3_DIRECTOR_H

#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Installed Unlimited director interface through slot56. The later SDK's ModulePath names the operation that retail
// cRZCOMDllDirector symbols call ModuleName; registration and child attachment belong to the concrete director class.
class ScDirector : public ScUnknown {
public:
  virtual bool InitializeCOM(ScCom *com, ScStringView *library_path) = 0;
  virtual bool OnStart(ScCom *com) = 0;
  virtual void EnumClassObjects(void (*callback)(u32 clsid, u32 reserved, void *context), void *context) = 0;
  virtual bool GetClassObject(u32 clsid, u32 iid, void **out) = 0;
  virtual bool CanUnloadNow() = 0;
  virtual bool OnUnload() = 0;
  virtual u32 RefCount() = 0;
  virtual u32 RemoveRef() = 0;
  virtual ScFramework *FrameWork() = 0;
  virtual ScCom *GZCOM() = 0;
  virtual u32 GetDirectorID() const = 0;
  virtual bool ModulePath(ScStringView *out) = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  typedef void (*ScClassCallback)(u32 clsid, u32 reserved, void *pContext);
};
struct ScDllClassEntry;

// Stable class-registration records replace the original ordered library tree, without owning factory products.
struct ScDllClassTable {
  ScDllClassEntry *first = nullptr, *last = nullptr;
  u32 count = 0;
  ~ScDllClassTable();
};

// Genuine common DLL-director receiver, with its separate framework-hook identity and borrowed common child directors.
// Native collections replace library storage; original field36 stays unwritten and has no asserted director-ID meaning.
class ScDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  ScDllDirector();
  virtual ~ScDllDirector();
  u32 GetDirectorID() const override = 0;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  u32 RefCount() override;
  bool InitializeCOM(ScCom *com, ScStringView *path) override;
  // Installed OnStart reaches its own module-global framework. Each module implements this existing hook explicitly.
  bool OnStart(ScCom *com) override = 0;
  bool GetClassObject(u32 clsid, u32 iid, void **out) override;
  void EnumClassObjects(ScClassCallback callback, void *context) override;
  bool CanUnloadNow() override;
  bool OnUnload() override;
  ScFramework *FrameWork() override;
  ScCom *GZCOM() override;
  bool ModulePath(ScStringView *out) override;
  virtual bool AppRegister(ScRegistry *registry);
  virtual void AddDirector(ScDllDirector *director);
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;

protected:
  using ScFactory = ScUnknown *(*)();
  // Retail AddCls has the same three arguments and a void return; native storage retains the original ordering.
  void AddClass(u32 clsid, ScFactory factory, u32 reserved);
  ScArray<ScDllDirector *> children;
  ScDllClassTable classes;
  u32 references, unknown36;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

#endif
