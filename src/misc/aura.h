#ifndef SC3_MISC_AURA_H
#define SC3_MISC_AURA_H

#include <map>

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3_string.h"
#include "common/thread.h"
#include "sprites/api.h"

// Concrete i8 specialization of the original cRZCellMap; columns own actual scalar arrays, not foreign addresses.
class ScSignedGrid {
public:
  ScSignedGrid(u32 width, u32 height, const i8 *fill);
  virtual ~ScSignedGrid();
  // Full native transport phases; private nominal bool/BYTE declarations remain unresolved.
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  void SetAllCells(const i8 *fill);
  u32 width, height;
  i8 **columns;
};

// Original28-byte city map specialization owns a shifted allocation while exposing full city dimensions.
class CtSignedMap : public ScCellMap<i8> {
public:
  CtSignedMap(i32 shift);
  virtual ~CtSignedMap();
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
  bool Init(CtCity *city, const i8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, i8 *value) const override;
  void SetValue(u32 x, u32 z, const i8 *value) override;
  void SetAllCells(const i8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i8 *value) override;

protected:
  u32 references;
  bool initialized;
  u32 width, height;
  i32 coordinate_shift;
  ScSignedGrid *grid;
};

// Concrete i16 specialization of the original cRZCellMap; columns own actual scalar arrays, not foreign addresses.
class ScShortGrid {
public:
  ScShortGrid(u32 width, u32 height, const i16 *fill);
  virtual ~ScShortGrid();
  // Full native transport phases; private nominal bool/BYTE declarations remain unresolved.
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  void SetAllCells(const i16 *fill);
  u32 width, height;
  i16 **columns;
};

// Original28-byte city map specialization owns a shifted allocation while exposing full city dimensions.
class CtShortMap : public ScCellMap<i16> {
public:
  CtShortMap(i32 shift);
  virtual ~CtShortMap();
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
  bool Init(CtCity *city, const i16 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, i16 *value) const override;
  void SetValue(u32 x, u32 z, const i16 *value) override;
  void SetAllCells(const i16 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value) override;

protected:
  u32 references;
  bool initialized;
  u32 width, height;
  i32 coordinate_shift;
  ScShortGrid *grid;
};

class CtAgentTree;
class CtPoliceLayer;
class CtFireLayer;
class CtCrimeLayer;
class EcLayer;
class CtZoneLayer;
class CtOrdinanceLayer;
class CtResidentialLayer;
class ScOccupantManager;
class ScGraphInfoView;
class UiQueryWindow;
class UiQuery;

// Original sixteen lattice offsets used by the daily scan; coordinates advance by sixteen between offsets.
extern const u32 CtAuraScanOrder[16][2];

// Actual188-byte aura receiver: sized-map base, five separate interface views, held city services and a short map.
// Original receiver fields and recovered methods remain direct; private nominal-signature gaps stay explicitly qualified.
class CtAura : public CtSignedMap,
               public CtAuraLayer,
               public CtLayer,
               public CtAgent,
               public CtChangeReceiver,
               public SpMapSpriteFactory {
public:
  CtAura();
  ~CtAura() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *out) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const i8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, i8 *out) const override;
  void SetValue(u32 x, u32 z, const i8 *value) override;
  void SetAllCells(const i8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i8 *value) override;
  i8 GetMayorRating() override;
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
  bool Simulate(i32 phase) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
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
  static void RegisterAgentModifier(const ScString &key, const ScString &value, void *context);

protected:
  // Known native effects retain explicit gaps in the private nominal return/enum declarations.
  u8 DoDailySim();
  void EndOfMonth();
  void EndOfCycle();
  u8 OccupantChanged(u32 change, ScOccupant *occupant);

  // Complete native query effects preserve the AL carrier; private nominal bool/BYTE declarations remain unresolved.
  u8 OnWindowCreate(ScMessage *message, ScUnknown *source);
  virtual u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  virtual u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  virtual u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);

  // The original modifier has a signed effect word and radius byte; its fourth byte is uninitialized padding.
  struct CtModifier {
    i16 effect;
    u8 radius;
  };

  static std::map<u32, CtModifier> modifiers;
  static u16 update_period;
  static u8 optimal_police, high_eq_threshold, low_eq_threshold, high_le_threshold, low_le_threshold;
  static i8 optimal_police_effect, oppressive_police_effect, inadequate_police_effect, crime_factor, fire_factor;
  static i16 air_factor, water_factor;
  static i8 garbage_factor, radiation_factor, high_eq_factor, low_eq_factor, high_le_factor, low_le_factor;
  static i8 parking_effect, alternate_driving_effect, nuclear_free_effect;

  bool layer_initialized, new_city;
  CtCity *city;
  CtAgentTree *agent_tree;
  CtPoliceLayer *police;
  CtFireLayer *fire;
  CtCrimeLayer *crime;
  EcLayer *pollution;
  CtZoneLayer *zone;
  CtOrdinanceLayer *ordinances;
  CtResidentialLayer *residential;
  ScOccupantManager *occupants;
  CtShortMap modifiers_map;
  ScLock lock;
  i32 rating_count, rating_sum;
  i8 mayor_rating;
  ScGraphInfoView *rating_history;
  u32 cells_per_day;
  u8 scan_step;
  u32 scan_x, scan_z;
};

#endif
