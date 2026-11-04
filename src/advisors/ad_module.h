#ifndef SC3_ADVISORS_AD_MODULE_H
#define SC3_ADVISORS_AD_MODULE_H

#include "city/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "resources/resource.h"

// SIMADV's compiled copy of the city-agent base retains its owning city prefix.
class CtAgentBase : public CtAgent {
public:
  CtAgentBase();
  virtual ~CtAgentBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  CtAgentType *agent_type;
  u32 references;
};

class AdDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct AdClassRegistration {
  AdClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct AdDirectorClasses {
  AdDirectorClasses();
  ~AdDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  AdClassRegistration head;
};

// Recovered advisor-module director base; child directors and registered factory functions are borrowed.
class AdDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  AdDllDirector();
  virtual ~AdDllDirector();
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
  virtual void AddDirector(AdDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  ScArray<AdDllDirector *> children;
  AdDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class AdDirector : public AdDllDirector {
public:
  AdDirector();
  ~AdDirector() override;
  u32 GetDirectorID() const override;
};

ScUnknown *AdCreateBudget();
ScUnknown *AdCreateCityPlanning();
ScUnknown *AdCreateDemographics();
ScUnknown *AdCreateEnvironment();
ScUnknown *AdCreatePublicSafety();
ScUnknown *AdCreateTransportation();
ScUnknown *AdCreateUtilities();
ScUnknown *AdCreatePetitioners();

class ScDirector;
class ScFramework;
class ScMessageServer;
class RsManager;
class CtCity;
class ScLanguageManager;
class ApGameInterface;
class ApPreferences;
class ApBoxX;
class OcAttribCache;
class CtAgentTree;

// The director registers the eight concrete advisor and petitioner factories.
ScDirector *AdGetDirector();
ScFramework *AdGetFramework();
ScMessageServer *AdGetMessageServer();
RsManager *AdGetTextManager();
ScLanguageManager *AdGetLanguageManager();
ApGameInterface *AdGetApplication();
CtCity *AdGetCity();
ApPreferences *AdGetAppPreferences();
ApBoxX *AdGetBoxX();
OcAttribCache *AdGetOccupantAttribCache();
CtAgentTree *AdGetAgentTree();

// Three established DWORDs of SIMADV's local random receiver; its original private class name is unavailable.
class AdRandom {
public:
  explicit AdRandom(u32 seed);
  u32 Next(u32 bound);
  u32 state, initial_state, auxiliary;

private:
  void SeedStreams(u32 seed);
};

class RsDatabaseSegment;
class RsStream;
class CtCity;

// This unused ABI word selects the existing write-open constructor overload.
inline constexpr u32 kScArchiveWriteOverloadTag = 0x12345678;

// These scoped record/stream helpers are the common-library copies compiled into SIMADV.
class ScArchiveRecord {
public:
  ScArchiveRecord(RsDatabaseSegment *segment, RsKey key);
  // Writer overload retains the unused extra ABI word; its original nominal type remains unresolved.
  ScArchiveRecord(RsDatabaseSegment *segment, RsKey key, u32 unused);
  virtual ~ScArchiveRecord();
  virtual i32 IsValid();
  u8 failed;
  RsRecord *record;
  RsDatabaseSegment *segment;
};

class ScArchiveReader : public ScArchiveRecord {
public:
  ScArchiveReader(RsDatabaseSegment *segment, RsKey key);
  ScArchiveReader(RsDatabaseSegment *segment, RsKey key, u32 unused);
  ~ScArchiveReader() override;
  i32 IsValid() override;
  RsStream *GetStream();
  RsStream *stream;
};

// Empty virtual frame base identified by the final319A8 vtable reset and its deleting destructor2BD74.
class ScArchiveBase {
public:
  virtual ~ScArchiveBase();
};

class ScArchiveFrame : public ScArchiveBase {
public:
  ScArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  ScArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 flag);
  ~ScArchiveFrame() override;
  RsStream *GetStream();
  // Native effect phase of2BF36; its unconsumed original private return is not asserted.
  void Fail();
  u16 version;
  u8 flag, extension;
  u32 magic;
  ScArchiveReader *reader;
  u8 valid;
  u32 major, minor, patch;
};

#endif
