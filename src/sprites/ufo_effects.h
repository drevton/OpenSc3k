#ifndef SC3_SPRITES_UFO_EFFECTS_H
#define SC3_SPRITES_UFO_EFFECTS_H

#include <list>

#include "city/sc_stopwatch.h"
#include "common/message_queue.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"

class SpAcidRain;

// The toxic cloud combines the real basic sprite with its secondary display identity.
// The cloud owns the concrete rain display while it is active.
class SpToxicCloud : public SpBasicSprite, public SpToxicCloudDisplay {
public:
  SpToxicCloud();
  ~SpToxicCloud() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  u8 Init(uptr owner_id, i32 x, i32 z, i32 altitude) override;
  u8 DoFade(u8 visible, u8 notify) override;
  void SetVelocity(const ScVec3F &velocity) override;
  u8 StartRaining() override;
  u8 StopRaining() override;

private:
  static u32 GetSpriteKey(i32 zoom, i32 variant, i32 base);
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  ScVec3F velocity;
  u8 byte216, registered;
  u32 word220, elapsed, duration;
  f32 fade_rate;
  u8 notify;
  SpBasicSprite *shadow;
  SpAcidRain *rain;
  u8 stop_rain;
  uptr owner_id; // Borrowed disaster instance identity, returned unchanged in the retirement message.
};

ScUnknown *SpCreateToxicCloudDisplay();

class SpUfoGroup;

// Original SIMSPR director factory; returns an unretained primary UFO-group interface.
ScUnknown *SpCreateUfoGroup();

// The installed private helpers return Boolean results; their original literal names are not established.
i32 SpUfoPassedPosition2D(const ScVec3F &start, const ScVec3F &current, const ScVec3F &target);
bool SpUfoPassedPosition3D(const ScVec3F &start, const ScVec3F &current, const ScVec3F &target);

// Original UFO command values shared by instruction queues and deferred group messages.
// Keep the instruction field and public payloads as DWORDs; these names do not change their storage or ABI.
enum SpUfoOp : u32 {
  kSpUfoOpWait = 1,
  kSpUfoOpMove = 2,
  kSpUfoOpVaporize = 3,
  kSpUfoOpExplode = 4,
  kSpUfoOpFadeOut = 5,
  kSpUfoOpDropBomb = 6,
  kSpUfoOpCropCircle = 7,
  kSpUfoOpAbduct = 8,
  kSpUfoOpSetSpeed = 9,
  kSpUfoOpSpawn = 10,
};

// Original 36-byte PC instruction data; the opcode selects the active payload and the remaining words are unresolved.
struct SpUfoInstruction {
  u32 opcode;

  union {
    ScVec3F position;
    ScOccupant *occupant;
    SpUfoGroup *group; // Borrowed from the group that submits the spawn command.
    f32 speed;

    struct {
      u32 duration, elapsed;
    } wait;
  };

  u8 effectState, reserved[3];
  u32 elapsed;
  SpBasicSprite *beam; // Owned reference; KillCurrentInstruction removes, unprepares and releases it.
  u32 word28, word32;
};

// The UFO's primary simulator identity precedes the original basic-sprite and effects-occupant bases.
// Native list storage preserves the recovered ownership and instruction order; original-memory overlays are not used.
class SpUfo : public SpUfoSimulator, public SpBasicSprite {
public:
  SpUfo();
  ~SpUfo() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  u8 Init(const ScVec3F &position, u8 automaticAttack, i32 mode, i8 level) override;
  void GetPosition(u32 *x, u32 *z, u32 *altitude) override;
  void Shutdown() override; // The installed private nominal return remains unidentified.
  void ShootPlasmaBall(i32 x, i32 z, i32 altitude) override;
  u8 SetAutoAttack(u8 enabled) override;
  void ClearAllInstructions() override;
  void DestroySelfAsSoonAsPossible() override;
  u8 AddInstruction(const SpUfoInstruction &instruction);
  i32 CanOccupantBeAbducted(ScOccupant *occupant);
  u32 GetMicrosecondsTillNextPlasmaShot();
  // The installed body leaves a pointer in EAX; its discarded nominal return remains unresolved.
  void GenerateNewSpecificDestinationPosition(const ScVec3F &from, const ScVec3F &to);
  void GenerateNewRandomDestinationPosition();          // Original discarded nominal return remains unresolved.
  u32 GetSpriteKey(i32 zoom, i32 difficulty, u32 base); // Original result's signedness is unresolved.
  void SetSpanRects();                                  // Callers discard the original private nominal return.
  u8 FixFrontMoveInstructionToAvoidObjects();
  void PrepareInstructionSpecificSprites(i32 zoom, i32 rotation);
  void UnprepareInstructionSpecificSprites(i32 zoom, i32 rotation);
  void KillCurrentInstruction(); // Discarded nominal return is unidentified.
  u8 AddToAbductionList(ScOccupant *occupant);
  u8 GetAndAddMiscellaneousObjectsToAbductionList(u32 count);
  void ClearAbductionList(); // Installed callers discard the lock helper's residual return.
  u8 CurrentlyDoingBuildingVaporizationOrCropCircleProduction();

protected:
  friend class SpUfoGroup;
  u8 registered, automaticAttack;
  // Linux cSC3UFOSimulator InstructionData list symbols establish the original STL facility.
  std::list<SpUfoInstruction> instructions;
  u8 instructionsActive;
  // The Linux list<occupant *> symbols and installed sentinel establish this original STL facility.
  std::list<ScOccupant *> abducted;
  i8 state220;
  u8 state221;
  i8 level;
  i8 state223;
  i32 word224, turnDirection;
  ScVec3F destination, direction;
  f32 movementSpeed;
  u32 word260, word264, word268, word272;
  ScStopwatch soundTimer;
  SpBasicSprite *shadow;
  u8 state340;
  GxBuffer *image344;
  ScPoint overlayPosition;
  ScVec3F vector356;
  u32 overlayId;
  void PrepNewInstruction(); // Original byte return is observed; its nominal declaration remains unidentified.
  u8 DoInstructionState(u8 *moved, u8 *frameChanged, f32 elapsed);
  void DoBigUFOSimulation(f32 elapsed);                       // Discarded private nominal return remains unidentified.
  void AddToAnimationSystem();                                // Discarded original nominal return remains unidentified.
  void RemoveFromAnimationSystem();                           // Discarded original nominal return remains unidentified.
  void TurnOffAnyCurrentlyPlayingSounds(u8 force);            // Discarded original nominal return remains unidentified.
  void PrepareBeamSprite(i32 zoom, i32 rotation, i32 opcode); // Discarded nominal return unidentified.
  void UnprepareBeamSprite(i32 zoom, i32 rotation, i32 opcode); // Discarded nominal return unidentified.
};

// The group retains its ordered UFO interfaces and drains its original deferred message queue.
// Factories return the primary interface without acquiring a reference.
class SpUfoGroup : public SpUfoGroupSimulator {
public:
  SpUfoGroup();
  virtual ~SpUfoGroup();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  u8 Init(u8 enabled, u32 count, i32 level) override;
  u8 Shutdown() override;
  u8 SetCount(u32 count) override;
  u32 GetCount() override;
  u8 AddUFOToGroup(SpUfoSimulator *ufo, u8 add, u8 shutdown) override;
  SpUfoSimulator *GetUFO(u32 index) override;
  bool DoesUFOBelongToGroup(const SpUfoSimulator *ufo) const override;
  bool DoesAnyUFOExistAnywhere() const override;
  bool DoesUFOExistAnywhere(const SpUfoSimulator *ufo) const override;
  u32 GetAllUFOsAnywhere(SpUfoSimulator **objects, u32 *count) const override;
  u32 GetListOfCurrentlyVisibleUFOs(SpUfoSimulator **objects, u32 *count) const override;
  i32 IsAnyUFOCurrentlyVisible() const override;
  u32 ReleaseAsynchronous() override;
  u32 DestroyAllUFOs(u8 deferred) override;
  u32 GetInstructionLoad() override;
  i32 GetUFOWithLowestInstructionLoad(); // Observed full-word result; private nominal declaration is unidentified.
  u8 AddInstructionVaporizeBuilding(i32 x, i32 z, i32 index, u8 deferred) override;
  u8 AddInstructionMakeCropCircle(i32 x, i32 z, i32 index, u8 deferred) override;
  u8 AddInstructionSelfDestruct(u8 explode, i32 index, u8 deferred) override;
  u8 AddInstructionSuckUpPerson(ScOccupant *occupant, i32 index, u8 deferred) override;
  u8 AddInstructionSpawnUFO(u8 enabled, i32 index, u8 deferred) override;
  u8 AddInstructionMigrateToArea(const ScRect &bounds, i32 index, u8 deferred) override;
  u8 AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) override;
  u8 AddInstructionDropBomb(i32 x, i32 z, i32 index, u8 deferred) override;

protected:
  u32 references;
  ScArray<SpUfoSimulator *> members;
  i32 level;
  u8 automaticAttack;
  ScQueue messages;
};

#endif
