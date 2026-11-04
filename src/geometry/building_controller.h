#ifndef SC3_GEOMETRY_BUILDING_CONTROLLER_H
#define SC3_GEOMETRY_BUILDING_CONTROLLER_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "occupants/api.h"

class CtZoneLayer;
class EcBudgetLayer;
class CtNeighborsLayer;
class ScDirtBag;
class GeDemolitionView;
class NwManager;

// Original 132-byte controller has building, city-layer, change-receiver and factory subobjects at0/4/8/12.
// City attachment, legacy tile import, centroid tracking, defaults and record persistence are restored.
// Building placement remains unfinished.
class GeBuildingController : public GeBuildingLayer, public CtLayer, public CtChangeReceiver, public OcFactory {
public:
  GeBuildingController();
  virtual ~GeBuildingController();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
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
  bool Shutdown() override;
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
  bool CreateOccupant(RsKey *key, u32 iid, void **out) override;
  bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) override;
  bool CreateBuilding(const RsKey &key, u32 iid, void **out) override;
  bool CreateBuilding(GeAttrib *attributes, u32 iid, void **out) override;
  bool CreateBuilding(u32 first, u32 second, u32 iid, void **out) override;
  GeAttrib *GetDefaultAttribData(BdBuilding *building) override;
  SpDefinition *GetDefaultSpriteData(BdBuilding *building) override;
  bool IsBuildable(const GeCellRect &rectangle, bool flat) const override;
  bool CanPlaceBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) override;
  bool CanPlaceShorelineBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) override;
  bool CanPlacePlumbingBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) override;
  bool CanPlaceSubwayBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) override;
  bool CanPlaceMultiBuilding(const CtCoord &position, const RsKey &first, const RsKey &second, const RsKey &third,
      const RsKey &fourth, GeBounds *affected, u32 *price) override;
  bool PlaceBuilding(
      const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) override;
  bool PlaceShorlineBuilding(
      const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) override;
  bool PlacePlumbingBuilding(
      const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) override;
  bool PlaceSubwayBuilding(
      const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) override;
  bool PlaceMultiBuilding(const CtCoord &position, const RsKey &first, const RsKey &second, const RsKey &third,
      const RsKey &fourth, GeBounds *affected, u32 *price, bool freeOfCharge) override;
  virtual bool InsertBuilding(BdBuilding *building) override;
  virtual bool RemoveBuilding(BdBuilding *building) override;
  virtual void GetCityCenter(u32 *x, u32 *z) override;
  virtual u32 GetTotalAreaOfBuildings() const override;
  i32 CheckOccupantTracking(u32 type) override;
  bool StartTrackingOccupantCount(u32 type) override;
  bool CreatePlacementTool(const RsKey &key, bool singleUse, bool freeOfCharge, u32 iid, void **out) override;
  bool DoPlaceBuildingTool(const RsKey &key, bool singleUse, bool freeOfCharge) override;
  bool DoPlaceBuildingTool(GeAttrib *attributes, bool singleUse, bool freeOfCharge) override;
  bool DoPlaceBuildingTool(u32 first, u32 second, bool singleUse, bool freeOfCharge) override;

protected:
  // Linux names bool free/apply and signed int mode; private nominal returns and PC input constness remain unresolved.
  u8 CheckPlaceBuilding(
      const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge, i32 mode, bool apply);
  u8 CheckPlaceBuilding(const CtCoord &position, const RsKey &first, const RsKey &second, const RsKey &third,
      const RsKey &fourth, GeBounds *affected, u32 *price, bool freeOfCharge, i32 mode, bool apply);
  // Original072F6 private byte-result carrier; its nominal bool/BYTE declaration remains unresolved.
  u8 CheckBuildingSite(
      const GeBounds *footprint, u32 type, GeBounds *affected, u32 *price, u8 *altitude, bool freeOfCharge, i32 mode);
  // Partial original commit effects; the building-site demolition operation remains explicitly unresolved.
  u8 PlaceBuilding(const GeBounds *footprint, ScArray<BdBuilding *> *buildings, GeBounds *affected, u32 quotedPrice,
      u8 altitude, bool freeOfCharge, i32 mode);
  // Original08599 returns a request identifier or the failed ancestor predicate carrier. Signedness is unresolved.
  u32 NotifyPlacedBuilding(BdBuilding *building);
  // Original private tracking updates share the module type-to-group and group-count tables.
  // Native read-side phase; original03EAE private nominal return remains unresolved.
  u8 LoadTrackingGroups();
  void ResetTrackingCounts();
  bool TrackInserted(ScOccupant *occupant);
  bool TrackRemoved(ScOccupant *occupant);
  u32 CountOccupantsOfType(ScOccupantManager *manager, u32 type);

  u8 initialized;
  u32 references;
  ScOccupantManager *surface, *undergroundOne, *undergroundTwo;
  CtZoneLayer *zones;
  ScDirtBag *terrain;
  NwManager *transit;
  EcBudgetLayer *budget;
  GeDemolitionView *demolition;
  CtNeighborsLayer *neighbors;
  CtCity *city;
  GeBounds bounds, extent;
  u32 centerX, centerZ, weightedX, weightedZ, totalArea;
};

class ScOccupant;

// Original synchronous message payload; the receiver writes the request identifier before returning.
struct GeDialogRequest {
  u32 identifier, group, instance;
  ScString *text;
  u32 reserved[4];
};

u32 SC3_STDCALL GeRequestDialog(u32 instance);
u32 GeEmitLandmarkNotify(u32 instance, ScOccupant *occupant);

#endif
