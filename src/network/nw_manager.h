#ifndef SC3_NETWORK_NW_MANAGER_H
#define SC3_NETWORK_NW_MANAGER_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3_string.h"
#include "geometry/ge_types.h"
#include "network/nw_occupant.h"
#include "network/nw_tool.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/generic_dialogs.h"

// Actual contiguous resource-key storage referenced by the six module-global Set catalogs.
struct NwPieceCatalog {
  RsKey *begin, *end, *storageEnd;
};

// ROAD, HWAY, RAIL, SUBW, PIPE and POWR, at original globals 10032348..1003235C.
// The Set-file loading family must populate these before catalog predicates are called.
extern NwPieceCatalog *NwPieceCatalogs[6];

// Contiguous piece IDs used by protection, collapse and bridge resources.
struct NwPieceList {
  u32 *begin, *end, *storageEnd;
};

// Native decoding of the bridge resource's five counted groups; capacities are the original local copy limits.
// This is a resource aggregate, not an original game receiver or a replacement method argument bundle.
struct NwBridgePattern {
  u32 count[5];
  u32 start[5], leading[5], span[20], trailing[5], finish[5];
};

u32 NwBridgeReadWord(const u32 **cursor, const u32 *end);
void NwBridgeSkipWords(const u32 **cursor, const u32 *end, u32 count);
void NwReadBridgePattern(const u32 *cursor, const u32 *end, NwBridgePattern *pattern);
void NwValidateBridgeFillers(const NwBridgePattern *pattern, u32 leading, u32 trailing);
void NwValidateBridgePair(const NwBridgePattern *first, const NwBridgePattern *second, u32 start, u32 middle,
    u32 finish, u32 repeats, u32 leading, u32 trailing);

extern NwPieceList *NwProtectedTables[6];
extern NwPieceCatalog *NwDiagonalCatalog;

// The source stores each byte state beside its arithmetic-shifted packed piece ID in a sixteen-byte record.
struct NwConversion {
  u32 fromPiece;
  u8 fromState;
  u32 toPiece;
  u8 toState;
};
static_assert(sizeof(NwConversion) == 16);

struct NwConversions {
  NwConversion *begin, *end, *storageEnd;
};

// Simple and complex conversion globals share ROAD/HWAY/RAIL/SUBW/PIPE/POWR physical ordering.
extern NwConversions *NwSimpleConversions[6], *NwComplexConversions[6];
extern NwPieceList *NwCollapsePieces;
extern NwPieceList *NwBridgePieces[4]; // ROAD, HWAY, RAIL and POWR.

// Stack-owned construction prompt. Callback references never delete the two-string owner.
class NwConfirmDialog : public UiWindowCallback {
public:
  NwConfirmDialog(const ScString &body, const ScString &caption, u32 style);
  virtual ~NwConfirmDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  i32 Show();
  u8 Attach(WnWindow *window);

protected:
  ScString body, caption;
  u32 style, references;
};

class ScOccupantManager;
class OcIteratorTest;
class ScDirtBag;
class UiCityCursor;
class EcBudgetLayer;

// The primary four-slot interface is established by constructor10BE8 and its separate selection-base adjustments.
// DrawLine retains its original const endpoint references and genuine mutable selection/bounds outputs.
class NwStroke : public ScUnknown {
public:
  virtual u8 DrawLine(const CtCoord &start, const CtCoord &finish, SpSelection **out, GeBounds *changed) = 0;
};

// The actual drag receiver owns the secondary selection and an independent primary reference count.
// Its line builder uses real city, terrain, cursor, budget and network-manager dependencies.
class NwDrag : public NwStroke, public NwSelection {
public:
  NwDrag();
  ~NwDrag() override; // Original secondary selection +40; primary has no destructor entry.
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 DrawLine(const CtCoord &start, const CtCoord &finish, SpSelection **out, GeBounds *changed) override;
  // Original primary +16; only the low byte of the unsigned first input is stored.
  virtual u8 Setup(u32 wide, ScOccupantManager *target, OcIteratorTest *first, OcIteratorTest *second,
      OcIteratorTest *third, ScDirtBag *dirt, u32 policy, u32 water, u8 option, u8 ignore, i32 configuration,
      u32 price);
  void AddCell(const CtCoord &position, u8 rejected);
  void AddCell(i32 x, i32 z, u8 rejected);
  u32 GetTileId(u16 x, u16 z);
  bool EmptyOrTree(CtCoord position);
  u8 IsUnprotectedRoad(CtCoord position);
  bool GroundSignature(const CtCoord &position, u8 signature[4]);
  bool SurfaceSignature(const CtCoord &position, u8 signature[4]);
  bool GroundFlat(const CtCoord &position);
  bool SurfaceFlat(const CtCoord &position);
  bool GroundSlope(const CtCoord &position);
  bool SlopeX(const CtCoord &position);
  bool SlopeZ(const CtCoord &position);
  u8 Water(const CtCoord &position);
  bool CanEnter(i32 x, i32 z);
  u8 CanStep(i32 x, i32 z, i32 dx, i32 dz);
  u32 toolReferences; // Original28; distinct from NwSelection's reference word at24.
  GeBounds bounds;    // Original32..52.
  u8 doubleWidth;     // Original56.
  ScOccupantManager *layer;
  OcIteratorTest *primary, *secondary, *tertiary; // Original64/68/72.
  CtCity *city;
  UiCityCursor *cursorMap;
  ScDirtBag *terrain;
  EcBudgetLayer *budget; // Original88; first written by Setup.
  u32 config92, waterRun;
  u8 config100, ignoreSlope; // Original101 remains unwritten until Setup.
  u32 field104, field108, field112, field116;
  GeBounds changes;        // Original120..140, centered through live city getters by construction.
  i32 config144; // Original signed network selector, first written by Setup.
  u32 tileCost;
};

extern i32 NwDragMarkCount; // Original module-wide signed marker cap at100322A4.

// The diagonal stroke keeps the same providers and selection ownership while choosing its three-cell pattern.
class NwDiagonalDrag : public NwDrag {
public:
  ~NwDiagonalDrag() override;
  u8 DrawLine(const CtCoord &start, const CtCoord &finish, SpSelection **out, GeBounds *changed) override;
};

class EcBudgetLayer;
class CtZoneLayer;
class OcIteratorTest;
struct NwPieceList;
struct NwPieceCatalog;
struct NwConversions;
class RsFile;
struct NwRuleRecord;
struct NwRuleRecords;
struct NwNeighborhood;
class NwRuleCollection;

// Recovered network-manager methods use its actual city and terrain members.
// The city is also the receiver of the bounds getters; it is not a separate native map service.
// The canonical layer groups the proven same-pointer message prefix; later manager interfaces remain partial.
class NwManager : public CtLayer, public SpMapSpriteFactory, public OcFactory, public ScQueryInfoString {
public:
  NwManager();
  bool QueryInterface(u32 iid, void **out) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  // Actual fourth-interface12/16, with unchanged output slots on refusal.
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *firstText,
      ScStringView *secondText) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *target, ScImporter *source) override;
  bool Init(CtCity *target) override;
  bool Init(CtCity *target, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool Save(CtCity *target, RsDatabaseSegment *segment) override;
  u8 SaveLayer(
      RsDatabaseSegment *segment, ScOccupantManager *layer, OcIteratorTest *test, const RsKey &key, u32 *count);
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Actual creator AL carriers; private nominal bool/BYTE return spelling remains unresolved.
  virtual u8 CreateOccupant(OcPersistInfo *info, u32 iid, void **out);     // Original +232.
  virtual u8 CreateOccupant(void **out, u32 iid, u32 piece, u32 rotation); // Manager +72.
  bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) override; // Original primary +76/factory +12.
  bool CreateOccupant(RsKey *key, u32 iid, void **out) override;           // Original primary +80/factory +16.
  virtual bool QueryPredicate(void **out, u32 iid, i32 selector);   // Manager +84; later interface remains partial.
  virtual int HasTransportAt(const CtCoord &position) const;        // Original +88, DWORD BOOL.
  virtual bool HasRoadAt(const CtCoord &position) const;            // Original +92.
  virtual bool HasHighwayAt(const CtCoord &position) const;         // Original +96.
  virtual bool HasRailAt(const CtCoord &position) const;            // Original +100.
  virtual bool HasSubwayAt(const CtCoord &position) const;          // Original +104.
  virtual bool HasPipeAt(const CtCoord &position) const;            // Original +108.
  virtual bool HasPowerAt(const CtCoord &position) const;           // Original +112.
  virtual bool HasRoadFallbackAt(const CtCoord &position) const;    // Original +116; distinct repeated road query.
  virtual u8 RoadCost(SpSelection *selection, u32 *cost) const;     // Original primary120.
  virtual u8 HighwayCost(SpSelection *selection, u32 *cost) const;  // Original primary124.
  virtual u8 RailCost(SpSelection *selection, u32 *cost) const;     // Original primary128.
  virtual u8 SubwayCost(SpSelection *selection, u32 *cost) const;   // Original primary132.
  virtual u8 PipeCost(SpSelection *selection, u32 *cost) const;     // Original primary136.
  virtual u8 PowerCost(SpSelection *selection, u32 *cost) const;    // Original primary140.
  virtual u8 DiagonalCost(SpSelection *selection, u32 *cost) const; // Original primary144; diagonal command72 calls172.
  virtual u8 BuildRoad(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildHighway(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildRail(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildSubway(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildPipe(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildPower(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  virtual u8 BuildDiagonal(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus);
  // Original primary184 preserves the interactive removal result in AL; its nominal bool/BYTE spelling is unresolved.
  virtual u8 RemoveConnectionInteractive(ScOccupantManager *manager, ScOccupant *occupant);
  // Original primary264 uses a genuine integer output and a Linux-confirmed Boolean prompt input.
  virtual void ShouldWeRemoveConnection(ScOccupant *occupant, i32 *decision, bool prompt);
  virtual bool ReloadDefinition();      // Original primary188.
  virtual u32 GetRoadCount() const;     // Original primary196.
  virtual u32 GetHighwayCount() const;  // Original primary200.
  virtual u32 GetRailCount() const;     // Original primary204.
  virtual u32 GetSubwayCount() const;   // Original primary208.
  virtual u32 GetPipeCount() const;     // Original primary212.
  virtual u32 GetPowerCount() const;    // Original primary216.
  virtual f64 GetTransitDamage() const; // Original primary220.
  // Original primary228 SetmDebugRules remains omitted until its return declaration is established.
  virtual u8 BuildSelection(SpSelection *selection, i32 networkType, u32 *cost, GeBounds *affected, CtCoord *focus);
  // Actual primary240 has eight separate inputs; selection and focus are unused by this original body.
  virtual bool BuildBatch(ScOccupantManager *layer, SpSelection *selection, i32 networkType, u32 cost,
      GeBounds *affected, CtCoord *positions, u32 count, CtCoord *focus);
  // Original180/256/260; mutable bounds and position-list references use explicit pointers.
  // All observed callers discard the private nominal returns, which remain unresolved.
  virtual void CollapseSupport(
      ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation, GeBounds *affected, bool prompt);
  virtual void CollapseSupport(ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation, GeBounds *affected,
      bool apply, NwPositionList *removed, bool prompt);
  virtual void CollapseStructure(ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation, GeBounds *affected,
      i32 *decision, bool apply, NwPositionList *removed);
  // Original private244/248/252; outputs retain pointers, and only the container representation is native.
  virtual void MyRemoveOccupant(ScOccupantManager *layer, u32 x, u32 z, GeBounds *affected, i32 *decision,
      bool apply, NwPositionList *removed);
  virtual void MyRemoveOccupant(ScOccupantManager *layer, u32 x, u32 z, GeBounds *affected, i32 *decision);
  virtual void MyRemoveOccupantIfMatch(ScOccupantManager *layer, u32 x, u32 z, u32 piece, i32 rotation,
      i32 *decision, bool apply, NwPositionList *removed);
  // Private metadata-conversion effects; the unused stock return declaration remains unresolved.
  void ConvertPiece(i32 directionLimit, i32 networkType, u32 piece, u8 state, u32 *outPiece, u8 *outState);
  // Direct manager receiver and nine stack inputs are proven; private bool/BYTE return spelling is unresolved.
  u8 ClassifyNeighbor(NwRuleRecord *record, const CtCoord &origin, ScOccupant *occupant, i32 x, i32 z,
      i32 directionLimit, i32 networkType, OcIteratorTest *positive, OcIteratorTest *negative);
  // Whole collection effects; private bool/BYTE nominal return spelling remains unresolved.
  u8 CollectNeighborhood(NwNeighborhood **out, NwPositionList *pending, NwPositionList *work, NwRuleCollection *rules,
      ScOccupantManager *layer, OcIteratorTest *positive, OcIteratorTest *negative, const CtCoord &origin,
      i32 directionLimit, i32 networkType);
  // Whole commit effects; private bool/BYTE return and Boolean formal spelling remain unresolved.
  u8 CommitRules(ScOccupantManager *layer, const CtCoord &origin, NwRuleRecords *results, OcIteratorTest *replacement,
      OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32 networkType);
  // Whole stage effects; the original ignored-return nominal declaration remains unresolved.
  void ApplyRules(NwPositionList *work, NwPositionList *pending, NwPositionList *changed, NwPositionList *deferred,
      NwRuleCollection *rules, ScOccupantManager *layer, OcIteratorTest *positive, OcIteratorTest *negative,
      OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32 directionLimit, i32 networkType);
  virtual bool MatchesNetwork(u32 networkType, ScOccupant *occupant); // Original primary +332.
  // Original primary +336/+340 effects; private nominal byte/Boolean and flag spellings remain unresolved.
  virtual u8 ClearRegion(u32 networkType, SpSelection *selection, GeBounds *affected, u32 *cost,
      NwPositionList *removed, u8 charge, u8 apply);
  virtual u8 ClearRegionLayer(u32 networkType, SpSelection *selection, GeBounds *affected, u32 *cost,
      NwPositionList *removed, u8 charge, u8 apply);
  virtual bool CanReplaceAt(i32 x, i32 z, i32 networkType); // Original primary +276.
  // Original primary280; the unused cost pointer and by-value endpoints retain their separate ABI inputs.
  virtual u8 PrepareEndpoints(CtCoord first, CtCoord last, u32 *cost, i32 networkType);
  virtual bool HasNetworkConflict(u32 networkType, CtCoord position); // Original primary +344, position by value.
  // Original primary272 widths are corroborated by PC word loads and the Linux DoPrepare symbol.
  virtual bool BuildStraightRun(u16 x0, u16 z0, u16 x1, u16 z1, i16 dx, i16 dz, u16 networkType, NwPositionList *output,
      u8 apply); // Original primary +272.
  // Original ignored return remains unresolved; the genuine processed-count output is preserved.
  virtual void DecomposeRuns(NwPositionList *nodes, NwPositionList *output, i32 networkType, u8 apply,
      i32 *processed); // Original primary +192.
  // Region inputs retain the original aggregate and scalar types; unused return declarations remain unresolved.
  void CollectRegion(
      ScOccupantManager *layer, OcIteratorTest *positive, u32 type, const GeBounds &rectangle, NwPositionList *output);
  virtual void RefreshNetwork(const GeBounds &rectangle, u32 mask);
  // Original primary224 transit-damage phase; private unused return declaration remains unresolved.
  virtual void SetTransitDamage(f32 fraction);
  // Whole tiling effects use the original by-value position list; its unused return declaration remains unresolved.
  void ApplyTiling(NwPositionList positions, i32 networkType, GeBounds *box);
  // Private byte-return effect carrier; nominal bool/BYTE return and flag declarations remain unresolved.
  u8 ReplaceCrossing(ScOccupantManager *layer, const CtCoord &position, OcIteratorTest *positive,
      OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32 networkType, i16 dx, i16 dz, u8 toggle);
  // Original primary308 effects; its unused private return declaration remains unresolved.
  virtual void ConnectNeighbors(NwPositionList *positions, i32 networkType, u32 constructionCost);
  // Original primary312; four by-value endpoint triples and the two original direction inputs.
  virtual u8 BuildHighwayRamp(ScOccupantManager *layer, u32 count, i32 networkType, CtCoord first, CtCoord last,
      CtCoord previous, CtCoord earlier, i16 dx, i16 dz);
  // Original primary316; unused accepted count/first endpoint remain separate by-value inputs.
  virtual u8 BuildSlopeRamp(ScOccupantManager *layer, u32 count, i32 networkType, CtCoord first, CtCoord last,
      CtCoord previous, CtCoord *focus, i16 dx, i16 dz);
  // Original primary320 uses the real stack-owned prompt and modal-window callback.
  virtual u8 ConfirmConstruction(u32 text, i32 amount);
  // Actual primary324/328 have six separate inputs and full DWORD failure results.
  virtual u32 BuildBridge(ScOccupantManager *layer, NwPositionList *positions, i32 networkType, NwPieceList *pieces,
      NwPositionList *removed, u32 *cost);
  virtual u32 BuildHighwayBridge(ScOccupantManager *layer, NwPositionList *positions, i32 networkType,
      NwPieceList *pieces, NwPositionList *removed, u32 *cost);
  // Whole original placement effects; its private unused return declaration remains unresolved.
  void PlaceSpan(
      ScOccupantManager *layer, const CtCoord &position, u32 packed, i16 altitude, i32 direction, u32 length);
  // Whole private lookup retains the full DWORD result, including the attribute fallback instance.
  u32 GetTileId(ScOccupant *occupant);
  // Unsigned cell-word overload; preserves full QI/attribute result and retained occupant lifetime.
  u32 GetTileId(u16 x, u16 z);
  u8 IsUnprotectedRoad(CtCoord position);
  // Original unsigned cell coordinates and byte altitude; terrain-preparation selectors are signed DWORDs.
  bool CanPrepareVertex(u32 x, u32 z, u8 altitude, i32 networkType);
  bool PrepareVertex(u32 x, u32 z, u8 altitude, i32 networkType);
  u8 SetAltitude(u32 x, u32 z, u8 altitude);
  u8 ReadAltitude(u32 x, u32 z, u8 *altitude);
  bool PlacementSignature(const CtCoord &position, i8 signature[4]);
  bool PlacementFlat(const CtCoord &position);
  bool PlacementSlope(const CtCoord &position);
  bool PlacementWater(const CtCoord &position);
  u8 SelectSlope(const CtCoord &position, i32 networkType, NwPositionList *slopes, u32 *piece, u8 *state, u8 toggle);
  virtual bool IsRoadPiece(u32 piece);
  virtual bool IsHighwayPiece(u32 piece);
  virtual bool IsRailPiece(u32 piece);
  virtual bool IsSubwayPiece(u32 piece);
  virtual bool IsPipePiece(u32 piece);
  virtual bool IsPowerPiece(u32 piece);
  u8 ClassifyPiece(u32 piece);
  bool IsProtectedPiece(i32 networkType, u32 piece);
  // Original nonvirtual file methods; their raw AL carriers retain private nominal bool/BYTE gaps.
  u8 ReadFile(RsFile *file, char *scratch);
  u8 GrokFileTileSet(RsFile *file, char *scratch, NwPieceCatalog *catalog);
  u8 GrokFileConvertSets(RsFile *file, char *scratch, NwConversions *conversions);
  u8 GrokFileProtectedSets(RsFile *file, char *scratch, NwPieceList *pieces);
  u8 LoadCatalogs();
  u8 LoadRules();
  u8 LoadData();
  virtual ~NwManager(); // Original final primary virtual +348; intervening methods remain unreconstructed.
  // Private notification helper effects; their unused original return declarations remain unresolved.
  void AddSurface(ScOccupant *occupant);
  void RemoveSurface(ScOccupant *occupant);
  void AddPipe(ScOccupant *occupant);
  void RemovePipe(ScOccupant *occupant);
  void AddSubway(ScOccupant *occupant);
  void RemoveSubway(ScOccupant *occupant);
  u32 roadCount, highwayCount, railCount, subwayCount, pipeCount, powerCount; // Original +16..+36.
  u32 references;                                                             // Original +40.
  CtCity *city;          // Original owner +44; cISC3City CellCountX/Z are virtual +204/+208.
  ScDirtBag *terrain;    // Original owner +48.
  CtZoneLayer *zone;     // Original +52; reached canonical zone lifetime prefix.
  EcBudgetLayer *budget; // Original +56; city producer +348 returns the actual budget provider.
  ScOccupantManager *surfaceLayer, *undergroundOne, *undergroundTwo; // Original +60/+64/+68.
  CtNeighborsLayer *neighbors;                                       // Original +72.
  f32 damageFraction; // Original +76; committed by the transit-damage update phase.
  GeBounds bounds;    // Original bounds +80..+100.
  u8 debugRules;      // Original owner +104.
};

#endif
