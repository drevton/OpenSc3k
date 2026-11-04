#ifndef SC3_AUDIO_SPATIAL_SAMPLE_H
#define SC3_AUDIO_SPATIAL_SAMPLE_H

#include "audio/api.h"
#include "audio/sample.h"

struct IDirectSound3DBuffer;

// The installed 3D producer has an AuSample primary interface and an independent cIGZSnd3D secondary view.
class AuSpatialSample : public AuSample, public AuSound3D {
public:
  AuSpatialSample();
  AuSpatialSample(const ScStringView &name, AuStreamingType streaming, u8 value);
  ~AuSpatialSample() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Play(i32 volume, u8 loop, AuSoundDuplication duplication) override;
  u8 SetVolume(i32 volume) override;
  u8 SetPan(i32 pan) override;

  // These overrides are the installed secondary-interface adjusting thunks to the actual AuSample methods.
#define AU_SPATIAL_FORWARD(type, name, args, call)                                                                     \
  type name args override {                                                                                            \
    return AuSample::name call;                                                                                        \
  }
#define AU_SPATIAL_FORWARD_VOID(name, args, call)                                                                      \
  void name args override {                                                                                            \
    AuSample::name call;                                                                                               \
  }
  AU_SPATIAL_FORWARD(u8, Init, (), ())
  AU_SPATIAL_FORWARD(u8, Shutdown, (), ())
  AU_SPATIAL_FORWARD(u8, SetSoundFile, (const ScStringView &name, AuStreamingType type), (name, type))
  AU_SPATIAL_FORWARD(u8, SetSoundFile, (const char *name, u32 size, AuStreamingType type), (name, size, type))
  AU_SPATIAL_FORWARD(u8, IsPlaying, (), ())
  AU_SPATIAL_FORWARD(u8, Stop, (), ())
  AU_SPATIAL_FORWARD(u8, StopAllInstances, (), ())
  AU_SPATIAL_FORWARD(AuSoundType, GetSoundType, (), ())
  AU_SPATIAL_FORWARD(AuSoundStatus, GetStatus, (), ())
  AU_SPATIAL_FORWARD(u32, GetSoundStreamMinimumFileSize, (), ())
  AU_SPATIAL_FORWARD(u8, SetSoundStreamMinimumFileSize, (u32 value), (value))
  AU_SPATIAL_FORWARD(u8, SetSoundDefaultStreamingType, (AuStreamingType value), (value))
  AU_SPATIAL_FORWARD(u32, GetUseCount, (), ())
  AU_SPATIAL_FORWARD(i32, GetID, (), ())
  AU_SPATIAL_FORWARD_VOID(SetID, (i32 value), (value))
  AU_SPATIAL_FORWARD(tWAVEFORMATEX *, GetWaveFormat, (), ())
  AU_SPATIAL_FORWARD(u8, IsLoaded, (), ())
  AU_SPATIAL_FORWARD(u8, Load, (), ())
  AU_SPATIAL_FORWARD(u8, Unload, (), ())
  AU_SPATIAL_FORWARD(u8, SetCompletionNotification, (AuSoundCompletion callback, u32 message), (callback, message))
  AU_SPATIAL_FORWARD(u8, StopCompletionNotification, (), ())
  AU_SPATIAL_FORWARD(u32, EstimateRemainingPlayTime, (), ())
  AU_SPATIAL_FORWARD(i32, GetVolume, (), ())
  AU_SPATIAL_FORWARD(
      u8, FadeVolume, (i32 first, i32 second, u32 duration, u8 fadeOption), (first, second, duration, fadeOption))
  AU_SPATIAL_FORWARD(i32, GetPan, (), ())
  AU_SPATIAL_FORWARD(i32, GetFrequency, (), ())
  AU_SPATIAL_FORWARD(u8, SetFrequency, (i32 value), (value))
  AU_SPATIAL_FORWARD(i32, GetOriginalFrequency, (), ())
  AU_SPATIAL_FORWARD(u32, GetPosition, (), ())
  AU_SPATIAL_FORWARD(u8, SetPosition, (u32 value), (value))
  AU_SPATIAL_FORWARD(i32, GetSoundPriority, (), ())
  AU_SPATIAL_FORWARD_VOID(SetSoundPriority, (i32 value), (value))
  AU_SPATIAL_FORWARD_VOID(SetStreamingBufferSizeMultiplier, (u32 value), (value))
#undef AU_SPATIAL_FORWARD
#undef AU_SPATIAL_FORWARD_VOID

  void GetPosition(f32 *x, f32 *y, f32 *z) override;
  u8 SetPosition(f32 x, f32 y, f32 z) override;
  void GetVelocity(f32 *x, f32 *y, f32 *z) override;
  u8 SetVelocity(f32 x, f32 y, f32 z) override;
  void GetAll(f32 *x, f32 *y, f32 *z, f32 *vx, f32 *vy, f32 *vz) override;
  u8 SetAll(f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz) override;
  void SetPower(u32 power) override;
  void GetMinimumDistance(f32 *distance) override;
  u8 SetMinimumDistance(f32 distance) override;
  void GetMaximumDistance(f32 *distance) override;
  u8 SetMaximumDistance(f32 distance) override;
  u8 SetEffectsLevel(i32 level) override;
  u8 CreateBuffer(_DSBUFFERDESC *description) override;
  u8 ReleasePrimaryBuffer() override;
  u8 OpenStream(u8 reload) override;

private:
  // SDL producers leave these DirectSound interfaces null and select the original software path.
  IDirectSound3DBuffer *spatialBuffers[8];
  f32 minimumDistance, maximumDistance;
  u8 hardware;
  i32 attenuation;
  u32 unknown284;
  f32 position[3];
  u8 option;
};

#endif
