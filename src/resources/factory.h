#ifndef SC3_RESOURCES_FACTORY_H
#define SC3_RESOURCES_FACTORY_H

#include <map>
#include <utility>
#include <vector>

#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "graphics/image.h"
#include "resources/module_object.h"
#include "resources/resource.h"
#include "resources/stream.h"

class RsDllDirector;

// Preserve the actual two-word registration key and unowned no-argument constructor pointer.
using RsClassKey = std::pair<u32, u32>;
using RsFactoryConstructor = ScUnknown *(*)();

class RsDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  RsDllDirector();
  virtual ~RsDllDirector();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool InitializeCOM(ScCom *com, ScStringView *library_path) override;
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
  virtual void AddDirector(RsDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  std::vector<RsDllDirector *> children;
  std::map<RsClassKey, RsFactoryConstructor> classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class RsDirector : public RsDllDirector {
public:
  RsDirector();
  bool OnStart(ScCom *com) override;
  u32 GetDirectorID() const override;
};

class RsResourceDirector : public RsDllDirector {
public:
  RsResourceDirector();
  bool OnStart(ScCom *com) override;
  u32 GetDirectorID() const override;
};

class RsImageDirector : public RsDllDirector {
public:
  RsImageDirector();
  u32 GetDirectorID() const override;
};

RsDirector *RsGetDirector();
RsResourceDirector *RsGetResourceDirector();
RsImageDirector *RsGetImageDirector();
ScFramework *RsGetFrameWork();
ScCom *RsGetCom();
u32 RsAddModuleRef();
u32 RsRemoveModuleRef();
SC3_API ScDirector *GZDllGetGZCOMDirector();

// Original IID80 maps ordered resource types to owned filename extensions, with its independent reference counter.
class RsExtensionMap : public ScUnknown {
public:
  RsExtensionMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Insert(u32 type, const ScStringView *extension);
  virtual u8 GetType(const ScStringView *extension, u32 *out);
  virtual u8 GetExtension(const u32 *type, ScStringView *out);
  virtual void Clear();
  virtual void CopyFrom(const RsExtensionMap *source);
  virtual ~RsExtensionMap();
  virtual u32 Size();
  std::map<u32, ScString> extensions;
  u32 references;
};

class RsFactoryInterface : public ScUnknown {
public:
  virtual bool Load(RsRecord *record, u32 iid, void **out) = 0;
  virtual bool New(u32 type, u32 iid, void **out) = 0;
  virtual bool ReadRecord(RsResource *resource, RsRecord *record) = 0;
  virtual bool WriteRecord(const RsResource *resource, RsRecord *record) = 0;
  // Original slot28 returns a zero byte in the recovered factory producers; nominal bool/BYTE remains unresolved.
  virtual u8 LockServer(u32 locks) = 0;
};

// The concrete factory base keeps the original distinct factory and module-reference interfaces.
class RsFactory : public RsFactoryInterface, public RsModuleObject {
public:
  RsFactory();
  ~RsFactory() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Load(RsRecord *record, u32 iid, void **out) override;
  bool ReadRecord(RsResource *resource, RsRecord *record) override;
  bool WriteRecord(const RsResource *resource, RsRecord *record) override;
  u8 LockServer(u32 locks) override;
  virtual bool SerialRead(RsResource *resource, RsStream *stream) = 0;
  virtual bool SerialWrite(RsResource *resource, RsStream *stream) = 0;
};

// Resource DLL's actual service base uses its module identity, rather than the executable's reference owner.
class RsServiceBase : public ScSystemService, public RsModuleObject {
public:
  RsServiceBase(u32 identifier, i32 priority);
  ~RsServiceBase() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 value) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool value) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool serviceRunning;
};

// Actual palette-file options identity5E1, distinct from the factory and module-reference identities.
class RsPaletteLoader : public ScUnknown {
public:
  virtual u8 LoadFile(const ScStringView *name, GxPaletteView **out) = 0;
};

// The installed resource palette has the ordinary twenty-byte palette first and the real resource base second.
class RsPaletteResource : public GxPalette, public RsResource {
public:
  RsPaletteResource();
  ~RsPaletteResource() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
};

class RsPaletteFactory : public RsFactory, public RsPaletteLoader {
public:
  RsPaletteFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
  u8 LoadFile(const ScStringView *name, GxPaletteView **out) override;
};

// The stock property factory constructs the real resource through COM and loads its separate property interface.
class RsPropertyFactory : public RsFactory {
public:
  RsPropertyFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
  virtual u32 ReadCookie(RsStream *stream);
};

// Original serialized-resource interface IID7A1E9434. Preserve raw transport AL for Read12 and Write16.
class RsSerializable : public ScUnknown {
public:
  virtual u8 Read(RsStream *stream) = 0;
  virtual u8 Write(RsStream *stream) = 0;
};

// This stock factory delegates construction by type to GZCOM and reading to the resource's own serialized interface.
class RsSerializableFactory : public RsFactory {
public:
  RsSerializableFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// The text factory derives from the real two-interface factory base and its module-owned reference implementation.
class RsTextFactory : public RsFactory {
public:
  RsTextFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

#endif
