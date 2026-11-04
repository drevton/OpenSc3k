#ifndef SC3_GEOMETRY_SELECTION_H
#define SC3_GEOMETRY_SELECTION_H

#include "city/ct_layer.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"
#include "sprites/city_window.h"

// Original default iterator filter at vtable accepts every occupant.
class GeIteratorTest : public OcIteratorTest {
public:
  GeIteratorTest();
  virtual ~GeIteratorTest();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

protected:
  u32 references;
};

class EcBudgetLayer;
class CtZoneLayer;
class CtNeighborsLayer;
class ScDirtBag;
class GeFlora;
class GeBuildingLayer;
class NwManager;

// Original100-byte demolition layer. Slot84 retains an explicit missing-code boundary at the operation itself.
class GeDemolition : public GeDemolitionView {
public:
  GeDemolition();
  virtual ~GeDemolition();
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
  u8 CanDemolishOccupants(ScOccupantManager *manager, const GeBounds *region, OcIteratorTest *test, u32 mode, u32 *cost,
      GeBounds *affected, SpSelection **selection) override;
  // Original80: byte truthiness is proven; nominal bool/BYTE spelling is not available in the installed stripped DLL.
  u8 CanDemolishFromDirtChange(const GeBounds *region, u32 mode, u32 *cost, GeBounds *affected, SpSelection **selection,
      u8 changed_only) override;
  u8 CanDemolishFromDirtChange(u32 layers, const GeBounds *region, u32 mode, u32 *cost) override;
  u8 CanDemolishForBuildingSite(u32 layers, const GeBounds *region, u32 mode, u32 *cost, GeBounds *affected,
      SpSelection **selection) override;
  u8 DemolishOccupants(ScOccupantManager *manager, const GeBounds *region, OcIteratorTest *test, u32 mode,
      bool freeOfCharge, u32 *cost, GeBounds *affected) override;
  u8 DemolishFromDirtChange(const GeBounds *bounds, u32 privilege, bool free_of_charge, u32 *cost,
      GeBounds *affected, bool changed_only) override;
  u8 DemolishFromDirtChange(u32 layers, const GeBounds *region, u32 mode, bool freeOfCharge, u32 *cost) override;
  // Original100 is an actual constant-false implementation with six untouched inputs/outputs.
  virtual u8 DemolishForBuildingSite(
      u32 layers, const GeBounds *region, u32 privilege, bool freeOfCharge, u32 *cost, GeBounds *affected);
  // Original104 changes trees or building occupancy; the raw AL carrier retains unresolved private bool/BYTE spelling.
  virtual u8 ChangeOccupancyState(
      ScOccupantManager *manager, ScOccupant *occupant, u32 privilege, i32 state, bool enforcePermission);
  // Original108: Linux proves these inputs and outputs; the nominal return remains unidentified and is discarded.
  virtual void DemolitionRemoveOccupant(
      ScOccupant *occupant, u32 privilege, u32 *cost, GeBounds *affected, u32 *changes);

protected:
  u8 CanRemove(u32 mode, ScOccupant *occupant);
  u8 HasNeighborConnection(ScOccupant *occupant);
  u8 RemoveTypeGroup(u32 mode, ScOccupantManager *manager, ScOccupant *occupant);
  u32 references;
  GeBounds bounds, extent;
  EcBudgetLayer *budget;
  CtZoneLayer *zones;
  NwManager *transit;
  ScOccupantManager *surface, *undergroundTwo, *undergroundOne;
  ScDirtBag *terrain;
  GeFlora *flora;
  GeBuildingLayer *buildings;
  CtNeighborsLayer *neighbors;
  u32 unitCost;
};

// The installed demolition filter has its independent count followed by four byte-sized layer selectors.
class GeDemolitionTest : public OcIteratorTest {
public:
  explicit GeDemolitionTest(u32 layers);
  virtual ~GeDemolitionTest();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

private:
  u32 references;
  u8 surface, first, combined, second;
};

struct CtCoord;
struct RsKey;
class GeAttrib;

// Same-primary flora interface406B1196 uses the canonical city-layer prefix and installed slots72..108.
// Private nominal bool/BYTE and input-const gaps remain explicit; no filler slots or default implementations are added.
class GeFlora : public CtLayer {
public:
  virtual bool Create(u32 kind, u32 density, u32 iid, void **out) = 0;
  virtual bool CreateOccupant(RsKey *key, u32 iid, void **out) = 0;
  virtual bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) = 0;
  virtual u8 CanGrow(const CtCoord &point, u32 *cost) = 0;
  virtual u8 Grow(const CtCoord &point) = 0;
  virtual u8 InsertTrees(const CtCoord &point, i32 count) = 0;
  virtual u8 IncreaseDensity(const CtCoord &point) = 0;
  virtual u8 DecreaseDensity(const CtCoord &point) = 0;
  virtual u8 GetDensity(const CtCoord &point, u8 *density) = 0;
  virtual u8 SetDensity(const CtCoord &point, u8 density) = 0;
};

// Actual SIMGEOM enumerator owner: three coordinate-storage pointers, cursor and reference count.
// The shared sprite selection interface is separate from this concrete producer's extra virtual methods.
class GeSelection : public SpSelection {
public:
  GeSelection();
  GeSelection(const GeSelection &source);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Next(u32 maximum, CtCoord *out, u32 *count) override;
  u8 Skip(u32 count) override;
  u8 Reset() override;
  u8 Clone(SpSelection **out) override;
  virtual bool GetBounds(GeBounds *bounds); // Original +28.
  // Defined short batches retain their extra history item; full batches omit the stock out-of-array read.
  virtual u8 AppendFrom(SpSelection *source); // Original +32; private nominal AL type remains unresolved.
  virtual u8 SortAndCompact();                // Original +36; original ordering with ignored unique end.
  virtual ~GeSelection();                     // Original +40.
  void Append(const CtCoord &position);
  void Clear();
  CtCoord *begin, *end, *storageEnd;
  u32 cursor, references;

private:
  void Reserve(u32 capacity);
  GeSelection &operator=(const GeSelection &) = delete;
};

class CtAgentTree;

// Original 16-byte type-tree filter at table2942C. Its count and retained tree belong to this independent receiver.
class GeTypeTest final : public OcIteratorTest {
public:
  GeTypeTest(u32 type);
  ~GeTypeTest();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

private:
  u32 references, type;
  CtAgentTree *tree;
};

class ScByteMap;
class CtPowerLayer;
class GeLayer;

// Concrete animation filter at original vtable10029F70. Its destructor restores the default filter's base vtable.
class GeAnimationTest : public GeIteratorTest {
public:
  GeAnimationTest();
  ~GeAnimationTest() override;
  bool Test(ScOccupant *occupant) override;

protected:
  friend class GeLayer;
  bool zeroZoneEnabled, otherZoneEnabled;
  CtPowerLayer *power;
  ScByteMap *zones;
};

// Original cSC3OccTestBuilding shares the eight-byte default filter lifetime and adds only its building predicate.
class GeBuildingTest : public GeIteratorTest {
public:
  GeBuildingTest();
  ~GeBuildingTest() override;
  bool Test(ScOccupant *occupant) override;
};

#endif
