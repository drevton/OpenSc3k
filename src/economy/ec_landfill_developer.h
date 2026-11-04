#ifndef SC3_ECONOMY_EC_LANDFILL_DEVELOPER_H
#define SC3_ECONOMY_EC_LANDFILL_DEVELOPER_H

#include "city/api.h"
#include "common/sc3_array.h"
#include "economy/api.h"
#include "economy/ec_development.h"
#include "economy/ec_rci_random.h"

class CtAgentTree;
class CtCity;
class CtZoneLayer;
class EcLandfillGrid;
class EcLayer;
class EcValve;
class GeBuildingLayer;
class NwRuleCollection;
class NwTrafficLayer;
class OcAttribCache;
class ScOccupantManager;
struct EcLandfillCell;
struct NwRuleRecords;
struct NwNeighborhood;

// Original class 82348DE5 exposes a zoning developer and the separate landfill-decay view.
// Its road-access implementation requires the two original traffic operations not yet exposed by that shared interface.
class EcLandfillDeveloper : public CtZoneDeveloper, public CtLandfillDecay, public EcCommandOwner {
public:
  EcLandfillDeveloper();
  virtual ~EcLandfillDeveloper();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(CtCity *city, const char *growth_data) override;
  u8 Shutdown() override;
  bool BindDemand() override;
  i32 Evaluate(const ScPointXZ &point) override;
  u8 AddRule(EcZoneRule *rule) override;
  u8 SerialRead(CtCity *city, RsStream *stream) override;
  u8 SerialWrite(CtCity *city, RsStream *stream) override;
  f32 GetDecayRate() override;

private:
  // Original 1001A549 has an unused private nominal return; callers consume its city effects.
  void DecayCell(const ScPointXZ &point);
  // Original private DWORD and discarded-result spellings remain unresolved.
  i32 AvailableCapacity(const ScPointXZ &point);
  void SetActiveCapacity(const ScPointXZ &point, i16 capacity, u8 road_access);
  u8 PlaceCenter(const ScPointXZ &point);
  // The original discarded result is an incidental recursive callback value.
  void MarkZone(u32 x, u32 z);
  u8 FindGrowableNeighbor(const ScPointXZ &center, ScPointXZ *result);
  u8 ApplyTiling(const ScPointXZ &center, const NwRuleRecords &records);
  u8 BuildNeighborhood(NwNeighborhood **out, const ScPointXZ &center);
  // Original 1001AA11 needs traffic GetMaxDimension at slot80 and the eight-argument cell trip at slot72.
  u8 CheckRoadAccess(const ScPointXZ &point, i32 *reason);
  u8 initialized;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  EcValve *valve;
  GeBuildingLayer *buildings;
  ScOccupantManager *occupants;
  CtZoneLayer *zones;
  EcLayer *pollution;
  NwTrafficLayer *traffic;
  u32 maximum_x, maximum_z;
  EcLandfillCell *cells;
  EcLandfillGrid *grid;
  NwRuleCollection *rules[7];
  static bool tuning_loaded;
  static f32 decay_rate;
};

class EcLayer;
struct EcMixedLotGroup;

// Original SIMRCI class A2C1EDA3 combines several lot sizes and a retained pollution-layer view.
class EcMixedLotRule : public EcRuleBase {
public:
  EcMixedLotRule();
  ~EcMixedLotRule() override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *source) override;
  // Original private Boolean/BYTE return spelling remains unresolved.
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcMixedLotGroup *groups;
  EcLayer *pollution;
  static bool tuning_loaded;
  static u32 minimum_size, maximum_size, air_limit, water_limit;
};

class CtAgentTree;
class OcAttribCache;
class GeBuildingLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class CtLandValueLayer;
class NwTrafficLayer;
class ScOccupantManager;
class EcLayer;
class EcValve;
class GePortMap;
class BdBuilding;

// The installed 8001 developer uses borrowed city services and a retained port-map and industrial-connection valve.
class EcIndustrialPortDeveloper : public CtZoneDeveloper, public EcCommandOwner {
public:
  EcIndustrialPortDeveloper();
  virtual ~EcIndustrialPortDeveloper();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(CtCity *city, const char *growth_data) override;
  u8 Shutdown() override;
  bool BindDemand() override;
  i32 Evaluate(const ScPointXZ &point) override;
  u8 AddRule(EcZoneRule *rule) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) override;

private:
  u8 TryRules(const GeCellRect &rectangle);
  u8 ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state);
  u8 initialized;
  ScArray<EcZoneRule *> rules;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  EcValve *valve;
  GeBuildingLayer *buildings;
  CtPowerLayer *power;
  ScOccupantManager *occupants;
  CtZoneLayer *zones;
  GePortMap *port;
  CtPlumbingLayer *water;
  EcLayer *pollution;
  NwTrafficLayer *traffic;
};

// The separate installed 5001 developer also retains land value and evaluates commercial port growth.
class EcCommercialPortDeveloper : public CtZoneDeveloper, public EcCommandOwner {
public:
  EcCommercialPortDeveloper();
  virtual ~EcCommercialPortDeveloper();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(CtCity *city, const char *growth_data) override;
  u8 Shutdown() override;
  bool BindDemand() override;
  i32 Evaluate(const ScPointXZ &point) override;
  u8 AddRule(EcZoneRule *rule) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) override;

private:
  u8 TryRules(const GeCellRect &rectangle, i32 demand, i32 maximum);
  u8 ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state);
  u8 initialized;
  ScArray<EcZoneRule *> rules;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  EcValve *valve;
  ScOccupantManager *occupants;
  GeBuildingLayer *buildings;
  CtPowerLayer *power;
  CtZoneLayer *zones;
  CtLandValueLayer *land_value;
  GePortMap *port;
  CtPlumbingLayer *water;
  EcLayer *pollution;
  NwTrafficLayer *traffic;
};

#endif
