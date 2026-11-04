#ifndef SC3_ECONOMY_EC_DEVELOPMENT_H
#define SC3_ECONOMY_EC_DEVELOPMENT_H

#include "city/api.h"
#include "common/sc3_array.h"
#include "economy/api.h"
#include "economy/ec_rci_random.h"

class GeBuildingLayer;
class GePortMap;
class CtZoneLayer;
class ScOccupantManager;
class ScDirtBag;
struct EcBlockGroup;
struct EcLotGroup;
struct EcPortRoadGroup;
struct EcPortBuildingGroup;
struct EcPortPieceGroup;

// The installed IID1 identity is separate from the rule identity; both share one owner and reference count.
class EcRuleUnknown : public ScUnknown {};

// Common rule receiver recovered from the shared constructor and IID1/rule identities.
class EcRuleBase : public EcCommandOwner, public EcZoneRule, public EcRuleUnknown {
public:
  EcRuleBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  u32 SetMaxDistance(u32 distance) override;
  virtual ~EcRuleBase();

protected:
  EcRci::ScRandom random;
  u32 max_distance;
  GeBuildingLayer *buildings;
  CtZoneLayer *zones;
};

// SIMRCI residential square-block development rule, class A22653C0.
class EcBlockRule : public EcRuleBase {
public:
  EcBlockRule();
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;
  ~EcBlockRule() override;

private:
  EcBlockGroup *groups;
};

// Residential block rule that also requires each candidate tile to be near transport, class 822C9D13.
class EcTransportBlockRule : public EcRuleBase {
public:
  EcTransportBlockRule();
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;
  ~EcTransportBlockRule() override;

private:
  EcBlockGroup *groups;
};

// Port-zone rule, class E2D3FD92. Its QueryInterface exposes only the development-rule identity.
class EcPortBlockRule : public EcRuleBase {
public:
  EcPortBlockRule();
  bool QueryInterface(u32 iid, void **out) override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;
  ~EcPortBlockRule() override;

private:
  EcBlockGroup *groups;
  GePortMap *port;
};

// Earlier residential lot rule, class 222C9D00. The placement candidate is not yet verified or published.
class EcLotRule : public EcRuleBase {
public:
  EcLotRule();
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;
  ~EcLotRule() override;

private:
  EcLotGroup *groups;
};

// Commercial block development rule, class 822C9D62.
class EcCommercialBlockRule : public EcRuleBase {
public:
  EcCommercialBlockRule();
  ~EcCommercialBlockRule() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcBlockGroup *groups;
};

// Industrial block development rule, class C22C9D34.
class EcIndustrialBlockRule : public EcRuleBase {
public:
  EcIndustrialBlockRule();
  ~EcIndustrialBlockRule() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcBlockGroup *groups;
};

// Industrial block development rule, class 422B55F2.
class EcIndustrialBlockRule2 : public EcRuleBase {
public:
  EcIndustrialBlockRule2();
  ~EcIndustrialBlockRule2() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcBlockGroup *groups;
};

// Industrial block development rule, class 822C9D24.
class EcIndustrialBlockRule3 : public EcRuleBase {
public:
  EcIndustrialBlockRule3();
  ~EcIndustrialBlockRule3() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcBlockGroup *groups;
};

// Commercial lot rule, class 422C9D52. Its placement candidate is not yet verified or registered.
class EcCommercialLotRule2 : public EcRuleBase {
public:
  EcCommercialLotRule2();
  ~EcCommercialLotRule2() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcLotGroup *groups;
};

// Commercial lot rule, class 622C9D42. Its placement candidate is not yet verified or registered.
class EcCommercialLotRule3 : public EcRuleBase {
public:
  EcCommercialLotRule3();
  ~EcCommercialLotRule3() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcLotGroup *groups;
};

// Original road-associated port rule, class C2D3FD7E.
class EcPortRoadRule : public EcRuleBase {
public:
  EcPortRoadRule();
  ~EcPortRoadRule() override;
  bool QueryInterface(u32 iid, void **out) override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcPortRoadGroup *groups;
  ScOccupantManager *occupants;
  ScDirtBag *dirt;
  GePortMap *port;
};

// Port-building rule, class 2207D9EE, including its edge and random-cell placement choices.
class EcPortBuildingRule : public EcRuleBase {
public:
  EcPortBuildingRule();
  ~EcPortBuildingRule() override;
  bool QueryInterface(u32 iid, void **out) override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  // These private byte results are observed directly; their nominal source spelling remains unresolved.
  u8 ScoreNeighbor(u32 x, u32 z, u8 direction);
  u8 HasPortNeighbor(u32 x, u32 z, u8 direction);
  u8 ClassifyNeighbor(u32 x, u32 z, u8 direction);
  u8 PlaceAdjacentPort(const GeCellRect &rectangle);
  u8 PlaceDoublePort(const GeCellRect &rectangle);
  u8 PlacePortRow(const GeCellRect &rectangle);
  EcPortBuildingGroup *groups;
  ScOccupantManager *occupants;
  GePortMap *port;
};

// Port-piece rule, class 81FFC3CE, selecting a neighboring edge before placing its pieces.
class EcPortPieceRule : public EcRuleBase {
public:
  EcPortPieceRule();
  ~EcPortPieceRule() override;
  bool QueryInterface(u32 iid, void **out) override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *group) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  // Original private byte return and direction nominal spellings are still unresolved.
  u8 PlaceEdgeRow(i32 direction, i32 score, const GeCellRect &rectangle);
  // The score effect is recovered; the original unused return declaration is unresolved.
  void ScoreRegion(i32 *score, const GeCellRect &rectangle, u32 identifier);
  EcPortPieceGroup *groups;
  ScOccupantManager *occupants;
  GePortMap *port;
};

class CtAgentTree;
class OcAttribCache;
class GeBuildingLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class ScOccupantManager;
class CtLandValueLayer;
class NwTrafficLayer;
class CtOrdinanceLayer;
class EcLayer;
class EcValve;
class BdBuilding;
struct EcCommercialGrowth;
struct EcCommercialIndex;

// Real SIMRCI commercial zone-developer identity and its connected service interfaces.
class EcCommercialDeveloper : public CtZoneDeveloper, public EcCommandOwner {
public:
  EcCommercialDeveloper();
  virtual ~EcCommercialDeveloper();
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

protected:
  // Private growth identifier and flag nominal spellings remain unresolved.
  u32 SelectGrowth(u8 value, u8 include_all);
  u32 SelectHigherValue(u32 current, u8 value, u8 include_all);
  u32 SelectLowerValue(u32 current, u8 value, u8 include_all);
  u32 SelectHigherDensity(u32 current, u8 value);
  u8 TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum);
  u8 TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance);
  u32 FindGrowthType(u32 identifier);
  u8 ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state);
  u8 initialized, minimum_growth, maximum_growth;
  EcCommercialIndex *growth_by_minimum, *growth_by_class;
  EcCommercialGrowth *growth_records;
  ScArray<EcZoneRule *> rules;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  GeBuildingLayer *buildings;
  CtPowerLayer *power;
  CtPlumbingLayer *water;
  ScOccupantManager *occupants;
  EcValve *commercial_valve;
  CtZoneLayer *zones;
  CtLandValueLayer *land_value;
  NwTrafficLayer *traffic;
  EcLayer *pollution;
  CtOrdinanceLayer *ordinances;
};

class CtAgentTree;
class OcAttribCache;
class GeBuildingLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class ScOccupantManager;
class CtLandValueLayer;
class NwTrafficLayer;
class CtOrdinanceLayer;
class CtResidentialLayer;
class BdBuilding;
class EcLayer;
class EcValve;
struct EcIndustrialIndex;
struct EcIndustrialGrowth;

// The installed industrial developer owns six ordered growth indexes and the actual retained city services.
// This receiver retains the original developer contract and direct city-layer dependencies.
class EcIndustrialDeveloper : public CtZoneDeveloper, public EcCommandOwner {
public:
  EcIndustrialDeveloper();
  virtual ~EcIndustrialDeveloper();
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

protected:
  // The installed year calculation's final byte is observed only through its fields; its nominal return is unresolved.
  void UpdateCleanIndustry(u32 year);
  // The result is the original identifier; the private byte argument names remain unresolved.
  u32 SelectGrowth(u8 value, u8 allow_agriculture, u8 include_all);
  u32 SelectHigherValue(u32 current, u8 value, u8 include_all);
  u32 SelectLowerValue(u32 current, u8 value, u8 include_all);
  u32 SelectHigherDensity(u32 current, u8 value);
  u8 TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum);
  u8 TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance_bonus);
  u32 FindGrowthType(u32 identifier);
  u8 ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state);
  u8 initialized, minimum_growth, maximum_growth;
  EcIndustrialIndex *growth_indexes[6];
  EcIndustrialGrowth *growth_records;
  ScArray<EcZoneRule *> rules;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  GeBuildingLayer *buildings;
  CtPowerLayer *power;
  CtPlumbingLayer *water;
  ScOccupantManager *occupants;
  EcValve *industrial_valve;
  CtZoneLayer *zones;
  CtLandValueLayer *land_value;
  NwTrafficLayer *traffic;
  EcLayer *pollution;
  CtResidentialLayer *residential;
  CtOrdinanceLayer *ordinances;
  u8 minimum_clean, maximum_clean, clean_initialized;
};

class GeBuildingType;
class CtZoneLayer;
struct GeCellRect;

namespace EcRci {
class ScRandom;
}

// The lot rules retain separate weighted small, large and shoreline building lists.
struct EcLotGroup {
  EcLotGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> small, large, shoreline;
};

// Shared implementation of the installed residential and commercial corner-lot placement paths.
u8 EcPlaceCornerLots(EcLotGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 minimum, u32 maximum_size,
    u32 threshold, u32 shore_chance, const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum);

// Apply the separate commercial corner and transport-proximity placement path.
u8 EcPlaceCommercialLots(EcLotGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 minimum, u32 maximum_size,
    u32 threshold, u32 shore_chance, const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum);

class GePortType;
class GeBuildingType;

// Original port-piece groups hold ten borrowed directional descriptors and a general building list.
struct EcPortPieceGroup {
  EcPortPieceGroup *next;
  GePortType *pieces[10];
  ScArray<GeBuildingType *> buildings;
};

// Check the original directional fallbacks and required general-building list.
u8 EcPortPieceGroupValid(EcPortPieceGroup *group);

struct EcDirectionalLotGroup;

// Original class 61F44687 stores directional two-cell lots and a one-cell filler list.
class EcDirectionalLotRule : public EcRuleBase {
public:
  EcDirectionalLotRule();
  ~EcDirectionalLotRule() override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  u32 SetMaxDistance(u32 distance) override;
  void AddConfig(GeBuildingGroupView *source) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  EcDirectionalLotGroup *groups;
};

struct EcIndustrialStripGroup;
class GeBuildingType;
struct ScPointXZ;

// Original SIMRCI class 630EA608 chooses a one-, two- or three-deep industrial lot strip.
class EcIndustrialStripRule : public EcRuleBase {
public:
  EcIndustrialStripRule();
  ~EcIndustrialStripRule() override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *source) override;
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  // The three placement candidates retain unresolved private Boolean/BYTE return spellings.
  u8 PlaceOne(const GeCellRect &rectangle, u32 group, GeBuildingType *building);
  u8 PlaceTwo(const GeCellRect &rectangle, u32 group, GeBuildingType *building);
  u8 PlaceThree(const GeCellRect &rectangle, u32 group, GeBuildingType *building);
  // Original 100133AC's result is discarded by these placement callers; its private nominal return remains unresolved.
  void PlaceSelected(GeBuildingType *building, const ScPointXZ &point, u8 reverse);
  EcIndustrialStripGroup *groups;
  ScOccupantManager *occupants;
};

struct EcLargeLotGroup;
struct ScPointXZ;
class GeBuildingType;

// Original SIMRCI class E22F4A83 configures large lots, directional pieces and one-cell fillers.
class EcLargeLotRule : public EcRuleBase {
public:
  EcLargeLotRule();
  ~EcLargeLotRule() override;
  bool Init(CtCity *city) override;
  u8 Shutdown() override;
  void AddConfig(GeBuildingGroupView *source) override;
  // Original 10011721 has a placement candidate; its private Boolean/BYTE return spelling remains unresolved.
  u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) override;

private:
  // Original helper 100133AC returns the underlying placement result, which these callers discard.
  void PlaceSelected(GeBuildingType *building, const ScPointXZ &point, u8 reverse);
  EcLargeLotGroup *groups;
  ScOccupantManager *occupants;
};

#endif
