#ifndef SC3_AUDIO_API_H
#define SC3_AUDIO_API_H

#include "common/api.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Effect slots named by the installed AUDIO.INI sound definitions.
enum AuEffectId : i32 {
  kAuEffectUiZoomIn = 1,
  kAuEffectUiZoomOut = 2,
  kAuEffectUiRotate = 4,
  kAuEffectUiButtonClick = 12,
  kAuEffectUiCheckboxClick = 13,
  kAuEffectUiLayTrees = 17,
  kAuEffectUiError = 30,
  kAuEffectUiClickOnNewsticker = 47,
  kAuEffectGoToDisaster = 55,
  kAuEffectCloseWindow = 56,
  kAuEffectDisasterPremonitionTremor = 57,
  kAuEffectDisasterTornado = 60,
  kAuEffectDisasterEarthquake = 61,
  kAuEffectDisasterFire = 62,
  kAuEffectDisasterRiot = 63,
  kAuEffectDisasterUfo = 74,
  kAuEffectDisasterUfoSuckUpPeople = 75,
  kAuEffectUfoVaporizeBuilding = 77,
  kAuEffectUfoMakeCropCircle = 78,
  kAuEffectUfoExplodes = 79,
  kAuEffectUfoFireLaserBomb = 80,
  kAuEffectZoom4Loop = 81,
  kAuEffectUndergroundLoop = 94,
  kAuEffectUiButtonClickSmall = 146,
  kAuEffectThunder = 170,
  kAuEffectDisasterToxicCloud = 175,
  kAuEffectDisasterWhirlpool = 229,
  kAuEffectDisasterToxicRain = 231,
  kAuEffectDisasterLocustSwarm = 233,
  kAuEffectDisasterLocustsEat = 235,
  kAuEffectDisasterSpaceJunk = 237,
  kAuEffectWhirlpoolDestroysBridge = 239,
  kAuEffectWhirlpoolDestroysMarina = 240,
  kAuEffectLocustsDie = 242,
  kAuEffectSpaceJunkHits = 243,
  kAuEffectSpaceJunkShakesScreen = 244,
  kAuEffectUiCamera = 245,
  kAuEffectUiCropDispatch = 246,
  kAuEffectSpaceJunkHitsWater = 250,
  kAuEffectWhirlpoolSucksUpBoat = 251,
  kAuEffectLocustFlyBy = 254,
};

// Audio-manager selectors use AUDIO.INI event/effect labels and verified installed callers; slot names remain provisional.
// Payload words retain each operation's original separate arguments.
enum AuAudioEvent : u32 {
  kAuEventPlayEffect = 0x00000001,
  kAuEventEffectSlot52 = 2,
  kAuEventEffectSlot48 = 3,
  kAuEventEffectSlot84 = 4,
  kAuEventEffectSlot56 = 5,
  kAuEventEffectSlot60 = 6,
  kAuEventLoadEffect = 0x00000008,
  kAuEventUnloadEffect = 0x00000009,
  kAuEventButtonClick = 12,
  kAuEventPlayBuildingSound = 0x000000c8,
  kAuEventPlayCellEffect = 0x000000ca,
  kAuEventPlayCellInterfaceSound = 0x000000cb,
  kAuEventSuckUpPerson = 207,
  kAuEventEarlyWarningSiren = 209,
  kAuEventStartDisaster = 213,  // Installed handler plays effect 102, labelled StartDisaster in AUDIO.INI.
  kAuEventAmbience = 0x000000da,
  kAuEventSetEffectsEnabled = 1000,
  kAuEventSetMusicEnabled = 1001,
  kAuEventSetMusicVolume = 0x000003ea,
  kAuEventSetEffectsVolume = 0x000003eb,
  kAuEventSetAmbienceVolume = 0x000003ec,
  kAuEventSetMuteAll = 1007,
  kAuEventSetDirectSound3D = 1008,
  kAuEventSetSongEnabled = 0x000003f4,
  kAuEventPlaySong = 0x000003f5,
  kAuEventStopSong = 0x000003f6,
  kAuEventBeginSongEnumeration = 0x000003f7,
  kAuEventNextSong = 0x000003f8,
  kAuEventCurrentSong = 0x000003f9,
  kAuEventMoveMusicVolumeSlider = 1018,
  kAuEventMoveAmbienceVolumeSlider = 1019,
  kAuEventMoveEffectsVolumeSlider = 1020,
  kAuEventUpdateView = 0x00000400,
  kAuEventGameSpeedChanged = 1025,
  kAuEventTrafficDensityChanged = 1026,
  kAuEventCityPopulationChanged = 1027,
  kAuEventMainMenu = 1037,
  kAuEventAttachCityInfo = 0x0000040e,
  kAuEventDetachCityInfo = 0x0000040f,
  kAuEventCityCreation = 1042,
  kAuEventCityReady = 1043,
  kAuEventCreditsOpen = 1046,
  kAuEventCreditsEndReached = 1047,
  kAuEventCreditsDismissed = 1048,
  kAuEventTerrainEditor = 1049,
  kAuEventPlaySong15 = 1050,
  kAuEventBeginFileSelection = 1051,
  kAuEventCancelFileSelection = 1052,
  kAuEventDisasterEnded = 1057,
};

class CtCity;

// Retail cISC3AudioInfo362C derives from the actual message-target base and starts its own methods at16.
// T_UCHAR results keep their raw byte width; nominal bool/BYTE spelling still needs concrete-producer declarations.
class AuInfo : public ScMessageTarget {
public:
  virtual u8 Init(CtCity *city) = 0;
  virtual u8 Shutdown() = 0;
  virtual i32 CellCountX() = 0;
  virtual i32 CellCountZ() = 0;
  virtual u8 CellHasOccupant(i32 x, i32 z) = 0;
  virtual u8 CellIsAbandoned(i32 x, i32 z) = 0;
  virtual u8 CellIsDestroyed(i32 x, i32 z) = 0;
  virtual u8 CellIsWater(i32 x, i32 z) = 0;
  virtual u8 CellHasRoad(i32 x, i32 z) = 0;
  virtual u8 CellHasHighway(i32 x, i32 z) = 0;
  virtual u8 CellHasRail(i32 x, i32 z) = 0;
  virtual u8 CellHasSubway(i32 x, i32 z) = 0;
  virtual u8 CellHasPipe(i32 x, i32 z) = 0;
  virtual u8 CellHasPower(i32 x, i32 z) = 0;
  virtual u8 CellHasTunnel(i32 x, i32 z) = 0;
  virtual u8 CellHasTree(i32 x, i32 z) = 0;
  virtual u8 CellIsGarbage(i32 x, i32 z) = 0;
  virtual u8 CellHasSpriteInSurfaceView(i32 x, i32 z) = 0;
  virtual u8 CellHasSpriteInPipeView(i32 x, i32 z) = 0;
  virtual u8 CellHasSpriteInSubwayView(i32 x, i32 z) = 0;
  virtual i32 GetCellAmbienceType(i32 x, i32 z, i32 *first, i32 *second) = 0;
  virtual i32 GetCellZoneType(i32 x, i32 z) = 0;
  virtual i32 GetCellPopulation(i32 x, i32 z) = 0;
  virtual i32 GetCellLandValue(i32 x, i32 z) = 0;
  virtual i32 GetCellCrimeValue(i32 x, i32 z) = 0;
  virtual i32 GetCellRadiationValue(i32 x, i32 z) = 0;
  virtual i32 GetCellAura(i32 x, i32 z) = 0;
  virtual i32 GetCityMayoralRating() = 0;
  virtual i32 GetCityPopulation() = 0;
  virtual i32 GetTrafficDensity(i32 x, i32 z) = 0;
  virtual i32 GetCellSpriteHeight(i32 x, i32 z) = 0;
  virtual u8 CellIsPowered(i32 x, i32 z) = 0;
  virtual u8 CellIsUnpoweredBuilding(i32 x, i32 z) = 0;
  virtual i32 GetBuildingSoundId(i32 x, i32 z) = 0;
  virtual i32 GetCellInterfaceSoundId(i32 x, i32 z) = 0;
  virtual u8 IsInEditModeCity() = 0;
  virtual i32 DisasterCount() = 0;
  virtual i32 TerrainHeight(i32 x, i32 z) = 0;
  virtual u8 GetCityDate(u32 *first, u32 *second, u32 *third) = 0;
  virtual u32 CurrentFloraSet() = 0;
  virtual u32 CurrentBuildingSet() = 0;
  virtual u32 CurrentLandScape() = 0;
};

class ScStringView;
class AuSound;
class AuSound3D;
class AuListener3D;
class AuEffectsBus;
enum class AuStreamingType : i32;

// Retail cIGZSndSys3075/3076 supplies the complete interface through slot152, without a virtual destructor.
// T_UCHAR entries retain raw bytes: bool versus BYTE nominal spelling remains unresolved without producer evidence.
class AuSoundDevice : public ScUnknown {
public:
  virtual u8 GetInitFlag() = 0;
  virtual u8 GetEnabledFlag() = 0;
  virtual void SetEnabledFlag(u8 enabled) = 0;
  virtual AuSound *GetPrimarySound() = 0;
  virtual i32 GetMasterVolume() = 0;
  virtual u8 SetMasterVolume(i32 volume) = 0;
  virtual u8 SetSoundParameters(u32 first, u32 second, u32 third) = 0;
  virtual u8 SndRegister(AuSound *sound) = 0;
  // MSVC reverses each overload group; the installed ID-removal consumer calls slot48.
  virtual u8 SndUnregister(i32 identifier, u8 release) = 0;
  virtual u8 SndUnregister(AuSound *sound, u8 release) = 0;
  virtual u8 SndUnregisterAll(u8 release) = 0;
  virtual u8 IsSndRegistered(i32 identifier) = 0;
  virtual u8 IsSndRegistered(AuSound *sound) = 0;
  virtual AuSound *GetSndPtr(i32 identifier) = 0;
  virtual AuSound3D *GetSndPtr3D(i32 identifier) = 0;
  virtual u32 GetRegisteredSndCount() = 0;
  virtual void EnableGarbageCollection(u32 interval) = 0;
  virtual void DisableGarbageCollection() = 0;
  virtual void GarbageCollectNow() = 0;
  virtual AuSound *SndCreate() = 0;
  virtual AuSound *SndCreate(ScStringView *name, AuStreamingType streaming) = 0;
  virtual u8 SndPlay(i32 identifier, i32 volume, u8 parameter) = 0;
  virtual AuSound3D *SndCreate3D() = 0;
  virtual AuSound3D *SndCreate3D(ScStringView *name, AuStreamingType streaming, u8 parameter) = 0;
  virtual AuListener3D *SndCreate3DListener() = 0;
  virtual u8 UseSnd3DListener(AuListener3D *listener) = 0;
  virtual u8 ReleaseSnd3DListener() = 0;
  virtual AuListener3D *GetSnd3DListener() = 0;
  virtual AuEffectsBus *GetEffectsBus() = 0;
  virtual u8 SetEffectsPreset(i32 preset) = 0;
  virtual i32 GetEffectsPreset() = 0;
  virtual u8 GetUseDriverSupport() = 0;
  virtual void SetUseDriverSupport(u8 enabled) = 0;
  virtual u32 GetDriverInfoCount() = 0;
  virtual u8 GetDriverInfo(ScStringView *first, ScStringView *second, u32 index) = 0;
  virtual u8 GetDriverInfoCurrent(ScStringView *first, ScStringView *second) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cIGZRadio.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original cIGZRadio order, with retail A477 settings and typed sound return.
class AuSound;

class AuRadio : public ScUnknown {

public:
  enum class AuRadioOption : i32 { DJ = 0, Commercials = 1, Jingle = 2, English = 3 };

  // Original RadioSettings is a public28-byte struct. Read/write leave auto_mute untouched.
  struct AuRadioSettings {
    u32 volume, is_on, current_station, dj, commercials, auto_mute, english_fallback;
  };

  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;

  virtual bool OnIdle(u32) = 0;

  virtual AuSound *GetRadioSound() = 0;

  virtual bool GetCurrentStation(u32 *) = 0;
  virtual bool GetCurrentStation(char *) = 0;

  virtual AuRadioStation *GetStation(u32) = 0;
  virtual bool SetStation(u32) = 0;
  virtual AuRadioStation *GetStation(char const *) = 0;
  virtual bool SetStation(char const *) = 0;

  virtual i32 GetVolume() = 0;
  virtual bool SetVolume(i32) = 0;

  virtual u32 GetStationCount() = 0;
  virtual bool IsStationAvailable(char const *) = 0;
  virtual bool AddStation(AuRadioStation *) = 0;
  virtual bool RemoveStation(AuRadioStation *, bool) = 0;
  virtual bool RemoveStation(char const *, bool) = 0;
  virtual bool RemoveAllStations(bool) = 0;

  virtual bool TurnOn() = 0;
  virtual bool TurnOff() = 0;
  virtual bool IsOn() = 0;

  virtual void Mute() = 0;
  virtual void UnMute() = 0;

  virtual bool GetOption(AuRadioOption radioOption) = 0;
  virtual void SetOption(bool, AuRadioOption radioOption) = 0;

  virtual bool GetRadioDirectory(ScStringView *) = 0;
  virtual void SetRadioDirectory(ScStringView const &) = 0;

  virtual void ReadSettings(AuRadio::AuRadioSettings *) = 0;
  virtual void WriteSettings(AuRadio::AuRadioSettings *) = 0;
};

static_assert(sizeof(AuRadio::AuRadioSettings) == 28);

// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cIGZRadioStation.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class AuRadioStation : public ScUnknown {

public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;

  virtual bool OnIdle() = 0;

  virtual bool SetRadio(AuRadio *) = 0;
  virtual void SetStation() = 0;
  virtual void UnsetStation() = 0;

  virtual void GetStationCallSign(char *) = 0; // Parameter is an array with a length of 4.
  virtual void GetStationDescription(ScStringView *) = 0;

  virtual bool IsOnTheAir() = 0;
};

class ScStringView;
struct tWAVEFORMATEX;
enum class AuSoundType : i32;
enum class AuStreamingType : i32;
enum class AuSoundDuplication : i32;
enum class AuSoundStatus : i32;
// Retail CodeView type 0x11d8 is a cdecl pointer to void callback(u32); installed Stop invokes it directly.
using AuSoundCompletion = void (*)(u32);

// Original cIGZSnd3012 full virtual order through152. Raw-byte results retain unresolved bool/BYTE nominal spelling.
// The sound producer remains explicit; this interface supplies no default implementations.
class AuSound : public ScUnknown {
public:
  virtual u8 Init() = 0;
  virtual u8 Shutdown() = 0;
  // Installed station consumers call the String overload at24; MSVC reverses this overload group.
  virtual u8 SetSoundFile(const ScStringView &name, AuStreamingType type) = 0;
  virtual u8 SetSoundFile(const char *name, u32 size, AuStreamingType type) = 0;
  virtual u8 Play(i32 volume, u8 loop, AuSoundDuplication duplication) = 0;
  virtual u8 IsPlaying() = 0;
  virtual u8 Stop() = 0;
  virtual u8 StopAllInstances() = 0;
  virtual AuSoundType GetSoundType() = 0;
  virtual AuSoundStatus GetStatus() = 0;
  virtual u32 GetSoundStreamMinimumFileSize() = 0;
  virtual u8 SetSoundStreamMinimumFileSize(u32 size) = 0;
  virtual u8 SetSoundDefaultStreamingType(AuStreamingType type) = 0;
  virtual u32 GetUseCount() = 0;
  virtual i32 GetID() = 0;
  virtual void SetID(i32 identifier) = 0;
  virtual tWAVEFORMATEX *GetWaveFormat() = 0;
  virtual u8 IsLoaded() = 0;
  virtual u8 Load() = 0;
  virtual u8 Unload() = 0;
  virtual u8 SetCompletionNotification(AuSoundCompletion callback, u32 message) = 0;
  virtual u8 StopCompletionNotification() = 0;
  virtual u32 EstimateRemainingPlayTime() = 0;
  virtual i32 GetVolume() = 0;
  virtual u8 SetVolume(i32 volume) = 0;
  virtual u8 FadeVolume(i32 first, i32 second, u32 duration, u8 option) = 0;
  virtual i32 GetPan() = 0;
  virtual u8 SetPan(i32 pan) = 0;
  virtual i32 GetFrequency() = 0;
  virtual u8 SetFrequency(i32 frequency) = 0;
  virtual i32 GetOriginalFrequency() = 0;
  virtual u32 GetPosition() = 0;
  virtual u8 SetPosition(u32 position) = 0;
  virtual i32 GetSoundPriority() = 0;
  virtual void SetSoundPriority(i32 priority) = 0;
  virtual void SetStreamingBufferSizeMultiplier(u32 multiplier) = 0;
};

// Retail cIGZSnd3D3039 extends the sound interface at slots156..200; raw byte returns retain unknown bool/BYTE spelling.
class AuSound3D : public AuSound {
public:
  virtual void GetPosition(f32 *x, f32 *y, f32 *z) = 0;
  virtual u8 SetPosition(f32 x, f32 y, f32 z) = 0;
  virtual void GetVelocity(f32 *x, f32 *y, f32 *z) = 0;
  virtual u8 SetVelocity(f32 x, f32 y, f32 z) = 0;
  virtual void GetAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz) = 0;
  virtual u8 SetAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz) = 0;
  virtual void SetPower(u32 power) = 0;
  virtual void GetMinimumDistance(f32 *distance) = 0;
  virtual u8 SetMinimumDistance(f32 distance) = 0;
  virtual void GetMaximumDistance(f32 *distance) = 0;
  virtual u8 SetMaximumDistance(f32 distance) = 0;
  virtual u8 SetEffectsLevel(i32 level) = 0;
};

#endif
