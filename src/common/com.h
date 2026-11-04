#ifndef SC3_COMMON_COM_H
#define SC3_COMMON_COM_H

#include <set>

#include "common/api.h"
#include "common/framework.h"
#include "common/thread.h"

class ScDirector;

// Original32-byte library record. The linked module lends its director; freeing the record does not Release it.
class ScComLibrary {
public:
  explicit ScComLibrary(const ScString &path);
  ScComLibrary(ScDirector *linked, const ScString &name);
  ScComLibrary(const ScComLibrary &other);
  bool Load();
  bool Free();
  bool IsLoaded() const;
  ScDirector *GetDirector() const;
  bool operator<(const ScComLibrary &other) const;
  mutable bool loaded;
  mutable ScDirector *director;
  ScString path;
  ScDirector *linked_director; // Borrowed resident director, retained across logical Free/Load cycles.
};

struct ScComClass {
  u32 version;
  ScComLibrary *library;
};

// Actual cRZThreadSafeObject base: a deleting identity, owned recursive section and stored validity result.
class ScThreadSafe {
public:
  // Construct the owned section before recording its virtual validity result.
  ScThreadSafe() : region_initialized(lock.IsValid() != 0) {
  }

  // Destroy the owned critical section with the base's normal member destruction.
  virtual ~ScThreadSafe() {
  }

protected:
  ScLock lock;
  i32 region_initialized;
};

// Embedded COM owner from the Windows framework, including its real service, module and deleting identities.
// Standard map/set storage replaces only the original library container machinery.
class ScComCore : public ScCom, public ScServiceBase, public ScThreadSafe {
public:
  ScComCore();
  ~ScComCore() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetClassObject(u32 class_id, u32 iid, void **out) override;
  ScFramework *FrameWork() override;
  bool AddLibrary(const ScStringView &path) override;
  // Native static configuration; the original named-loading interface remains unsupported.
  bool RegisterDirector(ScDirector *director, const ScString &name);
  void FreeUnusedLibraries() override;
  bool CreateGuid(u32 *out) override;
  bool RealInit() override;
  void RealShutdown() override;
  void SetServiceRunning(bool value) override;
  bool UpdateClassRegistry(ScComLibrary *library);
  bool GetLibObject(ScComLibrary *library, u32 class_id, u32 iid, void **out);
  bool CanUnloadLibrary(ScComLibrary *library);
  void FreeAllLibraries();
  // The real director-enumeration ABI supplies an opaque callback context; it is typed immediately in the body.
  static void AddEntryCallback(u32 class_id, u32 version, void *context);

private:
  bool initialized;
  std::map<u32, ScComClass> classes;
  std::set<ScComLibrary> libraries;
  ScComLibrary *current_library;
};

#endif
