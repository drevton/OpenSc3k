#ifndef SC3_ECONOMY_EC_LAND_VALUE_H
#define SC3_ECONOMY_EC_LAND_VALUE_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/maps.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3.h"
#include "economy/api.h"
#include "economy/ec_sector.h"
#include "sprites/api.h"

// One original ordered population-threshold point and its raw byte modifier.
struct EcLandCurvePoint {
  EcLandCurvePoint *next;
  u64 threshold;
  u8 modifier;
};

// One original agent-type key and its four raw signed-byte influence accumulators.
struct EcLandAgentModifier {
  EcLandAgentModifier *next;
  u32 type;
  u8 residential, commercial, industrial, all;
};

class ScString;

// The original private result is a raw AL byte; its nominal Boolean/BYTE spelling is unresolved.
u8 EcInterpolateLandCurve(const EcLandCurvePoint *first, u32 population);
u8 EcLoadLandCurve(ScString *input, u8 kind);
void EcParseLandAgentModifier(const ScString &key, const ScString &value, void *context);

class CtCity;
class CtCrimeLayer;
class CtOrdinanceLayer;
class CtZoneLayer;
class EcLandValue;
class EcLayer;
class EcValve;
class EcValveLayer;
class GeBuildingLayer;
class ScOccupantManager;

// SIMRCI's actual land-value manipulator; service fields and computation are still being recovered.
class EcLandValueManipulator : public CtLayerManipulator, public EcCommandOwner {
public:
  EcLandValueManipulator();
  virtual ~EcLandValueManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Original private byte results and the connected retained service fields are not fully recovered.
  u8 Init(CtCity *city);
  u8 Shutdown();

private:
  friend class EcLandValue;
  CtCity *city;
  ScOccupantManager *occupants;
  EcLandValue *land;
  GeBuildingLayer *buildings;
  EcLayer *pollution;
  CtCrimeLayer *crime;
  EcValveLayer *valve_layer;
  CtZoneLayer *zones;
  CtOrdinanceLayer *ordinances;
  EcValve *residential_valve, *commercial_valve, *industrial_valve;
  u8 traversal_phase;
  u32 traversal_x, traversal_z;
  u32 cells_per_step;
  u16 update_period, thresholds[5];
  f32 low_factors[5], high_factors[5], map_factors[4];
  static u32 initial_x, initial_z;
};

// The five original radiation, water, air, garbage and crime tuning triples.
extern u16 EcLandUpdatePeriod;
extern u32 EcLandThresholds[5];
extern f32 EcLandLowFactors[5], EcLandHighFactors[5];
extern f32 EcLandMapFactors[4], EcLandHomelessEffect;
extern u8 EcLandWaterEffect, EcLandHillEffect;

class CtAgentTree;
class ScOccupantManager;
class CtZoneLayer;
class EcValve;
class EcValveLayer;
class EcLandValueManipulator;
class ScDirtBag;
class ScString;
class UiQuery;
struct EcLandCurvePoint;
struct EcLandAgentModifier;

namespace EcRci {
class ScGraphInfo;
}

// The four SIMRCI influence maps have the same stored short cells as the city sized-map specialization,
// but pin the SIMRCI director instead of the SIMSERV director.
class EcLandInfluenceMap : public CtSizedShortMap {
public:
  EcLandInfluenceMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
};

// SIMRCI land-value layer: the five genuine interfaces follow its shifted unsigned-byte map.
class EcLandValue : public CtSizedByteMap,
                    public CtLandValueLayer,
                    public CtLayer,
                    public CtChangeReceiver,
                    public SpMapSpriteFactory {
public:
  EcLandValue();
  ~EcLandValue() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  u8 GetAverageValue() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;

private:
  friend u8 EcLoadLandCurve(ScString *input, u8 kind);
  friend void EcParseLandAgentModifier(const ScString &key, const ScString &value, void *context);
  friend class EcLandValueManipulator;
  // The private query operations retain their recovered byte carriers; nominal Boolean/BYTE spellings remain unresolved.
  u8 OnWindowCreate(ScMessage *message, ScUnknown *source);
  u8 QueryTile(ScMessage *message, UiQuery *query);
  // Private nominal return and selector signedness remain unresolved; installed callers pass five separate inputs.
  u8 BuildQueryRow(i32 x, i32 z, ScString *label, ScString *value, i32 selector);
  void RebuildWaterCoverage();
  // Original private DWORD result spelling remains unresolved.
  i32 WaterDistance(u32 x, u32 z);
  void UpdateGraph();
  // Recompute the original influence grids and refresh their demand-curve modifiers.
  void RecomputeInfluenceLayers();
  void RefreshDemandModifiers();
  // Original 1001D616 refreshes aggregate state; its discarded private return remains unresolved.
  void FinishSweep();
  // Original private Boolean/BYTE return spelling remains unresolved.
  u8 StampOccupant(u32 operation, ScOccupant *occupant);
  bool layer_initialized;
  CtCity *city;
  ScOccupantManager *occupants;
  CtZoneLayer *zones;
  EcValveLayer *valve_layer;
  ScDirtBag *terrain;
  EcValve *valve;
  EcLandValueManipulator *manipulator;
  CtAgentTree *agent_tree;
  EcLandInfluenceMap influences[4];
  u8 average_value;
  i32 field196, field200;
  u32 field204;
  EcRci::ScGraphInfo *graph;
  u8 *water_coverage;
  u32 coverage_width;
  u8 average_altitude;
  u8 residential_modifier, commercial_modifier, industrial_modifier;
  static u8 water_influence_radius;
  static EcLandCurvePoint *residential_curve, *commercial_curve, *industrial_curve;
  static u8 curve_start[3], curve_limit[3];
  static EcLandAgentModifier *agent_modifiers;
};

// Land-value raster retains both the byte value map and its zoning mask.
class GxLandValueSprite : public GxRciMapSprite {
public:
  GxLandValueSprite(ScCellMap<u8> *values, ScCellMap<u8> *zones);
  ~GxLandValueSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  ScCellMap<u8> *values, *zones;
};

// Influence raster borrows the layer's signed-short map for its lifetime.
class GxLandInfluenceSprite : public GxRciMapSprite {
public:
  GxLandInfluenceSprite(u32 kind, ScCellMap<i16> *values);
  ~GxLandInfluenceSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  ScCellMap<i16> *values;
};

#endif
