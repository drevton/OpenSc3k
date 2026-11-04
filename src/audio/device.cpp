#define WIN32_LEAN_AND_MEAN
#include "audio/device.h"

// The Windows SDK sound header requires multimedia format declarations first.
#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
// clang-format on
#undef GetObject
#undef PlaySound
#endif

#include <ctype.h>
#include <cassert>
#include <cmath>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <new>
#include <set>

#include "audio/api.h"
#include "audio/local_radio.h"
#include "audio/sample.h"
#include "audio/spatial_sample.h"
#include "audio/wave_reader.h"
#include "common/api.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// The configurable stream-size threshold starts at the original minimum byte count.
enum AuStreamSizeDefault : u32 {
  kAuDefaultMinimumStreamBytes = 0x40000,
};

// Streaming buffer sizing retains the lower twenty-five bits of the rate-derived segment size.
enum AuStreamBufferMask : u32 {
  kAuStreamSegmentSizeMask = 0x1ffffff,
};

// Spatial fallback clears CTRL3D, CTRLPAN and MUTE3DATMAXDISTANCE before enabling pan; bits follow the Windows SDK.
enum AuSpatialFallbackMask : u32 {
  kAuWithoutSpatialAndPanCapabilities = 0xfffdffaf,
};

// Sample playback frequency is clamped to the original upper bound.
enum AuSampleFrequencyLimit : i32 {
  kAuSampleMaximumFrequency = 0x186a0,
};

// A looping sample has no finite estimated remaining playback interval.
enum AuSampleRemainingTime : u32 {
  kAuSampleIndefiniteRemainingTime = 0xffffffff,
};

// The service returns its original fixed framework scheduling priority.
enum AuDeviceServiceOrder : i32 {
  kAuDeviceServicePriority = 0x7a120,
};

#if !defined(_WIN32)
// Windows GUID layout at the external driver-enumeration boundary.
struct _GUID {
  u32 Data1;
  u16 Data2, Data3;
  u8 Data4[8];
};
static_assert(sizeof(_GUID) == 16);
#endif

// Keep the device's recursive lock held for the same scope as the original registry guard.
class AuRegistryGuard {
public:
  // Acquire the device lock before accessing its sound table.
  explicit AuRegistryGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the lock on normal return and exception unwinding.
  ~AuRegistryGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

AuSoundRegistry::AuSoundRegistry() : buckets(193, nullptr), count(0) {
}

// Destroy the remaining nodes without releasing the borrowed sound interfaces.
AuSoundRegistry::~AuSoundRegistry() {
  for (AuSoundEntry *head : buckets) {
    while (head) {
      AuSoundEntry *next = head->next;
      delete head;
      head = next;
    }
  }
}

// Copy both driver strings and separately own a copy of its optional GUID.
AuDriverInfo::AuDriverInfo(const AuDriverInfo &other)
    : guid(nullptr), description(other.description), module(other.module) {
  if (other.guid)
    guid = new _GUID(*other.guid);
}

// Copy a driver's optional GUID and both description strings into an owned enumeration record.
AuDriverInfo::AuDriverInfo(const _GUID *id, const ScString &first, const ScString &second)
    : guid(nullptr), description(first), module(second) {
  if (id)
    guid = new _GUID(*id);
}

int SC3_STDCALL AuDevice::EnumerateDriver(_GUID *id, const char *description, const char *module, void *context) {
  return static_cast<AuDevice *>(context)->AddDriver(id, description, module);
}

// Copy one enumerated driver's GUID and strings into the device's owned driver list.
int AuDevice::AddDriver(_GUID *id, const char *description, const char *module) {
  ScString first(description), second(module);
  AuDriverInfo info(id, first, second);
  drivers.push_back(info);
  return 1;
}

AuDriverInfo::~AuDriverInfo() {
  delete guid;
}

// Initialize the device's two interfaces and owned registry, profiler, lock and driver state.
AuDevice::AuDevice()
    : serviceId(kAuSoundDeviceServiceId), enabled(1), initialized(0), driverSupport(1), directSound(nullptr),
      capabilities(nullptr), primary(nullptr), listener(nullptr), bits(16), rate(22050), channels(2),
      effectsBus(nullptr), effectsPreset(0), timer(ScStopwatch::Milliseconds, 0), collectionInterval(-1),
      references(0) {
}

// The driver collection, lock, profiler and registry are destroyed in that order.
AuDevice::~AuDevice() {
  Shutdown();
  capabilities = nullptr;
  initialized = 0;
}

u8 AuDevice::SetInitFlag(u8 value) {
  initialized = value;
  return value;
}

// Create a temporary 3D sound buffer and ask the effects object to probe its property support.
u8 AuDevice::ProbeDriver() {
  return 0;
}

// Save the requested PCM parameters, update the primary sound's format and apply it to its DirectSound buffer.
u8 AuDevice::SetSoundParameters(u32 newBits, u32 newRate, u32 newChannels) {
  bits = newBits;
  rate = newRate;
  channels = newChannels;
  AuSample *sound = static_cast<AuSample *>(primary);
  sound->format.wFormatTag = 1;
  sound->format.nChannels = static_cast<u16>(channels);
  sound->format.nSamplesPerSec = rate;
  sound->format.nAvgBytesPerSec = channels * rate * (bits >> 3);
  sound->format.nBlockAlign = static_cast<u16>(bits >> 2);
  sound->format.wBitsPerSample = static_cast<u16>(bits);
  sound->format.cbSize = 0;
  SDL_AudioSpec spec{};
  spec.channels = static_cast<int>(channels);
  spec.freq = static_cast<int>(rate);
  spec.format = bits == 8 ? SDL_AUDIO_U8 : bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_UNKNOWN;
  if (!spec.format)
    return 0;
  SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, nullptr);
  if (!stream)
    return 0;
  SDL_DestroyAudioStream(stream);
  return 1;
}

void AuDevice::EnableGarbageCollection(u32 interval) {
  collectionInterval = static_cast<i32>(interval < 5000 ? 5000 : interval);
  timer.Start();
}

void AuDevice::DisableGarbageCollection() {
  collectionInterval = -1;
  timer.Stop();
}

// Restart the collection timer and inspect up to ten sounds in the original bucket order.
// The safe initial cursor excludes the original indeterminate first-cursor read.
void AuDevice::GarbageCollectNow() {
  AuRegistryGuard guard(&lock);
  timer.Restart();
  u32 remaining = sounds.count < 10 ? sounds.count : 10;
  if (!remaining)
    return;
  AuSoundEntry *entry = collectionCursor;
  if (entry)
    entry = Find(entry->id);
  if (!entry) {
    for (AuSoundEntry *head : sounds.buckets) {
      if (head) {
        entry = head;
        break;
      }
    }
  }
  collectionCursor = entry;
  while (remaining--) {
    entry = collectionCursor;
    assert(entry);
    AuSound *sound = entry->sound;
    if (!sound->IsPlaying()) {
      if (sound->GetSoundPriority() < 8)
        sound->Unload();
    }
    entry = collectionCursor;
    assert(entry);
    AuSoundEntry *next = entry->next;
    if (!next) {
      usize bucket = entry->id % sounds.buckets.size() + 1;
      while (bucket < sounds.buckets.size() && !sounds.buckets[bucket])
        ++bucket;
      if (bucket < sounds.buckets.size())
        next = sounds.buckets[bucket];
    }
    if (!next) {
      for (AuSoundEntry *head : sounds.buckets) {
        if (head) {
          next = head;
          break;
        }
      }
    }
    collectionCursor = next;
    entry = next;
  }
}

// Construct an unnamed sound, initialize its ID and retain the returned interface.
AuSound *AuDevice::SndCreate() {
  AuSample *sound = new AuSample();
  sound->Init();
  sound->AddRef();
  return sound;
}

// Assign the requested file before initializing and retaining the new sound interface.
AuSound *AuDevice::SndCreate(ScStringView *name, AuStreamingType streaming) {
  AuSample *sound = new AuSample();
  sound->SetSoundFile(*name, streaming);
  sound->Init();
  sound->AddRef();
  return sound;
}

// Construct the spatial producer and initialize and retain its secondary 3D interface.
AuSound3D *AuDevice::SndCreate3D() {
  AuSpatialSample *sound = new AuSpatialSample();
  AuSound3D *view = static_cast<AuSound3D *>(sound);
  view->Init();
  view->AddRef();
  return view;
}

// Construct the named spatial producer, assign its file, then initialize and retain the primary sound.
AuSound3D *AuDevice::SndCreate3D(ScStringView *name, AuStreamingType streaming, u8 option) {
  AuSpatialSample *sound = new AuSpatialSample(*name, streaming, option);
  sound->SetSoundFile(*name, streaming);
  sound->Init();
  sound->AddRef();
  return static_cast<AuSound3D *>(sound);
}

// Construct and retain the device's listener once, publishing it in the original module-global slot.
AuListener3D *AuDevice::SndCreate3DListener() {
  if (!listener) {
    listener = new AuListener();
    listener->Init();
    listener->AddRef();
    AuCurrentListener = listener;
  }
  return listener;
}

// Apply a preset through the live effects object and remember it only when that operation succeeds.
u8 AuDevice::SetEffectsPreset(i32 preset) {
  if (!effectsBus)
    return 0;
  u8 result = effectsBus->SetPreset(preset);
  if (result)
    effectsPreset = preset;
  return result;
}

u32 AuDevice::GetDriverInfoCount() {
  return static_cast<u32>(drivers.size());
}

// Copy the driver's module and description into the two outputs, in that order.
// An out-of-range index clears both outputs; reentrant removal excludes the original out-of-range read.
u8 AuDevice::GetDriverInfo(ScStringView *first, ScStringView *second, u32 index) {
  if (index >= drivers.size()) {
    first->FromChar("");
    second->FromChar("");
    return 0;
  }
  first->FromChar(drivers[index].module.begin, drivers[index].module.ScString::Strlen());
  assert(index < drivers.size());
  second->FromChar(drivers[index].description.begin, drivers[index].description.ScString::Strlen());
  return 1;
}

// Select the non-primary entry for the two-driver case, or the first null/default GUID otherwise.
// Forward the chosen index through the virtual driver-info operation.
u8 AuDevice::GetDriverInfoCurrent(ScStringView *first, ScStringView *second) {
  const char *driver = SDL_GetCurrentAudioDriver();
  const char *name = sdlDevice ? SDL_GetAudioDeviceName(sdlDevice) : nullptr;
  if (!driver || !name)
    return 0;
  first->FromChar(driver);
  second->FromChar(name);
  return 1;
}

// Initialize DirectSound and the primary producer in original order; a missing host interface has no defined handle.
bool AuDevice::Init() {
  if (!GetEnabledFlag() || GetInitFlag())
    return true;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    return false;
  sdlAudioInitialized = true;
  int count = 0;
  SDL_AudioDeviceID *devices = SDL_GetAudioPlaybackDevices(&count);
  if (!devices) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlAudioInitialized = false;
    return false;
  }
  const char *driver = SDL_GetCurrentAudioDriver();
  for (int index = 0; index < count; ++index) {
    const char *name = SDL_GetAudioDeviceName(devices[index]);
    if (name)
      AddDriver(nullptr, name, driver ? driver : "");
  }
  SDL_free(devices);
  sdlDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (!sdlDevice || !SDL_ResumeAudioDevice(sdlDevice)) {
    if (sdlDevice)
      SDL_CloseAudioDevice(sdlDevice);
    sdlDevice = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlAudioInitialized = false;
    return false;
  }
  AuSample *sound = new AuSample();
  primary = sound;
  sound->format.wFormatTag = 1;
  sound->format.nChannels = static_cast<u16>(channels);
  sound->format.nSamplesPerSec = rate;
  sound->format.nAvgBytesPerSec = rate * channels * (bits >> 3);
  sound->format.nBlockAlign = static_cast<u16>(bits >> 2);
  sound->format.wBitsPerSample = static_cast<u16>(bits);
  sound->format.cbSize = 0;
  driverSupport = 0;
  effectsPreset = 0;
  SetInitFlag(1);
  return true;
}

// Collection uses the existing timer and remains disabled while its interval is minus one.
bool AuDevice::OnIdle(u32) {
  if (collectionInterval != -1 && timer.GetElapsedTime() > static_cast<u32>(collectionInterval))
    GarbageCollectNow();
  return true;
}

// Find a sound entry by the full unsigned bits of its identifier.
AuSoundEntry *AuDevice::Find(u32 identifier) {
  AuSoundEntry *entry = sounds.buckets[identifier % sounds.buckets.size()];
  while (entry && entry->id != identifier)
    entry = entry->next;
  return entry;
}

// Grow the original prime-sized bucket table, moving each old chain in head-insertion order.
void AuDevice::Reserve(u32 requested) {
  static const u32 kAuSoundBucketPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
      196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189,
      805306457, 1610612741, 3221225473u, 4294967291u};
  if (requested <= sounds.buckets.size())
    return;
  u32 size = kAuSoundBucketPrimes[sizeof(kAuSoundBucketPrimes) / sizeof(kAuSoundBucketPrimes[0]) - 1];
  for (u32 prime : kAuSoundBucketPrimes) {
    if (prime >= requested) {
      size = prime;
      break;
    }
  }
  std::vector<AuSoundEntry *> next(size, nullptr);
  for (AuSoundEntry *head : sounds.buckets) {
    while (head) {
      AuSoundEntry *entry = head;
      head = head->next;
      u32 index = entry->id % size;
      entry->next = next[index];
      next[index] = entry;
    }
  }
  sounds.buckets.swap(next);
}

// Remove a matching entry from its current bucket and release its node storage.
void AuDevice::Erase(AuSoundEntry *entry) {
  AuSoundEntry **link = &sounds.buckets[entry->id % sounds.buckets.size()];
  while (*link && *link != entry)
    link = &(*link)->next;
  if (!*link)
    return;
  *link = entry->next;
  delete entry;
  --sounds.count;
}

// Publish either the primary sound interface or its secondary service interface, retaining the shared object.
bool AuDevice::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuSoundDevice);
  HANDLE_QUERY_INTERFACE(IID_AuSoundDevice, AuSoundDevice);
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuDevice::AddRef() {
  return ++references;
}

// Drop a shared interface reference, deleting the device when its count is at most one.
u32 AuDevice::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

u8 AuDevice::GetInitFlag() {
  return initialized;
}

u8 AuDevice::GetEnabledFlag() {
  return enabled;
}

void AuDevice::SetEnabledFlag(u8 value) {
  enabled = value;
}

// Borrow the device's primary sound interface.
AuSound *AuDevice::GetPrimarySound() {
  return primary;
}

i32 AuDevice::GetMasterVolume() {
  return primary->GetVolume();
}

// Forward the master volume change to the primary sound.
u8 AuDevice::SetMasterVolume(i32 volume) {
  if (!primary->SetVolume(volume))
    return 0;
  if (!sdlDevice)
    return 0;
  i32 value = primary->GetVolume(), scaled = value * value / 1024;
  i32 direct = scaled ? (scaled == 1024 ? 0 : 4000 * scaled / 1024 - 4000) : -10000;
  f32 gain = direct <= -10000 ? 0.0f : static_cast<f32>(pow(10.0, direct / 2000.0));
  return SDL_SetAudioDeviceGain(sdlDevice, gain);
}

// Register a borrowed sound under its ID, rejecting a duplicate before changing the table.
u8 AuDevice::SndRegister(AuSound *sound) {
  u32 id = static_cast<u32>(sound->GetID());
  AuRegistryGuard guard(&lock);
  if (Find(id))
    return 0;
  Reserve(sounds.count + 1);
  u32 index = id % sounds.buckets.size();
  AuSoundEntry *entry = new AuSoundEntry{sounds.buckets[index], id, sound};
  sounds.buckets[index] = entry;
  ++sounds.count;
  return 1;
}

u8 AuDevice::SndUnregister(i32 identifier, u8 release) {
  AuRegistryGuard guard(&lock);
  AuSoundEntry *entry = Find(static_cast<u32>(identifier));
  if (!entry)
    return 0;
  if (release) {
    entry->sound->Release();
  }
  Erase(entry);
  return 1;
}

// Remove the first entry containing this sound, then optionally release the sound.
u8 AuDevice::SndUnregister(AuSound *sound, u8 release) {
  AuRegistryGuard guard(&lock);
  for (AuSoundEntry *head : sounds.buckets) {
    for (AuSoundEntry *entry = head; entry; entry = entry->next) {
      if (entry->sound == sound) {
        Erase(entry);
        if (release)
          sound->Release();
        return 1;
      }
    }
  }
  return 0;
}

// Erase each registered sound in bucket order, releasing it after its entry when requested.
// Recompute the bucket from the current entry so a reentrant registration and rehash follows the original iterator.
u8 AuDevice::SndUnregisterAll(u8 release) {
  AuRegistryGuard guard(&lock);
  usize index = 0;
  while (index < sounds.buckets.size() && !sounds.buckets[index])
    ++index;
  AuSoundEntry *entry = index < sounds.buckets.size() ? sounds.buckets[index] : nullptr;
  while (entry) {
    index = entry->id % sounds.buckets.size();
    AuSoundEntry *next = entry->next;
    if (!next) {
      ++index;
      while (index < sounds.buckets.size() && !sounds.buckets[index])
        ++index;
      next = index < sounds.buckets.size() ? sounds.buckets[index] : nullptr;
    }
    AuSound *sound = entry->sound;
    Erase(entry);
    if (release)
      sound->Release();
    entry = next;
  }
  return 1;
}

u8 AuDevice::IsSndRegistered(i32 identifier) {
  AuRegistryGuard guard(&lock);
  return Find(static_cast<u32>(identifier)) != nullptr;
}

// Search the registered values for the supplied sound pointer.
u8 AuDevice::IsSndRegistered(AuSound *sound) {
  AuRegistryGuard guard(&lock);
  for (AuSoundEntry *head : sounds.buckets)
    for (AuSoundEntry *entry = head; entry; entry = entry->next)
      if (entry->sound == sound)
        return 1;
  return 0;
}

// Borrow the sound registered under an ID, or return null when no entry exists.
AuSound *AuDevice::GetSndPtr(i32 identifier) {
  AuRegistryGuard guard(&lock);
  AuSoundEntry *entry = Find(static_cast<u32>(identifier));
  return entry ? entry->sound : nullptr;
}

// Forward the lookup through the normal sound slot and return its 3D interface spelling.
AuSound3D *AuDevice::GetSndPtr3D(i32 identifier) {
  return reinterpret_cast<AuSound3D *>(GetSndPtr(identifier));
}

u32 AuDevice::GetRegisteredSndCount() {
  return sounds.count;
}

u8 AuDevice::SndPlay(i32 identifier, i32 volume, u8 parameter) {
  AuSound *sound = GetSndPtr(identifier);
  return sound ? sound->Play(volume, parameter, static_cast<AuSoundDuplication>(1)) : 0;
}

// Retain a supplied listener only while the device has no current listener; the original does not store it.
u8 AuDevice::UseSnd3DListener(AuListener3D *value) {
  if (listener)
    return 0;
  value->AddRef();
  return 1;
}

// Release and clear the current listener when one is present.
u8 AuDevice::ReleaseSnd3DListener() {
  if (!listener)
    return 0;
  listener->Release();
  listener = nullptr;
  return 1;
}

// Borrow the currently stored listener.
AuListener3D *AuDevice::GetSnd3DListener() {
  return listener;
}

// Borrow the currently stored effects bus.
AuEffectsBus *AuDevice::GetEffectsBus() {
  return effectsBus;
}

i32 AuDevice::GetEffectsPreset() {
  return effectsPreset;
}

u8 AuDevice::GetUseDriverSupport() {
  return driverSupport;
}

void AuDevice::SetUseDriverSupport(u8 value) {
  driverSupport = value;
}

u32 AuDevice::GetServiceID() const {
  return serviceId;
}

void AuDevice::SetServiceID(u32 identifier) {
  serviceId = identifier;
}

i32 AuDevice::GetServicePriority() const {
  return kAuDeviceServicePriority;
}

bool AuDevice::IsServiceRunning() const {
  return true;
}

void AuDevice::SetServiceRunning(bool) {
}

bool AuDevice::OnTick(u32) {
  return true;
}

bool AuDevice::OnInstall() {
  return true;
}

// Release the registered sounds, listener, primary producer and DirectSound interface in the original order.
bool AuDevice::Shutdown() {
  SndUnregisterAll(1);
  if (effectsBus)
    effectsBus = nullptr;
  ScRelease(&listener);
  if (primary) {
    primary->Stop();
    if (primary)
      delete static_cast<AuSample *>(primary);
    primary = nullptr;
  }
  if (sdlDevice) {
    SDL_CloseAudioDevice(sdlDevice);
    sdlDevice = 0;
  }
  if (sdlAudioInitialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlAudioInitialized = false;
  }
  SetInitFlag(0);
  return true;
}


AuEffectsBus::AuEffectsBus()
    : properties(nullptr), environmentSupported(0), sourceSupported(0), modeSupported(0), mode(1) {
}

// Query the buffer's property interface and the original environment, source and optional mode capabilities.
u8 AuEffectsBus::Initialize(IDirectSound3DBuffer *) {
  return 0;
}

// Apply the selected environment record; a negative remainder would index outside the original table.
u8 AuEffectsBus::SetPreset([[maybe_unused]] i32 preset) {
  return 0;
}

// Query the 3D buffer's property interface, submit the four-byte source level and release the interface.
u8 SC3_STDCALL AuSetEffectsLevel(IDirectSound3DBuffer *, f32) {
  return 0;
}

AuListener::AuListener()
    : references(0), device(nullptr), distanceUnit(static_cast<AuDistanceUnits>(7)), distance(1.0f), position{},
      front{0.0f, 0.0f, 1.0f}, top{0.0f, 1.0f, 0.0f}, listener(nullptr), driverSupport(0) {
  device = AuGetSoundDevice();
}

AuListener::~AuListener() {
  Shutdown();
}

// Publish and retain the same listener for its two supported interface identities.
bool AuListener::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuListener3D);
  HANDLE_QUERY_INTERFACE(IID_AuListener3D, AuListener3D);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuListener::AddRef() {
  return ++references;
}

u32 AuListener::Release() {
  u32 previous = references--;
  if (previous == 1) {
    delete this;
    return 0;
  }
  return references;
}

// Acquire the optional DirectSound 3D interface and apply the stored distance factor when a primary buffer exists.
bool AuListener::Init() {
  driverSupport = 0;
  return SetDistanceFactor(distanceUnit, distance);
}

// Release and clear the owned DirectSound listener interface, if any.
bool AuListener::Shutdown() {
  return true;
}

// Read the DirectSound position when available, or the SDL software listener's cached position.
void AuListener::GetPosition(f32 *x, f32 *y, f32 *z) {
  *x = position[0];
  *y = position[1];
  *z = position[2];
}

// Save the position and, when active, submit it immediately before committing deferred changes.
bool AuListener::SetPosition(f32 x, f32 y, f32 z) {
  position[0] = x;
  position[1] = y;
  position[2] = z;
  return true;
}

// Read the DirectSound velocity, or write three zeroes when no listener exists.
void AuListener::GetVelocity(f32 *x, f32 *y, f32 *z) {
  *x = *y = *z = 0;
}

// Submit deferred velocity to DirectSound, or accept it without storing it when no listener exists.
bool AuListener::SetVelocity([[maybe_unused]] f32 x, [[maybe_unused]] f32 y, [[maybe_unused]] f32 z) {
  return true;
}

// Read DirectSound orientation when present, or return the cached orientation vectors.
void AuListener::GetOrientation(f32 *fx, f32 *fy, f32 *fz, f32 *ux, f32 *uy, f32 *uz) {
  *fx = front[0];
  *fy = front[1];
  *fz = front[2];
  *ux = top[0];
  *uy = top[1];
  *uz = top[2];
}

// Submit deferred orientation when active, or replace the cached vectors when no listener exists.
bool AuListener::SetOrientation(f32 fx, f32 fy, f32 fz, f32 ux, f32 uy, f32 uz) {
  front[0] = fx;
  front[1] = fy;
  front[2] = fz;
  top[0] = ux;
  top[1] = uy;
  top[2] = uz;
  return true;
}

// Return position, velocity and orientation in source order, reusing the original temporary vector.
void AuListener::GetAll(
    f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *fx, f32 *fy, f32 *fz, f32 *ux, f32 *uy, f32 *uz) {
  *x = position[0];
  *y = position[1];
  *z = position[2];
  *vx = *vy = *vz = 0;
  *fx = front[0];
  *fy = front[1];
  *fz = front[2];
  *ux = top[0];
  *uy = top[1];
  *uz = top[2];
}

// Save position and submit all three vectors as deferred changes, committing only after all submissions.
bool AuListener::SetAll(f32 x, f32 y, f32 z, [[maybe_unused]] f32 vx, [[maybe_unused]] f32 vy,
    [[maybe_unused]] f32 vz, f32 fx, f32 fy, f32 fz, f32 ux, f32 uy, f32 uz) {
  position[0] = x;
  position[1] = y;
  position[2] = z;
  front[0] = fx;
  front[1] = fy;
  front[2] = fz;
  top[0] = ux;
  top[1] = uy;
  top[2] = uz;
  return true;
}

// Read the DirectSound Doppler factor, or return zero when no listener exists.
void AuListener::GetDopplerFactor(f32 *factor) {
  *factor = 0;
}

bool AuListener::SetDopplerFactor([[maybe_unused]] f32 factor) {
  return true;
}

// Read the DirectSound rolloff factor, or return zero when no listener exists.
void AuListener::GetRolloffFactor(f32 *factor) {
  *factor = 0;
}

bool AuListener::SetRolloffFactor([[maybe_unused]] f32 factor) {
  return true;
}

void AuListener::GetDistanceFactor(f32 *factor) {
  *factor = distance;
}

void AuListener::GetDistanceFactor(AuDistanceUnits *units, f32 *factor) {
  *units = distanceUnit;
  *factor = distance;
}

// Translate the original distance unit into its factor and apply it immediately when DirectSound is active.
bool AuListener::SetDistanceFactor(AuDistanceUnits units, f32 factor) {
  switch (static_cast<i32>(units)) {
  case 1:
    distance = 0.0254f;
    break;
  case 2:
    distance = 0.3048f;
    break;
  case 3:
    distance = 0.9144f;
    break;
  case 4:
    distance = 1609.34f;
    break;
  case 5:
    distance = 0.01f;
    break;
  case 6:
    distance = 0.1f;
    break;
  case 7:
    distance = 1.0f;
    break;
  case 8:
    distance = 10.0f;
    break;
  case 9:
    distance = 1000.0f;
    break;
  default:
    distance = factor;
    break;
  }
  return true;
}

IDirectSound *AuDirectSound;
IDirectSoundBuffer *AuPrimaryBuffer;
AuListener3D *AuCurrentListener;
static AuSoundDevice *AuCachedSoundDevice;

// Cache the sound service obtained from the real framework and release the temporary service reference.
AuSoundDevice *AuGetSoundDevice() {
  if (!AuCachedSoundDevice) {
    AuSoundDevice *service = nullptr;
    AuGetFrameWork()->GetSystemService(kAuSoundDeviceServiceId, IID_AuSoundDevice, reinterpret_cast<void **>(&service));
    AuCachedSoundDevice = service;
    if (service)
      service->Release();
  }
  return AuCachedSoundDevice;
}

AuDirector::AuDirector() {
  AddDirector(AuGetSoundDirector());
}

AuDirector::~AuDirector() {
}

u32 AuDirector::GetDirectorID() const {
  return kAuDirectorId;
}

// Request the sound device through the stored COM root and install a published system-service view in the live framework.
// Preserve the ignored factory result and temporary service release before the unconditional success return.
bool AuDirector::OnStart(ScCom *) {
  ScSystemService *service = nullptr;
  GZCOM()->GetClassObject(kAuDeviceClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    AuGetFrameWork()->AddSystemService(service);
    service->Release();
  }
  return true;
}

AuSoundDirector::AuSoundDirector() {
  AddClass(kAuDeviceClassId, AuCreateDevice, 0);
}

AuSoundDirector::~AuSoundDirector() {
}

u32 AuSoundDirector::GetDirectorID() const {
  return kAuSoundDirectorId;
}

// Acquire the original registry service, run the child registration pass and release the temporary registry afterward.
bool AuSoundDirector::OnStart(ScCom *) {
  ScRegistry *registry = nullptr;
  AuGetFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

// Construct a sound device and publish its secondary system-service interface without adding a reference.
ScUnknown *AuCreateDevice() {
  return static_cast<ScSystemService *>(new AuDevice());
}

alignas(AuDirector) static u8 AuDirectorStorage[sizeof(AuDirector)];
alignas(AuSoundDirector) static u8 AuSoundDirectorStorage[sizeof(AuSoundDirector)];
static bool AuDirectorInitialized, AuSoundDirectorInitialized;

static void AuDestroyDirector() {
  reinterpret_cast<AuDirector *>(AuDirectorStorage)->~AuDirector();
}

static void AuDestroySoundDirector() {
  reinterpret_cast<AuSoundDirector *>(AuSoundDirectorStorage)->~AuSoundDirector();
}

// Set the original lazy-initialization flag before constructing the main director and registering its exit handler.
AuDirector *AuGetDirector() {
  if (!AuDirectorInitialized) {
    AuDirectorInitialized = true;
    new (AuDirectorStorage) AuDirector();
    atexit(AuDestroyDirector);
  }
  return reinterpret_cast<AuDirector *>(AuDirectorStorage);
}

// Initialize the sound-class child independently, retaining the original flag-before-construction ordering.
AuSoundDirector *AuGetSoundDirector() {
  if (!AuSoundDirectorInitialized) {
    AuSoundDirectorInitialized = true;
    new (AuSoundDirectorStorage) AuSoundDirector();
    atexit(AuDestroySoundDirector);
  }
  return reinterpret_cast<AuSoundDirector *>(AuSoundDirectorStorage);
}

// Borrow the framework stored by the actual module director without creating another service root.
ScFramework *AuGetFrameWork() {
  return AuGetDirector()->FrameWork();
}

#if !defined(SC3_MONOLITHIC)
// Export the same lazy main director without querying another interface or acquiring another reference.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return AuGetDirector();
}
#endif

// The original threshold is shared by every sound in this DLL and initially contains 262144 bytes.
static u32 AuStreamMinimum = kAuDefaultMinimumStreamBytes;
static u32 AuReleasedPrimaryBuffers;
static u32 AuCreatedPrimaryBuffers, AuCreatedDescribedBuffers, AuFailedBufferReleases;
u32 AuCreatedBuffers;

// Each playing instance owns a decoder and a bound SDL stream; queued audio stays in the source PCM format.
struct AuSdlVoice {
  SDL_AudioStream *stream;
  AuWaveReader *reader;
  u64 submitted;
  u32 length, alignment;
  u16 bits, channels;
  i32 pan;
  bool playing, looping, exhausted, failed;
};

// Supply decoded PCM on the SDL audio thread, reopening the source at its original loop boundary when requested.
static void SDLCALL AuFeedSdlVoice(void *context, SDL_AudioStream *stream, int additional, int) {
  auto *voice = static_cast<AuSdlVoice *>(context);
  if (!voice->playing || voice->exhausted || voice->failed || additional <= 0)
    return;
  u8 buffer[8192];
  u8 panned[16384];
  u32 wanted = static_cast<u32>(additional);
  if (voice->channels == 1)
    wanted = (wanted + 1) / 2;
  bool reset_without_progress = false;
  if (voice->alignment > 1)
    wanted = (wanted + voice->alignment - 1) / voice->alignment * voice->alignment;
  while (wanted) {
    u32 count = wanted < sizeof(buffer) ? wanted : sizeof(buffer);
    count -= count % voice->alignment;
    if (!count)
      break;
    u32 read = voice->reader->Read(buffer, count);
    if (read) {
      reset_without_progress = false;
      const u8 *source = buffer;
      u32 submitted = read;
      if (voice->pan || voice->channels == 1) {
        f32 left = voice->pan ? static_cast<f32>(pow(10.0, -voice->pan / 2000.0)) : 1.0f;
        u32 sample_size = voice->bits / 8, frames = read / (sample_size * voice->channels);
        for (u32 frame = 0; frame < frames; ++frame) {
          for (u32 channel = 0; channel < 2; ++channel) {
            u32 input = (frame * voice->channels + (voice->channels == 1 ? 0 : channel)) * sample_size;
            u32 output = (frame * 2 + channel) * sample_size;
            f32 gain = channel == 0 ? left : 1.0f;
            if (sample_size == 1) {
              i32 value = static_cast<i32>((static_cast<i32>(buffer[input]) - 128) * gain) + 128;
              panned[output] = static_cast<u8>(value);
            } else {
              u16 raw = static_cast<u16>(buffer[input] | (static_cast<u16>(buffer[input + 1]) << 8));
              i16 value = static_cast<i16>(static_cast<i16>(raw) * gain);
              panned[output] = static_cast<u8>(value);
              panned[output + 1] = static_cast<u8>(static_cast<u16>(value) >> 8);
            }
          }
        }
        source = panned;
        submitted = frames * 2 * sample_size;
      }
      if (!SDL_PutAudioStreamData(stream, source, static_cast<int>(submitted))) {
        voice->failed = true;
        return;
      }
      voice->submitted += read;
      wanted -= read < wanted ? read : wanted;
    }
    if (read < count) {
      if (!voice->looping) {
        voice->exhausted = true;
        if (!SDL_FlushAudioStream(stream))
          voice->failed = true;
        return;
      }
      if (reset_without_progress || !voice->reader->Reset()) {
        voice->failed = true;
        return;
      }
      reset_without_progress = read == 0;
    }
  }
}

// Open a wave or XA decoder and bind its SDL conversion stream to the live playback device.
// Streaming accepts only the original extensions; buffered sounds try PCM for every non-XA filename.
bool AuSample::OpenSdlVoice(u32 index, bool streaming) {
  ScString suffix = filename.Right(4);
  for (char *p = suffix.begin; *p; ++p)
    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  bool wav = suffix.Strlen() == 4 && memcmp(suffix.Data(), ".WAV", 4) == 0;
  ScString extension = suffix.Right(2);
  bool xa = extension.Strlen() == 2 && memcmp(extension.Data(), "XA", 2) == 0;
  if (streaming && !wav && !xa)
    return false;
  AuSdlVoice *voice = new AuSdlVoice{};
  voice->reader = xa ? static_cast<AuWaveReader *>(new AuXaReader()) : static_cast<AuWaveReader *>(new AuPcmReader());
  if (!voice->reader->Open(filename.Data())) {
    delete voice->reader;
    delete voice;
    return false;
  }
  WAVEFORMATEX source = *voice->reader->GetFormat();
  SDL_AudioSpec spec{};
  spec.channels = source.nChannels == 1 ? 2 : source.nChannels;
  spec.freq = source.nSamplesPerSec;
  spec.format = source.wBitsPerSample == 8 ? SDL_AUDIO_U8 :
      source.wBitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_UNKNOWN;
  if (!spec.format || !source.nBlockAlign || source.nBlockAlign > sizeof(u8[8192]) ||
      (source.nChannels != 1 && source.nChannels != 2) ||
      source.nBlockAlign != source.nChannels * (source.wBitsPerSample / 8)) {
    delete voice->reader;
    delete voice;
    return false;
  }
  voice->length = voice->reader->GetLength();
  voice->alignment = source.nBlockAlign;
  voice->bits = source.wBitsPerSample;
  voice->channels = source.nChannels;
  voice->pan = sdlPan;
  if (streaming && !index)
    CloseSdlVoice(index);
  voice->stream = SDL_CreateAudioStream(&spec, nullptr);
  AuDevice *sound = static_cast<AuDevice *>(device);
  if (!voice->stream || !SDL_SetAudioStreamGetCallback(voice->stream, AuFeedSdlVoice, voice) ||
      !SDL_BindAudioStream(sound->sdlDevice, voice->stream)) {
    if (voice->stream)
      SDL_DestroyAudioStream(voice->stream);
    delete voice->reader;
    delete voice;
    return false;
  }
  sdlVoices[index] = voice;
  if (!index) {
    format = source;
    dataLength = voice->length;
    sdlFrequency = source.nSamplesPerSec;
  }
  return true;
}

// Release a bound audio stream before retiring the decoder used by its callback.
void AuSample::CloseSdlVoice(u32 index) {
  AuSdlVoice *voice = sdlVoices[index];
  if (!voice)
    return;
  SDL_DestroyAudioStream(voice->stream);
  delete voice->reader;
  delete voice;
  sdlVoices[index] = nullptr;
}

// Reset the original decoder and queued PCM before enabling a new or restarted playback instance.
bool AuSample::StartSdlVoice(u32 index, bool repeat) {
  AuSdlVoice *voice = sdlVoices[index];
  if (!voice || !SDL_LockAudioStream(voice->stream))
    return false;
  bool result = voice->reader->Reset() && SDL_ClearAudioStream(voice->stream);
  if (result) {
    voice->submitted = 0;
    voice->exhausted = false;
    voice->failed = false;
    voice->looping = repeat;
    voice->playing = true;
  }
  SDL_UnlockAudioStream(voice->stream);
  return result;
}

// Keep an instance active until SDL consumes the final queued source bytes or reports a playback failure.
bool AuSample::IsSdlVoicePlaying(u32 index) {
  AuSdlVoice *voice = sdlVoices[index];
  if (!voice || !SDL_LockAudioStream(voice->stream))
    return false;
  bool playing = voice->playing && !voice->failed;
  bool exhausted = voice->exhausted;
  SDL_UnlockAudioStream(voice->stream);
  if (!playing)
    return false;
  return !exhausted || SDL_GetAudioStreamQueued(voice->stream) > 0;
}

AuStreamingState::AuStreamingState()
    : reader(nullptr), bufferSize(0), segmentSize(0), nextOffset(0), previousCursor(0), advancedBytes(0), stopped(0),
      ended(0), trailingSegments(0), timerId(0), timerActive(1), playing(0), repeatBuffer(0), busy(0) {
}

// Match the sound's original scoped lock, including release during exception unwinding.
class AuSampleGuard {
public:
  // Enter the sound lock before accessing mutable playback state.
  explicit AuSampleGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Leave the sound lock when the protected operation ends.
  ~AuSampleGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Publish the sound interface for the two supported identities and retain the same object.
bool AuSample::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuSound);
  HANDLE_QUERY_INTERFACE(IID_AuSound, AuSound);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuSample::AddRef() {
  return ++referenceCount;
}

u32 AuSample::Release() {
  u32 previous = referenceCount--;
  if (previous == 1) {
    delete this;
    return 0;
  }
  return referenceCount;
}

u8 AuSample::Init() {
  SetID(static_cast<i32>(reinterpret_cast<uptr>(this)));
  return 1;
}

u8 AuSample::Shutdown() {
  Unload();
  return 1;
}

// Read the input string's length and data, then forward them to the byte-range overload.
u8 AuSample::SetSoundFile(const ScStringView &name, AuStreamingType type) {
  u32 length = name.Strlen();
  const char *data = name.Data();
  return SetSoundFile(data, length, type);
}

// Copy the named byte range and choose streaming from the requested mode and the live file-size probe.
u8 AuSample::SetSoundFile(const char *name, u32 size, AuStreamingType type) {
  AuSampleGuard guard(&lock);
  filename.FromChar(name, size);
  looping = 0;
  streamingType = type;
  reload = 1;
  if (static_cast<i32>(type) == 1)
    streamed = 1;
  else if (static_cast<i32>(type) == 2)
    streamed = IsLargeFile();
  else
    streamed = 0;
  return 1;
}

// Load and play a buffered sound using its duplication policy, or forward a streamed sound to its producer.
// Preserve the original buffer-lost retry and schedule completion only after a successful play path.
u8 AuSample::Play(i32 requestedVolume, u8 loop, AuSoundDuplication duplication) {
  if (!device->GetInitFlag())
    return 1;
  volume = requestedVolume;
  if (reload) {
    reload = 0;
    Unload();
  }
  if (streamed)
    return StartStream(loop, 1);
  if (!IsLoaded() && !Load())
    return 0;
  i32 policy = static_cast<i32>(duplication);
  if (!policy)
    policy = static_cast<i32>(playbackPolicy);
  if (loop)
    looping = 1;
  u32 index = 0;
  if (IsPlaying()) {
    index = sdlRecent;
    if (policy == 2)
      return 1;
    if (policy == 3) {
      for (index = 0; index < 8 && IsSdlVoicePlaying(index); ++index) {}
      if (index == 8 || !sdlVoices[index])
        return 0;
    }
  }
  SetVolume(requestedVolume);
  if (!StartSdlVoice(index, looping != 0))
    return 0;
  sdlRecent = index;
  if (completionCallback && !timer.IsRunning())
    ScheduleCompletion();
  return 1;
}

u8 AuSample::ScheduleCompletion() {
  if (timer.IsRunning() || !completionCallback)
    return 0;
  u32 remaining = EstimateRemainingPlayTime();
  timer.StartOneShot(remaining, nullptr);
  return 1;
}

u8 AuSample::IsPlaying() {
  for (u32 index = 0; index < 8; ++index) {
    if (!sdlVoices[index])
      return 0;
    if (IsSdlVoicePlaying(index)) {
      sdlRecent = index;
      return 1;
    }
  }
  return 0;
}

// Stop each playing buffer under the sound lock, stopping its timer and notifying the callback after each stop.
u8 AuSample::Stop() {
  AuSampleGuard guard(&lock);
  if (streamed)
    return StopStream();
  for (u32 index = 0; index < 8; ++index) {
    AuSdlVoice *voice = sdlVoices[index];
    if (!voice)
      break;
    if (!IsSdlVoicePlaying(index))
      continue;
    if (!SDL_LockAudioStream(voice->stream))
      return 0;
    voice->playing = false;
    bool stopped = SDL_ClearAudioStream(voice->stream);
    SDL_UnlockAudioStream(voice->stream);
    if (!stopped)
      return 0;
    if (timer.IsRunning())
      timer.Stop();
    if (completionCallback)
      completionCallback(completionMessage);
  }
  return 1;
}

// Stop all populated buffers, cancel an active timer and notify once if a sound was playing.
u8 AuSample::StopAllInstances() {
  u8 wasPlaying = IsPlaying();
  if (streamed)
    return StopStream();
  for (u32 index = 0; index < 8; ++index) {
    AuSdlVoice *voice = sdlVoices[index];
    if (!voice)
      break;
    if (!SDL_LockAudioStream(voice->stream))
      return 0;
    voice->playing = false;
    bool stopped = SDL_ClearAudioStream(voice->stream);
    SDL_UnlockAudioStream(voice->stream);
    if (!stopped)
      return 0;
  }
  if (timer.IsRunning())
    timer.Stop();
  if (wasPlaying && completionCallback)
    completionCallback(completionMessage);
  return 1;
}

// Report buffered, streaming or unavailable sound type from the original live state.
AuSoundType AuSample::GetSoundType() {
  return static_cast<AuSoundType>(sdlVoices[0] ? 1 : streamed ? 3 : 0);
}

AuSoundStatus AuSample::GetStatus() {
  return static_cast<AuSoundStatus>(identifier ? IsPlaying() != 0 : 5);
}

u32 AuSample::GetSoundStreamMinimumFileSize() {
  return AuStreamMinimum;
}

u8 AuSample::SetSoundStreamMinimumFileSize(u32 size) {
  AuStreamMinimum = size;
  return 1;
}

u8 AuSample::SetSoundDefaultStreamingType(AuStreamingType type) {
  streamingType = type;
  return 1;
}

u32 AuSample::GetUseCount() {
  u32 count = 0;
  for (AuSdlVoice *voice : sdlVoices)
    if (voice)
      ++count;
  return count;
}

i32 AuSample::GetID() {
  return identifier;
}

void AuSample::SetID(i32 value) {
  identifier = value;
}

// Return the address of the sound's owned wave format.
tWAVEFORMATEX *AuSample::GetWaveFormat() {
  return &format;
}

// Report whether the primary DirectSound buffer exists.
u8 AuSample::IsLoaded() {
  return sdlVoices[0] != nullptr;
}

// Load a buffered or streaming sound under the lock, using the existing buffer and stream-ready state.
u8 AuSample::Load() {
  if (!device->GetInitFlag())
    return 1;
  AuSampleGuard guard(&lock);
  if (!IsLoaded() && !streamed)
    return LoadBuffered();
  if (streamed && !streamReady)
    return PrimeStream(1);
  return 1;
}

// Stop the timer and playing sound, retire the streaming reader and state, then release the primary buffer.
u8 AuSample::Unload() {
  AuSampleGuard guard(&lock);
  if (timer.IsRunning())
    timer.Stop();
  if (IsPlaying())
    Stop();
  for (u32 index = 0; index < 8; ++index)
    CloseSdlVoice(index);
  streamReady = 0;
  return 1;
}

// Open a PCM or XA stream and create the original six-segment DirectSound ring buffer.
u8 AuSample::OpenStream(u8 replace) {
  if (!device->GetInitFlag())
    return 1;
  if (IsPlaying()) {
    if (!replace)
      return 0;
    Stop();
  }
  return OpenSdlVoice(0, true);
}

// Prime the first streaming buffer; a zero segment size reaches undefined division in the original.
u8 AuSample::PrimeStream(u8 reopen) {
  if (!device->GetInitFlag())
    return 1;
  if (reopen && !OpenStream(1))
    return 0;
  if (!sdlVoices[0])
    return 0;
  reload = 0;
  streamReady = 1;
  return 1;
}

// Start the original streaming buffer and its multimedia refill timer.
u8 AuSample::StartStream(u8 loop, u32 reopen) {
  if (!device->GetInitFlag())
    return 1;
  if (loop)
    looping = 1;
  if (playbackPolicy == 1 && IsPlaying())
    StopStream();
  if (!streamReady && !PrimeStream(static_cast<u8>(reopen)))
    return 0;
  if (volume != 1024)
    SetVolume(volume);
  return StartSdlVoice(0, looping != 0);
}

// Stop the stream's multimedia timer and buffer, then close and release its reader.
u8 AuSample::StopStream() {
  streamReady = 0;
  AuSdlVoice *voice = sdlVoices[0];
  if (!voice)
    return 1;
  if (!SDL_LockAudioStream(voice->stream))
    return 0;
  voice->playing = false;
  bool stopped = SDL_ClearAudioStream(voice->stream);
  SDL_UnlockAudioStream(voice->stream);
  return stopped;
}

// Release the primary DirectSound buffer under the recursive lock and increment the original release counter.
u8 AuSample::ReleasePrimaryBuffer() {
  AuSampleGuard guard(&lock);
  CloseSdlVoice(0);
  return 1;
}

// Load the chosen wave reader directly into the locked DirectSound buffer, preserving failed-open handle ownership.
u8 AuSample::LoadBuffered() {
  if (!device->GetInitFlag())
    return 1;
  return filename.Strlen() && OpenSdlVoice(0, false);
}

// Find the first idle populated buffer, or ask DirectSound to duplicate the first empty slot's value.
u8 AuSample::FindAvailableBuffer(IDirectSoundBuffer **) {
  return 0;
}

u8 AuSample::CreateBuffer(_DSBUFFERDESC *description) {
  if (!device->GetInitFlag())
    return 1;
  if (description)
    return 0;
  return sdlVoices[0] || OpenSdlVoice(0, false);
}

// Create a primary buffer with control-volume support, retrying without that flag before setting its format.
u8 AuSample::CreatePrimaryBuffer() {
  return 0;
}

// Store the completion callback and its original 32-bit argument.
u8 AuSample::SetCompletionNotification(AuSoundCompletion callback, u32 message) {
  completionCallback = callback;
  completionMessage = message;
  return 1;
}

u8 AuSample::StopCompletionNotification() {
  completionCallback = nullptr;
  if (timer.IsRunning())
    timer.Stop();
  return 1;
}

// Compute remaining time from the original bit depth, byte length and play cursor; zero divisors are unavailable.
u32 AuSample::EstimateRemainingPlayTime() {
  if (!IsPlaying()) {
    assert(format.wBitsPerSample && format.nSamplesPerSec);
    return 1000u * ((8000u * (dataLength / format.wBitsPerSample)) / format.nSamplesPerSec);
  }
  if (looping)
    return kAuSampleIndefiniteRemainingTime;
  u32 position = GetPosition();
  assert(format.wBitsPerSample && format.nSamplesPerSec);
  return 8000u * ((dataLength - position) / format.wBitsPerSample) / format.nSamplesPerSec;
}

// Compare the named file's size with the DLL-wide streaming threshold after opening it with the original API.
u8 AuSample::IsLargeFile() {
  ScString native = ScNativeFilePath(filename.Data());
  SDL_IOStream *file = SDL_IOFromFile(native.Data(), "rb");
  if (!file)
    return 0;
  i64 size = SDL_GetIOSize(file);
  SDL_CloseIO(file);
  return size >= 0 && static_cast<u64>(size) >= AuStreamMinimum;
}

i32 AuSample::GetVolume() {
  return volume;
}

// Clamp and save the requested volume, then apply its master-scaled DirectSound value to the primary buffer.
u8 AuSample::SetVolume(i32 value) {
  if (value < 0)
    value = 0;
  if (value > 1024)
    value = 1024;
  volume = value;
  if (!sdlVoices[0])
    return 1;
  i32 scaled = static_cast<i32>(static_cast<u32>(value) * static_cast<u32>(device->GetMasterVolume())) / 1024;
  i32 direct = scaled ? (scaled == 1024 ? 0 : static_cast<i32>(4000u * static_cast<u32>(scaled)) / 1024 - 4000)
      : -10000;
  f32 gain = direct <= -10000 ? 0.0f : static_cast<f32>(pow(10.0, direct / 2000.0));
  for (AuSdlVoice *voice : sdlVoices)
    if (voice && !SDL_SetAudioStreamGain(voice->stream, gain))
      return 0;
  return 1;
}


// Fade volume over ten steps synchronously or start the original periodic timer for asynchronous fading.
u8 AuSample::FadeVolume(i32 first, i32 second, u32 duration, u8 asynchronous) {
  ScStopwatch clock(ScStopwatch::Milliseconds, 0);
  if (!device->GetInitFlag())
    return 1;
  i32 delta = static_cast<i32>(static_cast<u32>(second) - static_cast<u32>(first)) / 10;
  if (asynchronous) {
    if (fadeTimer)
      return 0;
    fadeTimer = new AuCallbackTimer();
    fadeTimer->SetCallbackFunction(AuFadeSound, reinterpret_cast<uptr>(this));
    fadeTimer->SetResolution(100);
    fadeTarget = second;
    fadeStep = delta;
    SetVolume(first);
    fadeTimer->StartPeriodic(duration / 10, nullptr);
    return 1;
  }
  i32 current = first;
  SetVolume(first);
  for (u32 i = 0; i < 10; ++i) {
    clock.Restart();
    while (clock.GetElapsedTime() < duration / 10)
      ScThread::SleepCurrentThread(0);
    current = static_cast<i32>(static_cast<u32>(current) + static_cast<u32>(delta));
    SetVolume(current);
  }
  return 1;
}

// Read pan from the primary buffer, using the original centered fallback when it is absent or fails.
i32 AuSample::GetPan() {
  return sdlVoices[0] ? sdlPan : 512;
}

// Clamp pan to the original 0..1024 range and forward it to the primary DirectSound buffer.
u8 AuSample::SetPan(i32 value) {
  if (!sdlVoices[0])
    return 0;
  if (value < 0)
    value = 0;
  if (value > 1024)
    value = 1024;
  sdlPan = value;
  for (AuSdlVoice *voice : sdlVoices) {
    if (!voice)
      continue;
    if (!SDL_LockAudioStream(voice->stream))
      return 0;
    voice->pan = value;
    SDL_UnlockAudioStream(voice->stream);
  }
  return 1;
}

// Return the primary buffer's frequency, falling back to the sound's original format rate.
i32 AuSample::GetFrequency() {
  return sdlVoices[0] ? sdlFrequency : GetOriginalFrequency();
}

// Skip frequency changes when the device is uninitialized; otherwise clamp and update the primary buffer.
u8 AuSample::SetFrequency(i32 value) {
  if (!device->GetInitFlag())
    return 1;
  if (!sdlVoices[0] || !format.nSamplesPerSec)
    return 0;
  if (value < 100)
    value = 100;
  if (value > kAuSampleMaximumFrequency)
    value = kAuSampleMaximumFrequency;
  f32 ratio = static_cast<f32>(value) / static_cast<f32>(format.nSamplesPerSec);
  for (AuSdlVoice *voice : sdlVoices)
    if (voice && !SDL_SetAudioStreamFrequencyRatio(voice->stream, ratio))
      return 0;
  sdlFrequency = value;
  return 1;
}

i32 AuSample::GetOriginalFrequency() {
  return static_cast<i32>(format.nSamplesPerSec);
}

// Return the current play cursor, or zero when the primary buffer is absent or the query fails.
u32 AuSample::GetPosition() {
  AuSdlVoice *voice = sdlVoices[0];
  if (!voice || !voice->length || !SDL_LockAudioStream(voice->stream))
    return 0;
  i32 queued = SDL_GetAudioStreamQueued(voice->stream);
  if (voice->channels == 1 && queued > 0)
    queued /= 2;
  u64 played = queued < 0 || static_cast<u64>(queued) > voice->submitted ? 0 : voice->submitted - queued;
  u32 result = static_cast<u32>(played % voice->length);
  SDL_UnlockAudioStream(voice->stream);
  return result;
}

// Skip position changes while the device is uninitialized, or forward the position to its primary buffer.
u8 AuSample::SetPosition(u32 position) {
  if (!device->GetInitFlag())
    return 1;
  AuSdlVoice *voice = sdlVoices[0];
  if (!voice || position >= voice->length || position % voice->alignment || !SDL_LockAudioStream(voice->stream))
    return 0;
  bool result = voice->reader->Reset() && SDL_ClearAudioStream(voice->stream);
  u8 discarded[4096];
  u32 remaining = position;
  while (result && remaining) {
    u32 count = remaining < sizeof(discarded) ? remaining : sizeof(discarded);
    u32 read = voice->reader->Read(discarded, count);
    if (read != count)
      result = false;
    remaining -= read;
  }
  if (result) {
    voice->submitted = position;
    voice->exhausted = false;
    voice->failed = false;
  }
  SDL_UnlockAudioStream(voice->stream);
  return result;
}

i32 AuSample::GetSoundPriority() {
  return priority;
}

void AuSample::SetSoundPriority(i32 value) {
  priority = value;
}

// Clamp the streaming-buffer multiplier to the original inclusive range of one through six.
void AuSample::SetStreamingBufferSizeMultiplier(u32 value) {
  if (!value)
    value = 1;
  if (value > 6)
    value = 6;
  streamingMultiplier = value;
}

static ScLock AuSampleLock;
static std::set<AuSample *> AuSamples;
using AuBusyFlag = SDL_AtomicInt;
static AuBusyFlag AuCompletionBusy, AuFadeBusy;

// Exchange the callback guard without changing the original overlap suppression order.
static i32 AuExchangeBusy(AuBusyFlag *flag, i32 value) {
  return SDL_SetAtomicInt(flag, value);
}

// Hold the original global sound lock throughout a registered timer callback.
class AuLiveSoundGuard {
public:
  // Enter the recursive live-sound lock.
  AuLiveSoundGuard() {
    AuSampleLock.Lock();
  }

  // Release the lock when the callback completes or unwinds.
  ~AuLiveSoundGuard() {
    AuSampleLock.Unlock();
  }
};

// Insert the actual producer address in the module's synchronized live-sound set.
void AuRegisterSample(AuSample *sample) {
  AuSampleLock.Lock();
  AuSamples.insert(sample);
  AuSampleLock.Unlock();
}

// Remove a producer from the live-sound set before its owned state is retired.
void AuUnregisterSample(AuSample *sample) {
  AuSampleLock.Lock();
  AuSamples.erase(sample);
  AuSampleLock.Unlock();
}

// Run completion handling only for a registered sound, suppressing overlapping timer callbacks.
void AuScheduledSound(uptr address) {
  AuLiveSoundGuard guard;
  AuSample *sample = reinterpret_cast<AuSample *>(address);
  if (AuSamples.find(sample) == AuSamples.end() || AuExchangeBusy(&AuCompletionBusy, 1) != 0)
    return;
  if (sample->IsPlaying())
    sample->ScheduleCompletion();
  else if (sample->completionCallback)
    sample->completionCallback(sample->completionMessage);
  AuExchangeBusy(&AuCompletionBusy, 0);
}

// Advance a registered sound's fade under both original live-set checks and suppress overlapping callbacks.
void AuFadeSound(uptr address) {
  AuLiveSoundGuard outer;
  AuSample *sample = reinterpret_cast<AuSample *>(address);
  if (AuSamples.find(sample) == AuSamples.end() || AuExchangeBusy(&AuFadeBusy, 1) != 0)
    return;
  {
    AuLiveSoundGuard inner;
    if (AuSamples.find(sample) != AuSamples.end()) {
      i32 next = static_cast<i32>(static_cast<u32>(sample->volume) + static_cast<u32>(sample->fadeStep));
      sample->SetVolume(next);
      if (next <= sample->fadeTarget && sample->fadeTimer) {
        sample->fadeTimer->Stop();
        delete sample->fadeTimer;
        sample->fadeTimer = nullptr;
      }
    }
  }
  AuExchangeBusy(&AuFadeBusy, 0);
}


AuSample::AuSample()
    : identifier(0), unknown28(0), looping(0), streamed(0), volume(1024), reload(0), completionMessage(0),
      playbackPolicy(1), priority(1), timerPeriodHeld(0), fadeTimer(nullptr), completionCallback(nullptr),
      streamingType(static_cast<AuStreamingType>(0)), buffers{}, recent(nullptr), dataLength(0), format{},
      stream(nullptr), streamReady(0), streamingMultiplier(1), capabilities(65760), referenceCount(0), device(nullptr) {
  timer.SetCallbackFunction(AuScheduledSound, reinterpret_cast<uptr>(this));
  timer.SetResolution(10);
  device = AuGetSoundDevice();
  AuRegisterSample(this);
}

AuSample::AuSample(const ScStringView &name, AuStreamingType streaming)
    : identifier(0), filename(name), unknown28(0), looping(0), streamed(0), volume(1024), reload(0),
      completionMessage(0), playbackPolicy(1), priority(1), timerPeriodHeld(0), fadeTimer(nullptr),
      completionCallback(nullptr), streamingType(streaming), buffers{}, recent(nullptr), dataLength(0), format{},
      stream(nullptr), streamReady(0), streamingMultiplier(1), capabilities(65760), referenceCount(0), device(nullptr) {
  timer.SetCallbackFunction(AuScheduledSound, reinterpret_cast<uptr>(this));
  timer.SetResolution(10);
  device = AuGetSoundDevice();
  AuRegisterSample(this);
}

// Stop playback, unregister the producer, and release its buffers and owned fade timer.
AuSample::~AuSample() {
  if (AuSample::IsPlaying())
    AuSample::Stop();
  AuUnregisterSample(this);
  AuSample::Unload();
  ScDelete(&fadeTimer);
}


// Hold the original sound lock through spatial-buffer release, including exception unwinding.
class AuSpatialGuard {
public:
  // Enter the sound's recursive lock.
  explicit AuSpatialGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Leave the sound lock when the protected operation ends.
  ~AuSpatialGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

AuSpatialSample::AuSpatialSample()
    : spatialBuffers{}, minimumDistance(10.0f), maximumDistance(1000.0f), hardware(0), attenuation(0), unknown284(0),
      position{}, option(0) {
  priority = 10;
  AuRegisterSample(this);
}

AuSpatialSample::AuSpatialSample(const ScStringView &name, AuStreamingType streaming, u8 value)
    : AuSample(name, streaming), spatialBuffers{}, minimumDistance(10.0f), maximumDistance(1000.0f), hardware(0),
      attenuation(0), unknown284(0), position{}, option(value) {
  priority = 10;
  AuRegisterSample(this);
}

AuSpatialSample::~AuSpatialSample() {
  AuUnregisterSample(this);
}

// Publish either the primary sound or secondary spatial interface and retain their shared object.
bool AuSpatialSample::QueryInterface(u32 iid, void **out) {
  if (iid == IID_ScUnknown || iid == IID_AuSound)
    *out = static_cast<AuSound *>(static_cast<AuSample *>(this));
  else if (iid == IID_AuSound3D)
    *out = static_cast<AuSound3D *>(this);
  else
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

u32 AuSpatialSample::AddRef() {
  return AuSample::AddRef();
}

u32 AuSpatialSample::Release() {
  return AuSample::Release();
}

// Commit deferred listener settings before forwarding playback through the primary sound implementation.
u8 AuSpatialSample::Play(i32 value, u8 loop, AuSoundDuplication duplication) {
  if (!device->GetInitFlag())
    return 1;
  assert(AuCurrentListener);
  return AuSample::Play(value, loop, duplication);
}

// Apply software distance attenuation while retaining the requested unattenuated sound volume.
u8 AuSpatialSample::SetVolume(i32 value) {
  if (!device->GetInitFlag())
    return 1;
  if (hardware)
    return AuSample::SetVolume(value);
  i32 adjusted = static_cast<i32>(static_cast<u32>(value) - static_cast<u32>(attenuation));
  u8 result = AuSample::SetVolume(adjusted);
  volume = value;
  return result;
}

u8 AuSpatialSample::SetPan(i32) {
  return 0;
}

// Read hardware position when available, or return the cached software position.
void AuSpatialSample::GetPosition(f32 *x, f32 *y, f32 *z) {
  *x = position[0];
  *y = position[1];
  *z = position[2];
}

// Store software position and compute its distance attenuation. Hardware samples forward the coordinates directly.
u8 AuSpatialSample::SetPosition(f32 x, f32 y, f32 z) {
  position[0] = x;
  position[1] = y;
  position[2] = z;
  assert(AuCurrentListener);
  AuListener *current = static_cast<AuListener *>(AuCurrentListener);
  double dx = static_cast<double>(x) - current->position[0];
  double dy = static_cast<double>(y) - current->position[1];
  double dz = static_cast<double>(z) - current->position[2];
  double raw = sqrt(dx * dx + dy * dy + dz * dz) * 6000.0 / maximumDistance;
  assert(std::isfinite(raw) && raw >= -9223372036854775808.0 && raw < 9223372036854775808.0);
  i32 saved = volume;
  attenuation = static_cast<i32>(static_cast<i64>(raw));
  i32 adjusted = static_cast<i32>(static_cast<u32>(saved) - static_cast<u32>(attenuation));
  u8 result = AuSample::SetVolume(adjusted);
  volume = saved;
  return result;
}

// Read hardware velocity when available, or return zero velocity for software spatialization.
void AuSpatialSample::GetVelocity(f32 *x, f32 *y, f32 *z) {
  *x = *y = *z = 0;
}

// Submit deferred velocity only for an initialized hardware 3D sound.
u8 AuSpatialSample::SetVelocity([[maybe_unused]] f32 x, [[maybe_unused]] f32 y, [[maybe_unused]] f32 z) {
  if (!device->GetInitFlag() || !hardware)
    return true;
  // The original hardware branch rejects a missing spatial buffer.
  assert(!spatialBuffers[0]);
  return 0;
}

// Read hardware position and velocity using the same temporary, or return the software position and zero velocity.
void AuSpatialSample::GetAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz) {
  *x = position[0];
  *y = position[1];
  *z = position[2];
  *vx = *vy = *vz = 0;
}

// Set the software position or forward hardware position and velocity with the original apply modes.
u8 AuSpatialSample::SetAll(f32 x, f32 y, f32 z, [[maybe_unused]] f32 vx, [[maybe_unused]] f32 vy,
    [[maybe_unused]] f32 vz) {
  if (!device->GetInitFlag())
    return true;
  if (!hardware)
    return SetPosition(x, y, z);
  assert(!spatialBuffers[0]);
  return 0;
}

// Clamp sound power, convert it through the listener's distance factor and set both distance bounds.
void AuSpatialSample::SetPower(u32 power) {
  static const f32 distances[] = {0, 1, 10, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
  if (!power)
    power = 1;
  if (power > 10)
    power = 10;
  assert(AuCurrentListener);
  f32 factor = 0;
  AuCurrentListener->GetDistanceFactor(&factor);
  f32 minimum = distances[power] / factor;
  SetMinimumDistance(minimum);
  SetMaximumDistance(minimum * 100.0f);
}

// Read the hardware minimum distance when that interface is present; other paths leave the output untouched.
void AuSpatialSample::GetMinimumDistance([[maybe_unused]] f32 *distance) {
}

// Store the minimum distance and apply it immediately when hardware 3D support is active.
u8 AuSpatialSample::SetMinimumDistance(f32 distance) {
  minimumDistance = distance;
  if (hardware) {
    assert(!spatialBuffers[0]);
    return 0;
  }
  return 1;
}

// Read the hardware maximum distance when that interface is present; other paths leave the output untouched.
void AuSpatialSample::GetMaximumDistance([[maybe_unused]] f32 *distance) {
}

// Store the maximum distance, treating an absent hardware buffer as success as the original does.
u8 AuSpatialSample::SetMaximumDistance(f32 distance) {
  maximumDistance = distance;
  if (hardware)
    assert(!spatialBuffers[0]);
  // Unlike the minimum-distance setter, the original accepts a missing hardware buffer here.
  return 1;
}

// Request the live effects bus, then submit the normalized signed level when a 3D buffer exists.
u8 AuSpatialSample::SetEffectsLevel([[maybe_unused]] i32 level) {
  device->GetEffectsBus();
  return true;
}

// Create a hardware 3D buffer when supported and preserve the original descriptor mutations on fallback.
// A null interface is only dereferenced when the original option requests a mode change.
u8 AuSpatialSample::CreateBuffer(_DSBUFFERDESC *description) {
  hardware = 0;
  return AuSample::CreateBuffer(description);
}

// Prepare a hardware 3D streaming buffer or use the primary producer's software path when unsupported.
// A missing interface after a successful allocation is kept explicit where the original would dereference it.
u8 AuSpatialSample::OpenStream(u8 replace) {
  hardware = 0;
  return AuSample::OpenStream(replace);
}

// Release every spatial interface before releasing each associated sound buffer under the sound lock.
u8 AuSpatialSample::ReleasePrimaryBuffer() {
  return AuSample::ReleasePrimaryBuffer();
}
