#ifndef SC3_ECONOMY_EC_POLLUTION_H
#define SC3_ECONOMY_EC_POLLUTION_H

#include <list>
#include <map>

#include "buildings/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_sector.h"
#include "economy/ec_zones.h"
#include "sprites/api.h"

// Packed pollution and garbage fields retain their independent masks, shifts and value limits.
enum EcPackedPollution : u32 {
  kEcSaltWaterFlag = 0x40000000,
  kEcRadioactiveFlag = 0x80000000,
  kEcPollutionWithoutAirMask = 0xc0007fff,
  kEcPollutionWithoutWaterMask = 0xffff8000,
  kEcPollutionSampleMask = 0x7fff,
  kEcPollutionSampleMaximum = 0x7fff,
  kEcGarbageAmountMask = 0x7fff,
  kEcGarbageAmountMaximum = 0x7fff,
  kEcWithoutActiveLandfillMask = 0x7fff,
  kEcLandfillCellCapacity = 0x7fff,
  kEcActiveLandfillFlag = 0x8000,
  kEcAirPollutionShift = 15,
  kEcActiveLandfillShift = 15,
  kEcRadioactiveShift = 31,
};

class ScOccupant;

// Reached private incinerator IID0345777A/table1001B160 extends the common mortality view at the same address.
class EcCombustion : public BdMortal {
public:
  virtual u32 GetProduction() = 0;     // Original52: daily capacity DWORD.
  virtual u32 GetCapacity() = 0;       // Original56: unsigned remaining monthly capacity.
  virtual u32 Consume(u32 amount) = 0; // Original60: accumulate raw amount and return it.
};

// Recycling IID634ACD1D/table1001B5D0 has the same shared mortality identity and its own two trailing outputs.
class EcRecycling : public BdMortal {
public:
  virtual u32 GetEffectiveness() = 0; // Original52 reads the model percentage.
  virtual u32 GetCapacity() = 0;      // Original56 reads the model population capacity.
};

// SIMUTIL publishes the same adjusted receiver for water-treatment and mortality identities.
// This native grouping preserves its observed shared prefix without asserting a historical source hierarchy.
class BdTreatment : public BdMortal {
public:
  virtual u32 GetProduction() = 0;     // Original +52, daily treatment capacity.
  virtual u32 GetCapacity() = 0;       // Original +56, unsigned remaining monthly capacity.
  virtual u32 Consume(u32 amount) = 0; // Original +60, adds and returns raw amount.
};

// Original STL pointer lists retain their game-specific reference ownership in the pollution methods.
using EcTrackedList = std::list<ScUnknown *>;

// Original pending coordinate payload keeps both complete DWORDs without interface ownership.
struct EcPendingCell {
  u32 first, second;
};

using EcPendingList = std::list<EcPendingCell>;

// Original packed modifier values occupy exactly eight bytes.
struct EcModifier {
  i16 air, water;
  u8 air_radius, water_radius;
  i16 garbage;
  u32 IsEqual(const EcModifier *other) const;
};

static_assert(sizeof(EcModifier) == 8);

// Original ordered unique-key modifier tree: unsigned DWORD agent IDs map to eight-byte modifier values.
// B4D7 performs unsigned lower-bound/equality traversal; Save6ABB serializes its in-order sequence.
using EcSavedModifiers = std::map<u32, EcModifier>;

const EcModifier *EcFindModifier(const EcSavedModifiers &values, u32 identifier);

struct GeCellRect;
class CtAgentType;
class CtLayerManipulator;
class EcDisposal;
class EcHistoryModel;
class UiQuery;

// Original module tuning globals, loaded by the recovered StaticInit path.
extern u32 EcAirMaximum, EcWaterMaximum, EcGarbageMaximum;
extern u32 EcAirThreshold, EcWaterThreshold, EcGarbageThreshold;
extern u32 EcIncineratorCapacity, EcEnergyCapacity;
extern u32 EcGarbageAbductionLimit;
extern f32 EcGarbageScale;
extern u16 EcPollingPeriod;
extern u8 EcOrdinanceEffects[12];         // Original consecutive bytes1002023E..10020249, in address order.
extern i32 EcTrafficPollution;            // Original DWORD1002024C.
extern EcSavedModifiers EcAgentModifiers; // Original module tuning map, separate from each city's saved modifiers.

// Pollution owner with its actual primary DWORD map and distinct interface identities.
// The query-string facet shares IID81C0CB7B; native STL members are not a foreign binary layout.
class EcPollution : public EcDwordMap,
                    public EcLayer,
                    public CtLayer,
                    public CtAgent,
                    public CtChangeReceiver,
                    public ScQueryInfoString,
                    public SpMapSpriteFactory {
public:
  EcPollution();
  ~EcPollution() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *firstText,
      ScStringView *secondText) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  bool Simulate(i32 phase) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  bool OccupantInserted(u32 layer, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 layer, ScOccupant *occupant) override;
  bool OccupantMoved(u32 layer, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 layer, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 layer) override;
  bool CellChanged(u32 layer, u32 x, u32 z) override;
  bool CellChanged(u32 layer, u32 x0, u32 z0, u32 x1, u32 z1) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  u32 GetLayerType() override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *text) override;
  bool DebugClassTag(ScStringView *text) override;
  bool DebugTypeTag(ScStringView *text) override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  void GetValue(u32 x, u32 z, u32 *value) const override;
  void SetValue(u32 x, u32 z, const u32 *value) override;
  void SetAllCells(const u32 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u32 *value) override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u32 *fill) override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(CtCity *city, ScImporter *source) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  virtual bool GetManipulator(const CtAgentType &agent, CtLayerManipulator **manipulator, i32 *cost) override;
  virtual bool IsSaltWater(u32 x, u32 z) const override;
  virtual bool IsRadioactive(u32 x, u32 z) const override;
  virtual void GetAirValue(u32 x, u32 z, u32 *value) const override;
  virtual void GetWaterValue(u32 x, u32 z, u32 *value) const override;
  virtual void GetGarbageValue(u32 x, u32 z, u32 *value) const override;
  virtual bool IsAirPolluted(u32 x, u32 z) const override;
  virtual bool IsWaterPolluted(u32 x, u32 z) const override;
  virtual bool IsGarbageAccumulated(u32 x, u32 z) const override;
  virtual bool IsGarbageUncollected() override;
  virtual i32 GetTotalGarbageUncollected() override;
  virtual i32 GetAverageAirValue() const override;
  virtual i32 GetAverageWaterValue() const override;
  virtual i16 GetAverageGarbageValue() const override;
  virtual i32 GetMaxExpectedAirValue() const override;
  virtual i32 GetMaxExpectedWaterValue() const override;
  virtual i32 GetMaxExpectedGarbageValue() const override;
  virtual void SetIsSaltWater(u32 x, u32 z, bool enabled) override;
  virtual void SetIsRadioactive(u32 x, u32 z, bool enabled) override;
  virtual void SetAirValue(u32 x, u32 z, u32 value) override;
  virtual void SetWaterValue(u32 x, u32 z, u32 value) override;
  virtual void SetGarbageValue(u32 x, u32 z, u32 value) override;
  virtual bool IsActiveLandfill(u32 x, u32 z) override;
  virtual void SetIsActiveLandfill(u32 x, u32 z, bool enabled) override;
  virtual i32 GetTotalLandfillCapacity() override;
  virtual i32 GetAvailableLandfillCapacity() override;
  virtual i32 GetDailyIncineratorCapacity() override;
  virtual i32 GetDailyWasteToEnergyCapacity() override;
  virtual i32 GetRecyclingCenterEffect() override;
  virtual i64 GetTotalGarbageProduced() override;
  virtual i64 GetTotalGarbageImported() override;
  virtual i64 GetTotalGarbageExported() override;
  virtual i64 GetTotalGarbageRecycled() override;
  virtual i64 GetTotalGarbageIncinerated() override;
  virtual i64 GetTotalGarbageConvertedToEnergy() override;
  virtual i64 GetTotalGarbageLandfilled() override;
  i64 GetTotalGarbageProducedLastMonth() override;
  i64 GetTotalGarbageImportedLastMonth() override;
  i64 GetTotalGarbageExportedLastMonth() override;
  i64 GetTotalGarbageRecycledLastMonth() override;
  i64 GetTotalGarbageIncineratedLastMonth() override;
  i64 GetTotalGarbageConvertedToEnergyLastMonth() override;
  i64 GetTotalGarbageLandfilledLastMonth() override;
  virtual i32 GetGarbageBuildingCapacity(u32 kind) override;
  virtual i32 GetAgingWaterTreatmentCount() override;
  virtual i32 GetAgingIncineratorCount() override;
  virtual i32 GetAgingRecyclingCenterCount() override;
  virtual f32 GetGarbageScalingFactor() override;
  virtual void GarbagePutInLandfill(u32 quantity) override;

protected:
  // Private query helpers retain the actual byte result and separate original arguments.
  u8 BuildAdvisorText(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, u8 *warning);
  u8 QueryTile(ScMessage *message, UiQuery *query);
  u8 HandleQuery(ScMessage *message, ScUnknown *sender);
  i32 RouteGarbage(i32 quantity, i32 *incinerator, i32 *energy);
  // Native simulation phases; source-private7FD4/7A9E nominal declarations remain unasserted.
  void UpdateRecycling();
  virtual void FinalizeCycle();
  virtual bool UpdateTick();
  // Native effects of7B5D/7E6B; private source nominal returns remain unresolved.
  virtual void UpdateMonth();
  void UpdateReadout();
  // Native side-effect path shared by the two public callbacks; private095E7 nominal return remains unproved.
  bool ChangeOccupant(u32 operation, ScOccupant *occupant);
  // Native side effects of1000A032; no inferred private return contract is asserted.
  void TrackFacility(ScOccupant *occupant, u32 operation, u32 kind);
  // Water-treatment side effects use the verified foreign SIMUTIL building interface.
  void TrackWaterTreatment(ScOccupant *occupant, u32 operation);
  // Native accumulation phase of100095E7; the private source return remains unresolved.
  void ApplyModifier(const GeCellRect &bounds, const EcModifier &modifier);
  void ApplyModifierHalo(const GeCellRect &bounds, const EcModifier &modifier, i32 x0, i32 z0, i32 x1, i32 z1);

  // Offsets below describe the original pollution interface (whole owner +0x1C), not native STL member offsets.
  bool layer_initialized;                                  // +0x18, separate from primary-map initialized
  CtCity *owning_city;                                     // +0x1C
  ScUnknown *ordinances;                                   // +0x20, retained CtOrdinanceLayer interface
  CtZoneLayer *zones;                                      // +0x24
  ScUnknown *traffic, *neighbors;                          // +0x28/+0x30, retained traffic and neighbor interfaces
  CtResidentialLayer *residential;                         // +0x2C, actual population provider
  CtAgentTree *agent_types;                                // +0x34, retained type hierarchy
  ScMessageServer *messages;                               // +0x38
  EcIntMap first_input, second_input;                      // +0x3C, +0x58; signed accumulation maps
  ScLock occupant_lock;                                    // +0x74
  EcTrackedList tracked[4];                                // +0x98..+0xA4, water/incinerator/energy/recycling
  EcPendingList pending[4];                                // +0xA8..+0xB4, saved coordinate records
  i32 aging_treatment, aging_incinerator, aging_recycling; // +0xB8, +0xBC, +0xC0
  EcSavedModifiers saved_modifiers;                        // +0xC4, original saved-agent modifier tree
  i32 average_air, average_water;                          // +0xD0, +0xD4
  u32 accumulated_air, accumulated_water;                  // +0xD8, +0xDC
  u32 water_before_treatment, water_treated, treatment_denominator, treatment_numerator; // +0xE0..+0xEC
  u32 affected_cells;                  // +0xF0, number of cells accumulated in the current cycle
  EcHistoryModel *history_model;       // +0xF4, owned retained history model, created by Init
  ScString text;                       // +0xF8, original concrete eight-byte empty string
  EcShortMap garbage_produced;         // +0x10C, coarse signed-WORD production map
  EcWordMap garbage;                   // +0x128, embedded full-city unsigned-WORD map
  u16 average_garbage;                 // +0x138
  EcDisposal *disposal;                // +0x13C
  i32 daily_incinerator, daily_energy; // +0x140, +0x144
  f32 reduction_fraction;              // +0x148
  bool garbage_uncollected;            // +0x14C
  i32 total_uncollected;               // +0x150
  u64 totals[7];                       // +0x154..+0x184
  u64 history[7][12];                  // +0x18C..+0x424
  u64 current[7];                      // +0x42C..+0x45C
  u32 garbage_abducted;                // +0x464
  u32 landfill_cells;                  // +0x468
  u32 blocks_per_tick;                 // +0x46C
  u32 cell_x, cell_z;                  // +0x470, +0x474
  EcCursorRecord cursor;               // +0x47C, record prefix at whole+0x494, plain cursor state at whole+0x498
};

// The installed32-bit owner is0x4D0 bytes; modern STL members make this native class a compiler-dependent layout.

#endif
