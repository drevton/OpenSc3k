#ifndef SC3_AUDIO_SAMPLE_H
#define SC3_AUDIO_SAMPLE_H

#include "audio/wave_reader.h"

#include "audio/api.h"
#include "audio/callback_timer.h"
#include "common/sc3_string.h"
#include "common/thread.h"

struct IDirectSoundBuffer;
struct _DSBUFFERDESC;
struct AuStreamingState;
struct AuSdlVoice;
class AuSample;
void AuRegisterSample(AuSample *sample);
void AuUnregisterSample(AuSample *sample);
void AuFadeSound(uptr address);

// The installed 232-byte sound producer owns its buffers, callback timer and file state.
// The streaming refill method retains its observed physical return with its nominal type unresolved.
class AuSample : public AuSound {
public:
  AuSample();
  AuSample(const ScStringView &name, AuStreamingType streaming);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init() override;
  u8 Shutdown() override;
  u8 SetSoundFile(const ScStringView &name, AuStreamingType type) override;
  u8 SetSoundFile(const char *name, u32 size, AuStreamingType type) override;
  u8 Play(i32 volume, u8 loop, AuSoundDuplication duplication) override;
  u8 IsPlaying() override;
  u8 Stop() override;
  u8 StopAllInstances() override;
  AuSoundType GetSoundType() override;
  AuSoundStatus GetStatus() override;
  u32 GetSoundStreamMinimumFileSize() override;
  u8 SetSoundStreamMinimumFileSize(u32 size) override;
  u8 SetSoundDefaultStreamingType(AuStreamingType type) override;
  u32 GetUseCount() override;
  i32 GetID() override;
  void SetID(i32 identifier) override;
  tWAVEFORMATEX *GetWaveFormat() override;
  u8 IsLoaded() override;
  u8 Load() override;
  u8 Unload() override;
  u8 SetCompletionNotification(AuSoundCompletion callback, u32 message) override;
  u8 StopCompletionNotification() override;
  u32 EstimateRemainingPlayTime() override;
  i32 GetVolume() override;
  u8 SetVolume(i32 volume) override;
  u8 FadeVolume(i32 first, i32 second, u32 duration, u8 option) override;
  i32 GetPan() override;
  u8 SetPan(i32 pan) override;
  i32 GetFrequency() override;
  u8 SetFrequency(i32 frequency) override;
  i32 GetOriginalFrequency() override;
  u32 GetPosition() override;
  u8 SetPosition(u32 position) override;
  i32 GetSoundPriority() override;
  void SetSoundPriority(i32 priority) override;
  void SetStreamingBufferSizeMultiplier(u32 multiplier) override;
  virtual ~AuSample();
  // Private producer slots 160..192; AL values retain their unresolved nominal byte spelling.
  virtual u8 LoadBuffered();
  virtual u8 PrimeStream(u8 reload);
  virtual u8 IsLargeFile();
  virtual u8 StartStream(u8 loop, u32 reload);
  virtual u8 StopStream();
  virtual u8 FindAvailableBuffer(IDirectSoundBuffer **out);
  virtual u8 CreateBuffer(_DSBUFFERDESC *description);
  virtual u8 CreatePrimaryBuffer();
  virtual u8 ReleasePrimaryBuffer();
  // Original private slot196 receives a raw reload byte; its nominal bool/BYTE spelling remains unresolved.
  virtual u8 OpenStream(u8 reload);

private:
  friend class ApAnimationPlayer;
  friend class AuDevice;
  friend class AuSpatialSample;
  friend void AuScheduledSound(uptr address);
  friend void AuFadeSound(uptr address);
  u8 ScheduleCompletion();
  bool OpenSdlVoice(u32 index, bool streaming);
  void CloseSdlVoice(u32 index);
  bool StartSdlVoice(u32 index, bool repeat);
  bool IsSdlVoicePlaying(u32 index);
  AuSdlVoice *sdlVoices[8]{};
  u32 sdlRecent = 0;
  i32 sdlFrequency = 0, sdlPan = 0;
  i32 identifier;
  ScString filename;
  u32 unknown28;
  u8 looping, streamed;
  i32 volume;
  u8 reload;
  u32 completionMessage, playbackPolicy;
  i32 priority;
  ScLock lock;
  u8 timerPeriodHeld;
  AuCallbackTimer timer;
  AuCallbackTimer *fadeTimer;
  i32 fadeStep, fadeTarget;
  AuSoundCompletion completionCallback;
  AuStreamingType streamingType;
  IDirectSoundBuffer *buffers[8], *recent;
  u32 dataLength;
  WAVEFORMATEX format;
  AuStreamingState *stream;
  u8 streamReady;
  u32 streamingMultiplier, capabilities, referenceCount;
  AuSoundDevice *device;
};

#endif
