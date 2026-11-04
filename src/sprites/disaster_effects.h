#ifndef SC3_SPRITES_DISASTER_EFFECTS_H
#define SC3_SPRITES_DISASTER_EFFECTS_H

#include <list>
#include <vector>

#include "common/message_queue.h"
#include "network/street_traffic.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"

class GxImageBuilder;

// The rain's concrete steam sprite owns its animation stage and frame, without a separate interface identity.
class SpToxicSteam : public SpBasicSprite {
public:
  SpToxicSteam();
  ~SpToxicSteam() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  u8 Init(i32 x, i32 z);
  u8 StopSteaming();

private:
  static u32 GetSpriteKey(i32 stage, i32 zoom, bool mask);
  void SetSpanRects();
  u8 SetSteamStage(i32 stage);
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  u8 registered, frame;
  u32 elapsed;
  i32 stage;
};

// Original acid-rain secondary interface, IID8496DAE7, with its two display operations.
class SpAcidRainDisplay : public ScUnknown {
public:
  virtual u8 Init(i32 x, i32 z, i32 altitude) = 0;
  virtual u8 StopRaining() = 0;
};

// The original rain object combines a basic sprite and a secondary display, with five zoom-specific image sets.
// Rain retains its concrete steam sprite while the column reaches the ground.
class SpAcidRain : public SpBasicSprite, public SpAcidRainDisplay {
public:
  SpAcidRain();
  ~SpAcidRain() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  u8 Init(i32 x, i32 z, i32 altitude) override;
  u8 StopRaining() override;
  static u32 GetSpriteKey(i32 zoom, u32 base);

private:
  void InitializeBufferSizeInfo();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void BlitWithSourceWrapAround(GxBuffer *target, GxImageBuilder *span, GxBuffer *mask, GxBuffer *piece,
      const ScRect &rect, i32 zoom, ScRect *clip, i32 length, i32 source_y, i32 piece_y, i32 target_y) const;
  void DrawRainBottom(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
      i32 zoom, ScRect *clip) const;
  void DrawRainMiddle(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
      i32 zoom, ScRect *clip) const;
  void DrawRainTop(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
      i32 zoom, ScRect *clip) const;
  SpToxicSteam *steam;
  i32 x, altitude, z;
  u8 stopping, registered;
  GxBuffer *middle[5], *bottom[5];
  i32 height;
  f32 current_height, stopping_height, offset, speed;
  u8 byte284[5];
  i32 band_first[5], band_second[5], band_third[5], band_fourth[5], band_fifth[5];
  i32 top_height[5], bottom_height[5], middle_height[5];
};

ScUnknown *SpCreateAcidRainDisplay();

// Original space-debris display retains separate message and animation-tick identities.
// Private scheduling and trajectory creation retain their reached effects with qualified nominal returns.
class SpSpaceDebrisDisplay : public SpDebrisDisplay, public SpTickable {
public:
  SpSpaceDebrisDisplay();
  virtual ~SpSpaceDebrisDisplay();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  u8 Init() override;
  u8 Shutdown() override;
  u8 Start() override;
  u8 Stop() override;
  void SetAverageDebrisTime(u32 duration) override;
  u32 GetCountOfRemainingSpaceDebris() override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;

private:
  // The original private return spellings are unresolved; these declarations preserve their reached effects.
  void SetNextDebrisTime();
  void CreateNewRandomDebris();
  u32 references;
  u8 registered;
  u32 elapsed, next_time, average_time;
};

// Original impact display owns a controller primary followed by the shared basic sprite and occupant views.
// Terrain notification and private animation helpers retain their reached effects with qualified nominal returns.
class SpSpaceDebrisImpactDisplay : public SpImpactDisplay, public SpBasicSprite {
public:
  SpSpaceDebrisImpactDisplay();
  ~SpSpaceDebrisImpactDisplay() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 size) override;
  void SetTimeToLive(u32 duration) override;
  void DoTerrainChanged(const ScRect &rectangle) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  // These private operations retain their observed effects; their discarded nominal returns remain unresolved.
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  u32 size_kind;
  u8 registered;
  u32 color_base, mask_base, age, frame_interval, time_to_live, elapsed, last_frame;
  i32 fade_direction;
};

// Original fire artwork owns its size, animation-frame state and two elapsed words after the shared sprite base.
class SpFireSprite : public SpBasicSprite {
public:
  explicit SpFireSprite(i32 size);
  ~SpFireSprite() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  // Private method names and inputs are established; nominal results remain qualified.
  u8 SetSize(i32 size);
  void SetSpanRects();

  i8 size;
  u8 variation, registered, byte_203, frame, frame_count;
  u32 frame_interval, last_frame;
};

// Original fire controller contains the real message queue after its animation and sprite state.
// The queue implementation must be linked into each DLL separately to preserve its module-local instance count.
class SpFire : public SpFireSimulator, public SpTickable {
public:
  SpFire();
  virtual ~SpFire();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 SetLevel(i8 level, u8 deferred) override;
  u8 Init(ScOccupant *occupant, i8 mode) override;
  u8 Init(i32 x, i32 z, i32 y, i8 mode, u8 enabled) override;
  u32 ReleaseAsynchronous() override;
  void GetPositionForExplosion(i32 *x, i32 *z, i32 *y) override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;

private:
  // Original helper effects are recovered, with private discarded nominal returns kept provisional.
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void RemoveSprites();
  void CreateFireSprite(u32 index, u32 size, i32 x, i32 z, i32 y);
  void DestroySprite(u32 index);
  void DestroySprites(u32 first);
  u32 references, tick_time;
  u8 enabled, registered;
  ScOccupant *occupant;
  u8 level;
  i32 cell_x, cell_z, cell_y;
  SpFireSprite *sprites[3];
  ScVec3F position;
  ScQueue messages;
};

// Original shadow changes the occupant's concrete identity without adding fields to the basic sprite.
class SpObjectShadow : public SpBasicSprite {
public:
  ~SpObjectShadow() override;
};

// Tornado debris extends the original shared debris sprite with its orbit center and motion parameters.
class SpTornadoDebris : public SpDebris {
public:
  SpTornadoDebris();
  ~SpTornadoDebris() override;
  u8 Init(ScVec3F *center, ScVec3F *motion, i32 variant, i8 style);
  i32 Tick(u32 elapsed) override;

private:
  // Original private return spelling is unresolved; callers consume only the two vector mutations.
  void GetLinearVectorAndVelocity(ScVec3F *out_velocity, ScVec3F *out_position);
  u8 active;
  ScVec3F center;
  f32 radius, radial_speed, target_height, scale, angle, angular_speed, vertical_speed;
};

// Original whirlpool debris owns a primary controller and the shared sprite and occupant identities.
class SpWhirlpoolPiece : public SpWhirlpoolDebris, public SpBasicSprite {
public:
  SpWhirlpoolPiece();
  ~SpWhirlpoolPiece() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, i32 variant) override;
  u8 ChangePosition(CtCoord *position) override;
  void SetTimeBetweenFrames(u32 duration) override;
  void SetTimeToLive(u32 duration) override;
  SpWhirlpoolDebris *MakeNewWhirlpoolDebris() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  // The private helper returns remain unresolved; reached effects retain their original ordering.
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void SetFadeDirection(i32 direction);
  i32 variant;
  u32 image_base;
  u8 registered, frame, frame_count;
  u32 frame_interval, time_to_live, elapsed, last_frame;
  i32 fade_direction;
  u32 word_236;
};

// The original whirlpool owns two artwork sprites and an STL list of abducted occupants.
// Linux list symbols establish the original container; its native storage need not match the retail allocator ABI.
class SpWhirlpool : public SpWhirlpoolSimulator, public SpBasicSprite {
public:
  SpWhirlpool();
  ~SpWhirlpool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(const ScVec3F &position, i32 variant, i8 mode, f32 speed) override;
  void SetMicrosecondsBetweenFrames(u32 duration) override;
  void SetObjectFastestSpeedTimes100000000(u32 speed) override;
  void SetOuterGravityDistance(u32 distance) override;
  i32 GetWhirlpoolDiameterAnimUnits() override;
  void GetLocation(i32 *x, i32 *z) override;
  void DoFade(u8 visible, u8 notify) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  // Each record retains its occupant and any queried vehicle/debris views until abduction cleanup releases them.
  struct AbductedObject {
    ScOccupant *occupant;
    NwVehicle *object_interface;
    SpWhirlpoolDebris *debris;
    u32 opacity, phase, flags;
  };
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void ClearAbductionList();
  void SetSpanRects();
  u8 GetAndAddMiscellaneousObjectsToAbductionList();
  u8 AddToAbductionList(ScOccupant *occupant);
  void DestroyAnyLocalBridges();
  void DoDisappearanceSplash(AbductedObject *object, ScVec3F *position);
  void SimulateAbductedOccupants(u32 elapsed);
  void SimulateRotatingDebris(u32 elapsed);
  static u8 WouldWhirlpoolFitInLocation(ScVec3F *position);
  u8 CanOccupantBeAbducted(ScOccupant *occupant);
  u32 GetSpriteKey(i32 zoom, i32 variant_index, u32 base);
  bool GetNewVelocityForWhirlpool(const ScVec3F &position, const ScVec3F &velocity,
      ScVec3F *out_velocity, f32 speed, f32 distance);
  u8 registered, notify, frame, frame_count, fade_frame_count;
  i8 fade_step, variant;
  i32 size_kind, state, next_state;
  u32 ticks;
  f32 fastest_speed, outer_distance;
  f32 speed;
  ScVec3F velocity;
  u32 elapsed, last_frame, frame_interval;
  SpBasicSprite *artwork[2];
  std::list<AbductedObject> abducted;
};

// Original tornado controller has primary, sprite and effects-occupant identities and owns five auxiliary sprites.
class SpTornado : public SpTornadoSimulator, public SpBasicSprite {
public:
  SpTornado();
  ~SpTornado() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 y, const ScVec3F &direction, i8 severity) override;
  u8 SetPosition(i32 x, i32 z, i32 y) override;
  void GetPosition(i32 *x, i32 *z, i32 *y) override;
  u8 SetDirection(const ScVec3F &direction) override;
  u8 SetSpeed(const f32 &speed) override;
  i32 GetCurrentSurfaceType() override;
  void DoFade(u8 visible, u8 notify) override;
  void CreateTornadoDebris() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  // The original discarded private return spellings remain unresolved.
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void AddRemoveBaseGraphic(u8 enabled);
  i32 GetSurfaceTypeForCell(i32 x, i32 z);
  void SendNotification(const ScMessage &message);
  u8 registered;
  i8 fade_frame;
  i8 fade_frames;
  i8 fade_step;
  i8 frame, frame_count, variant;
  i32 state, next_state, graphic;
  u8 notify;
  ScVec3F velocity;
  f32 speed;
  u32 elapsed, last_frame, ticks, interval;
  i32 surface_type;
  ScOccupant *last_occupant;
  i32 cell_x, cell_z;
  ScVec3F vector_276;
  f32 height;
  SpBasicSprite *base_sprite, *overlay_sprite, *other_sprites[3];
};

ScUnknown *SpCreateSpaceDebrisDisplay();
ScUnknown *SpCreateSpaceDebrisImpactDisplay();
ScUnknown *SpCreateFireSimulator();
ScUnknown *SpCreateTornadoSimulator();
ScUnknown *SpCreateWhirlpoolSimulator();

class DsLocustControl;

// Original death-spray secondary identity exposes the same interval slot as the distinct smoke interface.
class SpDeathSprayView : public ScUnknown {
public:
  virtual u32 SetTimeBetweenFrames(u32 duration) = 0;
};

// Original 228-byte death-spray sprite; its animation body is folded with the unrelated smoke sprite.
class SpDeathSpray : public SpBasicSprite, public SpDeathSprayView {
public:
  SpDeathSpray();
  ~SpDeathSpray() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 SetTimeBetweenFrames(u32 duration) override;
  u8 Init(i32 x, i32 z, i32 altitude, i32 variant);
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(i32 zoom, i32 variant, u32 base);
  i32 variant;
  u32 elapsed, frame_elapsed, word216;
  u8 registered, frame, frame_count;
  u32 frame_interval;
};

// The installed locust instruction node copies a 24-byte game record; individual payload spellings remain unresolved.
struct SpLocustInstruction {
  u32 type, payload[5];
};

// Original 44-byte PC record for a large locust's overlay, motion and borrowed image references.
struct SpBigLocust {
  ScPoint position;
  ScVec3F velocity;
  GxBuffer *color, *mask;
  u32 variant;
  u8 byte32;
  u32 overlay;
  u8 overlay_active;
};

// Original locust primary identity followed by the basic-sprite and occupant identities.
// The original instruction processing and large-locust overlay simulation use their actual shared effects state.
class SpLocust : public SpLocustSimulator, public SpBasicSprite {
public:
  SpLocust();
  ~SpLocust() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(const ScVec3F &position, i32 level, i8 initial_state) override;
  void Shutdown() override;
  void ClearAllInstructions() override;
  void GetPosition(u32 *x, u32 *altitude, u32 *z) override;
  void DestroySelfAsSoonAsPossible() override;
  void SetGroup(SpLocustGroupSimulator *group) override;
  u8 SetState(SpLocustState state) override;
  i32 GetState() const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;

private:
  friend class SpLocustGroup;
  static u32 GetSpriteKey(i32 zoom, i32 state, u32 base);
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void GenerateNewRandomDestinationPosition();
  void GenerateNewSpecificDestinationPosition(const ScVec3F &from, const ScVec3F &target);
  u8 AddInstruction(const SpLocustInstruction &instruction);
  u8 PrepNewInstruction();
  u8 DoInstructionState(bool *moved, bool *changed, f32 elapsed);
  i32 FixFrontMoveInstructionToAvoidObjects();
  void CheckForDestroyedTrees();
  void DoBigLocustsSimulation(f32 elapsed);
  u8 registered;
  std::list<SpLocustInstruction> instructions;
  u8 instructions_changed, byte213, limit;
  i8 state;
  u32 word216, word220;
  ScVec3F vector224, vector236;
  f32 speed;
  u32 word252, word256, word260;
  SpBasicSprite *shadow;
  i32 cell_x, cell_y, cell_z;
  DsLocustControl *dependency;
  SpLocustGroupSimulator *group;
  u8 byte288;
  std::vector<SpBigLocust> big_locusts;
  u8 byte304;
};

// The original locust group owns its child vector and an embedded deferred-message queue.
// Unrecovered global-pool and instruction operations remain pure virtual through the original interface.
class SpLocustGroup : public SpLocustGroupSimulator {
public:
  SpLocustGroup();
  virtual ~SpLocustGroup();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  u8 Init(u32 count, i32 level) override;
  void Shutdown() override;
  u8 SetCount(u32 count) override;
  u32 GetCount() override;
  u8 AddLocustsToGroup(SpLocustSimulator *locust, u8 add, u8 destroy) override;
  SpLocustSimulator *GetLocusts(u32 index) override;
  bool DoesLocustsBelongToGroup(const SpLocustSimulator *locust) const override;
  bool DoesAnyLocustsExistAnywhere() const override;
  bool DoesLocustsExistAnywhere(const SpLocustSimulator *locust) const override;
  u32 GetAllLocustsAnywhere(SpLocustSimulator **objects, u32 *count) const override;
  u32 GetListOfCurrentlyVisibleLocusts(SpLocustSimulator **objects, u32 *count) const override;
  u8 GetEatingSwarmsCount(u32 *eating, u32 *visible) const override;
  i32 IsAnyLocustsCurrentlyVisible() const override;
  u32 ReleaseAsynchronous() override;
  u32 DestroyAllLocusts(u8 deferred) override;
  u8 DistributeTargetListAmongSwarms(ScVec3U *targets, u32 count) override;
  void DropDDTTrail(i32 x, i32 z, i32 altitude) override;
  u8 AddInstructionAttackCrops(i32 x, i32 z, i32 altitude, i32 index, u8 deferred) override;
  u8 AddInstructionDisappear(i32 index, u8 deferred) override;
  u8 AddInstructionMigrateToArea(const ScRect &area, i32 index, u8 deferred) override;
  u8 AddInstructionPoisonSwarm(i32 x, i32 z, SpLocustSimulator *swarm, u8 deferred) override;
  u8 AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) override;
  i32 GetInstructionLoad() override;

private:
  i32 GetLocustsWithLowestInstructionLoad();
  u32 references;
  std::vector<SpLocustSimulator *> locusts;
  i32 level;
  ScQueue instructions;
};

ScUnknown *SpCreateLocustSimulator();
ScUnknown *SpCreateLocustGroupSimulator();

#endif
