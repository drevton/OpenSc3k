#ifndef SC3_TERRAIN_TR_DIRT_BAG_H
#define SC3_TERRAIN_TR_DIRT_BAG_H

#include "city/api.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "terrain/api.h"

class CtChangeReceiver;

// Original secondary source at DirtBag +4. The receiver vector retains actual city-change interfaces.
class TrChangeSource : public CtChangeSource {
public:
  explicit TrChangeSource(u32 sender);
  virtual ~TrChangeSource();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 GetID() const override;
  virtual bool AddReceiver(CtChangeReceiver *receiver, u32 *sender) override;
  virtual bool RemoveReceiver(CtChangeReceiver *receiver) override;
  virtual bool RemoveAll() override;
  virtual void SetEnabled(u8 enabled) override;
  virtual u8 GetEnabled() const override;
  virtual bool NotifyAll() override;
  virtual bool Notify(u32 x, u32 z) override;
  virtual bool Notify(u32 left, u32 top, u32 right, u32 bottom) override;

protected:
  void GrowReceivers();
  // Original secondary +4/+8, followed by raw enabled byte +12 and actual receiver vector +16/+20/+24.
  u32 sender_id, source_references;
  // Original secondary +12, therefore DirtBag +16. Noncanonical nonzero byte values remain intact.
  u8 updates_enabled;
  CtChangeReceiver **receivers_begin, **receivers_end, **receivers_limit;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScStringView;
class ScDirtBag;

// Genuine SDK cISC3DirtGenerator contract, independent of the partial concrete generator's fields and private phases.
class TrGeneratorInterface : public ScUnknown {
public:
  virtual bool Init(u32 cells_x, u32 cells_z) = 0;
  // Unlimited175A2 shares the void destructor tail and defines no Boolean result; Init discards it.
  virtual void Shutdown() = 0;
  virtual void SetDifficulty(i32 difficulty) = 0;
  virtual bool IsReady() const = 0;
  virtual bool GenerateRandom(i32 seed, u8 control3, u8 control4, u8 control5, u8 flags) = 0;
  virtual bool GenerateAltFromBMP8(const ScStringView *name, u8 threshold) = 0;
  virtual bool GenerateAltFromBMP24(const ScStringView *name) = 0;
  virtual bool ReflectMap(ScDirtBag *target) = 0;
};

class GxBuffer;
class RsManager;

// Original scoped SIMDIRT image resource owner. The queried external ABI publishes directly into its actual pointer slot.
class TrImageHandle {
public:
  TrImageHandle();
  TrImageHandle(const TrImageHandle &other);
  TrImageHandle &operator=(const TrImageHandle &other);
  TrImageHandle(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~TrImageHandle();
  GxBuffer *Value() const;
  bool IsBufferPresent() const;

protected:
  void LoadResource(RsManager *manager, u32 group, u32 instance);
  void *buffer;
};

class GxBuffer;
class GxPaletteView;
class TrDirtBag;
class ScStringView;

// Recovered IID5DD raster base fields and reached methods from the Windows copy of cSC3MapSprite.
// The shared IID5DD prefix follows the installed slot order; private nominal declarations remain qualified.
class TrRasterSource : public GxRasterSource {
public:
  TrRasterSource();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 id, u32 width, u32 height) override;
  u32 GetId() const override;
  bool SetId(u32 id) override;
  bool SetPalette(GxPaletteView *source) override;
  GxPaletteView *GetPalette() override;
  i32 GetBaseWidth() const override;
  i32 GetBaseHeight() const override;
  bool SetBaseSize(u32 width, u32 height) override;
  // Windows stores raw bytes; Linux bool spelling does not justify normalizing this state.
  u8 SetPostFilter(u8 enabled) override;
  // Native effects of private1001DAF2; the discarded nominal result declaration is undocumented.
  void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) override;
  u8 IsPostFilter() const override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;
  // Folded debug entries return byte zero; original nominal Boolean/BYTE declarations remain unresolved.
  u8 DebugInstanceTag(ScStringView *out) override;
  u8 DebugClassTag(ScStringView *out) override;
  u8 DebugTypeTag(ScStringView *out) override;
  virtual bool GetColor(u32 x, u32 z, u32 *out);
  virtual bool DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual bool DrawAnnotations(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual u8 SetDebugInstanceTag(const ScStringView &value);
  virtual u8 SetDebugClassTag(const ScStringView &value);
  virtual ~TrRasterSource();

protected:
  u32 references, kind;
  GxPaletteView *palette;
  i32 width, height;
  u8 enabled;
  // Actual four corner coordinates at original+28..56; constructor's three defining points produce the unit square.
  i32 corner_x[4], corner_z[4];
};

// Actual64-byte PC derived terrain map-color source, retaining the terrain owner at+60.
// The factory publishes its actual raster identity while retaining the original terrain owner.
class TrTerrainMapSprite : public TrRasterSource {
public:
  explicit TrTerrainMapSprite(TrDirtBag *terrain);
  ~TrTerrainMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

protected:
  TrDirtBag *terrain;
};

class GxBuffer;
class ScString;

// Original terrain palette rows and image shade loading. Renderer palette selection remains a separate receiver method.
class TrPalette {
public:
  // Original maximum light index: PC100208F8 low byte and Linux mkLightIndexMax at5171B.
  static constexpr u8 LightIndexMax = 31;
  TrPalette();
  ~TrPalette();
  void Shutdown();
  // Native binary64 result of the original static angular-step helper; nominal FP declaration remains qualified.
  static f64 GetLightStep();
  // Original const cDCColor* payload uses canonical packed RGB values; private nominal BYTE/bool remains unresolved.
  u8 InitFromPalette(i32 count, const ScRgb8 *rgb);
  u8 InitFromBuffer(GxBuffer *image);
  u8 InitFromFile(const ScString &name);
  u8 InitFromPaletteFile(const ScString &name);

  bool initialized;
  u16 maximum_row;
  u16 **rows;
};

class CtCity;

// Concrete record ownership reached by the terrain frame. The read and write constructors pass the real key by value.
// Retail cSLAutoDBRecord declarations establish the Boolean predicate and signed unused writer argument.
class TrRecordGuard {
public:
  TrRecordGuard(RsDatabaseSegment *segment, RsKey key);
  TrRecordGuard(RsDatabaseSegment *segment, RsKey key, i32 flags);
  virtual ~TrRecordGuard();
  virtual bool IsValid() const;
  RsRecord *GetDBRecordPointer() const;
  u8 aborted;
  RsRecord *record;
  RsDatabaseSegment *segment;
};

// The derived record retains the single queried stream and releases it before the base closes or aborts the record.
class TrRecordStream : public TrRecordGuard {
public:
  TrRecordStream(RsDatabaseSegment *segment, RsKey key);
  TrRecordStream(RsDatabaseSegment *segment, RsKey key, i32 flags);
  ~TrRecordStream() override;
  bool IsValid() const override;
  RsStream *GetDBSerialRecordPointer() const;

private:
  // Direct QueryInterface publication uses the genuine external void** ABI without aliasing a typed pointer object.
  void *stream_identity;
};

// Original cSLSaveRecordVersionInfo owns the version, two flag bytes and cookie in its twelve-byte Windows base.
class TrRecordVersion {
public:
  TrRecordVersion();
  TrRecordVersion(u16 version, u8 debug);
  virtual ~TrRecordVersion();

  // Return the stored version WORD through the original const accessor, inlined by the installed terrain callers.
  u16 RecordVersion() const {
    return version;
  }

  // Return the stored debug byte. Retail type20 leaves its nominal bool/BYTE declaration unresolved.
  u8 IsDebug() const {
    return debug;
  }

protected:
  u16 version;
  u8 debug, extended;
  u32 magic;
};

// Actual terrain record-frame fields, independent of foreign addresses and initialized-memory comparison metadata.
// Retail cSLAutoSaveRecordVersionInfo declarations establish the by-value key, WORD version and Boolean debug flag.
class TrRecordFrame : public TrRecordVersion {
public:
  TrRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  TrRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, bool debug);
  ~TrRecordFrame() override;
  RsStream *GetDBSerialRecordPointer() const;
  void SetFailed();

  // Return the frame's current validity byte without normalizing stream replies; nominal bool/BYTE remains unresolved.
  u8 IsValid() const {
    return valid;
  }

protected:
  TrRecordStream *record;
  u8 valid;
  i32 city_version[3];
};

// Original localized text owner retains the String interface independently of its three-word resource key.
class TrTextResource {
public:
  TrTextResource();
  TrTextResource(const TrTextResource &other);
  TrTextResource &operator=(const TrTextResource &other);
  TrTextResource(u32 instance, u32 group);
  virtual ~TrTextResource();
  // Original conversion returns an owned String value; its return storage is supplied by the C++ compiler.
  operator ScString();
  const ScStringView *Value() const;
  void GetResKey(RsKey *out) const;
  bool IsStringPresent();

protected:
  void LoadResource();
  void UnloadResource();
  // GetResource publishes directly through its genuine void** ABI slot; typed access does not alias a pointer object.
  void *text_identity;
  RsKey key;
};

// Source symbols establish these free-function names and inputs; native signed results carry the -1 sentinel.
i32 TrDistanceToEdge(u16 *distance, u32 width, u32 x, u32 z);
i32 TrSeaportFindPath(u16 *distance, u32 width, u32 *path, i32 limit, u32 x, u32 z);
// Native decomposition of the original seaport-map computation; private helper return declarations remain unresolved.
void TrSeaportRecompute(u16 *distance, u32 *reach, u32 *open, u32 *deep, u32 width);

class CtCity;
class ScImporter;
class RsDatabaseSegment;
class TrRenderer;
class SpSprite;
class UiQuery;
class UiQueryWindow;

// Original byte-grid storage is X-major. The column allocations belong to the terrain lifetime methods.
struct TrByteGrid {
  u32 width, height;
  u8 **columns;
};

// Original change-map columns contain ceil(height / 32) words, including initialized tail bits.
struct TrChangeGrid {
  u32 width, height, words;
  u32 **columns;
};

// Recovered terrain object with real ownership and the three verified interface identities.
// Private helper declaration gaps remain recorded; this native declaration is not a complete binary layout.
class TrDirtBag : public ScDirtBag, public TrChangeSource, public SpMapSpriteFactory {
public:
  TrDirtBag();
  ~TrDirtBag() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *context) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  virtual bool Init(CtCity *city, ScImporter *importer) override;
  virtual bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  void GetVertexAltitude(u32 x, u32 z, u8 *altitude) const override;
  void GetVertexAltitudeDirt(u32 x, u32 z, u8 *altitude) const override;
  void GetVertexAltitudeWater(u32 x, u32 z, u8 *altitude) const override;
  bool IsWater(u32 x, u32 z) const override;
  u8 CellWaterVertCount(u32 x, u32 z) const override;
  bool IsRealWater(u32 x, u32 z) const override;
  u8 GetGlobalSeaLevel() const override;
  void SetGlobalSeaLevel(u8 level) override;
  u8 GetAverageAltitude() const override;
  u32 GetLayerType() override;
  void GetVertexLight(u32 x, u32 z, u8 *light) const override;
  u8 GetDirtClod(u8 kind, u32 x, u32 z, bool dry_only, SpSprite **out, u8 *altitude, u8 overlay) const override;
  u8 GetDirtClodSingle(u8 kind, u16 row, u32 x, u32 z, bool dry_only, SpSprite **out, u8 *altitude) const override;
  u8 GetDirtClodEdge(u32 x, u32 z, bool reverse_side, bool dry_only, SpSprite **out, u8 *altitude) const override;
  bool SetVertexAltitude(u32 x, u32 z, u8 height) override;
  bool SetVertexAltitude(u32 left, u32 top, u32 right, u32 bottom, u8 height) override;
  bool SetWaterTable(u32 x, u32 z, u8 height, bool shore) override;
  bool InitFloraLevel(u32 x, u32 z, u8 flora) override;
  bool SetWaterTable(u32 left, u32 top, u32 right, u32 bottom, u8 height) override;
  f32 AltitudeScale() const override;
  bool GenerateAltFromBMP(ScStringView *name) override;
  bool RecalcSaltWater() override;
  const ScStringView *GetQueryString(u32 x, u32 z) const override;
  void Tick(i32 first, i32 second, const ScRect &rectangle, u32 flags) override;
  bool GetGrid() const override;
  void SetGrid(bool enabled) override;
  void RegenPalette(u8 index) override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  u32 VertexCountX() const override;
  u32 VertexCountZ() const override;
  u8 MinAltitudeAllowed() const override;
  u8 MaxAltitudeAllowed() const override;
  u8 MaxAltitudeDeltaAllowed() const override;
  bool InCellBounds(u32 x, u32 z) const override;
  bool InCellBounds(u32 left, u32 top, u32 right, u32 bottom) const override;
  bool InVertexBounds(u32 x, u32 z) const override;
  bool InVertexBounds(u32 left, u32 top, u32 right, u32 bottom) const override;
  bool IsValidVertexAltitude(u8 altitude) const override;
  bool IsValidVertexLight(u8 light) const override;
  u8 AltitudeMax() const override;
  u8 AltitudeMin() const override;
  u8 MaxAltitudeDelta() const override;
  void ResetChanges() const override;
  void MarkVertexChanged(u32 x, u32 z) const override;
  void MarkCellChanged(u32 x, u32 z) const override;
  bool ChangedByLastOperation(u32 x, u32 z) const override;
  void LockUpdates(bool locked) override;
  void RecomputeSeaportMap() override;
  bool Distance(u32 x, u32 z, u32 *distance) const override;
  bool GetWaterPath(u32 x, u32 z, u32 capacity, ScPointXZ *path, u32 *count) const override;
  u32 GetPathGranularity() const override;
  void BlockCell(u32 x, u32 z, bool blocked) override;
  bool IsCellBlocked(u32 x, u32 z) const override;
  bool IsDeepWaterPresent() override;
  bool IsNeighborPresent(u8 directions) const override;
  bool InstallFunctionFilter(TrFunctionFilter filter, void *context, bool install) override;
  bool CanRaiseTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost) const override;
  bool CanLowerTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost) const override;
  bool CanLevelTerrain(const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost) const override;
  bool GetOptimalLevel(const GeBounds &bounds, u8 *altitude, GeBounds *affected, u32 *cost) const override;

  bool RaiseTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) override;
  bool LowerTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) override;
  bool LevelTerrain(const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost, bool free_of_charge) override;

  bool CanSetupZone(const GeBounds &bounds, u32 *cost, bool smooth) const override;
  bool SetupZone(const GeBounds &bounds, u32 *cost, bool free_of_charge, bool smooth) override;
  bool CanPrepareForRoad(SpSelection *selection, GeBounds *affected, u32 *cost) const override;
  bool PrepareForRoad(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) override;
  bool CanPrepareForHighway(SpSelection *selection, GeBounds *affected, u32 *cost) const override;
  bool PrepareForHighway(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) override;
  bool CanPlaceWater(SpSelection *selection, GeBounds *affected, u32 *cost) const override;
  bool PlaceWater(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) override;
  bool CanPlaceWater(const GeBounds &bounds, GeBounds *affected, u32 *cost) const override;
  bool PlaceWater(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) override;

protected:
  friend class TrRenderer;
  // Private sprite helpers retain the original separate arguments and verified Linux names, widths and const receivers.
  u8 GetDirtClodLand(u8 kind, u32 x, u32 z, SpSprite **out, u8 *altitude, u8 overlay) const;
  u8 GetDirtClodShore(u8 kind, u32 x, u32 z, SpSprite **out, u8 *altitude, u8 overlay) const;
  u8 GetDirtClodWater(u8 kind, u32 x, u32 z, SpSprite **out, u8 *altitude, u8 overlay) const;
  u8 GetDirtClodCursor(u8 kind, u32 x, u32 z, u16 row, bool dry_only, SpSprite **out, u8 *altitude) const;
  // Native phases shared by the original simulation-start and terrain-message entries.
  void ClearSaltFlags() const;
  void PublishInitialLayers();
  // Private message effects under recovery; their discarded original nominal returns remain unresolved.
  u32 ScanSelectedWater(SpSelection *selection, u32 *left, u32 *top, u32 *right, u32 *bottom, bool apply) const;
  void ClearCityZones();
  // Original member registration effects; the discarded private nominal return remains undocumented.
  void RegisterCheats();
  void CheatLoadTerrain(ScStringView *command);
  void CheatTerrainShift(i32 delta);
  // Complete private query effects; their original nominal bool/BYTE declarations remain unresolved.
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 flags);
  // Native shared interior phases; the original zone entry signatures remain separate above.
  void ShapeZone(u32 left, u32 top, u32 right, u32 bottom, bool smooth, bool apply, u32 *raised, u32 *lowered) const;
  bool ApplyTerrain(
      const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost, bool free_of_charge, i32 direction);
  void SeedAppliedTerrain(u32 x, u32 z, u8 altitude, i32 direction);
  // Native shared phase of the original separate raise/lower validation entries.
  bool CanShiftTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool raising) const;
  // This observed register carrier leaves the private source return type explicitly unresolved.
  virtual u32 GetVariantValue(ScRect rectangle);
  virtual bool FunctionFilterTestPassed(const GeBounds &bounds, u32 cost, i32 kind) const;
  // Original primary state: initialized+36, shared terrain references+40, filter sentinel+44.
  bool initialized;
  u32 references;
  // Original sentinel pointer at +44; receiver lifetime constructs and destroys this circular filter chain.
  TrFilterLink *filters;
  // Native decomposition of 10006370's verified effects; its private source return declaration remains unresolved.
  void PublishChanges(u32 left, u32 top, u32 right, u32 bottom, u8 changed_only);
  // Native decomposition of private 10007010 effects; its unused return declaration remains unresolved.
  void CalculateAndSetVertexLight(u32 left, u32 top, u32 right, u32 bottom);
  // Original const five-sample helper inputs; its unused nominal return remains undocumented.
  void GetVertAltForLightCalc(u32 x, u32 z, u8 *heights) const;
  // Original typed lighting inputs/outputs; discarded private nominal returns remain unresolved.
  void GetVertexNormal(f32 step, f32 scale, const u8 *heights, ScPoint3F *normal) const;
  void GetVertexLightIndex(const ScPoint3F &normal, const ScPoint3F &light, u8 *out) const;
  static void VecDotProduct(const ScPoint3F &first, const ScPoint3F &second, f32 *out);
  static void VecUnit(const ScPoint3F &value, ScPoint3F *out);
  // Verified original fields: initialized +36, ground +48, water +56, sea +60, changes +64.
  TrByteGrid *ground, *shade, *water;
  u8 sea;
  TrChangeGrid *changes;
  // Original optional initialization caches: flora bytes +68 and shore bits +72.
  TrByteGrid *flora_cache;
  TrChangeGrid *shore_cache;
  // Verified original fields: distance map +76, blocked cells +92, deep-water flag +96, city +100.
  u16 *distances;
  u32 *water_reach, *water_open, *water_deep;
  u32 *blocked_cells;
  bool deep_water;
  CtCity *city;
};

class TrDirtBag;
class ScDirtBag;
class GxBuffer;
class SpSprite;
struct TrByteGrid;

// Shared renderer receiver retains actual palette/cache state and borrowed terrain planes for the tile drawing classes.
class TrRenderer : public ScMessageTarget {
public:
  TrRenderer();
  ~TrRenderer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Observed private startup/shutdown effects; AL is one, but the original nominal declarations are undocumented.
  u8 Init();
  u8 Shutdown();
  // Original168B7 raw byte effects; private nominal BYTE/bool declaration remains undocumented.
  u8 SetGrid(u8 enabled);
  // Source const getter preserves the raw Windows grid byte; nominal Boolean/BYTE spelling remains undocumented.
  u8 GetGrid() const;
  // Complete private16852 AL effects; original nominal integer return width remains undocumented.
  u8 GetLightIndex(f32 cosine) const;

  u8 GetMaxLightIndex() const {
    return TrPalette::LightIndexMax;
  }

  // Forward the palette's angular step through the original const renderer method, verified by Linux3ECE8.
  // The native f64 preserves its observed FP result; the original nominal FP return declaration remains unresolved.
  f64 GetLightStep() const {
    return TrPalette::GetLightStep();
  }

  // Read the actual renderer-owned palette row and light column, preserving the inlined Windows lookup.
  // Linux3EC6C verifies the inputs; u16 carries the pixel while nominal return width/signedness remains unresolved.
  u16 GetColorInNative16Bit(u8 palette, u16 row, u8 light) const {
    return palettes[palette].rows[row][light];
  }

  bool DoMessage(ScMessage *message) override;
  // Native effects of private palette methods; their discarded nominal declarations remain unresolved.
  void RegenPalette(u8 index);
  u8 BuildColorLightTables();
  void ReleaseColorLightTables();
  // Complete original inlined cache-clear effects; the discarded nominal return declaration remains unresolved.
  void InvalidateColorTable();
  void SetAltitudePalette(u32 selection);
  // Verified effects of private10016798; its discarded nominal return declaration remains unresolved.
  void SetDirtBag(ScDirtBag *terrain);
  // Source-verified const factory inputs; observed AL is one, but nominal BYTE/bool returns remain undocumented.
  u8 GetDirtClodLand(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const;
  u8 GetDirtClodShore(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const;
  u8 GetDirtClodWater(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const;
  u8 GetDirtClodCursor(u32 cell_x, u32 cell_z, u8 palette_kind, u16 row, bool dry_only, SpSprite **out) const;
  u8 GetDirtClodEdge(u32 cell_x, u32 cell_z, bool reverse_side, bool dry_only, SpSprite **out) const;

protected:
  friend class TrDirtBag;
  friend class TrTile;
  friend class TrDryTile;
  friend class TrWaterTile;
  friend class TrReliefTile;
  friend class TrSingleTile;
  friend class TrEdgeTile;
  friend void TrLoadOverlayColor(GxBuffer *device, i32 zoom, u8 index);
  // Original ready+8, grid+9, palettes+12, terrain+20, ground/shade/water+24/+28/+32 and sea+36.
  u32 references;
  bool ready;
  u8 grid;
  TrPalette *palettes;
  u32 field16;
  ScDirtBag *terrain;
  TrByteGrid *ground, *shade, *water;
  u8 sea;
  u32 selection;
  u16 *cache;
};

// Original global10024A28 is published by terrain StaticInit, independently of the private factory singleton10025BAC.
extern TrRenderer *TrTerrainRenderer;
TrRenderer *TrGetRenderer();
// Original non-owning private cache10025BAC, distinct from the terrain-published renderer.
extern TrRenderer *TrRendererSingleton;

// Original five-level pixel geometry tables populated by the renderer constructor.
extern u32 TrTileWidths[5], TrTileHalfWidths[5], TrTileHeights[5], TrTileScales[5];

#endif
