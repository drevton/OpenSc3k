#ifndef SC3_SPRITES_SP_EFFECT_H
#define SC3_SPRITES_SP_EFFECT_H

#include <atomic>

#include "city/api.h"
#include "city/maps.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "occupants/animation_manager.h"
#include "sprites/sp_tick_manager.h"

// Trajectory source signatures distinguish space debris from UFO plasma in shared impact notifications.
enum SpTrajectorySignature : u32 {
  kSpSpaceDebrisTrajectorySignature = 0x56773821,
  kSpUfoPlasmaTrajectorySignature = 0x237793c6,
};

class ScOccupant;
class RsStream;
class SpLocustGroupSimulator;
enum class SpLocustState : i32;
struct ScVec3F;
struct ScVec3U;
struct CtCoord;
struct ScRect;

// Primary82B5A0DA of registered class92B5A0DA, final table SIMSPR10067D44.
// Names describe the recovered operations; private nominal class/method spellings remain unavailable.
class SpParticleEffect : public ScUnknown {
public:
  virtual void Init() = 0; // Original12=24CCE is an empty RET; exact Linux cSC3ParticleSimulator::Init.
  // Original16=24CCF, RET20; signed fixed-coordinate DWORDs, full mode/count words, defined DWORD particle count.
  virtual u32 DoParticleGeneration(i32 x, i32 z, i32 y, i32 mode, i32 count) = 0;
};

// Primary22A34686 of classA223BE6C; Linux identifies the complete earthquake interface through slot20.
// Output references use explicit pointers. Discarded private returns retain their qualified used-effects void spelling.
class SpEarthquakeView : public ScUnknown {
public:
  virtual u8 Init(const ScVec3I *first, const ScVec3I *second, ScVec3I *third, i32 duration, i8 severity, i32 argument,
      bool start) = 0;
  virtual void GetEarthquakePosition(ScVec3I *first, ScVec3I *second, ScVec3I *third) = 0;
  virtual void DoEarthquakeEffect(i32 severity, i32 duration, i32 argument) = 0;
};

// Actual68-byte PC earthquake simulator with separate primary and animation-tick identities.
// Its three point values are copied by Init; the animation phase controls notification and screen shaking.
class SpEarthquake final : public SpEarthquakeView, public SpTickable {
public:
  SpEarthquake();
  ~SpEarthquake();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(const ScVec3I *first, const ScVec3I *second, ScVec3I *third, i32 duration, i8 severity, i32 argument,
      bool start) override;
  void GetEarthquakePosition(ScVec3I *first, ScVec3I *second, ScVec3I *third) override;
  void DoEarthquakeEffect(i32 severity, i32 duration, i32 argument) override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  u32 references;
  i8 severity;
  u8 registered;
  u32 elapsed, word20, duration, word28;
  ScVec3I first, second, third;
};

ScUnknown *SpCreateEarthquakeSimulator();

// PrimaryE2A34505 of registered classF2A34505, final table SIMSPR10064CAC.
// Exact Linux names identify cISC3ExplosionSimulator; retain its complete original primary interface.
class SpExplosionView : public ScUnknown {
public:
  // Original12=264A4, RET24; mode is sign-extended from its byte, the other byte arguments remain raw.
  virtual u8 Init(i32 x, i32 z, i32 y, u8 severity, i8 mode, u8 start) = 0;
  // Original16=26615 stores three signed radii and leaves the final input in EAX; nominal result stays qualified.
  virtual i32 SetBlastRadius(i32 first, i32 second, i32 third) = 0;
  virtual u8 GetOccupantTrajectories(ScVec3F *positions, ScVec3F *out, u32 count) = 0;
};

// Original nuclear-explosion controllerD223BE6C, distinct from the explosion and debris interfaces.
// Linux names establish the five initialization inputs and the cell-to-explosion position operation.
class SpNuclearView : public ScUnknown {
public:
  virtual u8 Init(i32 x, i32 z, i32 altitude, i8 first, i8 second) = 0;
  virtual u8 GetCoordinatesForPowerPlantExplosion(i32 cell_x, i32 cell_z, i32 *x, i32 *z, i32 *altitude) = 0;
};

// Original synchronous blast message rows pair each retained occupant with its outward velocity vector.
// Native-width pointers preserve the same logical payload without transporting original foreign addresses.
struct SpBlastTarget {
  ScOccupant *occupant;
  ScVec3F velocity;
};

// Original explosion-debris controller has only its six-argument initialization after the Unknown prefix.
class SpDebrisView : public ScUnknown {
public:
  virtual u8 Init(i32 x, i32 z, i32 altitude, const ScVec3F &velocity, i32 variant, i8 mode) = 0;
};

// Primary0223BE6C of registered class1223BE6C; Linux symbols identify the concrete producer as cSC3FireSimulator.
// This declaration exposes its reached primary contract; construction, fields and implementation remain with SIMSPR.
class SpFireSimulator : public ScUnknown {
public:
  // Original12=2ED24, RET8/AL1. Preserve raw PC bytes; Linux names prove signed level and Boolean deferred semantics.
  virtual u8 SetLevel(i8 level, u8 deferred) = 0;
  // MSVC reverses this overload group: occupant Init occupies20 and the five-argument Init occupies16.
  // PC2E972 RET8/AL0or1 ignores its mode operand; the nominal PC mode width remains qualified by Linux's signed byte.
  virtual u8 Init(ScOccupant *occupant, i8 mode) = 0;
  // PC2E9F2 RET20/AL0or1; Linux Init(iiiScb) proves five separate operands, including signed mode and Boolean gate.
  virtual u8 Init(i32 x, i32 z, i32 y, i8 mode, u8 enabled) = 0;
  // Original24=2EA78 writes three fixed-coordinate outputs; this native used-effects void spelling is provisional.
  virtual void GetPositionForExplosion(i32 *x, i32 *z, i32 *y) = 0;
  // Original28=2E944 sends3378D0F9; native u32 carries observed refcount-minus-one. Nominal return is unresolved.
  virtual u32 ReleaseAsynchronous() = 0;
};

// PrimaryC504A44E of registered classC504A44F; Linux exact-IID QI names cSC3SpaceDebrisImpactDisplay.
// This reached declaration leaves concrete sprite production with SIMSPR.
class SpImpactDisplay : public ScUnknown {
public:
  // Original12=3810E, RET12 and raw AL0/1; Linux Init(iii) proves three separate signed operands.
  virtual u8 Init(i32 x, i32 z, i32 size) = 0;
  // equals the input and does not prove the original nominal return declaration.
  virtual void SetTimeToLive(u32 duration) = 0;
  // Original20=3824D receives the changed cell rectangle. Its discarded nominal return remains unresolved.
  virtual void DoTerrainChanged(const ScRect &rectangle) = 0;
};

// Primary650496CC of class650496CD; exact Linux DAF08 names cSC3SpaceDebrisDisplay.
// PC registration4C8B6/factory4CF84 constructs37C52 with primary66A24; concrete production remains with SIMSPR.
class SpDebrisDisplay : public ScMessageTarget {
public:
  // Original16=37CDC is the independent initialization entry, despite the merged preceding destructor decompilation.
  // Linux DAD48 Init(), PC AL1; the nominal bool/BYTE return spelling remains unresolved.
  virtual u8 Init() = 0;
  // Original20=37D4E; Linux Shutdown, private nominal bool/BYTE return remains unresolved.
  virtual u8 Shutdown() = 0;
  // Original24=37DBE and28=37DCA have no arguments and AL1; Linux DAEB0/DAEE0 name Start/Stop.
  virtual u8 Start() = 0;
  virtual u8 Stop() = 0;
  // Original32=37DD1 stores the full word; Linux DAEF4 takes unsigned int. Used-effects void is provisional
  virtual void SetAverageDebrisTime(u32 duration) = 0;
  // Original36=37E8F queries trajectory class56773821. Linux DB060 gives this name; full u32 is a lossless
  // count carrier, with the original signed/unsigned return typedef still unresolved.
  virtual u32 GetCountOfRemainingSpaceDebris() = 0;
};

// Primary4223BE6C of registered class5223BE6C; exact Linux IID identifies cSC3TornadoSimulator.
// This reached native subset is not a foreign vtable overlay; concrete construction remains with SIMSPR.
class SpTornadoSimulator : public ScUnknown {
public:
  // Original12=2F976, RET20/raw AL0or1; Linux Init(iii,const cRZ3DPointFloat32&,signed char).
  virtual u8 Init(i32 x, i32 z, i32 y, const ScVec3F &direction, i8 severity) = 0;
  // Original16=2F88B ignores three words and returns AL1; nominal bool/BYTE spelling remains unresolved.
  virtual u8 SetPosition(i32 x, i32 z, i32 y) = 0;
  // Original20=314B8 writes fixed-coordinate X/Z/altitude; its discarded nominal return remains unresolved.
  virtual void GetPosition(i32 *x, i32 *z, i32 *y) = 0;
  // Original24=2FBD5 normalizes the horizontal direction and returns AL1; nominal byte spelling is provisional.
  virtual u8 SetDirection(const ScVec3F &direction) = 0;
  // Original28=2FC40, RET4/raw AL1; the referenced float is input and the nominal bool/BYTE spelling is unresolved.
  virtual u8 SetSpeed(const f32 &speed) = 0;
  // Original32=2F890 returns the full stored surface word; nominal signedness is unresolved.
  virtual i32 GetCurrentSurfaceType() = 0;
  // Original36=2FC5D takes two raw bytes; Linux DoFade(bool,bool). Used-effects void is provisional because
  // the original nominal return is unresolved and the caller discards the residual register value.
  virtual void DoFade(u8 visible, u8 notify) = 0;
  // Original40=3126D takes no arguments; Linux names CreateTornadoDebris. Used-effects void is provisional:
  // handler/Release register residue does not prove the original nominal return declaration.
  virtual void CreateTornadoDebris() = 0;
};

// Primary8223BE6C of registered class9223BE6C; exact Linux QI identifies cSC3WhirlpoolSimulator.
// This reached native subset leaves its concrete sprite fields and production with SIMSPR.
class SpWhirlpoolSimulator : public ScUnknown {
public:
  // PC ignores the full variant word, stores the signed mode and consumes the speed as float.
  virtual u8 Init(const ScVec3F &position, i32 variant, i8 mode, f32 speed) = 0;
  // Original16=35F76,20=35F83,24=35FA5 consume full unsigned words; exact Linux names establish units.
  virtual void SetMicrosecondsBetweenFrames(u32 duration) = 0;
  virtual void SetObjectFastestSpeedTimes100000000(u32 speed) = 0;
  virtual void SetOuterGravityDistance(u32 distance) = 0;
  // Original28=3620A returns 2304; Linux names this diameter getter, whose nominal signedness remains unresolved.
  virtual i32 GetWhirlpoolDiameterAnimUnits() = 0;
  // Original32=35FC1 writes fixed X/Z outputs; Linux D8CA4 names GetLocation(int&,int&). Used-effects void
  virtual void GetLocation(i32 *x, i32 *z) = 0;
  // Original36=369CF takes two raw bytes; Linux DoFade(bool,bool). Its used-effects void return remains
  // provisional because the original nominal return is unresolved and the caller discards lock-release residue.
  virtual void DoFade(u8 visible, u8 notify) = 0;
};

// Primary6223BE6C; exact Linux QID9B94 identifies cSC3UFOSimulator and its cISC3UFOSimulator view.
// Group AddUFOToGroup and vector symbols prove this same interface pointer is stored and returned by GetUFO.
class SpUfoSimulator : public ScUnknown {
public:
  // Original12=3161A; Linux C8B60 identifies the point, Boolean, integer and signed-byte input order.
  virtual u8 Init(const ScVec3F &position, u8 automaticAttack, i32 mode, i8 level) = 0;
  // Original16=318C6; callers consume only the teardown effects and its nominal return is unresolved.
  virtual void Shutdown() = 0;
  // Original20=34071 and24=33ABF match the Linux ShootPlasmaBall and ClearAllInstructions names.
  virtual void ShootPlasmaBall(i32 x, i32 z, i32 altitude) = 0;
  virtual void ClearAllInstructions() = 0;
  // Original28=314B8, RET12; Linux D8974 GetPosition(unsigned int&,unsigned int&,unsigned int&).
  virtual void GetPosition(u32 *x, u32 *z, u32 *altitude) = 0;
  // Original32=314AB stores and returns the raw input byte; Linux D895C names SetAutoAttack(bool).
  virtual u8 SetAutoAttack(u8 enabled) = 0;
  // Original36=33AD6 requests retirement after adjusting the pending instruction queue.
  // Its private nominal return remains unidentified; callers use the effects.
  virtual void DestroySelfAsSoonAsPossible() = 0;
};

// PrimaryA2CC6284 of registered classB2CC6284; exact Linux QI identifies cSC3UFOGroupSimulator.
// Its primary identity also publishes IID58D, the native message-target prefix; the historical base spelling is unknown.
// Original leading interface table order is retained; private nominal return qualifications remain explicit.
class SpUfoGroupSimulator : public ScMessageTarget {
public:
  bool DoMessage(ScMessage *message) override = 0;
  // Original16=345FA, RET12/raw AL0or1; Linux D9F8C Init(bool,unsigned int,int).
  virtual u8 Init(u8 enabled, u32 count, i32 level) = 0;
  virtual u8 Shutdown() = 0;
  virtual u8 SetCount(u32 count) = 0;
  // Original28=3A76E returns the full vector count; Linux D89EC names GetCount. Native u32 is a lossless
  // count carrier; the original signed/unsigned typedef spelling remains unresolved.
  virtual u32 GetCount() = 0;
  virtual u8 AddUFOToGroup(SpUfoSimulator *ufo, u8 add, u8 shutdown) = 0;
  // Original36=353CE returns the borrowed indexed interface or null. Linux DA238 plus typed vector insertion
  // CDEC0 establish the cISC3UFOSimulator pointee independently of matching method names.
  virtual SpUfoSimulator *GetUFO(u32 index) = 0;
  virtual bool DoesUFOBelongToGroup(const SpUfoSimulator *ufo) const = 0;
  // Original44=349DC is a Boolean global-pool predicate; exact Linux DA1AC supplies this name.
  virtual bool DoesAnyUFOExistAnywhere() const = 0;
  virtual bool DoesUFOExistAnywhere(const SpUfoSimulator *ufo) const = 0;
  // Original52=34A04 caps caller capacity and copies global same-interface pointers. Linux CDFBC names
  // GetAllUFOsAnywhere(cISC3UFOSimulator**,unsigned int&); u32 transports the actual returned output count.
  virtual u32 GetAllUFOsAnywhere(SpUfoSimulator **objects, u32 *count) const = 0;
  virtual u32 GetListOfCurrentlyVisibleUFOs(SpUfoSimulator **objects, u32 *count) const = 0;
  // Original60=3B193 defines full DWORD0/1; Linux DA1FC names IsAnyUFOCurrentlyVisible() const.
  // Native i32 preserves the PC BOOL-width result rather than inferring a byte return from the Linux build.
  virtual i32 IsAnyUFOCurrentlyVisible() const = 0;
  // Original64=34653 queues actual asynchronous retirement and returns reference-count-minus-one.
  // Linux DA070 names ReleaseAsynchronous; original signed/unsigned nominal result remains qualified.
  virtual u32 ReleaseAsynchronous() = 0;
  // Original68=34BAC returns the destroyed global count or zero when deferred; Linux CE1FC proves Boolean input.
  virtual u32 DestroyAllUFOs(u8 deferred) = 0;
  // Original72=34C80 and76=34D61, RET16/raw AL0or1. Exact Linux CE620/CE758 use three signed words and bool.
  virtual u8 AddInstructionVaporizeBuilding(i32 x, i32 z, i32 index, u8 deferred) = 0;
  virtual u8 AddInstructionMakeCropCircle(i32 x, i32 z, i32 index, u8 deferred) = 0;
  // Original80=34E42, RET12/raw AL0or1; exact Linux CE890 takes bool,int,bool.
  virtual u8 AddInstructionSelfDestruct(u8 explode, i32 index, u8 deferred) = 0;
  // Original84=34F29, RET12/raw AL0or1; exact Linux CE9D0 proves an occupant pointer, not a spawn flag.
  virtual u8 AddInstructionSuckUpPerson(ScOccupant *occupant, i32 index, u8 deferred) = 0;
  virtual u8 AddInstructionSpawnUFO(u8 enabled, i32 index, u8 deferred) = 0;
  // Original92=350A9, RET12/raw AL0or1; exact Linux CEBE0 takes const cRZRect&,int,bool.
  virtual u8 AddInstructionMigrateToArea(const ScRect &bounds, i32 index, u8 deferred) = 0;
  virtual u8 AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) = 0;
  virtual u8 AddInstructionDropBomb(i32 x, i32 z, i32 index, u8 deferred) = 0;
  virtual u32 GetInstructionLoad() = 0; // Full DWORD carrier; nominal signedness remains unresolved.
};

// Primary2123BE6C of class3123BE6C; exact Linux QIC2778 identifies cSC3WaterSplashSimulator.
// The five signed words and signed frame byte follow Linux Init(iiiiiSc); PC250F8 preserves their raw bits.
class SpWaterSplash : public ScUnknown {
public:
  // Original12=250F8, RET24/raw AL0or1. Nominal PC bool/BYTE result spelling remains unresolved.
  virtual u8 Init(i32 x, i32 z, i32 altitude, i32 first, i32 last, i8 frames) = 0;
};

// Original cISC3TrailingSmoke is the secondary identity at smoke+200, separate from the sprite primary.
class SpSmokeView : public ScUnknown {
public:
  // Installed27453 stores and returns the complete interval word; its nominal source result remains qualified.
  virtual u32 SetTimeBetweenFrames(u32 duration) = 0;
};

// Actual cISC3WaterDebris identity04A4427E. Its two timer setters return their complete stored DWORD values.
// This interface is separate from trailing smoke, despite the folded interval setter and matching prefix layout.
class SpWaterDebrisView : public ScUnknown {
public:
  virtual u32 SetTimeBetweenFrames(u32 duration) = 0;
  virtual u32 SetTimeToLive(u32 duration) = 0;
};

// Primary44BEC830 of class44BEC831; exact Linux QIDA94C identifies cSC3WhirlpoolDebris.
// This reached native declaration preserves real methods without adopting the distinct animation7101 interface.
class SpWhirlpoolDebris : public ScUnknown {
public:
  // Original12=358BE, RET16/raw AL0or1; Linux Init(iiii) proves four separate signed words.
  virtual u8 Init(i32 x, i32 z, i32 altitude, i32 variant) = 0;
  // Original16=35AF0, RET4/raw AL; Linux ChangePosition(cSC3CityCoord&) uses x/z/altitude at0/4/8.
  virtual u8 ChangePosition(CtCoord *position) = 0;
  // Original20=35D85 and24=35D92 store raw DWORDs216/220. These used-effects void returns are provisional;
  virtual void SetTimeBetweenFrames(u32 duration) = 0;
  virtual void SetTimeToLive(u32 duration) = 0;
  // Original28=35707 constructs an unretained same-primary object. The native interface pointer preserves that
  // identity; the original concrete/interface pointee spelling remains unresolved.
  virtual SpWhirlpoolDebris *MakeNewWhirlpoolDebris() = 0;
};

// PrimaryC535119B; exact Linux DB404 and typed AddLocustsToGroup D6D24 prove the cISC3LocustsSimulator view.
// PC final table66DC8 and Linux __vt_20cSC3LocustsSimulator establish the complete primary ordering.
class SpLocustSimulator : public ScUnknown {
public:
  // Original12=38A47, RET12/raw AL; Linux Init(const cRZ3DPointFloat32&,int,signed char).
  virtual u8 Init(const ScVec3F &position, i32 level, i8 state) = 0;
  // Original16=38CBB and20=39C2D; nominal returns are unresolved and only their effects are exposed here.
  virtual void Shutdown() = 0;
  virtual void ClearAllInstructions() = 0;
  // Original24=38917 is proved through final primary66DC8 and Linux D8E2C GetPosition(RUiN21).
  virtual void GetPosition(u32 *x, u32 *altitude, u32 *z) = 0;
  // Original28=39C44 and32=39152; these discarded nominal returns remain provisional.
  virtual void DestroySelfAsSoonAsPossible() = 0;
  virtual void SetGroup(SpLocustGroupSimulator *group) = 0;
  // Original36=38D79, RET4/raw AL; Linux names the enum argument but its individual values remain unresolved.
  virtual u8 SetState(SpLocustState state) = 0;
  // Original40=3A5DC sign-extends the state byte; nominal enum spelling remains unresolved.
  virtual i32 GetState() const = 0;
};

// PrimaryC535123A of class65351250, PC final table66E44; Linux __vt_25cSC3LocustsGroupSimulator confirms ordering.
class SpLocustGroupSimulator : public ScMessageTarget {
public:
  // Original16=3A82D RET8/raw AL; Linux Init(unsigned int,int).
  virtual u8 Init(u32 count, i32 level) = 0;
  // Original20=34623 dispatches SetCount(0); its nominal return remains unresolved.
  virtual void Shutdown() = 0;
  // The original nominal bool/BYTE return remains qualified; the disaster discards this result.
  virtual u8 SetCount(u32 count) = 0;
  // Original28=3A76E returns full vector size; original nominal count typedef remains qualified.
  virtual u32 GetCount() = 0;
  // Original32=3AB3E, RET12/raw AL; Linux takes a typed child and two Boolean arguments.
  virtual u8 AddLocustsToGroup(SpLocustSimulator *locust, u8 add, u8 destroy) = 0;
  // Original36=353CE returns a borrowed typed vector entry or null; Linux DB99C names GetLocusts(unsigned int).
  virtual SpLocustSimulator *GetLocusts(u32 index) = 0;
  // Original40=349B9; Linux names this Boolean const membership predicate.
  virtual bool DoesLocustsBelongToGroup(const SpLocustSimulator *locust) const = 0;
  // Original44=3AEC0 tests the global pool; Linux DB8AC names this Boolean const predicate.
  virtual bool DoesAnyLocustsExistAnywhere() const = 0;
  // Original48=3AE81; Linux names this Boolean const global-membership predicate.
  virtual bool DoesLocustsExistAnywhere(const SpLocustSimulator *locust) const = 0;
  // Original52=3AEE8 and Linux D75E4 preserve typed child-pointer output and mutable unsigned count.
  // Native u32 retains the defined returned count, with original nominal return typedef unresolved.
  virtual u32 GetAllLocustsAnywhere(SpLocustSimulator **objects, u32 *count) const = 0;
  // Original56=3AF69 and Linux D76B0 write typed locust pointers and the mutable unsigned count.
  virtual u32 GetListOfCurrentlyVisibleLocusts(SpLocustSimulator **objects, u32 *count) const = 0;
  // Original60=3B086 RET8/raw AL1; exact Linux D7828 GetEatingSwarmsCount(RUiT1) writes both global counts.
  virtual u8 GetEatingSwarmsCount(u32 *eating, u32 *visible) const = 0;
  // Original64=3B193 defines full DWORD0/1; Linux DB8FC gives the const method name, not PC return width.
  virtual i32 IsAnyLocustsCurrentlyVisible() const = 0;
  // Original68=3A877 queues retirement and returns reference-count-minus-one; Linux DB770 supplies its name.
  virtual u32 ReleaseAsynchronous() = 0;
  // Original72=3B1B4 returns global destruction count or zero when deferred; Linux D7988 takes bool.
  virtual u32 DestroyAllLocusts(u8 deferred) = 0;
  // Preserve X/altitude/Z triples without changing them into float points or a disaster-owned nominal type.
  virtual u8 DistributeTargetListAmongSwarms(ScVec3U *targets, u32 count) = 0;
  // Original80=3B6DB, RET12; Linux D843C DropDDTTrail(int,int,int). Used-effects void is provisional
  // because the original caller ignores the final display Release residue, not a proven source result.
  virtual void DropDDTTrail(i32 x, i32 z, i32 altitude) = 0;
  // Original84=3B288, RET20/raw AL; Linux takes four signed words and a Boolean deferred flag.
  virtual u8 AddInstructionAttackCrops(i32 x, i32 z, i32 altitude, i32 index, u8 deferred) = 0;
  // Original88=3B37C RET8/raw AL0or1; Linux D7EF8 uses signed index and Boolean deferred flag.
  virtual u8 AddInstructionDisappear(i32 index, u8 deferred) = 0;
  // Original92=3B437, RET12/raw AL; Linux takes a const rectangle, signed index and Boolean deferred flag.
  virtual u8 AddInstructionMigrateToArea(const ScRect &area, i32 index, u8 deferred) = 0;
  // Original96=3B57F, RET16/raw AL0or1; exact Linux DB93C takes int,int,typed child pointer,bool.
  virtual u8 AddInstructionPoisonSwarm(i32 x, i32 z, SpLocustSimulator *swarm, u8 deferred) = 0;
  // Original100=3B5BE, RET12/raw AL; Linux takes float speed, signed index and Boolean deferred flag.
  virtual u8 AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) = 0;
  // Original104=3B676 returns the full load word; Linux names the method, while nominal signedness remains qualified.
  virtual i32 GetInstructionLoad() = 0;
};

// Secondary82A34B4F of class92A34B4F; exact Linux AC830 names cSC3ToxicCloudDisplay and selects complete+200.
// PC registration4C85C/factory4CE3E constructs1F374 with final secondary637E0; concrete production remains with SIMSPR.
class SpToxicCloudDisplay : public ScUnknown {
public:
  // Original12=1F5C9 RET16/raw AL0or1; Linux AA084 Init(unsigned int,int,int,int).
  // The source spells owner as unsigned int but passes the disaster instance address for retirement matching.
  // Preserve that pointer identity at native width; the three animation coordinates remain signed32.
  virtual u8 Init(uptr owner_id, i32 x, i32 z, i32 altitude) = 0;
  // Original16=1F580 RET8 returns the raw notify byte; Linux AC7C8 takes bool,bool.
  virtual u8 DoFade(u8 visible, u8 notify) = 0;
  // Original20=1F4DB RET4 copies three floats; Linux AC608 takes const cRZ3DPointFloat32&.
  virtual void SetVelocity(const ScVec3F &velocity) = 0;
  // Original24=1FA57 raw AL0or1 and28=1FAEE raw AL1; Linux AA6EC/AC884 supply these no-argument names.
  virtual u8 StartRaining() = 0;
  virtual u8 StopRaining() = 0;
};

// Reached cISC3NeighborTileDisplay primary, IID A2E7C81A. The concrete producer also owns a message-target facet.
class SpNeighborDisplay : public ScUnknown {
public:
  virtual u8 Init(ScOccupantManager *manager, i32 layer, u8 post) = 0;
  virtual void Shutdown() = 0;
  // The original private nominal result remains unresolved; known callers consume only these effects.
  virtual void ShowOrHide(u8 enabled) = 0;
};

// Reached cISC3PowerOuttageDisplay prefix, constructed by the sprite manager. Discarded nominal returns stay qualified.
class SpPowerDisplay : public ScUnknown {
public:
  virtual u8 Init(u32 width, u32 height) = 0;
  // Installed slot16 is an empty RET4. No normal result is defined or consumed by its callers.
  virtual void ShowOrHide(u8 enabled) = 0;
  virtual void Shutdown() = 0;
  virtual u8 UpdatePowerMap(const ScCellMap<bool> *map) = 0;
};

class SpPowerIcon;
class CtZoneLayer;

// Original bit-column storage used only by the power display. The final partial word retains its source fill bits.
class SpPowerBits {
public:
  SpPowerBits(u32 width, u32 height, const bool *fill);
  virtual ~SpPowerBits();
  void SetAllCells(const bool *fill);
  void SetValue(u32 x, u32 z, const bool *value);
  u32 width, height, words;
  u32 **columns;
};

// Actual sparse-matrix payload pairs a borrowed occupant with its pooled indicator at signed cell coordinates.
struct SpPowerCell {
  i32 x, z;
  ScOccupant *occupant;
  SpPowerIcon *sprite;
};

// Original cSC3PowerOuttageDisplay owns primary, message and city-change identities and one retained icon pool.
// Native arrays replace its generic vectors, hash membership and sparse matrices; game methods remain direct calls.
class SpPower final : public SpPowerDisplay, public ScMessageTarget, public CtChangeReceiver {
public:
  SpPower();
  ~SpPower();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(u32 width, u32 height) override;
  void ShowOrHide(u8 enabled) override;
  void Shutdown() override;
  u8 UpdatePowerMap(const ScCellMap<bool> *map) override;
  bool DoMessage(ScMessage *message) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 left, u32 top, u32 right, u32 bottom) override;
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  i32 IsCellZoned(u32 x, u32 z);
  bool IsOccupantEligibleForPowerSprite(ScOccupant *occupant);
  void SetBlinkTimeForZoom(i32 zoom);
  u8 OccupantInserted(ScOccupant *occupant, u8 post);
  u8 OccupantRemoved(ScOccupant *occupant, u8 post);
  u8 OccupantCellRangeChanged(u32 left, u32 top, u32 right, u32 bottom);
  void DoViewChange(
      i32 old_zoom, i32 zoom, i32 old_rotation, i32 rotation, const ScRect &old_bounds, const ScRect &bounds);
  // The private pool/reconciliation methods' discarded nominal declarations remain unresolved.
  void ClearDisplayCompletely(u8 reset);
  void SetupDisplayCompletely(u8 reset);
  void DoSpriteChangeForTranslation(const ScRect &old_bounds, const ScRect &bounds);
  i32 GrowSpritePool(i32 count);
  SpPowerIcon *AddUnpoweredOccupantSprite(i32 x, i32 z, ScOccupant *occupant, u8 move, u8 clip);
  u32 references;
  u8 initialized, visible, registered, power_updated, update_pending;
  CtCity *city;
  CtZoneLayer *zones;
  u32 change_sender, pool_limit;
  ScArray<SpPowerIcon *> pool, available;
  SpPowerBits *power_bits;
  i32 center_x, center_z;
  ScRect view_bounds;
  i32 zoom, rotation;
  ScArray<ScPointXZ> removed, added;
  ScArray<ScOccupant *> occupants;
  ScArray<SpPowerCell> building_cells, zone_cells;
  CtRandomIterator cell_order;
  CtLinearIterator pool_order;
  i32 margins[5];
};

// Reached cISC3RadiationDisplay prefix. These are the actual producer's operations, with no synthetic service callbacks.
class SpRadiationDisplay : public ScUnknown {
public:
  virtual u8 Init(u8 post) = 0;
  virtual void Shutdown() = 0;
  virtual void ShowOrHide(u8 enabled) = 0;
};

class SpBasicSprite;
class EcLayer;

// Original sparse rows expose x/z/sprite records in signed z-then-x order. Storage uses the approved trivial array.
struct SpCellIcon {
  i32 x, z;
  SpBasicSprite *sprite;
};

// Recovered cSC3RadiationDisplay owns primary and message identities and retains each icon through its cell collection.
// Only generic ordered-row storage is replaced; icon ownership, subscription and animation calls remain explicit.
class SpRadiation final : public SpRadiationDisplay, public ScMessageTarget {
public:
  SpRadiation();
  ~SpRadiation();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(u8 post) override;
  void Shutdown() override;
  void ShowOrHide(u8 enabled) override;
  bool DoMessage(ScMessage *message) override;
  u8 UpdateRadiationDisplay(const EcLayer *pollution, u32 x, u32 z);
  void UpdateCell(u32 x, u32 z, u8 radiation);
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void GetIconPositionFromCell(u32 x, u32 z, u32 *icon_x, u32 *icon_z);
  void AdjustCellIconIfItHasRadiation(u32 x, u32 z);
  u32 references;
  u8 visible, registered;
  u32 word16;
  ScArray<SpCellIcon> icons;
};

// Recovered cSC3CityViewEffectsOccupant: a byte reference count and city flags, geometry flags and float position.
// This is the original shared effects base; derived effects must still supply their real sprite instance.
class SpEffectOccupant : public ScOccupant {
public:
  SpEffectOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *) override;
  bool DoQueryInfo(ScMessage *, ScUnknown *) override;
  bool Init(GeAttrib *) override;
  bool Init(RsKey *) override;
  bool Init(CtCity *, OcPersistInfo *) override;
  void Shutdown() override;
  bool Save(CtCity *, OcPersistInfo *) override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32) override;
  u8 MajorType() const override;
  u8 SubTypeInfo() const override;
  u32 GetSubTypeData(u32) override;
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  bool CanSimRemove() const override;
  void SetSimRemove(bool) override;
  bool CanUserRemove() const override;
  void SetUserRemove(bool) override;
  bool GetName(ScStringView *) override;
  GeAttrib *GetAttribData() override;
  bool GetSubSystemData(u32, void **) override;
  u32 TypeId() const override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **) override;
  u32 GetSoundObjectId(i32) override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpSprite *GetSpriteInst(u32 layer) override;
  SpSprite *GetSpriteInst() override = 0;
  u32 GetDefaultLayerFlag() const override;
  u8 SetDefaultLayerFlag(u32) override;
  void GetOrientation(u16 *, u16 *, u16 *) const override;
  bool GetRotationFlag(u32 *) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u8 GetAlt() const override;
  void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u16 GetAltAnimCells() const override;
  void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsY() const override;
  void SetLocation(const CtCoord &) override;
  void SetLocationInCellCoords(u32, u32) override;
  void SetAlt(u8) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool) override;
  i32 Tick(u32) override;
  void SetSubTypeInfo(u8) override;
  void SetOrientation(u16, u16, u16) override;
  u8 SetRotationFlag(u32) override;
  u32 GetMemUse() const override;
  bool DebugInstanceTag(ScStringView *) override;
  bool DebugClassTag(ScStringView *) override;
  bool DebugTypeTag(ScStringView *) override;
  virtual ~SpEffectOccupant();
  virtual void GetBoundingRectInAnimCellCoords(GeCellRect *out) const;
  virtual void SetAltAnimCells(u16 altitude);
  virtual void SetLocationInAnimCoords(u32 x, u32 z);
  // Original tail304/308: two arguments and AL1; Linux proves city/serial-record parameters, not bool/BYTE spelling.
  virtual u8 SerialRead(CtCity *city, RsStream *record);
  virtual u8 SerialWrite(CtCity *city, RsStream *record);

  u8 in_city, references;
  u32 geom_flags;
  ScVec3F position;
};

class SpCellMap;
class SpView;
class SpManagerView;
class SpTickManager;
class SpImageCache;
class ScDirtBag;
class ScMessageServer;
class OcAnimManager;
class ApBoxX;
class CtNeighborsLayer;
struct ApPerformanceInfo;

// The original module keeps both notification receivers alive for the lifetime of the DLL.
class SpViewChange : public ScMessageTarget {
public:
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
};

class SpLandChange : public ScMessageTarget {
public:
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
};

class ScRandom;

// Original cSC3EffectsManager owns a reference count; the drawing dependencies are actual module-wide static fields.
class SpEffects final : public ScUnknown {
public:
  SpEffects();
  ~SpEffects();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Callers discard both results. These used-effects void declarations retain unresolved nominal-return spelling.
  void InitEffects(SpCellMap *map);
  void ShutdownEffects();
  static bool initialized;
  static ApPerformanceInfo *performance;
  static SpCellMap *cell_map;
  static SpView *view;
  static SpManagerView *manager;
  static CtCity *city;
  static ScDirtBag *dirt;
  static SpTickManager *tick_manager;
  static SpImageCache *images;
  static ScMessageServer *messages;
  static ScOccupantManager *surface;
  static OcAnimManager *animated;
  static ApBoxX *box;
  static CtNeighborsLayer *neighbors;
  static u32 transparent_color;
  static i32 zoom, rotation, view_id, micro_width, micro_height;
  static SpViewChange view_change;
  static SpLandChange land_change;
  static ScRandom random;
  // Original shared interlocked flag10072CC0 protects multiple effects families, including the locust pool.
  static std::atomic<i32> thread_safe_flag;
  // Scaling's original discarded nominal return remains unresolved; retain only the reached vector mutation.
  static void Scale2DVector(ScVec3F *vector, f32 length);
  // Original floating-register result is retained; literal float/double source spelling remains unresolved.
  static f64 AngleOf2DVector(const ScVec3F &vector);
  static f64 LengthOf2DVector(const ScVec3F &vector);
  static f64 LengthOf3DVector(const ScVec3F &vector);
  static void Scale3DVector(ScVec3F *vector, f32 length);
  static i32 Position3DIsIn2DBounds(const ScVec3F &point);
  static i32 GetAltitudeOfTerrainInAnimUnits(f32 x, f32 z);
  u32 references;
};

// Actual cISC3WeatherViewSimulator prefix; byte results retain the installed AL contract where nominal spelling is unknown.
class SpWeatherView : public ScUnknown {
public:
  virtual u8 Init() = 0;
  // Shutdown's discarded original result remains unresolved; production uses only its reached effects.
  virtual void Shutdown() = 0;
  virtual u8 ShowOrHide(u8 enabled) = 0;
  virtual i32 GetWeatherVisibility() = 0;
  virtual u8 SetCloudProperties(const ScVec3F &direction, u8 move, u8 lightning, i8 level) = 0;
  virtual u8 SetCurrentPrecipitation(i32 type) = 0;
};

class SpCloud;
class SpPrecipitation;
class SpLightning;

// Original cISC3TrajectoryObject primary interface4123BE6C. Private nominal bool/BYTE spellings remain qualified.
class SpTrajectoryView : public ScUnknown {
public:
  enum class Motion : i32 { Linear = 0, Decelerating = 1, Seeking = 2 };
  virtual u8 Init(i32 x, i32 z, i32 altitude, const ScVec3F &velocity, i32 kind) = 0;
  virtual void SetTrajectoryType(Motion mode, void *data) = 0;
  virtual u8 EnableTrailingSmoke(u8 enabled) = 0;
  virtual i32 GetType() = 0;
  virtual u32 GetSourceSignature() = 0;
  virtual u32 SetSourceSignature(u32 signature) = 0;
  virtual u32 GetCountOfTrajectoryObjects(u32 signature) = 0;
};

// Recovered cSC3WeatherViewSimulator keeps separate primary, animation and message identities.
// The actual cloud/precipitation, lightning and bird paths call their concrete producers and city services directly.
class SpWeatherSim : public SpWeatherView, public SpTickable, public ScMessageTarget {
public:
  SpWeatherSim();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init() override;
  void Shutdown() override;
  u8 ShowOrHide(u8 enabled) override;
  i32 GetWeatherVisibility() override;
  u8 SetCloudProperties(const ScVec3F &direction, u8 move, u8 enable_lightning, i8 level) override;
  u8 SetCurrentPrecipitation(i32 type) override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  bool DoMessage(ScMessage *message) override;
  virtual ~SpWeatherSim();
  // Private raw AL/DWORD carriers preserve reached results; their original nominal return declarations remain unresolved.
  u8 SetVisibilityFlags(i32 clouds, i32 precipitation, i32 unused);
  u8 ClearVisibilityFlags(i32 clouds, i32 precipitation, i32 unused);
  void ShowClouds(u8 enabled);
  u8 ShowPrecipitation(u8 enabled);
  // These original cleanup bodies discard their nominal register results; missing particle bodies stay explicit.
  void RemoveFromAnimationSystem();
  void AddToAnimationSystem();
  void SetupInitialWeather();
  void DestroyAllClouds();
  u32 DestroyAllPrecipitation();
  u32 CalculateAppropriateCloudCount();
  u32 SetNewPrecipitationToggleTime(u32 time);
  u32 SetNewLightningStrikeTime(u32 time);
  u32 SetNewCreateBirdFlockTime(u32 time);
  u8 GetPrecipitationPositionForCloud(u32 index, ScVec3F *out);
  u8 AttachPrecipitationToCloud(i32 type, u32 index, u32 time);
  void PickRandomSeedPositionForCloud(ScVec3F *point, u8 edge);
  void GenerateInitialClouds(u8 fill_all, u8 insert);
  void WrapPositionToOtherSideOfMap(ScVec3F *point);
  void GetNewLightningStrikePosition(const ScVec3F &origin, ScVec3F *out);
  // The private bird producer's ignored nominal result remains unresolved; the actual trajectory lifetime is preserved.
  void CreateNewBirdFlock(i32 edge);
  u8 SetLevel(i8 value);
  static i32 VectorToAppropriateEdge(const ScVec3F &direction);
  u32 references;
  u8 registered;
  u32 clock;
  u8 cloud_flags, move_clouds, flag26;
  i8 level;
  u8 lightning_enabled;
  u16 cloud_limit, compass;
  i32 altitude;
  ScVec3F wind;
  f32 cloud_height, cloud_speed;
  ScArray<SpCloud *> clouds;
  u8 precipitation_flags;
  i32 precipitation_type;
  SpPrecipitation *precipitation;
  u8 flag84;
  i32 precipitation_cloud;
  u32 precipitation_since, next_precipitation;
  u8 strike_enabled;
  SpLightning *lightning;
  u32 next_lightning, last_lightning;
  u8 birds_enabled;
  u32 next_bird, word124;
};

#endif
