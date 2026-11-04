#ifndef SC3_AUDIO_LOCAL_RADIO_H
#define SC3_AUDIO_LOCAL_RADIO_H

#include <list>
#include <vector>

#include "audio/api.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_string.h"

// Retail A4EC owns the actual int vector and pause stopwatch, with the three playback-state fields between them.
struct AuRadioState {
  AuRadioState();
  ~AuRadioState();
  std::vector<i32> cycle;
  u32 index, subindex;
  bool paused;
  ScStopwatch pause_timer;
};

// Retail A4F9 proves the owned String lists/vector and borrowed radio/sound fields. Modern STL storage is not an overlay.
class AuDiskStation : public AuRadioStation {
public:
  enum class AuRadioCycle : i32 { Music, DJ, Commercial, Jingle };
  AuDiskStation();
  virtual ~AuDiskStation();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnIdle() override;
  bool SetRadio(AuRadio *radio) override;
  void SetStation() override;
  void UnsetStation() override;
  void GetStationCallSign(char *out) override;
  void GetStationDescription(ScStringView *out) override;
  bool IsOnTheAir() override;

protected:
  virtual void LoadAllPlaylists();
  virtual void ShufflePlaylist();
  virtual void RandomizeStringList(std::list<ScString> *list);
  virtual void ShuffleCycle();
  virtual bool IsCycleTypePresent(AuRadioCycle type);
  virtual void MoveToNextPlaylistItem(AuRadioCycle type);
  virtual bool ReadCallSignFromDisk();
  virtual bool StartPlayingNextSelectionOfGivenType(AuRadioCycle type);
  virtual void PlayWelcome();

private:
  friend class AuRadioPlayer;
  bool on_air, played_welcome;
  AuRadioState state;
  AuRadio *radio;
  AuSound *sound;
  ScString call_sign, description, station_directory;
  ScString music_directory;
  std::list<ScString> music;
  std::list<ScString>::iterator music_item;
  ScString dj_directory;
  std::list<ScString> dj;
  std::list<ScString>::iterator dj_item;
  ScString commercial_directory;
  std::list<ScString> commercials;
  std::list<ScString>::iterator commercial_item;
  ScString jingle_directory;
  std::list<ScString> jingles;
  std::list<ScString>::iterator jingle_item;
  u32 references;
};

// Original cRZRadio362A owns the station vector, directory String and separate radio/service identities.
// Retail A43F proves vector<cIGZRadioStation*> use. Disk discovery constructs the real owned station producer.
class AuRadioPlayer : public AuRadio, public ScSystemService {
public:
  AuRadioPlayer();
  virtual ~AuRadioPlayer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnIdle(u32 elapsed) override;
  AuSound *GetRadioSound() override;
  bool GetCurrentStation(u32 *index) override;
  bool GetCurrentStation(char *name) override;
  AuRadioStation *GetStation(u32 index) override;
  bool SetStation(u32 index) override;
  AuRadioStation *GetStation(const char *name) override;
  bool SetStation(const char *name) override;
  i32 GetVolume() override;
  bool SetVolume(i32 volume) override;
  u32 GetStationCount() override;
  bool IsStationAvailable(const char *name) override;
  bool AddStation(AuRadioStation *station) override;
  bool RemoveStation(AuRadioStation *station, bool release) override;
  bool RemoveStation(const char *name, bool release) override;
  bool RemoveAllStations(bool release) override;
  bool TurnOn() override;
  bool TurnOff() override;
  bool IsOn() override;
  void Mute() override;
  void UnMute() override;
  bool GetOption(AuRadioOption option) override;
  void SetOption(bool value, AuRadioOption option) override;
  bool GetRadioDirectory(ScStringView *out) override;
  void SetRadioDirectory(const ScStringView &directory) override;
  void ReadSettings(AuRadioSettings *settings) override;
  void WriteSettings(AuRadioSettings *settings) override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool OnTick(u32 elapsed) override;
  bool OnInstall() override;
  virtual void SetLanguageManager(ScLanguageManager *manager);
  virtual void SetSoundSystem(AuSoundDevice *system);

protected:
  virtual bool SetupDiskStations();
  virtual bool DoesAtLeastOneSoundEntryExistForTheGivenDirectory(const ScString &directory);
  virtual bool GetLanguageDirectoryName(u32 language, ScString *out);
  virtual u32 FinalRelease();

private:
  u32 identifier;
  std::vector<AuRadioStation *> stations;
  i32 volume;
  u32 mute_count;
  bool on;
  u32 current_station;
  bool dj, commercials, english_fallback;
  ScString directory;
  ScLanguageManager *language_manager;
  AuSoundDevice *sound_system;
  AuSound *radio_sound;
  u32 references;
  bool initialized;
};

enum class AuDistanceUnits : i32;

// Retail cIGZSnd3DListener3056 prefix consumed by the executable render service.
class AuListener3D : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual void GetPosition(f32 *x, f32 *y, f32 *z) = 0;
  virtual bool SetPosition(f32 x, f32 y, f32 z) = 0;
  virtual void GetVelocity(f32 *x, f32 *y, f32 *z) = 0;
  virtual bool SetVelocity(f32 x, f32 y, f32 z) = 0;
  virtual void GetOrientation(f32 *front_x, f32 *front_y, f32 *front_z, f32 *top_x, f32 *top_y, f32 *top_z) = 0;
  virtual bool SetOrientation(f32 front_x, f32 front_y, f32 front_z, f32 top_x, f32 top_y, f32 top_z) = 0;
  virtual void GetAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *front_x, f32 *front_y, f32 *front_z,
      f32 *top_x, f32 *top_y, f32 *top_z) = 0;
  virtual bool SetAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 front_x, f32 front_y, f32 front_z, f32 top_x,
      f32 top_y, f32 top_z) = 0;
  // Retail cIGZSnd3DListener3056 slots52..76. MSVC reverses the two distance-factor overloads.
  virtual void GetDopplerFactor(f32 *factor) = 0;
  virtual bool SetDopplerFactor(f32 factor) = 0;
  virtual void GetRolloffFactor(f32 *factor) = 0;
  virtual bool SetRolloffFactor(f32 factor) = 0;
  virtual void GetDistanceFactor(f32 *factor) = 0;
  virtual void GetDistanceFactor(AuDistanceUnits *units, f32 *factor) = 0;
  virtual bool SetDistanceFactor(AuDistanceUnits units, f32 factor) = 0;
};

// Original cIGZSoundRenderClient30ED has only Render and no Unknown base.
class AuRenderClient {
public:
  virtual void Render() = 0;
};

// Original cIGZSoundRenderServer2417 primary identity, independent of its service identity.
class AuRenderServer : public ScUnknown {
public:
  virtual bool AddClient(AuRenderClient *client) = 0;
  virtual bool RemoveClient(AuRenderClient *client) = 0;
  virtual AuSoundDevice *GetSoundSystem() = 0;
  virtual bool IsClientPresent(AuRenderClient *client) = 0;
  virtual void GetViewerPosition(f32 *x, f32 *y, f32 *z) = 0;
  virtual void GetViewerOrientation(f32 *front_x, f32 *front_y, f32 *front_z, f32 *top_x, f32 *top_y, f32 *top_z) = 0;
  virtual void GetViewerVelocity(f32 *x, f32 *y, f32 *z) = 0;
  virtual void GetViewerAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *front_x, f32 *front_y, f32 *front_z,
      f32 *top_x, f32 *top_y, f32 *top_z) = 0;
  virtual void SetViewerPosition(f32 x, f32 y, f32 z) = 0;
  virtual void SetViewerOrientation(f32 front_x, f32 front_y, f32 front_z, f32 top_x, f32 top_y, f32 top_z) = 0;
  virtual void SetViewerVelocity(f32 x, f32 y, f32 z) = 0;
  virtual void SetViewerAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 front_x, f32 front_y, f32 front_z,
      f32 top_x, f32 top_y, f32 top_z) = 0;
};

// Original cGZSoundRenderServer owns a list of borrowed clients and caches borrowed sound/listener views.
// CodeView3165 and list-public symbols46EB5F..46EC85 establish original STL list/find use.
class AuRenderService : public AuRenderServer, public ScSystemService {
public:
  AuRenderService();
  ~AuRenderService();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 elapsed) override;
  bool OnIdle(u32 elapsed) override;
  bool OnInstall() override;
  bool AddClient(AuRenderClient *client) override;
  bool RemoveClient(AuRenderClient *client) override;
  AuSoundDevice *GetSoundSystem() override;
  bool IsClientPresent(AuRenderClient *client) override;
  void GetViewerPosition(f32 *x, f32 *y, f32 *z) override;
  void GetViewerOrientation(f32 *front_x, f32 *front_y, f32 *front_z, f32 *top_x, f32 *top_y, f32 *top_z) override;
  void GetViewerVelocity(f32 *x, f32 *y, f32 *z) override;
  void GetViewerAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz, f32 *front_x, f32 *front_y, f32 *front_z,
      f32 *top_x, f32 *top_y, f32 *top_z) override;
  void SetViewerPosition(f32 x, f32 y, f32 z) override;
  void SetViewerOrientation(f32 front_x, f32 front_y, f32 front_z, f32 top_x, f32 top_y, f32 top_z) override;
  void SetViewerVelocity(f32 x, f32 y, f32 z) override;
  void SetViewerAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 front_x, f32 front_y, f32 front_z, f32 top_x,
      f32 top_y, f32 top_z) override;

private:
  u32 identifier;
  std::list<AuRenderClient *> clients;
  AuSoundDevice *sound_system;
  AuListener3D *listener;
};

// Return the framework-owned service as a borrowed view; the acquisition reference is released before return.
AuSoundDevice *AuGetAppSoundDevice();

#endif
