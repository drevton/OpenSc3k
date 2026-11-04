#ifndef SC3_ECONOMY_EC_ZONES_H
#define SC3_ECONOMY_EC_ZONES_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/sc3_array.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"
#include "sprites/api.h"

class CtCity;
class RsStream;
class EcZoneLayer;

// SIMRCI's traversal state owns a packed-cell sequence and preserves its independent arithmetic traversal mode.
class EcZoneCursor {
  friend class EcZoneLayer;

public:
  EcZoneCursor();
  ~EcZoneCursor();
  // The original private nominal return and coordinate signedness remain unresolved.
  u8 Next(i32 *x, i32 *z);
  // Reset's state effects are known; the original private return declaration remains unresolved.
  void Reset(u8 shuffle);

protected:
  i8 phase;
  i32 mode;
  u8 refill;
  i32 min_x, min_z, max_x, max_z, count;
  ScArray<u16> positions;
  i32 used, current, stride;
};

// The original record receiver adds two virtual serialization operations before the cursor state.
class EcZoneCursorRecord : public EcZoneCursor {
public:
  virtual u8 SerialRead(CtCity *city, RsStream *record);
  virtual u8 SerialWrite(CtCity *city, RsStream *record);
};

// SIMRCI's two occupant filters share a local reference count without a module pin.
class EcZoneFilter : public OcIteratorTest {
public:
  EcZoneFilter();
  virtual ~EcZoneFilter();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;

private:
  u32 references;
};

// Transport proximity includes the road geometry flag and four original transit occupant types.
class EcZoneTransportFilter : public EcZoneFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

// Road counting uses only the road geometry flag.
class EcZoneRoadFilter : public EcZoneFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class WnWindowManager;
class SpCityWindowView;
class SpViewInterface;
class UiCityCursor;
class EcBudgetLayer;
class CtZoneLayer;

// SIMRCI's own city-view handler owns its host, view, cursor and budget while borrowing the window manager.
class EcZoneToolBase : public SpViewHandler {
public:
  EcZoneToolBase();
  virtual ~EcZoneToolBase();
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnKeyDown(u32 key, u32 flags) override;
  bool OnKeyUp(u32 key, u32 flags) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseDownR(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpR(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  bool GetCursor(u32 *out) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  bool AttachHost(SpCityWindowView *host);
  bool DetachIfRequested(SpCityWindowView *host);

protected:
  WnWindowManager *windows;
  SpCityWindowView *window;
  SpViewInterface *view;
  UiCityCursor *cursor_map;
  EcBudgetLayer *budget;
  GeBounds bounds;
  u8 mouse_scroll, edge_scroll, cursor_visible, cost_visible;
  i32 cost;
  u8 invalid, insufficient_funds, field58, detach_after_input;
};

// The installed area handler retains the drag endpoints and the additional six-word sentinel region.
// Mouse gestures use the installed view picking and area callbacks.
class EcZoneAreaTool : public EcZoneToolBase {
public:
  EcZoneAreaTool();
  ~EcZoneAreaTool() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  // The private callback return spellings remain unresolved; the installed entries return raw AL.
  virtual u8 BeginAt(const CtCoord *point);
  virtual u8 Commit(const GeBounds *area);
  virtual u8 PreviewDrag(const GeBounds *area);
  virtual u8 PreviewHover(const CtCoord *point);
  virtual u8 PaintCursor(const GeBounds *area, GeBounds *affected);

protected:
  u8 active;
  GeBounds drag, extra;
};

// The actual zoning tool has separate city-view handler and command identities sharing one command lifetime.
// The area handler and command identity share one command lifetime.
class EcZoneTool : public EcZoneAreaTool, public EcRciCommand {
public:
  EcZoneTool(u32 cursor, i32 zone, u16 color, u16 sound);
  ~EcZoneTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  bool Execute() override;
  u8 Commit(const GeBounds *area) override;
  u8 PreviewDrag(const GeBounds *area) override;
  u8 PreviewHover(const CtCoord *point) override;
  u8 PaintCursor(const GeBounds *area, GeBounds *affected) override;

private:
  CtZoneLayer *zones;
  u32 cursor_id;
  i32 zone_type;
  u16 cursor_color, success_sound;
};

// A failure count keyed by its original reason and zone byte.
struct EcZoneFailure {
  EcZoneFailure *next;
  i32 reason;
  u8 zone;
  u32 count;
};

// The filled-demand count associated with an unsigned identifier.
struct EcZoneDemand {
  EcZoneDemand *next;
  u32 identifier, value;
};
class ScOccupantManager;
class OcIteratorTest;
class UiQuery;
class ScString;
class EcValveLayer;
class ScDirtBag;
class GeBuildingLayer;
class GeDemolitionView;
class CtPowerLayer;
class CtLandValueLayer;
struct ScPointXZ;

// SIMRCI's embedded change source owns its sender, reference field and retained receiver vector.
class EcZoneChangeSource : public CtChangeSource {
public:
  explicit EcZoneChangeSource(u32 sender);
  ~EcZoneChangeSource();
  u32 GetID() const override;
  bool AddReceiver(CtChangeReceiver *receiver, u32 *sender) override;
  bool RemoveReceiver(CtChangeReceiver *receiver) override;
  bool RemoveAll() override;
  void SetEnabled(u8 enabled) override;
  u8 GetEnabled() const override;
  bool NotifyAll() override;
  bool Notify(u32 x, u32 z) override;
  bool Notify(u32 left, u32 top, u32 right, u32 bottom) override;

protected:
  void GrowReceivers();
  u32 sender_id, source_references;
  u8 notify_enabled;
  CtChangeReceiver **receivers_begin, **receivers_end, **receivers_limit;
};

// The SIMRCI zone layer has distinct byte-map, zoning, city-layer and change-receiver identities.
// Persistence, simulation, notifications and remaining interfaces stay abstract until recovered.
class EcZoneLayer : public EcByteMap,
                    public CtZoneLayer,
                    public CtLayer,
                    public EcZoneChangeSource,
                    public CtAgent,
                    public CtChangeReceiver,
                    public SpMapSpriteFactory {
public:
  EcZoneLayer();
  ~EcZoneLayer() override;
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
  u32 GetZoneCount(u8 zone) override;
  u32 GetUndevelopedTileCount() override;
  u32 GetUndevelopedTileCount(u8 zone) override;
  u32 GetDevelopmentFailureCount(u8 zone, i32 reason) override;
  u32 GetAbandonedTileCount() override;
  bool RegisterZoneDeveloper(u8 zone, CtZoneDeveloper *developer, u32 flags) override;
  bool UnregisterZoneDeveloper(u8 zone) override;
  CtZoneDeveloper *GetZoneDeveloper(u8 zone) override;
  bool PlaceBuilding(GeBuildingType *building, const ScPointXZ &point, i32 state, u16 rotation, bool check) override;
  bool PlaceBuilding(const RsKey &key, const ScPointXZ &point, i32 state, u16 rotation, bool check) override;
  bool IsNearTransport(const ScPointXZ &point, u32 distance) override;
  u32 GetRoadCount(const ScPointXZ &first, const ScPointXZ &last) override;
  void FindBuildableRect(
      GeCellRect *out, u32 x, u32 z, u32 distance, u8 minimum_value, u32 type, bool ignore_power) override;
  u32 GetFilledDemand(u32 identifier) override;
  void AddToFilledDemand(u32 identifier, i32 amount) override;
  bool GetZoneColor(u32 x, u32 z, u8 *color_type, u16 *color) override;
  bool CanZone(i32 zone, const GeBounds &bounds, i32 *cost) override;
  bool PlaceZone(i32 zone, const GeBounds &bounds, i32 *cost, bool free_of_charge) override;
  bool NerdsRule() override;
  void ReadZoneDeveloperDescriptions(bool bind_demand) override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool StaticShutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
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

protected:
  friend void EcParseZoneRules(const ScString &key, const ScString &value, void *context);
  u8 AddDeveloperRule(u8 zone, EcZoneRule *rule);
  bool RegisterCommandFactory();
  // Original private return nominal spelling remains unresolved; the result is consumed as a byte.
  u8 DispatchZoneQuery(i32 x, i32 z, ScStringView *label, ScStringView *value, ScOccupant *target, u32 code);
  u8 QueryTile(ScMessage *message, UiQuery *query);
  void SimulateZones();
  // The internal operation's nominal return is unresolved; callers use its Boolean result.
  u8 UpdateOccupantDemand(i32 operation, ScOccupant *occupant);
  bool TileMatches(
      const u8 *altitude, const ScPointXZ &point, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power);
  u8 GrowRectX(GeCellRect *rectangle, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power);
  u8 GrowRectZ(GeCellRect *rectangle, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power);

  struct DeveloperSlot {
    CtZoneDeveloper *developer;
    u32 flags;
  };

  bool layer_initialized;
  CtCity *city;
  u32 city_width, city_height;
  u32 zone_counts[23], undeveloped_counts[23], current_undeveloped_counts[23];
  u32 total_undeveloped, current_undeveloped, abandoned, current_abandoned;
  DeveloperSlot developers[23];
  EcZoneFailure *failures, *current_failures;
  EcZoneDemand *demand;
  EcByteMap failure_map;
  ScOccupantManager *occupants;
  EcValveLayer *valve_layer;
  ScDirtBag *terrain;
  GeBuildingLayer *building_layer;
  GeDemolitionView *demolition;
  CtPowerLayer *power;
  CtLandValueLayer *land_value;
  OcIteratorTest *transport_test, *road_test;
  u32 cells_per_step, next_x, next_z;
  EcZoneCursorRecord cursor;
  bool nerds_rule;
  static OcAttribCache *attributes;
};

#endif
