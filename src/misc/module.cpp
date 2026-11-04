#include "misc/module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "misc/api.h"
#include "misc/aura.h"
#include "misc/budget.h"
#include "misc/city_hall.h"
#include "misc/landmark.h"
#include "misc/neighbors.h"
#include "misc/ordinances.h"
#include "misc/world.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "window/api.h"

class ScMiscRegistryRef {
public:
  // Initialize the external output slot before acquiring the module-global registry; ignore the query predicate.
  ScMiscRegistryRef() : object(nullptr) {
    ScGetMiscDirector()->FrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  }

  // Release the live acquisition output after AppRegister, including any replacement made through its retained slot.
  ~ScMiscRegistryRef() {
    if (object)
      static_cast<ScRegistry *>(object)->Release();
  }

  void *object;
};

// Original module subclass adds registrations and its registry-specific AppRegister operation to the real common base.
class ScMiscDirector final : public ScDllDirector {
public:
  ScMiscDirector();
  static ScUnknown *CreateAura();
  static ScUnknown *CreateWorld();
  static ScUnknown *CreateNeighbors();
  static ScUnknown *CreateOrdinances();
  static ScUnknown *CreateCommandFactory();
  static ScUnknown *CreateWeather();
  static ScUnknown *CreateBudget();
  static ScUnknown *CreateMayorsHouse();
  static ScUnknown *CreatePark();
  static ScUnknown *CreateZoo();
  static ScUnknown *CreateBallPark();
  static ScUnknown *CreateMarina();
  static ScUnknown *CreateCityHall();
  static ScUnknown *CreateStatue();
  static ScUnknown *CreateSpaceport();
  static ScUnknown *CreateAirport();
  static ScUnknown *CreateStadium();
  static ScUnknown *CreateSeaport();
  static ScUnknown *CreateStockExchange();
  static ScUnknown *CreateCountryClub();
  static ScUnknown *CreatePrison();
  static ScUnknown *CreateToxicWastePlant();
  static ScUnknown *CreateCasino();
  static ScUnknown *CreateGigamall();
  static ScUnknown *CreateUniversity();
  static ScUnknown *CreateThemePark();
  static ScUnknown *CreateMilitaryBase();
  static ScUnknown *CreateGeyserPark();
  static ScUnknown *CreateCastle();
  static ScUnknown *CreateLandmark();
  static ScUnknown *CreateLighthouse();
  static ScUnknown *CreateDefenseContractor();
  static ScUnknown *CreatePerformingArtsCenter();
  static ScUnknown *CreateScienceCenter();
  static ScUnknown *CreateMedicalResearchCenter();
  static ScUnknown *CreateCourthouse();
  ~ScMiscDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

// Construct the actual aura receiver and publish its separate aura interface with the original zero object reference count.
ScUnknown *ScMiscDirector::CreateAura() {
  return static_cast<CtAuraLayer *>(new CtAura);
}

// Construct the real world receiver and publish its primary identity with no initial object or module hold.
ScUnknown *ScMiscDirector::CreateWorld() {
  return static_cast<CtWorldLayer *>(new CtWorld);
}

// Construct the actual neighbor receiver and publish its primary layer identity without adding an initial reference.
ScUnknown *ScMiscDirector::CreateNeighbors() {
  return static_cast<CtNeighborsLayer *>(new CtNeighbors);
}

// Construct the actual ordinance primary with its original zero reference count and separate city-layer identity.
ScUnknown *ScMiscDirector::CreateOrdinances() {
  return static_cast<CtOrdinanceLayer *>(new CtOrdinances);
}

// Construct the actual registered command factory without adding an initial object or module hold.
ScUnknown *ScMiscDirector::CreateCommandFactory() {
  return static_cast<ScCommandFactory *>(new CtCommandFactory);
}

// Construct the actual weather primary without adding an initial object or module hold.
ScUnknown *ScMiscDirector::CreateWeather() {
  return static_cast<EnWeatherLayer *>(new EnWeather);
}

// Construct the actual budget primary with its original zero initial reference count and real secondary layer identity.
ScUnknown *ScMiscDirector::CreateBudget() {
  return static_cast<EcBudgetLayer *>(new EcBudget);
}

// Construct the original mayor receiver and publish its building primary without adding an initial reference.
ScUnknown *ScMiscDirector::CreateMayorsHouse() {
  return static_cast<BdBuilding *>(new OcMayorsHouse);
}

// Construct the original park and return its building primary without adding a hold before the requested query.
ScUnknown *ScMiscDirector::CreatePark() {
  return static_cast<BdBuilding *>(new OcPark);
}

// Construct the actual zoo and publish its building primary before the requested interface query takes a hold.
ScUnknown *ScMiscDirector::CreateZoo() {
  return static_cast<BdBuilding *>(new OcZoo);
}

// Construct the original ball park with zero initial references and return its real building primary.
ScUnknown *ScMiscDirector::CreateBallPark() {
  return static_cast<BdBuilding *>(new OcBallPark);
}

// Construct the original marina and return its building primary before the requested interface acquires a hold.
ScUnknown *ScMiscDirector::CreateMarina() {
  return static_cast<BdBuilding *>(new OcMarina);
}

// Construct the actual dated City Hall receiver and return its building identity with zero initial references.
ScUnknown *ScMiscDirector::CreateCityHall() {
  return static_cast<BdBuilding *>(new OcCityHall);
}

// Construct the actual dated statue receiver before the requested interface query acquires its first hold.
ScUnknown *ScMiscDirector::CreateStatue() {
  return static_cast<BdBuilding *>(new OcStatue);
}

// Construct the actual dated animated spaceport and publish its zero-reference building primary.
ScUnknown *ScMiscDirector::CreateSpaceport() {
  return static_cast<BdBuilding *>(new OcSpaceport);
}

// Construct the original dated Stadium receiver and publish its building primary with no initial object hold.
ScUnknown *ScMiscDirector::CreateStadium() {
  return static_cast<BdBuilding *>(new OcStadium);
}

// Construct the real airport and return its building primary without adding an initial hold.
ScUnknown *ScMiscDirector::CreateAirport() {
  return static_cast<BdBuilding *>(new OcAirport);
}

// Construct the real seaport before the requested interface query takes its first reference.
ScUnknown *ScMiscDirector::CreateSeaport() {
  return static_cast<BdBuilding *>(new OcSeaport);
}

// Construct the actual dated stock-exchange receiver and return its zero-reference building primary.
ScUnknown *ScMiscDirector::CreateStockExchange() {
  return static_cast<BdBuilding *>(new OcStockExchange);
}

// Construct the actual dated courthouse and return its building identity with zero initial holds.
ScUnknown *ScMiscDirector::CreateCourthouse() {
  return static_cast<BdBuilding *>(new OcCourthouse);
}

// Construct the actual dated medical research center and return its building identity with zero initial holds.
ScUnknown *ScMiscDirector::CreateMedicalResearchCenter() {
  return static_cast<BdBuilding *>(new OcMedicalResearchCenter);
}

// Construct the actual dated science center and return its building identity with zero initial holds.
ScUnknown *ScMiscDirector::CreateScienceCenter() {
  return static_cast<BdBuilding *>(new OcScienceCenter);
}

// Construct the actual dated performing arts center and return its building identity with zero initial holds.
ScUnknown *ScMiscDirector::CreatePerformingArtsCenter() {
  return static_cast<BdBuilding *>(new OcPerformingArtsCenter);
}

// Construct the actual defense contractor and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateDefenseContractor() {
  return static_cast<BdBuilding *>(new OcDefenseContractor);
}

// Construct the actual lighthouse and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateLighthouse() {
  return static_cast<BdBuilding *>(new OcLighthouse);
}

// Construct the actual landmark and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateLandmark() {
  return static_cast<BdBuilding *>(new OcLandmark);
}

// Construct the actual castle and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateCastle() {
  return static_cast<BdBuilding *>(new OcCastle);
}

// Construct the original dated animated geyser park and return its building primary with zero initial references.
ScUnknown *ScMiscDirector::CreateGeyserPark() {
  return static_cast<BdBuilding *>(new OcGeyserPark);
}

// Construct the original military base with its propagation guard and return a zero-reference building.
ScUnknown *ScMiscDirector::CreateMilitaryBase() {
  return static_cast<BdBuilding *>(new OcMilitaryBase);
}

// Construct the original theme park with its propagation guard and return a zero-reference building.
ScUnknown *ScMiscDirector::CreateThemePark() {
  return static_cast<BdBuilding *>(new OcThemePark);
}

// Construct the original university with its propagation guard and return a zero-reference building.
ScUnknown *ScMiscDirector::CreateUniversity() {
  return static_cast<BdBuilding *>(new OcUniversity);
}

// Construct the actual gigamall and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateGigamall() {
  return static_cast<BdBuilding *>(new OcGigamall);
}

// Construct the actual casino and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateCasino() {
  return static_cast<BdBuilding *>(new OcCasino);
}

// Construct the actual toxic waste plant and publish its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateToxicWastePlant() {
  return static_cast<BdBuilding *>(new OcToxicWastePlant);
}

// Construct the actual maximum-security prison and return its zero-reference building primary.
ScUnknown *ScMiscDirector::CreatePrison() {
  return static_cast<BdBuilding *>(new OcPrison);
}

// Construct the original dated country club and return its building primary without an initial hold.
ScUnknown *ScMiscDirector::CreateCountryClub() {
  return static_cast<BdBuilding *>(new OcCountryClub);
}

// Register all original class IDs in source order while keeping each unavailable constructor explicit.
ScMiscDirector::ScMiscDirector() {
  AddClass(kCtAuraClassId, CreateAura, 0);
  AddClass(kCtWorldClassId, CreateWorld, 0);
  AddClass(kCtNeighborsClassId, CreateNeighbors, 0);
  AddClass(kCtOrdinancesClassId, CreateOrdinances, 0);
  AddClass(kEnWeatherClassId, CreateWeather, 0);
  AddClass(kEcBudgetClassId, CreateBudget, 0);
  AddClass(kOcMayorsHouseClassId, CreateMayorsHouse, 0);
  AddClass(kOcParkClassId, CreatePark, 0);
  AddClass(kOcZooClassId, CreateZoo, 0);
  AddClass(kOcBallParkClassId, CreateBallPark, 0);
  AddClass(kOcMarinaClassId, CreateMarina, 0);
  AddClass(kOcCityHallClassId, CreateCityHall, 0);
  AddClass(kOcStatueClassId, CreateStatue, 0);
  AddClass(kOcSpaceportClassId, CreateSpaceport, 0);
  AddClass(kOcAirportClassId, CreateAirport, 0);
  AddClass(kOcSeaportClassId, CreateSeaport, 0);
  AddClass(kOcStadiumClassId, CreateStadium, 0);
  AddClass(kOcStockExchangeClassId, CreateStockExchange, 0);
  AddClass(kOcGigamallClassId, CreateGigamall, 0);
  AddClass(kOcCasinoClassId, CreateCasino, 0);
  AddClass(kOcCourthouseClassId, CreateCourthouse, 0);
  AddClass(kOcMedicalResearchCenterClassId, CreateMedicalResearchCenter, 0);
  AddClass(kOcScienceCenterClassId, CreateScienceCenter, 0);
  AddClass(kOcPerformingArtsCenterClassId, CreatePerformingArtsCenter, 0);
  AddClass(kOcMilitaryBaseClassId, CreateMilitaryBase, 0);
  AddClass(kOcDefenseContractorClassId, CreateDefenseContractor, 0);
  AddClass(kOcGeyserParkClassId, CreateGeyserPark, 0);
  AddClass(kOcLighthouseClassId, CreateLighthouse, 0);
  AddClass(kOcCountryClubClassId, CreateCountryClub, 0);
  AddClass(kOcThemeParkClassId, CreateThemePark, 0);
  AddClass(kOcLandmarkClassId, CreateLandmark, 0);
  AddClass(kOcToxicWastePlantClassId, CreateToxicWastePlant, 0);
  AddClass(kOcPrisonClassId, CreatePrison, 0);
  AddClass(kOcUniversityClassId, CreateUniversity, 0);
  AddClass(kOcCastleClassId, CreateCastle, 0);
  AddClass(kCtCommandFactoryClassId, CreateCommandFactory, 0);
}

ScMiscDirector::~ScMiscDirector() {
}

u32 ScMiscDirector::GetDirectorID() const {
  return kScMiscDirectorId;
}

bool ScMiscDirector::OnStart(ScCom *) {
  ScMiscRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

// Build and register the original game-command registry name with its exact lowercase category, key and value.
// Preserve the result/key/value construction and reverse destruction order around both registry calls.
bool ScMiscDirector::AppRegister(ScRegistry *registry) {
  ScString name, key("0xa30f7da9"), value("0");
  bool valid = registry->MakeRegistryName("cogamecmd", key, value, &name);
  if (valid)
    valid = registry->Register(name);
  return valid;
}

alignas(ScMiscDirector) static u8 ScMiscDirectorStorage[sizeof(ScMiscDirector)];
static u8 ScMiscDirectorInitialized;

static void ScDestroyMiscDirector() {
  reinterpret_cast<ScMiscDirector *>(ScMiscDirectorStorage)->~ScMiscDirector();
}

// Publish the initialization bit before placement construction, then register the original exit-time destruction.
// Reentrant lookup observes the same static receiver rather than another director or a new synchronization protocol.
ScDirector *ScGetMiscDirector() {
  if (!(ScMiscDirectorInitialized & 1)) {
    ScMiscDirectorInitialized |= 1;
    new (static_cast<void *>(ScMiscDirectorStorage)) ScMiscDirector;
    atexit(ScDestroyMiscDirector);
  }
  return static_cast<ScDirector *>(reinterpret_cast<ScMiscDirector *>(ScMiscDirectorStorage));
}

#if !defined(SC3_MONOLITHIC)
// Borrow the module's single original director through its actual DLL export without adding a reference.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return ScGetMiscDirector();
}
#endif

static WnWindowManager *ScMiscWindows;
static WnResources *ScMiscWindowResources;
static RsManager *ScMiscResources;
static ScCommandFactory *ScMiscCommands;
static ScMessageServer *ScMiscMessages;
static ScLanguageManager *ScMiscLanguage;
static void *ScMiscApplication;
static u8 ScMiscApplicationInitialized;

static void ScMiscApplicationExit() {
}

// Set the one-time bit before querying directly into the live application slot, then register the original empty exit
// hook.
// Ignore the query predicate and preserve both published reentry and the original absence of a retry.
ApGameInterface *ScGetMiscApplication() {
  if (!(ScMiscApplicationInitialized & 1)) {
    ScMiscApplicationInitialized |= 1;
    ScGetMiscDirector()->FrameWork()->Application()->QueryInterface(IID_ApGameInterface, &ScMiscApplication);
    atexit(ScMiscApplicationExit);
  }
  return static_cast<ApGameInterface *>(ScMiscApplication);
}

// Cache this module's message server before releasing its temporary service reference.
// An unpublished server is queried again on the next call, including after a failed lookup.
ScMessageServer *ScGetMiscMessageServer() {
  if (!ScMiscMessages) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMiscMessages = static_cast<ScMessageServer *>(object);
    if (ScMiscMessages)
      ScMiscMessages->Release();
  }
  return ScMiscMessages;
}

// Cache this module's language manager before releasing its temporary service reference.
// A null publication remains retryable and the lookup predicate is not substituted for the published pointer.
ScLanguageManager *ScGetMiscLanguageManager() {
  if (!ScMiscLanguage) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScMiscLanguage = static_cast<ScLanguageManager *>(object);
    if (ScMiscLanguage)
      ScMiscLanguage->Release();
  }
  return ScMiscLanguage;
}

// Read the current borrowed city from the optional application on each call rather than caching a changing city
// identity.
CtCity *ScGetMiscCity() {
  ApGameInterface *application = ScGetMiscApplication();
  return application ? application->GetCity() : nullptr;
}

// Publish the borrowed window-manager cache before retiring its temporary acquisition hold.
// A missing service remains null and is queried again on the next call, independent of the query's Boolean reply.
WnWindowManager *ScGetMiscWindowManager() {
  if (!ScMiscWindows) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    WnWindowManager *windows = static_cast<WnWindowManager *>(object);
    ScMiscWindows = windows;
    if (windows)
      windows->Release();
  }
  return ScMiscWindows;
}

// Publish this module's borrowed window-resource service before releasing the temporary lookup hold.
// Retry a null cache on a later call, preserving reentry during the release and ignoring the lookup predicate.
WnResources *ScGetMiscWindowResources() {
  if (!ScMiscWindowResources) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    WnResources *resources = static_cast<WnResources *>(object);
    ScMiscWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return ScMiscWindowResources;
}

// Borrow this module's resource manager through its own framework and cache it before retiring the acquisition hold.
// A null result remains eligible for another lookup; a release callback observes the already published pointer.
RsManager *ScGetMiscResourceManager() {
  if (!ScMiscResources) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    ScMiscResources = manager;
    if (manager)
      manager->Release();
  }
  return ScMiscResources;
}

// Publish the module's borrowed command-factory cache before retiring its temporary service acquisition.
// A missing service retries on a later call; the original query predicate is ignored.
ScCommandFactory *ScGetMiscCommandFactory() {
  if (!ScMiscCommands) {
    void *object = nullptr;
    ScGetMiscDirector()->FrameWork()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &object);
    ScCommandFactory *factory = static_cast<ScCommandFactory *>(object);
    ScMiscCommands = factory;
    if (factory)
      factory->Release();
  }
  return ScMiscCommands;
}

// Borrow the current application's occupant-attribute cache on every call, preserving the optional application result.
OcAttribCache *ScGetMiscAttribCache() {
  ApGameInterface *application = ScGetMiscApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Borrow the application agent tree on each call, returning null only when the optional application is absent.
CtAgentTree *ScGetMiscAgentTree() {
  ApGameInterface *application = ScGetMiscApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

ScGameCommandFactory::ScGameCommandFactory() : references(0) {
}

// Publish the primary identity for Unknown or the actual command-factory interface, then take its live virtual hold.
// Unsupported identifiers clear the output.
bool ScGameCommandFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining this module's director, then return the live field after that operation.
u32 ScGameCommandFactory::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

// Decrement before module retirement and decide deletion from the live count after the director operation.
// The original zero-count decrement wraps; no new zero guard changes that contract.
u32 ScGameCommandFactory::Release() {
  --references;
  ScGetMiscDirector()->RemoveRef();
  u32 value = references;
  if (!value) {
    delete this;
    return 0;
  }
  return value;
}

// Ask children in source order with the original three arguments and stop at the first successful publication.
// Retain the captured cursor while observing the live end before falling through to the actual subclass operation.
bool ScGameCommandFactory::GetCommand(u32 classId, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 i = 0; i < children.size; ++i, ++cursor)
    if ((*cursor)->GetCommand(classId, iid, out))
      return true;
  return CreateCommand(classId, iid, out);
}

// Refuse a null factory; otherwise retain it before appending its actual interface pointer.
bool ScGameCommandFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

// Member destruction retires the sequence storage after the original release walk.
ScGameCommandFactory::~ScGameCommandFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

CtCommandFactory::CtCommandFactory() {
}

CtCommandFactory::~CtCommandFactory() {
}

// Construct the actual show-budget command for its single class identifier, then query the requested primary interface.
// A refused interface destroys the zero-reference command; unsupported classes clear the output before returning false.
bool CtCommandFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  if (classId != kCtBudgetCommandClassId) {
    *out = nullptr;
    return false;
  }
  CtShowBudgetCommand *command = new CtShowBudgetCommand;
  if (!command->QueryInterface(iid, out)) {
    delete command;
    return false;
  }
  return true;
}

ScMiscRefCount::ScMiscRefCount() : references(0) {
}

ScMiscRefCount::~ScMiscRefCount() {
}

u32 ScMiscRefCount::AddRef() {
  ScGetMiscDirector()->AddRef();
  return ++references;
}

// A zero count stays zero. Otherwise remove the module reference before decrementing the live count.
u32 ScMiscRefCount::RemoveRef() {
  if (references) {
    ScGetMiscDirector()->RemoveRef();
    --references;
  }
  return references;
}

// Consult the virtual removal result and destroy through the secondary lifetime interface only when it returns zero.
u32 ScMiscRefCount::Release() {
  u32 value = RemoveRef();
  if (!value)
    delete this;
  return value;
}

u32 ScMiscRefCount::RefCount() {
  return references;
}

ScGameCommand::ScGameCommand() {
}

// Drain pending inputs before outputs, transferring each pointer out of its queue before releasing it.
// The descriptor sequences and backing storage retire after the queued argument callbacks finish.
ScGameCommand::~ScGameCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool ScGameCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScGameCommand::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 ScGameCommand::Release() {
  return ScMiscRefCount::Release();
}

// Retain the supplied input before publishing it at the back of the pending argument sequence.
bool ScGameCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Leave an empty output untouched; otherwise publish the oldest retained input before removing its queue entry.
bool ScGameCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 ScGameCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate input descriptors in insertion order, observing descriptors appended by the callback before testing the end.
bool ScGameCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    ScCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain the supplied output before publishing it at the back of the result sequence.
bool ScGameCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

bool ScGameCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 ScGameCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate output descriptors in order and pass the original separate type, flags and context arguments.
bool ScGameCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    ScCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool ScGameCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool ScGameCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

// Set the live factory's memory/color options before loading the actual buffer directly into this wrapper's field.
// Retire acquired options before the factory; a missing factory or options interface leaves the buffer empty.
ScBuffer::ScBuffer(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = ScGetMiscResourceManager();
  RsFactoryInterface *factory;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      Load(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the retained buffer before clearing its field; no application or factory holds belong to this scope.
ScBuffer::~ScBuffer() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

// Borrow the resolved buffer through the original folded next-word getter without adding a reference.
GxBuffer *ScBuffer::GetBuffer() {
  return buffer;
}

bool ScBuffer::Load(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  return manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Acquire the localized String through this DLL's single resource-manager cache, shared with image loading.
// Resolve directly into the retained field and clear it when the actual resource query refuses the key.
CtMiscTextResource::CtMiscTextResource(u32 instance, u32 group)
    : text(nullptr), key{kRsTextResourceType, group, instance} {
  if (!ScGetMiscResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

CtMiscTextResource::~CtMiscTextResource() {
  ScRelease(&text);
}

// Copy the retained String through its actual interface, or construct the original empty owned String when absent.
ScString CtMiscTextResource::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

static bool CtMiscTokenEquals(const ScString &token, const char *name, u32 length) {
  return (u32)(token.end - token.begin) == length && !memcmp(token.begin, name, length);
}

// Return borrowed preferences through this module's optional application without another application cache.
static ApPreferences *CtGetMiscPreferences() {
  ApGameInterface *application = ScGetMiscApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

bool SC3_STDCALL CtTryMiscToken(ScString *text, const ScString &token, u32 *position) {
  u32 tokenLength = token.Strlen(), start = *position, length = (u32)(text->end - text->begin);
  u32 count = start < length ? length - start : 0;
  if (count > tokenLength)
    count = tokenLength;
  ScString candidate = start < length ? ScString(text->begin + start, count) : ScString();
  if ((u32)(candidate.end - candidate.begin) != (u32)(token.end - token.begin) ||
      memcmp(candidate.begin, token.begin, (usize)(candidate.end - candidate.begin)))
    return false;
  if (CtMiscTokenEquals(token, "%MAYOR%", 7) || CtMiscTokenEquals(token, "%YOURNAME%", 10)) {
    CtCity *city = ScGetMiscCity();
    if (city) {
      ScStringView *sizeName = city->GetMayorName(), *dataName = city->GetMayorName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetMayorName()->Strlen() - 1;
    }
    return true;
  }
  if (CtMiscTokenEquals(token, "%CITYNAME%", 10) || CtMiscTokenEquals(token, "%YOURCITY%", 10)) {
    CtCity *city = ScGetMiscCity();
    if (city) {
      ScStringView *sizeName = city->GetCityName(), *dataName = city->GetCityName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetCityName()->Strlen() - 1;
    }
    return true;
  }
  if (CtMiscTokenEquals(token, "%POPULATION%", 12)) {
    ScString fallback("0");
    CtCity *city = ScGetMiscCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", population);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, (u32)(number.end - number.begin));
      *position += (u32)(number.end - number.begin) - 1;
    } else {
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position += (u32)(fallback.end - fallback.begin) - 1;
    }
    return true;
  }
  if (CtMiscTokenEquals(token, "%YEAR%", 6)) {
    CtCity *city = ScGetMiscCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", (i32)year);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, (u32)(number.end - number.begin));
      *position += (u32)(number.end - number.begin) - 1;
    } else {
      ScString fallback("1900");
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position += (u32)(fallback.end - fallback.begin) - 1;
    }
    return true;
  }
  if (CtMiscTokenEquals(token, "%ANYNEIGHBOR%", 13)) {
    CtCity *city = ScGetMiscCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return false;
    ScRandom random(kScRandomSeedFromClock);
    ScString name;
    neighbors->GetNeighborName((u8)random.Bounded(4), &name);
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, (u32)(name.end - name.begin));
    *position += (u32)(name.end - name.begin) - 1;
    return true;
  }
  if (CtMiscTokenEquals(token, "%PARADENAME%", 12)) {
    ScString name;
    i32 country = CtGetMiscPreferences()->GetCountry();
    u32 instance = 0;
    switch (country) {
    case 6:
      instance = 0x258;
      break;
    case 17:
      instance = 0x259;
      break;
    case 19:
      instance = 0x25a;
      break;
    case 31:
      instance = 0x25b;
      break;
    case 32:
      instance = 0x25c;
      break;
    case 45:
      instance = 0x25d;
      break;
    case 46:
      instance = 0x25e;
      break;
    case 49:
      instance = 0x25f;
      break;
    case 54:
      instance = 0x260;
      break;
    }
    if (instance) {
      CtMiscTextResource resource(instance, kRsSupplementalGameTextGroup);
      name = resource.GetText();
    }
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, (u32)(name.end - name.begin));
    *position += (u32)(name.end - name.begin) - 1;
  }
  return true;
}

static const ScString CtMiscTokenNames[8] = {ScString("%MAYOR%", 7), ScString("%YOURNAME%", 10), ScString("%CITYNAME%", 10),
    ScString("%YOURCITY%", 10), ScString("%POPULATION%", 12), ScString("%YEAR%", 6), ScString("%PARADENAME%", 12),
    ScString("%ANYNEIGHBOR%", 13)};

static u32 CtMiscFindPercent(const ScString &text, u32 start) {
  u32 length = text.Strlen();
  const char *bytes = text.Data();
  for (u32 i = start; i < length; ++i)
    if (bytes[i] == '%')
      return i;
  return UINT32_MAX;
}

u32 SC3_STDCALL CtExpandMiscTokens(ScString *text) {
  u32 position = CtMiscFindPercent(*text, 0);
  while (position < text->Strlen()) {
    CtSubstituteMiscToken(text, &position);
    position = CtMiscFindPercent(*text, position + 1);
  }
  return position;
}

// Try the eight original names in their short-circuit order, with neighbor preceding parade despite static init order.
// The real leaf updates the shared text and position before the next live scan.
bool SC3_STDCALL CtSubstituteMiscToken(ScString *text, u32 *position) {
  return CtTryMiscToken(text, CtMiscTokenNames[0], position) || CtTryMiscToken(text, CtMiscTokenNames[1], position) ||
      CtTryMiscToken(text, CtMiscTokenNames[2], position) || CtTryMiscToken(text, CtMiscTokenNames[3], position) ||
      CtTryMiscToken(text, CtMiscTokenNames[4], position) || CtTryMiscToken(text, CtMiscTokenNames[5], position) ||
      CtTryMiscToken(text, CtMiscTokenNames[7], position) || CtTryMiscToken(text, CtMiscTokenNames[6], position);
}
