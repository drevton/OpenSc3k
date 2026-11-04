#ifndef SC3_AUDIO_AUDIO_CORE_H
#define SC3_AUDIO_AUDIO_CORE_H

#include "application/api.h"
#include "audio/api.h"
#include "audio/audio_effects.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "ui/commands.h"

class AuFreshnessPlayer;

// One original ambience channel holds its tuning and the borrowed freshness player it controls.
class AuAmbientChannel {
public:
  // The two flags retain the observed raw byte values; their private bool/BYTE spellings remain unresolved.
  AuAmbientChannel(i32 index, i32 weight, u8 enabled, i32 threshold, i32 bias, AuFreshnessPlayer *player, u8 flag);

  i32 index, weight;
  u8 enabled, flag;
  i32 threshold, cells, firstTotal, secondTotal, percentage, adjustedPercentage, bias;
  AuFreshnessPlayer *player;
};

// AUDIO's original secondary reference identity accounts for its own module director.
class AuCommandOwner {
public:
  AuCommandOwner();
  virtual ~AuCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();

private:
  u32 references;
};

// The common command copy owns both descriptor sequences and both retained argument queues.
class AuGameCommand : public UiCommandInterface, public AuCommandOwner {
public:
  AuGameCommand();
  ~AuGameCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Push retains the argument; a successful pop transfers that retained reference to the caller.
  bool PushBackInput(ScVariant *argument) override;
  bool PopFrontInput(ScVariant **out) override;
  u32 InputQueueSize() override;
  bool EnumInputParameters(void (*callback)(i32, u32, void *), void *context) override;
  // Output arguments have the same retained-until-popped ownership as inputs.
  bool PushBackOutput(ScVariant *argument) override;
  bool PopFrontOutput(ScVariant **out) override;
  u32 OutputQueueSize() override;
  bool EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) override;
  virtual bool AddInputParameter(i32 type, u32 flags);
  virtual bool AddOutputParameter(i32 type, u32 flags);

private:
  ScArray<UiCommandParameter> inputsDescription, outputsDescription;
  ScArray<ScVariant *> inputs, outputs;
};

// The registered AUDIO command describes two inputs and dispatches the original radio and application operations.
class AuRadioCommand : public AuGameCommand {
public:
  AuRadioCommand();
  ~AuRadioCommand() override;
  bool Execute() override;
};

// AUDIO's copy of the chained command factory owns its child factory references.
class AuCommandFactoryBase : public ScCommandFactory {
public:
  AuCommandFactoryBase();
  virtual ~AuCommandFactoryBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 command, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual bool CreateCommand(u32 command, u32 iid, void **out) = 0;

private:
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// The AUDIO director registers this concrete factory for its game command.
class AuCommandFactory : public AuCommandFactoryBase {
public:
  ~AuCommandFactory() override;
  bool CreateCommand(u32 command, u32 iid, void **out) override;
};

class ApGameInterface;

// AUDIO owns a single director with the original ten class registrations and registry entry.
class AuAudioDirector : public ScDllDirector {
public:
  AuAudioDirector();
  ~AuAudioDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

AuAudioDirector *AuGetAudioDirector();
ApGameInterface *AuGetApplication();

class ApBoxX;
class CtAgentTree;
class CtAuraLayer;
class CtPowerLayer;
class CtSchemeManager;
class DsLayer;
class EcLayer;
class NwManager;
class NwTrafficLayer;
class ScDirtBag;
class ScOccupantManager;

// AUDIO's original city-information producer retains the city and its queried layer interfaces.
// Unrecovered layer and occupant methods stop explicitly at their original interface boundaries.
class AuAudioInfo : public AuInfo {
public:
  AuAudioInfo();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  u8 Init(CtCity *target) override;
  u8 Shutdown() override;
  i32 CellCountX() override;
  i32 CellCountZ() override;
  u8 CellHasOccupant(i32 x, i32 z) override;
  u8 CellIsAbandoned(i32 x, i32 z) override;
  u8 CellIsDestroyed(i32 x, i32 z) override;
  u8 CellIsWater(i32 x, i32 z) override;
  u8 CellHasRoad(i32 x, i32 z) override;
  u8 CellHasHighway(i32 x, i32 z) override;
  u8 CellHasRail(i32 x, i32 z) override;
  u8 CellHasSubway(i32 x, i32 z) override;
  u8 CellHasPipe(i32 x, i32 z) override;
  u8 CellHasPower(i32 x, i32 z) override;
  u8 CellHasTunnel(i32 x, i32 z) override;
  u8 CellHasTree(i32 x, i32 z) override;
  u8 CellIsGarbage(i32 x, i32 z) override;
  u8 CellHasSpriteInSurfaceView(i32 x, i32 z) override;
  u8 CellHasSpriteInPipeView(i32 x, i32 z) override;
  u8 CellHasSpriteInSubwayView(i32 x, i32 z) override;
  i32 GetCellAmbienceType(i32 x, i32 z, i32 *first, i32 *second) override;
  i32 GetCellZoneType(i32 x, i32 z) override;
  i32 GetCellPopulation(i32 x, i32 z) override;
  i32 GetCellLandValue(i32 x, i32 z) override;
  i32 GetCellCrimeValue(i32 x, i32 z) override;
  i32 GetCellRadiationValue(i32 x, i32 z) override;
  i32 GetCellAura(i32 x, i32 z) override;
  i32 GetCityMayoralRating() override;
  i32 GetCityPopulation() override;
  i32 GetTrafficDensity(i32 x, i32 z) override;
  i32 GetCellSpriteHeight(i32 x, i32 z) override;
  u8 CellIsPowered(i32 x, i32 z) override;
  u8 CellIsUnpoweredBuilding(i32 x, i32 z) override;
  i32 GetBuildingSoundId(i32 x, i32 z) override;
  i32 GetCellInterfaceSoundId(i32 x, i32 z) override;
  u8 IsInEditModeCity() override;
  i32 DisasterCount() override;
  i32 TerrainHeight(i32 x, i32 z) override;
  u8 GetCityDate(u32 *first, u32 *second, u32 *third) override;
  u32 CurrentFloraSet() override;
  u32 CurrentBuildingSet() override;
  u32 CurrentLandScape() override;
  virtual ~AuAudioInfo();

private:
  u8 initialized;
  CtCity *city;
  ApBoxX *box;
  ScCellMap<u8> *landValue, *secondaryMap;
  CtAuraLayer *aura;
  ScCellMap<u8> *crime;
  ScOccupantManager *occupants;
  CtAgentTree *agentTree;
  ScUnknown *unknown40;
  CtPowerLayer *power;
  EcLayer *pollution;
  NwManager *network;
  NwTrafficLayer *traffic;
  ScUnknown *valves;
  DsLayer *disasters;
  ScDirtBag *terrain;
  CtSchemeManager *scheme;
  u32 references, width, height;
};

class AuListener3D;

// The AUDIO listener controller stores world position, facing and the borrowed 3D listener.
class AuListenerController {
public:
  AuListenerController();
  // Original methods return AL=1; their private bool/BYTE nominal spelling is not established.
  u8 Init();
  u8 SetPosition(i32 x, i32 y, i32 z);
  u8 SetOrientation(i32 x, i32 y, i32 z);
  u8 SetZoom(i32 value);

private:
  friend class AuActorEffect;
  f32 positionX, positionY, positionZ, facingX, facingY, facingZ;
  i32 zoom;
  u8 flag28;
  AuListener3D *listener;
};

// The original AUDIO selection generator keeps two seeds and the independent Gaussian-cache fields.
class AuRandom {
public:
  explicit AuRandom(u32 base);
  void Seed(u32 base);
  u32 RandomUint32Uniform();
  u32 RandomUint32Uniform(u32 bound);

private:
  u32 current, second;
  i32 normalReady;
  f64 cachedNormal;
};

class AuFreshnessScheduler;
class AuFreshnessPlayer;
extern u8 AuForcedQuadrantEnabled;
extern u32 AuForcedQuadrant;
class AuBuildingAmbience;
class AuAudioEffect;
class AuAmbientChannel;
class AuInfo;
class AuMusicPlayer;
class AuSound;
class AuSoundCache;
class AuSoundDevice;
class AuCallbackTimer;
class ScMessageServer;
class ScLock;
class ScStopwatch;
class SpIsoViewInfo;

// These service accessors return borrowed views; the framework owns their lifetime.
AuSoundDevice *AuGetManagerSoundDevice();
ScMessageServer *AuGetMessageServer();
extern AuSoundDevice *AuManagerSoundDevice;
ScString *AuGetAudioSoundDirectory();
ScString *AuGetMusicDirectory();

// The AUDIO manager implements BoxX and a separate message-target identity. Unrecovered members remain opaque.
class AuAudioManager : public ApBoxX, public ScMessageTarget {
public:
  AuAudioManager();
  virtual ~AuAudioManager();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Initialization uses the recovered manager operations; native pointer-sized timer contexts remain unresolved.
  bool Init(ScString *configuration) override;
  bool Shutdown() override;
  i32 Event(i32 event, uptr first, uptr second, uptr third, uptr fourth) override;
  void Update(u32 argument) override;
  bool DoMessage(ScMessage *message) override;

private:
  friend class AuMusicPlayer;
  friend class AuStreamMusicSong;
  friend class AuBuildingAmbience;
  friend class AuAudioIni;
  friend class AuBuildingSounds;
  friend class AuSingleEffect;
  friend class AuActorEffect;
  friend class AuFadeEffect;
  friend class AuSparseEffect;
  friend class AuDisasterEffect;
  friend void SC3_CDECL AuLoadSingleSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadSparseSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadDisasterSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadFadingSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadActorSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadMusic(const ScStringView &key, const ScStringView &value, void *reader);
  // The recovered music-selection result retains its observed byte width; nominal bool/BYTE spelling is unresolved.
  u8 UpdateMusic();
  // These original private helpers have incidental or unspecified returns; their callers discard them.
  void StartTimer();
  void StopTimer();
  void StopLoop();
  void SetLoop(ScString *name, i32 looping, i32 volume, i32 cookie, u8 streaming);
  void GetScanBounds(i32 *left, i32 *right, i32 *top, i32 *bottom);
  void RefreshDisasterEffects();
  void HandleAmbienceEvent(i32 value, u32 subtype);
  void PlayCellEffect(i32 x, i32 z);
  void UpdateRadiation();
  void UpdateAmbienceChannels();
  // The installed predicate returns AL=0/1; its private bool/BYTE spelling remains unresolved.
  u8 IsAudibleCell(i32 x, i32 z);
  i32 SubwayCoverage();
  i32 PipeCoverage();
  i32 CellSoundId(i32 x, i32 z);
  i32 CellOverlaySoundId(i32 x, i32 z);
  i32 AudibleSafety();
  // Update the current view and its audible ambience selection.
  u8 UpdateView(SpIsoViewInfo *view);
  // Apply the current ambience mix to the recovered freshness players.
  u8 UpdateAmbienceMix();
  u32 references;
  u8 initialized;
  AuMusicPlayer *musicPlayer;
  u32 unknown20;
  i32 playbackState, savedPlaybackState;
  u8 flag32, flag33;
  i32 musicVolume, effectsVolume, ambienceVolume;
  u8 flag48;
  i32 zoomLoopVolume, buildingRandomMultiplier, trafficDensityMinimum, trafficDensityMultiplier, vehicleHonkDivisor;
  i32 vehicleMaximumDistance, vehicleMinimumDistance, buildingAmbienceAttenuation, buildingAmbienceMinimumZoom;
  i32 honkAttenuation, honkMinimumZoom, vehicleAttenuation, vehicleMinimumZoom, vehicleEffectsLevel;
  i32 vehicleEffectsPreset, vehicleTallBuildingThreshold, forceCrimeTempo, forceAmbienceQuadrant;
  u8 flag124, flag125, flag126;
  u32 unknown128;
  AuCallbackTimer *updateTimer;
  ScStopwatch *clock;
  AuFreshnessScheduler *scheduler;
  AuRandom random;
  u32 unknown168, unknown172;
  u8 flag176;
  ScLock *lock;
  AuListenerController listener;
  AuBuildingSounds buildingSounds;
  AuFreshnessPlayer *player264, *player268, *player272, *player276, *player280, *player284, *player288, *player292;
  AuFreshnessPlayer *player296, *player300, *player304;
  AuFreshnessPlayer *player308;
  AuFreshnessPlayer *player312, *player316, *player320, *player324, *player328, *player332, *player336, *player340;
  AuFreshnessPlayer *player344, *player348, *player352, *player356, *player360, *player364;
  // Sound creation supplies held references, released by the corresponding shutdown paths.
  AuSound *sound368, *loopSound, *geigerSound, *sound380;
  u32 loopCookie;
  AuBuildingAmbience *buildingAmbience;
  AuSoundCache *cache; // Retired disk-cache slot retained for the recovered object layout.
  AuInfo *audioInfo;
  AuAudioEffect *effects[512];
  i32 viewWorldX, viewWorldZ, viewCellX, viewCellZ;
  i32 viewRight, viewBottom, viewLeft, viewTop, cityWidth, cityHeight, viewRadius;
  i32 viewZoom, unknown2496, viewRotation, unknown2504, viewMode, unknown2512, unknown2516, unknown2520;
  i32 unknown2524, unknown2528, unknown2532;
  AuAmbientChannel *channels[15];
  i32 currentLandscape, visibleCells, unknown2604, minimumFadeVolume, musicStartDelay, unknown2616, unknown2620;
};

#endif
