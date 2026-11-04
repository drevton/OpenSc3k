#define WIN32_LEAN_AND_MEAN
#include "audio/local_radio.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "application/application_core.h"
#include "common/ids.h"
#include "resources/file.h"
#include "resources/manager_object.h"

// The service returns its original fixed framework scheduling priority.
enum AuRenderServiceServiceOrder : i32 {
  kAuRenderServiceServicePriority = 0x61a80,
};

// Construct the empty station vector, owned directory and original zero-reference radio/service identities.
// Station discovery and sound creation occur later during Init.
AuRadioPlayer::AuRadioPlayer()
    : identifier(IID_AuRadio), volume(1024), mute_count(0), on(false), current_station(0), dj(true), commercials(true),
      english_fallback(true), language_manager(nullptr), sound_system(nullptr), radio_sound(nullptr), references(0),
      initialized(false) {
}

AuRadioPlayer::~AuRadioPlayer() {
}

// Publish the radio facet for its IID and the secondary service facet for Unknown or service queries.
// All successful queries retain through the radio's actual virtual AddRef.
bool AuRadioPlayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AuRadio, AuRadio);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScSystemService);
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuRadioPlayer::AddRef() {
  return ++references;
}

// Decrement before invoking the real final-release notification when the old count was one.
u32 AuRadioPlayer::Release() {
  if (--references == 0)
    return FinalRelease();
  return references;
}

// Shut down through the live virtual interface before deleting the radio object and its member storage.
u32 AuRadioPlayer::FinalRelease() {
  Shutdown();
  delete this;
  return 0;
}

// Create the radio sound when a sound system is available, then discover stations before publishing initialization.
// A previously requested on state is replayed after discovery by clearing it before the virtual TurnOn call.
bool AuRadioPlayer::Init() {
  if (!initialized) {
    if (sound_system)
      radio_sound = sound_system->SndCreate();
    SetupDiskStations();
    initialized = true;
    if (on) {
      on = false;
      TurnOn();
    }
  }
  return true;
}

// Retire station references before stopping and releasing the sound. The original leaves its borrowed sound field unwritten.
// Repeated shutdown returns false once the initialized flag has been cleared.
bool AuRadioPlayer::Shutdown() {
  if (!initialized)
    return false;
  RemoveAllStations(true);
  if (radio_sound) {
    if (radio_sound->IsPlaying())
      radio_sound->Stop();
    radio_sound->Release();
  }
  initialized = false;
  return true;
}

bool AuRadioPlayer::OnIdle(u32) {
  if (!on || stations.empty())
    return false;
  return stations[current_station]->OnIdle();
}

bool AuRadioPlayer::OnTick(u32) {
  return true;
}

// Borrow the sound created during initialization without acquiring another reference.
AuSound *AuRadioPlayer::GetRadioSound() {
  return radio_sound;
}

// Publish the selected station index only when the station vector is nonempty.
bool AuRadioPlayer::GetCurrentStation(u32 *index) {
  if (stations.empty())
    return false;
  *index = current_station;
  return true;
}

// Ask the selected station for its four-byte call sign, preserving the output when no station exists.
bool AuRadioPlayer::GetCurrentStation(char *name) {
  if (stations.empty())
    return false;
  stations[current_station]->GetStationCallSign(name);
  return true;
}

// Borrow the indexed station, returning null for an index beyond the current vector.
AuRadioStation *AuRadioPlayer::GetStation(u32 index) {
  return index < stations.size() ? stations[index] : nullptr;
}

// Compare exactly four call-sign bytes in vector order. The candidate starts as the original four question marks.
AuRadioStation *AuRadioPlayer::GetStation(const char *name) {
  ScString requested, candidate;
  requested.FromChar(name, 4);
  candidate.FromChar("????", 4);
  u32 count = static_cast<u32>(stations.size());
  for (u32 index = 0; index < count; ++index) {
    stations[index]->GetStationCallSign(candidate.begin);
    if (!memcmp(requested.Data(), candidate.Data(), 4))
      return stations[index];
  }
  return nullptr;
}

// Unset the old station and stop its sound before checking/clamping a changed selection.
// The existing selection is an internal valid-index invariant; the requested new index clamps to the final station.
bool AuRadioPlayer::SetStation(u32 index) {
  if (index != current_station) {
    stations[current_station]->UnsetStation();
    radio_sound->Stop();
    if (stations.empty())
      return false;
    if (index >= stations.size())
      index = static_cast<u32>(stations.size()) - 1;
    current_station = index;
    if (on)
      stations[index]->SetStation();
  }
  return true;
}

// Locate the first exact four-byte call sign and dispatch the matching index through the virtual selection method.
bool AuRadioPlayer::SetStation(const char *name) {
  ScString requested, candidate;
  requested.FromChar(name, 4);
  candidate.FromChar("????", 4);
  u32 count = static_cast<u32>(stations.size());
  for (u32 index = 0; index < count; ++index) {
    stations[index]->GetStationCallSign(candidate.begin);
    if (!memcmp(requested.Data(), candidate.Data(), 4))
      return SetStation(index);
  }
  return false;
}

i32 AuRadioPlayer::GetVolume() {
  return volume;
}

bool AuRadioPlayer::SetVolume(i32 value) {
  if (static_cast<u32>(value) > 1024)
    value = 1024;
  AuSound *sound = radio_sound;
  volume = value;
  if (sound && !mute_count)
    sound->SetVolume(value);
  return true;
}

u32 AuRadioPlayer::GetStationCount() {
  return static_cast<u32>(stations.size());
}

bool AuRadioPlayer::IsStationAvailable(const char *name) {
  return GetStation(name) != nullptr;
}

// Bind and initialize a new station before appending it. The supplied station reference is transferred without AddRef.
// A first station on an already-on radio triggers index selection and one immediate idle notification.
bool AuRadioPlayer::AddStation(AuRadioStation *station) {
  for (AuRadioStation *existing : stations)
    if (existing == station)
      return false;
  station->SetRadio(this);
  station->Init();
  stations.push_back(station);
  if (stations.size() == 1 && IsOn()) {
    SetStation(0u);
    OnIdle(0);
  }
  return true;
}

// Release a found station before erasing its captured vector position when the caller requests ownership retirement.
// Neither current selection nor the radio's on state is repaired by this original method.
bool AuRadioPlayer::RemoveStation(AuRadioStation *station, bool release) {
  for (auto it = stations.begin(); it != stations.end(); ++it)
    if (*it == station) {
      if (release)
        station->Release();
      stations.erase(it);
      return true;
    }
  return false;
}

bool AuRadioPlayer::RemoveStation(const char *name, bool release) {
  AuRadioStation *station = GetStation(name);
  return station ? RemoveStation(station, release) : false;
}

// Optionally release each captured station pointer, then clear the live vector. The original result is always false.
bool AuRadioPlayer::RemoveAllStations(bool release) {
  if (release)
    for (AuRadioStation *station : stations)
      if (station)
        station->Release();
  stations.clear();
  return false;
}

// Publish on before station callbacks, then select, idle and set volume only when a sound is available.
// An empty vector leaves on unchanged, and every normal path returns success.
bool AuRadioPlayer::TurnOn() {
  if (!on && !stations.empty()) {
    on = true;
    if (radio_sound) {
      stations[current_station]->SetStation();
      stations[current_station]->OnIdle();
      radio_sound->SetVolume(volume);
    }
  }
  return true;
}

// Clear the on flag before stopping a live sound, leaving the current station selection intact.
bool AuRadioPlayer::TurnOff() {
  if (on) {
    on = false;
    if (radio_sound)
      radio_sound->Stop();
  }
  return true;
}

bool AuRadioPlayer::IsOn() {
  return on;
}

void AuRadioPlayer::Mute() {
  if (++mute_count == 1 && static_cast<u32>(volume) > 896 && radio_sound)
    radio_sound->SetVolume(896);
}

// Retire one mute request and restore an above896 configured volume only after the last request is removed.
void AuRadioPlayer::UnMute() {
  if (mute_count) {
    --mute_count;
    if (!mute_count && static_cast<u32>(volume) > 896 && radio_sound)
      radio_sound->SetVolume(volume);
  }
}

// Read the three actual options. The declared Jingle option has no stored state and returns false.
bool AuRadioPlayer::GetOption(AuRadioOption option) {
  switch (option) {
  case AuRadioOption::DJ:
    return dj;
  case AuRadioOption::Commercials:
    return commercials;
  case AuRadioOption::English:
    return english_fallback;
  default:
    return false;
  }
}

// Store supported option flags without adding behavior for the unimplemented Jingle selector.
void AuRadioPlayer::SetOption(bool value, AuRadioOption option) {
  switch (option) {
  case AuRadioOption::DJ:
    dj = value;
    break;
  case AuRadioOption::Commercials:
    commercials = value;
    break;
  case AuRadioOption::English:
    english_fallback = value;
    break;
  default:
    break;
  }
}

bool AuRadioPlayer::GetRadioDirectory(ScStringView *out) {
  out->FromChar(directory.Data(), directory.Strlen());
  return true;
}

void AuRadioPlayer::SetRadioDirectory(const ScStringView &value) {
  ScString copy(value);
  directory = copy;
}

// Apply option flags before volume, station selection and the final on/off notification. Auto-mute is not consumed.
void AuRadioPlayer::ReadSettings(AuRadioSettings *settings) {
  u32 index = settings->current_station;
  i32 new_volume = static_cast<i32>(settings->volume);
  bool new_on = settings->is_on != 0;
  dj = settings->dj != 0;
  commercials = settings->commercials != 0;
  english_fallback = settings->english_fallback != 0;
  SetVolume(new_volume);
  SetStation(index);
  if (new_on)
    TurnOn();
  else
    TurnOff();
}

// Publish the six actual settings words while preserving the caller's unused auto-mute word at20.
void AuRadioPlayer::WriteSettings(AuRadioSettings *settings) {
  settings->volume = static_cast<u32>(volume);
  settings->is_on = on;
  settings->current_station = current_station;
  settings->dj = dj;
  settings->commercials = commercials;
  settings->english_fallback = english_fallback;
}

u32 AuRadioPlayer::GetServiceID() const {
  return identifier;
}

void AuRadioPlayer::SetServiceID(u32 value) {
  identifier = value;
}

i32 AuRadioPlayer::GetServicePriority() const {
  return -INT32_MAX;
}

bool AuRadioPlayer::IsServiceRunning() const {
  return true;
}

void AuRadioPlayer::SetServiceRunning(bool) {
}

bool AuRadioPlayer::OnInstall() {
  return true;
}

void AuRadioPlayer::SetLanguageManager(ScLanguageManager *manager) {
  language_manager = manager;
}

void AuRadioPlayer::SetSoundSystem(AuSoundDevice *system) {
  sound_system = system;
}

// Append a separator only after the real language-directory query succeeds.
bool AuRadioPlayer::GetLanguageDirectoryName(u32 language, ScString *out) {
  if (!language_manager->GetLanguageDirectoryName(out, language))
    return false;
  out->Append("\\");
  return true;
}

// Search the directory plus the original WAV wildcard. A successful search handle is closed before returning true.
bool AuRadioPlayer::DoesAtLeastOneSoundEntryExistForTheGivenDirectory(const ScString &path) {
  ScString pattern(path);
  pattern.Append("*.wav");
  return RsDirectoryListing::DoesAnyEntryExistThatMatchesPattern(pattern);
}

// Discover station directories using the original call-sign wildcard, replacing old stations only after a nonempty listing.
// Register each real station before filling its paths, then choose localized or English speech/commercial/jingle suffixes.
bool AuRadioPlayer::SetupDiskStations() {
  ScString station_root(directory), commercial_root(directory), probe, language, empty;
  std::list<ScString> directories;
  RsDirectoryListing listing(empty, false, false, false);
  station_root.Append("stations\\");
  commercial_root.Append("commercl\\");
  listing.ChangeDirectory(station_root, false);
  listing.ReadDirectoryDirectoryEntriesIntoStringList(&directories, 0);
  if (directories.empty())
    return false;
  RemoveAllStations(true);
  u32 added = 0;
  for (ScString &name : directories) {
    probe = station_root;
    probe.Append(name.Data(), name.Strlen());
    probe.Append("\\K*.");
    if (!RsDirectoryListing::DoesAnyEntryExistThatMatchesPattern(probe))
      continue;
    AuDiskStation *station = new AuDiskStation;
    station->AddRef();
    AddStation(station);
    ++added;
    name.Append("\\");
    station->station_directory = station_root;
    station->station_directory.Append(name.Data(), name.Strlen());
    station->music_directory = station->station_directory;
    station->music_directory.Append("music\\");
    station->dj_directory = station->station_directory;
    station->dj_directory.Append("dj\\");
    GetLanguageDirectoryName(0, &language);
    probe = station->dj_directory;
    probe.Append(language.Data(), language.Strlen());
    if (language_manager->GetCurrentLanguage() != 1 && !DoesAtLeastOneSoundEntryExistForTheGivenDirectory(probe)) {
      if (GetOption(AuRadioOption::English))
        GetLanguageDirectoryName(1, &language);
      else
        language.Erase(0, language.ScString::Strlen());
    }
    station->dj_directory.Append(language.Data(), language.Strlen());
    station->commercial_directory = directory;
    station->commercial_directory.Append("commercl\\");
    GetLanguageDirectoryName(0, &language);
    probe = station->commercial_directory;
    probe.Append(language.Data(), language.Strlen());
    if (language_manager->GetCurrentLanguage() != 1 && !DoesAtLeastOneSoundEntryExistForTheGivenDirectory(probe)) {
      if (GetOption(AuRadioOption::English))
        GetLanguageDirectoryName(1, &language);
      else
        language.Erase(0, language.ScString::Strlen());
    }
    station->commercial_directory.Append(language.Data(), language.Strlen());
    station->jingle_directory = station->station_directory;
    station->jingle_directory.Append("jingle\\");
    GetLanguageDirectoryName(0, &language);
    probe = station->jingle_directory;
    probe.Append(language.Data(), language.Strlen());
    if (language_manager->GetCurrentLanguage() != 1 && !DoesAtLeastOneSoundEntryExistForTheGivenDirectory(probe)) {
      if (GetOption(AuRadioOption::English))
        GetLanguageDirectoryName(1, &language);
      else
        language.Erase(0, language.ScString::Strlen());
    }
    station->jingle_directory.Append(language.Data(), language.Strlen());
  }
  if (current_station >= stations.size())
    current_station = 0;
  return added != 0;
}

// C++23 has no random_shuffle; retain its observed random draws without introducing a different generator.
static void AuShuffleRadioCycle(std::vector<i32> *cycle) {
  for (u32 i = 1; i < cycle->size(); ++i) {
    u32 other = static_cast<u32>(ScRand()) % (i + 1);
    i32 value = (*cycle)[i];
    (*cycle)[i] = (*cycle)[other];
    (*cycle)[other] = value;
  }
}

AuRadioState::AuRadioState() : pause_timer(ScStopwatch::Milliseconds, 0) {
}

AuRadioState::~AuRadioState() {
}

// Create the original four-kind cycle, default call sign and borrowed null radio/sound fields.
// Randomize before resetting playback state and starting the millisecond pause stopwatch.
AuDiskStation::AuDiskStation()
    : on_air(true), played_welcome(false), radio(nullptr), sound(nullptr), call_sign("????"), references(0) {
  for (i32 type = 0; type != 4; ++type)
    state.cycle.push_back(type);
  AuShuffleRadioCycle(&state.cycle);
  state.index = 0;
  state.subindex = 0;
  state.paused = false;
  state.pause_timer.SetResolution(ScStopwatch::Milliseconds);
  state.pause_timer.Start();
}

AuDiskStation::~AuDiskStation() {
}

// Publish the same station identity for Unknown and the original station IID, retaining it through virtual AddRef.
bool AuDiskStation::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuRadioStation);
  HANDLE_QUERY_INTERFACE(IID_AuRadioStation, AuRadioStation);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuDiskStation::AddRef() {
  return ++references;
}

// Delete through the original virtual destructor when the decremented reference count reaches zero.
u32 AuDiskStation::Release() {
  if (--references == 0) {
    delete this;
    return 0;
  }
  return references;
}

bool AuDiskStation::Init() {
  return true;
}

bool AuDiskStation::Shutdown() {
  return true;
}

// Borrow the supplied radio before querying and storing its shared sound without acquiring either reference.
bool AuDiskStation::SetRadio(AuRadio *value) {
  radio = value;
  sound = value->GetRadioSound();
  return true;
}

bool AuDiskStation::IsOnTheAir() {
  return on_air;
}

void AuDiskStation::SetStation() {
  ReadCallSignFromDisk();
  LoadAllPlaylists();
  ShufflePlaylist();
  if (!played_welcome)
    PlayWelcome();
}

void AuDiskStation::UnsetStation() {
}

void AuDiskStation::GetStationCallSign(char *out) {
  memcpy(out, call_sign.begin, 4);
}

void AuDiskStation::GetStationDescription(ScStringView *out) {
  out->FromChar(description.begin == description.end ? call_sign.Data() : description.Data());
}

// Replace each of the four playlists with matching ordinary wave filenames in platform enumeration order.
// Directory and filter changes defer cache refresh because the file-list reader enumerates directly.
void AuDiskStation::LoadAllPlaylists() {
  ScString empty, filter("*.wav");
  RsDirectoryListing directory(empty, false, false, false);
  directory.SetNewDirectoryEntryFilter(filter, false);
  music.clear();
  directory.ChangeDirectory(music_directory, false);
  directory.SetNewDirectoryEntryFilter(filter, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&music);
  dj.clear();
  directory.ChangeDirectory(dj_directory, false);
  directory.SetNewDirectoryEntryFilter(filter, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&dj);
  commercials.clear();
  directory.ChangeDirectory(commercial_directory, false);
  directory.SetNewDirectoryEntryFilter(filter, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&commercials);
  jingles.clear();
  directory.ChangeDirectory(jingle_directory, false);
  directory.SetNewDirectoryEntryFilter(filter, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&jingles);
}

// Copy the linked Strings into the original temporary vector, empty the list, shuffle and append the shuffled copies.
void AuDiskStation::RandomizeStringList(std::list<ScString> *list) {
  if (list->empty())
    return;
  std::vector<ScString> shuffled;
  for (const ScString &value : *list)
    shuffled.push_back(value);
  list->clear();
  for (u32 i = 1; i < shuffled.size(); ++i) {
    u32 other = static_cast<u32>(ScRand()) % (i + 1);
    ScString value(shuffled[i]);
    shuffled[i] = shuffled[other];
    shuffled[other] = value;
  }
  for (const ScString &value : shuffled)
    list->push_back(value);
}

// Shuffle nonempty playlists and avoid repeating their previous final entry at the front by exchanging first and last.
// Each active iterator is then reset to the new first entry.
void AuDiskStation::ShufflePlaylist() {
  ScString previous, temporary;
  std::list<ScString> *lists[] = {&music, &dj, &commercials, &jingles};
  std::list<ScString>::iterator *items[] = {&music_item, &dj_item, &commercial_item, &jingle_item};
  for (u32 i = 0; i != 4; ++i) {
    std::list<ScString> *list = lists[i];
    if (list->empty())
      continue;
    previous = list->back();
    RandomizeStringList(list);
    if (previous.Strlen() == list->front().Strlen() &&
        !memcmp(previous.Data(), list->front().Data(), previous.Strlen())) {
      temporary = list->back();
      list->back() = list->front();
      list->front() = temporary;
    }
    *items[i] = list->begin();
  }
}

// Shuffle the playback cycle and avoid repeating a nonmusic final kind immediately at its new front.
void AuDiskStation::ShuffleCycle() {
  i32 previous = state.cycle.back();
  AuShuffleRadioCycle(&state.cycle);
  if (previous != 0 && state.cycle.front() == previous) {
    i32 first = state.cycle.front();
    state.cycle.front() = state.cycle.back();
    state.cycle.back() = first;
  }
}

// Music needs only a nonempty list; speech, commercial and jingle lists also require their actual radio option.
bool AuDiskStation::IsCycleTypePresent(AuRadioCycle type) {
  switch (type) {
  case AuRadioCycle::Music:
    return !music.empty();
  case AuRadioCycle::DJ:
    return radio->GetOption(AuRadio::AuRadioOption::DJ) && !dj.empty();
  case AuRadioCycle::Commercial:
    return radio->GetOption(AuRadio::AuRadioOption::Commercials) && !commercials.empty();
  case AuRadioCycle::Jingle:
    return radio->GetOption(AuRadio::AuRadioOption::Jingle) && !jingles.empty();
  default:
    return false;
  }
}

// Advance the requested linked playlist and wrap its sentinel back to the first entry.
void AuDiskStation::MoveToNextPlaylistItem(AuRadioCycle type) {
  switch (type) {
  case AuRadioCycle::Music:
    if (++music_item == music.end())
      music_item = music.begin();
    break;
  case AuRadioCycle::DJ:
    if (++dj_item == dj.end())
      dj_item = dj.begin();
    break;
  case AuRadioCycle::Commercial:
    if (++commercial_item == commercials.end())
      commercial_item = commercials.begin();
    break;
  case AuRadioCycle::Jingle:
    if (++jingle_item == jingles.end())
      jingle_item = jingles.begin();
    break;
  }
}

// Read the first matching metadata filename stems into the call sign and description, preserving either on absence.
// The returned value is solely the resulting call sign's nonempty state.
bool AuDiskStation::ReadCallSignFromDisk() {
  ScString empty, filter("*.id*");
  RsDirectoryListing directory(empty, false, false, false);
  directory.ChangeDirectory(station_directory, true);
  directory.SetNewDirectoryEntryFilter(filter, true);
  if (directory.Size())
    directory.GetEntry(0)->GetStem(&call_sign);
  filter = ScString("*.dsc");
  directory.SetNewDirectoryEntryFilter(filter, true);
  if (directory.Size())
    directory.GetEntry(0)->GetStem(&description);
  return call_sign.begin != call_sign.end;
}

// Advance before constructing the selected filename, then stop the previous sound and start the next wave.
// Refresh volume through the radio after the play attempt; the original normal result is true regardless of sound replies.
bool AuDiskStation::StartPlayingNextSelectionOfGivenType(AuRadioCycle type) {
  ScString filename;
  switch (type) {
  case AuRadioCycle::Music:
    if (++music_item == music.end())
      music_item = music.begin();
    filename = music_directory;
    filename.Append(music_item->Data(), music_item->Strlen());
    break;
  case AuRadioCycle::DJ:
    if (++dj_item == dj.end())
      dj_item = dj.begin();
    filename = dj_directory;
    filename.Append(dj_item->Data(), dj_item->Strlen());
    break;
  case AuRadioCycle::Commercial:
    if (++commercial_item == commercials.end())
      commercial_item = commercials.begin();
    filename = commercial_directory;
    filename.Append(commercial_item->Data(), commercial_item->Strlen());
    break;
  case AuRadioCycle::Jingle:
    if (++jingle_item == jingles.end())
      jingle_item = jingles.begin();
    filename = jingle_directory;
    filename.Append(jingle_item->Data(), jingle_item->Strlen());
    break;
  default:
    return false;
  }
  if (sound->IsPlaying())
    sound->Stop();
  sound->SetSoundFile(filename, static_cast<AuStreamingType>(1));
  sound->Play(1024, 0, static_cast<AuSoundDuplication>(1));
  radio->SetVolume(radio->GetVolume());
  return true;
}

// Play the station's welcome wave when it exists and sound is available, marking it played only after successful play.
void AuDiskStation::PlayWelcome() {
  ScString filename(station_directory);
  filename.Append("welcome.wav");
  if (sound && RsFile::FileExists(filename)) {
    sound->SetSoundFile(filename, static_cast<AuStreamingType>(2));
    if (sound->Play(1024, 0, static_cast<AuSoundDuplication>(1))) {
      radio->SetVolume(radio->GetVolume());
      state.paused = false;
      played_welcome = true;
    }
  }
}

// Wait four seconds after playback stops, then follow the shuffled cycle and the original per-kind random gates.
bool AuDiskStation::OnIdle() {
  if (!IsOnTheAir())
    return true;
  if (!state.paused) {
    if (!sound->IsPlaying()) {
      state.paused = true;
      state.pause_timer.Reset();
      state.pause_timer.Start();
    }
    return true;
  }
  if (state.pause_timer.GetElapsedTime() < 4000)
    return true;
  if (state.cycle[state.index] == 0 && IsCycleTypePresent(AuRadioCycle::Music) && ScRand() % 100 < 75) {
    ++state.subindex;
    state.paused = false;
    StartPlayingNextSelectionOfGivenType(AuRadioCycle::Music);
    return true;
  }
  if (++state.index == state.cycle.size()) {
    ShuffleCycle();
    state.index = 0;
  }
  AuRadioCycle type = static_cast<AuRadioCycle>(state.cycle[state.index]);
  bool play = false;
  switch (type) {
  case AuRadioCycle::Music:
    play = IsCycleTypePresent(type);
    break;
  case AuRadioCycle::DJ:
    play = ScRand() % 100 < 50 && IsCycleTypePresent(type);
    break;
  case AuRadioCycle::Commercial:
  case AuRadioCycle::Jingle:
    play = ScRand() % 100 < 10 && IsCycleTypePresent(type);
    break;
  }
  if (play) {
    state.paused = false;
    StartPlayingNextSelectionOfGivenType(type);
  }
  return true;
}

static AuSoundDevice *AuAppCachedSoundDevice;

// Original module-local cRZSysServPtr specialization acquires before cache publication and releases afterward.
class AuSoundDeviceRef {
public:
  AuSoundDeviceRef();
  ~AuSoundDeviceRef();
  AuSoundDevice *object;
};

AuSoundDeviceRef::AuSoundDeviceRef() : object(nullptr) {
  ApGetFramework()->GetSystemService(kAuSoundDeviceServiceId, IID_AuSoundDevice, reinterpret_cast<void **>(&object));
}

AuSoundDeviceRef::~AuSoundDeviceRef() {
  if (object)
    object->Release();
}

// Cache the actual service view before releasing the acquisition reference. Missing services are queried again later.
AuSoundDevice *AuGetAppSoundDevice() {
  if (!AuAppCachedSoundDevice) {
    AuSoundDeviceRef acquired;
    AuAppCachedSoundDevice = acquired.object;
  }
  return AuAppCachedSoundDevice;
}

AuRenderService::AuRenderService() : identifier(IID_AuRenderServer), sound_system(nullptr), listener(nullptr) {
}

AuRenderService::~AuRenderService() {
}

u32 AuRenderService::AddRef() {
  return 0;
}

u32 AuRenderService::Release() {
  return 0;
}

// Publish the primary render identity or secondary system-service identity, then invoke the original no-op retain.
bool AuRenderService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AuRenderServer);
  HANDLE_QUERY_INTERFACE(IID_AuRenderServer, AuRenderServer);
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuRenderService::GetServiceID() const {
  return identifier;
}

void AuRenderService::SetServiceID(u32 value) {
  identifier = value;
}

i32 AuRenderService::GetServicePriority() const {
  return kAuRenderServiceServicePriority;
}

bool AuRenderService::IsServiceRunning() const {
  return true;
}

void AuRenderService::SetServiceRunning(bool) {
}

bool AuRenderService::Init() {
  sound_system = AuGetAppSoundDevice();
  listener = sound_system->GetSnd3DListener();
  if (!listener)
    listener = sound_system->SndCreate3DListener();
  return true;
}

bool AuRenderService::Shutdown() {
  return true;
}

bool AuRenderService::OnInstall() {
  return true;
}

bool AuRenderService::OnTick(u32) {
  return true;
}

// Render every client in insertion order during the idle notification, without retaining its borrowed pointer.
bool AuRenderService::OnIdle(u32) {
  for (auto it = clients.begin(); it != clients.end(); ++it)
    (*it)->Render();
  return true;
}

// Return the borrowed sound-system view selected during initialization.
AuSoundDevice *AuRenderService::GetSoundSystem() {
  return sound_system;
}

bool AuRenderService::IsClientPresent(AuRenderClient *client) {
  return std::find(clients.begin(), clients.end(), client) != clients.end();
}

// Append a new borrowed client at the end; duplicate pointers are rejected without another render entry.
bool AuRenderService::AddClient(AuRenderClient *client) {
  if (std::find(clients.begin(), clients.end(), client) != clients.end())
    return false;
  clients.push_back(client);
  return true;
}

// Remove the matching borrowed client pointer, returning false when none is registered.
bool AuRenderService::RemoveClient(AuRenderClient *client) {
  if (std::find(clients.begin(), clients.end(), client) == clients.end())
    return false;
  clients.remove(client);
  return true;
}

void AuRenderService::GetViewerPosition(f32 *x, f32 *y, f32 *z) {
  listener->GetPosition(x, y, z);
}

void AuRenderService::GetViewerOrientation(
    f32 *front_x, f32 *front_y, f32 *front_z, f32 *top_x, f32 *top_y, f32 *top_z) {
  listener->GetOrientation(front_x, front_y, front_z, top_x, top_y, top_z);
}

void AuRenderService::GetViewerVelocity(f32 *x, f32 *y, f32 *z) {
  listener->GetVelocity(x, y, z);
}

void AuRenderService::GetViewerAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *front_x, f32 *front_y,
    f32 *front_z, f32 *top_x, f32 *top_y, f32 *top_z) {
  listener->GetAll(x, y, z, vx, vy, vz, front_x, front_y, front_z, top_x, top_y, top_z);
}

void AuRenderService::SetViewerPosition(f32 x, f32 y, f32 z) {
  listener->SetPosition(x, y, z);
}

void AuRenderService::SetViewerOrientation(f32 front_x, f32 front_y, f32 front_z, f32 top_x, f32 top_y, f32 top_z) {
  listener->SetOrientation(front_x, front_y, front_z, top_x, top_y, top_z);
}

void AuRenderService::SetViewerVelocity(f32 x, f32 y, f32 z) {
  listener->SetVelocity(x, y, z);
}

void AuRenderService::SetViewerAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 front_x, f32 front_y, f32 front_z,
    f32 top_x, f32 top_y, f32 top_z) {
  listener->SetAll(x, y, z, vx, vy, vz, front_x, front_y, front_z, top_x, top_y, top_z);
}
