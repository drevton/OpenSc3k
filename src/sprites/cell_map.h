#ifndef SC3_SPRITES_CELL_MAP_H
#define SC3_SPRITES_CELL_MAP_H

#include <atomic>

#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/ge_types.h"

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class SpSprite;
class GxBuffer;
struct SpDynamicSprite;
struct ApPerformanceInfo;

// Source cell fields use native-width pointers. Only comparison code handles the original x86 stride.
struct SpCell {
  SpSprite *sprite;
  i16 top, bottom;
  u8 root_x, root_z, size, altitude;
  SpDynamicSprite *dynamic;
  u16 flags, unknown18;
};

struct SpFlagEntry {
  u8 operation, x, z;
  i8 zoom;
};

struct SpArtRequest {
  u32 operation, sequence, zoom, bounds;
};

struct SpDirtyRect {
  ScRect rectangle;
  u32 flags;
};

// Original20-byte overlay record retains source and optional mask, plus the caller's point and issued identifier.
struct SpOverlay {
  GxBuffer *buffer, *mask;
  ScPoint position;
  u32 id;
};

// Original draw records occupy 44 bytes on x86; native pointers retain their native width here.
struct SpDrawRect {
  u16 kind;

  union {
    SpSprite *sprite;
    SpDynamicSprite *dynamic;
  };

  i16 x, y, clip_left, clip_top, clip_right, clip_bottom;
  u32 depth;
  u8 priority;
  ScRect footprint;
};

struct SpVisibleRun {
  i32 x, z;
  u32 count;
};

struct SpDynamicSprite {
  SpSprite *sprite;
  ScRect rectangle;
  i32 x, z, altitude;
  SpDynamicSprite *next;
  u32 depth;
  u8 priority, flags;
};

struct SpSpatialNode {
  union {
    SpDynamicSprite *sprite;
    SpCell *cell;
  };

  SpSpatialNode *next;
  u8 primary;
};

struct SpDynamicNode {
  SpDynamicNode *next;
  SpDynamicSprite *value;
};

// Original pick payload stores micro position, a borrowed sprite and the dynamic/static selection discriminator.
class SpPickObject {
public:
  i32 x, z, altitude;
  SpSprite *sprite;
  u8 dynamic;
};

// Embedded bounded spiral traversal used by the original asynchronous sprite-art loader.
class SpSpiral {
public:
  SpSpiral();
  i32 Seed(i32 x, i32 z);
  u8 Next(i32 *out_x, i32 *out_z);
  u8 Turn();
  i32 Extend();
  i32 x, z, end_x, end_z;
  ScRect bounds;
  i32 direction, order, step;
  u8 state, horizontal, excluded;
};

// Actual change-queue payload: twenty inline bytes, or an FFFF marker followed by one DWORD value/sentinel.
struct SpChange {
  SpSprite *sprite;
  i32 x, z;
  u8 invalidate;
  u16 flags;

  union {
    u8 bytes[20];

    struct {
      u16 marker, padding;
      u32 value;
    } word;
  } payload;
};

// Recovered fields and methods of cSC3CitySpriteCellMap; this is not a foreign binary-layout declaration.
class SpCellMap : public ScUnknown {
public:
  SpCellMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Original cISC3CitySpriteCellMap virtual order. Unreconstructed operations remain pure.
  virtual bool Init(u32 cellCountX, u32 cellCountZ, u32 screenWidth, u32 screenHeight, i32 unknown5, i32 unknown6,
      i32 zoom, i32 rotation, bool unknown9);
  virtual bool Shutdown();
  virtual void GetBackgroundBuffer(GxBuffer **out);
  virtual void SetBackgroundBuffer(GxBuffer *buffer, bool update);
  virtual void GetViewRect(ScRect *out) const;
  virtual void SetViewClipRect(const ScRect &clip);
  virtual void RemoveViewClipRect();
  virtual void GetViewClipRect(ScRect *out) const;
  virtual bool MoveRelative(i32 x, i32 y, bool update);
  virtual bool MoveAbsolute(i32 x, i32 y, bool update);
  virtual i32 GetZoom() const;
  virtual void SetZoom(i32 zoom, bool update, i32 focus_x, i32 focus_z);
  virtual i32 ZoomIn(bool update, i32 focus_x, i32 focus_z);
  virtual i32 ZoomOut(bool update, i32 focus_x, i32 focus_z);
  virtual i32 GetRotate() const;
  virtual void SetRotate(i32 rotation, bool update, i32 focus_x, i32 focus_z);
  virtual i32 RotateLeft(bool update, i32 focus_x, i32 focus_z);
  virtual i32 RotateRight(bool update, i32 focus_x, i32 focus_z);
  virtual i32 ScreenCellSizeX();
  virtual i32 ScreenCellAdjustmentY();
  virtual i32 ScreenCellSizeZ();
  virtual i32 MicroYToPixelUnits(i32 value, i32 scale);
  virtual bool Insert(SpSprite *sprite, u32 x, u32 z, u8 altitude, u8 size, bool invalidate, u32 flags);
  virtual bool Remove(u32 x, u32 z, bool invalidate);
  virtual bool RemoveAll();
  virtual bool StartChangeSpriteBatch();
  virtual bool EndChangeSpriteBatch();
  virtual bool ChangeSprite(u32 x, u32 z, bool invalidate, u32 flags, void *payload);
  virtual bool GetCellsForViewCornersUnclipped(ScPoint *corners, bool tight, ScRect *unused);
  virtual bool DoPick(SpPick *pick, bool artwork);
  virtual bool IsSpriteVisible(i32 x, i32 z, bool check_occlusion);
  virtual bool IsDynamicSpriteVisible(SpSprite *sprite, i32 x, i32 z, bool check_occlusion);
  virtual bool IsCityPixelSolidForSprite(i32 x, i32 z, i32 pixel_x, i32 pixel_y, bool city_relative);
  virtual void ActualGridToDrawGridMicroVirtual(i32 x, i32 z, i32 *out_x, i32 *out_z);
  virtual void DrawGridToActualGridMicroVirtual(i32 x, i32 z, i32 *out_x, i32 *out_z);
  virtual bool IsEmpty(i32 x, i32 z);
  virtual bool IsEmptyDrawGrid(i32 x, i32 z);
  virtual SpSprite *GetSprite(i32 x, i32 z);
  virtual SpSprite *GetSpriteDrawGrid(i32 x, i32 z);
  virtual u32 GetCellSizeOfSprite(i32 x, i32 z);
  virtual bool InCellBounds(i32 x, i32 z);
  virtual u32 GetActualGridSizeX();
  virtual u32 GetActualGridSizeZ();
  virtual u32 GetDrawGridSizeX();
  virtual u32 GetDrawGridSizeZ();
  virtual u32 GetAltitudeActualGrid(i32 x, i32 z);
  virtual void GetRegCellForSprite(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void GetCityPixelRectForSprite(i32 unknown1, i32 unknown2, ScRect *unknown3);
  virtual void GetVisibleAreaRectangle(ScRect *pixels, ScRect *cells);
  virtual void GetPixelSizeOfSprite(i32 unknown1, i32 unknown2, u32 *unknown3, u32 *unknown4);
  virtual void SpritePixelToMicroActualGrid(
      i32 x, i32 z, u32 pixel_x, u32 pixel_y, i32 *out_x, i32 *out_z, i32 *out_altitude);
  virtual void GetEntireCityBaseRect(ScRect *unknown1, bool unknown2);
  // Retail CodeView4932 declares these five terrain/sprite-height results as signed LONG32.
  virtual i32 GetLowestTerrainAltitude(bool micro_units) const;
  virtual i32 GetHighestTerrainAltitude(bool micro_units) const;
  virtual i32 GetLowestSpriteHeight(bool micro_units) const;
  virtual i32 GetHighestSpriteHeight(bool micro_units);
  virtual i32 GetHeightOfSprite(i32 x, i32 z, bool micro_units);
  virtual void Position3DToWindowPixel(i32 unknown1, i32 unknown2, i32 unknown3, i32 *unknown4, i32 *unknown5);
  virtual bool IsPointWithinCityOutline(i32 x, i32 y, bool window_relative);
  virtual u32 GetUnusedCellCount();
  virtual bool InMicroCellBounds(i32 x, i32 z);
  virtual bool InsertDynamicSprite(SpSprite *sprite, i32 x, i32 z, i32 altitude, bool invalidate, i32 layer, u32 flags);
  virtual bool RemoveDynamicSprite(SpSprite *sprite, i32 x, i32 z, bool invalidate);
  virtual bool RemoveAllDynamicSprites(bool invalidateScreen);
  virtual bool MoveDynamicSprite(SpSprite *sprite, i32 old_x, i32 old_z, i32 x, i32 z, i32 altitude);
  virtual bool ChangeDynamicSprite(SpSprite *sprite, i32 x, i32 z, u32 flags, void *payload);
  virtual void InvalidateDynamicSprite(SpDynamicSprite *sprite);
  virtual void GetDynamicSpritesInVolume(i32 unknown1, i32 unknown2, i32 unknown3, i32 unknown4, i32 unknown5,
      i32 unknown6, SpSprite **unknown7, u32 *unknown8);
  virtual bool IsChanged();
  virtual bool IsAbleToModifyOccupants();
  virtual GxBuffer *GetCityBufferPtr();
  virtual void GetTilingOriginWindowPixel(i32 *x, i32 *y);
  virtual bool UpdateCitySurface(ScRect const &rect);
  virtual void AddDirtyRect(const ScRect *rectangle, bool clip, u32 flags);
  virtual void AddDirtyRectWindowPixelUnits(ScRect const &rect, bool clip, u32 flags);
  virtual u32 AddOverlayBuffer(const ScPoint &position, GxBuffer *buffer, GxBuffer *mask);
  virtual void RemoveOverlayBuffer(u32 id);
  virtual void MoveOverlayBuffer(ScPoint *position, u32 id);
  virtual void InvalidateEntireScreen();
  virtual void UpdateAllDirtyRects(bool first, bool second);
  virtual void UpdateCitySurfaceAll();
  virtual i32 IncrementAlphaBlendCount(i32 count);
  virtual void CopyAllExtraRectsToBackBuffer();
  virtual void CopyEntireAreaToBackBuffer();
  virtual bool IsBackgroundTaskEnabled();
  virtual bool EnableBackgroundTask(bool enableBackgroundTask, bool processBackgroundTasks);
  virtual bool IsBackgroundTaskIdle();
  virtual bool IsCurrentZoomAndRotationLoaded();
  virtual bool IsZoomAndRotationLoaded(i32 zoom, i32 rotation);
  virtual bool LoadZoomAndRotation(i32 zoom, i32 rotation);
  virtual bool WindowPixelToActualGrid_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4,
      bool unknown5, SpPickObject *unknown6, OcTest *unknown7);
  virtual void ActualGridToWindowPixel_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void CityPixelToWindowPixel_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void WindowPixelToCityPixel_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual bool CityPixelToDrawGrid_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4, bool unknown5,
      SpPickObject *unknown6, OcTest *unknown7);
  virtual void CityPixelToDrawGridNoAltitude_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void DrawGridToCityPixel_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void DrawGridToCityPixelNoAltitude_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual bool CityPixelToActualGrid_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4, bool unknown5,
      SpPickObject *unknown6, OcTest *unknown7);
  virtual void ActualGridToCityPixel_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void ActualGridToCityPixelNoAltitude_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void DrawGridToActualGrid_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual void ActualGridToDrawGrid_NonInline(i32 unknown1, i32 unknown2, i32 *unknown3, i32 *unknown4);
  virtual bool MicroActualGridToCityPixel_NonInline(
      i32 unknown1, i32 unknown2, i32 unknown3, i32 *unknown4, i32 *unknown5);
  virtual ~SpCellMap();

  void ActualGridToDrawGrid(i32 x, i32 z, i32 *out_x, i32 *out_z);
  void DrawGridToActualGrid(i32 x, i32 z, i32 *out_x, i32 *out_z);
  void GetDrawRegistration(i32 x, i32 z, i32 *out_x, i32 *out_z);
  void CityPixelToDrawGridNoAltitude(i32 x, i32 y, i32 *out_x, i32 *out_z);
  bool CityPixelToMicroDrawGrid(i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork);
  // The private inverse's discarded nominal return is unresolved; preserve its two actual outputs.
  void CityPixelToMicroDrawGridNoAltitude(i32 x, i32 y, i32 *out_x, i32 *out_z);
  u32 CellPart(i32 x, i32 y, i32 draw_x, i32 draw_z);
  i32 InDrawBounds(i32 x, i32 z);
  i32 BuildVisibleRuns(const ScRect *rectangle, i32 minimum_diagonal);
  void ActualGridToCityPixel(i32 x, i32 z, i32 *out_x, i32 *out_y);
  i32 DrainFlags();
  void RebuildGeometry();
  i32 TranslateDiamond(i32 x, i32 y);
  void RebuildOutline();
  void ClearDirtyRects();
  // Reached private draw-list boundary; the SDK does not expose its nominal declaration.
  u32 QueueDrawRect(const ScRect *rectangle, u32 flags);
  u32 BuildDisplayListLowZoom(const ScRect *rectangle, u32 flags);
  u32 BuildDisplayListHighZoom(const ScRect *rectangle, u32 flags);
  bool OutsideCity(const ScRect *rectangle, bool relative);
  void ResolveRoot(SpCell *cell, SpCell **root, i32 *out_x, i32 *out_z);
  u32 SortDrawRecords(u32 first, u32 count, bool refine);
  void RebuildDepths();
  i32 PropagateLayerUpdate(i32 old_zoom, i32 old_rotation, i32 zoom, i32 rotation);
  u8 ReleaseSpriteArt(SpSprite *sprite, i32 zoom, i32 rotation, u32 x, u32 z);
  u8 DemandSpriteArt(SpSprite *sprite, i32 zoom, i32 rotation, u32 x, u32 z);
  void RefreshRegistration(bool refresh);
  i16 RefreshTileRegistration(i32 x, i32 z);
  i32 UpdateMaximumSpan(i32 previous, i32 current);
  i32 UpdateStaticIndex(SpCell *cell, const ScRect *previous, const ScRect *current);
  void Expose(const ScRect *previous, const ScRect *viewport, bool invalidate);
  // Original private dependencies remain explicit until their implementations and resource interfaces are recovered.
  u8 FillBackground(const ScRect *rectangle, bool locked);
  void FollowTarget();
  uptr FinishBackgroundTask();
  u8 StopLoader();
  u8 StartLoader();
  u32 RunLoader();
  u8 StartLoaderJob();
  u8 NextLoaderCell();
  void RemoveLoadedCheck(i32 zoom, i32 rotation);
  void QueueLoadedCheck(i32 zoom, i32 rotation, u32 position);
  void ActualGridToDrawGridMicro(i32 x, i32 z, i32 *out_x, i32 *out_z);
  i32 *DynamicToCityPixel(SpDynamicSprite *sprite, i32 *out_x, i32 *out_y);
  ScRect *DynamicBounds(SpDynamicSprite *sprite, ScRect *out);
  bool FindDynamicSprite(SpSprite *sprite, i32 *x, i32 *z, i32 *cell_x, u32 *cell_z);
  SpDynamicSprite *FindDynamicRecord(SpSprite *sprite, i32 x, i32 z);
  void UpdateDynamicIndex(SpDynamicSprite *sprite, const ScRect *previous, const ScRect *current);
  void IndexDynamicSprite(SpDynamicSprite *sprite);
  void UnindexDynamicSprite(SpDynamicSprite *sprite);
  void InsertDynamicBucket(i32 x, i32 z, SpDynamicSprite *sprite);
  void RemoveDynamicBucket(i32 x, i32 z, SpDynamicSprite *sprite);
  void ClearSpatialGrid();
  u32 ConfigureSpatialGrid(u32 columns, u32 rows, u8 refresh);
  i32 IndexStaticCell(SpCell *cell, ScRect *rectangle, u8 insert);
  i32 RefreshDynamicBounds();
  void ReindexDynamicSprites(u8 refresh);
  u32 DynamicDepth(SpDynamicSprite *sprite, i32 x, i32 z, u32 altitude);
  SpSpatialNode *GetSpatialBucket(i32 x, i32 y);
  bool PickDrawGrid(i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter);
  bool PickActualGrid(i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter);
  i32 *ClampActualGrid(i32 *x, i32 *z);
  u32 AllocateDirtyGrid(u32 columns, u32 rows);

  u32 actual_width, actual_height, draw_width, draw_height;
  SpCell **cells;
  i32 zoom, rotation;
  u32 cell_width, altitude_scale, cell_height, shift_x, shift_y, shift_altitude;
  u32 half_shift_x, half_shift_y, half_shift_altitude;
  ScRect viewport, world_clip;
  ScRect visible_outer, visible_inner;
  ScRect city_bounds;
  ScPoint diamond[4], outline[12];
  u8 initialized, pending, drawing_ready, threaded, loader_initialized;
  std::atomic<u8> stop_loader;
  u32 references;
  ScLock drawing_lock;
  ScArray<SpFlagEntry> completed_flags;
  SpSprite *fallback[6];
  u32 registered_count, fallback_art_references;
  u16 altitude_counts[256];
  u32 maximum_span, maximum_count;
  const ApPerformanceInfo *performance_info;
  u32 residency[5], art_sequence, work_mode;
  f32 load_budget;
  i32 loader_x, loader_z, loader_center_x, loader_center_z;
  SpSpiral loader_spiral;
  ScRect loader_bounds;
  i8 loader_zoom, loader_rotation;
  u8 preload;
  ScArray<SpArtRequest> art_requests;
  ScThread drawing_thread;
  SpDynamicNode **dynamic_buckets;
  u32 dynamic_bucket_count;
  ScArray<SpDirtyRect> dirty_rects;
  ScArray<SpDrawRect> draw_rects, extra_rects, overlay_rects;
  ScArray<u32> draw_order;
  u32 draw_total;
  GxBuffer *city_buffer, *background_buffer, *back_buffer;
  u32 background_color;
  SpDynamicSprite *follow_target;
  u32 frame_count;
  i32 alpha_blend_count;
  u32 copy_index;
  u8 following, immediate_redraw, copy_extra_rects, changed;
  u8 changing_batch;
  ScArray<SpChange> sprite_changes;
  ScArray<ScRect> copy_rects[2];
  u8 spatial_enabled;
  SpSpatialNode **spatial_buckets;
  u32 spatial_shift, spatial_height_shift, spatial_width, spatial_height;
  u32 bucket_width, bucket_height;
  f32 inverse_bucket_width, inverse_bucket_height;
  u8 *dirty_mask;
  u32 dirty_width, dirty_height, dirty_shift_x, dirty_shift_y, dirty_cell_width, dirty_cell_height;
  u8 dirty_mask_enabled;
  u8 minimum_altitude, maximum_altitude;
  ScArray<SpVisibleRun> visible_runs;
  ScArray<SpOverlay> overlays;
  u32 next_overlay_id = 0;
};

static_assert(sizeof(uptr) != 4 || sizeof(SpOverlay) == 20);

class SpSprite;
class ScOccupant;
struct ScPoint;

// Original cISC3Pick::SelectedItem, retail CodeView48EA/48EB; native pointers retain host width.
struct SpPickItem {
  SpPickItem();
  ScRect rectangle;
  CtCoord location;
  GeBounds area;
  i32 distance;
  ScGeomFlag flags;
  u8 aligned;
  ScOccupant *occupant;
  SpSprite *sprite;
};

enum class SpPickInput : i32 { ViewportPoint, ViewportRect, CityGridPoint, CityGridBounds };

// Complete original cISC3Pick order and signatures from retail CodeView4900..4912. Original byte returns remain
// bytes, and mutable references become explicit pointers without adding or reordering parameters.
class SpPick : public ScUnknown {
public:
  virtual u8 SetSelectionCriteria(GeBounds *bounds, ScGeomFilter *filter, u32 maximum) = 0;
  virtual u8 SetSelectionCriteria(CtCoord *point, ScGeomFilter *filter, u32 maximum) = 0;
  virtual u8 SetSelectionCriteria(ScRect *rectangle, ScGeomFilter *filter, u32 maximum) = 0;
  virtual u8 SetSelectionCriteria(ScPoint *point, ScGeomFilter *filter, u32 maximum) = 0;
  virtual SpPickInput GetSelectionInputType() = 0;
  virtual u8 GetSelectionInput(SpPickItem *out) = 0;
  virtual ScGeomFilter *GetSelectionTest() = 0;
  virtual u8 ClearSelection() = 0;
  virtual u32 GetMaxSelectionCount() const = 0;
  virtual u8 GetSelectedCount() = 0;
  // Original GetSelected(array, maximum); use a plural name because converting its reference overload to a pointer
  // would otherwise produce two identical parameter lists that differ only in return type.
  virtual u32 GetSelectedItems(SpPickItem *out, u32 maximum) = 0;
  virtual u8 GetSelected(SpPickItem *out, u32 index) = 0;
  virtual u32 AddSelection(const SpPickItem &item, u32 index) = 0;
};

static_assert(sizeof(uptr) != 4 || sizeof(SpPickItem) == 72);

#endif
