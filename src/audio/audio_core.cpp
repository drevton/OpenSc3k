#include "audio/audio_core.h"

#include <stdlib.h>

#include <cassert>
#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "audio/audio_effects.h"
#include "audio/audio_freshness.h"
#include "audio/audio_music.h"
#include "audio/callback_timer.h"
#include "audio/local_radio.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "city/sc_stopwatch.h"
#include "city/services.h"
#include "common/ids.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "economy/api.h"
#include "graphics/sc3_graphics.h"
#include "network/nw_manager.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "sprites/view_camera.h"
#include "terrain/api.h"
#include "window/api.h"

// The population event stores the city count used to select the traffic ambience Z position at zoom three.
enum AuTrafficAmbiencePopulation : i32 {
  kAuLargeCityTrafficAmbiencePopulation = 0x249f0,
  kAuMediumCityTrafficAmbiencePopulation = 0xc350,
};

// The AUDIO command factory constructs its radio command only for this command identity.
enum AuRadioCommandIdentity : u32 {
  kAuRadioCommandId = 0x40f30d8c,
};

// Radio commands describe an operation followed by its optional integer argument.
enum AuRadioParameterId : u32 {
  kAuRadioOperationParameterId = 0x10d2da5,
  kAuRadioOptionalArgumentParameterId = 0x210d2dac,
};

// The registered AUDIO factory initializes its otherwise unwritten reference counter to avoid indeterminate reads.
class AuAudioFactory final : public ScUnknown {
public:
  AuAudioFactory();
  ~AuAudioFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateManager(ApBoxX **out);
  u8 CreateEffect(void **out);
  // These five shared hooks pop one 32-bit argument each; their nominal parameter types remain unresolved.
  u8 UnresolvedSlot20(u32 value);
  u8 UnresolvedSlot24(u32 value);
  u8 UnresolvedSlot28(u32 value);
  u8 UnresolvedSlot32(u32 value);
  u8 UnresolvedSlot36(u32 value);
  u8 InitializeManager(u32 value, ApBoxX **out);

private:
  u32 references;
};

AuAudioFactory::AuAudioFactory() : references(0) {
}

AuAudioFactory::~AuAudioFactory() {
}

// Publish the factory for its registered or unknown identity and retain it when the identity matches.
bool AuAudioFactory::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_ScUnknown || iid == IID_AuAudioFactory)
    *out = this;
  if (!*out)
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

// Increment the factory reference count before retaining the AUDIO director.
u32 AuAudioFactory::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and retire the factory when its count reaches zero.
u32 AuAudioFactory::Release() {
  if (references) {
    AuGetAudioDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Construct and retain an AUDIO manager, then publish its primary application interface.
u8 AuAudioFactory::CreateManager(ApBoxX **out) {
  AuAudioManager *manager = new AuAudioManager;
  manager->AddRef();
  *out = static_cast<ApBoxX *>(manager);
  return 1;
}

u8 AuAudioFactory::CreateEffect(void **out) {
  AuSingleEffect *effect = new AuSingleEffect;
  return effect->QueryInterface(IID_AuAudioEffect, out);
}

u8 AuAudioFactory::UnresolvedSlot20(u32) {
  return 1;
}

u8 AuAudioFactory::UnresolvedSlot24(u32) {
  return 1;
}

u8 AuAudioFactory::UnresolvedSlot28(u32) {
  return 1;
}

u8 AuAudioFactory::UnresolvedSlot32(u32) {
  return 1;
}

u8 AuAudioFactory::UnresolvedSlot36(u32) {
  return 1;
}

u8 AuAudioFactory::InitializeManager(u32, ApBoxX **out) {
  if (!CreateManager(out))
    return 0;
  ApBoxX *manager = nullptr;
  if (!(*out)->QueryInterface(IID_ApBoxX, reinterpret_cast<void **>(&manager)))
    return 0;
  manager->Release();
  return 1;
}

// Construct the registered AUDIO factory with its undefined original counter initialized safely.
static ScUnknown *AuCreateAudio221B6B3E() {
  return new AuAudioFactory;
}

// Construct the manager registered under the original AUDIO class identity.
static ScUnknown *AuCreateAudio221A79C0() {
  return static_cast<ApBoxX *>(new AuAudioManager());
}

// Construct the default one-shot effect registered under the original class identity.
static ScUnknown *AuCreateAudioE23F0B8B() {
  return new AuSingleEffect;
}

// Construct the registered spatial effect with its default empty sound configuration.
static ScUnknown *AuCreateAudio623F0B9C() {
  return new AuEffect623F0B9C;
}

// Construct the default actor effect registered under the original class identity.
static ScUnknown *AuCreateAudioE23F0BAC() {
  return new AuActorEffect;
}

// Construct the default disaster effect registered under the original class identity.
static ScUnknown *AuCreateAudio223F0BBB() {
  return new AuDisasterEffect;
}

// Construct the default sparse effect registered under the original class identity.
static ScUnknown *AuCreateAudio023F0BCB() {
  return new AuSparseEffect;
}

// Construct the city-information producer registered by the AUDIO director.
static ScUnknown *AuCreateAudio83156797() {
  return new AuAudioInfo();
}

// Construct the chained command factory registered for the AUDIO game command.
static ScUnknown *AuCreateAudioC315699D() {
  return new AuCommandFactory();
}

// Construct the interactive music radio station registered by the AUDIO director.
static ScUnknown *AuCreateAudio2315701E() {
  return new AuInteractiveStation();
}

// Register the ten original class identities in their constructor order without constructing their products.
AuAudioDirector::AuAudioDirector() {
  AddClass(kAuAudioFactoryClassId, AuCreateAudio221B6B3E, 0);
  AddClass(kApBoxXClassId, AuCreateAudio221A79C0, 0);
  AddClass(kAuSingleEffectClassId, AuCreateAudioE23F0B8B, 0);
  AddClass(kAuSpatialEffectClassId, AuCreateAudio623F0B9C, 0);
  AddClass(kAuActorEffectClassId, AuCreateAudioE23F0BAC, 0);
  AddClass(kAuDisasterEffectClassId, AuCreateAudio223F0BBB, 0);
  AddClass(kAuSparseEffectClassId, AuCreateAudio023F0BCB, 0);
  AddClass(kAuInfoClassId, AuCreateAudio83156797, 0);
  AddClass(kAuCommandFactoryClassId, AuCreateAudioC315699D, 0);
  AddClass(kAuRadioStationClassId, AuCreateAudio2315701E, 0);
}

AuAudioDirector::~AuAudioDirector() {
}

u32 AuAudioDirector::GetDirectorID() const {
  return kAuAudioDirectorId;
}

bool AuAudioDirector::OnStart(ScCom *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

bool AuAudioDirector::AppRegister(ScRegistry *registry) {
  ScString result, key("0xc315699d"), value("0");
  return registry->MakeRegistryName("cogamecmd", key, value, &result) && registry->Register(result);
}

alignas(AuAudioDirector) static u8 AuAudioDirectorStorage[sizeof(AuAudioDirector)];
static bool AuAudioDirectorInitialized;

static void AuDestroyAudioDirector() {
  reinterpret_cast<AuAudioDirector *>(AuAudioDirectorStorage)->~AuAudioDirector();
}

// Set the lazy flag before construction and register the original process-exit destructor.
AuAudioDirector *AuGetAudioDirector() {
  if (!AuAudioDirectorInitialized) {
    AuAudioDirectorInitialized = true;
    new (AuAudioDirectorStorage) AuAudioDirector();
    atexit(AuDestroyAudioDirector);
  }
  return reinterpret_cast<AuAudioDirector *>(AuAudioDirectorStorage);
}

#if !defined(SC3_MONOLITHIC)
// Export the same process-static director through the original module entry point.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return AuGetAudioDirector();
}
#endif

static bool AuApplicationCacheInitialized;
static ApGameInterface *AuCachedApplication;

static void AuEndApplicationCache() {
}

// Query the application interface once, retaining the original cached result for the module lifetime.
ApGameInterface *AuGetApplication() {
  if (!AuApplicationCacheInitialized) {
    AuApplicationCacheInitialized = true;
    AuGetAudioDirector()->FrameWork()->Application()->QueryInterface(
        IID_ApGameInterface, reinterpret_cast<void **>(&AuCachedApplication));
    atexit(AuEndApplicationCache);
  }
  return AuCachedApplication;
}

AuCommandOwner::AuCommandOwner() : references(0) {
}

AuCommandOwner::~AuCommandOwner() {
}

// Remove one reference and delete the complete receiver when the original decrement reaches zero.
u32 AuCommandOwner::Release() {
  u32 result = RemoveRef();
  if (!result)
    delete this;
  return result;
}

u32 AuCommandOwner::AddRef() {
  AuGetAudioDirector()->AddRef();
  return ++references;
}

// Release one director reference only when the secondary count is nonzero.
u32 AuCommandOwner::RemoveRef() {
  if (references) {
    AuGetAudioDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 AuCommandOwner::RefCount() {
  return references;
}

AuGameCommand::AuGameCommand() {
}

// Drain retained inputs and outputs before the original command sequences retire.
AuGameCommand::~AuGameCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool AuGameCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuGameCommand::AddRef() {
  return AuCommandOwner::AddRef();
}

u32 AuGameCommand::Release() {
  return AuCommandOwner::Release();
}

// Retain an input argument before appending it to the pending input queue.
bool AuGameCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Transfer the oldest retained input to the caller, leaving an empty output untouched.
bool AuGameCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 AuGameCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate input descriptors in their original order.
bool AuGameCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!inputsDescription.size)
    return false;
  for (u32 i = 0; i < inputsDescription.size; ++i) {
    UiCommandParameter value = inputsDescription.data[i];
    callback(value.type, value.flags, context);
  }
  return true;
}

// Retain an output argument before appending it to the pending output queue.
bool AuGameCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

// Transfer the oldest retained output to the caller, leaving an empty output untouched.
bool AuGameCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 AuGameCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate output descriptors in their original order.
bool AuGameCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!outputsDescription.size)
    return false;
  for (u32 i = 0; i < outputsDescription.size; ++i) {
    UiCommandParameter value = outputsDescription.data[i];
    callback(value.type, value.flags, context);
  }
  return true;
}

bool AuGameCommand::AddInputParameter(i32 type, u32 flags) {
  *inputsDescription.Append() = {type, flags};
  return true;
}

bool AuGameCommand::AddOutputParameter(i32 type, u32 flags) {
  *outputsDescription.Append() = {type, flags};
  return true;
}

AuRadioCommand::AuRadioCommand() {
  AddInputParameter(3, kAuRadioOperationParameterId);
  AddInputParameter(3, kAuRadioOptionalArgumentParameterId);
}

AuRadioCommand::~AuRadioCommand() {
}

// Consume the radio command and send its original radio or application event, preserving the optional second input.
bool AuRadioCommand::Execute() {
  ApGameInterface *application = AuGetApplication();
  if (!InputQueueSize() || !application)
    return false;
  AuRadio *radio = application->GetRadio();
  ScVariant *argument;
  bool result = PopFrontInput(&argument);
  u32 operation = *argument->AsUint32();
  argument->Release();
  switch (operation) {
  case 1:
    if (!radio)
      return false;
    radio->TurnOn();
    application->GetBoxX()->Event(kAuEventSetMusicEnabled, 1, 0, 0, 0);
    return result;
  case 2:
    if (!radio)
      return false;
    radio->TurnOff();
    application->GetBoxX()->Event(kAuEventSetMusicEnabled, 0, 0, 0, 0);
    return result;
  case 3:
    if (!radio)
      return false;
    if (radio->IsOn()) {
      radio->TurnOff();
      application->GetBoxX()->Event(kAuEventSetMusicEnabled, 0, 0, 0, 0);
    } else {
      radio->TurnOn();
      application->GetBoxX()->Event(kAuEventSetMusicEnabled, 1, 0, 0, 0);
    }
    return result;
  case 4:
    application->GetBoxX()->Event(kAuEventSetEffectsEnabled, 1, 0, 0, 0);
    return result;
  case 5:
    application->GetBoxX()->Event(kAuEventSetEffectsEnabled, 0, 0, 0, 0);
    return result;
  case 6:
    return result;
  case 7:
    if (!InputQueueSize())
      return false;
    result = PopFrontInput(&argument);
    argument->AsUint32();
    argument->Release();
    return result;
  default:
    return false;
  }
}

AuCommandFactoryBase::AuCommandFactoryBase() : references(0) {
}

// Release the original captured child range, stopping when a null entry is encountered.
AuCommandFactoryBase::~AuCommandFactoryBase() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

bool AuCommandFactoryBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the AUDIO director.
u32 AuCommandFactoryBase::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Decrement the object count, release the director reference and delete the complete factory at zero.
u32 AuCommandFactoryBase::Release() {
  --references;
  AuGetAudioDirector()->RemoveRef();
  u32 result = references;
  if (!result) {
    delete this;
    return 0;
  }
  return result;
}

bool AuCommandFactoryBase::GetCommand(u32 command, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 i = 0; i < children.size; ++i, ++cursor)
    if ((*cursor)->GetCommand(command, iid, out))
      return true;
  return CreateCommand(command, iid, out);
}

// Retain a non-null child before appending it to the chained factory sequence.
bool AuCommandFactoryBase::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

AuCommandFactory::~AuCommandFactory() {
}

// Construct the AUDIO command for its original identity and return its requested interface.
bool AuCommandFactory::CreateCommand(u32 command, u32 iid, void **out) {
  if (command != kAuRadioCommandId) {
    *out = nullptr;
    return false;
  }
  AuRadioCommand *value = new AuRadioCommand();
  if (!value->QueryInterface(iid, out)) {
    delete value;
    return false;
  }
  return true;
}

static ScMessageServer *AuCachedMessageServer;

// Cache the framework's message-server identity while releasing the temporary service reference.
ScMessageServer *AuGetMessageServer() {
  if (!AuCachedMessageServer) {
    ScMessageServer *server = nullptr;
    AuGetAudioDirector()->FrameWork()->GetSystemService(
        kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&server));
    AuCachedMessageServer = server;
    if (server)
      server->Release();
  }
  return AuCachedMessageServer;
}

// Obtain the application's current city without retaining it.
static CtCity *AuGetCity() {
  ApGameInterface *application = AuGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Obtain the current city window through the application when it is available.
static SpCityWindowView *AuGetCityWindowView() {
  ApGameInterface *application = AuGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Obtain the city window's current view without retaining it.
static SpViewInterface *AuGetCurrentView() {
  SpCityWindowView *window = AuGetCityWindowView();
  return window ? window->GetCurrentView() : nullptr;
}

// Get the occupant manager used by the AUDIO queries; a missing view is undefined in the original callers.
static ScOccupantManager *AuGetViewOccupants() {
  SpViewInterface *view = AuGetCurrentView();
  assert(view);
  return view->CurrentOccupantManager();
}

// Obtain the application's agent-type tree without acquiring a separate reference.
static CtAgentTree *AuGetAgentTree() {
  ApGameInterface *application = AuGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Initialize the city and layer fields, including safe null terrain and scheme pointers before initialization.
AuAudioInfo::AuAudioInfo()
    : initialized(0), city(nullptr), box(nullptr), landValue(nullptr), secondaryMap(nullptr), aura(nullptr),
      crime(nullptr), occupants(nullptr), agentTree(nullptr), power(nullptr), pollution(nullptr), network(nullptr),
      traffic(nullptr), valves(nullptr), disasters(nullptr), terrain(nullptr), scheme(nullptr), references(0), width(0),
      height(0) {
}

bool AuAudioInfo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuInfo);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, AuInfo);
  HANDLE_QUERY_INTERFACE(IID_AuInfo, AuInfo);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuAudioInfo::AddRef() {
  return ++references;
}

u32 AuAudioInfo::Release() {
  return ScReleaseImpl(this, &references);
}

// Translate the three original city notifications into AUDIO events and ignore other messages.
bool AuAudioInfo::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ViewInfoUpdated) {
    if (static_cast<u32>(message->data1) == kSpViewUpdate_Snapshot)
      box->Event(kAuEventUpdateView, message->data2, 0, 0, 0);
  } else if (message->type == kCtMsg_GameSpeedChanged) {
    box->Event(kAuEventGameSpeedChanged, static_cast<u32>(message->data1) + 1u, 0, 0, 0);
  } else if (message->type == kCtMsg_DisasterEnded) {
    box->Event(kAuEventDisasterEnded, 0, 0, 0, 0);
  }
  return true;
}

// Retain the city, acquire its layers and register the three original city notifications.
u8 AuAudioInfo::Init(CtCity *target) {
  initialized = 1;
  city = ScAddRefNotNull(target);
  width = city->CellCountX();
  height = city->CellCountZ();
  agentTree = AuGetAgentTree();
  assert(agentTree);
  agentTree->AddRef();
  occupants = city->SurfaceOccupantManager();
  assert(occupants);
  occupants->AddRef();
  box = AuGetApplication()->GetBoxX();
  CtLayer *layer = city->GetSpecificLayer(kNwTransitLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic));
  layer = city->GetSpecificLayer(kEcLandValueLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_ScCellMapU8, reinterpret_cast<void **>(&landValue));
  layer = city->GetSpecificLayer(kEcZoneLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_ScCellMapU8, reinterpret_cast<void **>(&secondaryMap));
  layer = city->GetSpecificLayer(kCtPowerSimulationLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_CtPowerLayer, reinterpret_cast<void **>(&power));
  layer = city->GetSpecificLayer(kCtCrimeLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_ScCellMapU8, reinterpret_cast<void **>(&crime));
  layer = city->GetSpecificLayer(kEcPollutionLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution));
  layer = city->GetSpecificLayer(kNwManagerLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_NwManager, reinterpret_cast<void **>(&network));
  layer = city->GetSpecificLayer(kCtAuraLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
  terrain = city->DirtBag();
  if (!terrain) {
    Shutdown();
    return 0;
  }
  terrain->AddRef();
  scheme = AuGetApplication()->CitySchemeManager();
  if (!scheme) {
    Shutdown();
    return 0;
  }
  scheme->AddRef();
  layer = city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  layer = city->GetSpecificLayer(kDsControllerLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_DsLayer, reinterpret_cast<void **>(&disasters));
  ScMessageServer *server = AuGetMessageServer();
  assert(server);
  server->AddNotification(this, kWnMsg_ViewInfoUpdated);
  server = AuGetMessageServer();
  assert(server);
  server->AddNotification(this, kCtMsg_GameSpeedChanged);
  server = AuGetMessageServer();
  assert(server);
  server->AddNotification(this, kCtMsg_DisasterEnded);
  return 1;
}

// Remove notifications and release the retained city layers in the original order.
u8 AuAudioInfo::Shutdown() {
  ScMessageServer *server = AuGetMessageServer();
  assert(server);
  server->RemoveNotification(this, kWnMsg_ViewInfoUpdated);
  server = AuGetMessageServer();
  assert(server);
  server->RemoveNotification(this, kCtMsg_GameSpeedChanged);
  server = AuGetMessageServer();
  assert(server);
  server->RemoveNotification(this, kCtMsg_DisasterEnded);
  ScRelease(&landValue);
  ScRelease(&secondaryMap);
  ScRelease(&aura);
  ScRelease(&crime);
  ScRelease(&valves);
  ScRelease(&disasters);
  ScRelease(&agentTree);
  ScRelease(&traffic);
  ScRelease(&power);
  ScRelease(&network);
  ScRelease(&pollution);
  ScRelease(&terrain);
  ScRelease(&occupants);
  ScRelease(&city);
  return 1;
}

// Return whether the current view has an occupant at the given cell.
u8 AuAudioInfo::CellHasOccupant(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  occupant->Release();
  return 1;
}

// Test the current building occupant for abandonment, including its destroyed states.
u8 AuAudioInfo::CellIsAbandoned(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  u8 result = 0;
  if ((occupant->GetGeomFlag() & 0x100) == 0x100) {
    BdBuilding *building = nullptr;
    occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    assert(building);
    u32 state = building->GetOccupancyState();
    result = state == 2 || state == 16 || state == 8;
    building->Release();
  }
  occupant->Release();
  return result;
}

// Test the current building occupant for either original destroyed state.
u8 AuAudioInfo::CellIsDestroyed(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  u8 result = 0;
  if ((occupant->GetGeomFlag() & 0x100) == 0x100) {
    BdBuilding *building = nullptr;
    occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    assert(building);
    u32 state = building->GetOccupancyState();
    result = state == 8 || state == 16;
    building->Release();
  }
  occupant->Release();
  return result;
}

// Test the occupant geometry flags for the tree bit and release the acquired occupant.
u8 AuAudioInfo::CellHasTree(i32 x, i32 z) {
  ScOccupantManager *manager = AuGetViewOccupants();
  ScOccupant *occupant = nullptr;
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  bool result = (occupant->GetGeomFlag() & 0x400) == 0x400;
  occupant->Release();
  return result;
}

// Classify garbage only when the pollution layer reports accumulation and the zone map returns type 17.
u8 AuAudioInfo::CellIsGarbage(i32 x, i32 z) {
  if (!pollution->IsGarbageAccumulated(static_cast<u32>(x), static_cast<u32>(z)))
    return 0;
  u8 value = 0;
  secondaryMap->GetValue(static_cast<u32>(x), static_cast<u32>(z), &value);
  return value == 17;
}

u8 AuAudioInfo::CellHasSpriteInSurfaceView(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  SpSprite *sprite = occupant->GetSpriteInst(1);
  occupant->Release();
  return sprite != nullptr;
}

u8 AuAudioInfo::CellHasSpriteInPipeView(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  SpSprite *sprite = occupant->GetSpriteInst(2);
  occupant->Release();
  return sprite != nullptr;
}

u8 AuAudioInfo::CellHasSpriteInSubwayView(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 0;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant)
    return 0;
  SpSprite *sprite = occupant->GetSpriteInst(4);
  occupant->Release();
  return sprite != nullptr;
}

// Initialize both outputs and classify garbage, terrain and occupant ambience in the original decision order.
i32 AuAudioInfo::GetCellAmbienceType(i32 x, i32 z, i32 *first, i32 *second) {
  *first = 50;
  *second = 50;
  secondaryMap->CellCountX();
  secondaryMap->CellCountZ();
  if (CellIsGarbage(x, z))
    return 14;
  ScOccupant *occupant = nullptr;
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 1;
  manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant);
  if (!occupant) {
    if (terrain->IsWater(static_cast<u32>(x), static_cast<u32>(z)))
      return 6;
    *first = 0;
    return 1;
  }
  u32 flags = occupant->GetGeomFlag();
  if (flags & 0x100) {
    BdBuilding *building = nullptr;
    occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    assert(building);
    u32 type = occupant->TypeId();
    u32 state = building->GetOccupancyState();
    if (state == 2) {
      occupant->Release();
      building->Release();
      return 9;
    }
    if (state == 4) {
      *first = agentTree->IsAncestor(3000, type) ? 0 : 100;
      occupant->Release();
      building->Release();
      return 5;
    }
    if (building->GetOccupancyState() != 1) {
      occupant->Release();
      building->Release();
      return 9;
    }
    occupant->Release();
    building->Release();
    if (agentTree->IsAncestor(8000, type)) {
      if (agentTree->IsAncestor(9300, type))
        return CurrentBuildingSet() != 1 ? 8 : 1;
      *first = agentTree->IsAncestor(8700, type) || agentTree->IsAncestor(8900, type) ||
          agentTree->IsAncestor(9100, type) ? 100 : 0;
      return 4;
    }
    if (agentTree->IsAncestor(5000, type)) {
      if (agentTree->IsAncestor(5100, type) || agentTree->IsAncestor(5500, type))
        *first = 0;
      else if (agentTree->IsAncestor(6300, type) || agentTree->IsAncestor(6700, type))
        *first = 100;
      return 3;
    }
    if (agentTree->IsAncestor(3000, type)) {
      if (agentTree->IsAncestor(4300, type) || agentTree->IsAncestor(4500, type) ||
          agentTree->IsAncestor(4700, type)) {
        *first = 100;
        return 3;
      }
      if (agentTree->IsAncestor(3700, type) || agentTree->IsAncestor(3900, type) ||
          agentTree->IsAncestor(4100, type)) {
        *first = 0;
        return 3;
      }
      if (!agentTree->IsAncestor(4300, type) && !agentTree->IsAncestor(4500, type) &&
          !agentTree->IsAncestor(4700, type))
        return 2;
      *first = 100;
      return 3;
    }
    if (agentTree->IsAncestor(12330, type))
      return 11;
    if (agentTree->IsAncestor(12310, type))
      return 10;
    return agentTree->IsAncestor(100, type) ? 7 : 0;
  }
  *first = (flags & 0x400) == 0x400 ? 100 : 0;
  occupant->Release();
  return 1;
}

// Combine the zone map, current occupant and terrain to select the original zone classification.
i32 AuAudioInfo::GetCellZoneType(i32 x, i32 z) {
  static const u8 zoneTypes[] = {1, 2, 2, 2, 1, 3, 3, 3, 1, 4, 4, 4, 1, 4, 10, 11, 4, 12, 4, 4, 4, 4, 1};
  u8 zone = static_cast<u8>(static_cast<u32>(z) >> 24);
  secondaryMap->GetValue(static_cast<u32>(x), static_cast<u32>(z), &zone);
  ScOccupantManager *manager = AuGetViewOccupants();
  if (!manager)
    return 1;
  ScOccupant *occupant = nullptr;
  if (manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant)) {
    assert(occupant);
    if (!zone) {
      u32 type = occupant->TypeId();
      if (agentTree->IsAncestor(100, type)) {
        if (agentTree->IsAncestor(3000, type))
          zone = 2;
        else if (agentTree->IsAncestor(5000, type))
          zone = 6;
        else if (agentTree->IsAncestor(8000, type))
          zone = 10;
        else if (agentTree->IsAncestor(11000, type))
          zone = 13;
        else if (agentTree->IsAncestor(12310, type))
          zone = 14;
        else if (agentTree->IsAncestor(12330, type))
          zone = 15;
        else if (agentTree->IsAncestor(12850, type))
          zone = 17;
      }
      occupant->Release();
      return zoneTypes[zone];
    }
    i32 buildingType = 0;
    if (occupant->MajorType() == 1) {
      BdBuilding *building = nullptr;
      if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building)) && building) {
        if (building->GetOccupancyState() == 4)
          buildingType = 5;
        else if (building->GetOccupancyState() == 2)
          buildingType = 9;
        building->Release();
      }
    }
    occupant->Release();
    if (buildingType)
      return buildingType;
  } else if (!zone) {
    CtCity *currentCity = AuGetCity();
    if (currentCity) {
      ScDirtBag *currentTerrain = currentCity->DirtBag();
      assert(currentTerrain);
      if (currentTerrain->IsWater(static_cast<u32>(x), static_cast<u32>(z)))
        return 6;
    }
  }
  assert(zone < sizeof(zoneTypes));
  return zoneTypes[zone];
}

// Read traffic mode three, preserving the coordinate's original high byte if the output is untouched.
i32 AuAudioInfo::GetTrafficDensity(i32 x, i32 z) {
  u8 value = static_cast<u8>(static_cast<u32>(z) >> 24);
  traffic->GetVolume(static_cast<u32>(x), static_cast<u32>(z), &value, 3);
  return value;
}

i32 AuAudioInfo::GetCellSpriteHeight(i32 x, i32 z) {
  ScOccupantManager *manager = AuGetViewOccupants();
  ScOccupant *occupant = nullptr;
  if (!manager || !manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant))
    return 0;
  assert(occupant);
  i32 result = static_cast<i32>(occupant->SizeInAnimCellsY());
  occupant->Release();
  return result;
}

// Check whether a building occupant has either relevant major type and lacks power.
u8 AuAudioInfo::CellIsUnpoweredBuilding(i32 x, i32 z) {
  ScOccupantManager *manager = AuGetViewOccupants();
  ScOccupant *occupant = nullptr;
  if (!manager || !manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant))
    return 0;
  assert(occupant);
  u8 type = occupant->MajorType();
  occupant->Release();
  return (type == 1 || type == 3) && !CellIsPowered(x, z);
}

i32 AuAudioInfo::GetBuildingSoundId(i32 x, i32 z) {
  ScOccupantManager *manager = AuGetViewOccupants();
  ScOccupant *occupant = nullptr;
  if (!manager || !manager->GetOccupantAtCell(static_cast<u32>(x), static_cast<u32>(z), &occupant))
    return 0;
  assert(occupant);
  i32 result = static_cast<i32>(occupant->GetSoundObjectId(0));
  occupant->Release();
  return result;
}

i32 AuAudioInfo::CellCountX() {
  return static_cast<i32>(width);
}

i32 AuAudioInfo::CellCountZ() {
  return static_cast<i32>(width);
}

u8 AuAudioInfo::CellIsWater(i32 x, i32 z) {
  return terrain->IsWater(static_cast<u32>(x), static_cast<u32>(z));
}

u8 AuAudioInfo::CellHasRoad(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasRoadAt(position);
}

u8 AuAudioInfo::CellHasHighway(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasHighwayAt(position);
}

u8 AuAudioInfo::CellHasRail(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasRailAt(position);
}

u8 AuAudioInfo::CellHasSubway(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasSubwayAt(position);
}

u8 AuAudioInfo::CellHasPipe(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasPipeAt(position);
}

u8 AuAudioInfo::CellHasPower(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasPowerAt(position);
}

u8 AuAudioInfo::CellHasTunnel(i32 x, i32 z) {
  CtCoord position{static_cast<i32>(static_cast<u32>(x) << 8), static_cast<i32>(static_cast<u32>(z) << 8), 0};
  return network->HasRoadFallbackAt(position);
}

i32 AuAudioInfo::GetCellPopulation(i32, i32) {
  return 100;
}

// Read the land-value byte, preserving the original output byte when the layer leaves it untouched.
i32 AuAudioInfo::GetCellLandValue(i32 x, i32 z) {
  u8 value = static_cast<u8>(static_cast<u32>(z) >> 24);
  landValue->GetValue(static_cast<u32>(x), static_cast<u32>(z), &value);
  return value;
}

// Read the crime-value byte with the original zero-initialized output.
i32 AuAudioInfo::GetCellCrimeValue(i32 x, i32 z) {
  u8 value = 0;
  crime->GetValue(static_cast<u32>(x), static_cast<u32>(z), &value);
  return value;
}

i32 AuAudioInfo::GetCellRadiationValue(i32 x, i32 z) {
  return pollution->IsRadioactive(static_cast<u32>(x), static_cast<u32>(z));
}

// Read the aura byte as its original unsigned result, preserving an untouched output byte.
i32 AuAudioInfo::GetCellAura(i32 x, i32 z) {
  u8 value = static_cast<u8>(static_cast<u32>(z) >> 24);
  aura->GetValue(static_cast<u32>(x), static_cast<u32>(z), reinterpret_cast<i8 *>(&value));
  return value;
}

i32 AuAudioInfo::GetCityMayoralRating() {
  return 50;
}

i32 AuAudioInfo::GetCityPopulation() {
  return 0;
}

// Read the power map's Boolean cell value with the original zero-initialized output.
u8 AuAudioInfo::CellIsPowered(i32 x, i32 z) {
  bool value = false;
  power->GetValue(static_cast<u32>(x), static_cast<u32>(z), &value);
  return value;
}

i32 AuAudioInfo::GetCellInterfaceSoundId(i32 x, i32 z) {
  if (CellHasRoad(x, z) || CellHasHighway(x, z))
    return 14;
  if (CellHasRail(x, z))
    return 51;
  if (CellHasPower(x, z))
    return 16;
  return CellHasTunnel(x, z) ? 52 : 0;
}

u8 AuAudioInfo::IsInEditModeCity() {
  return city->GetEditMode() == 1;
}

i32 AuAudioInfo::DisasterCount() {
  return static_cast<i32>(disasters->GetActiveDisasterCount());
}

// Read terrain height at the converted coordinates and return it in the original scaled units.
i32 AuAudioInfo::TerrainHeight(i32 x, i32 z) {
  u8 value = static_cast<u8>(static_cast<u32>(z) >> 24);
  terrain->GetVertexAltitude(static_cast<u32>(x >> 8), static_cast<u32>(z >> 8), &value);
  return static_cast<i32>(value) << 8;
}

// Forward the three date outputs only when this information producer and its city are initialized.
u8 AuAudioInfo::GetCityDate(u32 *first, u32 *second, u32 *third) {
  if (initialized != 1 || !city)
    return 0;
  city->GetDate(first, second, third);
  return 1;
}

u32 AuAudioInfo::CurrentFloraSet() {
  if (!scheme)
    return 0;
  u32 value = static_cast<u32>(reinterpret_cast<uptr>(this));
  scheme->GetSoundIdForCurrentFloraSet(&value);
  return value;
}

// Read the current building-set sound ID, preserving the original untouched-output fallback.
u32 AuAudioInfo::CurrentBuildingSet() {
  if (!scheme)
    return 0;
  u32 value = static_cast<u32>(reinterpret_cast<uptr>(this));
  scheme->GetSoundIdForCurrentBuildingSet(&value);
  return value;
}

// Read the current landscape sound ID, preserving the original untouched-output fallback.
u32 AuAudioInfo::CurrentLandScape() {
  if (!scheme)
    return 0;
  u32 value = static_cast<u32>(reinterpret_cast<uptr>(this));
  scheme->GetSoundIdForCurrentLandScape(&value);
  return value;
}

AuAudioInfo::~AuAudioInfo() {
}

// The original manager initialization caches this borrowed sound-device service for message dispatch.
AuSoundDevice *AuManagerSoundDevice;
static AuAudioManager *AuManagerSingleton;
static ScString AuSoundDirectory;
static ScString AuMusicDirectory;
static u32 AuAmbienceMixFlag;
static i32 AuPreviousRadiation;
u8 AuForcedQuadrantEnabled;
u32 AuForcedQuadrant;
static AuFreshnessScheduler *AuSchedulerSingleton;

static void AuManagerTimerCallback(uptr context) {
  reinterpret_cast<AuAudioManager *>(context)->Update(static_cast<u32>(context));
}

// Borrow the sound directory populated during AUDIO manager initialization.
ScString *AuGetAudioSoundDirectory() {
  return &AuSoundDirectory;
}

// Borrow the process-wide music directory without copying or retaining it.
ScString *AuGetMusicDirectory() {
  return &AuMusicDirectory;
}

// Borrow the preferences interface from the current game application, when the application exists.
static ApPreferences *AuGetManagerPreferences() {
  ApGameInterface *application = AuGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Cache the borrowed graphics service after releasing its temporary acquired reference.
static GxGraphics *AuGetManagerGraphics() {
  static GxGraphics *graphics;
  if (!graphics) {
    GxGraphics *acquired = nullptr;
    AuGetAudioDirector()->FrameWork()->GetSystemService(
        kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&acquired));
    graphics = acquired;
    if (acquired)
      acquired->Release();
  }
  return graphics;
}

// Cache the borrowed window-resource service after releasing its temporary acquired reference.
static WnResources *AuGetManagerWindowResources() {
  static WnResources *resources;
  if (!resources) {
    WnResources *acquired = nullptr;
    AuGetAudioDirector()->FrameWork()->GetSystemService(
        kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&acquired));
    resources = acquired;
    if (acquired)
      acquired->Release();
  }
  return resources;
}

// Cache the borrowed sound-device service and release the temporary service reference after acquisition.
AuSoundDevice *AuGetManagerSoundDevice() {
  static AuSoundDevice *device;
  if (!device) {
    AuSoundDevice *acquired = nullptr;
    AuGetAudioDirector()->FrameWork()->GetSystemService(
        kAuSoundDeviceServiceId, IID_AuSoundDevice, reinterpret_cast<void **>(&acquired));
    device = acquired;
    if (acquired)
      acquired->Release();
  }
  return device;
}

AuAudioManager::AuAudioManager()
    : references(0), initialized(0), musicPlayer(nullptr), unknown20(0), playbackState(0), savedPlaybackState(0),
      flag32(0), flag33(0), musicVolume(1024), effectsVolume(1024), ambienceVolume(1024), flag48(0),
      zoomLoopVolume(1024), buildingRandomMultiplier(10000), trafficDensityMinimum(15), trafficDensityMultiplier(20),
      vehicleHonkDivisor(1), vehicleMaximumDistance(4000), vehicleMinimumDistance(256),
      buildingAmbienceAttenuation(250), buildingAmbienceMinimumZoom(4), honkAttenuation(250), honkMinimumZoom(4),
      vehicleAttenuation(300), vehicleMinimumZoom(4), vehicleEffectsLevel(0), vehicleEffectsPreset(-1),
      vehicleTallBuildingThreshold(0), forceCrimeTempo(0), forceAmbienceQuadrant(0), flag124(0), flag125(1), flag126(0),
      unknown128(0), updateTimer(nullptr), clock(nullptr), scheduler(nullptr), random(kScRandomSeedFromClock),
      unknown172(0), flag176(0), lock(nullptr), player264(nullptr), player268(nullptr), player272(nullptr),
      player276(nullptr), player280(nullptr), player284(nullptr), player288(nullptr), player292(nullptr),
      player296(nullptr), player300(nullptr), player304(nullptr), player308(nullptr), player312(nullptr),
      player316(nullptr), player320(nullptr), player324(nullptr), player328(nullptr), player332(nullptr),
      player336(nullptr), player340(nullptr), player344(nullptr), player348(nullptr), player352(nullptr),
      player356(nullptr), player360(nullptr), player364(nullptr), sound368(nullptr), loopSound(nullptr),
      geigerSound(nullptr), sound380(nullptr), loopCookie(0), buildingAmbience(nullptr), cache(nullptr),
      audioInfo(nullptr), viewWorldX(0), viewWorldZ(0), viewRight(0), viewBottom(0), viewLeft(0), viewTop(0),
      cityWidth(0), cityHeight(0), viewRadius(0), viewZoom(0), unknown2496(-1), viewRotation(0), unknown2504(0),
      viewMode(0), unknown2512(-1), unknown2516(0), unknown2520(0), unknown2524(0), unknown2528(0), unknown2532(0),
      currentLandscape(0), visibleCells(0), unknown2604(0), minimumFadeVolume(1024), musicStartDelay(10000),
      unknown2620(0) {
  AuManagerSingleton = this;
  for (u32 i = 0; i < 512; ++i)
    effects[i] = nullptr;
  lock = new ScLock();
  clock = new ScStopwatch(ScStopwatch::Milliseconds, 0);
}

AuAudioManager::~AuAudioManager() {
  delete lock;
  delete clock;
}

bool AuAudioManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ApBoxX, ApBoxX);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuAudioManager::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and retire the manager when its count reaches zero.
u32 AuAudioManager::Release() {
  if (references) {
    AuGetAudioDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Check the view bounds and Manhattan distance, including the original rotation-dependent extended region.
u8 AuAudioManager::IsAudibleCell(i32 x, i32 z) {
  if (x > viewRight || x < viewLeft || z > viewBottom || z < viewTop)
    return 0;
  i64 dx = static_cast<i64>(x) - viewCellX, dz = static_cast<i64>(z) - viewCellZ;
  assert(dx >= -static_cast<i64>(INT32_MAX) && dx <= INT32_MAX &&
      dz >= -static_cast<i64>(INT32_MAX) && dz <= INT32_MAX);
  i64 distance = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
  assert(distance <= INT32_MAX);
  if (distance <= viewRadius)
    return 1;
  if (viewRotation != 0 && viewRotation != 2)
    return 0;
  if (!((x > viewRight && z > viewBottom) || (x < viewRight && z < viewBottom)))
    return 0;
  i64 zoomOffset = 6 - static_cast<i64>(viewZoom), extension = 4 * zoomOffset;
  i64 limit = static_cast<i64>(viewRadius) + extension;
  assert(zoomOffset >= INT32_MIN && zoomOffset <= INT32_MAX && extension >= INT32_MIN && extension <= INT32_MAX &&
      limit >= INT32_MIN && limit <= INT32_MAX);
  return distance <= limit;
}

// Expand the visible cell rectangle for the current zoom and clamp it to the city bounds.
void AuAudioManager::GetScanBounds(i32 *left, i32 *right, i32 *top, i32 *bottom) {
  i64 zoom = 6 - static_cast<i64>(viewZoom), span = 6 * zoom;
  i64 x0 = static_cast<i64>(viewLeft) - span, x1 = static_cast<i64>(viewRight) + span;
  i64 z0 = static_cast<i64>(viewTop) - span, z1 = static_cast<i64>(viewBottom) + span;
  i64 lastX = static_cast<i64>(cityWidth) - 1, lastZ = static_cast<i64>(cityHeight) - 1;
  assert(zoom >= INT32_MIN && zoom <= INT32_MAX && span >= INT32_MIN && span <= INT32_MAX &&
      x0 >= INT32_MIN && x0 <= INT32_MAX && x1 >= INT32_MIN && x1 <= INT32_MAX &&
      z0 >= INT32_MIN && z0 <= INT32_MAX && z1 >= INT32_MIN && z1 <= INT32_MAX &&
      lastX >= INT32_MIN && lastX <= INT32_MAX && lastZ >= INT32_MIN && lastZ <= INT32_MAX);
  *left = static_cast<i32>(x0 > 0 ? x0 : 0);
  *right = static_cast<i32>(x1 < lastX ? x1 : lastX);
  *top = static_cast<i32>(z0 > 0 ? z0 : 0);
  *bottom = static_cast<i32>(z1 < lastZ ? z1 : lastZ);
}

// Return the percentage of audible cells containing subway tracks.
i32 AuAudioManager::SubwayCoverage() {
  i32 left, right, top, bottom;
  GetScanBounds(&left, &right, &top, &bottom);
  assert(left > right || (right != INT32_MAX && (top > bottom || bottom != INT32_MAX)));
  i32 total = 0, matching = 0;
  for (i32 x = left; x <= right; ++x) {
    for (i32 z = top; z <= bottom; ++z) {
      if (IsAudibleCell(x, z)) {
        assert(total != INT32_MAX);
        ++total;
        assert(audioInfo);
        if (audioInfo->CellHasSubway(x, z))
          ++matching;
      }
    }
  }
  assert(total && matching <= INT32_MAX / 100);
  return 100 * matching / total;
}

// Return the percentage of audible cells containing water pipes.
i32 AuAudioManager::PipeCoverage() {
  i32 left, right, top, bottom;
  GetScanBounds(&left, &right, &top, &bottom);
  assert(left > right || (right != INT32_MAX && (top > bottom || bottom != INT32_MAX)));
  i32 total = 0, matching = 0;
  for (i32 x = left; x <= right; ++x) {
    for (i32 z = top; z <= bottom; ++z) {
      if (IsAudibleCell(x, z)) {
        assert(total != INT32_MAX);
        ++total;
        assert(audioInfo);
        if (audioInfo->CellHasPipe(x, z))
          ++matching;
      }
    }
  }
  assert(total && matching <= INT32_MAX / 100);
  return 100 * matching / total;
}

// Select the ambient sound associated with a cell and the current surface, subway or pipe view.
i32 AuAudioManager::CellSoundId(i32 x, i32 z) {
  assert(audioInfo);
  if (!audioInfo->CellHasOccupant(x, z))
    return audioInfo->CellIsWater(x, z) ? 140 : 0;
  if (viewMode != 0) {
    if (viewMode == 1 && audioInfo->CellHasSubway(x, z))
      return 53;
    if (viewMode == 2 && audioInfo->CellHasPipe(x, z))
      return 50;
    return 0;
  }
  if (audioInfo->CellIsDestroyed(x, z))
    return 0;
  if (audioInfo->CellIsAbandoned(x, z))
    return 143;
  if (audioInfo->CellIsGarbage(x, z))
    return 151;
  if (audioInfo->CellHasTree(x, z)) {
    u32 flora = audioInfo->CurrentFloraSet();
    if (flora == 1)
      return 252;
    if (flora == 2)
      return 253;
    return 151;
  }
  if (audioInfo->CellHasRoad(x, z) || audioInfo->CellHasHighway(x, z))
    return 14;
  if (audioInfo->CellHasRail(x, z))
    return 51;
  if (audioInfo->CellHasPower(x, z))
    return 144;
  if (audioInfo->CellHasTunnel(x, z))
    return 52;
  switch (audioInfo->GetCellZoneType(x, z)) {
  case 2: return 136;
  case 3: return 137;
  case 4: return 138;
  case 5: return 139;
  case 6: return 140;
  case 7: return 141;
  case 8: return 142;
  case 9: return 143;
  case 10: return 173;
  case 11: return 174;
  case 12: return 172;
  case 14: return 151;
  default: return 0;
  }
}

// Select the secondary sound for a cell from its view, transport links and zone type.
i32 AuAudioManager::CellOverlaySoundId(i32 x, i32 z) {
  if (viewMode != 0) {
    if (viewMode == 1) {
      assert(audioInfo);
      return audioInfo->CellHasSubway(x, z) ? 53 : 0;
    }
    if (viewMode == 2) {
      assert(audioInfo);
      return audioInfo->CellHasPipe(x, z) ? 50 : 0;
    }
    return 0;
  }
  assert(audioInfo);
  if (audioInfo->CellHasTree(x, z))
    return 17;
  if (audioInfo->CellHasRoad(x, z) || audioInfo->CellHasHighway(x, z))
    return 14;
  if (audioInfo->CellHasRail(x, z))
    return 51;
  if (audioInfo->CellHasPower(x, z))
    return 144;
  if (audioInfo->CellHasTunnel(x, z))
    return 52;
  switch (audioInfo->GetCellZoneType(x, z)) {
  case 2: return 136;
  case 3: return 137;
  case 4: return 138;
  case 5: return 139;
  case 6: return 361;
  case 7: return 272;
  case 8: return 368;
  case 9: return 143;
  case 10: return 259;
  case 11: return 291;
  case 12: return 312;
  case 14: return 151;
  default: return 0;
  }
}

// Average the audible cells' crime values and return the remaining safety percentage.
i32 AuAudioManager::AudibleSafety() {
  if (!audioInfo)
    return 50;
  i64 left, right, top, bottom;
  if (viewZoom == 4 || viewZoom == 5) {
    i32 scanLeft, scanRight, scanTop, scanBottom;
    GetScanBounds(&scanLeft, &scanRight, &scanTop, &scanBottom);
    left = scanLeft;
    right = scanRight;
    top = scanTop;
    bottom = scanBottom;
  } else {
    left = static_cast<i64>(viewCellX) - 20;
    right = static_cast<i64>(viewCellX) + 20;
    top = static_cast<i64>(viewCellZ) - 20;
    bottom = static_cast<i64>(viewCellZ) + 20;
    assert(left >= INT32_MIN && right <= INT32_MAX && top >= INT32_MIN && bottom <= INT32_MAX);
    if (left < 0)
      left = 0;
    if (right >= cityWidth)
      right = cityWidth;
    if (top < 0)
      top = 0;
    if (bottom >= cityHeight)
      bottom = cityHeight;
  }
  assert(left > right || (right != INT32_MAX && (top > bottom || bottom != INT32_MAX)));
  i32 total = 0, crime = 0;
  for (i32 x = static_cast<i32>(left); x <= right; ++x) {
    for (i32 z = static_cast<i32>(top); z <= bottom; ++z) {
      if (IsAudibleCell(x, z)) {
        assert(audioInfo && total != INT32_MAX);
        i64 sum = static_cast<i64>(crime) + audioInfo->GetCellCrimeValue(x, z);
        assert(sum >= INT32_MIN && sum <= INT32_MAX);
        crime = static_cast<i32>(sum);
        ++total;
      }
    }
  }
  i32 average = total ? crime / total : 0;
  i64 safety = 100 - static_cast<i64>(average);
  assert(safety <= INT32_MAX);
  return safety < 0 ? 0 : static_cast<i32>(safety);
}

// Update the listener and freshness players from the camera, then refresh ambience and music.
u8 AuAudioManager::UpdateView(SpIsoViewInfo *view) {
  assert(view);
  i32 oldZoom = viewZoom, oldRotation = viewRotation;
  view->GetType(&viewMode);
  if (viewMode >= 10 && (viewMode <= 18 || viewMode == 30))
    viewMode = 0;
  CtCoord focus{INT32_MAX, INT32_MAX, INT32_MAX};
  view->GetInfo(&focus);
  viewWorldX = focus.x;
  viewWorldZ = focus.z;
  viewCellX = focus.x / 16;
  viewCellZ = focus.z / 16;
  GeBounds bounds;
  view->GetInfo(&bounds);
  i64 right = static_cast<i64>(bounds.maximum.x) + 16, bottom = static_cast<i64>(bounds.maximum.z) + 16;
  i64 left = static_cast<i64>(bounds.minimum.x) + 16, top = static_cast<i64>(bounds.minimum.z) + 16;
  assert(right <= INT32_MAX && bottom <= INT32_MAX && left <= INT32_MAX && top <= INT32_MAX);
  viewRight = static_cast<i32>(right / 256);
  viewBottom = static_cast<i32>(bottom / 256);
  viewLeft = static_cast<i32>(left / 256);
  viewTop = static_cast<i32>(top / 256);
  i64 dx1 = static_cast<i64>(viewCellX) - viewLeft, dx2 = static_cast<i64>(viewRight) - viewCellX;
  i64 dz1 = static_cast<i64>(viewCellZ) - viewTop, dz2 = static_cast<i64>(viewBottom) - viewCellZ;
  assert(dx1 >= INT32_MIN && dx1 <= INT32_MAX && dx2 >= INT32_MIN && dx2 <= INT32_MAX &&
      dz1 >= INT32_MIN && dz1 <= INT32_MAX && dz2 >= INT32_MIN && dz2 <= INT32_MAX);
  i32 radiusX = static_cast<i32>(dx1 > dx2 ? dx1 : dx2), radiusZ = static_cast<i32>(dz1 > dz2 ? dz1 : dz2);
  viewRadius = radiusX < radiusZ ? radiusX : radiusZ;
  view->GetZoom(&viewZoom);
  assert(viewZoom != INT32_MAX);
  ++viewZoom;
  if (oldZoom != viewZoom)
    listener.SetZoom(viewZoom);
  listener.SetPosition(viewWorldX, 0, viewWorldZ);
  view->GetCompass(&viewRotation);
  if (oldRotation != viewRotation) {
    switch (viewRotation) {
    case 0:
      listener.SetOrientation(-1, 0, -1);
      break;
    case 1:
      listener.SetOrientation(-1, 0, 1);
      break;
    case 2:
      listener.SetOrientation(1, 0, 1);
      break;
    case 3:
      listener.SetOrientation(1, 0, -1);
      break;
    }
  }
  AuFreshnessPlayer **viewPlayers[] = {&player304, &player320, &player324, &player328, &player352, &player356,
      &player344, &player348, &player364, &player360};
  for (AuFreshnessPlayer **entry : viewPlayers) {
    assert(*entry);
    (*entry)->SetCoordinates(viewWorldX, viewWorldZ);
  }
  for (i32 i = 0; i < 15; ++i) {
    assert(channels[i]);
    if (channels[i]->player)
      channels[i]->player->SetCoordinates(viewWorldX, viewWorldZ);
  }
  if (oldZoom != viewZoom)
    for (i32 i = 0; i < 512; ++i)
      if (effects[i])
        effects[i]->UnresolvedSlot44();
  UpdateAmbienceMix();
  return UpdateMusic();
}

// Scan audible cells into the landscape channels and update their configured freshness players.
void AuAudioManager::UpdateAmbienceChannels() {
  assert(audioInfo);
  u32 landscape = audioInfo->CurrentLandScape();
  if (landscape != 3) {
    assert(audioInfo);
    landscape = audioInfo->CurrentFloraSet();
  }
  if (landscape != static_cast<u32>(currentLandscape)) {
    assert(player264);
    player264->Stop();
    player264 = nullptr;
    currentLandscape = static_cast<i32>(landscape);
    if (landscape == 1)
      player264 = player272;
    else if (landscape == 2)
      player264 = player276;
    else if (landscape == 3)
      player264 = player280;
    else
      player264 = player268;
    assert(channels[1]);
    channels[1]->player = player264;
  }
  for (i32 i = 0; i < 15; ++i) {
    assert(channels[i]);
    channels[i]->cells = 0;
    channels[i]->firstTotal = 0;
    channels[i]->secondTotal = 0;
    channels[i]->percentage = 0;
    channels[i]->adjustedPercentage = 0;
  }
  if (viewMode == 0 && viewZoom >= 3 && playbackState == 5) {
    i32 left, right, top, bottom;
    GetScanBounds(&left, &right, &top, &bottom);
    i32 step = viewZoom == 3 ? 2 : 1, first = 0, second = 0;
    visibleCells = 0;
    for (i32 x = left; x <= right;) {
      for (i32 z = top; z <= bottom;) {
        if (IsAudibleCell(x, z)) {
          assert(visibleCells != INT32_MAX && audioInfo);
          ++visibleCells;
          i32 category = audioInfo->GetCellAmbienceType(x, z, &first, &second);
          assert(category >= 0 && category < 15 && channels[category]);
          AuAmbientChannel *channel = channels[category];
          i64 firstSum = static_cast<i64>(channel->firstTotal) + first;
          i64 secondSum = static_cast<i64>(channel->secondTotal) + second;
          assert(channel->cells != INT32_MAX && firstSum >= INT32_MIN && firstSum <= INT32_MAX &&
              secondSum >= INT32_MIN && secondSum <= INT32_MAX);
          ++channel->cells;
          channel->firstTotal = static_cast<i32>(firstSum);
          channel->secondTotal = static_cast<i32>(secondSum);
        }
        assert(z <= INT32_MAX - step);
        z += step;
      }
      assert(x <= INT32_MAX - step);
      x += step;
    }
    if (visibleCells) {
      for (i32 i = 0; i < 15; ++i) {
        AuAmbientChannel *channel = channels[i];
        i64 percentage = 100 * static_cast<i64>(channel->cells);
        assert(percentage <= INT32_MAX);
        channel->percentage = static_cast<i32>(percentage / visibleCells);
        channel->adjustedPercentage = channel->percentage;
      }
      for (i32 i = 0; i < 15; ++i) {
        AuAmbientChannel *channel = channels[i];
        if (channel->cells) {
          channel->firstTotal /= channel->cells;
          channel->secondTotal /= channel->cells;
          i64 weighted = static_cast<i64>(channel->cells) * channel->weight;
          assert(weighted >= INT32_MIN && weighted <= INT32_MAX);
          channel->adjustedPercentage = static_cast<i32>(weighted / visibleCells);
        } else {
          channel->adjustedPercentage = 0;
        }
      }
      i32 eligible = 0, other = 0;
      for (i32 i = 0; i < 15; ++i) {
        AuAmbientChannel *channel = channels[i];
        i64 sum = static_cast<i64>(channel->enabled && channel->adjustedPercentage >= channel->threshold ?
            eligible : other) + channel->adjustedPercentage;
        assert(sum >= INT32_MIN && sum <= INT32_MAX);
        if (channel->enabled && channel->adjustedPercentage >= channel->threshold)
          eligible = static_cast<i32>(sum);
        else
          other = static_cast<i32>(sum);
      }
      if (eligible) {
        for (i32 i = 0; i < 15; ++i) {
          AuAmbientChannel *channel = channels[i];
          if (channel->enabled) {
            if (channel->adjustedPercentage < channel->threshold) {
              channel->adjustedPercentage = 0;
            } else {
              i64 product = static_cast<i64>(other) * channel->adjustedPercentage;
              assert(product >= INT32_MIN && product <= INT32_MAX && !(product == INT32_MIN && eligible == -1));
              i64 adjusted = channel->adjustedPercentage + product / eligible;
              assert(adjusted >= INT32_MIN && adjusted <= INT32_MAX);
              channel->adjustedPercentage = static_cast<i32>(adjusted);
            }
          }
        }
      }
      i32 total = 0;
      for (i32 i = 0; i < 15; ++i) {
        if (channels[i]->enabled) {
          i64 sum = static_cast<i64>(total) + channels[i]->adjustedPercentage;
          assert(sum >= INT32_MIN && sum <= INT32_MAX);
          total = static_cast<i32>(sum);
        }
      }
      if (total) {
        for (i32 i = 0; i < 15; ++i) {
          AuAmbientChannel *channel = channels[i];
          if (channel->enabled) {
            i64 scaled = 100 * static_cast<i64>(channel->adjustedPercentage);
            assert(scaled >= INT32_MIN && scaled <= INT32_MAX && !(scaled == INT32_MIN && total == -1));
            channel->adjustedPercentage = static_cast<i32>(scaled / total);
          }
        }
        for (i32 i = 0; i < 15; ++i) {
          AuAmbientChannel *channel = channels[i];
          if (channel->enabled) {
            i32 increment = 25 - channel->adjustedPercentage % 25;
            if (increment < 10) {
              i64 adjusted = static_cast<i64>(channel->adjustedPercentage) + increment;
              assert(adjusted <= INT32_MAX);
              channel->adjustedPercentage = static_cast<i32>(adjusted);
            }
            if (channel->adjustedPercentage > 100)
              channel->adjustedPercentage = 100;
          }
        }
        for (i32 i = 0; i < 15; ++i) {
          AuAmbientChannel *channel = channels[i];
          AuFreshnessPlayer *player = channel->player;
          if (!player)
            continue;
          if (viewZoom == 3) {
            player->Stop();
            continue;
          }
          i32 zoomPosition = viewZoom == 5 ? 1000 : 0;
          bool blocked = !channel->flag && audioInfo->DisasterCount();
          if (!channel->adjustedPercentage || !ambienceVolume || blocked) {
            player->FadeVolume(2000, 0, 1);
            continue;
          }
          player->SetValue16548(channel->adjustedPercentage);
          if (channel->bias && channel->adjustedPercentage) {
            i64 product = static_cast<i64>(channel->bias) * channel->firstTotal;
            assert(product >= INT32_MIN && product <= INT32_MAX);
            i32 position = static_cast<i32>(product / 10);
            if (position > 1000)
              position = 1000;
            if (position < -1000)
              position = -1000;
            if (channel->bias < 0)
              position += 1000;
            if (position >= 1000)
              position = 1000;
            player->SetX(position);
          }
          player->StartAtVolume(ambienceVolume);
          player->SetZ(zoomPosition);
          if (channel->adjustedPercentage) {
            i64 range = 1024ll - minimumFadeVolume;
            i64 product = static_cast<i64>(channel->adjustedPercentage) * range;
            assert(range >= INT32_MIN && range <= INT32_MAX && product >= INT32_MIN && product <= INT32_MAX);
            i64 mix = static_cast<i64>(minimumFadeVolume) + product / 100;
            assert(mix >= INT32_MIN && mix <= INT32_MAX);
            product = static_cast<i64>(player->volume) * mix;
            assert(product >= INT32_MIN && product <= INT32_MAX);
            i32 volume = static_cast<i32>(product / 1024);
            if (volume >= 1024)
              volume = 1024;
            player->SetVolume(volume);
          }
        }
      }
    }
  } else {
    for (i32 i = 0; i < 15; ++i) {
      if (channels[i]->player)
        channels[i]->player->Stop();
    }
  }
}

// Refresh each configured disaster effect in the original fixed index order.
void AuAudioManager::RefreshDisasterEffects() {
  static constexpr i32 effectIds[] = {
      62, 61, 60, 84, 63, 74, 75, 175, 229, 231, 233, 235, 237, 247, 248, 249, 255};
  for (i32 id : effectIds) {
    if (effects[id]) {
      assert(dynamic_cast<AuDisasterEffect *>(effects[id]));
      static_cast<AuDisasterEffect *>(effects[id])->Refresh();
    }
  }
}

// Update the building, landscape, crime, traffic and underground ambience after the original counter threshold.
u8 AuAudioManager::UpdateAmbienceMix() {
  if (!flag126) {
    AuFreshnessPlayer **inactivePlayers[] = {&player352, &player356, &player360, &player364, &player348, &player344};
    for (AuFreshnessPlayer **entry : inactivePlayers)
      if (*entry)
        (*entry)->FadeVolume(2000, 0, 1);
    return 1;
  }
  if (static_cast<i32>(unknown172) < 4) {
    flag176 = 1;
    return 1;
  }
  flag176 = 0;
  unknown172 = 0;
  unknown2496 = viewZoom;
  unknown2512 = viewMode;
  assert(buildingAmbience);
  buildingAmbience->Update();
  UpdateAmbienceChannels();
  i64 trafficSum = static_cast<i64>(channels[2]->percentage) + channels[3]->percentage;
  assert(trafficSum >= INT32_MIN && trafficSum <= INT32_MAX);
  trafficSum += channels[4]->percentage;
  assert(trafficSum >= INT32_MIN && trafficSum <= INT32_MAX && trafficSum * 2 >= INT32_MIN &&
      trafficSum * 2 <= INT32_MAX);
  unknown2524 = static_cast<i32>(trafficSum * 2);
  if (unknown2524 > 100)
    unknown2524 = 100;
  i32 trafficVolume = 0;
  if (unknown2524 > 10 && ambienceVolume) {
    i64 product = static_cast<i64>(ambienceVolume) * unknown2524;
    i64 denominator = 100 * static_cast<i64>(ambienceVolume);
    assert(product >= INT32_MIN && product <= INT32_MAX && denominator >= INT32_MIN && denominator <= INT32_MAX);
    product *= zoomLoopVolume;
    assert(product >= INT32_MIN && product <= INT32_MAX && !(product == INT32_MIN && denominator == -1));
    i64 volume = product / denominator;
    assert(!volume || volume + 400 <= INT32_MAX);
    if (volume)
      volume += 400;
    trafficVolume = volume >= 1024 ? 1024 : static_cast<i32>(volume);
  }
  RefreshDisasterEffects();
  if (playbackState != 5 || viewMode != 0 || viewZoom < 4) {
    player352->FadeVolume(2000, 0, 1);
    player356->FadeVolume(2000, 0, 1);
    unknown2528 = 0;
  } else {
    i32 left, right, top, bottom;
    GetScanBounds(&left, &right, &top, &bottom);
    assert(left > right || (right != INT32_MAX && (top > bottom || bottom != INT32_MAX)));
    i32 count = 0, crimeTotal = 0, unpowered = 0;
    for (i32 x = left; x <= right; ++x) {
      for (i32 z = top; z <= bottom; ++z) {
        if (IsAudibleCell(x, z)) {
          assert(audioInfo);
          i32 crime = audioInfo->GetCellCrimeValue(x, z);
          i64 sum = static_cast<i64>(crimeTotal) + crime;
          assert(sum >= INT32_MIN && sum <= INT32_MAX);
          crimeTotal = static_cast<i32>(sum);
          assert(audioInfo);
          i32 radiation = audioInfo->GetCellRadiationValue(x, z);
          AuInfo *info = audioInfo;
          i64 scaled = 100 * static_cast<i64>(radiation);
          i64 radiationTotal = static_cast<i64>(unknown2528) + scaled;
          assert(scaled >= INT32_MIN && scaled <= INT32_MAX && radiationTotal >= INT32_MIN &&
              radiationTotal <= INT32_MAX);
          unknown2528 = static_cast<i32>(radiationTotal);
          assert(info);
          if (info->CellIsUnpoweredBuilding(x, z)) {
            assert(unpowered != INT32_MAX);
            ++unpowered;
          }
          assert(count != INT32_MAX);
          ++count;
        }
      }
    }
    i32 crimePercent = 0, unpoweredPercent = 0;
    if (count) {
      i64 scaledCrime = 100 * static_cast<i64>(crimeTotal), denominator = 255 * static_cast<i64>(count);
      i64 scaledUnpowered = 100 * static_cast<i64>(unpowered);
      assert(scaledCrime >= INT32_MIN && scaledCrime <= INT32_MAX && denominator <= INT32_MAX &&
          scaledUnpowered <= INT32_MAX);
      crimePercent = static_cast<i32>(scaledCrime / denominator);
      unpoweredPercent = static_cast<i32>(scaledUnpowered / count);
      unknown2528 /= count;
    }
    if (unknown2528 >= 100)
      unknown2528 = 100;
    if (!crimePercent || viewMode || viewZoom < 4) {
      player352->FadeVolume(2000, 0, 1);
    } else {
      i64 frequency = 65 * static_cast<i64>(crimePercent);
      assert(frequency >= INT32_MIN && frequency <= INT32_MAX);
      frequency /= 40;
      if (frequency) {
        player352->StartAtVolume(ambienceVolume);
        player352->SetValue60(static_cast<i32>(frequency));
        player352->SetZ(viewZoom == 4 ? 0 : 1000);
      } else {
        player352->Stop();
      }
    }
    if (unpoweredPercent && ambienceVolume && viewZoom >= 4 && !viewMode) {
      i64 value = 2 * static_cast<i64>(unpoweredPercent);
      assert(value >= INT32_MIN && value <= INT32_MAX);
      player356->SetValue16548(value >= 100 ? 100 : static_cast<i32>(value));
      player356->StartAtVolume(ambienceVolume);
      player356->SetZ(viewZoom == 4 ? 0 : 1000);
    } else {
      player356->FadeVolume(2000, 0, 1);
    }
  }
  i32 coverage = viewMode == 2 && playbackState == 5 ? PipeCoverage() : 0;
  if (coverage) {
    i64 position = 10 * static_cast<i64>(coverage);
    assert(position >= INT32_MIN && position <= INT32_MAX);
    player360->StartAtVolume(ambienceVolume);
    player360->SetZ(static_cast<i32>(position));
  } else {
    player360->FadeVolume(2000, 0, 1);
  }
  coverage = viewMode == 1 && playbackState == 5 ? SubwayCoverage() : 0;
  if (coverage) {
    i64 position = 10 * static_cast<i64>(coverage);
    assert(position >= INT32_MIN && position <= INT32_MAX);
    player364->StartAtVolume(ambienceVolume);
    player364->SetZ(static_cast<i32>(position));
  } else {
    player364->FadeVolume(2000, 0, 1);
  }
  assert(effects[kAuEffectUndergroundLoop]);
  effects[kAuEffectUndergroundLoop]->UnresolvedSlot56(0,
      ambienceVolume && playbackState == 5 && (viewMode == 1 || viewMode == 2) ? static_cast<uptr>(ambienceVolume) : 0);
  i32 density = 0;
  if (unknown2520 >= trafficDensityMinimum) {
    i64 product = static_cast<i64>(unknown2520) * trafficDensityMultiplier;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    density = product >= 75 ? 75 : static_cast<i32>(product);
  }
  if (density && ambienceVolume && !viewMode && viewZoom >= 3 && playbackState == 5) {
    player328->StartAtVolume(ambienceVolume);
    player328->SetValue60(density);
    i64 position = 1000 * static_cast<i64>(density);
    assert(position >= INT32_MIN && position <= INT32_MAX);
    player328->SetX(static_cast<i32>(position / 75));
    if (viewZoom == 5)
      player328->SetZ(1000);
    else if (viewZoom == 4)
      player328->SetZ(500);
    else if (viewZoom == 3)
      player328->SetZ(0);
  } else {
    player328->FadeVolume(2000, 0, 1);
  }
  if (!viewMode && ambienceVolume && playbackState == 5 && (viewZoom == 1 || viewZoom == 2)) {
    player304->StartAtVolume(ambienceVolume);
    player304->SetZ(viewZoom == 1 ? 0 : 500);
  } else if (viewMode || !ambienceVolume || playbackState != 5 || (viewZoom > 2 && viewZoom <= 5)) {
    player304->FadeVolume(2000, 0, 1);
  }
  if (viewZoom == 3 && !viewMode && ambienceVolume && playbackState == 5) {
    player320->StartAtVolume(ambienceVolume);
    if (trafficVolume) {
      i32 position = unknown2516 > kAuLargeCityTrafficAmbiencePopulation
          ? 1000
          : (unknown2516 > kAuMediumCityTrafficAmbiencePopulation ? 500 : 0);
      player324->SetZ(position);
      i64 product = static_cast<i64>(trafficVolume) * ambienceVolume;
      assert(product >= INT32_MIN && product <= INT32_MAX);
      i32 volume = static_cast<i32>(product / 1024);
      if (volume > 1024)
        volume = 1024;
      if (volume < 0)
        volume = 0;
      player324->StartAtVolume(volume);
    } else {
      player324->Stop();
    }
  } else {
    player320->FadeVolume(2000, 0, 1);
    player324->FadeVolume(2000, 0, 1);
  }
  assert(effects[kAuEffectZoom4Loop]);
  effects[kAuEffectZoom4Loop]->UnresolvedSlot56(
      0, viewZoom == 4 && !viewMode && ambienceVolume && playbackState == 5 ? static_cast<uptr>(trafficVolume) : 0);
  return 1;
}

// Capture system and driver versions, configure installed audio paths, and initialize the connected audio objects.
// Music and effects share the configured game directory; the retired CD-to-disk cache is never started.
bool AuAudioManager::Init(ScString *configuration) {
  ScString scratch;
  bool result = false;
  CtVersionInfo version;
  ScString systemVersion;
  u32 major = static_cast<u32>(version.GetSystemVersion(0u)), minor = static_cast<u32>(version.GetSystemVersion(1u));
  version.GetSystemVersion(&systemVersion);
  GxGraphics *graphics = AuGetManagerGraphics();
  assert(graphics);
  graphics->GetVideoDriverVersionInformation(&major, &minor);
  unknown2620 = static_cast<i32>(major);
  ScLock *heldLock = lock;
  heldLock->Lock();
  ApPreferences *preferences = AuGetManagerPreferences();
  assert(preferences);
  ApSoundSettings *sound = preferences->SoundPrefs();
  if (static_cast<u32>(sound->music_volume) > 1024)
    sound->music_volume = 1000;
  if (static_cast<u32>(sound->sound_effects_volume) > 1024)
    sound->sound_effects_volume = 700;
  if (static_cast<u32>(sound->ambience_volume) > 1024)
    sound->ambience_volume = 900;
  musicVolume = AuGetManagerPreferences()->SoundPrefs()->music ? sound->music_volume : 0;
  effectsVolume = AuGetManagerPreferences()->SoundPrefs()->sound_effects ? sound->sound_effects_volume : 0;
  ambienceVolume = AuGetManagerPreferences()->SoundPrefs()->ambience ? sound->ambience_volume : 0;
  assert(configuration);
  {
    ScString directory(*configuration);
    directory.Append("Res\\Sound\\");
    AuSoundDirectory = directory;
    AuMusicDirectory = directory;
  }
  {
    ScString iniPath(AuSoundDirectory);
    iniPath.Append("audio.ini");
    AuAudioIni ini(iniPath, this);
    WnResources *resources = AuGetManagerWindowResources();
    assert(resources);
    resources->SetVolume(effectsVolume);
    AuManagerSoundDevice = AuGetManagerSoundDevice();
    assert(AuManagerSoundDevice);
    if (AuManagerSoundDevice->GetPrimarySound()) {
      listener.Init();
      buildingSounds.Init(this);
      {
        ScString path(AuSoundDirectory);
        path.Append("Sfx\\Disasters\\Geiger.xa");
        geigerSound = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(2));
      }
      buildingAmbience = new AuBuildingAmbience();
      buildingAmbience->SetManager(this);
      scheduler = new AuFreshnessScheduler();
      AuSchedulerSingleton = scheduler;
      musicPlayer = new AuMusicPlayer();
      musicPlayer->Init(this, AuManagerSoundDevice);
      musicPlayer->SetVolume(musicVolume);
      if (ini.LoadAudio()) {
        bool found;
        {
          ScString name("MusicPlayerSongDurationMsec"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found) {
          u32 value = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
          if (value)
            musicPlayer->value156 = value;
        }
        {
          ScString name("MusicPlayerSongDelayMsec"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found)
          musicPlayer->value160 = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
        {
          ScString name("FreshnessIntroDurationMsec"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found)
          musicPlayer->value164 = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
        {
          ScString name("FreshnessOutroDurationMsec"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found)
          musicPlayer->value168 = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
        {
          ScString name("FreshnessMaxXSpeed"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found)
          musicPlayer->value172 = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
        const char *names[] = {"ZoomLoopVol", "BuildingRandMultiplier", "UseCdForMusic", "TrafficDensityMin",
            "TrafficDensityMultiplier", "VehicleHonkDivisor", "VehicleMaxDistance", "VehicleMinDistance",
            "BuildingsInAmbienceAttenuation", "BuildingsInAmbienceMinZoom", "HonkAttenuation", "HonkMinZoom",
            "VehicleAttenuation", "VehicleMinZoom", "VehicleEffectsLevel"};
        i32 *values[] = {&zoomLoopVolume, &buildingRandomMultiplier, &buildingRandomMultiplier,
            &trafficDensityMinimum, &trafficDensityMultiplier, &vehicleHonkDivisor, &vehicleMaximumDistance,
            &vehicleMinimumDistance, &buildingAmbienceAttenuation, &buildingAmbienceMinimumZoom, &honkAttenuation,
            &honkMinimumZoom, &vehicleAttenuation, &vehicleMinimumZoom, &vehicleEffectsLevel};
        for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
          {
            ScString name(names[i]), section("Options");
            found = ini.ReadEntry(section, name, &scratch);
          }
          if (found)
            *values[i] = static_cast<i32>(strtoul(scratch.Data(), nullptr, 10));
        }
        {
          ScString name("VehicleEffectsPreset"), section("Options");
          found = ini.ReadEntry(section, name, &scratch);
        }
        if (found) {
          vehicleEffectsPreset = static_cast<i32>(strtoul(scratch.Data(), nullptr, 10));
          AuManagerSoundDevice->SetEffectsPreset(vehicleEffectsPreset);
        }
        const char *remainingNames[] = {"VehicleEffectsTallBuildingCrit", "ForceCrimeTempo", "ForceAmbienceQuadrant",
            "AmbienceStartFadeAtPercent", "AmbienceMinimumFadedVolume", "FreshnessSelectionAreaMinMsec",
            "MusicStartDelayAfterStop"};
        i32 *remainingValues[] = {&vehicleTallBuildingThreshold, &forceCrimeTempo, &forceAmbienceQuadrant,
            &unknown2604, &minimumFadeVolume, &musicStartDelay, &unknown2616};
        for (u32 i = 0; i < sizeof(remainingNames) / sizeof(remainingNames[0]); ++i) {
          {
            ScString name(remainingNames[i]), section("Options");
            found = ini.ReadEntry(section, name, &scratch);
          }
          if (found) {
            u32 value = static_cast<u32>(strtoul(scratch.Data(), nullptr, 10));
            *remainingValues[i] = static_cast<i32>(value);
            if (i == 2 && value) {
              AuForcedQuadrantEnabled = 1;
              AuForcedQuadrant = value - 1;
            }
          }
        }
        ScString zoomPath(AuSoundDirectory);
        zoomPath.Append("Sfx\\Zones\\ambzoom4.xa");
        if (!sound368)
          sound368 = AuManagerSoundDevice->SndCreate(&zoomPath, static_cast<AuStreamingType>(1));
        {
          ScString commercialPath(AuSoundDirectory);
          commercialPath.Append("sfx\\zones\\commercial\\");
          scratch = commercialPath;
        }
        player284 = new AuFreshnessPlayer;
        {
          ScString filename("commercial.fsc");
          player284->InitFile(&scratch, &filename, scheduler, nullptr);
        }
        AuFreshnessPlayer **remainingPlayers[] = {&player288, &player292, &player268, &player272, &player276,
            &player280, &player296, &player300, &player304, &player312, &player316, &player320, &player324,
            &player328, &player332, &player336, &player340, &player344, &player348, &player352, &player356,
            &player360, &player364};
        const char *playerDirectories[] = {"sfx\\zones\\industrial\\", "sfx\\zones\\residential\\",
            "sfx\\zones\\nature\\", "sfx\\zones\\nature\\", "sfx\\zones\\nature\\", "sfx\\zones\\wind\\",
            "sfx\\zones\\nature\\", "sfx\\zones\\nature\\", "sfx\\zones\\Wind\\", "sfx\\zones\\Construction\\",
            "sfx\\zones\\Water\\", "sfx\\zones\\Zoom3\\", "sfx\\zones\\Zoom3City\\", "sfx\\zones\\Traffic\\",
            "sfx\\zones\\Agri\\", "sfx\\zones\\Seaport\\", "sfx\\zones\\Airport\\", "sfx\\zones\\Abandoned\\",
            "sfx\\zones\\Garbage\\", "sfx\\zones\\Crime\\", "sfx\\zones\\Unpowered\\", "sfx\\zones\\pipes\\",
            "sfx\\zones\\subway_v\\"};
        const char *playerFiles[] = {"industrial.fsc", "residential.fsc", "Nature.fsc", "Desert.fsc", "Jungle.fsc",
            "Arctic.fsc", "Desert.fsc", "Jungle.fsc", "Wind.fsc", "construction.fsc", "water.fsc", "zm3.fsc",
            "zm3city.fsc", "traffic.fsc", "Agricult.fsc", "Seaport.fsc", "Airports.fsc", "Abandoned.fsc",
            "Garbage.fsc", "Crime.fsc", "Unpwrd.fsc", "pipes.fsc", "subway_v.fsc"};
        for (u32 i = 0; i < sizeof(remainingPlayers) / sizeof(remainingPlayers[0]); ++i) {
          ScString directory(AuSoundDirectory);
          directory.Append(playerDirectories[i]);
          scratch = directory;
          *remainingPlayers[i] = new AuFreshnessPlayer;
          ScString filename(playerFiles[i]);
          (*remainingPlayers[i])->InitFile(&scratch, &filename, scheduler, nullptr);
          if (i == 5)
            player264 = player268;
        }
        channels[0] = new AuAmbientChannel(0, 100, 1, 101, 0, nullptr, 1);
        channels[1] = new AuAmbientChannel(1, 34, 1, 25, -500, player264, 0);
        channels[2] = new AuAmbientChannel(2, 200, 1, 25, 100, player292, 0);
        channels[3] = new AuAmbientChannel(3, 300, 1, 25, 100, player284, 1);
        channels[4] = new AuAmbientChannel(4, 300, 1, 25, 100, player288, 1);
        channels[11] = new AuAmbientChannel(11, 300, 1, 25, 100, player336, 1);
        channels[10] = new AuAmbientChannel(10, 300, 1, 25, 100, player340, 1);
        channels[5] = new AuAmbientChannel(5, 800, 0, 0, 100, player312, 0);
        channels[6] = new AuAmbientChannel(6, 34, 1, 25, 0, player316, 1);
        channels[7] = new AuAmbientChannel(7, 100, 1, 101, 0, nullptr, 1);
        channels[8] = new AuAmbientChannel(8, 200, 1, 25, 0, player332, 0);
        channels[9] = new AuAmbientChannel(9, 200, 1, 50, 0, player344, 1);
        channels[12] = new AuAmbientChannel(12, 100, 1, 50, 0, nullptr, 1);
        channels[13] = new AuAmbientChannel(13, 100, 1, 101, 0, nullptr, 1);
        channels[14] = new AuAmbientChannel(14, 250, 0, 30, 0, player348, 1);
        for (i32 i = 0; i < 512; ++i)
          if (effects[i])
            effects[i]->Init(this);
        ScMessageServer *server = AuGetMessageServer();
        assert(server);
        server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ActivateApp);
        clock->Start();
        unknown168 = 0;
        StartTimer();
        random.Seed(kScRandomSeedFromClock);
        initialized = 1;
        result = true;
      }
    }
  }
  heldLock->Unlock();
  return result;
}

void AuAudioManager::StartTimer() {
  if (!updateTimer) {
    updateTimer = new AuCallbackTimer;
    updateTimer->SetResolution(0);
    updateTimer->SetCallbackFunction(AuManagerTimerCallback, reinterpret_cast<uptr>(this));
    updateTimer->StartPeriodic(100, nullptr);
  }
}

// Stop the manager timer, allow pending callbacks to finish and destroy the timer.
void AuAudioManager::StopTimer() {
  if (updateTimer) {
    updateTimer->Stop();
    if (clock)
      clock->Delay(500);
    delete updateTimer;
    updateTimer = nullptr;
  }
}

void AuAudioManager::StopLoop() {
  if (loopSound) {
    loopSound->Stop();
    loopSound->Unload();
    loopSound->Release();
    loopSound = nullptr;
  }
  loopCookie = 0;
}

// Reuse a matching looping sound or construct and play the requested file at the supplied volume.
void AuAudioManager::SetLoop(ScString *name, i32 looping, i32 requestedVolume, i32 cookie, u8 streaming) {
  if (!requestedVolume || !cookie || static_cast<u32>(cookie) != loopCookie)
    StopLoop();
  if (requestedVolume) {
    if (!cookie || static_cast<u32>(cookie) == loopCookie) {
      if (loopSound) {
        loopSound->SetVolume(requestedVolume);
      } else {
        assert(AuManagerSoundDevice);
        loopSound = AuManagerSoundDevice->SndCreate(name, static_cast<AuStreamingType>(streaming != 0));
        assert(loopSound);
        loopSound->Play(requestedVolume, static_cast<u8>(looping), static_cast<AuSoundDuplication>(1));
      }
    } else {
      ScString copiedPath(*name);
      assert(AuManagerSoundDevice);
      loopSound = AuManagerSoundDevice->SndCreate(&copiedPath, static_cast<AuStreamingType>(streaming != 0));
      assert(loopSound);
      loopSound->Play(requestedVolume, static_cast<u8>(looping), static_cast<AuSoundDuplication>(1));
    }
  }
  loopCookie = static_cast<u32>(cookie);
}

// Stop and release the manager's audio objects, players and scheduler in their original shutdown order.
bool AuAudioManager::Shutdown() {
  if (!initialized)
    return true;
  lock->Lock();
  StopLoop();
  StopTimer();
  ScRelease(&sound368);
  ScRelease(&sound380);
  if (musicPlayer) {
    musicPlayer->Stop();
    musicPlayer->Shutdown();
    delete musicPlayer;
    musicPlayer = nullptr;
  }
  ScDelete(&buildingAmbience);
  for (i32 i = 0; i < 512; ++i) {
    if (effects[i]) {
      effects[i]->Shutdown();
      delete effects[i];
      effects[i] = nullptr;
    }
  }
  if (scheduler)
    scheduler->Pause();
  for (i32 i = 0; i < 15; ++i) {
    delete channels[i];
    channels[i] = nullptr;
  }
  if (geigerSound) {
    geigerSound->Stop();
    geigerSound->Release();
    geigerSound = nullptr;
  }
  AuFreshnessPlayer **retiringPlayers[] = {&player304, &player320, &player324, &player328, &player352, &player356,
      &player360, &player364, &player268, &player284, &player288, &player292, &player272, &player276, &player280,
      &player296, &player300, &player308, &player312, &player316, &player332, &player336, &player340, &player344,
      &player348};
  for (AuFreshnessPlayer **entry : retiringPlayers) {
    if (*entry) {
      (*entry)->Stop();
      (*entry)->Shutdown();
      delete *entry;
      *entry = nullptr;
    }
  }
  if (scheduler) {
    AuSchedulerSingleton = nullptr;
    scheduler->Shutdown();
    delete scheduler;
    scheduler = nullptr;
  }
  buildingSounds.Shutdown();
  initialized = 0;
  return true;
}

// Apply playback-state and volume gates, update the loop sound and select the requested special song.
u8 AuAudioManager::UpdateMusic() {
  if (!initialized || playbackState <= 0)
    return 1;
  if (playbackState <= 2 || playbackState == 4) {
    if (!musicPlayer)
      return 1;
    ScString path(*AuGetMusicDirectory());
    path.Append("Music\\3kloop.wav");
    musicPlayer->Stop();
    if (musicVolume)
      SetLoop(&path, 1, musicVolume, 2, 0);
    else
      StopLoop();
    return 1;
  }
  if (playbackState != 3 && playbackState != 5)
    return 1;
  if (flag48)
    flag48 = 0;
  if (!musicPlayer)
    return 1;
  if (!musicVolume || (audioInfo && audioInfo->DisasterCount())) {
    musicPlayer->Stop();
    return 1;
  }
  musicPlayer->SetVolume(musicVolume);
  if (musicVolume && !musicPlayer->IsPlaying() && clock->GetElapsedTime() - unknown20 > static_cast<u32>(unknown2616)) {
    musicPlayer->Play(musicVolume);
    unknown20 = 0;
  }
  if (flag32) {
    if (musicPlayer->IsSongEnabled(1))
      musicPlayer->PlaySong(1);
    flag32 = 0;
  }
  return 1;
}

// Play the selected cell interface sound or its network overlay for the current city view.
void AuAudioManager::PlayCellEffect(i32 x, i32 z) {
  assert(audioInfo);
  i32 soundId = audioInfo->GetCellInterfaceSoundId(x, z);
  i32 mode = viewMode;
  if (mode == 0) {
    assert(audioInfo);
    if (audioInfo->CellHasSpriteInSurfaceView(x, z)) {
      if (soundId) {
        assert(soundId >= 0 && soundId < 512);
        if (effects[soundId])
          effects[soundId]->UnresolvedSlot56(0, static_cast<u32>(effectsVolume));
        soundId = 22;
      } else {
        soundId = CellOverlaySoundId(x, z);
      }
    }
  } else if (mode == 1 || mode == 2) {
    assert(audioInfo);
    u8 sprite = mode == 1 ? audioInfo->CellHasSpriteInSubwayView(x, z) :
        audioInfo->CellHasSpriteInPipeView(x, z);
    if (!sprite) {
      if (mode == 1)
        soundId = 53;
    } else if (!soundId) {
      soundId = CellOverlaySoundId(x, z);
    } else {
      assert(soundId >= 0 && soundId < 512);
      if (effects[soundId])
        effects[soundId]->UnresolvedSlot56(0, static_cast<u32>(effectsVolume));
      soundId = 0;
    }
  } else if (mode == 12) {
    soundId = 16;
  }
  if (soundId) {
    assert(soundId >= 0 && soundId < 512);
    if (effects[soundId])
      effects[soundId]->Play(effectsVolume);
  }
}

// Respond to the three ambience event subtypes by stopping music or playing the matching ambience cue.
void AuAudioManager::HandleAmbienceEvent(i32 value, u32 subtype) {
  if (subtype == 1 && value == 60) {
    assert(musicPlayer);
    musicPlayer->Stop();
  } else if (subtype == 2 && viewZoom >= 3) {
    assert(musicPlayer);
    musicPlayer->StopCurrentSong();
    i32 highest = 0, selected = 0;
    for (i32 i = 0; i < 15; ++i) {
      assert(channels[i]);
      if (channels[i]->percentage > highest) {
        highest = channels[i]->adjustedPercentage;
        selected = i;
      }
    }
    i32 soundId = selected == 2 ? 59 : (selected == 3 || selected == 4 ? 64 : 58);
    assert(effects[soundId]);
    effects[soundId]->Play(ambienceVolume);
  } else if (subtype == 3 && value == 61) {
    assert(effects[kAuEffectDisasterPremonitionTremor]);
    effects[kAuEffectDisasterPremonitionTremor]->Play(ambienceVolume);
  }
}

// Dispatch the installed music, effect and city events under the audio-manager lock.
i32 AuAudioManager::Event(i32 event, uptr first, uptr second, uptr third, uptr fourth) {
  if (!initialized)
    return 1;
  ScLock *heldLock = lock;
  heldLock->Lock();
  i32 value = static_cast<i32>(first);
  u32 secondValue = static_cast<u32>(second);
  if (event == 1010)
    flag33 = 1;
  else if (event == kAuEventCurrentSong) {
    assert(musicPlayer);
    u32 songId = musicPlayer->CurrentSongId();
    if (first)
      *reinterpret_cast<u32 *>(first) = songId;
    if (effects[66])
      effects[66]->Play(effectsVolume);
  } else if (event == kAuEventMoveMusicVolumeSlider) {
    if (effects[66])
      effects[66]->Play(effectsVolume);
  } else if (event == kAuEventSetSongEnabled) {
    assert(musicPlayer);
    musicPlayer->SetSongEnabled(static_cast<u32>(first), second != 0);
    musicPlayer->UnresolvedHook();
  } else if (event == kAuEventStopSong) {
    assert(musicPlayer);
    musicPlayer->StopCurrentSong();
    musicPlayer->UnresolvedHook();
  } else if (event == kAuEventBeginSongEnumeration) {
    assert(musicPlayer);
    musicPlayer->BeginSongEnumeration();
  } else if (event == kAuEventNextSong) {
    assert(musicPlayer);
    assert(musicPlayer->songCursor != std::list<AuMusicSong *>::iterator());
    u32 songId = musicPlayer->NextSongId();
    assert(first);
    *reinterpret_cast<u32 *>(first) = songId;
    if (songId) {
      AuMusicSong *song = musicPlayer->FindSong(songId);
      assert(song);
      ScString name;
      song->GetName(&name);
      assert(second);
      reinterpret_cast<ScStringView *>(second)->FromChar(name.Data(), name.Strlen());
    }
  } else if (event == kAuEventUpdateView)
    UpdateView(reinterpret_cast<SpIsoViewInfo *>(first));
  else if (event == 1011) {
    flag33 = 0;
    UpdateMusic();
  } else if (event == kAuEventMainMenu || event == kAuEventCityCreation || event == kAuEventTerrainEditor ||
      event == kAuEventDisasterEnded) {
    if (event == kAuEventMainMenu)
      playbackState = 2;
    else if (event == kAuEventCityCreation)
      playbackState = 4;
    else if (event == kAuEventTerrainEditor)
      flag32 = 1;
    UpdateMusic();
  } else if (event == kAuEventCityReady || event == 1056) {
    if (audioInfo) {
      playbackState = 5;
      flag126 = 1;
      StopLoop();
      UpdateMusic();
      random.Seed(kScRandomSeedFromClock);
      assert(audioInfo);
      if (audioInfo->IsInEditModeCity())
        UpdateAmbienceMix();
    }
  } else if (event == kAuEventCityPopulationChanged) {
    unknown2516 = value;
    UpdateAmbienceMix();
  } else if (event == kAuEventBeginFileSelection || event == 1053) {
    if (playbackState != 2) {
      savedPlaybackState = playbackState;
      playbackState = 2;
    }
    UpdateAmbienceMix();
    UpdateMusic();
  } else if (event == kAuEventCancelFileSelection || event == 1054) {
    playbackState = savedPlaybackState;
    if (playbackState == 5) {
      StopLoop();
    }
    UpdateAmbienceMix();
    UpdateMusic();
  } else if (event == kAuEventSetAmbienceVolume && ambienceVolume != value) {
    ambienceVolume = value;
    unknown172 = 1000;
    AuAmbienceMixFlag = 1;
    UpdateAmbienceMix();
    AuAmbienceMixFlag = 0;
  } else if (event == kAuEventPlayBuildingSound) {
    buildingSounds.Play(value, static_cast<i32>(secondValue));
  } else if (event == kAuEventPlayCellEffect) {
    PlayCellEffect(value, static_cast<i32>(secondValue));
  } else if (event == kAuEventPlayCellInterfaceSound) {
    i32 x = value, z = static_cast<i32>(secondValue);
    assert(audioInfo);
    i32 soundId = audioInfo->GetCellInterfaceSoundId(x, z), mode = viewMode;
    bool useInterface = mode == 0;
    if (mode == 1 || mode == 2) {
      assert(audioInfo);
      useInterface = mode == 1 ? audioInfo->CellHasSpriteInSubwayView(x, z) != 0 :
          audioInfo->CellHasSpriteInPipeView(x, z) != 0;
    }
    if (useInterface && soundId) {
      assert(soundId >= 0 && soundId < 512);
      if (effects[soundId])
        effects[soundId]->UnresolvedSlot56(0, static_cast<u32>(effectsVolume));
    } else {
      soundId = CellSoundId(x, z);
      if (soundId) {
        assert(soundId >= 0 && soundId < 512);
        if (effects[soundId])
          effects[soundId]->Play(0);
      }
    }
  } else if (event == 11 || event == kAuEventButtonClick || event == 21 || event == 23 || event == 29 || event == 31 ||
      event == 35 || event == 41 || event == 45 || event == 48 || event == 52 || event == kAuEventEarlyWarningSiren ||
      event == kAuEventStartDisaster || event == kAuEventMoveAmbienceVolumeSlider ||
      event == kAuEventMoveEffectsVolumeSlider || (event >= 14 && event <= 19) || event == 32 || event == 42 ||
      event == 43 || event == 46 || event == 49 || event == 50 || event == 53 || event == 54) {
    i32 soundId = 56;
    if (event == kAuEventButtonClick)
      soundId = 12;
    else if (event == kAuEventEarlyWarningSiren)
      soundId = 48;
    else if (event == kAuEventStartDisaster)
      soundId = 102;
    else if (event == kAuEventMoveAmbienceVolumeSlider)
      soundId = playbackState == 5 ? -1 : 67;
    else if (event == kAuEventMoveEffectsVolumeSlider)
      soundId = 51;
    else if ((event >= 14 && event <= 19) || event == 32 || event == 42 || event == 43 || event == 46 ||
        event == 49 || event == 50 || event == 53 || event == 54)
      soundId = 146;
    if (soundId >= 0 && effects[soundId])
      effects[soundId]->Play(event == kAuEventMoveEffectsVolumeSlider ? 1020 : effectsVolume);
  } else if (event == kAuEventSuckUpPerson) {
    u32 soundId = value ? (random.RandomUint32Uniform() & 1u) + 85u : random.RandomUint32Uniform() % 5u + 88u;
    assert(effects[soundId]);
    effects[soundId]->Play(ambienceVolume);
  } else if (event == kAuEventAmbience) {
    HandleAmbienceEvent(value, secondValue);
  } else if (event == kAuEventCreditsOpen || event == kAuEventCreditsEndReached || event == kAuEventCreditsDismissed) {
    if (event == kAuEventCreditsOpen) {
      StopLoop();
    }
    assert(musicPlayer);
    musicPlayer->Stop();
    if (event == kAuEventCreditsOpen)
      musicPlayer->PlaySong(1);
    else {
      UpdateMusic();
    }
  } else if (event == kAuEventPlaySong) {
    if (playbackState == 2) {
      playbackState = 3;
      StopLoop();
    }
    assert(musicPlayer);
    musicPlayer->PlaySong(static_cast<u32>(first));
    musicPlayer->UnresolvedHook();
    UpdateMusic();
  } else if (event == kAuEventPlaySong15) {
    assert(musicPlayer);
    musicPlayer->PlaySong(15);
  } else if (event == kAuEventSetMusicVolume) {
    if (musicVolume != value) {
      musicVolume = value;
      if (!value)
        musicPlayer->Stop();
      UpdateMusic();
      if (!musicVolume)
        unknown20 = clock->GetElapsedTime();
    }
  } else if (event == kAuEventSetMuteAll)
    flag124 = value != 0;
  else if (event == kAuEventSetDirectSound3D)
    flag125 = value != 0;
  else if (event == kAuEventGameSpeedChanged)
    unknown2504 = value;
  else if (event == kAuEventTrafficDensityChanged)
    unknown2520 = value;
  else if (event == kAuEventDetachCityInfo) {
    AuFreshnessPlayer **players[] = {&player284, &player288, &player292, &player268, &player272, &player276, &player280,
        &player304, &player312, &player316, &player320, &player324, &player328, &player332, &player336, &player340,
        &player344, &player348, &player352, &player356, &player360, &player364, &player296, &player300};
    for (AuFreshnessPlayer **entry : players) {
      assert(*entry);
      (*entry)->Stop();
    }
    for (AuAudioEffect *effect : effects)
      if (effect)
        effect->UnresolvedSlot52(0);
    flag126 = 0;
    audioInfo = nullptr;
    assert(clock);
    clock->Delay(250);
  } else if (event == kAuEventAttachCityInfo) {
    audioInfo = reinterpret_cast<AuInfo *>(first);
    assert(audioInfo);
    cityWidth = audioInfo->CellCountX();
    cityHeight = audioInfo->CellCountZ();
  } else if ((event >= 1 && event <= 6) || event == kAuEventLoadEffect || event == kAuEventUnloadEffect) {
    if (event != kAuEventPlayEffect || !((value == 1 && viewZoom > 4) || (value == 2 && viewZoom == 1))) {
      assert(value >= 0 && value < 512);
      AuAudioEffect *effect = effects[value];
      if (effect) {
        switch (event) {
        case kAuEventPlayEffect:
          effect->Play(static_cast<i32>(secondValue));
          break;
        case kAuEventEffectSlot52:
          effect->UnresolvedSlot52(second);
          break;
        case kAuEventEffectSlot48:
          effect->UnresolvedSlot48(second, third, 0, fourth);
          break;
        case kAuEventEffectSlot84:
          effect->UnresolvedSlot84(second);
          break;
        case kAuEventEffectSlot56:
          effect->UnresolvedSlot56(second, third);
          break;
        case kAuEventEffectSlot60:
          effect->UnresolvedSlot60(second, third);
          break;
        case kAuEventLoadEffect:
          effect->LoadSound();
          break;
        case kAuEventUnloadEffect:
          effect->UnloadSound();
          break;
        }
      }
    }
  } else if (event == 7 || event == 10 || event == 13) {
    // These event numbers have no installed dispatch case.
  } else if (event == kAuEventSetEffectsVolume) {
    if (effectsVolume != value) {
      effectsVolume = value;
      WnResources *resources = AuGetManagerWindowResources();
      assert(resources);
      resources->SetVolume(effectsVolume);
    }
  } else if (event < 200 || (event > 200 && event < 218 && event != 207) || (event >= 1021 && event <= 1023) ||
      (event >= 1028 && event <= 1036) || (event >= 1040 && event <= 1041) || (event >= 1044 && event <= 1045) ||
      event == 1055 || (event == kAuEventSetAmbienceVolume && ambienceVolume == value) ||
      (event >= 219 && event <= 1009 && event != kAuEventSetMusicVolume && event != kAuEventSetEffectsVolume &&
          event != kAuEventSetAmbienceVolume)) {
    // These original dispatch cases have no side effects when reached.
  }
  heldLock->Unlock();
  return 0;
}

// Update Geiger clicks and the looping radiation sound from the current radiation level.
void AuAudioManager::UpdateRadiation() {
  i32 now = static_cast<i32>(clock->GetElapsedTime());
  i64 nextRadiation = static_cast<i64>(unknown2532) + 10;
  assert(nextRadiation <= INT32_MAX);
  if (now > nextRadiation) {
    if (unknown2528 > 20) {
      i64 scaled = 65535 * (static_cast<i64>(unknown2528) - 20);
      assert(scaled <= INT32_MAX);
      if (random.RandomUint32Uniform(90000) < static_cast<u32>(scaled / 80)) {
        assert(geigerSound);
        geigerSound->Play(ambienceVolume, 0, static_cast<AuSoundDuplication>(1));
      }
      unknown2532 = now;
    }
    if (unknown2528 != AuPreviousRadiation) {
      AuPreviousRadiation = unknown2528;
      if (unknown2528 <= 20) {
        ScRelease(&sound380);
      } else {
        i64 scale = 624 * (static_cast<i64>(unknown2528) - 20);
        assert(scale <= INT32_MAX);
        scale = scale / 80 + 400;
        i64 product = static_cast<i64>(ambienceVolume) * scale;
        assert(scale <= INT32_MAX && product >= INT32_MIN && product <= INT32_MAX);
        i32 volume = static_cast<i32>(product / 1024);
        if (!sound380) {
          assert(AuManagerSoundDevice);
          ScString path(AuSoundDirectory);
          path.Append("sfx\\disasters\\nukerad.xa");
          sound380 = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(0));
        }
        assert(sound380);
        if (sound380->IsPlaying())
          sound380->SetVolume(volume);
        else
          sound380->Play(volume, 1, static_cast<AuSoundDuplication>(2));
      }
    }
  }
}

// Skip updates while the lock is held, then advance music, effects, radiation and ambience under that lock.
void AuAudioManager::Update(u32) {
  if (lock->IsLocked())
    return;
  ScLock *heldLock = lock;
  heldLock->Lock();
  if (!initialized) {
    heldLock->Unlock();
    return;
  }
  u32 elapsed = clock->GetElapsedTime();
  if (unknown20 && elapsed - unknown20 > static_cast<u32>(unknown2616)) {
    UpdateMusic();
    unknown20 = 0;
  }
  musicPlayer->Tick();
  buildingSounds.Update();
  UpdateRadiation();
  u32 currentTime = clock->GetElapsedTime();
  i64 delta = static_cast<i64>(static_cast<i32>(currentTime)) - static_cast<i32>(unknown168);
  assert(delta >= INT32_MIN && delta <= INT32_MAX);
  if (delta > 100) {
    i32 count = static_cast<i32>(unknown172);
    assert(count != INT32_MAX);
    ++count;
    unknown172 = static_cast<u32>(count);
    unknown168 = currentTime;
    if (count > 10) {
      UpdateAmbienceMix();
      if (!unknown20)
        UpdateMusic();
      unknown172 = 0;
    }
    for (i32 i = 0; i < 512; ++i) {
      if (effects[i])
        effects[i]->Update();
    }
  }
  heldLock->Unlock();
}

// Apply the sound enablement message to the shared device and acknowledge every message.
bool AuAudioManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ActivateApp) {
    assert(AuManagerSoundDevice);
    AuManagerSoundDevice->SetMasterVolume(static_cast<u32>(message->data1) ? 1024 : 0);
  }
  return true;
}

AuListenerController::AuListenerController()
    : positionX(0), positionY(0), positionZ(0), facingX(0), facingY(0), facingZ(0), zoom(0), flag28(0),
      listener(nullptr) {
}

u8 AuListenerController::Init() {
  AuSoundDevice *device = AuGetManagerSoundDevice();
  assert(device);
  listener = device->GetSnd3DListener();
  if (!listener)
    listener = device->SndCreate3DListener();
  assert(listener);
  listener->SetRolloffFactor(0.0f);
  listener->SetPosition(0.0f, 0.0f, 0.0f);
  return 1;
}

// Store the integer world position as floats and send the same position to the 3D listener.
u8 AuListenerController::SetPosition(i32 x, i32 y, i32 z) {
  positionX = static_cast<f32>(x);
  positionY = static_cast<f32>(y);
  positionZ = static_cast<f32>(z);
  listener->SetPosition(positionX, positionY, positionZ);
  return 1;
}

// Update the listener's facing only when the integer direction differs from the stored float values.
u8 AuListenerController::SetOrientation(i32 x, i32 y, i32 z) {
  if (static_cast<f64>(x) != facingX || static_cast<f64>(y) != facingY || static_cast<f64>(z) != facingZ) {
    facingX = static_cast<f32>(x);
    facingY = static_cast<f32>(y);
    facingZ = static_cast<f32>(z);
    listener->SetOrientation(facingX, facingY, facingZ, 0.0f, 1.0f, 0.0f);
  }
  return 1;
}

u8 AuListenerController::SetZoom(i32 value) {
  zoom = value;
  return 1;
}

AuRandom::AuRandom(u32 base) {
  Seed(base);
  normalReady = 0;
}

// Seed both streams, using the system millisecond clock for the original all-ones sentinel.
void AuRandom::Seed(u32 base) {
  if (base == kScRandomSeedFromClock)
    base = ScMultimediaMilliseconds();
  current = base;
  second = base;
}

// Advance the original selection word and combine the carried product bits with the new word.
u32 AuRandom::RandomUint32Uniform() {
  u64 product = static_cast<u64>(current) * kScRandomMultiplier;
  u32 low = static_cast<u32>(product), updated = low + kScRandomIncrement;
  u32 high = (static_cast<u32>(product >> 32) << 16) + 0xffffu + (updated < low);
  current = updated;
  return (high & kScRandomRetainedHighWordMask) | (updated >> 16);
}

// Draw a bounded selection, using the original modulo and inclusive rejection paths.
u32 AuRandom::RandomUint32Uniform(u32 bound) {
  if (!bound)
    return 0;
  u32 next = current, value;
  do {
    u64 product = static_cast<u64>(next) * kScRandomMultiplier;
    u32 low = static_cast<u32>(product), high = static_cast<u32>(product >> 32) << 16;
    next = low + kScRandomIncrement;
    high += 0xffffu + (next < low);
    value = (high & kScRandomRetainedHighWordMask) | (next >> 16);
    if (bound < kScRandomModuloThreshold) {
      value %= bound;
      break;
    }
  } while (value > bound);
  current = next;
  return value;
}

// Store the channel's supplied tuning and borrowed player, clearing its five accumulated counters.
AuAmbientChannel::AuAmbientChannel(i32 indexValue, i32 weightValue, u8 enabledValue, i32 thresholdValue, i32 biasValue,
    AuFreshnessPlayer *playerValue, u8 flagValue)
    : index(indexValue), weight(weightValue), enabled(enabledValue), flag(flagValue), threshold(thresholdValue),
      cells(0), firstTotal(0), secondTotal(0), percentage(0), adjustedPercentage(0), bias(biasValue), player(playerValue) {
}
