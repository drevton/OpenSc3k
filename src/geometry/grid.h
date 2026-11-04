#ifndef SC3_GEOMETRY_GRID_H
#define SC3_GEOMETRY_GRID_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/thread.h"
#include "geometry/ge_types.h"
#include "geometry/module.h"
#include "geometry/selection.h"
#include "geometry/tool.h"
#include "geometry/utilities.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "sprites/sp_tick_manager.h"

class GeFlagGrid;
class CtCity;

// Actual28-byte byte-map base inlined into the port layer. City coordinates index a grid scaled by two bits.
class GeByteMap : public ScByteMap {
public:
  GeByteMap();
  virtual ~GeByteMap();
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
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  void SetAllCells(const u8 *value) override;

protected:
  u32 references;
  u8 initialized;
  u32 width, height, coordinateShift;
  GeFlagGrid *grid;
};

class ApGameInterface;
class ScDirtBag;
class ScOccupantManager;
class EcBudgetLayer;
struct GeRandom;

// Concrete84-byte flora controller, primary/secondary tables10029ABC/10029AA8.
// The actual tree-tool factory is registered by StaticInit; no missing successful layer defaults are substituted.
class GeFloraController : public GeFlora, public OcFactory {
public:
  GeFloraController();
  // Original private112 creates an uninitialized flora occupant and queries the requested interface.
  virtual u8 CreateOccupant(u32 iid, void **out);
  virtual ~GeFloraController();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateOccupant(RsKey *key, u32 iid, void **out) override;
  bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 SetDensity(const CtCoord &point, u8 density) override;
  // Recovered original84/88/96/100/104 operations; private nominal byte/Boolean declarations remain unresolved.
  u8 CanGrow(const CtCoord &point, u32 *cost) override;
  u8 Grow(const CtCoord &point) override;
  u8 InsertTrees(const CtCoord &point, i32 count) override;
  u8 IncreaseDensity(const CtCoord &point) override;
  u8 DecreaseDensity(const CtCoord &point) override;
  u8 GetDensity(const CtCoord &point, u8 *density) override;

protected:
  // Original0C60E: Linux establishes the member receiver and i32 output; nominal byte/Boolean return remains unknown.
  u8 Convert2KIdToFloraDensity(u8 value, i32 *density);
  u32 ChooseKind(const CtCoord &point);
  bool Create(u32 kind, u32 density, u32 iid, void **out) override;
  // Actual QueryInterface output storage: void* permits publication during the external COM call without aliasing.
  void *application;
  CtCity *city;
  ScDirtBag *terrain;
  EcBudgetLayer *budget;
  u8 initialized;
  u32 references;
  ScOccupantManager *occupants;
  GeBounds bounds, extent;
  static GeRandom *random;
};

// Flora-save filter at original vtable10029A94. DestructorC12D restores the default filter's base table10029224.
class GeFloraTest : public GeIteratorTest {
public:
  ~GeFloraTest() override;
  bool Test(ScOccupant *occupant) override;
};

class GeFlora;

// Original264-byte tree tool combines the88-byte point handler and172-byte command, then retains flora at260.
// Native command sequences reduce its size; all calls use real C++ interfaces rather than original memory offsets.
class GeFloraTool : public GeDragTool, public GeCommand {
public:
  GeFloraTool();
  ~GeFloraTool() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  bool GetCursor(u32 *out) override;
  bool IsValidHandlerByView(i32 viewKind, u32 flags) const override;
  u8 Commit(const CtCoord *point) override;
  u8 PreviewDrag(const CtCoord *point) override;
  u8 PreviewPoint(const CtCoord *point) override;
  u8 PaintCursor(const CtCoord *point, GeBounds *affected) override;

protected:
  GeFlora *flora;
};

// Actual24-byte factory for tree-command CLSID4253EC3F; its module-retaining count is distinct from the base field.
class GeFloraFactory : public GeClassFactory {
public:
  GeFloraFactory();
  ~GeFloraFactory() override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateCommand(u32 classId, u32 iid, void **out) override;

private:
  u32 factoryReferences;
};

class RsStream;

// Each grid value holds either a real occupant pointer or a packed root coordinate, selected by the flag grid.
union GeCell {
  ScOccupant *occupant;
  u32 anchor;
};

// Original column-oriented occupant storage, inlined by manager Init.
class GeCellGrid {
public:
  GeCellGrid(u32 width, u32 height);
  virtual ~GeCellGrid();
  u32 width, height;
  GeCell **columns;
};

// Shared byte columns hold occupancy/redirect flags and packed saved-city flora values.
class GeFlagGrid {
public:
  GeFlagGrid(u32 width, u32 height, const u8 *fill);
  bool Read(RsStream *stream);
  bool Write(RsStream *stream);
  virtual ~GeFlagGrid();
  u32 width, height;
  u8 **columns;
};

class GeLayer;

// Concrete grid iterator identified by the original vtable.
class GeCellIterator : public OcIterator {
public:
  GeCellIterator(GeLayer *value);
  virtual ~GeCellIterator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(const GeBounds *bounds, OcIteratorTest *value) override;
  bool Shutdown() override;
  ScOccupant *Reset() override;
  ScOccupant *Current() override;
  ScOccupant *Next() override;

protected:
  ScOccupant *QueryCell(u16 cellX, u16 cellZ);
  u32 references;
  GeLayer *manager;
  OcIteratorTest *test;
  u16 left, top, right, bottom;
  ScOccupant *current;
  u16 x, z;
};

class GeAttrib;
struct RsKey;
struct ScMessage;
class SpSprite;
class SpDefinition;
class ScStringView;
class CtAgentType;
class UiQuery;
class RsStream;

// Recovered SIMGEOM occupant state and spatial operations, not a binary vtable overlay.
// Attribute, sprite, persistence and lifetime methods retain their real fields. Query hooks preserve reached bytes.
class GeOccupant : public ScOccupant {
public:
  GeOccupant();
  virtual ~GeOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  virtual bool Init(GeAttrib *value) override;
  virtual bool Init(RsKey *key) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  virtual bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  virtual u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  bool IsGeomFlag(u32 mask) override;
  virtual u8 SetGeomFlag(u32 mask) override;
  virtual u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetSubTypeData(u32 mask) override;
  u32 GetOccLayerFlag() override;
  virtual void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  u8 AnimType() const override;
  u32 TypeId() const override;
  GeAttrib *GetAttribData() override;
  virtual bool GetSubSystemData(u32 id, void **out) override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  virtual u32 GetDefaultLayerFlag() const override;
  virtual u8 SetDefaultLayerFlag(u32 layer) override;
  virtual bool GetName(ScStringView *out) override;
  virtual CtAgentType *CityAgentType() override;
  virtual u32 GetSoundObjectId(i32 index) override;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) override;
  virtual SpDefinition *GetSpriteAttrib() override;
  SpSprite *GetSpriteInst() override;
  virtual SpSprite *GetSpriteInst(u32 layer) override;
  virtual u32 GetMemUse() const override;
  u8 SubTypeInfo() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
  virtual bool CanSimRemove() const override;
  virtual void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  virtual void SetUserRemove(bool enabled) override;
  virtual void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  virtual u8 GetAlt() const override;
  virtual void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  // Windows cISC3Occupant method4C38 proves WORD; installed sprite callers zero-extend AX.
  u16 GetAltAnimCells() const override;
  virtual void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  virtual u32 SizeInAnimCellsZ() const override;
  virtual u32 SizeInAnimCellsX() const override;
  virtual u32 SizeInAnimCellsY() const override;
  void SetLocation(const CtCoord &position) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  virtual bool GetMarkBit() const override;
  virtual void SetMarkBit(bool marked) override;
  virtual void SetSubTypeInfo(u8 subtype) override;
  virtual void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  virtual u8 SetRotationFlag(u32 rotation) override;
  virtual bool DebugInstanceTag(ScStringView *out) override;
  virtual bool DebugClassTag(ScStringView *out) override;
  virtual bool DebugTypeTag(ScStringView *out) override;

protected:
  // Original288 consumes a WORD; its ignored nominal return remains unresolved.
  virtual void SetAltAnimCells(u16 altitude);
  // Private query hooks preserve reached bytes; their nominal bool/BYTE source declarations remain unresolved.
  virtual u8 DoQueryViews(ScMessage *message, UiQueryWindow *window);
  virtual u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  virtual u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  virtual u8 DoExitQueryWindow(ScMessage *message, UiQueryWindow *window);

  // Actual private virtuals at +308/+312 use the retained definition and borrowed sprite chain.
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  // Original316/320 consume the city and record separately; reached AL results do not prove bool/BYTE spelling.
  virtual u8 SerialRead(CtCity *city, RsStream *record);
  virtual u8 SerialWrite(CtCity *city, RsStream *record);

  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

class ScOccupantManager;

// The three clearing operations expose their verified effects; their unused private nominal returns remain unresolved.
class GePortMap : public ScByteMap {
public:
  virtual u8 HasSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) = 0;
  virtual u8 HasNearbyFlag2(u32 x, u32 z) = 0;
  virtual u8 HasFlag1(u32 x, u32 z) = 0;
  virtual u8 HasFlag2(u32 x, u32 z) = 0;
  virtual u8 HasAdjacency(u32 x, u32 z) = 0;
  virtual u8 SetSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) = 0;
  virtual u8 SetNearbyFlag2(u32 x, u32 z) = 0;
  virtual u8 IncrementAdjacency(u32 x, u32 z) = 0;
  virtual void ClearSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) = 0;
  virtual void ClearNearbyFlag2(u32 x, u32 z) = 0;
  virtual void DecrementAdjacency(u32 x, u32 z) = 0;
};

// Actual88-byte layer: byte map0, port view28, city layer32 and change receiver36, followed by city state and lock52.
// Native port-region/removal effects retain explicit private nominal-return gaps; no full declaration parity is claimed.
class GePortLayer : public GeByteMap, public GePortMap, public CtLayer, public CtChangeReceiver {
public:
  GePortLayer();
  ~GePortLayer() override;
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
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  u8 HasSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) override;
  u8 HasNearbyFlag2(u32 x, u32 z) override;
  u8 HasFlag1(u32 x, u32 z) override;
  u8 HasFlag2(u32 x, u32 z) override;
  u8 HasAdjacency(u32 x, u32 z) override;
  u8 SetSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) override;
  u8 SetNearbyFlag2(u32 x, u32 z) override;
  u8 IncrementAdjacency(u32 x, u32 z) override;
  void ClearSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) override;
  void ClearNearbyFlag2(u32 x, u32 z) override;
  void DecrementAdjacency(u32 x, u32 z) override;
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
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;

protected:
  u8 HandleOccupantChange(u32 operation, ScOccupant *occupant);
  // Native effects of private helpers1223F/1240F; their discarded nominal return declarations remain unresolved.
  void RemoveRunway(ScOccupant *occupant);
  void RemovePortNeighbors(ScOccupant *occupant);
  u8 cityReady;
  CtCity *city;
  ScOccupantManager *occupants;
  ScLock mapLock;
};

class OcIterator;
class OcIteratorTest;
class GeAnimationTest;
class CtChangeReceiver;

// Original module limit. Insertion treats one as unrestricted; reconciliation uses signed remaining space.
extern u32 GeAnimatedOccupantLimit;

// These are the original five retained pointer ranges, not a replacement general-purpose vector framework.
struct GeWorkingSet {
  ScOccupant **begin, **end, **storageEnd;
};

// Original occupant manager: primary manager, animation tickable and message target at x86 offsets 0/4/8.
// The private reconciliation nominal return remains an explicit native effect projection.
class GeLayer : public ScOccupantManager, public SpTickable, public ScMessageTarget {
public:
  GeLayer();
  virtual ~GeLayer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  bool Init(CtCity *city, u32 sender, u32 inserted, u32 removed, u32 changed, u32 layer) override;
  virtual bool Shutdown() override;
  virtual void ClearAnimatedOccupantList(bool notify) override;
  virtual void RemoveAllOccupants() override;
  bool OccupantChanged(ScOccupant *occupant, u32 change) override;
  virtual bool RegisterChangeReceiver(CtChangeReceiver *receiver, u32 *sender, bool replay) override;
  bool UnregisterChangeReceiver(CtChangeReceiver *receiver) override;
  virtual bool UnregisterAllChangeReceivers() override;
  virtual bool PostOccupantAll(CtChangeReceiver *receiver) override;
  bool PostOccupantAll(ScMessageTarget *target) override;
  void RemoveOccupant(ScOccupant *occupant) override;
  virtual void RemoveOccupant(const CtCoord &point) override;
  void RemoveOccupantAtCell(u32 x, u32 z) override;
  bool InsertOccupant(ScOccupant *occupant) override;
  virtual bool InsertOccupantAtCell(u32 x, u32 z, ScOccupant *occupant) override;
  virtual bool GetIterator(OcIterator **out) override;
  virtual bool GetOccupants(OcIterator **out, const GeBounds *bounds, OcIteratorTest *test) override;
  virtual u32 GetCellsInX() override;
  virtual u32 GetCellsInZ() override;
  virtual u32 GetCellsInY() override;
  virtual u32 GetSenderId() const override;
  virtual u32 GetOccLayer() const override;
  // Original private140/101B0 preserves its byte input/result; nominal bool/BYTE remains unresolved.
  virtual u8 SetVisible(u8 value);
  bool IsEmpty(const GeBounds &bounds) override;
  bool DoesOccupantExistAtLocation(u32 x, u32 z) override;
  bool IsInBounds(const CtCoord &point) override;
  bool IsInBounds(const GeBounds &bounds) override;
  virtual bool IsInBoundsCell(u32 x, u32 z) override;
  virtual bool GetNeighborsOfOccupant(
      ScOccupant *occupant, u32 capacity, ScOccupant **out, u32 *count, bool (*test)(ScOccupant *occupant)) override;
  bool GetOccupant(const CtCoord &point, ScOccupant **out) override;
  bool GetOccupantAtCell(u32 x, u32 z, ScOccupant **out) override;
  virtual bool GetNeighbors(OcIterator **out, ScOccupant *occupant, OcIteratorTest *test) override;
  virtual bool GetOccupantsInCell(
      const GeBounds &bounds, u32 capacity, ScOccupant **out, u32 *count, bool (*test)(ScOccupant *occupant)) override;

protected:
  friend class GeCellIterator;
  virtual bool IsVacantCell(const GeCellRect &bounds);
  // Native effect phase of F630; the private original nominal return remains unresolved at its lock-release tail.
  void ReconcileAnimations(const GeBounds *bounds, u8 force);
  u8 initialized, visible;
  u32 references, insertMessage, removeMessage, changeMessage;
  OcIterator *iterator;
  GeAnimationTest *animationTest;
  GeWorkingSet working[5];
  i32 lastZoom, lastRotation;
  GeRandom *random;
  GeBounds window;
  u8 field88;
  u32 field8C;
  ScLock *gridLock, *animationLock;
  GeCellGrid *cells;
  GeFlagGrid *cellFlags;
  u32 maximumX, maximumZ, maximumY, fieldAC, senderId, occupantLayer;
  CtChangeReceiver **receiversBegin, **receiversEnd, **receiversStorageEnd;
};

// Primary flora interface0253E6AC retains its installed order through the type setter at36.
class GePlantIdentity : public ScUnknown {
public:
  virtual bool Init(RsKey *key) = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual u32 GetSubTypeData(u32 mask) = 0;
  virtual u32 GetFloraDensity() = 0;
  virtual u32 GetFloraType() = 0;
  // Linux names a u32 input for slot32; the Windows nominal input and discarded return remain provisional.
  virtual void SetFloraDensity(u32 density) = 0;
  // Linux names a u32 input for slot36; the Windows nominal input and discarded return remain provisional.
  virtual void SetFloraType(u32 type) = 0;
};

// Original flora object has its primary COM identity at zero and the twenty-byte occupant at +4. The primary
// query-info dispatch remains incomplete.
class GePlant : public GePlantIdentity, public GeOccupant {
public:
  GePlant();
  ~GePlant() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  using GeOccupant::Init;
  bool Init(RsKey *key) override;
  bool GetName(ScStringView *out) override;
  u32 GetSubTypeData(u32 mask) override;
  u32 GetFloraDensity() override;
  u32 GetFloraType() override;
  void SetFloraDensity(u32 density) override;
  void SetFloraType(u32 type) override;
  u32 GetGeomFlag() override;
  u8 MajorType() const override;
  ScOccupant *AsISC3Occupant() override;
};

#endif
