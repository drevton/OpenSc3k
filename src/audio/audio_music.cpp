#include "audio/audio_music.h"

#include <cassert>
#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "audio/audio_core.h"
#include "audio/audio_freshness.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"

// Streamed songs use this original player duration, interpreted by the song update in milliseconds.
enum AuDefaultStreamTiming : u32 {
  kAuDefaultStreamedSongDurationMs = 0x2bf20,
};

// A failed song start records this elapsed-time marker before starting the selection timer.
enum AuMusicPlaybackTimeMarker : u32 {
  kAuMusicPlaybackStartFailedTime = 0xffffffff,
};

// The selection timestamp sentinel means no song-transition timer is pending.
enum AuMusicSelectionTime : u32 {
  kAuMusicSelectionTimerStopped = 0xffffffff,
};

AuMusicSong::AuMusicSong() : initialized(0), enabled(0), player(nullptr), byte72(0) {
}

// Copy the song identity and strings, borrow the music player and reset the original playback flags.
u8 AuMusicSong::Init(u32 id, const ScStringView *songName, const ScStringView *songPath,
    const ScStringView *songDescription, u8 firstFlag, u8 secondFlag, AuMusicPlayer *owner) {
  identifier = id;
  name = *songName;
  path = *songPath;
  description = *songDescription;
  byte72 = firstFlag;
  player = owner;
  byte80 = 0;
  byte81 = 0;
  value84 = 0;
  byte66 = secondFlag;
  return 1;
}

u8 AuMusicSong::IsInitialized() {
  return initialized;
}

u8 AuMusicSong::GetFlag66() {
  return byte66;
}

void AuMusicSong::Update() {
}

u8 AuMusicSong::GetName(ScString *value) {
  *value = name;
  return 1;
}

u32 AuMusicSong::GetId() {
  return identifier;
}

u8 AuMusicSong::UnresolvedSlot36() {
  return 1;
}

u8 AuMusicSong::SetEnabled(u8 value) {
  enabled = value;
  return value;
}

u8 AuMusicSong::IsEnabled() {
  return enabled;
}

u8 AuMusicSong::UnresolvedSlot72(u32) {
  return 1;
}

u8 AuMusicSong::UnresolvedSlot76(u32) {
  return 1;
}

u8 AuMusicSong::UnresolvedSlot80(u32, u32) {
  return 1;
}

void AuMusicSong::SetFlag80() {
  byte81 = 0;
  byte80 = 1;
}

void AuMusicSong::ClearFlag80() {
  byte80 = 0;
}

void AuMusicSong::SetFlag81() {
  byte80 = 0;
  byte81 = 1;
}

void AuMusicSong::ClearFlag81() {
  byte81 = 0;
}

u32 AuMusicSong::GetValue84() {
  return value84;
}

u32 AuMusicSong::SetValue84(u32 value) {
  value84 = value;
  return value;
}

AuMusicSong::~AuMusicSong() {
}

AuFileMusicSong::AuFileMusicSong() : sound(nullptr) {
}

// Copy the supplied path through an owned temporary before initializing the common song fields.
u8 AuFileMusicSong::Init(u32 id, const ScStringView *songName, const ScStringView *songPath,
    const ScStringView *songDescription, u8 firstFlag, u8 secondFlag, AuMusicPlayer *owner) {
  ScString pathCopy(*songPath);
  u8 result = AuMusicSong::Init(id, songName, &pathCopy, songDescription, firstFlag, secondFlag, owner);
  initialized = result;
  return result;
}

u8 AuFileMusicSong::Shutdown() {
  Stop();
  return 1;
}

void AuFileMusicSong::Destroy() {
  delete this;
}

void AuFileMusicSong::Update() {
  if (sound && !sound->GetVolume() && sound->IsPlaying())
    Stop();
}

// Create the configured music sound, set its streaming buffer multiplier and start playback at the requested volume.
u8 AuFileMusicSong::Play(i32 requestedVolume) {
  AuSoundDevice *soundDevice = AuGetManagerSoundDevice();
  if (IsPlaying())
    Stop();
  ScString fullPath(*AuGetMusicDirectory());
  fullPath.Append(path.Data(), path.Strlen());
  sound = soundDevice->SndCreate(&fullPath, static_cast<AuStreamingType>(1));
  if (!sound)
    return 0;
  sound->SetStreamingBufferSizeMultiplier(3);
  return sound->Play(requestedVolume, 0, static_cast<AuSoundDuplication>(1));
}

u8 AuFileMusicSong::FadeOut() {
  if (sound) {
    i32 currentVolume = sound->GetVolume();
    sound->FadeVolume(currentVolume, 0, 2000, 1);
  }
  return 1;
}

u8 AuFileMusicSong::Stop() {
  if (sound) {
    sound->Stop();
    sound->Release();
    sound = nullptr;
  }
  return 1;
}

u8 AuFileMusicSong::UnresolvedSlot48() {
  return 0;
}

u8 AuFileMusicSong::IsPlaying() {
  return sound ? sound->IsPlaying() : 0;
}

u8 AuFileMusicSong::UnresolvedSlot56() {
  return 0;
}

u8 AuFileMusicSong::SetVolume(i32 value) {
  return sound ? sound->SetVolume(value) : 0;
}

u8 AuFileMusicSong::UnresolvedSlot84() {
  IsEnabled();
  return GetFlag66();
}

// Shut down the factory's primary sound and deallocate its storage without invoking Release or its destructor.
// The original leaves the borrowed sound-registry entry intact; using that entry after deallocation is undefined.
AuFileMusicSong::~AuFileMusicSong() {
  if (sound) {
    sound->Shutdown();
    ::operator delete(sound);
    sound = nullptr;
  }
}

AuStreamMusicSong::AuStreamMusicSong()
    : unknown108(0), decoder(nullptr), clock(ScStopwatch::Milliseconds, 0), playStart(0) {
}

// Build the source path, initialize the song base and load its owned freshness player from that resource.
u8 AuStreamMusicSong::Init(u32 id, const ScStringView *songName, const ScStringView *songPath,
    const ScStringView *songDescription, u8 firstFlag, u8 secondFlag, AuMusicPlayer *owner) {
  player = owner;
  ScString fullPath(*AuGetMusicDirectory());
  fullPath.Append(songPath->Data(), songPath->Strlen());
  sourcePath = fullPath;
  u8 result = AuMusicSong::Init(id, songName, &sourcePath, songDescription, firstFlag, secondFlag, owner);
  if (result) {
    decoder = new AuFreshnessPlayer;
    result = decoder->InitResource(&sourcePath, player->manager->scheduler, nullptr);
  }
  initialized = result;
  return result;
}

// Restart streamed-song timing, choose an initial position and start its freshness player at the requested volume.
u8 AuStreamMusicSong::Play(i32 requestedVolume) {
  clock.Reset();
  clock.Start();
  playStart = static_cast<i32>(clock.GetElapsedTime());
  lastUpdate = -10000;
  playPhase = 1;
  if (!player)
    return 0;
  normalizedX = 0;
  normalizedZ = static_cast<i32>(player->manager->random.RandomUint32Uniform() % 1000u);
  decoder->SetZ(normalizedZ);
  decoder->SetX(normalizedX);
  return decoder->StartAtVolume(requestedVolume);
}

// Advance the streamed song's intro, selection movement and fade using its player's timing and city safety values.
// Keep arithmetic intermediates wide while retaining the original stored words and callback order.
void AuStreamMusicSong::Update() {
  i32 now = static_cast<i32>(clock.GetElapsedTime());
  i64 sinceUpdate = static_cast<i64>(now) - lastUpdate, elapsed = static_cast<i64>(now) - playStart;
  if (sinceUpdate < 5000)
    return;
  lastUpdate = now;
  i64 intro = static_cast<i32>(player->value164);
  if (elapsed < intro)
    return;
  i64 duration = static_cast<i32>(player->value156), outroStart = duration - intro;
  if (elapsed >= outroStart) {
    if (elapsed >= duration) {
      if (playPhase) {
        decoder->FadeVolume(2000, 0, 1);
        playPhase = 0;
      }
    } else if (playPhase != 3) {
      normalizedX = 0;
      decoder->SetX(0);
      playPhase = 3;
    }
    return;
  }
  if (playPhase != 2)
    playPhase = 2;
  i64 start = playStart + intro, middle = playStart + duration / 2, end = outroStart + playStart, nextX;
  if (elapsed < middle) {
    player->manager->AudibleSafety();
    i64 denominator = middle - start, numerator = (elapsed - start) * 1000;
    assert(denominator != 0);
    nextX = numerator / denominator;
  } else {
    i64 denominator = end - middle, numerator = (middle - elapsed) * 1000;
    assert(denominator != 0);
    nextX = numerator / denominator + 1000;
  }
  assert(nextX >= INT32_MIN && nextX <= INT32_MAX);
  normalizedX = static_cast<i32>(nextX);
  decoder->SetX(normalizedX);
  i64 scaledSafety = static_cast<i64>(player->manager->AudibleSafety()) * 10;
  i64 step = static_cast<i32>(player->value172), nextZ = normalizedZ;
  if (scaledSafety <= normalizedZ)
    nextZ -= step;
  else
    nextZ += step;
  if (nextZ >= 1000)
    nextZ = 1000;
  if (nextZ <= 0)
    nextZ = 0;
  normalizedZ = static_cast<i32>(nextZ);
  decoder->SetZ(normalizedZ);
}

// Shut down and delete the owned freshness player, then clear its pointer.
u8 AuStreamMusicSong::Shutdown() {
  if (decoder) {
    decoder->Shutdown();
    delete decoder;
    decoder = nullptr;
  }
  return 1;
}

void AuStreamMusicSong::Destroy() {
  delete this;
}

u8 AuStreamMusicSong::UnresolvedSlot36() {
  return 1;
}

u8 AuStreamMusicSong::FadeOut() {
  return Stop();
}

u8 AuStreamMusicSong::Stop() {
  return decoder ? decoder->Stop() : 1;
}

u8 AuStreamMusicSong::UnresolvedSlot48() {
  return 0;
}

u8 AuStreamMusicSong::IsPlaying() {
  return decoder ? decoder->IsPlaying() : 0;
}

u8 AuStreamMusicSong::UnresolvedSlot56() {
  return 0;
}

// Forward volume to the freshness player and report whether one exists.
u8 AuStreamMusicSong::SetVolume(i32 requestedVolume) {
  if (!decoder)
    return 0;
  decoder->SetVolume(requestedVolume);
  return 1;
}

u8 AuStreamMusicSong::UnresolvedSlot72(u32 value) {
  return decoder ? decoder->SetZ(static_cast<i32>(value)) : 0;
}

u8 AuStreamMusicSong::UnresolvedSlot76(u32 value) {
  return decoder ? decoder->SetX(static_cast<i32>(value)) : 0;
}

// Forward both raw coordinate words when a freshness player exists and report success.
u8 AuStreamMusicSong::UnresolvedSlot80(u32 first, u32 second) {
  if (decoder)
    decoder->SetCoordinates(static_cast<i32>(first), static_cast<i32>(second));
  return 1;
}

u8 AuStreamMusicSong::UnresolvedSlot84() {
  // The streamed song's hook does nothing and leaves its byte result unset; nothing consumes it, so report 0.
  return 0;
}

// Shut down and delete the owned player before the stopwatch and song strings are destroyed.
AuStreamMusicSong::~AuStreamMusicSong() {
  if (decoder) {
    decoder->Shutdown();
    delete decoder;
    decoder = nullptr;
  }
}

// Initialize the player, empty song list and elapsed-time counter with the original defaults.
AuMusicPlayer::AuMusicPlayer()
    : initialized(0), manager(nullptr), device(nullptr), current(nullptr), serial(100), playing(0), paused(0),
      clock(ScStopwatch::Milliseconds, 0), volume(1024), value140(0), value144(0), value148(0),
      selectionTime(kAuMusicSelectionTimerStopped), value156(kAuDefaultStreamedSongDurationMs), value160(3000),
      value164(1000), value168(1000), value172(100) {
}

AuMusicPlayer::~AuMusicPlayer() {
}

u8 AuMusicPlayer::Init(AuAudioManager *owner, AuSoundDevice *soundDevice) {
  initialized = 1;
  manager = owner;
  device = soundDevice;
  return 1;
}

// Stop the current song, shut down and delete each owned song, then clear borrowed interfaces and playback flags.
// Deleting after shutdown also releases each song's C++ fields, which the original raw free left allocated.
u8 AuMusicPlayer::Shutdown() {
  if (current) {
    current->Stop();
    current = nullptr;
  }
  for (AuMusicSong *song : songs) {
    song->Shutdown();
    delete song;
  }
  songs.clear();
  manager = nullptr;
  device = nullptr;
  playing = 0;
  paused = 0;
  return 1;
}

// Advance the current song and select another after the configured delay under the player lock.
u8 AuMusicPlayer::Tick() {
  lock.Lock();
  if (current)
    current->Update();
  if (playing && !paused) {
    if (selectionTime == kAuMusicSelectionTimerStopped) {
      assert(current);
      if (!current->IsPlaying())
        StartSelectionTimer();
    } else if (clock.GetElapsedTime() - selectionTime > value160) {
      SelectSong();
      selectionTime = kAuMusicSelectionTimerStopped;
    }
  }
  lock.Unlock();
  return 1;
}

// Report a current song with a pending transition immediately; otherwise query the original song's playing state.
bool AuMusicPlayer::IsPlaying() {
  if (selectionTime != kAuMusicSelectionTimerStopped && current)
    return true;
  if (!current)
    return false;
  return current->IsPlaying() != 0;
}

u32 AuMusicPlayer::CurrentSongId() {
  if (!current)
    return 0;
  return current->IsPlaying() ? current->GetId() : 0;
}

// Resume a playing song or request the original automatic selection under the player lock.
u8 AuMusicPlayer::Play(i32) {
  lock.Lock();
  if (paused)
    paused = 0;
  if (!IsPlaying())
    playing = SelectSong();
  u8 result = playing;
  lock.Unlock();
  return result;
}

// Fade the current song, clear it and reset playback and pending selection under the player lock.
u8 AuMusicPlayer::Stop() {
  lock.Lock();
  if (current) {
    current->FadeOut();
    current = nullptr;
  }
  selectionTime = kAuMusicSelectionTimerStopped;
  playing = 0;
  lock.Unlock();
  return 1;
}

// Store a changed volume and forward it to the current song while playback is active.
u8 AuMusicPlayer::SetVolume(i32 value) {
  if (static_cast<u32>(value) == volume)
    return 1;
  volume = static_cast<u32>(value);
  if (playing && current)
    return current->SetVolume(value);
  return 1;
}

u32 AuMusicPlayer::RandomIndex(u32 bound) {
  assert(bound);
  return manager->random.RandomUint32Uniform() % bound;
}

i32 AuMusicPlayer::CountEnabledSongs() {
  i32 count = 0;
  for (AuMusicSong *song : songs) {
    if (song->IsEnabled() && song->GetFlag66())
      ++count;
  }
  return count;
}

// Select an eligible song, favoring songs least recently played, and start its playback and timing.
u8 AuMusicPlayer::SelectSong() {
  if (current)
    current->Stop();
  i32 count = CountEnabledSongs();
  if (!count)
    return 0;
  i32 threshold = 0, limit = 2 * count / 3;
  for (AuMusicSong *song : songs) {
    i32 age = static_cast<i32>(serial - song->GetValue84());
    if (song->IsEnabled() && song->GetFlag66() && threshold <= age)
      threshold = age;
  }
  if (threshold >= limit)
    threshold = limit;
  AuMusicSong **candidates = new AuMusicSong *[songs.size()];
  u32 candidateCount = 0;
  for (AuMusicSong *song : songs) {
    i32 age = static_cast<i32>(serial - song->GetValue84());
    if (age >= threshold && song->IsEnabled() && song->GetFlag66())
      candidates[candidateCount++] = song;
  }
  current = candidates[RandomIndex(candidateCount)];
  delete[] candidates;
  clock.Reset();
  clock.Start();
  current->SetValue84(++serial);
  if (current->Play(manager->musicVolume)) {
    value148 = clock.GetElapsedTime();
    selectionTime = kAuMusicSelectionTimerStopped;
  } else {
    value148 = kAuMusicPlaybackStartFailedTime;
    StartSelectionTimer();
  }
  playing = 1;
  return 1;
}

u8 AuMusicPlayer::IsSongEnabled(u32 identifier) {
  AuMusicSong *song = FindSong(identifier);
  return song ? song->IsEnabled() : 0;
}

u8 AuMusicPlayer::PlaySong(u32 identifier) {
  if (!volume)
    return 1;
  if (current && current->IsPlaying())
    current->Stop();
  StopSelectionTimer();
  current = FindSong(identifier);
  if (!current)
    return 0;
  clock.Reset();
  clock.Start();
  current->SetValue84(++serial);
  if (current->Play(manager->musicVolume))
    value148 = clock.GetElapsedTime();
  else
    selectionTime = kAuMusicSelectionTimerStopped;
  return 1;
}

u32 AuMusicPlayer::BeginSongEnumeration() {
  songCursor = songs.begin();
  return static_cast<u32>(songs.size());
}

u32 AuMusicPlayer::NextSongId() {
  while (songCursor != songs.end()) {
    AuMusicSong *song = *songCursor++;
    if (song->GetFlag66())
      return song->GetId();
  }
  return 0;
}

void AuMusicPlayer::StopSelectionTimer() {
  clock.Stop();
  selectionTime = kAuMusicSelectionTimerStopped;
}

u32 AuMusicPlayer::StartSelectionTimer() {
  clock.Reset();
  clock.Start();
  selectionTime = clock.GetElapsedTime();
  return selectionTime;
}

void AuMusicPlayer::UnresolvedHook() {
}

u32 AuMusicPlayer::StopCurrentSong() {
  if (current)
    current->Stop();
  current = nullptr;
  return StartSelectionTimer();
}

// Return the first song with the requested identifier, or null when the list has no match.
AuMusicSong *AuMusicPlayer::FindSong(u32 identifier) {
  for (auto it = songs.begin(); it != songs.end(); ++it) {
    if ((*it)->GetId() == identifier)
      return *it;
  }
  return nullptr;
}

u32 AuMusicPlayer::SetSongEnabled(u32 identifier, u8 enabledValue) {
  AuMusicSong *song = FindSong(identifier);
  if (!song)
    return 0;
  u8 result = song->SetEnabled(enabledValue);
  if (!enabledValue && song == current)
    return StopCurrentSong();
  return result;
}

// Append the song, then apply the stored enabled preference to the first song with its identifier.
u8 AuMusicPlayer::AddSong(AuMusicSong *song) {
  songs.push_back(song);
  u32 identifier = song->GetId();
  ApPreferences *preferences = AuGetApplication()->GetAppPreferences();
  u8 enabledValue = preferences->IsSongEnabled(identifier);
  SetSongEnabled(identifier, enabledValue);
  return 1;
}

static bool AuInteractiveOnAir;

AuInteractiveStation::AuInteractiveStation() : radio(nullptr), references(0) {
}

bool AuInteractiveStation::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuRadioStation);
  HANDLE_QUERY_INTERFACE(IID_AuRadioStation, AuRadioStation);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuInteractiveStation::AddRef() {
  return ++references;
}

u32 AuInteractiveStation::Release() {
  u32 previous = references--;
  if (previous == 1)
    return FinalRelease();
  return references;
}

bool AuInteractiveStation::Init() {
  ScString directory;
  radio->GetRadioDirectory(&directory);
  directory.Append("Stations\\Freshness\\Music\\");
  return true;
}

bool AuInteractiveStation::Shutdown() {
  ScRelease(&radio);
  return true;
}

bool AuInteractiveStation::OnIdle() {
  return true;
}

// Release the old radio before retaining and storing the new radio interface.
bool AuInteractiveStation::SetRadio(AuRadio *value) {
  ScRelease(&radio);
  value->AddRef();
  radio = value;
  return true;
}

void AuInteractiveStation::SetStation() {
}

void AuInteractiveStation::UnsetStation() {
}

// Write the original four-byte call sign without appending a terminator.
void AuInteractiveStation::GetStationCallSign(char *out) {
  out[0] = 'K';
  out[1] = 'F';
  out[2] = 'R';
  out[3] = 'S';
}

void AuInteractiveStation::GetStationDescription(ScStringView *out) {
  out->FromChar("Interactive music that depends on the aura of your city.");
}

bool AuInteractiveStation::IsOnTheAir() {
  return AuInteractiveOnAir;
}

// Shut the station down before deleting the complete object.
u32 AuInteractiveStation::FinalRelease() {
  Shutdown();
  delete this;
  return 0;
}

AuInteractiveStation::~AuInteractiveStation() {
}
