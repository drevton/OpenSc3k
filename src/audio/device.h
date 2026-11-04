#ifndef SC3_AUDIO_DEVICE_H
#define SC3_AUDIO_DEVICE_H

#include <vector>

#include "audio/api.h"
#include "audio/local_radio.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/thread.h"

struct IDirectSound;
struct _DSCAPS;
struct _GUID;

struct AuSoundEntry {
  AuSoundEntry *next;
  u32 id;
  AuSound *sound;
};

// The device's original sound table owns nodes but does not retain the stored sounds.
struct AuSoundRegistry {
  AuSoundRegistry();
  ~AuSoundRegistry();
  std::vector<AuSoundEntry *> buckets;
  u32 count;
};

// A driver entry owns its copied GUID and the two concrete game strings.
struct AuDriverInfo {
  AuDriverInfo(const _GUID *id, const ScString &description, const ScString &module);
  AuDriverInfo(const AuDriverInfo &other);
  ~AuDriverInfo();
  _GUID *guid;
  ScString description, module;
};

// The installed device supplies both the sound interface and the system-service interface.
// Its construction, timer and DirectSound-backed operations remain under reconstruction.
class AuDevice : public AuSoundDevice, public ScSystemService {
public:
  AuDevice();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetInitFlag() override;
  u8 GetEnabledFlag() override;
  void SetEnabledFlag(u8 enabled) override;
  AuSound *GetPrimarySound() override;
  i32 GetMasterVolume() override;
  u8 SetMasterVolume(i32 volume) override;
  u8 SetSoundParameters(u32 bits, u32 rate, u32 channels) override;
  u8 SndRegister(AuSound *sound) override;
  u8 SndUnregister(i32 identifier, u8 release) override;
  u8 SndUnregister(AuSound *sound, u8 release) override;
  u8 SndUnregisterAll(u8 release) override;
  u8 IsSndRegistered(i32 identifier) override;
  u8 IsSndRegistered(AuSound *sound) override;
  AuSound *GetSndPtr(i32 identifier) override;
  AuSound3D *GetSndPtr3D(i32 identifier) override;
  u32 GetRegisteredSndCount() override;
  void EnableGarbageCollection(u32 interval) override;
  void DisableGarbageCollection() override;
  void GarbageCollectNow() override;
  AuSound *SndCreate() override;
  AuSound *SndCreate(ScStringView *name, AuStreamingType streaming) override;
  u8 SndPlay(i32 identifier, i32 volume, u8 parameter) override;
  AuSound3D *SndCreate3D() override;
  AuSound3D *SndCreate3D(ScStringView *name, AuStreamingType streaming, u8 parameter) override;
  AuListener3D *SndCreate3DListener() override;
  u8 UseSnd3DListener(AuListener3D *listener) override;
  u8 ReleaseSnd3DListener() override;
  AuListener3D *GetSnd3DListener() override;
  AuEffectsBus *GetEffectsBus() override;
  u8 SetEffectsPreset(i32 preset) override;
  i32 GetEffectsPreset() override;
  u8 GetUseDriverSupport() override;
  void SetUseDriverSupport(u8 enabled) override;
  u32 GetDriverInfoCount() override;
  u8 GetDriverInfo(ScStringView *first, ScStringView *second, u32 index) override;
  u8 GetDriverInfoCurrent(ScStringView *first, ScStringView *second) override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;
  virtual ~AuDevice();
  // The stored AL is known; its private nominal return type is not yet established.
  virtual u8 SetInitFlag(u8 value);
  virtual u8 ProbeDriver();

private:
  friend class ApAnimationPlayer;
  friend class AuSample;
  static int SC3_STDCALL EnumerateDriver(_GUID *id, const char *description, const char *module, void *context);
  int AddDriver(_GUID *id, const char *description, const char *module);
  AuSoundEntry *Find(u32 identifier);
  void Reserve(u32 requested);
  void Erase(AuSoundEntry *entry);
  u32 serviceId;
  u8 enabled, initialized, driverSupport;
  IDirectSound *directSound;
  _DSCAPS *capabilities;
  AuSound *primary;
  AuListener3D *listener;
  u32 bits, rate, channels;
  AuEffectsBus *effectsBus;
  i32 effectsPreset;
  AuSoundRegistry sounds;
  AuSoundEntry *collectionCursor = nullptr;
  ScStopwatch timer;
  i32 collectionInterval;
  ScLock lock;
  std::vector<AuDriverInfo> drivers;
  u32 references;
  u32 sdlDevice = 0;
  bool sdlAudioInitialized = false;
};

struct IDirectSound3DBuffer;
struct IKsPropertySet;

// The original 12-byte effects object caches an EAX property interface and its supported properties.
class AuEffectsBus {
public:
  AuEffectsBus();
  u8 Initialize(IDirectSound3DBuffer *buffer);
  u8 SetPreset(i32 preset);

private:
  IKsPropertySet *properties;
  u8 environmentSupported, sourceSupported, modeSupported;
  u32 mode;
};

u8 SC3_STDCALL AuSetEffectsLevel(IDirectSound3DBuffer *buffer, f32 level);

struct IDirectSound3DListener;

// The original listener retains cached position, orientation and distance state beside its DirectSound interface.
class AuListener final : public AuListener3D {
public:
  AuListener();
  ~AuListener();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void GetPosition(f32 *x, f32 *y, f32 *z) override;
  bool SetPosition(f32 x, f32 y, f32 z) override;
  void GetVelocity(f32 *x, f32 *y, f32 *z) override;
  bool SetVelocity(f32 x, f32 y, f32 z) override;
  void GetOrientation(f32 *fx, f32 *fy, f32 *fz, f32 *ux, f32 *uy, f32 *uz) override;
  bool SetOrientation(f32 fx, f32 fy, f32 fz, f32 ux, f32 uy, f32 uz) override;
  void GetAll(
      f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *fx, f32 *fy, f32 *fz, f32 *ux, f32 *uy, f32 *uz) override;
  bool SetAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 fx, f32 fy, f32 fz, f32 ux, f32 uy, f32 uz) override;
  void GetDopplerFactor(f32 *factor) override;
  bool SetDopplerFactor(f32 factor) override;
  void GetRolloffFactor(f32 *factor) override;
  bool SetRolloffFactor(f32 factor) override;
  void GetDistanceFactor(f32 *factor) override;
  void GetDistanceFactor(AuDistanceUnits *units, f32 *factor) override;
  bool SetDistanceFactor(AuDistanceUnits units, f32 factor) override;

private:
  friend class AuSpatialSample;
  u32 references;
  AuSoundDevice *device;
  AuDistanceUnits distanceUnit;
  f32 distance, position[3], front[3], top[3];
  IDirectSound3DListener *listener;
  u32 driverSupport;
};

class AuSoundDevice;
class AuListener3D;
struct IDirectSound;
struct IDirectSoundBuffer;

// Actual main sound DLL director with the separately registered sound-class child.
class AuDirector : public ScDllDirector {
public:
  AuDirector();
  ~AuDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

// The child owns the original single device constructor registration and common registry-start operation.
class AuSoundDirector : public ScDllDirector {
public:
  AuSoundDirector();
  ~AuSoundDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

AuDirector *AuGetDirector();
AuSoundDirector *AuGetSoundDirector();
ScFramework *AuGetFrameWork();
AuSoundDevice *AuGetSoundDevice();
extern IDirectSound *AuDirectSound;
extern IDirectSoundBuffer *AuPrimaryBuffer;
extern AuListener3D *AuCurrentListener;
extern u32 AuCreatedBuffers;
ScUnknown *AuCreateDevice();

class AuWaveReader;

// Original 56-byte streaming state, including the reader, segment cursors and multimedia timer flags.
struct AuStreamingState {
  AuStreamingState();
  AuWaveReader *reader;
  u32 bufferSize, segmentSize, nextOffset, previousCursor, advancedBytes, stopped, ended, trailingSegments;
  u32 timerId, timerActive, playing, repeatBuffer, busy;
};

#endif
