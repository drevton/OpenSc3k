#include "audio/audio_effects.h"

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>

#include <cassert>

#include "application/api.h"
#include "audio/api.h"
#include "audio/audio_core.h"
#include "audio/audio_music.h"
#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"

static ScLanguageManager *AuAudioLanguage;

AuAudioEffect::AuAudioEffect() : AuAudioEffect(0, 0, 0, 0) {
}

AuAudioEffect::AuAudioEffect(u32 id, u32 type, u8 direct, u32 value)
    : baseReferences(0), references(0), unknown12(0), manager(nullptr), initialized(0), byte21(0), identifier(id),
      unknown28(0), unknown32(0), category(type), volume(0), directPath(direct), unknown48(0), value52(value) {
}

AuAudioEffect::~AuAudioEffect() {
}

AuSingleEffect::AuSingleEffect()
    : AuAudioEffect(), sound(nullptr), factoryFlag(0), resident(0), playFlags(0) {
}

// Store the one-shot configuration and copy its filename after initializing the common effect base.
AuSingleEffect::AuSingleEffect(u32 id, u32 type, u8 direct, const ScStringView *name, u8 keepResident,
    u8 useFactory, u32 initialVolume, u32 flags)
    : AuAudioEffect(id, type, direct, 0), sound(nullptr), factoryFlag(useFactory), filename(*name),
      resident(keepResident), playFlags(flags) {
  volume = initialVolume;
}

AuSingleEffect::~AuSingleEffect() {
}

AuEffect623F0B9C::AuEffect623F0B9C() : AuAudioEffect(), sound(nullptr), factoryFlag(0), resident(0) {
}

AuEffect623F0B9C::~AuEffect623F0B9C() {
}

// Publish the registered effect for the common effect or unknown identity and retain it on success.
bool AuEffect623F0B9C::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuEffect623F0B9C);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuEffect623F0B9C);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuEffect623F0B9C::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and delete the effect once its count reaches zero.
u32 AuEffect623F0B9C::Release() {
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

u8 AuEffect623F0B9C::Init(AuAudioManager *owner) {
  manager = owner;
  ScString path;
  if (directPath) {
    path = filename;
  } else {
    path = *AuGetAudioSoundDirectory();
    path.Append(filename.Data(), filename.Strlen());
  }
  sound = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(factoryFlag != 0), 0);
  if (resident)
    sound->Load();
  initialized = 1;
  return 1;
}

u8 AuEffect623F0B9C::Shutdown() {
  if (resident)
    sound->Unload();
  sound->Release();
  sound = nullptr;
  initialized = 0;
  return 1;
}

u8 AuEffect623F0B9C::Update() {
  return 1;
}

u8 AuEffect623F0B9C::Play(i32) {
  return 1;
}

u8 AuEffect623F0B9C::IsPlaying(i32) {
  return sound->IsPlaying();
}

void AuEffect623F0B9C::UnresolvedSlot44() {
}

void AuEffect623F0B9C::UnresolvedSlot48(uptr, uptr, uptr, uptr) {
}

u8 AuEffect623F0B9C::UnresolvedSlot52(uptr) {
  return 1;
}

u8 AuEffect623F0B9C::UnresolvedSlot56(uptr, uptr) {
  return 0;
}

u8 AuEffect623F0B9C::UnresolvedSlot60(uptr, uptr) {
  return 0;
}

u8 AuEffect623F0B9C::UnresolvedSlot64() {
  return 1;
}

u8 AuEffect623F0B9C::LoadSound() {
  if (resident)
    return 0;
  sound->Load();
  return 1;
}

u8 AuEffect623F0B9C::UnloadSound() {
  if (resident)
    return 0;
  sound->Unload();
  return 1;
}

u8 AuEffect623F0B9C::UnresolvedSlot80(u32, u32) {
  return 1;
}

u8 AuEffect623F0B9C::UnresolvedSlot84(uptr) {
  return 1;
}

AuDisasterEffect::AuDisasterEffect()
    : AuAudioEffect(), sound(nullptr), value80(0), value84(0), byte88(0), value92(0), byte96(0) {
}

// Copy the disaster sound path and initialize its volume, timing values and original state bytes.
AuDisasterEffect::AuDisasterEffect(u32 id, u32 type, u8 direct, const ScStringView *source,
    u32 initial84, u32 initialVolume, u32 initial80)
    : AuAudioEffect(id, type, direct, 0), path(*source), sound(nullptr), value80(initial80), value84(initial84),
      byte88(0), value92(100), byte96(0) {
  volume = initialVolume;
}

AuDisasterEffect::~AuDisasterEffect() {
}

// Publish the disaster effect for the common effect or unknown identity and retain it on success.
bool AuDisasterEffect::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuDisasterEffect);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuDisasterEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuDisasterEffect::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and delete the disaster effect when its count reaches zero.
u32 AuDisasterEffect::Release() {
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

// Create the optional disaster sound from its direct or sound-directory path and mark the effect initialized.
u8 AuDisasterEffect::Init(AuAudioManager *owner) {
  manager = owner;
  if (path.Strlen()) {
    ScString fullPath;
    if (directPath) {
      fullPath = path;
    } else {
      fullPath = *AuGetAudioSoundDirectory();
      fullPath.Append(path.Data(), path.Strlen());
    }
    sound = AuManagerSoundDevice->SndCreate(&fullPath, static_cast<AuStreamingType>(0));
  }
  byte88 = 0;
  initialized = 1;
  return 1;
}

// Stop an active disaster effect through its virtual hook, then release its sound and clear initialization.
u8 AuDisasterEffect::Shutdown() {
  if (byte88)
    UnresolvedSlot84(0);
  ScRelease(&sound);
  initialized = 0;
  return 1;
}

u8 AuDisasterEffect::Update() {
  return 1;
}

// Start the disaster sound and its configured dependent effect at the current calculated volume.
u8 AuDisasterEffect::Play(i32) {
  u8 result = 1;
  i32 selected = CalculateVolume();
  manager->UpdateMusic();
  if (selected) {
    if (sound) {
      sound->Load();
      result = sound->Play(selected, 1, static_cast<AuSoundDuplication>(2));
    }
    if (value80) {
      manager->effects[value80]->Play(0);
      byte88 = 1;
      Refresh();
    }
  }
  byte88 = 1;
  return result;
}

u8 AuDisasterEffect::IsPlaying(i32) {
  return byte88;
}

void AuDisasterEffect::UnresolvedSlot44() {
  if (byte88)
    Refresh();
}

void AuDisasterEffect::UnresolvedSlot48(uptr, uptr, uptr, uptr) {
}

u8 AuDisasterEffect::UnresolvedSlot52(uptr value) {
  if (!byte88)
    return 1;
  if (sound)
    sound->Stop();
  u32 linked = value80;
  byte88 = 0;
  byte96 = 0;
  if (linked) {
    AuAudioEffect *dependent = manager->effects[linked];
    if (dependent)
      dependent->UnresolvedSlot56(0, 0);
  }
  if (identifier == 61 && manager->unknown2520 > 10) {
    i32 target = 0;
    switch (manager->viewZoom) {
    case 3:
      target = 147;
      break;
    case 4:
      target = 148;
      break;
    case 5:
      target = 149;
      break;
    }
    if (target && manager->effects[target])
      manager->effects[target]->Play(static_cast<i32>(value));
  }
  return UnresolvedSlot84(0);
}

// Set the disaster active flag and its secondary state from the second event payload, then refresh its sound.
u8 AuDisasterEffect::UnresolvedSlot56(uptr, uptr value) {
  byte88 = 1;
  byte96 = static_cast<u32>(value) != 0;
  Refresh();
  return 1;
}

// Clamp the second event payload to the original 0â€“100 range and refresh the disaster sound.
u8 AuDisasterEffect::UnresolvedSlot60(uptr, uptr value) {
  i32 selected = static_cast<i32>(value);
  if (selected < 0)
    selected = 0;
  if (selected > 100)
    selected = 100;
  value92 = static_cast<u32>(selected);
  Refresh();
  return 1;
}

u8 AuDisasterEffect::UnresolvedSlot64() {
  return 1;
}

u8 AuDisasterEffect::LoadSound() {
  return 1;
}

u8 AuDisasterEffect::UnloadSound() {
  return 1;
}

u8 AuDisasterEffect::UnresolvedSlot80(u32, u32) {
  return 1;
}

// Stop the active disaster and forward the stop request to its configured dependent effect when present.
u8 AuDisasterEffect::UnresolvedSlot84(uptr) {
  if (byte88) {
    UnresolvedSlot52(0);
    if (value80) {
      AuAudioEffect *dependent = manager->effects[value80];
      if (dependent)
        dependent->UnresolvedSlot84(0);
    }
  }
  return 1;
}

// Store the new volume and update, start or stop an active disaster sound as the original requests.
u8 AuDisasterEffect::SetBaseVolume(u32 value, u32) {
  volume = value;
  if (!byte88)
    return 1;
  if (!volume) {
    sound->Stop();
    return 1;
  }
  u8 playing = sound->IsPlaying();
  i64 scaled = 2ll * static_cast<i32>(volume);
  assert(scaled >= INT32_MIN && scaled <= INT32_MAX);
  if (playing)
    return sound->SetVolume(static_cast<i32>(scaled) / 3);
  return sound->Play(static_cast<i32>(scaled) / 3, 1, static_cast<AuSoundDuplication>(2));
}

// Calculate the disaster volume from the current view, ambience volume and configured percentage.
i32 AuDisasterEffect::CalculateVolume() {
  if (manager->viewMode)
    return 0;
  if (value84)
    return value84 - 1 <= 1 ? manager->ambienceVolume : 0;
  i32 ambience = manager->ambienceVolume;
  i64 first = 3ll * ambience, second = 2ll * ambience;
  assert(first >= INT32_MIN && first <= INT32_MAX && second >= INT32_MIN && second <= INT32_MAX);
  second *= static_cast<i32>(value92);
  assert(second >= INT32_MIN && second <= INT32_MAX);
  i64 result = first / 5 + second / 500;
  assert(result >= INT32_MIN && result <= INT32_MAX);
  if (manager->viewZoom < 4) {
    i64 zoom = static_cast<i64>(manager->viewZoom) - 4;
    i64 adjustment = 100 * zoom;
    assert(zoom >= INT32_MIN && zoom <= INT32_MAX && adjustment >= INT32_MIN && adjustment <= INT32_MAX);
    result += adjustment;
    assert(result >= INT32_MIN && result <= INT32_MAX);
  }
  if (!byte96) {
    result -= 250;
    assert(result >= INT32_MIN && result <= INT32_MAX);
  }
  return ambience / 2 > result ? ambience / 2 : static_cast<i32>(result);
}

// Refresh the active disaster sound and propagate its calculated volume to the configured dependent effect.
void AuDisasterEffect::Refresh() {
  if (!byte88) {
    if (!byte96)
      return;
    Play(0);
  }
  i32 selected = CalculateVolume();
  if (sound) {
    if (selected) {
      if (sound->IsPlaying())
        sound->SetVolume(selected);
      else
        sound->Play(selected, 1, static_cast<AuSoundDuplication>(2));
    } else {
      sound->Stop();
    }
  }
  if (value80)
    manager->effects[value80]->UnresolvedSlot56(0, static_cast<u32>(selected));
}

// Copy both fading-effect paths and initialize the supplied flags, volume and original zeroed state fields.
AuFadeEffect::AuFadeEffect(u32 id, u32 type, u8 direct, u8 initial104, u32 initialVolume,
    u8 initial116, u8 initial117, const ScStringView *firstPath, const ScStringView *secondPath)
    : AuAudioEffect(id, type, direct, 0), first(*firstPath), second(*secondPath), firstSound(nullptr),
      secondSound(nullptr), byte104(initial104), unknown108(0), unknown112(0), byte116(initial116),
      byte117(initial117) {
  volume = initialVolume;
}

AuFadeEffect::~AuFadeEffect() {
}

// Publish the fading effect for the common effect or unknown identity and retain it on success.
bool AuFadeEffect::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuFadeEffect);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuFadeEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuFadeEffect::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and delete the fading effect when its count reaches zero.
u32 AuFadeEffect::Release() {
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

// Create the configured fading sounds in order, preserving the original direct-path selection and resident loading.
u8 AuFadeEffect::Init(AuAudioManager *owner) {
  manager = owner;
  if (first.Strlen()) {
    ScString path;
    if (directPath) {
      path = first;
    } else {
      path = *AuGetAudioSoundDirectory();
      path.Append(first.Data(), first.Strlen());
    }
    firstSound = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(1));
  }
  if (second.Strlen()) {
    ScString path;
    if (directPath) {
      path = first;
    } else {
      path = *AuGetAudioSoundDirectory();
      path.Append(second.Data(), second.Strlen());
    }
    secondSound = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(0));
  }
  if (byte104) {
    if (firstSound)
      firstSound->Load();
    if (secondSound)
      secondSound->Load();
  }
  initialized = 1;
  return 1;
}

// Stop both fading sounds, unload resident data and release their owned references in the original order.
u8 AuFadeEffect::Shutdown() {
  if (firstSound)
    firstSound->Stop();
  if (secondSound)
    secondSound->Stop();
  if (byte104) {
    if (firstSound)
      firstSound->Unload();
    if (secondSound)
      secondSound->Unload();
  }
  ScRelease(&firstSound);
  ScRelease(&secondSound);
  initialized = 0;
  return 1;
}

u8 AuFadeEffect::Update() {
  if (!secondSound || (!unknown108 && !unknown112))
    return 1;
  if (unknown108 && byte116) {
    i64 target = static_cast<i64>(unknown108) - 70;
    assert(target >= INT32_MIN);
    unknown108 = target < 0 ? 0 : static_cast<i32>(target);
  }
  if ((unknown112 && unknown112 == unknown108 && secondSound->GetVolume() == unknown112 &&
          secondSound->IsPlaying()) ||
      (!unknown112 && !unknown108 && !secondSound->IsPlaying()))
    return 1;
  if (unknown112 != unknown108) {
    i64 next = static_cast<i64>(unknown112) + (unknown112 < unknown108 ? 70 : -70);
    assert(next >= INT32_MIN && next <= INT32_MAX);
    if ((unknown112 < unknown108 && next > unknown108) || (unknown112 > unknown108 && next < unknown108))
      unknown112 = unknown108;
    else
      unknown112 = static_cast<i32>(next);
  }
  if (unknown112) {
    if (secondSound) {
      if (secondSound->IsPlaying()) {
        i32 value = unknown112;
        if (secondSound->GetVolume() != value)
          secondSound->SetVolume(value);
      } else {
        secondSound->Play(unknown112, 1, static_cast<AuSoundDuplication>(2));
      }
    }
  } else {
    secondSound->Stop();
  }
  return 1;
}

i32 AuFadeEffect::CurrentVolume() {
  return byte117 ? manager->ambienceVolume : manager->effectsVolume;
}

u8 AuFadeEffect::Play(i32) {
  if (!unknown108) {
    if (firstSound)
      firstSound->Play(CurrentVolume(), 0, static_cast<AuSoundDuplication>(2));
    i64 scaled = 400ll * CurrentVolume();
    assert(scaled >= INT32_MIN && scaled <= INT32_MAX);
    unknown112 = static_cast<i32>(scaled) / 1024;
  }
  unknown108 = CurrentVolume();
  return 1;
}

u8 AuFadeEffect::IsPlaying(i32) {
  return unknown112 != 0;
}

void AuFadeEffect::UnresolvedSlot44() {
}

void AuFadeEffect::UnresolvedSlot48(uptr, uptr, uptr, uptr) {
}

u8 AuFadeEffect::UnresolvedSlot52(uptr) {
  return UnresolvedSlot84(0);
}

// Scale the second event payload by the selected manager volume and store the target fading level.
u8 AuFadeEffect::UnresolvedSlot56(uptr, uptr value) {
  i64 scaled = static_cast<i64>(static_cast<i32>(value)) * CurrentVolume();
  assert(scaled >= INT32_MIN && scaled <= INT32_MAX);
  unknown108 = static_cast<i32>(scaled) / 1024;
  return 1;
}

// Scale the second event payload by the selected manager volume and store the current fading level.
u8 AuFadeEffect::UnresolvedSlot60(uptr, uptr value) {
  i64 scaled = static_cast<i64>(static_cast<i32>(value)) * CurrentVolume();
  assert(scaled >= INT32_MIN && scaled <= INT32_MAX);
  unknown112 = static_cast<i32>(scaled) / 1024;
  return 1;
}

u8 AuFadeEffect::UnresolvedSlot64() {
  return 1;
}

u8 AuFadeEffect::LoadSound() {
  return 1;
}

u8 AuFadeEffect::UnloadSound() {
  return 1;
}

u8 AuFadeEffect::UnresolvedSlot80(u32, u32) {
  return 1;
}

// Clear both fading levels and stop each configured sound in source order.
u8 AuFadeEffect::UnresolvedSlot84(uptr) {
  unknown108 = 0;
  unknown112 = 0;
  if (firstSound)
    firstSound->Stop();
  if (secondSound)
    secondSound->Stop();
  return 1;
}

// The configured actor constructor retains this original opaque initial-state word; the default constructor uses zero.
// Its purpose and units remain unresolved.
enum AuActorInitialStateValue : u32 {
  kAuActorUnresolvedConfiguredInitialState = 100000,
};

AuActorEffect::AuActorEffect()
    : AuAudioEffect(), value56(0), firstSound(nullptr), secondSound(nullptr), thirdSound(nullptr), value132(0),
      unknown136(0), unknown140(0), unknown144(0), unknown148(0), unknown164(0), unknown176(0), unknown180(0) {
}

// Copy the actor's paths and initialize its state, zeroing unwritten history to avoid indeterminate reads.
AuActorEffect::AuActorEffect(u32 id, u32 type, u8 direct, u32 initial52, u32 initial56, const ScStringView *firstPath,
    const ScStringView *secondPath, const ScStringView *thirdPath)
    : AuAudioEffect(id, type, direct, initial52), value56(initial56), firstSound(nullptr), secondSound(nullptr),
      thirdSound(nullptr), first(*firstPath), second(*secondPath), third(*thirdPath),
      value132(kAuActorUnresolvedConfiguredInitialState), unknown136(0), unknown140(0), unknown144(0), unknown148(0),
      unknown152(0), unknown156(0), unknown160(0), unknown164(0), unknown176(0), unknown180(0) {
}

AuActorEffect::~AuActorEffect() {
}

// Publish the actor effect for the common effect or unknown identity and retain it on success.
bool AuActorEffect::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuActorEffect);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuActorEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuActorEffect::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and delete the actor effect when its count reaches zero.
u32 AuActorEffect::Release() {
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

AuSparseEffect::AuSparseEffect() : AuAudioEffect(), count(0), value60(0), byte320(0) {
  for (i32 i = 0; i < 8; ++i) {
    sounds[i] = nullptr;
    firstValues[i] = 0;
    secondValues[i] = 1024;
  }
}

AuSparseEffect::AuSparseEffect(u32 id, u32 type, u8 direct, i32 entryCount, const ScStringView *const *entryNames,
    const i32 *first, const i32 *second, u32 initial60, u32 initial324, u32 initial328, u32 initial332, u32 initial336)
    : AuAudioEffect(id, type, direct, 0), count(entryCount), value60(initial60), byte320(0), value324(initial324),
      value328(initial328), value332(initial332), value336(initial336) {
  assert(count <= 8);
  for (i32 i = 0; i < count; ++i) {
    names[i] = *entryNames[i];
    firstValues[i] = first[i];
    secondValues[i] = second[i];
  }
  for (i32 i = 0; i < 8; ++i)
    sounds[i] = nullptr;
}

AuSparseEffect::~AuSparseEffect() {
}

// Publish the sparse effect for the common effect or unknown identity and retain it on success.
bool AuSparseEffect::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuSparseEffect);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuSparseEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuSparseEffect::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

// Release the director for a held reference and delete the sparse effect when its count reaches zero.
u32 AuSparseEffect::Release() {
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

u8 AuSparseEffect::Init(AuAudioManager *owner) {
  initialized = 1;
  manager = owner;
  return 1;
}

// Stop and release each populated sparse sound in order, then clear initialization.
u8 AuSparseEffect::Shutdown() {
  for (i32 i = 0; i < count; ++i) {
    if (sounds[i]) {
      sounds[i]->Stop();
      sounds[i]->Release();
      sounds[i] = nullptr;
    }
  }
  initialized = 0;
  return 1;
}

// Retire completed sparse sounds, then create eligible random sounds within the configured date and view range.
u8 AuSparseEffect::Update() {
  for (i32 i = 0; i < count; ++i) {
    if (sounds[i] && !sounds[i]->IsPlaying()) {
      sounds[i]->Stop();
      sounds[i]->Release();
      sounds[i] = nullptr;
    }
  }
  if (!byte320 || (manager->viewZoom > manager->buildingAmbienceMinimumZoom && manager->viewMode))
    return 1;
  if (manager->audioInfo) {
    u32 dateFirst, dateSecond, dateThird;
    [[maybe_unused]] u8 dateAvailable = manager->audioInfo->GetCityDate(&dateFirst, &dateSecond, &dateThird);
    assert(dateAvailable);
    if (value324 < value332 || (value324 == value332 && value328 < value336)) {
      if (dateFirst < value324 || dateFirst > value332 || (dateFirst == value324 && dateSecond < value328) ||
          (dateFirst == value332 && dateSecond >= value336))
        return 1;
    } else if (value324 != value332) {
      if ((dateFirst > value332 || (dateFirst == value332 && dateSecond >= value336)) &&
          (dateFirst < value324 || (dateFirst == value324 && dateSecond < value328)))
        return 1;
    } else {
      if (dateFirst != value324 || (dateSecond < value328 && dateSecond >= value336))
        return 1;
    }
  }
  for (i32 i = 0; i < count; ++i) {
    u32 random = manager->random.RandomUint32Uniform();
    i64 threshold = static_cast<i64>(manager->buildingRandomMultiplier) * firstValues[i];
    assert(threshold >= INT32_MIN && threshold <= INT32_MAX);
    if (random < static_cast<u32>(static_cast<i32>(threshold)) && !sounds[i]) {
      ScString path;
      if (directPath) {
        path = names[i];
      } else {
        path = *AuGetAudioSoundDirectory();
        path.Append(names[i].Data(), names[i].Strlen());
      }
      sounds[i] = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(1), 0);
      i64 zoom = 5ll - manager->viewZoom;
      i64 attenuation = static_cast<i64>(manager->buildingAmbienceAttenuation) * zoom;
      i64 soundVolume = static_cast<i64>(manager->ambienceVolume) - attenuation;
      assert(zoom >= INT32_MIN && zoom <= INT32_MAX && attenuation >= INT32_MIN && attenuation <= INT32_MAX &&
          soundVolume >= INT32_MIN && soundVolume <= INT32_MAX);
      if (soundVolume > 300)
        sounds[i]->Play(static_cast<i32>(soundVolume), 0, static_cast<AuSoundDuplication>(1));
    }
  }
  return 1;
}

u8 AuSparseEffect::Play(i32) {
  byte320 = 1;
  return 1;
}

u8 AuSparseEffect::IsPlaying(i32) {
  return byte320;
}

void AuSparseEffect::UnresolvedSlot44() {
}

void AuSparseEffect::UnresolvedSlot48(uptr, uptr, uptr, uptr) {
}

u8 AuSparseEffect::UnresolvedSlot52(uptr) {
  byte320 = 0;
  return 1;
}

// Play the first sparse sound, lazily creating it and using the manager volume when the event supplies zero.
u8 AuSparseEffect::UnresolvedSlot56(uptr, uptr value) {
  i32 selected = static_cast<i32>(value);
  if (!selected)
    selected = manager->effectsVolume;
  if (!selected)
    return 1;
  ScString path;
  if (directPath) {
    path = names[0];
  } else {
    path = *AuGetAudioSoundDirectory();
    path.Append(names[0].Data(), names[0].Strlen());
  }
  if (!sounds[0])
    sounds[0] = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(1));
  sounds[0]->Play(selected, 0, static_cast<AuSoundDuplication>(1));
  return 1;
}

u8 AuSparseEffect::UnresolvedSlot60(uptr, uptr) {
  return 1;
}

u8 AuSparseEffect::UnresolvedSlot64() {
  return 1;
}

u8 AuSparseEffect::LoadSound() {
  return 0;
}

u8 AuSparseEffect::UnloadSound() {
  return 0;
}

u8 AuSparseEffect::UnresolvedSlot80(u32, u32) {
  return 1;
}

// Stop and release each sparse sound, then mark the effect as stopped.
// The original raw-freed sound storage; releasing the retained interface avoids dangling live references.
u8 AuSparseEffect::UnresolvedSlot84(uptr) {
  for (i32 i = 0; i < count; ++i) {
    if (sounds[i]) {
      sounds[i]->Stop();
      sounds[i]->Release();
      sounds[i] = nullptr;
    }
  }
  byte320 = 1;
  return 1;
}

// Create the actor's configured 3D sounds in order and apply the manager's distance settings to each one.
u8 AuActorEffect::Init(AuAudioManager *owner) {
  manager = owner;
  if (first.Strlen()) {
    ScString path;
    if (directPath) {
      path = first;
    } else {
      path = *AuGetAudioSoundDirectory();
      path.Append(first.Data(), first.Strlen());
    }
    firstSound = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(0), 0);
    firstSound->SetMinimumDistance(static_cast<f32>(manager->vehicleMinimumDistance));
    firstSound->SetMaximumDistance(10000.0f);
  }
  if (second.Strlen()) {
    ScString path;
    if (directPath) {
      path = second;
    } else {
      path = *AuGetAudioSoundDirectory();
      path.Append(second.Data(), second.Strlen());
    }
    secondSound = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(1), 0);
    secondSound->SetMinimumDistance(static_cast<f32>(manager->vehicleMinimumDistance));
    secondSound->SetMaximumDistance(10000.0f);
  }
  if (third.Strlen()) {
    ScString path;
    if (directPath) {
      path = third;
    } else {
      path = *AuGetAudioSoundDirectory();
      path.Append(third.Data(), third.Strlen());
    }
    thirdSound = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(1), 0);
    thirdSound->SetMinimumDistance(static_cast<f32>(manager->vehicleMinimumDistance));
    thirdSound->SetMaximumDistance(10000.0f);
  }
  initialized = 1;
  return 1;
}

// Stop, shut down and release each owned 3D sound in source order, then clear initialization.
u8 AuActorEffect::Shutdown() {
  if (firstSound) {
    firstSound->Stop();
    firstSound->Shutdown();
    firstSound->Release();
    firstSound = nullptr;
  }
  if (secondSound) {
    secondSound->Stop();
    secondSound->Shutdown();
    secondSound->Release();
    secondSound = nullptr;
  }
  if (thirdSound) {
    thirdSound->Stop();
    thirdSound->Shutdown();
    thirdSound->Release();
    thirdSound = nullptr;
  }
  initialized = 0;
  return 1;
}

// Borrow the current city window from the AUDIO application's game interface.
static SpCityWindowView *AuGetActorWindow() {
  ApGameInterface *application = AuGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the current view from the current city window when one exists.
static SpViewInterface *AuGetActorView() {
  SpCityWindowView *window = AuGetActorWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the current isometric view's sprite-cell map without acquiring another reference.
static SpCellMap *AuGetActorCellMap() {
  SpViewInterface *view = AuGetActorView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteCellMap() : nullptr;
}

// Project a world position into the current view and check whether it lies inside the screen clip rectangle.
static bool AuActorVisible(i32 x, i32 y, i32 z) {
  SpCellMap *map = AuGetActorCellMap();
  assert(map);
  i32 cityX = 0, cityY = 0, screenX = 0, screenY = 0;
  map->MicroActualGridToCityPixel_NonInline(x, z, y, &cityX, &cityY);
  map->CityPixelToWindowPixel_NonInline(cityX, cityY, &screenX, &screenY);
  SpViewInterface *view = AuGetActorView();
  assert(view);
  ScRect clip;
  [[maybe_unused]] bool clipAvailable = view->GetScreenClipRect(&clip);
  assert(clipAvailable);
  return screenX >= clip.left && screenX <= clip.right && screenY >= clip.top && screenY <= clip.bottom;
}

// Compute the actor's audible target volume from its visible position and current view settings.
i32 AuActorEffect::CalculateActorVolume(i32 x, i32 y, i32 z) {
  if (manager->viewZoom < 4 || manager->viewMode || !AuActorVisible(x, y, z) ||
      (value56 == 1 && static_cast<i32>(unknown164) < 100))
    return 0;
  i64 zoom = 5 - static_cast<i64>(manager->viewZoom);
  i64 attenuation = static_cast<i64>(manager->vehicleAttenuation) * zoom;
  i64 volumeValue = static_cast<i64>(manager->effectsVolume) - attenuation;
  assert(zoom >= INT32_MIN && zoom <= INT32_MAX && attenuation >= INT32_MIN && attenuation <= INT32_MAX &&
      volumeValue >= INT32_MIN && volumeValue <= INT32_MAX);
  return volumeValue < 0 ? 0 : static_cast<i32>(volumeValue);
}

// Move the active actor sound toward its visible target volume, starting or stopping playback at the transitions.
u8 AuActorEffect::Update() {
  if (!initialized || !firstSound)
    return 0;
  unknown176 = unknown136 ? CalculateActorVolume(static_cast<i32>(unknown152), static_cast<i32>(unknown156),
      static_cast<i32>(unknown160)) : 0;
  i32 previous = static_cast<i32>(unknown180), target = static_cast<i32>(unknown176);
  if (previous != target || (firstSound->IsPlaying() && firstSound->GetVolume() != previous)) {
    if (!previous)
      unknown180 = target > 300 ? 300 : target;
    i32 current = static_cast<i32>(unknown180);
    if (current < target) {
      i64 next = static_cast<i64>(current) + 50;
      assert(next <= INT32_MAX);
      unknown180 = target < next ? target : static_cast<i32>(next);
    } else if (current > target) {
      i64 next = static_cast<i64>(current) - 50;
      assert(next >= INT32_MIN);
      unknown180 = target > next ? target : static_cast<i32>(next);
    }
    if (unknown180) {
      if (previous)
        firstSound->SetVolume(static_cast<i32>(unknown180));
      else
        firstSound->Play(static_cast<i32>(unknown180), 1, static_cast<AuSoundDuplication>(1));
    } else {
      if (unknown136) {
        u32 actor = unknown136;
        unknown176 = 0;
        unknown140 = 0;
        ClearActor(actor);
      }
      if (firstSound)
        firstSound->Stop();
    }
  }
  return 1;
}

u8 AuActorEffect::Play(i32) {
  unknown148 = 0;
  return 1;
}

u8 AuActorEffect::IsPlaying(i32) {
  return firstSound->IsPlaying();
}

void AuActorEffect::UnresolvedSlot44() {
  if (manager->listener.zoom < 4)
    UnresolvedSlot84(0);
}

// Track an audible actor, project its world position and update its spatial sound position for the current rotation.
void AuActorEffect::UnresolvedSlot48(uptr actorValue, uptr xValue, uptr, uptr zValue) {
  u32 actor = static_cast<u32>(actorValue);
  i32 x = static_cast<i32>(xValue), z = static_cast<i32>(zValue);
  if (!manager->effectsVolume || !firstSound)
    return;
  if (manager->listener.zoom < 4) {
    UnresolvedSlot84(actor);
    return;
  }
  if ((!value56 && !unknown144) || (unknown136 && unknown136 != actor))
    return;
  unknown156 = unknown148;
  unknown152 = static_cast<u32>(x);
  unknown160 = static_cast<u32>(z);
  assert(manager->audioInfo);
  i32 ground = manager->audioInfo->TerrainHeight(x, z);
  if (unknown156) {
    i64 difference = static_cast<i64>(static_cast<i32>(unknown156)) - ground;
    assert(difference >= INT32_MIN && difference <= INT32_MAX);
    unknown164 = static_cast<u32>(static_cast<i32>(difference));
  } else {
    unknown156 = static_cast<u32>(ground);
  }
  i32 target = CalculateActorVolume(x, static_cast<i32>(unknown156), z);
  if (!target) {
    if (unknown136) {
      unknown176 = 0;
      unknown140 = 0;
      ClearActor(actor);
    }
    return;
  }
  if (!unknown136)
    SetActor(actor);
  unknown140 = 2;
  unknown176 = static_cast<u32>(target);
  SpCellMap *map = AuGetActorCellMap();
  SpViewInterface *view = AuGetActorView();
  assert(view);
  ScRect clip;
  [[maybe_unused]] bool clipAvailable = view->GetScreenClipRect(&clip);
  assert(clipAvailable);
  i64 width = static_cast<i64>(clip.right) - clip.left, height = static_cast<i64>(clip.bottom) - clip.top;
  assert(width >= INT32_MIN && width <= INT32_MAX && height >= INT32_MIN && height <= INT32_MAX);
  i32 centerX = static_cast<i32>(width) / 2, centerY = static_cast<i32>(height) / 2;
  i32 cityX = 0, cityY = 0, screenX = 0, screenY = 0;
  assert(map);
  map->MicroActualGridToCityPixel_NonInline(x, z, static_cast<i32>(unknown156), &cityX, &cityY);
  map->CityPixelToWindowPixel_NonInline(cityX, cityY, &screenX, &screenY);
  unknown168 = static_cast<u32>(screenX);
  unknown172 = static_cast<u32>(screenY);
  i64 dx = 0, dz = 0;
  switch (manager->viewRotation) {
  case 0:
    dx = static_cast<i64>(screenX) - centerX;
    dz = static_cast<i64>(screenY) - centerY;
    break;
  case 1:
    dx = static_cast<i64>(centerY) - screenY;
    dz = static_cast<i64>(screenX) - centerX;
    break;
  case 2:
    dx = static_cast<i64>(centerX) - screenX;
    dz = static_cast<i64>(centerY) - screenY;
    break;
  case 3:
    dx = static_cast<i64>(screenY) - centerY;
    dz = static_cast<i64>(centerX) - screenX;
    break;
  }
  assert(dx >= INT32_MIN && dx <= INT32_MAX && dz >= INT32_MIN && dz <= INT32_MAX);
  firstSound->SetPosition(manager->listener.positionX + static_cast<f32>(static_cast<i32>(dx) / 10), 0.0f,
      manager->listener.positionZ + static_cast<f32>(static_cast<i32>(dz) / 10));
}

void AuActorEffect::ClearActor(u32) {
  unknown136 = 0;
}

u32 AuActorEffect::SetActor(u32 actor) {
  unknown136 = actor;
  return actor;
}

// Clear the current actor when the supplied identifier matches, or when zero selects the current actor.
u8 AuActorEffect::UnresolvedSlot52(uptr value) {
  u32 actor = static_cast<u32>(value);
  if (actor) {
    if (actor == unknown136) {
      unknown176 = 0;
      ClearActor(actor);
    }
  } else {
    actor = unknown136;
    unknown176 = 0;
    if (actor)
      ClearActor(actor);
  }
  return 1;
}

// Update the actor's value only for the current identifier and clear that identifier when the value becomes zero.
u8 AuActorEffect::UnresolvedSlot56(uptr firstValue, uptr secondValue) {
  u32 actor = static_cast<u32>(firstValue), value = static_cast<u32>(secondValue);
  if (actor == unknown136) {
    unknown176 = 0;
    unknown144 = value;
    if (!value)
      ClearActor(actor);
  }
  return 1;
}

// Occasionally select a vehicle horn and play it near the listener, or record the supplied height for type one.
u8 AuActorEffect::UnresolvedSlot60(uptr, uptr secondValue) {
  if (!value56) {
    u32 divisor = static_cast<u32>(manager->vehicleHonkDivisor);
    if (!divisor || manager->random.RandomUint32Uniform() <= UINT32_MAX / divisor) {
      if (thirdSound && !thirdSound->IsPlaying()) {
        i64 zoom = 5 - static_cast<i64>(manager->viewZoom);
        i64 attenuation = static_cast<i64>(manager->honkAttenuation) * zoom;
        i64 adjusted = static_cast<i64>(manager->effectsVolume) - attenuation;
        assert(zoom >= INT32_MIN && zoom <= INT32_MAX && attenuation >= INT32_MIN && attenuation <= INT32_MAX &&
            adjusted >= INT32_MIN && adjusted <= INT32_MAX);
        i32 hornVolume = static_cast<i32>(adjusted);
        u32 choice = manager->random.RandomUint32Uniform() & 7;
        if (hornVolume > 300) {
          static const char *const names[8] = {"sfx\\vehicles\\carhrn1.xa", "sfx\\vehicles\\carhrn2.xa",
              "sfx\\vehicles\\carhrn3.xa", "sfx\\vehicles\\carhrn4.xa", "sfx\\vehicles\\carhrn5.xa",
              "sfx\\vehicles\\carhrn6.xa", "sfx\\vehicles\\carhrn7.xa", "sfx\\vehicles\\carhrn8.xa"};
          ScString path;
          if (!directPath)
            path = *AuGetAudioSoundDirectory();
          path.Append(names[choice]);
          u8 loaded = thirdSound->SetSoundFile(path, static_cast<AuStreamingType>(2));
          thirdSound->SetPosition(manager->listener.positionX, 0.0f, manager->listener.positionZ);
          if (loaded) {
            thirdSound->Play(hornVolume, 0, static_cast<AuSoundDuplication>(1));
            if (manager->random.RandomUint32Uniform() & 1)
              thirdSound->SetEffectsLevel(manager->vehicleEffectsLevel);
          }
        }
      }
    }
  }
  if (value56 == 1)
    unknown148 = static_cast<u32>(secondValue);
  return 1;
}

u8 AuActorEffect::UnresolvedSlot64() {
  return 1;
}

u8 AuActorEffect::LoadSound() {
  firstSound->Load();
  return 1;
}

u8 AuActorEffect::UnloadSound() {
  firstSound->Unload();
  return 1;
}

u8 AuActorEffect::UnresolvedSlot80(u32, u32) {
  return 1;
}

// Stop and clear the current actor only when a nonzero identifier matches it.
u8 AuActorEffect::UnresolvedSlot84(uptr value) {
  u32 actor = static_cast<u32>(value);
  if (!actor)
    return 0;
  if (actor == unknown136) {
    unknown176 = 0;
    unknown180 = 0;
    if (firstSound)
      firstSound->Stop();
    unknown136 = 0;
  }
  return 1;
}

// Publish the one-shot effect or unknown identity and retain the object only for a recognized interface.
bool AuSingleEffect::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuSingleEffect);
  HANDLE_QUERY_INTERFACE(IID_AuAudioEffect, AuSingleEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuSingleEffect::AddRef() {
  ++references;
  AuGetAudioDirector()->AddRef();
  return references;
}

u32 AuSingleEffect::Release() {
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

u32 AuAudioEffect::ReleaseBaseReference() {
  if (baseReferences) {
    AuGetAudioDirector()->RemoveRef();
    --baseReferences;
  }
  return baseReferences;
}

u32 AuAudioEffect::GetBaseReferenceCount() {
  return baseReferences;
}

u8 AuAudioEffect::Init(AuAudioManager *owner) {
  manager = owner;
  return 1;
}

u8 AuAudioEffect::Shutdown() {
  return 1;
}

// Create the configured sound from its direct path or the AUDIO sound directory and preload resident sounds.
u8 AuSingleEffect::Init(AuAudioManager *owner) {
  manager = owner;
  ScString path;
  if (directPath) {
    path = filename;
  } else {
    path = *AuGetAudioSoundDirectory();
    path.Append(filename.Data(), filename.Strlen());
  }
  sound = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(factoryFlag != 0));
  if (resident)
    sound->Load();
  initialized = 1;
  return 1;
}

u8 AuSingleEffect::Shutdown() {
  if (sound) {
    if (resident)
      sound->Unload();
    sound->Release();
    sound = nullptr;
  }
  initialized = 0;
  return 1;
}

u8 AuSingleEffect::Update() {
  return 1;
}

u8 AuSingleEffect::Play(i32) {
  i64 scaled = static_cast<i64>(static_cast<i32>(volume)) * manager->effectsVolume;
  assert(scaled >= INT32_MIN && scaled <= INT32_MAX);
  sound->Play(static_cast<i32>(scaled) / 1024, 0, static_cast<AuSoundDuplication>(playFlags ? playFlags : 2));
  return 1;
}

u8 AuSingleEffect::IsPlaying(i32) {
  return sound->IsPlaying();
}

void AuSingleEffect::UnresolvedSlot44() {
}

void AuSingleEffect::UnresolvedSlot48(uptr, uptr, uptr, uptr) {
}

u8 AuSingleEffect::UnresolvedSlot52(uptr) {
  return 1;
}

u8 AuSingleEffect::UnresolvedSlot56(uptr, uptr) {
  return 0;
}

u8 AuSingleEffect::UnresolvedSlot60(uptr, uptr) {
  return 0;
}

u8 AuSingleEffect::UnresolvedSlot64() {
  return 1;
}

u8 AuAudioEffect::GetFlag21() {
  return byte21;
}

u8 AuSingleEffect::LoadSound() {
  if (resident)
    return 0;
  sound->Load();
  return 1;
}

u8 AuSingleEffect::UnloadSound() {
  if (resident)
    return 0;
  sound->Unload();
  return 1;
}

u8 AuSingleEffect::UnresolvedSlot80(u32, u32) {
  return 1;
}

u8 AuSingleEffect::UnresolvedSlot84(uptr) {
  return 1;
}

u32 AuAudioEffect::GetValue32() {
  return unknown32;
}

// Delete the complete effect through its virtual destructor and return the original receiver address.
AuAudioEffect *AuAudioEffect::Destroy() {
  AuAudioEffect *receiver = this;
  delete this;
  return receiver;
}

u8 AuAudioEffect::ConfigureBase(AuAudioManager *owner, u32 id, u32 type) {
  manager = owner;
  identifier = id;
  category = type;
  return 1;
}

u8 AuAudioEffect::SetBaseVolume(u32 value, u32) {
  volume = value;
  return 1;
}

// Replace the one-shot filename and playback settings before invoking its virtual initialization hook.
u8 AuSingleEffect::Configure(AuAudioManager *owner, u32, u32, const ScStringView *name,
    u8 keepResident, u8 useFactory, u32 initialVolume) {
  filename = *name;
  resident = keepResident;
  factoryFlag = useFactory;
  volume = initialVolume;
  return Init(owner);
}

// Borrow the language service from the AUDIO director, caching its interface after releasing the acquired reference.
static ScLanguageManager *AuGetAudioLanguage() {
  if (!AuAudioLanguage) {
    ScLanguageManager *language = nullptr;
    AuGetAudioDirector()->FrameWork()->GetSystemService(
        kScLanguageManagerServiceId, IID_ScLanguageManager, reinterpret_cast<void **>(&language));
    AuAudioLanguage = language;
    if (language)
      language->Release();
  }
  return AuAudioLanguage;
}

// Accept unfiltered sounds and select the country-specific entries used by the original AUDIO configuration.
static u8 SC3_STDCALL AuSoundCountryMatches(u32 selector) {
  if (!selector)
    return 1;
  ScLanguageManager *language = AuGetAudioLanguage();
  assert(language);
  u32 country = language->GetCurrentCountry();
  return (country == 8 || country == 54) ? selector == 1 : selector != 1;
}

// Register one 1ShotSfx entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterSingleSound(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("1ShotSfx", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Register one Sparse entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterSparseSound(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("Sparse", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Register one Disaster entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterDisasterSound(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("Disaster", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Register one Fading entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterFadingSound(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("Fading", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Register one Actor entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterActorSound(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("Actor", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Register one Music entry from the original AUDIO configuration in the shared registry.
void SC3_CDECL AuRegisterMusic(const ScString &key, const ScString &value, ApIniContext *) {
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  {
    ScString name;
    registry->MakeRegistryName("Music", key, value, &name);
    registry->Register(name);
  }
  registry->Release();
}

// Parse a one-shot registry entry, apply its country filter and replace the corresponding manager effect.
void SC3_CDECL AuLoadSingleSound(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseSingleSound(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseSingleSoundImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseSingleSound(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseSingleSoundImpl(this, keyCopy, valueCopy);
}

// Parse the one-shot fields and defer reader access until the ID and country filter pass.
void AuAudioIni::ParseSingleSoundImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, second, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data());
  if (!identifier || identifier > 512)
    return;
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  u32 fields[6];
  for (int field = 0; field < 6; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  token = parserValue.SplitToken("\"");
  second = parserValue.SplitToken("\"");
  if (parserValue.Strlen()) {
    parserValue.SplitToken(",");
    u32 filter = ScConvertToUint32(parserValue.Data());
    if (!AuSoundCountryMatches(filter))
      return;
  }
  if (identifier >= 512)
    return;
  assert(reader);
  AuAudioManager *owner = reader->manager;
  if (owner->effects[identifier]) {
    delete owner->effects[identifier];
    owner->effects[identifier] = nullptr;
  }
  owner->effects[identifier] = new AuSingleEffect(identifier, fields[0], fields[5] != 0, &second,
      fields[2] == 1, fields[3] == 1, fields[1], fields[4]);
}

// Parse an actor sound entry, apply its country restriction and replace the matching manager effect.
void SC3_CDECL AuLoadActorSound(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseActorSound(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseActorSoundImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseActorSound(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseActorSoundImpl(this, keyCopy, valueCopy);
}

// Parse actor paths and settings, then access the reader only after ID and country checks.
void AuAudioIni::ParseActorSoundImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, second, third, fourth, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data());
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken(",");
  u32 category = ScConvertToUint32(token.Data());
  token = parserValue.SplitToken(",");
  ScConvertToUint32(token.Data());
  token = parserValue.SplitToken(",");
  u32 filter = ScConvertToUint32(token.Data());
  u32 fields[2];
  for (int field = 0; field < 2; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  token = parserValue.SplitToken("\"");
  second = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken("\"");
  third = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken("\"");
  fourth = parserValue.SplitToken("\"");
  if (!identifier || identifier >= 512)
    return;
  if (!AuSoundCountryMatches(filter))
    return;
  assert(reader);
  AuAudioManager *owner = reader->manager;
  if (owner->effects[identifier]) {
    delete owner->effects[identifier];
    owner->effects[identifier] = nullptr;
  }
  owner->effects[identifier] = new AuActorEffect(identifier, category, fields[1] != 0, filter, fields[0],
      &second, &third, &fourth);
}

// Parse a disaster sound entry and replace the corresponding effect after validating its identifier.
void SC3_CDECL AuLoadDisasterSound(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseDisasterSound(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseDisasterSoundImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseDisasterSound(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseDisasterSoundImpl(this, keyCopy, valueCopy);
}

// Parse disaster settings before requiring a reader to install a valid effect.
void AuAudioIni::ParseDisasterSoundImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, second, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data());
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  u32 fields[4];
  for (int field = 0; field < 4; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  token = parserValue.SplitToken("\"");
  second = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  u32 linked = ScConvertToUint32(parserValue.Data());
  if (!identifier || identifier >= 512)
    return;
  assert(reader);
  AuAudioManager *owner = reader->manager;
  if (owner->effects[identifier]) {
    delete owner->effects[identifier];
    owner->effects[identifier] = nullptr;
  }
  owner->effects[identifier] = new AuDisasterEffect(identifier, fields[0], fields[3] != 0, &second,
      fields[2] == 1, fields[1], linked);
}

void SC3_CDECL AuLoadFadingSound(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseFadingSound(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseFadingSoundImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseFadingSound(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseFadingSoundImpl(this, keyCopy, valueCopy);
}

// Parse both fading paths before requiring a reader to install a valid effect.
void AuAudioIni::ParseFadingSoundImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, second, third, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data());
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  u32 fields[6];
  for (int field = 0; field < 6; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  token = parserValue.SplitToken("\"");
  second = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken("\"");
  third = parserValue.SplitToken("\"");
  if (!identifier || identifier >= 512)
    return;
  AuFadeEffect *effect = new AuFadeEffect(identifier, fields[0], fields[5] != 0, fields[2] == 1, fields[1],
      fields[3] == 1, fields[4] == 1, &second, &third);
  assert(reader);
  AuAudioManager *owner = reader->manager;
  if (owner->effects[identifier]) {
    delete owner->effects[identifier];
    owner->effects[identifier] = nullptr;
  }
  owner->effects[identifier] = effect;
}

// Parse sparse sound entries, replace the matching manager effect and release the temporary names after installation.
void SC3_CDECL AuLoadSparseSound(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseSparseSound(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseSparseSoundImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseSparseSound(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseSparseSoundImpl(this, keyCopy, valueCopy);
}

// Parse sparse entries and date fields before requiring a reader for a valid effect.
void AuAudioIni::ParseSparseSoundImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data()), fields[4];
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  for (int field = 0; field < 4; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  u32 entryCount = fields[2];
  token = parserValue.SplitToken(",");
  i32 firstDate, secondDate, thirdDate, fourthDate;
  [[maybe_unused]] i32 parsedDates = 4;
  if (token.Strlen() == 1 && token.Data()[0] == '0') {
    firstDate = 1;
    secondDate = 1;
    thirdDate = 12;
    fourthDate = 31;
  } else
    parsedDates = sscanf(token.Data(), "%2d%2d-%2d%2d", &firstDate, &secondDate, &thirdDate, &fourthDate);
  assert(entryCount <= 32);
  ScString **entries = new ScString *[entryCount];
  const ScStringView *names[32];
  i32 firstValues[32], secondValues[32];
  for (u32 index = 0; index < entryCount; ++index) {
    entries[index] = new ScString();
    token = parserValue.SplitToken("\"");
    *entries[index] = parserValue.SplitToken("\"");
    token = parserValue.SplitToken(",");
    token = parserValue.SplitToken(",");
    secondValues[index] = static_cast<i32>(ScConvertToUint32(token.Data()));
    if (index + 1 == entryCount)
      firstValues[index] = static_cast<i32>(ScConvertToUint32(parserValue.Data()));
    else {
      token = parserValue.SplitToken(",");
      firstValues[index] = static_cast<i32>(ScConvertToUint32(token.Data()));
    }
    names[index] = entries[index];
  }
  // The original invalid-ID path leaves the entry names and pointer array allocated.
  if (!identifier || identifier >= 512)
    return;
  assert(parsedDates == 4);
  assert(reader);
  AuAudioManager *owner = reader->manager;
  if (owner->effects[identifier]) {
    delete owner->effects[identifier];
    owner->effects[identifier] = nullptr;
  }
  owner->effects[identifier] = new AuSparseEffect(identifier, fields[0], fields[3] != 0,
      static_cast<i32>(entryCount), names, firstValues, secondValues, fields[1], static_cast<u32>(firstDate),
      static_cast<u32>(secondDate), static_cast<u32>(thirdDate), static_cast<u32>(fourthDate));
  for (u32 index = 0; index < entryCount; ++index)
    delete entries[index];
  delete[] entries;
}

// Copy the registry entry and pass it to the reader's original music parser.
void SC3_CDECL AuLoadMusic(const ScStringView &key, const ScStringView &value, void *context) {
  ScString valueCopy(value);
  ScString keyCopy(key);
  AuAudioIni *reader = static_cast<AuAudioIni *>(context);
  if (reader)
    reader->ParseMusic(keyCopy, valueCopy, reader);
  else
    AuAudioIni::ParseMusicImpl(nullptr, keyCopy, valueCopy);
}

void AuAudioIni::ParseMusic(const ScString &keyCopy, const ScString &valueCopy, AuAudioIni *) {
  ParseMusicImpl(this, keyCopy, valueCopy);
}

// Parse the song settings before accessing the reader and its music player.
void AuAudioIni::ParseMusicImpl(AuAudioIni *reader, const ScString &keyCopy, const ScString &valueCopy) {
  ScString first, second, third, token;
  ScString parserKey(keyCopy), parserValue(valueCopy);
  u32 identifier = ScConvertToUint32(parserKey.Data());
  token = parserValue.SplitToken("\"");
  first = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken(",");
  u32 type = ScConvertToUint32(token.Data());
  token = parserValue.SplitToken("\"");
  second = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  token = parserValue.SplitToken("\"");
  third = parserValue.SplitToken("\"");
  token = parserValue.SplitToken(",");
  u32 fields[3];
  for (int field = 0; field < 3; ++field) {
    token = parserValue.SplitToken(",");
    fields[field] = ScConvertToUint32(token.Data());
  }
  u32 enabled = ScConvertToUint32(parserValue.Data());
  assert(reader);
  AuAudioManager *owner = reader->manager;
  AuMusicPlayer *player = owner->musicPlayer;
  AuMusicSong *song;
  if (type)
    song = new AuStreamMusicSong;
  else
    song = new AuFileMusicSong;
  if (!song->Init(identifier, &first, &second, &third, fields[0] == 1, fields[1] == 1, player)) {
    song->Shutdown();
    delete song;
    return;
  }
  song->SetEnabled(enabled == 1);
  player->AddSong(song);
}

AuAudioIni::AuAudioIni(const ScString &path, AuAudioManager *owner) : ApIniFile(path), manager(owner) {
}

AuAudioIni::~AuAudioIni() {
}

// Register each original INI section, then enumerate its registry category in the installed order.
u8 AuAudioIni::LoadAudio() {
  ApIniContext *context = reinterpret_cast<ApIniContext *>(this);
  EnumEntries(ScString("1ShotSfx"), AuRegisterSingleSound, context);
  EnumEntries(ScString("Sparse"), AuRegisterSparseSound, context);
  EnumEntries(ScString("Disaster"), AuRegisterDisasterSound, context);
  EnumEntries(ScString("Fading"), AuRegisterFadingSound, context);
  EnumEntries(ScString("Actor"), AuRegisterActorSound, context);
  EnumEntries(ScString("Music"), AuRegisterMusic, context);
  ScRegistry *registry = nullptr;
  AuGetAudioDirector()->FrameWork()->GetSystemService(
      kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  assert(registry);
  registry->EnumCategory("1ShotSfx", AuLoadSingleSound, this);
  registry->EnumCategory("Sparse", AuLoadSparseSound, this);
  registry->EnumCategory("Disaster", AuLoadDisasterSound, this);
  registry->EnumCategory("Fading", AuLoadFadingSound, this);
  registry->EnumCategory("Actor", AuLoadActorSound, this);
  bool result = registry->EnumCategory("Music", AuLoadMusic, this);
  registry->Release();
  return result;
}

AuBuildingSounds::AuBuildingSounds() : initialized(0), manager(nullptr) {
}

void AuBuildingSounds::Init(AuAudioManager *owner) {
  if (!initialized) {
    manager = owner;
    initialized = 1;
    current = 0;
    for (u32 i = 0; i < 4; ++i) {
      sounds[i] = nullptr;
      identifiers[i] = -1;
    }
  }
}

void AuBuildingSounds::Shutdown() {
  if (initialized) {
    initialized = 0;
    for (u32 i = 0; i < 4; ++i) {
      sounds[i] = nullptr;
      identifiers[i] = -1;
    }
  }
}

// Retire each stopped building sound and clear its corresponding identifier.
void AuBuildingSounds::Update() {
  if (initialized) {
    for (u32 i = 0; i < 4; ++i) {
      if (sounds[i] && !sounds[i]->IsPlaying()) {
        sounds[i]->Shutdown();
        sounds[i]->Release();
        sounds[i] = nullptr;
        identifiers[i] = -1;
      }
    }
  }
}

i32 AuBuildingSounds::NextSlot() {
  if (++current >= 4)
    current = 0;
  if (sounds[current]) {
    sounds[current]->Shutdown();
    sounds[current]->Release();
    sounds[current] = nullptr;
    identifiers[current] = -1;
  }
  return current;
}

// Choose a building sound for the selected cell, position spatial sounds around the listener and start playback.
void AuBuildingSounds::Play(i32 x, i32 z) {
  i32 playbackVolume = manager->ambienceVolume;
  if (!manager->IsAudibleCell(x, z)) {
    i64 reduced = static_cast<i64>(playbackVolume) - 300;
    assert(reduced >= INT32_MIN);
    playbackVolume = static_cast<i32>(reduced);
  }
  if (playbackVolume < 0)
    playbackVolume = 0;
  if (manager->viewMode)
    return;
  for (i32 index = 0; index < 4; ++index) {
    if (sounds[index] && sounds[index]->IsPlaying()) {
      [[maybe_unused]] i32 slot = identifiers[current];
      assert(slot >= 0 && slot < 4);
    }
  }
  [[maybe_unused]] i32 quadrant = current % 4;
  assert(quadrant >= 0 && quadrant < 4);
  i32 slot = NextSlot();
  assert(manager->audioInfo);
  i32 soundId = manager->audioInfo->GetCellInterfaceSoundId(x, z);
  bool spatial = true;
  if (soundId == 280 || soundId == 283 || soundId == 286) {
    spatial = false;
    soundId = 135 - (manager->random.RandomUint32Uniform(2) != 0);
  } else if (soundId == 281 || soundId == 282 || soundId == 284 || soundId == 285) {
    spatial = false;
    soundId = 76;
  } else {
    i32 height = manager->audioInfo->GetCellSpriteHeight(x, z);
    i32 group = height > 2500 ? 2 : (height > 513 ? 1 : 0);
    if (group == 2)
      spatial = false;
    static const i32 choices[36] = {103, 104, 105, 106, 107, 108, 103, 104, 105, 106, 107, 108,
        109, 110, 111, 112, 109, 110, 111, 112, 109, 110, 111, 112,
        134, 135, 134, 135, 134, 135, 134, 135, 134, 135, 134, 135};
    soundId = choices[12 * group + manager->random.RandomUint32Uniform(12)];
  }
  assert(dynamic_cast<AuSingleEffect *>(manager->effects[soundId]));
  AuSingleEffect *effect = static_cast<AuSingleEffect *>(manager->effects[soundId]);
  ScString path(*AuGetAudioSoundDirectory());
  path.Append(effect->filename.Data(), effect->filename.Strlen());
  assert(AuManagerSoundDevice);
  if (spatial) {
    AuSound3D *sound = AuManagerSoundDevice->SndCreate3D(&path, static_cast<AuStreamingType>(0), 1);
    sounds[slot] = sound;
    assert(sound);
    sound->SetMinimumDistance(100.0f);
    sound->SetMaximumDistance(100000.0f);
    f32 positionX = quadrant < 2 ? 100.0f : -100.0f;
    f32 positionZ = quadrant == 0 || quadrant == 3 ? 100.0f : -100.0f;
    sound->SetPosition(positionX, 0.0f, positionZ);
  } else {
    sounds[slot] = AuManagerSoundDevice->SndCreate(&path, static_cast<AuStreamingType>(0));
    assert(sounds[slot]);
  }
  sounds[slot]->Play(playbackVolume, 0, static_cast<AuSoundDuplication>(1));
}

AuBuildingAmbience::AuBuildingAmbience() : manager(nullptr) {
}

AuBuildingAmbience::~AuBuildingAmbience() {
}

u8 AuBuildingAmbience::SetManager(AuAudioManager *owner) {
  manager = owner;
  return 1;
}

// Stop tracked effects and scan the visible city area for distinct adjacent building sound identifiers.
u8 AuBuildingAmbience::Update() {
  AuInfo *info = manager->audioInfo;
  while (!soundIds.empty()) {
    i32 id = soundIds.front();
    assert(id >= 0 && id < 512);
    if (manager->effects[id])
      manager->effects[id]->UnresolvedSlot52(0);
    soundIds.pop_front();
  }
  if (manager->viewZoom < manager->buildingAmbienceMinimumZoom || manager->viewMode || manager->playbackState != 5)
    return 1;
  assert(info);
  if (info->DisasterCount())
    return 1;
  i64 radius = 3 * (6 - static_cast<i64>(manager->viewZoom));
  i64 left = static_cast<i64>(manager->viewCellX) - radius, right = static_cast<i64>(manager->viewCellX) + radius;
  i64 top = static_cast<i64>(manager->viewCellZ) - radius, bottom = static_cast<i64>(manager->viewCellZ) + radius;
  assert(radius >= INT32_MIN && radius <= INT32_MAX && left >= INT32_MIN && left <= INT32_MAX &&
      right >= INT32_MIN && right <= INT32_MAX && top >= INT32_MIN && top <= INT32_MAX && bottom >= INT32_MIN &&
      bottom <= INT32_MAX);
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right > manager->cityWidth)
    right = manager->cityWidth;
  if (bottom > manager->cityHeight)
    bottom = manager->cityHeight;
  i32 previous = 0;
  for (i32 x = static_cast<i32>(left); x < right; ++x) {
    for (i32 z = static_cast<i32>(top); z < bottom; ++z) {
      i32 id = info->GetBuildingSoundId(x, z);
      if (id && id != previous) {
        assert(id >= 0 && id < 512);
        if (manager->effects[id])
          manager->effects[id]->Play(manager->ambienceVolume);
        soundIds.push_back(id);
        previous = id;
      }
    }
  }
  return 1;
}
