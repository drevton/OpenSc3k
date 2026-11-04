#ifndef SC3_AUDIO_AUDIO_MUSIC_H
#define SC3_AUDIO_AUDIO_MUSIC_H

#include <list>

#include "audio/api.h"
#include "city/sc_stopwatch.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "common/thread.h"

class AuMusicPlayer;
class AuSound;
class AuFreshnessPlayer;

// AUDIO's shared 88-byte song base retains the original virtual slot order and explicitly names unresolved hooks.
class AuMusicSong {
public:
  AuMusicSong();
  virtual u8 Init(u32 identifier, const ScStringView *name, const ScStringView *path,
      const ScStringView *description, u8 firstFlag, u8 secondFlag, AuMusicPlayer *player);
  virtual u8 Shutdown() = 0;
  // The binary discards the base hook's return; its original nominal return spelling remains unresolved.
  virtual void Destroy() = 0;
  virtual u8 IsInitialized();
  virtual u8 GetFlag66();
  virtual void Update();
  virtual u8 GetName(ScString *name);
  virtual u32 GetId();
  virtual u8 Play(i32 volume) = 0;
  virtual u8 UnresolvedSlot36();
  virtual u8 FadeOut() = 0;
  virtual u8 Stop() = 0;
  virtual u8 UnresolvedSlot48() = 0;
  virtual u8 IsPlaying() = 0;
  virtual u8 UnresolvedSlot56() = 0;
  virtual u8 SetEnabled(u8 enabled);
  virtual u8 IsEnabled();
  virtual u8 SetVolume(i32 volume) = 0;
  // The original hooks ignore these raw 32-bit words; their nominal types remain unresolved.
  virtual u8 UnresolvedSlot72(u32 value);
  virtual u8 UnresolvedSlot76(u32 value);
  virtual u8 UnresolvedSlot80(u32 first, u32 second);
  virtual u8 UnresolvedSlot84() = 0;
  virtual void SetFlag80();
  virtual void ClearFlag80();
  virtual void SetFlag81();
  virtual void ClearFlag81();
  virtual u32 GetValue84();
  virtual u32 SetValue84(u32 value);
  virtual ~AuMusicSong();

protected:
  ScString name, path, description;
  u8 initialized, enabled, byte66;
  AuMusicPlayer *player;
  u8 byte72;
  u32 identifier;
  u8 byte80, byte81;
  u32 value84;
};

// The single-file song borrows its music player through the base and owns one sound after playback starts.
class AuFileMusicSong : public AuMusicSong {
public:
  AuFileMusicSong();
  u8 Init(u32 identifier, const ScStringView *name, const ScStringView *path,
      const ScStringView *description, u8 firstFlag, u8 secondFlag, AuMusicPlayer *player) override;
  u8 Shutdown() override;
  void Destroy() override;
  void Update() override;
  u8 Play(i32 volume) override;
  u8 FadeOut() override;
  u8 Stop() override;
  u8 UnresolvedSlot48() override;
  u8 IsPlaying() override;
  u8 UnresolvedSlot56() override;
  u8 SetVolume(i32 volume) override;
  u8 UnresolvedSlot84() override;
  ~AuFileMusicSong() override;

private:
  AuSound *sound;
};

// The streamed song owns a separate source path and timing state; its remaining fields are not yet recovered.
class AuStreamMusicSong : public AuMusicSong {
public:
  AuStreamMusicSong();
  u8 Init(u32 identifier, const ScStringView *name, const ScStringView *path,
      const ScStringView *description, u8 firstFlag, u8 secondFlag, AuMusicPlayer *player) override;
  u8 Shutdown() override;
  void Destroy() override;
  void Update() override;
  u8 Play(i32 volume) override;
  u8 UnresolvedSlot36() override;
  u8 FadeOut() override;
  u8 Stop() override;
  u8 UnresolvedSlot48() override;
  u8 IsPlaying() override;
  u8 UnresolvedSlot56() override;
  u8 SetVolume(i32 volume) override;
  u8 UnresolvedSlot72(u32 value) override;
  u8 UnresolvedSlot76(u32 value) override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84() override;
  ~AuStreamMusicSong() override;

private:
  ScString sourcePath;
  u32 unknown108;
  AuFreshnessPlayer *decoder;
  ScStopwatch clock;
  i32 playStart, lastUpdate, normalizedZ, normalizedX, playPhase, unknown196;
};

// AUDIO's interactive music station borrows no radio until SetRadio retains one.
class AuInteractiveStation : public AuRadioStation {
public:
  AuInteractiveStation();
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
  virtual u32 FinalRelease();
  virtual ~AuInteractiveStation();

private:
  AuRadio *radio;
  u32 references;
};

class AuAudioManager;
class AuMusicSong;
class AuSoundDevice;
class ScStringView;

// The original music player owns its song list, recursive lock and embedded elapsed-time counter.
// Song objects themselves are retired by shutdown, not by this receiver's destructor.
class AuMusicPlayer {
public:
  AuMusicPlayer();
  virtual ~AuMusicPlayer();
  // The producer defines AL=1; the original nominal bool/BYTE spelling remains unresolved.
  u8 Init(AuAudioManager *owner, AuSoundDevice *soundDevice);
  // Shutdown retires the owned songs; the nominal bool/BYTE spelling of its result is unresolved.
  u8 Shutdown();
  // The original nominal bool/BYTE spelling remains unresolved.
  u8 Tick();
  bool IsPlaying();
  u32 CurrentSongId();
  u8 Play(i32 volume);
  // The installed AL=1 result's nominal bool/BYTE spelling remains unresolved.
  u8 Stop();
  // The forwarded byte result's original nominal spelling remains unresolved.
  u8 SetVolume(i32 value);

private:
  friend class AuAudioManager;
  friend class AuAudioIni;
  friend class AuStreamMusicSong;
  friend void SC3_CDECL AuLoadMusic(const ScStringView &key, const ScStringView &value, void *reader);
  u8 AddSong(AuMusicSong *song);
  u32 SetSongEnabled(u32 identifier, u8 enabled);
  u32 StartSelectionTimer();
  void StopSelectionTimer();
  u32 StopCurrentSong();
  AuMusicSong *FindSong(u32 identifier);
  u32 RandomIndex(u32 bound);
  i32 CountEnabledSongs();
  u8 SelectSong();
  u8 IsSongEnabled(u32 identifier);
  u8 PlaySong(u32 identifier);
  u32 BeginSongEnumeration();
  u32 NextSongId();
  // The installed hook has no side effects and its callers discard the unspecified return register.
  void UnresolvedHook();
  u32 unknown4;
  u8 initialized;
  AuAudioManager *manager;
  AuSoundDevice *device;
  ScLock lock;
  std::list<AuMusicSong *> songs;
  std::list<AuMusicSong *>::iterator songCursor;
  AuMusicSong *current;
  u32 serial;
  u8 playing, paused;
  ScStopwatch clock;
  u32 volume, value140, value144, value148, selectionTime;
  u32 value156, value160, value164, value168, value172;
};

#endif
