#include "sprites/cell_map.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
#include <vector>
#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_stopwatch.h"
#include "common/rect.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "sprites/api.h"

// Compute the dynamic record's unsigned depth key, including the rotation-specific bias for footprint flags one/two.
u32 SpCellMap::DynamicDepth(SpDynamicSprite *sprite, i32 x, i32 z, u32 altitude) {
  u32 bias = kSpCellDepthOrigin;
  if (sprite->flags & 3u) {
    if (rotation == 1)
      bias += 8192u;
    else if (rotation == 3)
      bias -= 8192u;
  }
  return bias - (((u32)x + (u32)z) << 6) - (altitude >> 2);
}

// Recompute every registered dynamic depth in the existing pointer-hash traversal order after camera rotation.
void SpCellMap::RebuildDepths() {
  for (u32 bucket = 0; bucket < dynamic_bucket_count; ++bucket)
    for (SpDynamicNode *node = dynamic_buckets[bucket]; node; node = node->next) {
      SpDynamicSprite *record = node->value;
      i32 x, z;
      ActualGridToDrawGridMicro(record->x, record->z, &x, &z);
      record->depth = DynamicDepth(record, x, z, (u32)record->altitude);
    }
}

void SpCellMap::InvalidateDynamicSprite(SpDynamicSprite *sprite) {
  AddDirtyRect(&sprite->rectangle, false, 0);
}

bool SpCellMap::StartChangeSpriteBatch() {
  changing_batch = 1;
  return true;
}

// Disable batching before dispatching queued dynamic/static operations in order, then clear the trivial record range.
// Pass a pointer to inline data or to the stored DWORD; only a stored minus-one value selects a null payload.
bool SpCellMap::EndChangeSpriteBatch() {
  changing_batch = 0;
  for (u32 i = 0; i < sprite_changes.size; ++i) {
    SpChange *change = &sprite_changes.data[i];
    u16 marker;
    memcpy(&marker, &change->payload, sizeof(marker));
    void *payload = &change->payload;
    if (marker == 0xffffu)
      payload = change->payload.word.value == UINT32_MAX ? nullptr : &change->payload.word.value;
    if (change->sprite)
      ChangeDynamicSprite(change->sprite, change->x, change->z, change->flags, payload);
    else
      ChangeSprite((u32)change->x, (u32)change->z, change->invalidate != 0, change->flags, payload);
  }
  sprite_changes.Clear();
  return true;
}

// Demand artwork, publish the dynamic record to the actual cell and spatial/hash indexes, then retain its sprite.
// Duplicate sprites reject before demanding art; invalidation precedes the final AddRef exactly as in the source.
bool SpCellMap::InsertDynamicSprite(
    SpSprite *sprite, i32 x, i32 z, i32 altitude, bool invalidate, i32 layer, u32 flags) {
  SpDynamicSprite **head = &cells[x / 256][z / 256].dynamic;
  for (SpDynamicSprite *record = *head; record; record = record->next)
    if (record->sprite == sprite)
      return false;
  if (!sprite->DemandArt(zoom, rotation, 0))
    return false;
  SpDynamicSprite *record = new SpDynamicSprite;
  record->sprite = sprite;
  record->x = x;
  record->z = z;
  record->altitude = altitude;
  record->flags = (u8)(flags & 15u);
  record->next = *head;
  i32 draw_x, draw_z;
  ActualGridToDrawGridMicro(x, z, &draw_x, &draw_z);
  record->depth = DynamicDepth(record, draw_x, draw_z, (u32)altitude);
  record->priority = (u8)layer;
  *head = record;
  DynamicBounds(record, &record->rectangle);
  IndexDynamicSprite(record);
  u32 bucket = (u32)(((uptr)record >> 2) % dynamic_bucket_count);
  dynamic_buckets[bucket] = new SpDynamicNode{dynamic_buckets[bucket], record};
  if (invalidate)
    InvalidateDynamicSprite(record);
  sprite->AddRef();
  return true;
}

// Remove the located cell record after optional invalidation and artwork release, then release its sprite ownership.
// Preserve surviving cell/hash/spatial order while replacing only the original record allocator with delete.
bool SpCellMap::RemoveDynamicSprite(SpSprite *sprite, i32 x, i32 z, bool invalidate) {
  SpDynamicSprite **link = SpFindDynamicLink(this, sprite, &x, &z);
  if (!link)
    return false;
  SpDynamicSprite *record = *link;
  if (invalidate)
    InvalidateDynamicSprite(record);
  sprite->ReleaseArt(zoom, rotation);
  *link = record->next;
  UnindexDynamicSprite(record);
  u32 bucket = (u32)(((uptr)record >> 2) % dynamic_bucket_count);
  SpDynamicNode **hash_link = &dynamic_buckets[bucket];
  while (*hash_link && (*hash_link)->value != record)
    hash_link = &(*hash_link)->next;
  SpDynamicNode *node = *hash_link;
  *hash_link = node->next;
  delete node;
  delete record;
  sprite->Release();
  return true;
}

// Remove each dynamic sprite in hash iteration order. Advance the iterator before invoking the virtual removal,
// then invalidate once when a nonempty collection was visited and the caller requested it.
bool SpCellMap::RemoveAllDynamicSprites(bool invalidate) {
  u32 bucket = 0;
  while (bucket < dynamic_bucket_count && !dynamic_buckets[bucket])
    ++bucket;
  SpDynamicNode *node = bucket < dynamic_bucket_count ? dynamic_buckets[bucket] : nullptr;
  if (!node)
    return true;
  while (node) {
    SpDynamicSprite *record = node->value;
    SpSprite *sprite = record->sprite;
    i32 x = record->x, z = record->z;
    node = node->next;
    if (!node)
      while (++bucket < dynamic_bucket_count) {
        node = dynamic_buckets[bucket];
        if (node)
          break;
      }
    RemoveDynamicSprite(sprite, x, z, false);
  }
  if (invalidate)
    InvalidateEntireScreen();
  return true;
}

// Collect dynamic sprites in a half-open micro-coordinate volume, bounded by the caller's input capacity.
// Preserve the source's zero upper-coordinate clipping and inclusive cell traversal, including its empty results.
void SpCellMap::GetDynamicSpritesInVolume(
    i32 min_x, i32 min_z, i32 min_altitude, i32 max_x, i32 max_z, i32 max_altitude, SpSprite **out, u32 *count) {
  u32 capacity = *count;
  min_x = ScIntMax(min_x, 0);
  min_z = ScIntMax(min_z, 0);
  if ((u32)max_x >= (actual_width << kCtMicroCoordinateShift))
    max_x = 0;
  if ((u32)max_z >= (actual_height << kCtMicroCoordinateShift))
    max_z = 0;
  *count = 0;
  for (i32 x = min_x / 256; x <= max_x / 256; ++x) {
    for (i32 z = min_z / 256; z <= max_z / 256; ++z) {
      for (SpDynamicSprite *record = cells[x][z].dynamic; record; record = record->next) {
        if (*count >= capacity)
          break;
        if (record->x >= min_x && record->x < max_x && record->z >= min_z && record->z < max_z &&
            record->altitude >= min_altitude && record->altitude < max_altitude)
          out[(*count)++] = record->sprite;
      }
      if (*count >= capacity)
        return;
    }
  }
}

// Move a dynamic sprite in place or relink it into its new cell, preserving altitude when the caller supplies minus one.
// Reindex its bounds before invalidating their union or their two disjoint rectangles, without changing art ownership.
bool SpCellMap::MoveDynamicSprite(SpSprite *sprite, i32 old_x, i32 old_z, i32 x, i32 z, i32 altitude) {
  SpDynamicSprite **link = SpFindDynamicLink(this, sprite, &old_x, &old_z);
  if (!link)
    return false;
  SpDynamicSprite *record = *link;
  ScRect previous = record->rectangle;
  bool moved_cell = old_x / 256 != x / 256 || old_z / 256 != z / 256;
  if (moved_cell)
    *link = record->next;
  record->x = x;
  record->z = z;
  if (altitude != -1)
    record->altitude = altitude;
  i32 draw_x, draw_z;
  ActualGridToDrawGridMicro(x, z, &draw_x, &draw_z);
  record->depth = DynamicDepth(record, draw_x, draw_z, (u32)record->altitude);
  if (moved_cell) {
    SpDynamicSprite **head = &cells[x / 256][z / 256].dynamic;
    record->next = *head;
    *head = record;
  }
  DynamicBounds(record, &record->rectangle);
  UpdateDynamicIndex(record, &previous, &record->rectangle);
  ScRect overlap;
  if (overlap.Intersect(&previous, &record->rectangle)) {
    ScRect united = {ScIntMin(previous.left, record->rectangle.left), ScIntMin(previous.top, record->rectangle.top),
        ScIntMax(previous.right, record->rectangle.right), ScIntMax(previous.bottom, record->rectangle.bottom)};
    AddDirtyRect(&united, false, 0);
  } else {
    AddDirtyRect(&previous, false, 0);
    AddDirtyRect(&record->rectangle, false, 0);
  }
  return true;
}

// Apply the original layer change, frame refresh or relative dirty rectangle to an existing dynamic sprite.
bool SpCellMap::ChangeDynamicSprite(SpSprite *sprite, i32 x, i32 z, u32 flags, void *payload) {
  if (changing_batch)
    return true;
  i32 column = x / 256;
  u32 row = (u32)(z / 256);
  SpDynamicSprite *record;
  for (;;) {
    record = cells[column][row].dynamic;
    while (record && record->sprite != sprite)
      record = record->next;
    if (record)
      break;
    if (!FindDynamicSprite(sprite, &x, &z, &column, &row))
      return false;
  }
  if (flags & 0x80u) {
    record->priority = *static_cast<u8 *>(payload);
    return true;
  }
  if (flags & 2u) {
    ScRect previous = record->rectangle;
    DynamicBounds(record, &record->rectangle);
    UpdateDynamicIndex(record, &previous, &record->rectangle);
    ScRect united = {ScIntMin(previous.left, record->rectangle.left), ScIntMin(previous.top, record->rectangle.top),
        ScIntMax(previous.right, record->rectangle.right), ScIntMax(previous.bottom, record->rectangle.bottom)};
    AddDirtyRect(&united, false, 0);
  } else {
    i32 draw_x, draw_z;
    ActualGridToDrawGrid(record->x / 256, record->z / 256, &draw_x, &draw_z);
    u32 depth = (u32)draw_x + (u32)draw_z;
    if (!payload)
      AddDirtyRect(&record->rectangle, false, depth);
    else {
      ScRect *offset = static_cast<ScRect *>(payload);
      ScRect rectangle = {(i32)((u32)offset->left + (u32)record->rectangle.left),
          (i32)((u32)record->rectangle.top + (u32)offset->top), (i32)((u32)record->rectangle.left + (u32)offset->right),
          (i32)((u32)record->rectangle.top + (u32)offset->bottom)};
      AddDirtyRect(&rectangle, false, depth);
    }
  }
  return true;
}

// Find the original borrowed dynamic record in its expected cell before using the full-map coordinate fallback.
// A failed fallback returns null; a successful one restarts the local chain search at the updated cell.
SpDynamicSprite *SpCellMap::FindDynamicRecord(SpSprite *sprite, i32 x, i32 z) {
  i32 column = x / 256;
  u32 row = (u32)(z / 256);
  for (;;) {
    for (SpDynamicSprite *record = cells[column][row].dynamic; record; record = record->next)
      if (record->sprite == sprite)
        return record;
    if (!FindDynamicSprite(sprite, &x, &z, &column, &row))
      return nullptr;
  }
}

// Apply the captured prefix of completed art requests while holding the recursive drawing lock.
// Reload live zoom/entries after calls and erase every queued record if the initial prefix was nonempty.
i32 SpCellMap::DrainFlags() {
  drawing_lock.Lock();
  u32 count = completed_flags.size;
  for (u32 i = 0; i < count; ++i) {
    SpFlagEntry entry = completed_flags.data[i];
    if (entry.operation != 0 && entry.operation != 2)
      continue;
    if (entry.zoom == zoom)
      ChangeSprite(entry.x, entry.z, true, entry.operation == 2 ? 8 : 16, nullptr);
    else {
      u16 mask = (u16)(16u << entry.zoom);
      if (entry.operation == 2)
        cells[entry.x][entry.z].flags |= mask;
      else
        cells[entry.x][entry.z].flags &= (u16)~mask;
    }
  }
  if (count)
    completed_flags.Clear();
  return drawing_lock.Unlock();
}

// Compute bounds and four base vertices after initialization. Apply the original translation and edge walks afterward.
void SpCellMap::RebuildGeometry() {
  if (!initialized)
    return;
  city_bounds.left = city_bounds.top = 0;
  diamond[0].x = diamond[1].y = 0;
  if (rotation == 0 || rotation == 2) {
    city_bounds.right = (i32)(((draw_width + draw_height) * cell_width) >> 1);
    city_bounds.bottom = (i32)(((draw_width + draw_height) * cell_height) >> 1);
    diamond[0].y = (i32)((draw_height * cell_height) >> 1);
    diamond[1].x = (i32)((draw_width * cell_width) >> 1);
    diamond[2].x = city_bounds.right;
    diamond[2].y = (i32)((draw_width * cell_height) >> 1);
    diamond[3].x = (i32)((draw_height * cell_width) >> 1);
    diamond[3].y = city_bounds.bottom;
  } else {
    city_bounds.right = (i32)(((draw_width + draw_height) * cell_height) >> 1);
    city_bounds.bottom = (i32)(((draw_width + draw_height) * cell_width) >> 1);
    diamond[0].y = (i32)((draw_width * cell_height) >> 1);
    diamond[1].x = (i32)((draw_height * cell_width) >> 1);
    diamond[2].x = city_bounds.bottom;
    diamond[2].y = (i32)((draw_height * cell_height) >> 1);
    diamond[3].x = (i32)((draw_width * cell_width) >> 1);
    diamond[3].y = city_bounds.right;
  }
  u32 offset_x = (draw_width - 1) * (u32)((i32)cell_width / -2);
  u32 offset_y = (u32)((i32)cell_height / -2);
  TranslateDiamond((i32)offset_x, (i32)offset_y);
  RebuildOutline();
}

// Translate the four live diamond points in their original order. The machine return retains the X offset.
i32 SpCellMap::TranslateDiamond(i32 x, i32 y) {
  for (u32 i = 0; i < 4; ++i) {
    diamond[i].x = (i32)((u32)diamond[i].x + (u32)x);
    diamond[i].y = (i32)((u32)diamond[i].y + (u32)y);
  }
  return x;
}

// Copy three diamond corners and project nine altitude-aware edge anchors in their original order.
// Both edge walks use actual_width, including rectangular maps; this preserves the source's defined behavior.
void SpCellMap::RebuildOutline() {
  outline[0] = diamond[2];
  outline[1] = diamond[3];
  outline[2] = diamond[0];
  for (u32 i = 0; i < 9; ++i) {
    u32 x = 0, z = 0;
    switch (i) {
    case 0: x = actual_width - 1; break;
    case 1: x = (3 * actual_width) >> 2; break;
    case 2: x = actual_width >> 1; break;
    case 3: x = actual_width >> 2; break;
    case 5: z = actual_width >> 2; break;
    case 6: z = actual_width >> 1; break;
    case 7: z = (3 * actual_width) >> 2; break;
    case 8: z = actual_width - 1; break;
    }
    ActualGridToCityPixel((i32)x, (i32)z, &outline[i + 3].x, &outline[i + 3].y);
  }
}

// Test the twelve-point altitude-aware outline using the original half-open edge crossings and integer division.
// Snapshot the polygon before translating a window-relative point, matching the source's temporary polygon copy.
bool SpCellMap::IsPointWithinCityOutline(i32 x, i32 y, bool window_relative) {
  ScPoint polygon[12];
  for (u32 i = 0; i < 12; ++i)
    polygon[i] = outline[i];
  if (window_relative)
    SpCellMap::WindowPixelToCityPixel_NonInline(x, y, &x, &y);
  bool inside = false;
  for (u32 i = 0, previous = 11; i < 12; previous = i++) {
    ScPoint first = polygon[i], second = polygon[previous];
    if ((first.y <= y && y < second.y) || (second.y <= y && y < first.y)) {
      i32 product = (i32)(((u32)second.x - (u32)first.x) * ((u32)y - (u32)first.y));
      i32 divisor = (i32)((u32)second.y - (u32)first.y);
      i32 crossing = (i32)((u32)first.x + (u32)(product / divisor));
      if (x < crossing)
        inside = !inside;
    }
  }
  return inside;
}

static u32 SpCellShift(f64 span) {
  return (u32)(i64)((log(span) + 0.1) / log(2.0));
}

// Select preferred dirty-grid counts for known displays, or the first even divisor from12 through24.
static void SpDirtyBlockCounts(u32 width, u32 height, u32 *columns, u32 *rows, i32 mode) {
  if (mode == 1) {
    *columns = *rows = 8;
    return;
  }
  if (mode != 0)
    return;
  switch (width) {
  case 640:
    *columns = 20;
    break;
  case 800:
  case 1280:
    *columns = 40;
    break;
  case 1024:
    *columns = 32;
    break;
  case 1152:
    *columns = 36;
    break;
  default:
    *columns = 12;
    while (*columns <= 24 && width % *columns)
      *columns += 2;
    if (*columns > 24)
      *columns = 8;
    break;
  }
  switch (height) {
  case 480:
    *rows = 20;
    break;
  case 600:
    *rows = 30;
    break;
  case 768:
    *rows = 32;
    break;
  case 864:
    *rows = 36;
    break;
  case 1024:
    *rows = 64;
    break;
  default:
    *rows = 12;
    while (*rows <= 24 && height % *rows)
      *rows += 2;
    if (*rows > 24)
      *rows = 8;
    break;
  }
}

// Reallocate and clear the dirty-byte grid, then derive its pixel spans from the live viewport.
u32 SpCellMap::AllocateDirtyGrid(u32 columns, u32 rows) {
  delete[] dirty_mask;
  dirty_mask = new u8[(usize)columns * rows]();
  dirty_width = columns;
  dirty_height = rows;
  dirty_shift_x = SpCellShift(columns);
  dirty_shift_y = SpCellShift(rows);
  dirty_cell_width = ((u32)viewport.right - (u32)viewport.left) / columns;
  dirty_cell_height = ((u32)viewport.bottom - (u32)viewport.top) / rows;
  return dirty_cell_height;
}

// Project containers replace allocator machinery; actual cells and cached sprite extents are not created here.
SpCellMap::SpCellMap() {
  initialized = 0;
  references = 0;
  background_color = 0;
  actual_width = actual_height = draw_width = draw_height = 0;
  cells = nullptr;
  zoom = 1;
  rotation = 0;
  cell_width = 16;
  altitude_scale = 2;
  cell_height = 8;
  shift_x = SpCellShift(cell_width);
  shift_y = SpCellShift(cell_height);
  shift_altitude = SpCellShift(altitude_scale);
  half_shift_x = shift_x - 1;
  half_shift_y = shift_y - 1;
  half_shift_altitude = shift_altitude - 1;
  viewport = world_clip = {0, 0, 640, 480};
  city_buffer = background_buffer = nullptr;
  pending = 0;
  city_bounds = {0, 0, 1, 1};
  registered_count = fallback_art_references = 0;
  minimum_altitude = 255;
  maximum_altitude = 0;
  maximum_span = 255;
  maximum_count = 0;
  alpha_blend_count = 0;
  copy_index = 0;
  changed = 0;
  following = 0;
  immediate_redraw = 0;
  copy_extra_rects = 0;
  visible_outer = visible_inner = {0, 0, 1, 1};
  inverse_bucket_width = inverse_bucket_height = 1.0f;
  drawing_ready = 0;
  follow_target = nullptr;
  spatial_enabled = 1;
  performance_info = nullptr;
  dirty_mask = nullptr;
  dirty_width = dirty_height = 8;
  dirty_shift_x = dirty_shift_y = 0;
  dirty_cell_width = dirty_cell_height = 0;
  dirty_mask_enabled = 0;
  spatial_buckets = nullptr;
  spatial_width = spatial_height = 8;
  spatial_shift = spatial_height_shift = 0;
  bucket_width = bucket_height = 0;
  dynamic_bucket_count = 769;
  dynamic_buckets = new SpDynamicNode *[dynamic_bucket_count]();
  changing_batch = loader_initialized = 0;
  threaded = 1;
  work_mode = 0;
  stop_loader = 0;
  load_budget = 5.0f;
  loader_x = loader_z = loader_center_x = loader_center_z = 0;
  loader_bounds = {0, 0, 0, 0};
  loader_zoom = loader_rotation = 0;
  art_sequence = 0;
  next_overlay_id = 0;
  frame_count = 0;
  back_buffer = nullptr;
  preload = 0;
  draw_total = 0;
  memset(altitude_counts, 0, sizeof(altitude_counts));
  AllocateDirtyGrid(dirty_width, dirty_height);
  ConfigureSpatialGrid(spatial_width, spatial_height, 0);
  for (u32 i = 0; i < 5; ++i)
    residency[i] = 0;
  for (u32 i = 1; i <= 5; ++i)
    fallback[i] = nullptr;
  for (u32 i = 0; i < 12; ++i)
    outline[i] = {0, 0};
  performance_info = SpGetPerformanceInfo();
}

// Publish the common or cell-map identity and acquire its reference through virtual dispatch. A rejected interface
// clears the output.
bool SpCellMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpCellMap);
  HANDLE_QUERY_INTERFACE(IID_SpCellMap, SpCellMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpCellMap::AddRef() {
  return ++references;
}

// Destroy the map at its last reference without first decrementing the live count.
u32 SpCellMap::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Free the two grid arrays before source shutdown, then release each overlay's source-buffer hold.
// Native container destruction also frees trivial records that the original slab containers owned.
SpCellMap::~SpCellMap() {
  delete[] dirty_mask;
  dirty_mask = nullptr;
  delete[] spatial_buckets;
  spatial_buckets = nullptr;
  SpCellMap::Shutdown();
  for (u32 i = 0; i < overlays.size; ++i)
    overlays.data[i].buffer->Release();
  for (u32 i = 0; i < dynamic_bucket_count; ++i) {
    SpDynamicNode *node = dynamic_buckets[i];
    while (node) {
      SpDynamicNode *next = node->next;
      delete node->value;
      delete node;
      node = next;
    }
  }
  delete[] dynamic_buckets;
}

static bool SpRestoreCitySurface(void *first, void *) {
  static_cast<SpCellMap *>(first)->changed = 1;
  return true;
}

// Create the city surface and actual-cell table after configuring the camera, dirty grid and background loader.
// Probe requested video-memory self-blits, then retain the original system-memory fallback and restore callback.
bool SpCellMap::Init(u32 width, u32 height, u32 screen_width, u32 screen_height, i32 origin_x, i32 origin_y,
    i32 requested_zoom, i32 requested_rotation, bool use_video_memory) {
  if (initialized)
    Shutdown();
  if (threaded && !loader_initialized) {
    for (u32 i = 0; i < 5; ++i) {
      SpGenericBuilding *building = new SpGenericBuilding;
      fallback[i + 1] = building;
      building->Init((i32)i, 0);
    }
    StartLoader();
    loader_bounds = {0, 0, (i32)(width - 1), (i32)(height - 1)};
    loader_spiral.bounds = loader_bounds;
    loader_initialized = 1;
  }
  actual_width = width;
  actual_height = height;
  if (requested_zoom >= 4) {
    origin_x &= ~1;
    origin_y &= ~1;
  }
  viewport = {origin_x, origin_y, (i32)screen_width, (i32)screen_height};
  world_clip = viewport;
  SetRotate(requested_rotation, false, INT32_MAX, INT32_MAX);
  SetZoom(requested_zoom, false, INT32_MAX, INT32_MAX);
  u32 columns, rows;
  dirty_width = dirty_height = UINT32_MAX;
  SpDirtyBlockCounts(screen_width, screen_height, &dirty_width, &dirty_height, 0);
  SpDirtyBlockCounts(screen_width, screen_height, &columns, &rows, 1);
  AllocateDirtyGrid(dirty_width, dirty_height);
  ConfigureSpatialGrid(columns, rows, 0);
  drawing_ready = 0;
  SpGetGraphics()->CreateBuffer(&city_buffer);
  bool ready = false;
  if (use_video_memory)
    ready = city_buffer->Init(screen_width, screen_height, GxColorType(GxColorType::Color::RGB16), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::Local, false);
  if (ready) {
    ready = false;
    if (city_buffer->Lock(GxLockFlags::Blt)) {
      ScRect source = {0, 10, (i32)screen_width, (i32)screen_height};
      ScRect destination = {0, 0, (i32)screen_width, (i32)(screen_height - 10)};
      ready = city_buffer->Blt(city_buffer, source, destination, nullptr) != 0;
      if (ready) {
        source = {0, 0, (i32)(screen_width - 10), (i32)screen_height};
        destination = {10, 0, (i32)screen_width, (i32)screen_height};
        ready = city_buffer->Blt(city_buffer, source, destination, nullptr) != 0;
      }
      city_buffer->Unlock(GxLockFlags::Blt);
      if (!ready)
        city_buffer->Shutdown();
    }
  }
  if (!ready)
    city_buffer->Init(screen_width, screen_height, GxColorType(GxColorType::Color::RGB16), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  city_buffer->SetSurfaceRestoreFunction(SpRestoreCitySurface, this, nullptr);
  if (city_buffer->Lock(GxLockFlags::Fill)) {
    city_buffer->Fill(0, nullptr);
    city_buffer->Unlock(GxLockFlags::Fill);
  }
  background_color = city_buffer->ConvertRGBValueToNative16Bit(80, 88, 111);
  ScRect full = {0, 0, (i32)screen_width, (i32)screen_height};
  FillBackground(&full, false);
  actual_width = draw_width = width;
  actual_height = draw_height = height;
  cells = new SpCell *[actual_width];
  for (u32 x = 0; x < actual_width; ++x)
    cells[x] = new SpCell[actual_height];
  for (u32 x = 0; x < actual_width; ++x)
    for (u32 z = 0; z < actual_height; ++z) {
      SpCell *cell = &cells[x][z];
      cell->altitude = cell->size = cell->root_x = cell->root_z = 0;
      cell->sprite = nullptr;
      cell->flags = 0;
      cell->dynamic = nullptr;
    }
  minimum_altitude = 255;
  maximum_altitude = 0;
  registered_count = 0;
  memset(altitude_counts, 0, sizeof(altitude_counts));
  maximum_span = cell_height;
  maximum_count = 0;
  copy_index = 0;
  copy_rects[0].Clear();
  copy_rects[1].Clear();
  pending = initialized = 1;
  RebuildGeometry();
  return true;
}

// Pick the original draw cell, returning cell-aligned micro coordinates for artwork selection.
// Otherwise invert the pixel point after a successful terrain-altitude correction, preserving the original hit result.
bool SpCellMap::CityPixelToMicroDrawGrid(i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork) {
  i32 draw_x, draw_z;
  bool hit = PickDrawGrid(x, y, &draw_x, &draw_z, artwork, nullptr, nullptr);
  if (artwork) {
    *out_x = i32(u32(draw_x) << kCtMicroCoordinateShift);
    *out_z = i32(u32(draw_z) << kCtMicroCoordinateShift);
  } else {
    i32 actual_x, actual_z;
    DrawGridToActualGrid(draw_x, draw_z, &actual_x, &actual_z);
    if (hit)
      y = i32(u32(y) + (u32(cells[actual_x][actual_z].altitude) << (shift_altitude & 31u)));
    CityPixelToMicroDrawGridNoAltitude(x, y, out_x, out_z);
  }
  return hit;
}

// Test the original four terrain-polygon edges with half-open vertical crossings and wrapped DWORD products.
// This is the arithmetic used by the source's temporary polygon, with its unused shape metadata omitted.
static bool SpTerrainPixel(i32 x, i32 y, u32 width, u32 height) {
  ScPoint polygon[] = {{-1, (i32)(height / 2)}, {(i32)(width / 2 - 1), 1},
      {(i32)(width + 1), (i32)(height / 2)}, {(i32)(width / 2), (i32)(height + 1)}};
  bool inside = false;
  for (u32 i = 0, previous = 3; i < 4; previous = i++) {
    ScPoint first = polygon[i], second = polygon[previous];
    if ((first.y <= y && y < second.y) || (second.y <= y && y < first.y)) {
      i32 product = (i32)(((u32)second.x - (u32)first.x) * ((u32)y - (u32)first.y));
      i32 crossing = (i32)((u32)first.x + (u32)(product / (i32)((u32)second.y - (u32)first.y)));
      if (x < crossing)
        inside = !inside;
    }
  }
  return inside;
}

// Walk draw-grid diagonals behind a city pixel, retaining the last solid static or nearer dynamic hit.
bool SpCellMap::PickDrawGrid(
    i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter) {
  CityPixelToDrawGridNoAltitude(x, y, out_x, out_z);
  i32 first_x = *out_x;
  if (first_x >= (i32)actual_width)
    return false;
  i32 first_z = *out_z;
  if (first_z >= (i32)actual_height)
    return false;
  i32 draw_x = first_x, draw_z = first_z;
  u32 phase = (CellPart(x, y, first_x, first_z) & 1u) == 0;
  *out_x = *out_z = -1;
  u32 dynamic_depth = UINT32_MAX;
  if (artwork && object) {
    object->sprite = nullptr;
    for (SpSpatialNode *node = GetSpatialBucket(x, y); node; node = node->next)
      if (node->primary == 1) {
        SpDynamicSprite *record = node->sprite;
        ScRect rectangle;
        DynamicBounds(record, &rectangle);
        if (x >= rectangle.left && x < rectangle.right && y >= rectangle.top && y < rectangle.bottom &&
            record->sprite && record->sprite->IsPixelOpaque((i32)((u32)x - (u32)rectangle.left),
                (i32)((u32)y - (u32)rectangle.top), zoom, rotation) && !filter) {
          *out_x = record->x / 256;
          *out_z = record->z / 256;
          object->x = record->x;
          object->z = record->z;
          object->altitude = record->altitude;
          object->dynamic = 1;
          object->sprite = record->sprite;
          dynamic_depth = record->depth;
        }
      }
  }
  for (;;) {
    if (draw_x >= 0 && draw_z >= 0) {
      if (draw_x >= (i32)actual_width || draw_z >= (i32)actual_height)
        break;
      i32 actual_x, actual_z;
      DrawGridToActualGrid(draw_x, draw_z, &actual_x, &actual_z);
      SpCell *cell = &cells[actual_x][actual_z], *root = &cells[cell->root_x][cell->root_z];
      u8 altitude = cell->altitude;
      SpSprite *registered = cell->sprite;
      SpSprite *sprite = root->flags & (16u << zoom) ? registered : fallback[root->size];
      if (sprite && (artwork || (cell->flags & 1u))) {
        i32 top = cell->top, bottom = cell->bottom, registration_x, registration_z;
        u32 width = (u32)cell->size << ((zoom + 3) & 31);
        GetDrawRegistration(actual_x, actual_z, &registration_x, &registration_z);
        i32 left = (i32)(((u32)registration_z - (u32)registration_x) << (half_shift_x & 31u));
        u32 baseline = (((u32)registration_x + (u32)registration_z) << (half_shift_y & 31u)) -
            ((u32)altitude << (shift_altitude & 31u));
        i32 upper = (i32)(baseline - (u32)top), lower = (i32)(baseline + (u32)bottom);
        if (x >= left && y >= upper && x < (i32)((u32)left + width) && y < lower &&
            sprite->IsPixelOpaque((i32)((u32)x - (u32)left), (i32)((u32)y - (u32)upper), zoom, rotation) &&
            !filter) {
          if (artwork) {
            if (object && object->dynamic) {
              u32 altitude_micro = (u32)altitude << kCtMicroCoordinateShift;
              u32 depth = kSpCellDepthOrigin - (((u32)draw_x + (u32)draw_z) << 14) - (altitude_micro >> 2);
              if (depth < dynamic_depth) {
                object->x = (i32)((u32)actual_x << kCtMicroCoordinateShift);
                object->z = (i32)((u32)actual_z << kCtMicroCoordinateShift);
                object->dynamic = 0;
                object->altitude = (i32)altitude_micro;
                object->sprite = registered;
              }
            } else {
              *out_x = draw_x;
              *out_z = draw_z;
              if (object) {
                object->x = (i32)((u32)actual_x << kCtMicroCoordinateShift);
                object->z = (i32)((u32)actual_z << kCtMicroCoordinateShift);
                object->dynamic = 0;
                object->altitude = (i32)((u32)altitude << kCtMicroCoordinateShift);
                object->sprite = registered;
              }
            }
          } else {
            *out_x = draw_x;
            *out_z = draw_z;
          }
        }
      } else if (sprite) {
        i32 left = (i32)(((u32)draw_z - (u32)draw_x) << (half_shift_x & 31u));
        u32 baseline = ((u32)draw_x + (u32)draw_z) << (half_shift_y & 31u);
        i32 local_y = (i32)((u32)y + cell_height / 2 + ((u32)altitude << (shift_altitude & 31u)) - baseline);
        if (SpTerrainPixel((i32)((u32)x - (u32)left), local_y, cell_width, cell_height) && !filter) {
          *out_x = draw_x;
          *out_z = draw_z;
        }
      }
    }
    phase ^= 1u;
    if (phase)
      draw_z = (i32)((u32)draw_z + 1u);
    else
      draw_x = (i32)((u32)draw_x + 1u);
  }
  if (*out_x != -1)
    return true;
  if (first_x < 0 || first_z < 0) {
    *out_x = first_x;
    *out_z = first_z;
  } else {
    *out_x = draw_x;
    *out_z = draw_z;
  }
  return false;
}

// Rotate the draw-grid hit into actual coordinates even when picking misses. Artwork hits then publish footprint
// root bytes in the original ordered stores, including the second lookup through the already-written X output.
bool SpCellMap::PickActualGrid(
    i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter) {
  i32 draw_x, draw_z;
  bool found = PickDrawGrid(x, y, &draw_x, &draw_z, artwork, object, filter);
  DrawGridToActualGrid(draw_x, draw_z, out_x, out_z);
  if (found && artwork) {
    *out_x = cells[*out_x][*out_z].root_x;
    *out_z = cells[*out_x][*out_z].root_z;
  }
  return found;
}

bool SpCellMap::CityPixelToDrawGrid_NonInline(
    i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter) {
  return PickDrawGrid(x, y, out_x, out_z, artwork, object, filter);
}

bool SpCellMap::CityPixelToActualGrid_NonInline(
    i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter) {
  return PickActualGrid(x, y, out_x, out_z, artwork, object, filter);
}

// Translate window pixels by the current viewport origin and pick in actual-grid coordinates.
// Preserve the helper's source result rather than adding bounds or output-validity status conventions.
bool SpCellMap::WindowPixelToActualGrid_NonInline(
    i32 x, i32 y, i32 *out_x, i32 *out_z, bool artwork, SpPickObject *object, OcTest *filter) {
  SpCellMap::WindowPixelToCityPixel_NonInline(x, y, &x, &y);
  return PickActualGrid(x, y, out_x, out_z, artwork, object, filter);
}

// Clamp actual-grid outputs in their original X-before-Z order. The private entry returns the supplied
// second output pointer; zero dimensions retain the source's final minus-one result.
i32 *SpCellMap::ClampActualGrid(i32 *x, i32 *z) {
  if (*x < 0)
    *x = 0;
  if (*x >= (i32)actual_width)
    *x = (i32)(actual_width - 1);
  if (*z < 0)
    *z = 0;
  if (*z >= (i32)actual_height)
    *z = (i32)(actual_height - 1);
  return z;
}

// Compute the actual-coordinate extrema of four points with the original maximum's minus-INT_MAX seed.
// The input points are already rotated; this helper adds no projection, clipping or temporary game interface.
static ScRect SpCornerBounds(const ScPoint *points) {
  ScRect bounds = {points[0].x, points[0].y,
      ScIntMax(points[0].x, -INT32_MAX), ScIntMax(points[0].y, -INT32_MAX)};
  for (u32 i = 1; i < 4; ++i) {
    bounds.left = ScIntMin(bounds.left, points[i].x);
    bounds.top = ScIntMin(bounds.top, points[i].y);
    bounds.right = ScIntMax(bounds.right, points[i].x);
    bounds.bottom = ScIntMax(bounds.bottom, points[i].y);
  }
  return bounds;
}

// Lazily pick all four clipped window corners and cache both outer and edge-midpoint actual-grid rectangles.
// Missing terrain picks use the minimum-altitude projection; null outputs are explicitly allowed by the source.
void SpCellMap::GetVisibleAreaRectangle(ScRect *inner, ScRect *outer) {
  if (!drawing_ready) {
    ScRect clip;
    GetViewClipRect(&clip);
    drawing_ready = 1;
    ScPoint corners[4];
    i32 pixel_x[] = {clip.left, clip.right, clip.left, clip.right};
    i32 pixel_y[] = {clip.top, clip.top, clip.bottom, clip.bottom};
    for (u32 i = 0; i < 4; ++i) {
      i32 x, y;
      SpCellMap::WindowPixelToCityPixel_NonInline(pixel_x[i], pixel_y[i], &x, &y);
      if (!PickDrawGrid(x, y, &corners[i].x, &corners[i].y, false, nullptr, nullptr))
        CityPixelToDrawGridNoAltitude(x, (i32)((u32)y + ((u32)minimum_altitude << (shift_altitude & 31u))),
            &corners[i].x, &corners[i].y);
    }
    i32 top_x = (i32)((u32)corners[1].x + (u32)corners[0].x) / 2;
    i32 top_z = (i32)((u32)corners[1].y + (u32)corners[0].y) / 2;
    i32 bottom_x = (i32)((u32)corners[3].x + (u32)corners[2].x) / 2;
    i32 bottom_z = (i32)((u32)corners[3].y + (u32)corners[2].y) / 2;
    ScPoint centers[] = {{top_x, top_z}, {top_x, bottom_z}, {bottom_x, top_z}, {bottom_x, bottom_z}};
    for (u32 i = 0; i < 4; ++i)
      DrawGridToActualGrid(centers[i].x, centers[i].y, &centers[i].x, &centers[i].y);
    visible_inner = SpCornerBounds(centers);
    ClampActualGrid(&visible_inner.left, &visible_inner.top);
    ClampActualGrid(&visible_inner.right, &visible_inner.bottom);
    for (u32 i = 0; i < 4; ++i)
      DrawGridToActualGrid(corners[i].x, corners[i].y, &corners[i].x, &corners[i].y);
    visible_outer = SpCornerBounds(corners);
    ClampActualGrid(&visible_outer.left, &visible_outer.top);
    ClampActualGrid(&visible_outer.right, &visible_outer.bottom);
  }
  if (inner) {
    inner->left = visible_inner.left;
    inner->top = visible_inner.top;
    inner->right = visible_inner.right;
    inner->bottom = visible_inner.bottom;
  }
  if (outer) {
    outer->left = visible_outer.left;
    outer->top = visible_outer.top;
    outer->right = visible_outer.right;
    outer->bottom = visible_outer.bottom;
  }
}

// Build an unclipped actual-grid diamond around the visible area. Tight mode starts from the inset rotated bounds;
// the other mode uses the full viewport width as its fixed draw-grid radius, and the third argument is unused.
bool SpCellMap::GetCellsForViewCornersUnclipped(ScPoint *corners, bool tight, ScRect *) {
  ScRect inner, outer;
  GetVisibleAreaRectangle(&inner, &outer);
  if (tight) {
    inner.left = (i32)((u32)inner.left + 1u);
    inner.right = (i32)((u32)inner.right - 1u);
    inner.bottom = (i32)((u32)inner.bottom - 1u);
    inner.top = (i32)((u32)inner.top + 1u);
    corners[1].x = inner.right;
    corners[0].y = inner.top;
    corners[1].y = inner.top;
    corners[3].x = inner.left;
    corners[2].x = inner.right;
    corners[3].y = inner.bottom;
    corners[0].x = inner.left;
    corners[2].y = inner.bottom;
    ScPoint rotated[4];
    for (u32 i = 0; i < 4; ++i)
      ActualGridToDrawGrid(corners[i].x, corners[i].y, &rotated[i].x, &rotated[i].y);
    ScRect bounds = SpCornerBounds(rotated);
    corners[0].x = bounds.left;
    corners[0].y = bounds.top;
    corners[2].x = bounds.right;
    corners[2].y = bounds.bottom;
    corners[1].x = bounds.right;
    corners[1].y = bounds.top;
    corners[3].x = bounds.left;
    corners[3].y = bounds.bottom;
    i32 center_x = (i32)((u32)bounds.left + (u32)bounds.right) / 2;
    i32 center_z = (i32)((u32)bounds.bottom + (u32)bounds.top) / 2;
    i32 half_x = (i32)((u32)bounds.right - (u32)bounds.left) / 2;
    i32 half_z = (i32)((u32)bounds.bottom - (u32)bounds.top) / 2;
    DrawGridToActualGrid(center_x, (i32)((u32)bounds.top - (u32)half_x), &corners[0].x, &corners[0].y);
    DrawGridToActualGrid((i32)((u32)bounds.right + (u32)half_z), center_z, &corners[1].x, &corners[1].y);
    DrawGridToActualGrid(center_x, (i32)((u32)bounds.bottom + (u32)half_x), &corners[2].x, &corners[2].y);
    DrawGridToActualGrid((i32)((u32)bounds.left - (u32)half_z), center_z, &corners[3].x, &corners[3].y);
  } else {
    i32 radius = (i32)((u32)world_clip.right - (u32)world_clip.left) >> (shift_x & 31u);
    i32 center_x = (i32)((u32)inner.right + (u32)inner.left) / 2;
    i32 center_z = (i32)((u32)inner.bottom + (u32)inner.top) / 2;
    ActualGridToDrawGrid(center_x, center_z, &center_x, &center_z);
    DrawGridToActualGrid(center_x, (i32)((u32)center_z - (u32)radius), &corners[0].x, &corners[0].y);
    DrawGridToActualGrid((i32)((u32)center_x - (u32)radius), center_z, &corners[1].x, &corners[1].y);
    DrawGridToActualGrid(center_x, (i32)((u32)center_z + (u32)radius), &corners[2].x, &corners[2].y);
    DrawGridToActualGrid((i32)((u32)center_x + (u32)radius), center_z, &corners[3].x, &corners[3].y);
  }
  return true;
}

SpPickItem::SpPickItem()
    : location{INT32_MAX, INT32_MAX, INT32_MAX},
      area{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, flags{0} {
}

static bool SpPickDistance(const SpPickItem &first, const SpPickItem &second) {
  return first.distance < second.distance;
}

// Fill a point selection's actual payload without retaining its borrowed sprite. Both screen rectangle endpoints
// equal the requested pixel; callers supply the original micro location, volume, geometry flags and depth.
static void SpSetPickItem(SpPickItem *item, i32 x, i32 y, const CtCoord &location, const GeBounds &area,
    i32 distance, ScGeomFlag flags, u8 aligned, SpSprite *sprite) {
  item->rectangle = {x, y, x, y};
  item->location = location;
  item->area = area;
  item->distance = distance;
  item->flags = flags;
  item->aligned = aligned;
  item->occupant = nullptr;
  item->sprite = sprite;
}

// Collect geometry-filtered dynamic and static hits at a viewport point, then publish nearest selections first. The
// original STL vector has a three-pointer header and 72-byte records; append/growth are at 100105BF/10011823.
bool SpCellMap::DoPick(SpPick *pick, bool artwork) {
  if (pick->GetSelectionInputType() != SpPickInput::ViewportPoint)
    return false;
  SpPickItem input;
  if (!pick->GetSelectionInput(&input))
    return false;
  i32 window_x = input.rectangle.left, window_y = input.rectangle.top;
  pick->GetMaxSelectionCount();
  ScGeomFilter *filter = pick->GetSelectionTest();
  std::vector<SpPickItem> hits;
  i32 pixel_x, pixel_y, draw_x, draw_z;
  SpCellMap::WindowPixelToCityPixel_NonInline(window_x, window_y, &pixel_x, &pixel_y);
  CityPixelToDrawGridNoAltitude(pixel_x, pixel_y, &draw_x, &draw_z);
  if (draw_x >= (i32)actual_width || draw_z >= (i32)actual_height) {
    SpPickItem fallback_item;
    CtCoord location = {
        (i32)((u32)draw_x << kCtMicroCoordinateShift), (i32)((u32)draw_z << kCtMicroCoordinateShift), 0};
    GeBounds area = {location, location};
    SpSetPickItem(&fallback_item, window_x, window_y, location, area,
        (i32)((0x10000u - (u32)draw_x - (u32)draw_z) << 14), {1}, 1, nullptr);
    pick->AddSelection(fallback_item, UINT32_MAX);
    return false;
  }
  i32 first_x = draw_x, first_z = draw_z, hit_x = -1, hit_z = -1;
  u32 phase = (CellPart(pixel_x, pixel_y, draw_x, draw_z) & 1u) == 0;
  for (SpSpatialNode *node = GetSpatialBucket(pixel_x, pixel_y); node; node = node->next)
    if (node->primary == 1) {
      SpDynamicSprite *record = node->sprite;
      ScRect rectangle;
      DynamicBounds(record, &rectangle);
      SpSprite *sprite = record->sprite;
      if (pixel_x >= rectangle.left && pixel_x < rectangle.right && pixel_y >= rectangle.top &&
          pixel_y < rectangle.bottom && sprite && sprite->IsPixelOpaque((i32)((u32)pixel_x - (u32)rectangle.left),
              (i32)((u32)pixel_y - (u32)rectangle.top), zoom, rotation)) {
        ScGeomFlag flags = {node->sprite->sprite->GetGeomFlag()};
        if (!filter || filter->Test(&flags)) {
          SpDynamicSprite *hit = node->sprite;
          hit_x = hit->x / 256;
          hit_z = hit->z / 256;
          CtCoord location = {hit->x, hit->z, hit->altitude};
          GeBounds area = {location, location};
          SpSetPickItem(&hits.emplace_back(), window_x, window_y, location, area, (i32)hit->depth, flags, 0, hit->sprite);
        }
      }
    }
  for (;;) {
    if (draw_x >= 0 && draw_z >= 0) {
      if (draw_x >= (i32)actual_width || draw_z >= (i32)actual_height)
        break;
      i32 actual_x, actual_z;
      DrawGridToActualGrid(draw_x, draw_z, &actual_x, &actual_z);
      SpCell *cell = &cells[actual_x][actual_z], *root = &cells[cell->root_x][cell->root_z];
      u8 altitude = cell->altitude;
      SpSprite *registered = cell->sprite;
      SpSprite *sprite = root->flags & (16u << zoom) ? registered : fallback[root->size];
      if (!sprite)
        continue;
      bool solid;
      if (artwork || (cell->flags & 1u)) {
        i32 top = cell->top, bottom = cell->bottom, registration_x, registration_z;
        u32 width = (u32)cell->size << ((zoom + 3) & 31);
        GetDrawRegistration(actual_x, actual_z, &registration_x, &registration_z);
        i32 left = (i32)(((u32)registration_z - (u32)registration_x) << (half_shift_x & 31u));
        u32 baseline = (((u32)registration_x + (u32)registration_z) << (half_shift_y & 31u)) -
            ((u32)altitude << (shift_altitude & 31u));
        i32 upper = (i32)(baseline - (u32)top), lower = (i32)(baseline + (u32)bottom);
        solid = pixel_x >= left && pixel_y >= upper && pixel_x < (i32)((u32)left + width) && pixel_y < lower &&
            sprite->IsPixelOpaque((i32)((u32)pixel_x - (u32)left), (i32)((u32)pixel_y - (u32)upper), zoom, rotation);
      } else {
        i32 left = (i32)(((u32)draw_z - (u32)draw_x) << (half_shift_x & 31u));
        u32 baseline = ((u32)draw_x + (u32)draw_z) << (half_shift_y & 31u);
        i32 local_y = (i32)((u32)pixel_y + cell_height / 2 + ((u32)altitude << (shift_altitude & 31u)) - baseline);
        solid = SpTerrainPixel((i32)((u32)pixel_x - (u32)left), local_y, cell_width, cell_height);
      }
      if (solid) {
        ScGeomFlag flags = {registered->GetGeomFlag()};
        if (!filter || filter->Test(&flags)) {
          hit_x = draw_x;
          hit_z = draw_z;
          CtCoord location;
          GeBounds area;
          if (artwork) {
            location = {(i32)(((u32)root->root_x - root->size + 1u) << kCtMicroCoordinateShift),
                (i32)((u32)root->root_z << kCtMicroCoordinateShift), root->altitude};
            area = {location,
                {(i32)((u32)location.x + ((u32)root->size << kCtMicroCoordinateShift)),
                    (i32)((u32)location.z + ((u32)root->size << kCtMicroCoordinateShift)), root->altitude}};
          } else {
            location = {(i32)((u32)draw_x << kCtMicroCoordinateShift), (i32)((u32)draw_z << kCtMicroCoordinateShift),
                (i32)((u32)cell->altitude << kCtMicroCoordinateShift)};
            area = {location, location};
          }
          i32 distance =
              (i32)(((0x10000u - (u32)draw_z - (u32)draw_x) << 14) - (((u32)altitude << kCtMicroCoordinateShift) >> 2));
          SpSetPickItem(&hits.emplace_back(), window_x, window_y, location, area, distance, flags, 1, registered);
        }
      }
    }
    phase ^= 1u;
    if (phase)
      draw_z = (i32)((u32)draw_z + 1u);
    else
      draw_x = (i32)((u32)draw_x + 1u);
  }
  if (hit_x != -1) {
    std::sort(hits.begin(), hits.end(), SpPickDistance);
    u32 maximum = pick->GetMaxSelectionCount();
    u32 count = UintMin(static_cast<u32>(hits.size()), maximum);
    for (u32 i = count; i; --i)
      pick->AddSelection(hits[i - 1], UINT32_MAX);
    return false;
  }
  if (first_x < 0 || first_z < 0) {
    draw_x = first_x;
    draw_z = first_z;
  }
  SpPickItem fallback_item;
  CtCoord location = {(i32)((u32)draw_x << kCtMicroCoordinateShift), (i32)((u32)draw_z << kCtMicroCoordinateShift), 0};
  GeBounds area = {location, location};
  SpSetPickItem(&fallback_item, window_x, window_y, location, area,
      (i32)((0x10000u - (u32)draw_x - (u32)draw_z) << 14), {1}, 1, nullptr);
  pick->AddSelection(fallback_item, UINT32_MAX);
  return false;
}

// Acquire the sprite's art and register its square footprint, retaining the instance only after successful placement.
bool SpCellMap::Insert(SpSprite *sprite, u32 x, u32 z, u8 altitude, u8 size, bool invalidate, u32 flags) {
  u32 extent = (u32)size - 1u;
  if (threaded) {
    drawing_lock.Lock();
    DrainFlags();
    for (i32 current = 0; current < 5; ++current) {
      fallback[size]->DemandArt(current, rotation, 0);
      ++fallback_art_references;
    }
  }
  if (sprite) {
    u32 last_x = x + extent;
    SpCell *root = &cells[last_x][z];
    root->sprite = sprite;
    root->size = size;
    root->flags = 0;
    root->top = 0;
    root->bottom = 0;
    root->altitude = altitude;
    root->root_z = (u8)z;
    root->root_x = (u8)last_x;
    u8 root_x = root->root_x;
    if (DemandSpriteArt(sprite, zoom, rotation, last_x, z)) {
      ScRect registration;
      sprite->GetRegistration(zoom, rotation, &registration);
      u32 span = (u32)registration.top + (u32)registration.bottom;
      for (u32 cell_x = x; cell_x <= last_x; ++cell_x) {
        for (u32 cell_z = z; cell_z <= z + extent; ++cell_z) {
          SpCell *cell = &cells[cell_x][cell_z];
          cell->sprite = sprite;
          cell->size = size;
          cell->altitude = altitude;
          cell->flags |= (u16)(flags & 15u);
          cell->root_x = root_x;
          cell->root_z = (u8)z;
          cell->top = (i16)registration.top;
          cell->bottom = (i16)registration.bottom;
          if (size >= 2)
            cell->flags |= 2u;
        }
      }
      cells[x][z].flags |= 0x2000u;
      cells[last_x][z].flags |= 0x4000u;
      cells[last_x][z + extent].flags |= 0x0800u;
      cells[x][z + extent].flags |= 0x1000u;
      ++registered_count;
      ++altitude_counts[altitude];
      if (altitude < minimum_altitude)
        minimum_altitude = altitude;
      if (altitude > maximum_altitude)
        maximum_altitude = altitude;
      if (span == maximum_span)
        ++maximum_count;
      else if (span > maximum_span) {
        maximum_span = span;
        maximum_count = 1;
      }
      if ((u32)zoom >= 3) {
        SpCell *cell = &cells[last_x][z];
        ScRect rectangle;
        GetCityPixelRectForSprite(cell->root_x, cell->root_z, &rectangle);
        if (rectangle.Intersect(&rectangle, &viewport))
          IndexStaticCell(cell, &rectangle, 1);
      }
      if (invalidate)
        ChangeSprite(last_x, z, true, 34, nullptr);
      sprite->AddRef();
      if (threaded)
        drawing_lock.Unlock();
      return true;
    }
  }
  if (threaded)
    drawing_lock.Unlock();
  return false;
}

// Remove the selected cell, releasing art and the retained instance only when its original root flag is set.
// Preserve invalidation before release, live callback state, altitude counts and the pre-clear registration refresh.
bool SpCellMap::Remove(u32 x, u32 z, bool invalidate) {
  if (threaded) {
    drawing_lock.Lock();
    DrainFlags();
  }
  if (!InCellBounds((i32)x, (i32)z)) {
    if (threaded)
      drawing_lock.Unlock();
    return false;
  }
  SpCell *cell = &cells[x][z];
  if (cell->flags & 0x4000u) {
    if ((u32)zoom >= 3) {
      ScRect rectangle;
      GetCityPixelRectForSprite(cell->root_x, cell->root_z, &rectangle);
      if (rectangle.Intersect(&rectangle, &viewport))
        IndexStaticCell(cell, &rectangle, 0);
    }
    if (invalidate)
      ChangeSprite(x, z, true, 66, nullptr);
    u32 span = (u32)((i32)cell->top + cell->bottom);
    bool release_all = false;
    if (threaded) {
      for (i32 current = 0; current < 5; ++current) {
        fallback[cell->size]->ReleaseArt(current, rotation);
        --fallback_art_references;
      }
      release_all = threaded != 0;
    }
    if (release_all) {
      for (i32 current = 0; current < 5; ++current)
        if (cells[x][z].flags & (16u << current))
          ReleaseSpriteArt(cells[x][z].sprite, current, rotation, x, z);
    } else
      ReleaseSpriteArt(cell->sprite, zoom, rotation, x, z);
    if (cell->sprite)
      cell->sprite->Release();
    u8 altitude = cell->altitude;
    --registered_count;
    --altitude_counts[altitude];
    u8 minimum = minimum_altitude;
    if (altitude == minimum && !altitude_counts[minimum]) {
      while (minimum != 255 && !altitude_counts[minimum])
        ++minimum;
      minimum_altitude = minimum;
    }
    u8 maximum = maximum_altitude;
    if (altitude == maximum && !altitude_counts[maximum]) {
      while (maximum && !altitude_counts[maximum])
        --maximum;
      maximum_altitude = maximum;
    }
    if (span == maximum_span && maximum_count == 1)
      RefreshRegistration(false);
  }
  cell->size = 0;
  cell->root_x = 0;
  cell->root_z = 0;
  cell->altitude = 0;
  cell->sprite = nullptr;
  cell->flags = 0;
  cell->top = 0;
  cell->bottom = 0;
  if (threaded)
    drawing_lock.Unlock();
  return true;
}

// Visit the live actual grid in X-major order and invoke the virtual remover without invalidation.
// Ignore each removal result and return true after the scan, preserving dimension changes made by callbacks.
bool SpCellMap::RemoveAll() {
  for (u32 x = 0; x < actual_width; ++x)
    for (u32 z = 0; z < actual_height; ++z)
      Remove(x, z, false);
  return true;
}

// Signal the original loader to stop, waking an idle worker before waiting at the caller's current priority.
u8 SpCellMap::StopLoader() {
  if (!drawing_thread.IsRunning())
    return 0;
  stop_loader = 1;
  if (!work_mode)
    drawing_thread.Resume();
  drawing_thread.SetPriority(ScThread::CurrentPriority());
  drawing_thread.Wait();
  return 1;
}

// Stop an initialized loader, release root art and remove retained static sprites before freeing the cell table.
// Release the background and city buffers in source order, publishing the cleared initialized state afterward.
bool SpCellMap::Shutdown() {
  if (!initialized)
    return true;
  if (loader_initialized) {
    StopLoader();
    for (u32 x = 0; x < actual_width; ++x) {
      for (u32 z = 0; z < actual_height; ++z) {
        for (i32 current = 0; current < 5; ++current)
          if ((cells[x][z].flags & 0x4000u) && (cells[x][z].flags & (16u << current)))
            ReleaseSpriteArt(cells[x][z].sprite, current, rotation, x, z);
      }
    }
    // The installed path frees these five fallback blocks directly, without any destructor or Release call.
    for (u32 size = 1; size <= 5; ++size) {
      ::operator delete(static_cast<void *>(fallback[size]));
      fallback[size] = nullptr;
    }
  }
  drawing_ready = 0;
  if (cells) {
    for (u32 x = 0; x < actual_width; ++x)
      for (u32 z = 0; z < actual_height; ++z)
        if (cells[x][z].flags & 0x4000u)
          Remove(x, z, false);
    for (u32 x = 0; x < actual_width; ++x)
      delete[] cells[x];
    delete[] cells;
    cells = nullptr;
  }
  ScRelease(&background_buffer);
  city_buffer->Release();
  initialized = 0;
  city_buffer = nullptr;
  return true;
}

// Apply resident-art notifications, sprite replacement and region invalidation to the footprint's actual root.
// The installed static batch recorder is a no-op, so batched ordinary changes are acknowledged without queuing.
bool SpCellMap::ChangeSprite(u32 x, u32 z, bool invalidate, u32 flags, void *payload) {
  SpCell *selected = &cells[x][z], *root = &cells[selected->root_x][selected->root_z];
  if (threaded) {
    if (flags & 0x18u) {
      drawing_lock.Lock();
      u16 mask = (u16)(16u << zoom);
      bool demand = (flags & 8u) != 0;
      if ((demand && !(root->flags & mask)) || (!demand && (root->flags & mask))) {
        ScRect previous, current;
        GetCityPixelRectForSprite((i32)x, (i32)z, &previous);
        i32 previous_span = (i32)root->top + root->bottom;
        if (demand)
          root->flags |= (u16)(16u << zoom);
        else
          root->flags &= (u16)~(16u << zoom);
        RefreshTileRegistration((i32)x, (i32)z);
        UpdateMaximumSpan(previous_span, (i32)root->top + root->bottom);
        GetCityPixelRectForSprite((i32)x, (i32)z, &current);
        if ((u32)zoom >= 3)
          UpdateStaticIndex(root, &previous, &current);
        current.Union(&previous, &current);
        if (invalidate)
          AddDirtyRect(&current, false, 0);
      }
      drawing_lock.Unlock();
      return true;
    }
    if (!(root->flags & (16u << zoom)))
      return true;
  }
  if (changing_batch)
    return true;
  const ScRect *relative;
  if (flags & 0x100u) {
    SpSprite *previous = root->sprite, *replacement = static_cast<SpSprite *>(payload);
    if (replacement)
      replacement->AddRef();
    if (previous)
      previous->Release();
    root->sprite = replacement;
    // The original footprint loop repeatedly writes this same root pointer, without updating the covered cells.
    if (root->size > 1) {
      x = (u32)root->root_x - root->size + 1u;
      z = root->root_z;
    }
    relative = nullptr;
  } else
    relative = static_cast<const ScRect *>(payload);
  if (flags & 1u) {
    i32 root_x = root->root_x, root_z = root->root_z, draw_x, draw_z;
    ActualGridToDrawGrid(root_x, root_z, &draw_x, &draw_z);
    ScRect rectangle;
    GetCityPixelRectForSprite(root_x, root_z, &rectangle);
    if (relative) {
      rectangle.left = (i32)((u32)rectangle.left + (u32)relative->left);
      rectangle.top = (i32)((u32)rectangle.top + (u32)relative->top);
      rectangle.right = (i32)((u32)rectangle.left + (u32)relative->right - (u32)relative->left);
      rectangle.bottom = (i32)((u32)rectangle.top + (u32)relative->bottom - (u32)relative->top);
    }
    if (invalidate) {
      GetDrawRegistration(root_x, root_z, &draw_x, &draw_z);
      AddDirtyRect(&rectangle, false, (u32)draw_x + (u32)draw_z);
    }
  } else if (root->sprite) {
    ScRect previous, registration, current;
    if (!relative)
      GetCityPixelRectForSprite((i32)x, (i32)z, &previous);
    root->sprite->GetRegistration(zoom, rotation, &registration);
    i32 previous_span = (i32)root->top + root->bottom;
    root->top = (i16)registration.top;
    root->bottom = (i16)registration.bottom;
    i32 current_span = (i32)root->top + root->bottom;
    GetCityPixelRectForSprite((i32)x, (i32)z, &current);
    if (relative) {
      current.left = (i32)((u32)current.left + (u32)relative->left);
      current.top = (i32)((u32)current.top + (u32)relative->top);
      current.right = (i32)((u32)current.left + (u32)relative->right - (u32)relative->left);
      current.bottom = (i32)((u32)current.top + (u32)relative->bottom - (u32)relative->top);
      if (invalidate)
        AddDirtyRect(&current, false, 0);
    } else {
      if ((u32)zoom >= 3 && !(flags & 0x60u))
        UpdateStaticIndex(root, &previous, &current);
      ScRect united;
      united.Union(&previous, &current);
      if (invalidate)
        AddDirtyRect(&united, false, 0);
    }
    if (previous_span != current_span)
      UpdateMaximumSpan(previous_span, current_span);
  }
  return true;
}

bool SpCellMap::IsEmpty(i32 x, i32 z) {
  return cells[x][z].sprite == nullptr;
}

// Pick the footprint root's resident sprite or size fallback and test its actual artwork opacity at the pixel.
// City-relative requests first subtract the original virtual sprite rectangle's top-left registration.
bool SpCellMap::IsCityPixelSolidForSprite(i32 x, i32 z, i32 pixel_x, i32 pixel_y, bool city_relative) {
  SpCell *cell = &cells[x][z];
  SpCell *root = &cells[cell->root_x][cell->root_z];
  i32 current_zoom = zoom;
  SpSprite *sprite = root->flags & (16u << current_zoom) ? root->sprite : fallback[root->size];
  if (!city_relative || !sprite)
    return sprite->IsPixelOpaque(pixel_x, pixel_y, current_zoom, rotation);
  ScRect rectangle;
  GetCityPixelRectForSprite(x, z, &rectangle);
  return sprite->IsPixelOpaque((i32)((u32)pixel_x - (u32)rectangle.left),
      (i32)((u32)pixel_y - (u32)rectangle.top), zoom, rotation);
}

// Refresh cached vertical registration from resident sprites or size fallbacks, then count the widest span.
// Registration may write only the two consumed low words; do not read the untouched rectangle bytes.
void SpCellMap::RefreshRegistration(bool refresh) {
  ScRect registration;
  maximum_span = maximum_count = 0;
  for (u32 x = 0; x < actual_width; ++x) {
    for (u32 z = 0; z < actual_height; ++z) {
      if (IsEmpty((i32)x, (i32)z))
        continue;
      SpCell *cell = &cells[x][z];
      if (refresh && cell->sprite) {
        SpSprite *sprite = cells[cell->root_x][cell->root_z].flags & (16u << zoom) ?
            cell->sprite : fallback[cell->size];
        sprite->GetRegistration(zoom, rotation, &registration);
        memcpy(&cell->top, &registration.top, sizeof(cell->top));
        memcpy(&cell->bottom, &registration.bottom, sizeof(cell->bottom));
      }
      u32 span = (u32)((i32)cell->top + cell->bottom);
      if (span == maximum_span)
        ++maximum_count;
      else if (span > maximum_span) {
        maximum_span = span;
        maximum_count = 1;
      }
    }
  }
}

// Refresh every covered cell's cached top and bottom from its resident artwork or size fallback.
i16 SpCellMap::RefreshTileRegistration(i32 x, i32 z) {
  SpCell *cell = &cells[x][z];
  ScRect registration;
  SpSprite *sprite = cell->flags & (16u << zoom) ? cell->sprite : fallback[cell->size];
  sprite->GetRegistration(zoom, rotation, &registration);
  i16 result = cell->size;
  i32 column = (i32)((u32)x - cell->size);
  while ((column = (i32)((u32)column + 1u)) <= x)
    for (u32 offset = 0; offset < cell->size; ++offset) {
      SpCell *covered = &cells[column][(i32)((u32)z + offset)];
      covered->top = (i16)registration.top;
      result = (i16)registration.bottom;
      covered->bottom = result;
    }
  return result;
}

// Recompute the maximum when its sole former contributor changes, then include the new signed span.
i32 SpCellMap::UpdateMaximumSpan(i32 previous, i32 current) {
  if ((u32)previous == maximum_span && maximum_count == 1)
    RefreshRegistration(false);
  i32 result = (i32)maximum_span;
  if (current == result)
    ++maximum_count;
  else if (current > result) {
    maximum_span = (u32)current;
    maximum_count = 1;
  }
  return result;
}

static ScRect SpDrawClip(const SpDrawRect *record) {
  return {record->clip_left, record->clip_top, record->clip_right, record->clip_bottom};
}

void SpCellMap::GetBackgroundBuffer(GxBuffer **out) {
  *out = background_buffer;
}

// Release the previous background and adopt the supplied buffer without AddRef, preserving the source's ownership.
// A requested update redraws the available city buffer before copying the entire visible area.
void SpCellMap::SetBackgroundBuffer(GxBuffer *buffer, bool update) {
  ScRelease(&background_buffer);
  background_buffer = buffer;
  if (update && city_buffer) {
    UpdateCitySurfaceAll();
    CopyEntireAreaToBackBuffer();
  }
}

// Fill the requested clip with the adopted tiled background or the city's flat background color.
// Temporarily switch the buffer's lock mode and return its last defined byte result, restoring lock64 when requested.
u8 SpCellMap::FillBackground(const ScRect *rectangle, bool locked) {
  u8 result;
  if (background_buffer) {
    if (locked)
      city_buffer->Unlock(GxLockFlags::Bits);
    result = city_buffer->Lock(GxLockFlags::Blt);
    if (result) {
      i32 origin_x, origin_y;
      GetTilingOriginWindowPixel(&origin_x, &origin_y);
      u32 x_offset = ((u32)rectangle->left - (u32)origin_x) % background_buffer->Width();
      u32 y_offset = ((u32)rectangle->top - (u32)origin_y) % background_buffer->Height();
      const ScRect *source = background_buffer->GetBufferArea();
      city_buffer->BltTiled(
          background_buffer, *(source), *(rectangle), (i32)(0u - x_offset), (i32)(0u - y_offset), nullptr);
      result = city_buffer->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (locked)
      city_buffer->Unlock(GxLockFlags::Bits);
    result = city_buffer->Lock(GxLockFlags::Fill);
    if (result) {
      city_buffer->Fill(background_color, *rectangle, nullptr);
      result = city_buffer->Unlock(GxLockFlags::Fill);
    }
  }
  if (locked)
    return city_buffer->Lock(GxLockFlags::Bits);
  return result;
}

// Follow a still-registered dynamic sprite when no draw list is pending, centering its current rectangle in the view.
// Keep the source's signed half divisions, zoom-four alignment and reentrancy flag around the real movement call.
void SpCellMap::FollowTarget() {
  if (!follow_target || draw_total)
    return;
  u32 bucket = (u32)(((uptr)follow_target >> 2) % dynamic_bucket_count);
  SpDynamicNode *node = dynamic_buckets[bucket];
  while (node && node->value != follow_target)
    node = node->next;
  if (!node) {
    follow_target = nullptr;
    return;
  }
  ScRect *rectangle = &follow_target->rectangle;
  i32 width = (i32)((u32)viewport.right - (u32)viewport.left) / 2;
  i32 height = (i32)((u32)viewport.bottom - (u32)viewport.top) / 2;
  i32 x = (i32)((u32)((i32)((u32)rectangle->left + (u32)rectangle->right) / 2) - (u32)width);
  i32 y = (i32)((u32)((i32)((u32)rectangle->top + (u32)rectangle->bottom) / 2) - (u32)height);
  if (zoom == 4) {
    x = (i32)((((u32)x - 1u) | 1u) + 1u);
    y = (i32)((((u32)y - 1u) | 1u) + 1u);
  }
  if (x != viewport.left || y != viewport.top) {
    following = 1;
    MoveAbsolute(x, y, true);
    following = 0;
  }
}

i32 SpCellMap::IncrementAlphaBlendCount(i32 count) {
  alpha_blend_count = (i32)((u32)alpha_blend_count + (u32)count);
  return alpha_blend_count;
}

// Render queued dirty regions in the original background, priority-sprite, ordinary-sprite and overlay order.
// Preserve failed-lock queues, optional presentation, copied clips and the frame's background-work cadence.
void SpCellMap::UpdateAllDirtyRects(bool lock, bool present) {
  if (!initialized)
    return;
  if (follow_target && !following)
    FollowTarget();
  if (changed) {
    if (!dirty_rects.size)
      InvalidateEntireScreen();
    changed = 0;
  }
  dirty_mask_enabled = alpha_blend_count != 0;
  if (!immediate_redraw) {
    u32 count = dirty_rects.size;
    for (u32 i = 0; i < count; ++i)
      AddDirtyRect(&dirty_rects.data[i].rectangle, true, dirty_rects.data[i].flags);
    dirty_rects.Clear();
  } else {
    dirty_rects.Clear();
    AddDirtyRect(&viewport, true, 0);
  }
  if (!draw_total) {
    if (present)
      CopyAllExtraRectsToBackBuffer();
    ++frame_count;
    if (work_mode && !(frame_count & 3u))
      FinishBackgroundTask();
    pending = 0;
    return;
  }
  if (!lock || city_buffer->Lock(GxLockFlags::Bits)) {
    u32 count = extra_rects.size;
    for (u32 i = 0; i < count; ++i) {
      SpDrawRect *record = &extra_rects.data[i];
      ScRect clip = SpDrawClip(record);
      if (record->kind == 3)
        FillBackground(&clip, true);
      if (copy_extra_rects)
        *copy_rects[copy_index].Append() = clip;
    }
    if (count)
      extra_rects.Clear();
    count = draw_rects.size;
    if (count) {
      for (u32 i = 0; i < count; ++i) {
        SpDrawRect *record = &draw_rects.data[draw_order.data[i]];
        if (record->kind != 1)
          continue;
        ScRect clip = SpDrawClip(record);
        record->sprite->Draw(city_buffer, record->x, record->y, zoom, rotation, &clip);
        if (copy_extra_rects)
          *copy_rects[copy_index].Append() = clip;
      }
      for (u32 i = 0; i < count; ++i) {
        SpDrawRect *record = &draw_rects.data[draw_order.data[i]];
        ScRect clip = SpDrawClip(record);
        SpSprite *sprite;
        if (!record->kind)
          sprite = record->sprite;
        else if (record->kind == 2)
          sprite = record->dynamic->sprite;
        else
          continue;
        sprite->Draw(city_buffer, record->x, record->y, zoom, rotation, &clip);
        if (copy_extra_rects)
          *copy_rects[copy_index].Append() = clip;
      }
      draw_rects.Clear();
    }
    count = overlay_rects.size;
    for (u32 i = 0; i < count; ++i) {
      SpDrawRect *record = &overlay_rects.data[i];
      ScRect clip = SpDrawClip(record);
      if (record->kind == 3)
        FillBackground(&clip, true);
      if (copy_extra_rects)
        *copy_rects[copy_index].Append() = clip;
    }
    if (count)
      overlay_rects.Clear();
    if (lock)
      city_buffer->Unlock(GxLockFlags::Bits);
    if (present)
      CopyAllExtraRectsToBackBuffer();
  }
  if (dirty_mask_enabled)
    memset(dirty_mask, 0, dirty_width * dirty_height);
  pending = 0;
  ++frame_count;
  draw_total = 0;
  if (work_mode && !(frame_count & 3u))
    FinishBackgroundTask();
  if (changed) {
    changed = 0;
    pending = 0;
  }
}

// Copy the full city surface to an available back buffer under its blit lock. The original area pointer is used
// for both source and destination, and a missing back buffer or failed lock leaves the surface untouched.
void SpCellMap::CopyEntireAreaToBackBuffer() {
  if (back_buffer && back_buffer->Lock(GxLockFlags::Blt)) {
    const ScRect *area = city_buffer->GetBufferArea();
    back_buffer->BltNoClip(city_buffer, *area, *area);
    back_buffer->Unlock(GxLockFlags::Blt);
  }
}

// Copy both retained rectangle ranges to the back buffer, then draw source-buffer overlays in their stored order.
// Masked overlays use the original point-target span-buffer operation, followed by release of its queried interface.
void SpCellMap::CopyAllExtraRectsToBackBuffer() {
  u32 first = copy_rects[0].size, second = copy_rects[1].size;
  if ((first || second) && back_buffer->Lock(GxLockFlags::Blt)) {
    for (u32 i = 0; i < first; ++i)
      back_buffer->BltNoClip(city_buffer, copy_rects[0].data[i], copy_rects[0].data[i]);
    for (u32 i = 0; i < second; ++i)
      back_buffer->BltNoClip(city_buffer, copy_rects[1].data[i], copy_rects[1].data[i]);
    back_buffer->Unlock(GxLockFlags::Blt);
  }
  if (overlays.size && back_buffer->Lock(GxLockFlags::Bits)) {
    for (u32 i = 0; i < overlays.size; ++i) {
      SpOverlay *overlay = &overlays.data[i];
      const ScRect *source = overlay->buffer->GetBufferArea();
      ScRect destination = {overlay->position.x, overlay->position.y,
          (i32)((u32)overlay->position.x + (u32)source->right),
          (i32)((u32)overlay->position.y + (u32)source->bottom)};
      if (!overlay->mask)
        overlay->buffer->BltSrc(back_buffer, *(source), destination, nullptr);
      else {
        GxImageBuilder *builder = nullptr;
        if (overlay->buffer->QueryInterface(IID_GxImageBuilder, reinterpret_cast<void **>(&builder))) {
          ScRect clip = *back_buffer->GetBufferArea();
          builder->SpanBufferAlphaBlt(back_buffer, overlay->mask, *source, overlay->position, &clip);
          builder->Release();
        }
      }
    }
    back_buffer->Unlock(GxLockFlags::Bits);
  }
}

static const ScArray<SpDrawRect> *SpDrawRecords;

// Compare overlapping footprints by priority and other pairs by wrapped depth. Footprint edges are signed DWORDs.
static i32 SpCompareDrawIndices(const u32 *first, const u32 *second) {
  const SpDrawRect *a = &SpDrawRecords->data[*first], *b = &SpDrawRecords->data[*second];
  if (a->priority != b->priority && a->footprint.right >= b->footprint.left &&
      a->footprint.left <= b->footprint.right && a->footprint.bottom >= b->footprint.top &&
      a->footprint.top <= b->footprint.bottom)
    return (i32)a->priority - b->priority;
  return (i32)(b->depth - a->depth);
}

// Exchange two indices while leaving their original draw records in place. Self-exchanges are harmless.
static void SpSwapDrawIndices(u32 *a, u32 *b) {
  u32 value = *a;
  *a = *b;
  *b = value;
}

// Preserve the last scalar machine result.
static i32 SpBubbleDrawIndices(u32 *indices, i32 count) {
  i32 pass = 1, result;
  bool changed;
  do {
    changed = false;
    result = count - pass;
    for (i32 i = 0; i < count - pass; ++i) {
      result = SpCompareDrawIndices(&indices[i], &indices[i + 1]);
      if (result > 0) {
        result = (i32)indices[i];
        SpSwapDrawIndices(&indices[i], &indices[i + 1]);
        changed = true;
      }
    }
    ++pass;
  } while (changed);
  return result;
}

// Partition around the middle index, moving strictly smaller records before it. Return the pivot's final position.
static i32 SpPartitionDrawIndices(u32 *indices, i32 first, i32 last) {
  SpSwapDrawIndices(&indices[first], &indices[(first + last) / 2]);
  u32 pivot = indices[first];
  i32 edge = first;
  for (i32 i = first + 1; i <= last; ++i)
    if (SpCompareDrawIndices(&indices[i], &pivot) < 0)
      SpSwapDrawIndices(&indices[++edge], &indices[i]);
  SpSwapDrawIndices(&indices[first], &indices[edge]);
  return edge;
}

// Sort the left partition recursively and walk the right partition in place. Return the final remaining size minus one.
static i32 SpQuickDrawIndices(u32 *indices, i32 count) {
  while (count > 1) {
    i32 middle = SpPartitionDrawIndices(indices, 0, count - 1);
    SpQuickDrawIndices(indices, middle);
    indices += middle + 1;
    count -= middle + 1;
  }
  return count - 1;
}

// Correct nearby priority overlaps with the source's bounded retries, including its following-pair swap.
static u32 SC3_STDCALL SpRefineDrawIndices(u32 *indices, u32 count) {
  u32 i = 0, attempts = 0;
  while (i < count - 2) {
    u32 best = i;
    if (SpDrawRecords->data[indices[i]].priority >= 2) {
      u32 end = UintMin(i + 7, count);
      for (u32 j = i + 1; j < end; ++j)
        if (SpCompareDrawIndices(&indices[i], &indices[j]) > 0)
          best = j;
    }
    if (best == i) {
      ++i;
      attempts = 0;
    } else {
      if (best == i + 1) {
        SpSwapDrawIndices(&indices[best], &indices[best + 1]);
      } else {
        u32 value = indices[i];
        for (u32 j = i; j < best; ++j)
          indices[j] = indices[j + 1];
        indices[best] = value;
      }
      if (++attempts > 6) {
        ++i;
        attempts = 0;
      }
    }
  }
  return count - 2;
}

// Extend the receiver's index storage and sort only the newly added range. Earlier draw orders remain untouched.
u32 SpCellMap::SortDrawRecords(u32 first, u32 count, bool refine) {
  while (draw_order.size < draw_rects.size)
    *draw_order.Append() = 0;
  for (u32 i = first; i < first + count; ++i)
    draw_order.data[i] = i;
  SpDrawRecords = &draw_rects;
  u32 result = first;
  if (count >= 2) {
    u32 *indices = draw_order.data + first;
    result = (u32)(count >= 8 ? SpQuickDrawIndices(indices, (i32)count) : SpBubbleDrawIndices(indices, (i32)count));
    if (refine)
      return SpRefineDrawIndices(indices, count);
  }
  return result;
}

// Preserve the installed double-store rounding used by all spatial-grid coordinates, without aliasing its bytes.
static i32 SpBucketCoordinate(i32 coordinate, f32 scale) {
  f64 value = (f64)coordinate * scale - 0.49900001 + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return (i32)((u32)bits + kScDoubleIntegerLowWordBias);
}

// Return the bucket containing a city-buffer pixel, rejecting positions outside the full viewport first.
// Retain the source's biased floating-point conversion after subtracting the viewport origin.
SpSpatialNode *SpCellMap::GetSpatialBucket(i32 x, i32 y) {
  if (x < viewport.left || y < viewport.top || x >= viewport.right || y >= viewport.bottom)
    return nullptr;
  SpCellMap::CityPixelToWindowPixel_NonInline(x, y, &x, &y);
  i32 column = SpBucketCoordinate(x, inverse_bucket_width), row = SpBucketCoordinate(y, inverse_bucket_height);
  return spatial_buckets[(u32)column + (u32)row * spatial_width];
}

// Compute the inclusive bucket range before clipping, preserving the original one-pixel right/bottom adjustment.
static ScRect SpBucketRange(const SpCellMap *map, const ScRect *rectangle) {
  return {SpBucketCoordinate((i32)((u32)rectangle->left - (u32)map->viewport.left), map->inverse_bucket_width),
      SpBucketCoordinate((i32)((u32)rectangle->top - (u32)map->viewport.top), map->inverse_bucket_height),
      SpBucketCoordinate((i32)((u32)rectangle->right - (u32)map->viewport.left - 1u), map->inverse_bucket_width),
      SpBucketCoordinate((i32)((u32)rectangle->bottom - (u32)map->viewport.top - 1u), map->inverse_bucket_height)};
}

// Clamp an already computed inclusive range to the grid's actual dimensions, retaining empty ranges.
static void SpClipBuckets(const SpCellMap *map, ScRect *rectangle) {
  rectangle->left = ScIntMax(rectangle->left, 0);
  rectangle->top = ScIntMax(rectangle->top, 0);
  rectangle->right = ScIntMin(rectangle->right, (i32)(map->spatial_width - 1u));
  rectangle->bottom = ScIntMin(rectangle->bottom, (i32)(map->spatial_height - 1u));
}

void SpCellMap::InsertDynamicBucket(i32 x, i32 z, SpDynamicSprite *sprite) {
  SpSpatialNode **head = &spatial_buckets[(u32)x + ((u32)z << spatial_shift)];
  SpSpatialNode *node = new SpSpatialNode;
  node->sprite = sprite;
  node->primary = 1;
  node->next = *head;
  *head = node;
}

// Unlink the first matching membership while retaining the order of other entries and their primary tags.
void SpCellMap::RemoveDynamicBucket(i32 x, i32 z, SpDynamicSprite *sprite) {
  SpSpatialNode **link = &spatial_buckets[(u32)x + ((u32)z << spatial_shift)];
  while (*link && (!(*link)->primary || (*link)->sprite != sprite))
    link = &(*link)->next;
  assert(*link);
  SpSpatialNode *node = *link;
  *link = node->next;
  delete node;
}

// Add every visible dynamic-sprite bucket membership in row-major order, retaining a primary node in each bucket.
void SpCellMap::IndexDynamicSprite(SpDynamicSprite *sprite) {
  ScRect range = SpBucketRange(this, &sprite->rectangle);
  if (range.right < 0 || range.bottom < 0 || range.left >= (i32)spatial_width || range.top >= (i32)spatial_height)
    return;
  SpClipBuckets(this, &range);
  for (i32 z = range.top; z <= range.bottom; ++z)
    for (i32 x = range.left; x <= range.right; ++x)
      InsertDynamicBucket(x, z, sprite);
}

// Remove the old rectangle's inclusive bucket memberships in row-major order before the record is released.
void SpCellMap::UnindexDynamicSprite(SpDynamicSprite *sprite) {
  ScRect range = SpBucketRange(this, &sprite->rectangle);
  SpClipBuckets(this, &range);
  for (i32 z = range.top; z <= range.bottom; ++z)
    for (i32 x = range.left; x <= range.right; ++x)
      RemoveDynamicBucket(x, z, sprite);
}

// Keep overlapping bucket nodes in place, first adding new memberships and then removing memberships left behind.
// Equality is checked before clipping, matching the original grid-transition order at screen edges.
void SpCellMap::UpdateDynamicIndex(SpDynamicSprite *sprite, const ScRect *previous, const ScRect *current) {
  ScRect next = SpBucketRange(this, current), old = SpBucketRange(this, previous);
  if (next.left == old.left && next.top == old.top && next.right == old.right && next.bottom == old.bottom)
    return;
  SpClipBuckets(this, &next);
  SpClipBuckets(this, &old);
  for (i32 z = next.top; z <= next.bottom; ++z)
    for (i32 x = next.left; x <= next.right; ++x)
      if (x < old.left || z < old.top || x > old.right || z > old.bottom)
        InsertDynamicBucket(x, z, sprite);
  for (i32 z = old.top; z <= old.bottom; ++z)
    for (i32 x = old.left; x <= old.right; ++x)
      if (x < next.left || z < next.top || x > next.right || z > next.bottom)
        RemoveDynamicBucket(x, z, sprite);
}

// Drain every spatial chain without releasing its borrowed cell or sprite. The bucket array remains allocated.
void SpCellMap::ClearSpatialGrid() {
  for (u32 i = 0; i < spatial_width * spatial_height; ++i) {
    SpSpatialNode *node = spatial_buckets[i];
    while (node) {
      SpSpatialNode *next = node->next;
      delete node;
      node = next;
    }
    spatial_buckets[i] = nullptr;
  }
}

// Reallocate only when dimensions or the integer bucket width change; the original deliberately ignores height changes.
// Preserve the observed DWORD result and refresh existing dynamic bounds only when its byte argument is nonzero.
u32 SpCellMap::ConfigureSpatialGrid(u32 columns, u32 rows, u8 refresh) {
  u32 width = ((u32)viewport.right - (u32)viewport.left) / columns;
  u32 result;
  if (spatial_buckets && columns == spatial_width && rows == spatial_height && bucket_width == width) {
    result = width;
  } else {
    if (spatial_buckets) {
      ClearSpatialGrid();
      delete[] spatial_buckets;
      spatial_buckets = nullptr;
    }
    spatial_buckets = new SpSpatialNode *[(usize)columns * rows]();
    spatial_width = columns;
    spatial_height = rows;
    spatial_shift = spatial_height_shift = 0;
    for (u32 value = columns; value > 1; value >>= 1)
      ++spatial_shift;
    for (u32 value = rows; value > 1; value >>= 1)
      ++spatial_height_shift;
    bucket_width = width;
    bucket_height = ((u32)viewport.bottom - (u32)viewport.top) / rows;
    result = bucket_height;
    inverse_bucket_width = (f32)(1.0 / (f64)bucket_width);
    inverse_bucket_height = (f32)(1.0 / (f64)bucket_height);
  }
  return refresh ? (u32)RefreshDynamicBounds() : result;
}

// Translate the caller's mutable rectangle into viewport coordinates and update its un-clipped static memberships.
// Duplicate inserts remain at the head; removal erases only the first matching pointer in each bucket.
i32 SpCellMap::IndexStaticCell(SpCell *cell, ScRect *rectangle, u8 insert) {
  rectangle->left = (i32)((u32)rectangle->left - (u32)viewport.left);
  rectangle->right = (i32)((u32)rectangle->right - (u32)viewport.left);
  rectangle->top = (i32)((u32)rectangle->top - (u32)viewport.top);
  rectangle->bottom = (i32)((u32)rectangle->bottom - (u32)viewport.top);
  ScRect range = {SpBucketCoordinate(rectangle->left, inverse_bucket_width),
      SpBucketCoordinate(rectangle->top, inverse_bucket_height),
      SpBucketCoordinate((i32)((u32)rectangle->right - 1u), inverse_bucket_width),
      SpBucketCoordinate((i32)((u32)rectangle->bottom - 1u), inverse_bucket_height)};
  i32 result = (i32)((u32)range.bottom - kScIntegerSignBit);
  for (i32 z = range.top; z <= range.bottom; ++z) {
    result = range.left;
    for (i32 x = range.left; x <= range.right; ++x) {
      SpSpatialNode **link = &spatial_buckets[(u32)x + ((u32)z << (spatial_shift & 31u))];
      if (insert) {
        SpSpatialNode *node = new SpSpatialNode;
        node->cell = cell;
        node->primary = 2;
        node->next = *link;
        *link = node;
      } else {
        while (*link && (*link)->cell != cell)
          link = &(*link)->next;
        if (*link) {
          SpSpatialNode *node = *link;
          *link = node->next;
          delete node;
        }
      }
      result = (i32)((u32)x + 1u);
    }
  }
  return result;
}

// Refresh each live dynamic record's artwork bounds in hash order. This helper does not rebuild spatial memberships.
i32 SpCellMap::RefreshDynamicBounds() {
  u32 bucket = 0;
  while (bucket < dynamic_bucket_count && !dynamic_buckets[bucket])
    ++bucket;
  SpDynamicNode *node = bucket < dynamic_bucket_count ? dynamic_buckets[bucket] : nullptr;
  while (node) {
    DynamicBounds(node->value, &node->value->rectangle);
    SpDynamicNode *previous = node;
    node = node->next;
    if (!node) {
      bucket = (u32)(((uptr)previous->value >> 2) % dynamic_bucket_count);
      while (++bucket < dynamic_bucket_count) {
        node = dynamic_buckets[bucket];
        if (node)
          break;
      }
    }
  }
  return 0;
}

// Ask for the static sprite's city-pixel rectangle, retaining the original callback even though its result is unused.
// The installed public predicate then returns false for every defined call.
bool SpCellMap::IsSpriteVisible(i32 x, i32 z, bool) {
  ScRect rectangle;
  GetCityPixelRectForSprite(x, z, &rectangle);
  return false;
}

// Test the live dynamic record against the city clip and, when requested at high zoom, foreground occluders.
// Preserve missing-record success, low-zoom refusal, inclusive intersections and the original two-pixel tolerance.
bool SpCellMap::IsDynamicSpriteVisible(SpSprite *sprite, i32 x, i32 z, bool check_occlusion) {
  SpDynamicSprite *record = FindDynamicRecord(sprite, x, z);
  if (!record)
    return true;
  ScRect *rectangle = &record->rectangle;
  if (rectangle->right < world_clip.left || rectangle->bottom < world_clip.top || rectangle->left > world_clip.right ||
      rectangle->top > world_clip.bottom)
    return false;
  if (!check_occlusion)
    return true;
  if ((u32)zoom < 3)
    return false;
  ScRect range = SpBucketRange(this, rectangle);
  if (range.right < 0 || range.bottom < 0 || range.left >= (i32)spatial_width || range.top >= (i32)spatial_height)
    return true;
  u32 depth = record->depth;
  SpClipBuckets(this, &range);
  for (i32 row = range.top; row <= range.bottom; ++row)
    for (i32 column = range.left; column <= range.right; ++column)
      for (SpSpatialNode *node = spatial_buckets[(u32)column + ((u32)row << (spatial_shift & 31u))]; node;
          node = node->next) {
        ScRect bounds, intersection;
        const ScRect *occluder;
        if (node->primary == 2) {
          SpCell *cell = node->cell;
          i32 draw_x, draw_z;
          ActualGridToDrawGrid(cell->root_x, cell->root_z, &draw_x, &draw_z);
          u32 cell_depth = kSpCellDepthOrigin - 16384u * ((u32)draw_x + (u32)draw_z) - ((u32)cell->altitude << 6);
          if (cell_depth >= depth)
            continue;
          GetCityPixelRectForSprite(cell->root_x, cell->root_z, &bounds);
          occluder = &bounds;
        } else {
          if (node->sprite->depth >= depth)
            continue;
          occluder = &node->sprite->rectangle;
        }
        if (intersection.IntersectInclusive(occluder, rectangle) &&
            (i32)((u32)intersection.right - (u32)intersection.left) >=
                (i32)((u32)rectangle->right - (u32)rectangle->left - 2u) &&
            (i32)((u32)intersection.bottom - (u32)intersection.top) >=
                (i32)((u32)rectangle->bottom - (u32)rectangle->top - 2u))
          return false;
      }
  return true;
}

// Adjust a static cell's bucket memberships after its vertical artwork span changes.
i32 SpCellMap::UpdateStaticIndex(SpCell *cell, const ScRect *previous, const ScRect *current) {
  ScRect next = SpBucketRange(this, current), old = SpBucketRange(this, previous);
  old.left = next.left;
  old.right = next.right;
  i32 result = old.bottom;
  if (old.top == next.top && old.bottom == next.bottom)
    return result;
  SpClipBuckets(this, &next);
  SpClipBuckets(this, &old);
  for (i32 z = next.top; z <= next.bottom; ++z)
    if (z < old.top || z > old.bottom)
      for (i32 x = next.left; x <= next.right; ++x) {
        SpSpatialNode **head = &spatial_buckets[(u32)x + ((u32)z << (spatial_shift & 31u))];
        SpSpatialNode *node = *head;
        while (node && node->cell != cell)
          node = node->next;
        if (!node)
          *head = new SpSpatialNode{.cell = cell, .next = *head, .primary = 2};
      }
  for (i32 z = old.top; z <= old.bottom; ++z)
    if (z < next.top || z > next.bottom)
      for (i32 x = old.left; x <= old.right; ++x) {
        SpSpatialNode **link = &spatial_buckets[(u32)x + ((u32)z << (spatial_shift & 31u))];
        while (*link && (*link)->cell != cell)
          link = &(*link)->next;
        if (*link) {
          SpSpatialNode *node = *link;
          *link = node->next;
          delete node;
        }
      }
  return (i32)((u32)old.bottom + 1u);
}

// Reinsert every dynamic record after a full spatial-grid rebuild, optionally refreshing its projected bounds first.
void SpCellMap::ReindexDynamicSprites(u8 refresh) {
  u32 bucket = 0;
  while (bucket < dynamic_bucket_count && !dynamic_buckets[bucket])
    ++bucket;
  SpDynamicNode *node = bucket < dynamic_bucket_count ? dynamic_buckets[bucket] : nullptr;
  while (node) {
    SpDynamicSprite *record = node->value;
    if (refresh)
      DynamicBounds(record, &record->rectangle);
    IndexDynamicSprite(record);
    SpDynamicNode *previous = node;
    node = node->next;
    if (!node) {
      bucket = (u32)(((uptr)previous->value >> 2) % dynamic_bucket_count);
      while (++bucket < dynamic_bucket_count) {
        node = dynamic_buckets[bucket];
        if (node)
          break;
      }
    }
  }
}

// Rebuild visible static memberships after camera translation, marking footprint roots temporarily to avoid duplicates.
void SpCellMap::Expose(const ScRect *, const ScRect *rectangle, bool refresh) {
  ClearSpatialGrid();
  if ((u32)zoom >= 3) {
    visible_runs.Clear();
    BuildVisibleRuns(rectangle, 0);
    ScArray<u16 *> marked;
    u32 count = visible_runs.size;
    for (u32 i = 0; i < count; ++i) {
      i32 run_count = (i32)visible_runs.data[i].count;
      for (i32 offset = 0; offset < run_count; ++offset) {
        SpVisibleRun *run = &visible_runs.data[i];
        i32 draw_x = (i32)((u32)run->x - (u32)offset), draw_z = (i32)((u32)run->z + (u32)offset);
        if (!InDrawBounds(draw_x, draw_z))
          continue;
        i32 x, z;
        DrawGridToActualGrid(draw_x, draw_z, &x, &z);
        SpCell *cell = &cells[x][z], *root;
        if (cell->size <= 1)
          root = cell;
        else
          ResolveRoot(cell, &root, &draw_x, &draw_z);
        if ((i16)root->flags < 0)
          continue;
        root->flags |= 0x8000u;
        *marked.Append() = &root->flags;
        // A zero-width empty cell cannot intersect the viewport; its untouched cached extents need not be read.
        if (!root->size)
          continue;
        u32 width = (u32)root->size << ((zoom + 3) & 31);
        i32 top = root->top, bottom = root->bottom;
        u32 baseline = (((u32)draw_x + (u32)draw_z) << (half_shift_y & 31u)) -
            ((u32)root->altitude << (shift_altitude & 31u));
        ScRect bounds;
        bounds.left = (i32)(((u32)draw_z - (u32)draw_x) << (half_shift_x & 31u));
        bounds.right = (i32)((u32)bounds.left + width);
        bounds.bottom = (i32)(baseline + (u32)bottom);
        bounds.top = (i32)(baseline - (u32)top);
        ScRect clip;
        if (clip.Intersect(rectangle, &bounds))
          IndexStaticCell(root, &clip, 1);
      }
    }
    for (u32 i = 0; i < marked.size; ++i)
      *marked.data[i] &= 0x7fffu;
  }
  ReindexDynamicSprites(refresh);
}

// Classify the city pixel's quadrant using the original unsigned half-cell divisions and draw-grid parity.
// The private source declaration is unavailable; the installed scalar result is always in the range zero through three.
u32 SpCellMap::CellPart(i32 x, i32 y, i32 draw_x, i32 draw_z) {
  u32 half_width = cell_width / 2, half_height = cell_height / 2;
  u32 pixel_x = (u32)x - half_width, pixel_y = (u32)y + half_height;
  if (((u32)draw_x + (u32)draw_z) & 1u)
    pixel_y += half_height;
  else
    pixel_x += half_width;
  return ((pixel_x / half_width) & 1u) + 2u * ((pixel_y / half_height) & 1u);
}

i32 SpCellMap::InDrawBounds(i32 x, i32 z) {
  return (u32)x < draw_width && (u32)z < draw_height;
}

// Rebuild the receiver's visible-cell runs and return their total cell count, preserving DWORD wrap and edge trimming.
// The installed lower-edge altitude scan discards its result; omit those ineffective reads rather than model them.
i32 SpCellMap::BuildVisibleRuns(const ScRect *rectangle, i32 minimum_diagonal) {
  visible_runs.Clear();
  i32 left_x, left_z, right_x, right_z, bottom_x, bottom_z;
  CityPixelToDrawGridNoAltitude(rectangle->left, rectangle->top, &left_x, &left_z);
  u32 left_part = CellPart(rectangle->left, rectangle->top, left_x, left_z);
  i32 x = (i32)((u32)left_x - (left_part == 1));
  i32 alternate_x = (i32)((u32)left_x + (left_part == 2));
  i32 z = (i32)((u32)left_z - (left_part == 0));
  CityPixelToDrawGridNoAltitude(rectangle->right, rectangle->top, &right_x, &right_z);
  u32 right_part = CellPart(rectangle->right, rectangle->top, right_x, right_z);
  i32 right_edge = (i32)((u32)right_x - (right_part == 1));
  i32 alternate_right = (i32)((u32)right_x + (right_part == 2));
  CityPixelToDrawGridNoAltitude(rectangle->left, rectangle->bottom, &bottom_x, &bottom_z);
  u32 bottom_part = CellPart(rectangle->left, rectangle->bottom, bottom_x, bottom_z);
  u32 expansion = (u32)((i32)((u32)minimum_altitude << (shift_altitude & 31u)) >> (shift_y & 31u));
  expansion += (maximum_span + ((u32)maximum_altitude << (shift_altitude & 31u))) >> (shift_y & 31u);
  ++expansion;
  i32 end_x = (i32)((u32)bottom_x + (bottom_part == 2) + expansion);
  i32 end_z = (i32)((u32)bottom_z + (bottom_part == 3) + expansion);
  u32 widths[] = {(u32)x - (u32)right_edge + 1u, (u32)alternate_x - (u32)alternate_right + 1u};
  bool advance_z = alternate_x <= x;
  u32 phase = 0, total = 0;
  for (;;) {
    if ((i32)((u32)x + (u32)z) >= minimum_diagonal || dirty_mask_enabled) {
      *visible_runs.Append() = {x, z, widths[phase]};
      total += widths[phase];
    }
    if (x == end_x && z == end_z)
      break;
    phase ^= 1u;
    if (advance_z)
      z = (i32)((u32)z + 1u);
    else
      x = (i32)((u32)x + 1u);
    advance_z = !advance_z;
  }
  if (visible_runs.size) {
    SpVisibleRun *last = &visible_runs.data[visible_runs.size - 1];
    if (left_part == 0) {
      last->x = (i32)((u32)last->x - 1u);
      last->z = (i32)((u32)last->z + 1u);
      --last->count;
      --total;
    }
    if (right_part == 1 && last->count) {
      --last->count;
      --total;
    }
  }
  return (i32)total;
}

// Route high zoom directly to its spatial display-list builder; low zoom first rebuilds the actual visible-cell runs.
// Return the selected builder's DWORD result without normalizing it or adding a status/output argument.
u32 SpCellMap::QueueDrawRect(const ScRect *rectangle, u32 flags) {
  if ((u32)zoom >= 3)
    return BuildDisplayListHighZoom(rectangle, flags);
  BuildVisibleRuns(rectangle, (i32)flags);
  return BuildDisplayListLowZoom(rectangle, flags);
}
