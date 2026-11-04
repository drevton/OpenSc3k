#ifndef SC3_AUDIO_AUDIO_FRESHNESS_H
#define SC3_AUDIO_AUDIO_FRESHNESS_H

#include <list>

#include "audio/audio_core.h"
#include "common/sc3.h"
#include "common/sc3_string.h"

class AuFreshnessScheduler;
class AuFreshnessTable;
class AuFreshnessVoice;
class AuSoundCache;
class ScStopwatch;
class AuCallbackTimer;

// The original freshness player owns a 64-by-64 grid of voices and borrows its scheduler and sound cache.
class AuFreshnessPlayer : public ScUnknown {
public:
  AuFreshnessPlayer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u8 Init(AuFreshnessTable *table, const ScStringView *directory, AuFreshnessScheduler *scheduler,
      AuSoundCache *soundCache);
  virtual u8 Shutdown();
  virtual u32 Update();
  virtual u8 Start();
  virtual u8 IsPlaying();
  virtual u8 Stop();
  virtual u8 SetVolume(i32 volume);
  virtual u8 FadeVolume(i32 duration, i32 volume, u8 stop);
  virtual u8 SetX(i32 value);
  virtual u8 SetZ(i32 value);
  virtual u8 SetValue76(i32 value);
  virtual u8 SetValue16548(i32 value);
  virtual i32 GetX();
  virtual i32 GetZ();
  virtual i32 GetValue76();
  virtual u8 SetFlag57(u8 value);
  virtual u8 GetDirectory(ScString *out);
  virtual u8 SetRadii(i32 x, i32 z);
  virtual u8 GetRadii(i32 *x, i32 *z);
  virtual ~AuFreshnessPlayer();
  virtual u8 InitResource(const ScStringView *path, AuFreshnessScheduler *scheduler, AuSoundCache *soundCache);
  virtual u8 InitFile(const ScStringView *directory, const ScStringView *filename, AuFreshnessScheduler *scheduler,
      AuSoundCache *soundCache);
  virtual u8 StartAtVolume(i32 volume);
  // The caller discards the result and the original path can leave EAX undefined.
  virtual void CancelFade();
  virtual u8 ApplyVolume(i32 volume);
  virtual u8 SetValue60(i32 value);
  virtual i32 SetValue16552(i32 value);
  virtual u8 SetFlag16556(u8 value);
  virtual i32 SetCoordinates(i32 first, i32 second);
  virtual bool Contains(i32 first, i32 second);
  virtual u8 StopInternal();
  virtual bool FindBestCell(i32 *row, i32 *column, i32 group);
  virtual u8 PickRandomCell(i32 *row, i32 *column);
  virtual i32 StopCell(i32 row, i32 column);
  virtual u8 ToggleCell(i32 row, i32 column);
  virtual i32 UpdateCell(i32 row, i32 column, i32 value);
  virtual i32 CountActive();
  virtual i32 CountFlagged();
  virtual i32 CountPlaying();
  virtual u8 InspectVoices();
  virtual i32 SelectionLimit();
  virtual i32 PlaybackLimit();

protected:
  friend class AuAudioManager;
  friend class AuFreshnessVoice;
  i32 FirstX();
  i32 LastX();
  i32 FirstZ();
  i32 LastZ();
  i32 RefreshSelection();
  i32 BeatDuration(i32 position);
  static i32 CellProbability(i32 level, i32 count);
  static void FadeCallback(uptr context);
  u8 initialized;
  u32 references;
  AuFreshnessScheduler *scheduler;
  AuRandom random;
  i32 volume;
  ScStopwatch *clock;
  AuFreshnessTable *table;
  AuSoundCache *cache; // Borrowed optional local sound cache.
  u8 playing, flag57;
  i32 value60, x, z, value72, value76, value80, value84, value88, value92;
  i32 value96, value100, value104, value108, radiusZ, radiusX;
  i32 value120, value124, value128, value132, value136, value140;
  AuFreshnessVoice *voices[64][64];
  ScString directory;
  i32 value16548, value16552;
  u8 flag16556, flag16557;
  AuCallbackTimer *timer;
  i32 value16564, value16568;
  u8 flag16572;
};

class AuCallbackTimer;
class AuFreshnessPlayer;
class ScStopwatch;
class ScLock;

extern ScLock AuFreshnessLock;

// The freshness scheduler owns its player-list nodes; shutdown separately releases the timer and clock.
class AuFreshnessScheduler {
public:
  AuFreshnessScheduler();
  ~AuFreshnessScheduler();
  // The only recovered caller discards the original helper's incidental return register.
  void Shutdown();
  int Pause();
  u32 GetState8();
  u32 DecrementState8();
  int AddPlayer(AuFreshnessPlayer *player);
  int RemovePlayer(AuFreshnessPlayer *player);

private:
  friend class AuAudioManager;
  static void TimerCallback(uptr context);
  // The timer callback discards the original dispatch helper's incidental return register.
  void Dispatch();
  u32 state0, state4, state8;
  AuCallbackTimer *timer;
  ScStopwatch *clock;
  u32 value20, value24, value28, value32;
  std::list<AuFreshnessPlayer *> players;
};

// The current STL adds a size word after the original 40-byte receiver's final list-head pointer.

// Each original cell-list link owns two payload words as well as its forward and backward links.
struct AuFreshnessLink {
  AuFreshnessLink *next, *previous;
  u32 first, second;
};

// A freshness cell contains the original string, scalar values and circular list sentinel.
class AuFreshnessCell {
public:
  AuFreshnessCell();
  ~AuFreshnessCell();

  ScString name;
  u8 flag20;
  i32 value24, value28, value32;
  u8 flag36, flag37;
  i32 value40, value44, value48, value52;
  u8 flag56;
  i32 value60, value64;
  u8 flag68;
  i32 value72, value76, value80;
  u8 flag84;
  AuFreshnessLink *sentinel;
};

// The loaded freshness table owns a fixed 64-by-64 grid of optional cell pointers.
class AuFreshnessTable {
public:
  AuFreshnessTable();
  virtual ~AuFreshnessTable();
  bool Load(ScString filename);

  i32 value4, value8, value12, value16, value20, value24, value28, value32;
  i32 value36, value40, value44, value48, value52, value56, value60, value64;
  AuFreshnessCell *cells[64][64];

private:
  bool ParseCells(ScString *contents, ScString *token, u32 version);
};

class AuFreshnessPlayer;
class AuFreshnessCell;
class AuSound3D;

// One original freshness playback controller belongs to a player and tracks an optional sound and table cell.
class AuFreshnessVoice {
public:
  explicit AuFreshnessVoice(AuFreshnessPlayer *player);
  bool Start(AuFreshnessCell *selected);
  bool Stop();
  bool Reset();
  u8 UpdateVolume(i32 value);
  // The original private return is unused and unresolved; the caller supplies only the defined low flag byte.
  void Update(u32 flags, i32 beat);
  u32 Random();
  // The original result is discarded by its only caller; the private nominal return is unresolved.
  void UpdatePosition();

private:
  friend class AuFreshnessPlayer;
  AuFreshnessPlayer *player;
  AuSound3D *sound;
  u32 state;
  AuFreshnessCell *cell;
  u32 value16;
  u8 flag20, flag21, flag22;
};

#endif
