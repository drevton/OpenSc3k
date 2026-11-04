#ifndef SC3_AUDIO_AUDIO_EFFECTS_H
#define SC3_AUDIO_AUDIO_EFFECTS_H

#include <list>

#include "application/application_core.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

class AuAudioManager;
class AuSound;
class AuSound3D;

// The recovered 56-byte base of AUDIO's sound effects. The remaining virtual slots are still under reconstruction;
// this partial interface stays abstract and must not be used to instantiate an effect yet.
class AuAudioEffect : public ScUnknown {
public:
  AuAudioEffect();
  AuAudioEffect(u32 identifier, u32 category, u8 directPath, u32 value);
  virtual ~AuAudioEffect();
  virtual u32 ReleaseBaseReference();
  virtual u32 GetBaseReferenceCount();
  virtual u8 Init(AuAudioManager *manager);
  virtual u8 Shutdown();
  // The manager invokes this hook during each audio update.
  virtual u8 Update() = 0;
  virtual u8 Play(i32 volume) = 0;
  virtual u8 IsPlaying(i32 value) = 0;
  // The next two slots receive the original event payloads; their nominal purposes remain unresolved.
  virtual void UnresolvedSlot44() = 0;
  virtual void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) = 0;
  virtual u8 UnresolvedSlot52(uptr value) = 0;
  virtual u8 UnresolvedSlot56(uptr first, uptr second) = 0;
  virtual u8 UnresolvedSlot60(uptr first, uptr second) = 0;
  virtual u8 UnresolvedSlot64() = 0;
  virtual u8 GetFlag21();
  virtual u8 LoadSound() = 0;
  virtual u8 UnloadSound() = 0;
  // The installed hook ignores two 32-bit words; their original nominal types and signedness are unresolved.
  virtual u8 UnresolvedSlot80(u32 first, u32 second) = 0;
  virtual u8 UnresolvedSlot84(uptr value) = 0;
  virtual u32 GetValue32();
  // The deleting hook returns the deleted receiver's address; its original nominal return spelling is unresolved.
  virtual AuAudioEffect *Destroy();
  virtual u8 ConfigureBase(AuAudioManager *manager, u32 identifier, u32 category);
  // The second word is ignored by the common implementation; its original nominal type remains unresolved.
  virtual u8 SetBaseVolume(u32 volume, u32 value);

protected:
  u32 baseReferences, references, unknown12;
  AuAudioManager *manager;
  u8 initialized, byte21;
  u32 identifier, unknown28, unknown32, category, volume;
  u8 directPath;
  u32 unknown48, value52;
};

// The original one-shot effect extends the common base with a retained sound and an owned filename.
// The nominal types of unused words and the deleting hook's return spelling remain unresolved.
class AuSingleEffect : public AuAudioEffect {
public:
  AuSingleEffect();
  AuSingleEffect(u32 identifier, u32 category, u8 directPath, const ScStringView *filename, u8 resident,
      u8 factoryFlag, u32 volume, u32 playFlags);
  ~AuSingleEffect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;
  virtual u8 Configure(AuAudioManager *manager, u32 first, u32 second, const ScStringView *filename,
      u8 resident, u8 factoryFlag, u32 volume);

private:
  friend class AuBuildingSounds;
  AuSound *sound;
  u8 factoryFlag;
  ScString filename;
  u8 resident;
  u32 playFlags;
};

// The effect retains a spatial sound, its source filename and original event slots.
class AuEffect623F0B9C : public AuAudioEffect {
public:
  AuEffect623F0B9C();
  ~AuEffect623F0B9C() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;

private:
  AuSound3D *sound;
  u8 factoryFlag;
  ScString filename;
  u8 resident;
};

// Disaster and fading effects retain their original owned strings and known constructor fields.
// Their concrete virtual implementations remain under reconstruction.
class AuDisasterEffect : public AuAudioEffect {
public:
  AuDisasterEffect();
  AuDisasterEffect(u32 identifier, u32 category, u8 directPath, const ScStringView *path,
      u32 value84, u32 initialVolume, u32 value80);
  ~AuDisasterEffect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;
  u8 SetBaseVolume(u32 volume, u32 value) override;
  virtual i32 CalculateVolume();
  virtual void Refresh();

private:
  ScString path;
  AuSound *sound;
  u32 value80, value84;
  u8 byte88;
  u32 value92;
  u8 byte96;
};

class AuFadeEffect : public AuAudioEffect {
public:
  AuFadeEffect(u32 identifier, u32 category, u8 directPath, u8 byte104, u32 initialVolume,
      u8 byte116, u8 byte117, const ScStringView *first, const ScStringView *second);
  ~AuFadeEffect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;

private:
  i32 CurrentVolume();
  ScString first, second;
  AuSound *firstSound, *secondSound;
  u8 byte104;
  i32 unknown108, unknown112;
  u8 byte116, byte117;
};

// The actor effect owns three source strings; unidentified fields remain opaque until their uses are recovered.
class AuActorEffect : public AuAudioEffect {
public:
  AuActorEffect();
  AuActorEffect(u32 identifier, u32 category, u8 directPath, u32 value52, u32 value56,
      const ScStringView *first, const ScStringView *second, const ScStringView *third);
  ~AuActorEffect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;

private:
  void ClearActor(u32 identifier);
  u32 SetActor(u32 actor);
  i32 CalculateActorVolume(i32 x, i32 y, i32 z);
  u32 value56;
  AuSound3D *firstSound, *secondSound, *thirdSound;
  ScString first, second, third;
  u32 value132, unknown136, unknown140, unknown144, unknown148, unknown152, unknown156, unknown160;
  u32 unknown164, unknown168, unknown172, unknown176, unknown180;
};

// Sparse effects retain eight owned filenames and corresponding sound/value slots.
// The concrete playback interface and the nominal types of its trailing constructor words remain unresolved.
class AuSparseEffect : public AuAudioEffect {
public:
  AuSparseEffect();
  AuSparseEffect(u32 identifier, u32 category, u8 directPath, i32 count, const ScStringView *const *names,
      const i32 *firstValues, const i32 *secondValues, u32 value60, u32 value324, u32 value328, u32 value332,
      u32 value336);
  ~AuSparseEffect() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(AuAudioManager *manager) override;
  u8 Shutdown() override;
  u8 Update() override;
  u8 Play(i32 volume) override;
  u8 IsPlaying(i32 value) override;
  void UnresolvedSlot44() override;
  void UnresolvedSlot48(uptr first, uptr second, uptr third, uptr fourth) override;
  u8 UnresolvedSlot52(uptr value) override;
  u8 UnresolvedSlot56(uptr first, uptr second) override;
  u8 UnresolvedSlot60(uptr first, uptr second) override;
  u8 UnresolvedSlot64() override;
  u8 LoadSound() override;
  u8 UnloadSound() override;
  u8 UnresolvedSlot80(u32 first, u32 second) override;
  u8 UnresolvedSlot84(uptr value) override;

private:
  i32 count;
  u32 value60;
  ScString names[8];
  AuSound *sounds[8];
  i32 firstValues[8], secondValues[8];
  u8 byte320;
  u32 value324, value328, value332, value336;
};

class AuAudioManager;

// AUDIO's original INI reader adds the manager used by its sound-entry callbacks.
class AuAudioIni : public ApIniFile {
public:
  AuAudioIni(const ScString &path, AuAudioManager *manager);
  ~AuAudioIni() override;
  // Sound and music entry callback bodies remain incomplete.
  u8 LoadAudio();

private:
  // The installed parsers discard their incidental return and receive the reader again as a third argument.
  void ParseSingleSound(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseSingleSoundImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  void ParseActorSound(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseActorSoundImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  void ParseDisasterSound(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseDisasterSoundImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  void ParseFadingSound(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseFadingSoundImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  void ParseSparseSound(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseSparseSoundImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  void ParseMusic(const ScString &key, const ScString &value, AuAudioIni *reader);
  static void ParseMusicImpl(AuAudioIni *reader, const ScString &key, const ScString &value);
  friend void SC3_CDECL AuLoadSingleSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadSparseSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadDisasterSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadFadingSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadActorSound(const ScStringView &key, const ScStringView &value, void *reader);
  friend void SC3_CDECL AuLoadMusic(const ScStringView &key, const ScStringView &value, void *reader);
  AuAudioManager *manager;
};

// Original section callbacks borrow the reader as their context and register the supplied key/value pair.
void SC3_CDECL AuRegisterSingleSound(const ScString &key, const ScString &value, ApIniContext *reader);
void SC3_CDECL AuRegisterSparseSound(const ScString &key, const ScString &value, ApIniContext *reader);
void SC3_CDECL AuRegisterDisasterSound(const ScString &key, const ScString &value, ApIniContext *reader);
void SC3_CDECL AuRegisterFadingSound(const ScString &key, const ScString &value, ApIniContext *reader);
void SC3_CDECL AuRegisterActorSound(const ScString &key, const ScString &value, ApIniContext *reader);
void SC3_CDECL AuRegisterMusic(const ScString &key, const ScString &value, ApIniContext *reader);

// Original registry enumeration callbacks receive the reader through the registry's generic ABI.
void SC3_CDECL AuLoadSingleSound(const ScStringView &key, const ScStringView &value, void *reader);
void SC3_CDECL AuLoadSparseSound(const ScStringView &key, const ScStringView &value, void *reader);
void SC3_CDECL AuLoadDisasterSound(const ScStringView &key, const ScStringView &value, void *reader);
void SC3_CDECL AuLoadFadingSound(const ScStringView &key, const ScStringView &value, void *reader);
void SC3_CDECL AuLoadActorSound(const ScStringView &key, const ScStringView &value, void *reader);
void SC3_CDECL AuLoadMusic(const ScStringView &key, const ScStringView &value, void *reader);

class AuAudioManager;

// The building ambience tracker borrows its manager and owns a list of active sound identifiers.
class AuBuildingAmbience {
public:
  AuBuildingAmbience();
  ~AuBuildingAmbience();
  // The installed operation returns AL=1; its private bool/BYTE nominal spelling remains unresolved.
  u8 SetManager(AuAudioManager *manager);
  u8 Update();

private:
  AuAudioManager *manager;
  std::list<i32> soundIds;
};

// Modern 32-bit STL keeps a trailing size word after the original eight-byte receiver.

class AuAudioManager;
class AuSound;

// The original building-sound player keeps four sound references and their identifiers.
class AuBuildingSounds {
public:
  AuBuildingSounds();
  void Init(AuAudioManager *owner);
  void Shutdown();
  void Update();
  i32 NextSlot();
  // The only recovered caller discards the return; its original nominal return type is unresolved.
  void Play(i32 x, i32 z);

private:
  friend class AuAudioManager;
  u8 initialized;
  AuAudioManager *manager;
  i32 current;
  AuSound *sounds[4];
  i32 identifiers[4];
};

#endif
