#include "advisors/ad_module.h"

#include "advisors/ad_ref_count.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "resources/api.h"
#include "resources/stream.h"

static constexpr u32 kAdRecordFrameMarker = 0xdeadbeef;

CtAgentBase::CtAgentBase() : agent_type(nullptr), references(0) {
}

CtAgentBase::~CtAgentBase() {
  ScRelease(&agent_type);
}

// Clear the genuine output before matching Unknown or CityAgent. Retain the published interface through virtual AddRef.
bool CtAgentBase::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_ScUnknown || iid == IID_CtAgent)
    *out = static_cast<CtAgent *>(this);
  if (!*out)
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

// Increment the object count before retaining the real module director, then return the live object count.
u32 CtAgentBase::AddRef() {
  ++references;
  AdGetDirector()->AddRef();
  return references;
}

// Drop a nonzero object reference before the module reference. Delete through the actual virtual destructor at zero.
u32 CtAgentBase::Release() {
  if (references) {
    --references;
    AdGetDirector()->RemoveRef();
  }
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

bool CtAgentBase::Simulate(i32 elapsed) {
  return agent_type->Simulate(this, elapsed);
}

i32 CtAgentBase::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool CtAgentBase::IsAgentLocking() {
  return true;
}

// Invoke the attached type's ID query for its original observable effects, then preserve the base method's zero result.
u32 CtAgentBase::TypeId() const {
  if (agent_type)
    agent_type->Id();
  return 0;
}

// Borrow the retained agent type without acquiring another reference.
CtAgentType *CtAgentBase::AgentType() {
  return agent_type;
}

// Release the former type before publishing and retaining its replacement. The original requires a nonnull replacement.
bool CtAgentBase::SetAgentType(CtAgentType *type) {
  if (agent_type)
    agent_type->Release();
  agent_type = type;
  type->AddRef();
  return true;
}

bool CtAgentBase::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtAgentBase::DebugClassTag(ScStringView *) {
  return false;
}

bool CtAgentBase::DebugTypeTag(ScStringView *) {
  return false;
}

// Borrow the application's actual agent-type tree through slot64; an absent application returns null.
CtAgentTree *AdGetAgentTree() {
  ApGameInterface *application = AdGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

static bool AdApplicationInitialized;
static ApGameInterface *AdApplication;

// Query the application interface exactly once, publishing the construction flag before the external calls.
// Keep the original borrowed module-lifetime pointer even when QueryInterface reports failure; missing output never retries.
ApGameInterface *AdGetApplication() {
  if (!AdApplicationInitialized) {
    AdApplicationInitialized = true;
    AdGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&AdApplication));
  }
  return AdApplication;
}

// Obtain the current city from the cached game application. A missing application yields the original null result.
CtCity *AdGetCity() {
  ApGameInterface *application = AdGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the current application's occupant-attribute cache through its real service getter.
// A missing application returns the original null pointer without retaining a replacement service.
OcAttribCache *AdGetOccupantAttribCache() {
  ApGameInterface *application = AdGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Borrow the application's actual BoxX event interface, returning the original null pointer when no application exists.
ApBoxX *AdGetBoxX() {
  ApGameInterface *application = AdGetApplication();
  return application ? application->GetBoxX() : nullptr;
}

ScString AdStopForcingAdvice("stop forcing advice", 19);
ScString AdForceMortimerToSay("force Mortimer to say", 21);
ScString AdForceMoeToSay("force Moe to say", 16);
ScString AdForceConstanceToSay("force Constance to say", 21);
ScString AdForceMariaToSay("force Maria to say", 18);
ScString AdForceRandallToSay("force Randall to say", 20);
ScString AdForceGusToSay("force Gus to say", 16);
ScString AdForceKarenToSay("force Karen to say", 18);
ScString AdForcePetitionersToSay("force petitioners to say", 24);
ScString AdGarbageInGarbageOut("garbage in, garbage out", 23);
ScString AdPowerToTheMasses("power to the masses", 19);
ScString AdWaterInTheDesert("water in the desert", 19);
ScString AdPayTributeToYourKing("pay tribute to your king", 24);
ScString AdZyxwvu("zyxwvu", 6);
ScString AdCallCousinVinnie("call cousin Vinnie", 18);
ScString AdLetsMakeADeal("let's make a deal", 17);

AdDirectorClasses::AdDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
AdDirectorClasses::~AdDirectorClasses() {
  while (head.next != &head) {
    AdClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void AdDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  AdClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  AdClassRegistration *entry = new AdClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

AdDllDirector::AdDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

AdDllDirector::~AdDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool AdDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdDllDirector::AddRef() {
  return ++references;
}

u32 AdDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 AdDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 AdDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *AdDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *AdDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool AdDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its own children, then append the child without a retain.
void AdDllDirector::AddDirector(AdDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void AdDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (AdClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool AdDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (AdClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
    if (entry->clsid != clsid)
      continue;
    ScUnknown *object = entry->create();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let live children veto unloading before checking this director's own reference word.
// The installed PC body differs from the available SDK's reference-count shortcut.
bool AdDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool AdDllDirector::OnUnload() {
  return true;
}

bool AdDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool AdDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool AdDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  AdGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool AdDllDirector::PreFrameWorkInit() {
  return true;
}

bool AdDllDirector::PreAppInit() {
  return true;
}

bool AdDllDirector::PostAppInit() {
  return true;
}

bool AdDllDirector::PreAppShutdown() {
  return true;
}

bool AdDllDirector::PostAppShutdown() {
  return true;
}

bool AdDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool AdDllDirector::AbortiveQuit() {
  return true;
}

bool AdDllDirector::OnInstall() {
  return true;
}

// Register the original seven advisor factories and petitioner manager without constructing their objects.
AdDirector::AdDirector() {
  classes.Insert(kAdBudgetAdvisorClassId, AdCreateBudget, 0);
  classes.Insert(kAdCityPlanningAdvisorClassId, AdCreateCityPlanning, 0);
  classes.Insert(kAdDemographicsAdvisorClassId, AdCreateDemographics, 0);
  classes.Insert(kAdEnvironmentAdvisorClassId, AdCreateEnvironment, 0);
  classes.Insert(kAdPublicSafetyAdvisorClassId, AdCreatePublicSafety, 0);
  classes.Insert(kAdTransportationAdvisorClassId, AdCreateTransportation, 0);
  classes.Insert(kAdUtilitiesAdvisorClassId, AdCreateUtilities, 0);
  classes.Insert(kAdPetitionersClassId, AdCreatePetitioners, 0);
}

AdDirector::~AdDirector() {
}

u32 AdDirector::GetDirectorID() const {
  return kAdDirectorId;
}

// Construct and register the module director on first access, with normal static destruction at process exit.
ScDirector *AdGetDirector() {
  static AdDirector director;
  return &director;
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Return the same borrowed module director through the original loader export at ordinal one.
// Static integration omits the per-DLL C symbol so each module keeps its independent director.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return AdGetDirector();
}
#endif

static ScMessageServer *AdMessageServer;
static RsManager *AdTextManager;
static ScLanguageManager *AdLanguageManager;

// Obtain the module framework through the real director interface. No ownership reference is added.
ScFramework *AdGetFramework() {
  return AdGetDirector()->FrameWork();
}

// Cache the framework message service before releasing its acquisition reference. Recursive lookups see the cache.
ScMessageServer *AdGetMessageServer() {
  if (!AdMessageServer) {
    void *object = nullptr;
    AdGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    AdMessageServer = server;
    if (server)
      server->Release();
  }
  return AdMessageServer;
}

// Cache the text resource service before dropping its acquisition reference. This is the original text-manager service IID.
RsManager *AdGetTextManager() {
  if (!AdTextManager) {
    void *object = nullptr;
    AdGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    AdTextManager = manager;
    if (manager)
      manager->Release();
  }
  return AdTextManager;
}

// Cache the language manager before dropping its acquisition reference so reentrant lookup observes the same manager.
// Failed acquisition leaves the cache empty and the next lookup retries the original framework request.
ScLanguageManager *AdGetLanguageManager() {
  if (!AdLanguageManager) {
    void *object = nullptr;
    AdGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(object);
    AdLanguageManager = manager;
    if (manager)
      manager->Release();
  }
  return AdLanguageManager;
}

// Return the borrowed application-preferences interface through the existing module application cache.
// An unavailable application gives the original null pointer; callers retain their own actual input invariants.
ApPreferences *AdGetAppPreferences() {
  ApGameInterface *application = AdGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

AdRandom::AdRandom(u32 seed) {
  SeedStreams(seed);
  auxiliary = 0;
}

// Both streams receive one tick reading for the sentinel, or the exact supplied DWORD otherwise.
void AdRandom::SeedStreams(u32 seed) {
  if (seed == kScRandomSeedFromClock)
    seed = ScSystemMilliseconds();
  state = seed;
  initial_state = seed;
}

// Advance the original multiply/add stream with its mixed high-word result, preserving both bounded branches.
// Zero consumes no state; large bounds use inclusive rejection, with the BSR mask reduced to its defined identity.
u32 AdRandom::Next(u32 bound) {
  if (!bound)
    return 0;
  u32 next = state, value;
  do {
    u64 product = static_cast<u64>(next) * kScRandomMultiplier;
    u32 low = static_cast<u32>(product), high = static_cast<u32>(product >> 32) << 16;
    next = low + kScRandomIncrement;
    high += 65535u + (next < low);
    value = (high & kScRandomRetainedHighWordMask) | (next >> 16);
    if (bound < kScRandomModuloThreshold) {
      value %= bound;
      break;
    }
  } while (value > bound);
  state = next;
  return value;
}

ScRefCount::ScRefCount() : references(0) {
}

ScRefCount::~ScRefCount() {
}

// Invoke the real virtual decrement operation before deleting through the complete object's virtual destructor at zero.
u32 ScRefCount::Release() {
  u32 remaining = RemoveRef();
  if (!remaining)
    delete this;
  return remaining;
}

u32 ScRefCount::AddRef() {
  AdGetDirector()->AddRef();
  return ++references;
}

// Release the module before decrementing a nonzero count, leaving zero unchanged and never deleting the object here.
u32 ScRefCount::RemoveRef() {
  if (references) {
    AdGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScRefCount::GetReferenceCount() const {
  return references;
}

ScArchiveRecord::ScArchiveRecord(RsDatabaseSegment *owner, RsKey key) : failed(0), record(nullptr), segment(owner) {
  if (segment && !segment->OpenRecord(key, &record))
    record = nullptr;
}

ScArchiveRecord::ScArchiveRecord(RsDatabaseSegment *owner, RsKey key, u32)
    : failed(0), record(nullptr), segment(owner) {
  if (segment && !segment->OpenRecordForWrite(key, &record))
    record = nullptr;
}

ScArchiveRecord::~ScArchiveRecord() {
  if (record) {
    if (!failed)
      failed = !segment->CloseRecord(&record);
    if (record && failed)
      segment->AbortRecord(&record);
  }
}

i32 ScArchiveRecord::IsValid() {
  return record != nullptr;
}

// Query the opened record for its genuine stream identity without retaining the segment separately.
ScArchiveReader::ScArchiveReader(RsDatabaseSegment *segment, RsKey key)
    : ScArchiveRecord(segment, key), stream(nullptr) {
  if (ScArchiveRecord::IsValid() && !record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)))
    stream = nullptr;
}

ScArchiveReader::ScArchiveReader(RsDatabaseSegment *segment, RsKey key, u32 unused)
    : ScArchiveRecord(segment, key, unused), stream(nullptr) {
  if (ScArchiveRecord::IsValid() && !record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)))
    stream = nullptr;
}

ScArchiveReader::~ScArchiveReader() {
  ScRelease(&stream);
}

i32 ScArchiveReader::IsValid() {
  return ScArchiveRecord::IsValid() && stream != nullptr;
}

// Borrow the record scope's queried stream without another retain.
RsStream *ScArchiveReader::GetStream() {
  return stream;
}

ScArchiveBase::~ScArchiveBase() {
}

// Read the framed header, then reopen the same record at its start when its magic selects the legacy unframed form.
ScArchiveFrame::ScArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : version(0), flag(0), extension(0), magic(0), reader(nullptr), valid(0), major(0), minor(0), patch(0) {
  if (!city)
    return;
  major = city->GetVersionComponentFromSaveFile(0);
  minor = city->GetVersionComponentFromSaveFile(1);
  patch = city->GetVersionComponentFromSaveFile(2);
  reader = new ScArchiveReader(segment, key);
  valid = static_cast<u8>(reader->IsValid());
  if (!valid) {
    delete reader;
    reader = nullptr;
    return;
  }
  valid = reader->GetStream()->ReadU16(&version);
  if (valid) {
    u8 packed = 0;
    valid = reader->GetStream()->ReadU8(&packed);
    flag = packed & 1;
    extension = (packed & 2) != 0;
    if (extension && valid)
      valid = reader->GetStream()->ReadU8(&packed);
  }
  if (valid)
    reader->GetStream()->ReadU32(&magic);
  if (valid)
    valid = magic == kAdRecordFrameMarker;
  if (magic != kAdRecordFrameMarker) {
    flag = 0;
    version = 0;
    magic = kAdRecordFrameMarker;
    delete reader;
    reader = nullptr;
    reader = new ScArchiveReader(segment, key);
    valid = static_cast<u8>(reader->IsValid());
    if (!valid) {
      delete reader;
      reader = nullptr;
    }
  }
}

// Open a write-side scope after reading current city versions, then write version and flags as two16-bit values.
// Preserve ignored magic-write refusal and ordinary closing of a scope retired after an earlier header refusal.
ScArchiveFrame::ScArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 input_version, u8 input_flag)
    : version(input_version), flag(input_flag), extension(0), magic(0), reader(nullptr), valid(0), major(0), minor(0),
      patch(0) {
  if (!city)
    return;
  major = city->GetVersionComponent(0);
  minor = city->GetVersionComponent(1);
  patch = city->GetVersionComponent(2);
  reader = new ScArchiveReader(segment, key, 0);
  valid = static_cast<u8>(reader->IsValid());
  if (valid) {
    valid = reader->GetStream()->WriteU16(version);
    if (valid)
      valid = reader->GetStream()->WriteU16(static_cast<u16>(flag ? 3 : 2));
    if (valid)
      reader->GetStream()->WriteU32(kAdRecordFrameMarker);
  }
  if (!valid) {
    delete reader;
    reader = nullptr;
  }
}

ScArchiveFrame::~ScArchiveFrame() {
  ScDelete(&reader);
}

// Borrow the current reader's stream, including a retained scope whose valid byte was cleared separately.
RsStream *ScArchiveFrame::GetStream() {
  return reader ? reader->GetStream() : nullptr;
}

// Mark the reader for abort during destruction and clear only the frame's valid byte.
void ScArchiveFrame::Fail() {
  if (reader)
    reader->failed = 1;
  valid = 0;
}
