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

// Tiling coordinates retain their viewport-origin bias; static records retain their diagonal-depth bias.
enum SpCellDrawingBias : u32 {
  kSpTilingOriginPixelBias = 0xffffc000,
  kSpStaticRecordDepthBias = 0x3fffc000,
};

// Static and dynamic sprite depth keys subtract their projected coordinates from the same original depth origin.
enum SpCellDepthValue : u32 {
  kSpCellDepthOrigin = 0x40000000,
};

// Synchronously demand every missing root sprite for the requested zoom under the drawing lock. Another rotation
// is rejected; demand results are ignored and each attempted cell is marked resident before publishing completion.
bool SpCellMap::LoadZoomAndRotation(i32 requested_zoom, i32 requested_rotation) {
  if (requested_rotation != rotation)
    return false;
  drawing_lock.Lock();
  DrainFlags();
  for (u32 x = 0; x < actual_width; ++x)
    for (u32 z = 0; z < actual_height; ++z)
      if ((cells[x][z].flags & 0x4000u) && !(cells[x][z].flags & (16u << requested_zoom))) {
        cells[x][z].sprite->DemandArt(requested_zoom, requested_rotation, false);
        cells[x][z].flags |= (u16)(16u << requested_zoom);
      }
  residency[requested_zoom] = 2;
  drawing_lock.Unlock();
  return true;
}

// Release resident art for the requested zoom group and clear each reached cell bit after its sprite call.
u8 SpCellMap::ReleaseSpriteArt(SpSprite *sprite, i32 requested_zoom, i32 requested_rotation, u32 x, u32 z) {
  if (threaded) {
    drawing_lock.Lock();
    u16 mask = (u16)(16u << requested_zoom);
    if (cells[x][z].flags & mask) {
      sprite->ReleaseArt(requested_zoom, requested_rotation);
      cells[x][z].flags &= (u16)~mask;
    }
    drawing_lock.Unlock();
    return 1;
  }
  i32 first = (u16)performance_info->memory_mb > 35 ? 0 : requested_zoom > 2 ? 3 : 0;
  i32 end = (u16)performance_info->memory_mb > 35 || requested_zoom > 2 ? 5 : 3;
  for (i32 current = first; current < end; ++current) {
    u16 mask = (u16)(16u << current);
    if (cells[x][z].flags & mask) {
      sprite->ReleaseArt(current, requested_rotation);
      cells[x][z].flags &= (u16)~mask;
    }
  }
  return 1;
}

// Demand missing art and retain the last returned byte, even when it is false. Each demand still sets its cell bit.
// If every bit was already set, the source returns the high byte of its original x argument.
u8 SpCellMap::DemandSpriteArt(SpSprite *sprite, i32 requested_zoom, i32 requested_rotation, u32 x, u32 z) {
  u8 result = (u8)(x >> 24);
  if (threaded) {
    drawing_lock.Lock();
    u16 mask = (u16)(16u << requested_zoom);
    if (!(cells[x][z].flags & mask)) {
      result = sprite->DemandArt(requested_zoom, requested_rotation, 0);
      cells[x][z].flags |= mask;
    }
    drawing_lock.Unlock();
    return result;
  }
  i32 first = (u16)performance_info->memory_mb > 35 ? 0 : requested_zoom > 2 ? 3 : 0;
  i32 end = (u16)performance_info->memory_mb > 35 || requested_zoom > 2 ? 5 : 3;
  for (i32 current = first; current < end; ++current) {
    u16 mask = (u16)(16u << current);
    if (!(cells[x][z].flags & mask)) {
      result = sprite->DemandArt(current, requested_rotation, 0);
      cells[x][z].flags |= mask;
    }
  }
  return result;
}

// Preserve live budget reads, per-cell art calls, request ordering and the worker resume before releasing the lock.
i32 SpCellMap::PropagateLayerUpdate(i32 old_zoom, i32 old_rotation, i32 new_zoom, i32 new_rotation) {
  if (threaded) {
    drawing_lock.Lock();
    DrainFlags();
    bool rotated = old_rotation != new_rotation;
    if (rotated) {
      for (u32 x = 0; x < actual_width; ++x) {
        for (u32 z = 0; z < actual_height; ++z) {
          if (!(cells[x][z].flags & 0x4000))
            continue;
          for (i32 current = 0; current < 5; ++current)
            if (cells[x][z].flags & (16u << current))
              ReleaseSpriteArt(cells[x][z].sprite, current, old_rotation, x, z);
          if (cells[x][z].flags & 1)
            DemandSpriteArt(cells[x][z].sprite, new_zoom, new_rotation, x, z);
        }
      }
      for (u32 i = 0; i < 5; ++i)
        residency[i] = 0;
    }
    if (rotated || old_zoom != new_zoom) {
      art_requests.Clear();
      *art_requests.Append() = {0, art_sequence++, 0, 0};
      if (!rotated) {
        for (u32 i = 0; i < 5; ++i)
          residency[i] = 1;
        for (u32 x = 0; x < actual_width; ++x)
          for (u32 z = 0; z < actual_height; ++z)
            if ((cells[x][z].flags & 0x4001) == 0x4001)
              DemandSpriteArt(cells[x][z].sprite, new_zoom, new_rotation, x, z);
      }
      ScRect pixels, visible;
      GetVisibleAreaRectangle(&pixels, &visible);
      for (i32 x = visible.left; x <= visible.right; ++x)
        for (i32 z = visible.top; z <= visible.bottom; ++z)
          if ((cells[x][z].flags & 0x4000) && !(cells[x][z].flags & (16u << new_zoom)))
            DemandSpriteArt(cells[x][z].sprite, new_zoom, new_rotation, (u32)x, (u32)z);
      bool evict_neighbors = false;
      if ((u16)performance_info->memory_mb < 20) {
        static const u32 padding[5] = {32, 20, 16, 8, 4};
        visible.left = (i32)((u32)visible.left - padding[new_zoom]);
        visible.top = (i32)((u32)visible.top - padding[new_zoom]);
        visible.right = (i32)((u32)visible.right + padding[new_zoom]);
        visible.bottom = (i32)((u32)visible.bottom + padding[new_zoom]);
        u32 packed = 0;
        if (visible.right >= 0 && visible.left <= (i32)(actual_width - 1) && visible.bottom >= 0 &&
            visible.top <= (i32)(actual_height - 1)) {
          visible.left = ScIntMax(visible.left, 0);
          visible.top = ScIntMax(visible.top, 0);
          visible.right = ScIntMin(visible.right, (i32)(actual_width - 1));
          visible.bottom = ScIntMin(visible.bottom, (i32)(actual_height - 1));
          packed = (u32)(u8)visible.left << 24 | (u32)(u8)visible.top << 16 | (u32)(u8)visible.right << 8 |
              (u8)visible.bottom;
        }
        *art_requests.Append() = {3, art_sequence++, (u32)new_zoom, packed};
        evict_neighbors = !rotated;
      } else {
        *art_requests.Append() = {1, art_sequence++, (u32)new_zoom, 0};
        if (rotated) {
          if ((u16)performance_info->memory_mb >= 30) {
            if (new_zoom > 0 && new_zoom != 3)
              *art_requests.Append() = {1, art_sequence++, (u32)new_zoom - 1, 0};
            if (new_zoom < 4 && new_zoom != 2 && (u16)performance_info->memory_mb >= 46)
              *art_requests.Append() = {1, art_sequence++, (u32)new_zoom + 1, 0};
          }
        } else if ((u16)performance_info->memory_mb >= 30) {
          for (i32 current = 0; current < 5; ++current)
            if (current < new_zoom - 1 || current > new_zoom + 1)
              *art_requests.Append() = {2, art_sequence++, (u32)current, 0};
          if (new_zoom > 0)
            *art_requests.Append() = {new_zoom == 3 ? 2u : 1u, art_sequence++, (u32)new_zoom - 1, 0};
          if (new_zoom < 4) {
            u32 operation = new_zoom == 2 || (u16)performance_info->memory_mb < 46 ? 2u : 1u;
            *art_requests.Append() = {operation, art_sequence++, (u32)new_zoom + 1, 0};
          }
        } else
          evict_neighbors = true;
      }
      if (evict_neighbors) {
        if (new_zoom > 0)
          *art_requests.Append() = {2, art_sequence++, (u32)new_zoom - 1, 0};
        if (new_zoom < 4)
          *art_requests.Append() = {2, art_sequence++, (u32)new_zoom + 1, 0};
        for (i32 current = 0; current < 5; ++current)
          if (current < new_zoom - 1 || current > new_zoom + 1)
            *art_requests.Append() = {2, art_sequence++, (u32)current, 0};
      }
    }
    work_mode = 2;
    drawing_thread.Resume();
    drawing_lock.Unlock();
  } else {
    bool release, update, clear_old = false;
    if (preload) {
      release = (u16)performance_info->memory_mb <= 35 || old_rotation != new_rotation;
      clear_old = (u16)performance_info->memory_mb <= 35 && old_rotation == new_rotation;
      update = true;
    } else {
      release = old_rotation != new_rotation ||
          ((u16)performance_info->memory_mb <= 35 &&
              ((old_zoom <= 2 && new_zoom >= 3) || (new_zoom <= 2 && old_zoom >= 3)));
      update = release;
    }
    if (update) {
      for (u32 x = 0; x < actual_width; ++x) {
        for (u32 z = 0; z < actual_height; ++z) {
          if (!(cells[x][z].flags & 0x4000))
            continue;
          if (release)
            ReleaseSpriteArt(cells[x][z].sprite, old_zoom, old_rotation, x, z);
          DemandSpriteArt(cells[x][z].sprite, new_zoom, new_rotation, x, z);
        }
      }
      if (clear_old)
        residency[old_zoom] = 0;
      residency[new_zoom] = 2;
    }
  }
  if (old_rotation == new_rotation && old_zoom == new_zoom)
    return new_rotation;
  u32 bucket = 0;
  while (bucket < dynamic_bucket_count && !dynamic_buckets[bucket])
    ++bucket;
  SpDynamicNode *node = bucket < dynamic_bucket_count ? dynamic_buckets[bucket] : nullptr;
  while (node) {
    SpSprite *sprite = node->value->sprite;
    sprite->ReleaseArt(old_zoom, old_rotation);
    sprite->DemandArt(new_zoom, new_rotation, 0);
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

SpSpiral::SpSpiral() : bounds{-INT32_MAX, -INT32_MAX, INT32_MAX, INT32_MAX}, direction(1) {
  Seed(0, 0);
}

// Reset the traversal to a requested center and prepare its first horizontal leg.
i32 SpSpiral::Seed(i32 center_x, i32 center_z) {
  x = center_x;
  z = center_z;
  end_z = center_z;
  if (direction == 1) {
    end_x = (i32)((u32)center_x + 1u);
    horizontal = 1;
    step = 1;
  } else {
    step = -1;
    end_x = (i32)((u32)center_x - 1u);
    horizontal = 1;
  }
  excluded = state = 0;
  if (center_x < bounds.left || center_z < bounds.top || center_x > bounds.right || center_z > bounds.bottom) {
    state = 2;
    excluded = 15;
  }
  return center_x;
}

// Extend the next spiral leg, reversing its direction at the same horizontal/vertical transitions as the source.
// Preserve DWORD wrap for the step growth and return the newly stored vertical endpoint.
i32 SpSpiral::Extend() {
  i32 result;
  if (horizontal) {
    horizontal = 0;
    end_x = x;
    if (direction == 0)
      step = (i32)(0u - (u32)step);
    result = (i32)((u32)z + (u32)step);
  } else {
    horizontal = 1;
    if (direction == 1)
      step = (i32)(0u - (u32)step);
    step = (i32)((u32)step + (step > 0 ? 1u : UINT32_MAX));
    end_x = (i32)((u32)step + (u32)x);
    result = z;
  }
  end_z = result;
  return result;
}

// Skip a completed leg and clamp the next leg to the bounded grid. Each exhausted edge is remembered so the
// traversal terminates once all four directions have passed the bounds rather than walking an unbounded spiral.
u8 SpSpiral::Turn() {
  for (;;) {
    if (excluded == 15)
      return 0;
    if (horizontal) {
      if (step > 0) {
        if (x > bounds.right)
          excluded |= 1;
        else {
          Extend();
          if (z < bounds.top)
            z = bounds.top;
          return 1;
        }
      } else if (x < bounds.left)
        excluded |= 2;
      else {
        Extend();
        if (z > bounds.bottom)
          z = bounds.bottom;
        return 1;
      }
    } else if (step > 0) {
      if (z > bounds.bottom)
        excluded |= 4;
      else {
        Extend();
        if (x > bounds.right)
          x = bounds.right;
        return 1;
      }
    } else if (z < bounds.top)
      excluded |= 8;
    else {
      Extend();
      if (x < bounds.left)
        x = bounds.left;
      return 1;
    }
    Extend();
    x = end_x;
    z = end_z;
  }
}

// Yield the center once, then advance one bounded horizontal or vertical cell on each call. Exhaustion leaves
// the caller's two outputs untouched, and successful calls publish X before Z.
u8 SpSpiral::Next(i32 *out_x, i32 *out_z) {
  if (state == 2)
    return 0;
  if (state == 0)
    state = 1;
  else if (horizontal) {
    if (step > 0) {
      x = (i32)((u32)x + 1u);
      if (x > bounds.right)
        x = end_x;
    } else {
      x = (i32)((u32)x - 1u);
      if (x < bounds.left)
        x = end_x;
    }
    if (x == end_x)
      Turn();
  } else {
    if (step > 0) {
      z = (i32)((u32)z + 1u);
      if (z > bounds.bottom)
        z = end_z;
    } else {
      z = (i32)((u32)z - 1u);
      if (z < bounds.top)
        z = end_z;
    }
    if (z == end_z)
      Turn();
  }
  if (excluded == 15) {
    state = 2;
    return 0;
  }
  *out_x = x;
  *out_z = z;
  return 1;
}

u8 SpCellMap::NextLoaderCell() {
  return loader_spiral.Next(&loader_x, &loader_z);
}

static u32 SC_THREAD_CALL SpLoaderEntry(void *argument) {
  return static_cast<SpCellMap *>(argument)->RunLoader();
}

// Start the loader only when its thread is not already running. The stop flag and priority are set under the
// original drawing lock, and the thread-start result does not replace the source's low-byte success result.
u8 SpCellMap::StartLoader() {
  if (drawing_thread.IsRunning())
    return 0;
  drawing_lock.Lock();
  stop_loader = 0;
  drawing_thread.Start(SpLoaderEntry, this);
  drawing_thread.SetPriority(2);
  drawing_lock.Unlock();
  return 1;
}

// Pop the completed art request and configure the next request's bounded traversal. Full-map jobs start around
// the cached visible center; packed jobs normalize their own bounds and preserve the original unused order word.
u8 SpCellMap::StartLoaderJob() {
  memmove(art_requests.data, art_requests.data + 1, (art_requests.size - 1) * sizeof(SpArtRequest));
  --art_requests.size;
  if (!art_requests.size)
    return 0;
  SpArtRequest *request = art_requests.data;
  u32 operation = request->operation;
  if (operation == 1 || operation == 2) {
    loader_bounds = {0, 0, (i32)(actual_width - 1), (i32)(actual_height - 1)};
    if (loader_spiral.state != 1)
      loader_spiral.order = operation == 1 ? 1 : 0;
    loader_center_x = (i32)((u32)visible_inner.left + (u32)visible_inner.right) / 2;
    loader_center_z = (i32)((u32)visible_inner.top + (u32)visible_inner.bottom) / 2;
    loader_spiral.bounds = loader_bounds;
    loader_spiral.Seed(loader_center_x, loader_center_z);
    loader_spiral.Next(&loader_x, &loader_z);
  } else if (operation == 3 || operation == 4) {
    u32 packed = request->bounds;
    loader_bounds = {(i32)(packed >> 24), (i32)((packed >> 16) & 255u),
        (i32)((packed >> 8) & 255u), (i32)(packed & 255u)};
    if (loader_bounds.left > loader_bounds.right) {
      i32 previous = loader_bounds.left;
      loader_bounds.left = loader_bounds.right;
      loader_bounds.right = previous;
    }
    if (loader_bounds.top > loader_bounds.bottom) {
      i32 previous = loader_bounds.top;
      loader_bounds.top = loader_bounds.bottom;
      loader_bounds.bottom = previous;
    }
    loader_center_x = (i32)((u32)loader_bounds.left + (u32)loader_bounds.right) / 2;
    loader_center_z = (i32)((u32)loader_bounds.top + (u32)loader_bounds.bottom) / 2;
    loader_spiral.bounds = loader_bounds;
    loader_spiral.Seed(loader_center_x, loader_center_z);
    if (loader_spiral.state != 1)
      loader_spiral.order = operation == 3 ? 1 : 0;
    loader_spiral.Next(&loader_x, &loader_z);
  } else if (operation == 5) {
    loader_bounds = {0, 0, (i32)(actual_width - 1), (i32)(actual_height - 1)};
    loader_x = loader_z = 0;
  } else {
    if (!operation) {
      loader_bounds = {0, 0, 0, 0};
      loader_zoom = loader_rotation = 0;
      loader_x = loader_z = 0;
    }
    return 1;
  }
  loader_zoom = (i8)request->zoom;
  loader_rotation = (i8)rotation;
  return 1;
}

// Execute queued residency work in the original weighted time slices.
u32 SpCellMap::RunLoader() {
  while (!stop_loader) {
    drawing_thread.PauseCheckpoint();
    drawing_lock.Lock();
    if (drawing_thread.PausePending()) {
      drawing_lock.Unlock();
      continue;
    }
    f32 work = 0.0f;
    while ((f64)work < load_budget) {
      if (!art_requests.size) {
        work_mode = 1;
        break;
      }
      u32 operation = art_requests.data[0].operation;
      if (operation == 1 || operation == 3) {
        SpCell *cell = &cells[loader_x][loader_z];
        if (cell->root_x == loader_x && cell->root_z == loader_z && cell->sprite &&
            !(cell->flags & (16u << loader_zoom))) {
          cell->sprite->DemandArt(loader_zoom, loader_rotation, false);
          *completed_flags.Append() = {2, (u8)loader_x, (u8)loader_z, loader_zoom};
          work = (f32)((f64)work + 1.0);
        } else
          work = (f32)((f64)work + (f64)0.03f);
        if (!NextLoaderCell()) {
          if (operation == 1)
            residency[loader_zoom] = 2;
          StartLoaderJob();
        }
      } else if (operation == 2 || operation == 4) {
        SpCell *cell = &cells[loader_x][loader_z];
        if (cell->root_x == loader_x && cell->root_z == loader_z && cell->sprite &&
            (cell->flags & (16u << loader_zoom)) && (cell->flags == 0 || loader_zoom != zoom)) {
          cell->sprite->ReleaseArt(loader_zoom, loader_rotation);
          *completed_flags.Append() = {0, (u8)loader_x, (u8)loader_z, loader_zoom};
          work = (f32)((f64)work + (f64)0.3f);
        } else
          work = (f32)((f64)work + (f64)0.03f);
        if (!NextLoaderCell()) {
          residency[loader_zoom] = 1;
          StartLoaderJob();
        }
      } else if (operation == 5) {
        bool loaded = IsZoomAndRotationLoaded(loader_zoom, loader_rotation);
        residency[loader_zoom] = loaded ? 2 : 1;
        StartLoaderJob();
      } else
        StartLoaderJob();
    }
    drawing_lock.Unlock();
    ScThread::SleepCurrentThread(20000);
  }
  drawing_thread.Exit(0);
  return 0;
}

// Change asynchronous loading under the drawing lock, draining completion flags before queue changes. Disabling
// without processing deliberately retains the enable flag, matching the installed method's conditional store.
bool SpCellMap::EnableBackgroundTask(bool enabled, bool process) {
  if (enabled == (threaded != 0))
    return true;
  if (!loader_initialized)
    return false;
  drawing_lock.Lock();
  DrainFlags();
  if (enabled) {
    threaded = 1;
    drawing_lock.Unlock();
    return true;
  }
  while (art_requests.size > 1)
    --art_requests.size;
  drawing_lock.Unlock();
  if (process) {
    ScStopwatch elapsed(ScStopwatch::Seconds, 0);
    i32 priority = drawing_thread.GetPriority();
    drawing_thread.SetPriority(4);
    elapsed.Restart();
    while (art_requests.size && elapsed.GetElapsedTime() < 120)
      DrainFlags();
    DrainFlags();
    drawing_thread.SetPriority(priority);
    threaded = 0;
  }
  return true;
}

// Drain completed artwork only while the drawing lock is unowned, then suspend an idle worker under the lock.
uptr SpCellMap::FinishBackgroundTask() {
  uptr result = reinterpret_cast<uptr>(&completed_flags);
  if (completed_flags.size) {
    result = (uptr)drawing_lock.IsLocked();
    if (!result)
      result = (uptr)DrainFlags();
  }
  if (work_mode == 2) {
    result = reinterpret_cast<uptr>(&art_requests);
    if (!art_requests.size)
      work_mode = 1;
  }
  if (work_mode == 1) {
    drawing_lock.Lock();
    drawing_thread.Suspend();
    work_mode = 0;
    return (uptr)drawing_lock.Unlock();
  }
  return result;
}

// Remove the first queued loaded-state probe for a zoom.
void SpCellMap::RemoveLoadedCheck(i32 requested_zoom, i32) {
  for (u32 i = 0; i < art_requests.size; ++i)
    if (art_requests.data[i].operation == 5 && art_requests.data[i].zoom == (u32)requested_zoom) {
      memmove(&art_requests.data[i], &art_requests.data[i + 1],
          (art_requests.size - i - 1) * sizeof(SpArtRequest));
      --art_requests.size;
      return;
    }
}

// Insert a loaded-state probe at the requested queue position, clamping only by traversing the existing range.
void SpCellMap::QueueLoadedCheck(i32 requested_zoom, i32, u32 position) {
  SpArtRequest request = {5, art_sequence++, (u32)requested_zoom, 0};
  u32 index = position < art_requests.size ? position : art_requests.size;
  art_requests.Append();
  memmove(&art_requests.data[index + 1], &art_requests.data[index],
      (art_requests.size - index - 1) * sizeof(SpArtRequest));
  art_requests.data[index] = request;
}

void SpCellMap::GetTilingOriginWindowPixel(i32 *x, i32 *y) {
  *x = (i32)(kSpTilingOriginPixelBias - (u32)viewport.left);
  *y = (i32)(kSpTilingOriginPixelBias - (u32)viewport.top);
}

i32 SpCellMap::GetZoom() const {
  return zoom;
}

i32 SpCellMap::GetRotate() const {
  return rotation;
}

// Copy the live city-pixel viewport in the original edge order. The output may share the map's rectangle storage.
void SpCellMap::GetViewRect(ScRect *out) const {
  out->left = viewport.left;
  out->top = viewport.top;
  out->right = viewport.right;
  out->bottom = viewport.bottom;
}

// Restore a full viewport-sized local clip through the live virtual setter. Preserve its zoom-four alignment behavior.
void SpCellMap::RemoveViewClipRect() {
  ScRect rectangle{
      0, 0, (i32)((u32)viewport.right - (u32)viewport.left), (i32)((u32)viewport.bottom - (u32)viewport.top)};
  SetViewClipRect(rectangle);
}

bool SpCellMap::MoveAbsolute(i32 x, i32 y, bool update) {
  return MoveRelative((i32)((u32)x - (u32)viewport.left), (i32)((u32)y - (u32)viewport.top), update);
}

// Normalize focus before checking initialization and request one higher zoom below four.
// Return the live zoom after the setter's callbacks, including when no change was requested.
i32 SpCellMap::ZoomIn(bool update, i32 focus_x, i32 focus_z) {
  if (focus_x != INT32_MAX && (focus_x < 0 || focus_x >= (i32)actual_width)) {
    focus_z = INT32_MAX;
    focus_x = INT32_MAX;
  }
  if (focus_z != INT32_MAX && (focus_z < 0 || focus_z >= (i32)actual_height)) {
    focus_x = INT32_MAX;
    focus_z = INT32_MAX;
  }
  if (initialized && zoom < 4)
    SetZoom(zoom + 1, update, focus_x, focus_z);
  return zoom;
}

// Normalize focus before checking initialization and request one lower zoom above zero.
i32 SpCellMap::ZoomOut(bool update, i32 focus_x, i32 focus_z) {
  if (focus_x != INT32_MAX && (focus_x < 0 || focus_x >= (i32)actual_width)) {
    focus_z = INT32_MAX;
    focus_x = INT32_MAX;
  }
  if (focus_z != INT32_MAX && (focus_z < 0 || focus_z >= (i32)actual_height)) {
    focus_x = INT32_MAX;
    focus_z = INT32_MAX;
  }
  if (initialized && zoom > 0)
    SetZoom(zoom - 1, update, focus_x, focus_z);
  return zoom;
}

// Request the preceding rotation, wrapping zero to three, without changing the supplied focus.
// Return the requested rotation even if the virtual setter leaves a different live value.
i32 SpCellMap::RotateLeft(bool update, i32 focus_x, i32 focus_z) {
  i32 next = rotation ? (i32)((u32)rotation - 1) : 3;
  SetRotate(next, update, focus_x, focus_z);
  return next;
}

// Request the following rotation, wrapping three to zero, without normalizing focus coordinates.
// Return the requested rotation after the setter rather than re-reading its potentially changed field.
i32 SpCellMap::RotateRight(bool update, i32 focus_x, i32 focus_z) {
  i32 next = rotation == 3 ? 0 : (i32)((u32)rotation + 1);
  SetRotate(next, update, focus_x, focus_z);
  return next;
}

i32 SpCellMap::ScreenCellSizeX() {
  return (i32)cell_width;
}

i32 SpCellMap::ScreenCellAdjustmentY() {
  return (i32)altitude_scale;
}

i32 SpCellMap::ScreenCellSizeZ() {
  return (i32)cell_height;
}

i32 SpCellMap::MicroYToPixelUnits(i32 value, i32 scale) {
  return (i32)((u32)value << scale) >> kCtMicroCoordinateShift;
}

u32 SpCellMap::GetActualGridSizeX() {
  return actual_width;
}

u32 SpCellMap::GetActualGridSizeZ() {
  return actual_height;
}

u32 SpCellMap::GetDrawGridSizeX() {
  return draw_width;
}

u32 SpCellMap::GetDrawGridSizeZ() {
  return draw_height;
}

// Return the borrowed sprite in an actual-grid cell. The original getter relies on valid cell coordinates.
SpSprite *SpCellMap::GetSprite(i32 x, i32 z) {
  return cells[x][z].sprite;
}

// Rotate draw-grid coordinates into the actual table and return its borrowed sprite. Empty cells return null.
SpSprite *SpCellMap::GetSpriteDrawGrid(i32 x, i32 z) {
  DrawGridToActualGrid(x, z, &x, &z);
  return cells[x][z].sprite;
}

bool SpCellMap::IsEmptyDrawGrid(i32 x, i32 z) {
  DrawGridToActualGrid(x, z, &x, &z);
  return cells[x][z].sprite == nullptr;
}

u32 SpCellMap::GetCellSizeOfSprite(i32 x, i32 z) {
  return cells[x][z].size;
}

bool SpCellMap::InCellBounds(i32 x, i32 z) {
  return (u32)x < actual_width && (u32)z < actual_height;
}

u32 SpCellMap::GetAltitudeActualGrid(i32 x, i32 z) {
  return cells[x][z].altitude;
}

// Return the borrowed city drawing surface. Its lifetime remains owned by the cell map.
GxBuffer *SpCellMap::GetCityBufferPtr() {
  return city_buffer;
}

// Intersect the local clip, then translate its origin while restoring the full viewport span.
// Zoom four aligns only the translated left/top coordinates, as in the original method.
void SpCellMap::SetViewClipRect(const ScRect &clip) {
  i32 width = (i32)((u32)viewport.right - (u32)viewport.left);
  i32 height = (i32)((u32)viewport.bottom - (u32)viewport.top);
  if (clip.right <= 0 || clip.left >= width || clip.bottom <= 0 || clip.top >= height)
    world_clip = {0, 0, 0, 0};
  else {
    world_clip.left = clip.left > 0 ? clip.left : 0;
    world_clip.top = clip.top > 0 ? clip.top : 0;
    world_clip.right = clip.right < width ? clip.right : width;
    world_clip.bottom = clip.bottom < height ? clip.bottom : height;
  }
  world_clip.left = (i32)((u32)world_clip.left + (u32)viewport.left);
  world_clip.top = (i32)((u32)world_clip.top + (u32)viewport.top);
  world_clip.right = (i32)((u32)world_clip.left + (u32)width);
  world_clip.bottom = (i32)((u32)world_clip.top + (u32)height);
  if (zoom >= 4) {
    world_clip.left &= ~1;
    world_clip.top &= ~1;
  }
}

// Subtract the live viewport origin from each clip edge. Preserve the original ordered writes for aliased outputs.
void SpCellMap::GetViewClipRect(ScRect *out) const {
  out->left = (i32)((u32)world_clip.left - (u32)viewport.left);
  out->top = (i32)((u32)world_clip.top - (u32)viewport.top);
  out->right = (i32)((u32)world_clip.right - (u32)viewport.left);
  out->bottom = (i32)((u32)world_clip.bottom - (u32)viewport.top);
}

// Change zoom, preserve or recenter the viewport, and rebuild the initialized view in source order.
// An unchanged zoom only performs a requested pending redraw; it does not enter the drawing lock.
void SpCellMap::SetZoom(i32 value, bool update, i32 focus_x, i32 focus_z) {
  if (value == zoom) {
    if (update && pending) {
      UpdateCitySurfaceAll();
      CopyEntireAreaToBackBuffer();
    }
    return;
  }
  if (threaded) {
    drawing_lock.Lock();
    DrainFlags();
  }
  i32 old_zoom = zoom;
  zoom = value;
  cell_width = 8u << value;
  cell_height = 4u << value;
  altitude_scale = 1u << value;
  shift_x = 3 + value;
  shift_y = 2 + value;
  shift_altitude = value;
  half_shift_x = shift_x - 1;
  half_shift_y = shift_y - 1;
  half_shift_altitude = value ? value - 1 : 0;
  u32 width = (u32)viewport.right - (u32)viewport.left;
  u32 height = (u32)viewport.bottom - (u32)viewport.top;
  if (focus_x != INT32_MAX && (focus_x < 0 || focus_x >= (i32)actual_width))
    focus_x = focus_z = INT32_MAX;
  if (focus_z != INT32_MAX && (focus_z < 0 || focus_z >= (i32)actual_height))
    focus_x = focus_z = INT32_MAX;
  ScRect clip;
  if (focus_x == INT32_MAX || focus_z == INT32_MAX) {
    GetViewClipRect(&clip);
    if (zoom >= 4) {
      viewport.left &= ~1;
      viewport.top &= ~1;
    }
    viewport.right = (i32)((u32)viewport.left + width);
    viewport.bottom = (i32)((u32)viewport.top + height);
  } else {
    i32 x, y;
    ActualGridToCityPixel(focus_x, focus_z, &x, &y);
    x = (i32)((u32)x + (u32)((i32)((u32)world_clip.right - (u32)world_clip.left) / -2));
    y = (i32)((u32)y + (u32)((i32)((u32)world_clip.bottom - (u32)world_clip.top) / -2));
    GetViewClipRect(&clip);
    if (zoom >= 4) {
      x &= ~1;
      y &= ~1;
    }
    viewport.left = x;
    viewport.top = y;
    viewport.right = (i32)((u32)x + width);
    viewport.bottom = (i32)((u32)y + height);
  }
  SetViewClipRect(clip);
  if (initialized) {
    drawing_ready = 0;
    RebuildGeometry();
    ClearDirtyRects();
    PropagateLayerUpdate(old_zoom, rotation, zoom, rotation);
    RefreshRegistration(true);
    ScRect previous = {-INT32_MAX, -INT32_MAX, -INT32_MAX, -INT32_MAX};
    Expose(&previous, &viewport, true);
    if (update) {
      UpdateCitySurfaceAll();
      CopyEntireAreaToBackBuffer();
    } else
      pending = 1;
  }
  if (threaded)
    drawing_lock.Unlock();
}

// Change rotation and draw dimensions, optionally recentering on the supplied cell before rebuilding.
// Unlike SetZoom, this method does not validate focus coordinates or reset the clip when focus is absent.
void SpCellMap::SetRotate(i32 value, bool update, i32 focus_x, i32 focus_z) {
  if (value == rotation) {
    if (update && pending) {
      UpdateCitySurfaceAll();
      CopyEntireAreaToBackBuffer();
    }
    return;
  }
  if (threaded) {
    drawing_lock.Lock();
    DrainFlags();
  }
  i32 old_rotation = rotation;
  rotation = value;
  if (value == 0 || value == 2) {
    draw_width = actual_width;
    draw_height = actual_height;
  } else {
    draw_width = actual_height;
    draw_height = actual_width;
  }
  if (focus_x != INT32_MAX && focus_z != INT32_MAX) {
    u32 width = (u32)viewport.right - (u32)viewport.left;
    u32 height = (u32)viewport.bottom - (u32)viewport.top;
    i32 x, y;
    ActualGridToCityPixel(focus_x, focus_z, &x, &y);
    x = (i32)((u32)x + (u32)((i32)((u32)world_clip.right - (u32)world_clip.left) / -2));
    y = (i32)((u32)y + (u32)((i32)((u32)world_clip.bottom - (u32)world_clip.top) / -2));
    ScRect clip;
    GetViewClipRect(&clip);
    if (zoom >= 4) {
      x &= ~1;
      y &= ~1;
    }
    viewport.left = x;
    viewport.top = y;
    viewport.right = (i32)((u32)x + width);
    viewport.bottom = (i32)((u32)y + height);
    SetViewClipRect(clip);
  }
  if (initialized) {
    drawing_ready = 0;
    RebuildGeometry();
    ClearDirtyRects();
    RebuildDepths();
    PropagateLayerUpdate(zoom, old_rotation, zoom, value);
    RefreshRegistration(true);
    ScRect previous = {-INT32_MAX, -INT32_MAX, -INT32_MAX, -INT32_MAX};
    Expose(&previous, &viewport, true);
    if (update) {
      UpdateCitySurfaceAll();
      CopyEntireAreaToBackBuffer();
    } else
      pending = 1;
  }
  if (threaded)
    drawing_lock.Unlock();
}

// Invert the actual camera rotation with the original independent width and height fields.
// Unsupported rotations leave both supplied outputs untouched, and X is always published before Z.
void SpCellMap::DrawGridToActualGrid(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  switch (rotation) {
  case 0:
    *out_x = x;
    *out_z = z;
    break;
  case 1:
    *out_x = z;
    *out_z = (i32)(actual_width - (u32)x - 1u);
    break;
  case 2:
    *out_x = (i32)(actual_width - (u32)x - 1u);
    *out_z = (i32)(actual_height - (u32)z - 1u);
    break;
  case 3:
    *out_x = (i32)(actual_height - (u32)z - 1u);
    *out_z = x;
    break;
  }
}

// Rotate actual cell coordinates into the draw grid. Unsupported rotations leave both output pointers untouched.
void SpCellMap::ActualGridToDrawGrid(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  switch (rotation) {
  case 0:
    *out_x = x;
    *out_z = z;
    break;
  case 1:
    *out_x = (i32)(actual_height - (u32)z - 1);
    *out_z = x;
    break;
  case 2:
    *out_x = (i32)(actual_width - (u32)x - 1);
    *out_z = (i32)(actual_height - (u32)z - 1);
    break;
  case 3:
    *out_x = z;
    *out_z = (i32)(actual_width - (u32)x - 1);
    break;
  }
}

// Project the rotated cell and read its altitude from the actual X-major cell table.
// Preserve X/Y write order and the final half-cell addition, including when the output pointers alias.
void SpCellMap::ActualGridToCityPixel(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  i32 draw_x, draw_z;
  ActualGridToDrawGrid(x, z, &draw_x, &draw_z);
  *out_x = (i32)(((u32)draw_z - (u32)draw_x) << half_shift_x);
  *out_y = (i32)((((u32)draw_x + (u32)draw_z) << half_shift_y) - ((u32)cells[x][z].altitude << shift_altitude));
  *out_x = (i32)((u32)*out_x + cell_width / 2);
}

// Rotate signed micro coordinates with the original independent map dimensions and one-micro-unit edge adjustment.
// Publish X before Z, leaving both outputs untouched for an unsupported rotation.
void SpCellMap::ActualGridToDrawGridMicro(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  switch (rotation) {
  case 0:
    *out_x = x;
    *out_z = z;
    break;
  case 1:
    *out_x = (i32)((actual_height << kCtMicroCoordinateShift) - (u32)z - 1u);
    *out_z = x;
    break;
  case 2:
    *out_x = (i32)((actual_width << kCtMicroCoordinateShift) - (u32)x - 1u);
    *out_z = (i32)((actual_height << kCtMicroCoordinateShift) - (u32)z - 1u);
    break;
  case 3:
    *out_x = z;
    *out_z = (i32)((actual_width << kCtMicroCoordinateShift) - (u32)x - 1u);
    break;
  }
}

void SpCellMap::ActualGridToDrawGridMicroVirtual(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  ActualGridToDrawGridMicro(x, z, out_x, out_z);
}

// Undo micro-coordinate rotation using independent actual dimensions and a one-micro-unit edge subtraction.
// Publish X before Z, including when the supplied outputs share storage.
void SpCellMap::DrawGridToActualGridMicroVirtual(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  switch (rotation) {
  case 0:
    *out_x = x;
    *out_z = z;
    break;
  case 1:
    *out_x = z;
    *out_z = (i32)((actual_width << kCtMicroCoordinateShift) - (u32)x - 1u);
    break;
  case 2:
    *out_x = (i32)((actual_width << kCtMicroCoordinateShift) - (u32)x - 1u);
    *out_z = (i32)((actual_height << kCtMicroCoordinateShift) - (u32)z - 1u);
    break;
  case 3:
    *out_x = (i32)((actual_height << kCtMicroCoordinateShift) - (u32)z - 1u);
    *out_z = x;
    break;
  }
}

// Publish the stored footprint registration cell in X/Z order. The query does not rotate or resolve its sprite.
void SpCellMap::GetRegCellForSprite(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  SpCell *cell = &cells[x][z];
  *out_x = cell->root_x;
  *out_z = cell->root_z;
}

// Resolve the footprint's draw-grid registration corner for the current rotation. Keep the two output stores ordered.
void SpCellMap::GetDrawRegistration(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  SpCell *cell = &cells[x][z];
  *out_x = cell->root_x;
  i32 root_z = cell->root_z;
  *out_z = root_z;
  switch (rotation) {
  case 1: {
    i32 root_x = *out_x;
    *out_x = (i32)(actual_height - (u32)root_z - 1u);
    *out_z = root_x;
    *out_z = (i32)((u32)root_x - cell->size + 1u);
    break;
  }
  case 2:
    *out_x = (i32)(cell->size + actual_width - (u32)*out_x - 2u);
    *out_z = (i32)(actual_height - cell->size - (u32)*out_z);
    break;
  case 3: {
    i32 root_x = *out_x;
    *out_x = *out_z;
    *out_z = (i32)(actual_width - (u32)root_x - 1u);
    *out_x = (i32)((u32)*out_x + cell->size - 1u);
    break;
  }
  }
}

// Convert a sprite-local pixel into micro-grid coordinates at its registration corner. The final altitude uses its
// cached top extent and terrain height after the two horizontal outputs have been written.
void SpCellMap::SpritePixelToMicroActualGrid(
    i32 x, i32 z, u32 pixel_x, u32 pixel_y, i32 *out_x, i32 *out_z, i32 *out_altitude) {
  i32 scale_x = 256 / (i32)cell_width, scale_y = 256 / (i32)altitude_scale;
  SpCell *cell = &cells[x][z];
  i32 top = cell->top;
  GetDrawRegistration(cell->root_x, cell->root_z, &x, &z);
  u32 horizontal = pixel_x * (u32)scale_x;
  SpCellMap::DrawGridToActualGridMicroVirtual((i32)((((u32)x + 1u) << kCtMicroCoordinateShift) - horizontal),
      (i32)(horizontal + ((u32)z << kCtMicroCoordinateShift)), out_x, out_z);
  *out_altitude = (i32)(((u32)cell->altitude << kCtMicroCoordinateShift) + (u32)scale_y * ((u32)top - pixel_y));
}

// Return the footprint width at the current zoom and its cached vertical registration span.
// Both outputs are DWORDs; preserve the first write before loading the two signed registration words.
void SpCellMap::GetPixelSizeOfSprite(i32 x, i32 z, u32 *width, u32 *height) {
  SpCell *cell = &cells[x][z];
  *width = (u32)cell->size << shift_x;
  *height = (u32)((i32)cell->top + cell->bottom);
}

// Project the footprint's registration anchor, accounting for camera rotation, altitude and cached sprite extents.
// Preserve the baseline top store before right/bottom and the final top adjustment.
void SpCellMap::GetCityPixelRectForSprite(i32 x, i32 z, ScRect *out) {
  SpCell *cell = &cells[x][z];
  i32 top = cell->top, bottom = cell->bottom, draw_x = cell->root_x, draw_z = cell->root_z;
  ActualGridToDrawGrid(draw_x, draw_z, &draw_x, &draw_z);
  switch (rotation) {
  case 1:
    draw_z = (i32)((u32)draw_z - cell->size + 1u);
    break;
  case 2:
    draw_x = (i32)((u32)draw_x + cell->size - 1u);
    draw_z = (i32)((u32)draw_z - cell->size + 1u);
    break;
  case 3:
    draw_x = (i32)((u32)draw_x + cell->size - 1u);
    break;
  }
  out->left = (i32)(((u32)draw_z - (u32)draw_x) << half_shift_x);
  i32 baseline = (i32)((((u32)draw_z + (u32)draw_x) << half_shift_y) - ((u32)cell->altitude << shift_altitude));
  out->top = baseline;
  out->right = (i32)((u32)out->left + ((u32)cell->size << shift_x));
  out->bottom = (i32)((u32)baseline + (u32)bottom);
  out->top = (i32)((u32)baseline - (u32)top);
}

bool SpCellMap::InMicroCellBounds(i32 x, i32 z) {
  return (u32)x < (actual_width << kCtMicroCoordinateShift) && (u32)z < (actual_height << kCtMicroCoordinateShift);
}

i32 SpCellMap::GetLowestTerrainAltitude(bool micro_units) const {
  return micro_units ? (i32)((u32)minimum_altitude << kCtMicroCoordinateShift) : minimum_altitude;
}

i32 SpCellMap::GetHighestTerrainAltitude(bool micro_units) const {
  return micro_units ? (i32)((u32)maximum_altitude << kCtMicroCoordinateShift) : maximum_altitude;
}

i32 SpCellMap::GetLowestSpriteHeight(bool) const {
  return (i32)cell_height;
}

// Subtract the cell-height baseline from the cached maximum registration span, clamping a negative result to zero.
// Preserve signed division after the original DWORD shift when ordinary units are requested.
i32 SpCellMap::GetHighestSpriteHeight(bool micro_units) {
  i32 height = (i32)(maximum_span << kCtMicroCoordinateShift);
  if (!micro_units)
    height /= 256;
  return ScIntMax((i32)((u32)height - cell_height), 0);
}

// Derive the sprite's height above its footprint from cached registration words and the live zoom.
// Convert units with signed truncating division before applying the original zero clamp.
i32 SpCellMap::GetHeightOfSprite(i32 x, i32 z, bool micro_units) {
  SpCell *cell = &cells[x][z];
  i32 half_footprint = (i32)(cell_height * cell->size) / 2;
  u32 span = (u32)((i32)cell->top + cell->bottom) - 2u * (u32)half_footprint;
  i32 height = (i32)(span << kCtMicroCoordinateShift) / (i32)(1u << zoom);
  if (!micro_units)
    height /= 256;
  return ScIntMax((i32)((u32)height - cell_height), 0);
}

// Copy the full city-base rectangle, optionally translating each corner into the live window coordinate system.
// Complete the first corner before loading the viewport origin for the second one.
void SpCellMap::GetEntireCityBaseRect(ScRect *out, bool window_units) {
  out->left = city_bounds.left;
  out->top = city_bounds.top;
  out->right = city_bounds.right;
  out->bottom = city_bounds.bottom;
  if (window_units) {
    SpCellMap::CityPixelToWindowPixel_NonInline(out->left, out->top, &out->left, &out->top);
    SpCellMap::CityPixelToWindowPixel_NonInline(out->right, out->bottom, &out->right, &out->bottom);
  }
}

void SpCellMap::ActualGridToWindowPixel_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  ActualGridToCityPixel(x, z, &x, &z);
  SpCellMap::CityPixelToWindowPixel_NonInline(x, z, out_x, out_y);
}

void SpCellMap::CityPixelToWindowPixel_NonInline(i32 x, i32 y, i32 *out_x, i32 *out_y) {
  *out_x = (i32)((u32)x - (u32)viewport.left);
  *out_y = (i32)((u32)y - (u32)viewport.top);
}

void SpCellMap::WindowPixelToCityPixel_NonInline(i32 x, i32 y, i32 *out_x, i32 *out_y) {
  *out_x = (i32)((u32)x + (u32)viewport.left);
  *out_y = (i32)((u32)y + (u32)viewport.top);
}

void SpCellMap::CityPixelToDrawGridNoAltitude_NonInline(i32 x, i32 y, i32 *out_x, i32 *out_z) {
  CityPixelToDrawGridNoAltitude(x, y, out_x, out_z);
}

// Project draw-grid coordinates, then test the rotated actual cell through the original live bounds virtual.
// Only an in-bounds cell contributes altitude, and the Boolean callback can change the live map state.
void SpCellMap::DrawGridToCityPixel_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  *out_x = (i32)(((u32)z - (u32)x) << half_shift_x);
  *out_y = (i32)(((u32)z + (u32)x) << half_shift_y);
  *out_x = (i32)((u32)*out_x + cell_width / 2);
  DrawGridToActualGrid(x, z, &x, &z);
  if (InCellBounds(x, z))
    *out_y = (i32)((u32)*out_y - ((u32)cells[x][z].altitude << shift_altitude));
}

// Project a draw-grid cell center without reading terrain. Keep the final X half-cell adjustment after both outputs.
void SpCellMap::DrawGridToCityPixelNoAltitude_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  *out_x = (i32)(((u32)z - (u32)x) << half_shift_x);
  *out_y = (i32)(((u32)z + (u32)x) << half_shift_y);
  *out_x = (i32)((u32)*out_x + cell_width / 2);
}

void SpCellMap::ActualGridToCityPixel_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  ActualGridToCityPixel(x, z, out_x, out_y);
}

// Rotate the actual cell into local temporaries and project its center without sampling altitude.
// Preserve the signed half-width division used by this particular original entry.
void SpCellMap::ActualGridToCityPixelNoAltitude_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_y) {
  ActualGridToDrawGrid(x, z, &x, &z);
  *out_x = (i32)(((u32)z - (u32)x) << half_shift_x);
  *out_y = (i32)(((u32)z + (u32)x) << half_shift_y);
  *out_x = (i32)((u32)*out_x + (u32)((i32)cell_width / 2));
}

void SpCellMap::DrawGridToActualGrid_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  DrawGridToActualGrid(x, z, out_x, out_z);
}

void SpCellMap::ActualGridToDrawGrid_NonInline(i32 x, i32 z, i32 *out_x, i32 *out_z) {
  ActualGridToDrawGrid(x, z, out_x, out_z);
}

// Rotate actual micro coordinates and project their signed pixel positions with truncation toward zero.
// The original helper writes the unadjusted Y before subtracting altitude and always returns true.
bool SpCellMap::MicroActualGridToCityPixel_NonInline(i32 x, i32 z, i32 altitude, i32 *out_x, i32 *out_y) {
  ActualGridToDrawGridMicro(x, z, &x, &z);
  *out_x = (i32)(((u32)z - (u32)x) << half_shift_x) / 256;
  i32 y = (i32)(((u32)z + (u32)x) << half_shift_y) / 256;
  *out_y = y;
  *out_y = (i32)((u32)y - (u32)((i32)((u32)altitude << shift_altitude) / 256));
  return true;
}

void SpCellMap::Position3DToWindowPixel(i32 x, i32 z, i32 altitude, i32 *out_x, i32 *out_y) {
  SpCellMap::MicroActualGridToCityPixel_NonInline(x, z, altitude, out_x, out_y);
  SpCellMap::CityPixelToWindowPixel_NonInline(*out_x, *out_y, out_x, out_y);
}

// Convert city pixels to the raw draw-grid cell, with signed division before the source arithmetic shifts.
// Preserve output order and live zoom reads when the supplied outputs alias receiver fields.
void SpCellMap::CityPixelToDrawGridNoAltitude(i32 x, i32 y, i32 *out_x, i32 *out_z) {
  u32 vertical = (u32)y + (u32)((i32)cell_height / 2);
  i32 horizontal = (i32)((u32)x + (u32)((i32)cell_width / -2)) / 2;
  *out_x = (i32)(vertical - (u32)horizontal) >> (((u32)zoom + 2u) & 31u);
  *out_z = (i32)(vertical + (u32)horizontal) >> (((u32)zoom + 2u) & 31u);
}

// Invert city-pixel coordinates into micro draw-grid units after the original half-cell offsets and signed division.
// Reload zoom for each output store, preserving aliasing with receiver fields without signed-shift overflow.
void SpCellMap::CityPixelToMicroDrawGridNoAltitude(i32 x, i32 y, i32 *out_x, i32 *out_z) {
  u32 vertical = u32(y) + u32(i32(cell_height) / 2);
  i32 horizontal = i32(u32(x) + u32(i32(cell_width) / -2)) / 2;
  *out_x = i32((vertical - u32(horizontal)) << ((6u - u32(zoom)) & 31u));
  *out_z = i32((vertical + u32(horizontal)) << ((6u - u32(zoom)) & 31u));
}

bool SpCellMap::IsBackgroundTaskEnabled() {
  return threaded != 0;
}

bool SpCellMap::IsBackgroundTaskIdle() {
  return art_requests.size == 0;
}

bool SpCellMap::IsCurrentZoomAndRotationLoaded() {
  return IsZoomAndRotationLoaded(zoom, rotation);
}

// Reject another rotation, otherwise verify every root cell's requested art bit unless the result is already cached.
// Publish the loaded cache only after the complete scan, including an empty actual grid.
bool SpCellMap::IsZoomAndRotationLoaded(i32 requested_zoom, i32 requested_rotation) {
  if (requested_rotation != rotation)
    return false;
  if (residency[requested_zoom] != 2) {
    for (u32 x = 0; x < actual_width; ++x)
      for (u32 z = 0; z < actual_height; ++z)
        if ((cells[x][z].flags & 0x4000u) && !(cells[x][z].flags & (16u << requested_zoom)))
          return false;
    residency[requested_zoom] = 2;
  }
  return true;
}

bool SpCellMap::IsChanged() {
  return pending != 0;
}

u32 SpCellMap::GetUnusedCellCount() {
  return fallback_art_references;
}

bool SpCellMap::IsAbleToModifyOccupants() {
  return draw_total == 0;
}

// Retain source and optional mask before reading the caller's point and issuing the next overlay identifier.
// Preserve append-after-counter-update ordering so ownership callbacks observe the original live state.
u32 SpCellMap::AddOverlayBuffer(const ScPoint &position, GxBuffer *buffer, GxBuffer *mask) {
  buffer->AddRef();
  if (mask)
    mask->AddRef();
  SpOverlay overlay{buffer, mask, position, next_overlay_id++};
  *overlays.Append() = overlay;
  return overlay.id;
}

// Release the first matching overlay's source and then its live mask before erasing that record.
// A missing identifier leaves references, storage and the monotonically issued identifier unchanged.
void SpCellMap::RemoveOverlayBuffer(u32 id) {
  for (u32 i = 0; i < overlays.size; ++i) {
    SpOverlay *overlay = &overlays.data[i];
    if (overlay->id != id)
      continue;
    overlay->buffer->Release();
    if (overlay->mask)
      overlay->mask->Release();
    memmove(overlay, overlay + 1, (usize)(overlays.size - i - 1) * sizeof(*overlay));
    --overlays.size;
    return;
  }
}

// Move only the first matching overlay, reading and writing X before Y in the installed order.
// Missing identifiers produce no geometry or ownership changes.
void SpCellMap::MoveOverlayBuffer(ScPoint *position, u32 id) {
  for (u32 i = 0; i < overlays.size; ++i)
    if (overlays.data[i].id == id) {
      overlays.data[i].position.x = position->x;
      overlays.data[i].position.y = position->y;
      return;
    }
}

// Pan the viewport and rebuild exposure, redirecting background loading when its visible center moves far enough.
// Small requested updates copy the retained surface area and dirty only newly exposed strips; large changes redraw all.
bool SpCellMap::MoveRelative(i32 x, i32 y, bool update) {
  if (zoom == 4) {
    x = (i32)((((u32)x - 1u) | 1u) + 1u);
    y = (i32)((((u32)y - 1u) | 1u) + 1u);
  }
  if (!x && !y) {
    if (update && pending) {
      UpdateCitySurfaceAll();
      CopyEntireAreaToBackBuffer();
    }
    return true;
  }
  drawing_ready = 0;
  ScRect previous = viewport;
  if (follow_target && !following)
    follow_target = nullptr;
  viewport.left = (i32)((u32)viewport.left + (u32)x);
  viewport.right = (i32)((u32)viewport.right + (u32)x);
  viewport.top = (i32)((u32)viewport.top + (u32)y);
  viewport.bottom = (i32)((u32)viewport.bottom + (u32)y);
  world_clip.left = (i32)((u32)world_clip.left + (u32)x);
  world_clip.right = (i32)((u32)world_clip.right + (u32)x);
  world_clip.top = (i32)((u32)world_clip.top + (u32)y);
  world_clip.bottom = (i32)((u32)world_clip.bottom + (u32)y);
  if (threaded && residency[zoom] != 2) {
    i32 center_x, center_z;
    PickActualGrid((i32)((u32)viewport.left + (u32)viewport.right) / 2,
        (i32)((u32)viewport.top + (u32)viewport.bottom) / 2, &center_x, &center_z, false, nullptr, nullptr);
    i64 distance_x = (i32)((u32)center_x - (u32)loader_center_x);
    i64 distance_z = (i32)((u32)center_z - (u32)loader_center_z);
    if (distance_x < 0)
      distance_x = -distance_x;
    if (distance_z < 0)
      distance_z = -distance_z;
    static const i32 thresholds[] = {120, 80, 40, 20, 10};
    if (distance_x + distance_z >= thresholds[zoom]) {
      drawing_lock.Lock();
      DrainFlags();
      if (residency[zoom] != 2) {
        ScRect inner, outer;
        GetVisibleAreaRectangle(&inner, &outer);
        if (!art_requests.size || (art_requests.data[0].operation != 1 && art_requests.data[0].operation != 3))
          *art_requests.Append() = {1, art_sequence++, (u32)zoom, 0};
        loader_center_x = (i32)((u32)inner.right + (u32)inner.left) / 2;
        loader_center_z = (i32)((u32)inner.bottom + (u32)inner.top) / 2;
        loader_zoom = (i8)zoom;
        loader_rotation = (i8)rotation;
        if ((u16)performance_info->memory_mb >= 20) {
          art_requests.data[0].operation = 1;
          loader_bounds = {0, 0, (i32)(actual_width - 1), (i32)(actual_height - 1)};
          loader_spiral.bounds = loader_bounds;
        } else {
          art_requests.data[0].operation = 3;
          static const u32 padding[] = {32, 20, 16, 8, 4};
          outer.left = (i32)((u32)outer.left - padding[zoom]);
          outer.right = (i32)((u32)outer.right + padding[zoom]);
          outer.top = (i32)((u32)outer.top - padding[zoom]);
          outer.bottom = (i32)((u32)outer.bottom + padding[zoom]);
          ScRect bounds = {0, 0, (i32)(actual_width - 1), (i32)(actual_height - 1)};
          outer.IntersectInclusive(&outer, &bounds);
          loader_bounds = outer;
          loader_spiral.bounds = outer;
          RemoveLoadedCheck(zoom, rotation);
          QueueLoadedCheck(zoom, rotation, 1);
        }
        loader_spiral.Seed(loader_center_x, loader_center_z);
        if (work_mode != 2) {
          work_mode = 2;
          drawing_thread.Resume();
        }
      }
      drawing_lock.Unlock();
    }
  }
  Expose(&previous, &viewport, false);
  if (!update) {
    pending = 1;
    return true;
  }
  i32 width = (i32)((u32)viewport.right - (u32)viewport.left);
  i32 height = (i32)((u32)viewport.bottom - (u32)viewport.top);
  i32 threshold_x = (i32)(((u32)width << 7) / 100u);
  i32 threshold_y = (i32)(((u32)height << 7) / 100u);
  i64 magnitude_x = x < 0 ? -(i64)x : x, magnitude_y = y < 0 ? -(i64)y : y;
  if (pending || changed || x < (i32)(0u - (u32)threshold_x) || x > threshold_x || y < (i32)(0u - (u32)threshold_y) ||
      y > threshold_y || magnitude_x >= width || magnitude_y >= height) {
    UpdateCitySurfaceAll();
    CopyEntireAreaToBackBuffer();
    return true;
  }
  ScRect source = {x > 0 ? x : 0, y > 0 ? y : 0, x > 0 ? width : (i32)((u32)width + (u32)x),
      y > 0 ? height : (i32)((u32)height + (u32)y)};
  ScRect destination = {x > 0 ? 0 : (i32)(0u - (u32)x), y > 0 ? 0 : (i32)(0u - (u32)y),
      x > 0 ? (i32)((u32)width - (u32)x) : width, y > 0 ? (i32)((u32)height - (u32)y) : height};
  if (y) {
    ScRect strip = {0, y > 0 ? (i32)((u32)height - (u32)y) : 0, width, y > 0 ? height : (i32)(0u - (u32)y)};
    AddDirtyRectWindowPixelUnits(strip, true, 0);
  }
  if (x) {
    ScRect strip = {x > 0 ? (i32)((u32)width - (u32)x) : 0, y >= 0 ? 0 : (i32)(0u - (u32)y),
        x > 0 ? width : (i32)(0u - (u32)x), y > 0 ? (i32)((u32)height - (u32)y) : height};
    AddDirtyRectWindowPixelUnits(strip, true, 0);
  }
  if (city_buffer->Lock(GxLockFlags::Blt)) {
    city_buffer->BltNoClip(city_buffer, source, destination);
    city_buffer->Unlock(GxLockFlags::Blt);
  }
  UpdateAllDirtyRects(true, true);
  return true;
}

bool SpCellMap::UpdateCitySurface(const ScRect &) {
  return false;
}

// Translate a window rectangle to the city-buffer origin before dispatching the ordinary virtual dirty-rect method.
// Keep bottom before right in the original local rectangle construction.
void SpCellMap::AddDirtyRectWindowPixelUnits(const ScRect &rectangle, bool clip, u32 flags) {
  i32 origin_x = viewport.left, origin_y = viewport.top;
  ScRect translated;
  translated.left = (i32)((u32)origin_x + (u32)rectangle.left);
  translated.top = (i32)((u32)origin_y + (u32)rectangle.top);
  translated.bottom = (i32)((u32)origin_y + (u32)rectangle.bottom);
  translated.right = (i32)((u32)origin_x + (u32)rectangle.right);
  AddDirtyRect(&translated, clip, flags);
}
static i32 SpEvenEdge(i32 value) {
  return (i32)((((u32)value - 1u) | 1u) + 1u);
}

// Erase the four trivial redraw/display-list ranges without freeing capacity, then clear the enabled dirty-cell mask.
void SpCellMap::ClearDirtyRects() {
  dirty_rects.Clear();
  draw_rects.Clear();
  extra_rects.Clear();
  overlay_rects.Clear();
  if (dirty_mask_enabled)
    memset(dirty_mask, 0, dirty_width * dirty_height);
}

// Split oversized requests in column-before-row order, or enqueue their actual rectangle and flags.
// Clipped requests feed the real draw-list boundary, optionally coalescing whole dirty cells through the byte mask.
void SpCellMap::AddDirtyRect(const ScRect *rectangle, bool clip, u32 flags) {
  i32 width = (i32)((u32)rectangle->right - (u32)rectangle->left);
  i32 height = (i32)((u32)rectangle->bottom - (u32)rectangle->top);
  if (changed || !width || !height)
    return;
  if (width > 200 || height > 200) {
    ScRect aligned = *rectangle;
    if (zoom >= 4) {
      aligned.left &= ~1;
      aligned.top &= ~1;
    }
    if (zoom == 4) {
      aligned.right = SpEvenEdge(aligned.right);
      aligned.bottom = SpEvenEdge(aligned.bottom);
    }
    if (width > 200) {
      ScRect part = {aligned.left, aligned.top, (i32)((u32)aligned.left + 200u), aligned.bottom};
      while (part.left < aligned.right) {
        AddDirtyRect(&part, clip, flags);
        part.left = (i32)((u32)part.left + 200u);
        part.right = ScIntMin((i32)((u32)part.left + 200u), aligned.right);
      }
      return;
    }
    if (height > 200) {
      ScRect part = {aligned.left, aligned.top, aligned.right, (i32)((u32)aligned.top + 200u)};
      while (part.top < aligned.bottom) {
        AddDirtyRect(&part, clip, flags);
        part.top = (i32)((u32)part.top + 200u);
        part.bottom = ScIntMin((i32)((u32)part.top + 200u), aligned.bottom);
      }
      return;
    }
  }
  if (!clip) {
    if (dirty_rects.size > 4000) {
      changed = 1;
      ClearDirtyRects();
    } else {
      SpDirtyRect *entry = dirty_rects.Append();
      entry->rectangle = *rectangle;
      entry->flags = flags;
    }
    return;
  }
  if (rectangle->right <= world_clip.left || rectangle->left >= world_clip.right ||
      rectangle->bottom <= world_clip.top || rectangle->top >= world_clip.bottom)
    return;
  ScRect clipped = {ScIntMax(rectangle->left, world_clip.left), ScIntMax(rectangle->top, world_clip.top),
      ScIntMin(rectangle->right, world_clip.right), ScIntMin(rectangle->bottom, world_clip.bottom)};
  if (zoom >= 4) {
    clipped.left &= ~1;
    clipped.top &= ~1;
  }
  if (zoom == 4) {
    clipped.right = SpEvenEdge(clipped.right);
    clipped.bottom = SpEvenEdge(clipped.bottom);
  }
  if (!dirty_mask_enabled) {
    QueueDrawRect(&clipped, flags);
    return;
  }
  u32 first_x = ((u32)clipped.left - (u32)viewport.left) / dirty_cell_width;
  u32 last_x = ((u32)clipped.right - (u32)viewport.left - 1u) / dirty_cell_width;
  u32 first_y = ((u32)clipped.top - (u32)viewport.top) / dirty_cell_height;
  u32 last_y = ((u32)clipped.bottom - (u32)viewport.top - 1u) / dirty_cell_height;
  for (i32 y = (i32)first_y; y <= (i32)last_y; y = (i32)((u32)y + 1u))
    for (i32 x = (i32)first_x; x <= (i32)last_x; x = (i32)((u32)x + 1u)) {
      u8 *marked = &dirty_mask[(u32)x + dirty_width * (u32)y];
      if (*marked)
        continue;
      *marked = 1;
      u32 tile_width = dirty_cell_width, tile_height = dirty_cell_height;
      ScRect cell;
      cell.left = (i32)((u32)viewport.left + (u32)x * tile_width);
      cell.top = (i32)((u32)viewport.top + (u32)y * tile_height);
      cell.right = (i32)((u32)cell.left + tile_width);
      cell.bottom = (i32)((u32)cell.top + tile_height);
      QueueDrawRect(&cell, flags);
    }
}

// Replace queued work with the original eight-by-eight invalidation grid, then request a full redraw.
// The request flag is separate from the pending-display byte and is cleared by the dirty-region consumer.
void SpCellMap::InvalidateEntireScreen() {
  ClearDirtyRects();
  changed = 0;
  i32 width = (i32)((u32)viewport.right - (u32)viewport.left) / 8;
  i32 height = (i32)((u32)viewport.bottom - (u32)viewport.top) / 8;
  u32 row_offset = 0;
  for (u32 row = 0; row < 8; ++row) {
    u32 column_offset = 0;
    for (u32 column = 0; column < 8; ++column) {
      ScRect part;
      part.left = (i32)(column_offset + (u32)viewport.left);
      part.top = (i32)(row_offset + (u32)viewport.top);
      part.right = (i32)((u32)width + (u32)part.left);
      part.bottom = (i32)((u32)height + (u32)part.top);
      AddDirtyRect(&part, false, 0);
      column_offset += (u32)width;
    }
    row_offset += (u32)height;
  }
  changed = 1;
}

// Queue the same full-surface grid, then immediately process both requested dirty-rectangle phases.
void SpCellMap::UpdateCitySurfaceAll() {
  ClearDirtyRects();
  changed = 0;
  i32 width = (i32)((u32)viewport.right - (u32)viewport.left) / 8;
  i32 height = (i32)((u32)viewport.bottom - (u32)viewport.top) / 8;
  u32 row_offset = 0;
  for (u32 row = 0; row < 8; ++row) {
    u32 column_offset = 0;
    for (u32 column = 0; column < 8; ++column) {
      ScRect part;
      part.left = (i32)(column_offset + (u32)viewport.left);
      part.top = (i32)(row_offset + (u32)viewport.top);
      part.right = (i32)((u32)width + (u32)part.left);
      part.bottom = (i32)((u32)height + (u32)part.top);
      AddDirtyRect(&part, false, 0);
      column_offset += (u32)width;
    }
    row_offset += (u32)height;
  }
  UpdateAllDirtyRects(true, true);
}

static i32 SpEdgeSide(const ScPoint *a, const ScPoint *b, i32 x, i32 y) {
  return (i32)(((u32)y - (u32)a->y) * ((u32)b->x - (u32)a->x) - ((u32)x - (u32)a->x) * ((u32)b->y - (u32)a->y));
}

// Test the rectangle's four relevant corners against the city's diamond. Relative rectangles include the live origin.
bool SpCellMap::OutsideCity(const ScRect *rectangle, bool relative) {
  ScRect translated;
  if (relative) {
    translated = {(i32)((u32)rectangle->left + (u32)viewport.left), (i32)((u32)rectangle->top + (u32)viewport.top),
        (i32)((u32)rectangle->right + (u32)viewport.left), (i32)((u32)rectangle->bottom + (u32)viewport.top)};
    rectangle = &translated;
  }
  return SpEdgeSide(&diamond[0], &diamond[1], rectangle->left, rectangle->top) < 0 ||
      SpEdgeSide(&diamond[1], &diamond[2], rectangle->right, rectangle->top) < 0 ||
      SpEdgeSide(&diamond[2], &diamond[3], rectangle->right, rectangle->bottom) < 0 ||
      SpEdgeSide(&diamond[3], &diamond[0], rectangle->left, rectangle->bottom) < 0;
}

// Resolve a multi-cell footprint to its owning cell and adjust its draw anchor for the current rotation.
void SpCellMap::ResolveRoot(SpCell *cell, SpCell **root, i32 *out_x, i32 *out_z) {
  *root = &cells[cell->root_x][cell->root_z];
  ActualGridToDrawGrid(cell->root_x, cell->root_z, out_x, out_z);
  switch (rotation) {
  case 2:
    *out_x = (i32)((u32)*out_x + cell->size - 1u);
    [[fallthrough]];
  case 1:
    *out_z = (i32)((u32)*out_z + 1u - cell->size);
    break;
  case 3:
    *out_x = (i32)((u32)*out_x + cell->size - 1u);
    break;
  }
}

// Reproduce the source's double-store bucket conversion without aliasing a double through an integer pointer.
static i32 SpBucketIndex(i32 coordinate, f32 scale) {
  f64 value = (f64)coordinate * scale - 0.49900001 + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return (i32)((u32)bits + kScDoubleIntegerLowWordBias);
}

// Fill the identical static-record stores shared by both zoom paths. Keep signed footprint edges and native sprite pointers.
static void SpWriteStaticRecord(
    SpCellMap *map, SpDrawRect *record, SpCell *cell, u32 x, u32 y, i32 draw_x, i32 draw_z, const ScRect *clip) {
  record->priority = 1;
  record->kind = cell->flags & 1u;
  record->sprite = cell->flags & (16u << ((u32)map->zoom & 31u)) ? cell->sprite : map->fallback[cell->size];
  record->x = (i16)(x - (u32)map->viewport.left);
  record->y = (i16)(y + (u32)(i32)cell->top - (u32)map->viewport.top);
  record->clip_left = (i16)((u32)clip->left - (u32)map->viewport.left);
  record->clip_top = (i16)((u32)clip->top - (u32)map->viewport.top);
  record->clip_right = (i16)((u32)clip->right - (u32)map->viewport.left);
  record->clip_bottom = (i16)((u32)clip->bottom - (u32)map->viewport.top);
  record->depth = kSpStaticRecordDepthBias - 16384u * ((u32)draw_z + (u32)draw_x) -
      ((u32)(cell->altitude >> 2) << kCtMicroCoordinateShift);
  record->footprint = {(i32)(((u32)cell->root_x - cell->size + 1u) << kCtMicroCoordinateShift),
      (i32)((u32)cell->root_z << kCtMicroCoordinateShift), (i32)(((u32)cell->root_x + 1u) << kCtMicroCoordinateShift),
      (i32)(((u32)cell->root_z + cell->size) << kCtMicroCoordinateShift)};
  if (cell->flags & 2u) {
    record->footprint.left = (i32)((u32)record->footprint.left + 100u);
    record->footprint.right = (i32)((u32)record->footprint.right - 100u);
    record->footprint.top = (i32)((u32)record->footprint.top + 100u);
    record->footprint.bottom = (i32)((u32)record->footprint.bottom - 100u);
  }
}

// Fill the repeated dynamic-record fields after the original registration call. This factors identical inline stores.
static void SpWriteDynamicRecord(
    SpCellMap *map, SpDrawRect *record, SpDynamicSprite *sprite, const ScRect *clip, i32 inflation) {
  record->kind = 2;
  record->dynamic = sprite;
  ScRect registration;
  sprite->sprite->GetRegistration(map->zoom, map->rotation, &registration);
  record->x = (i16)((u32)sprite->rectangle.left + (u32)registration.left - (u32)map->viewport.left);
  record->y = (i16)((u32)registration.top + (u32)sprite->rectangle.top - (u32)map->viewport.top);
  record->clip_left = (i16)((u32)clip->left - (u32)map->viewport.left);
  record->clip_top = (i16)((u32)clip->top - (u32)map->viewport.top);
  record->clip_right = (i16)((u32)clip->right - (u32)map->viewport.left);
  record->clip_bottom = (i16)((u32)clip->bottom - (u32)map->viewport.top);
  record->depth = sprite->depth;
  record->priority = sprite->priority;
  if (sprite->flags & 3u) {
    record->footprint.left = (i32)(((u32)(sprite->x / kCtMicroUnitsPerCell) << kCtMicroCoordinateShift) - 2u);
    record->footprint.top = (i32)(((u32)(sprite->z / kCtMicroUnitsPerCell) << kCtMicroCoordinateShift) - 2u);
    record->footprint.right = (i32)((u32)record->footprint.left + 260u);
    record->footprint.bottom = (i32)((u32)record->footprint.top + 260u);
    if (sprite->flags & 2u) {
      record->footprint.left = (i32)((u32)record->footprint.left + 50u);
      record->footprint.right = (i32)((u32)record->footprint.right - 50u);
      record->footprint.top = (i32)((u32)record->footprint.top + 50u);
      record->footprint.bottom = (i32)((u32)record->footprint.bottom - 50u);
    }
  } else {
    i32 radius = ScIntMax((i32)((u32)inflation * ((u32)sprite->rectangle.right - (u32)sprite->rectangle.left)), 100);
    record->footprint.left = (i32)((u32)sprite->x - (u32)radius);
    record->footprint.top = (i32)((u32)sprite->z - (u32)radius);
    record->footprint.right = (i32)((u32)sprite->x + (u32)radius);
    record->footprint.bottom = (i32)((u32)sprite->z + (u32)radius);
  }
}

// Append clipped static and dynamic records using half-cell anchor spacing, then repair overlap order.
// Temporary visit marks suppress duplicate footprints and are cleared before returning the appended-record count.
u32 SpCellMap::BuildDisplayListLowZoom(const ScRect *rectangle, u32 flags) {
  u32 first = draw_rects.size, count = 0;
  i32 inflation = (i32)(3u * (32u >> ((u32)zoom & 31u)) / 2u);
  ScArray<u16 *> marked_cells;
  if ((!flags || dirty_mask_enabled) && OutsideCity(rectangle, false)) {
    ++draw_total;
    ++count;
    SpDrawRect *record = extra_rects.Append();
    record->kind = 3;
    record->clip_left = (i16)((u32)rectangle->left - (u32)viewport.left);
    record->clip_top = (i16)((u32)rectangle->top - (u32)viewport.top);
    record->clip_right = (i16)((u32)rectangle->right - (u32)viewport.left);
    record->clip_bottom = (i16)((u32)rectangle->bottom - (u32)viewport.top);
  }
  u32 run_count = visible_runs.size;
  for (u32 run = 0; run < run_count; ++run) {
    u32 cells_in_run = visible_runs.data[run].count;
    for (u32 offset = 0; offset < cells_in_run; ++offset) {
      i32 draw_x = (i32)((u32)visible_runs.data[run].x - offset);
      i32 draw_z = (i32)((u32)visible_runs.data[run].z + offset);
      if (!InDrawBounds(draw_x, draw_z))
        continue;
      i32 actual_x, actual_z, root_x = draw_x, root_z = draw_z;
      DrawGridToActualGrid(draw_x, draw_z, &actual_x, &actual_z);
      SpCell *cell = &cells[actual_x][actual_z], *root = cell;
      if (cell->size > 1)
        ResolveRoot(cell, &root, &root_x, &root_z);
      if (root->sprite && !(root->flags & 0x8000u)) {
        *marked_cells.Append() = &root->flags;
        root->flags |= 0x8000u;
        u32 width = (u32)root->size << (((u32)zoom + 3u) & 31u);
        u32 x = ((u32)root_z - (u32)root_x) << (half_shift_x & 31u);
        u32 y = ((u32)root_x + (u32)root_z) << (half_shift_y & 31u);
        y -= ((u32)root->altitude << (shift_altitude & 31u)) + (u32)(i32)root->top;
        ScRect bounds = {(i32)x, (i32)y, (i32)(x + width), (i32)(y + (u32)(i32)root->bottom + (u32)(i32)root->top)},
               clip;
        if (clip.Intersect(rectangle, &bounds)) {
          ++count;
          ++draw_total;
          SpWriteStaticRecord(this, draw_rects.Append(), root, x, y, root_x, root_z, &clip);
        }
      }
      if (!spatial_enabled)
        for (SpDynamicSprite *sprite = cell->dynamic; sprite; sprite = sprite->next) {
          ScRect clip;
          if (clip.Intersect(rectangle, &sprite->rectangle)) {
            ++count;
            ++draw_total;
            SpWriteDynamicRecord(this, draw_rects.Append(), sprite, &clip, inflation);
          }
        }
    }
  }
  for (u32 i = 0; i < marked_cells.size; ++i)
    *marked_cells.data[i] &= ~0x8000u;
  if (spatial_enabled) {
    i32 left = SpBucketIndex((i32)((u32)rectangle->left - (u32)viewport.left), inverse_bucket_width);
    i32 right = SpBucketIndex((i32)((u32)rectangle->right - (u32)viewport.left - 1u), inverse_bucket_width);
    i32 top = SpBucketIndex((i32)((u32)rectangle->top - (u32)viewport.top), inverse_bucket_height);
    i32 bottom = SpBucketIndex((i32)((u32)rectangle->bottom - (u32)viewport.top - 1u), inverse_bucket_height);
    ScArray<SpDynamicSprite *> marked_sprites;
    for (i32 z = top; z <= bottom; ++z)
      for (i32 x = left; x <= right; ++x)
        for (SpSpatialNode *node = spatial_buckets[(u32)x + ((u32)z << (spatial_shift & 31u))]; node;
            node = node->next) {
          SpDynamicSprite *sprite = node->sprite;
          if ((sprite->flags & 0x80u) || node->primary != 1)
            continue;
          sprite->flags |= 0x80u;
          *marked_sprites.Append() = sprite;
          i32 draw_x = 0x8000, draw_z = 0x8000;
          if (flags)
            ActualGridToDrawGrid(sprite->x / 256, sprite->z / 256, &draw_x, &draw_z);
          ScRect clip;
          if (((i32)((u32)draw_x + (u32)draw_z) >= (i32)flags || dirty_mask_enabled) &&
              clip.Intersect(rectangle, &sprite->rectangle)) {
            ++count;
            ++draw_total;
            SpWriteDynamicRecord(this, draw_rects.Append(), sprite, &clip, inflation);
          }
        }
    for (u32 i = 0; i < marked_sprites.size; ++i)
      marked_sprites.data[i]->flags &= ~0x80u;
  }
  SortDrawRecords(first, draw_rects.size - first, true);
  return count;
}

// Build high-zoom records from mixed spatial buckets, using half-cell static anchors and suppressing duplicates.
// Clear both visit-mark sets before sorting the appended range; return the original sort result rather than its count.
u32 SpCellMap::BuildDisplayListHighZoom(const ScRect *rectangle, u32 flags) {
  u32 first = draw_rects.size;
  i32 inflation = (i32)(3u * (32u >> ((u32)zoom & 31u)) / 2u);
  ScArray<SpCell *> marked_cells;
  ScArray<SpDynamicSprite *> marked_sprites;
  if ((!flags || dirty_mask_enabled) && OutsideCity(rectangle, false)) {
    ++draw_total;
    SpDrawRect *record = extra_rects.Append();
    record->kind = 3;
    record->clip_left = (i16)((u32)rectangle->left - (u32)viewport.left);
    record->clip_top = (i16)((u32)rectangle->top - (u32)viewport.top);
    record->clip_right = (i16)((u32)rectangle->right - (u32)viewport.left);
    record->clip_bottom = (i16)((u32)rectangle->bottom - (u32)viewport.top);
  }
  i32 left = SpBucketIndex((i32)((u32)rectangle->left - (u32)viewport.left), inverse_bucket_width);
  i32 right = SpBucketIndex((i32)((u32)rectangle->right - (u32)viewport.left - 1u), inverse_bucket_width);
  i32 top = SpBucketIndex((i32)((u32)rectangle->top - (u32)viewport.top), inverse_bucket_height);
  i32 bottom = SpBucketIndex((i32)((u32)rectangle->bottom - (u32)viewport.top - 1u), inverse_bucket_height);
  for (i32 z = top; z <= bottom; ++z)
    for (i32 x = left; x <= right; ++x)
      for (SpSpatialNode *node = spatial_buckets[(u32)x + ((u32)z << (spatial_shift & 31u))]; node; node = node->next) {
        if (node->primary == 1) {
          SpDynamicSprite *sprite = node->sprite;
          if (sprite->flags & 0x80u)
            continue;
          sprite->flags |= 0x80u;
          *marked_sprites.Append() = sprite;
          i32 draw_x = 0x8000, draw_z = 0x8000;
          if (flags)
            ActualGridToDrawGrid(sprite->x / 256, sprite->z / 256, &draw_x, &draw_z);
          ScRect clip;
          if (((i32)((u32)draw_x + (u32)draw_z) >= (i32)flags || dirty_mask_enabled) &&
              clip.Intersect(rectangle, &sprite->rectangle)) {
            ++draw_total;
            SpWriteDynamicRecord(this, draw_rects.Append(), sprite, &clip, inflation);
          }
        } else {
          SpCell *cell = node->cell;
          if (!cell->sprite || (cell->flags & 0x8000u))
            continue;
          cell->flags |= 0x8000u;
          *marked_cells.Append() = cell;
          i32 draw_x, draw_z;
          if (cell->size > 1 || rotation) {
            SpCell *anchor = &cells[cell->root_x][cell->root_z];
            ActualGridToDrawGrid(anchor->root_x, anchor->root_z, &draw_x, &draw_z);
            if (rotation == 2 || rotation == 3)
              draw_x = (i32)((u32)draw_x + anchor->size - 1u);
            if (rotation == 1 || rotation == 2)
              draw_z = (i32)((u32)draw_z + 1u - anchor->size);
          } else {
            draw_x = cell->root_x;
            draw_z = cell->root_z;
          }
          if ((i32)((u32)draw_x + (u32)draw_z) < (i32)flags && !dirty_mask_enabled)
            continue;
          u32 width = (u32)cell->size << (((u32)zoom + 3u) & 31u);
          u32 pixel_x = ((u32)draw_z - (u32)draw_x) << (half_shift_x & 31u);
          u32 pixel_y = ((u32)draw_x + (u32)draw_z) << (half_shift_y & 31u);
          pixel_y -= ((u32)cell->altitude << (shift_altitude & 31u)) + (u32)(i32)cell->top;
          ScRect bounds = {(i32)pixel_x, (i32)pixel_y, (i32)(pixel_x + width),
                     (i32)(pixel_y + (u32)(i32)cell->bottom + (u32)(i32)cell->top)},
                 clip;
          if (clip.Intersect(rectangle, &bounds)) {
            ++draw_total;
            SpWriteStaticRecord(this, draw_rects.Append(), cell, pixel_x, pixel_y, draw_x, draw_z, &clip);
          }
        }
      }
  for (u32 i = 0; i < marked_sprites.size; ++i)
    marked_sprites.data[i]->flags &= ~0x80u;
  for (u32 i = 0; i < marked_cells.size; ++i)
    marked_cells.data[i]->flags &= ~0x8000u;
  return SortDrawRecords(first, draw_rects.size - first, true);
}

// Project the dynamic record's current micro position and altitude, preserving signed division and ordered Y stores.
i32 *SpCellMap::DynamicToCityPixel(SpDynamicSprite *sprite, i32 *out_x, i32 *out_y) {
  i32 x, z;
  ActualGridToDrawGridMicro(sprite->x, sprite->z, &x, &z);
  *out_x = (i32)(((u32)z - (u32)x + 256u) << (half_shift_x & 31u)) / 256;
  i32 y = (i32)(((u32)x + (u32)z - 256u) << (half_shift_y & 31u)) / 256;
  *out_y = y;
  *out_y = (i32)((u32)y - (u32)((i32)((u32)sprite->altitude << (shift_altitude & 31u)) / 256));
  return out_y;
}

// Refresh the dynamic sprite's projected artwork bounds through its real registration method.
// Preserve left/right-before-top/bottom output order and return the supplied rectangle pointer.
ScRect *SpCellMap::DynamicBounds(SpDynamicSprite *sprite, ScRect *out) {
  i32 x, y;
  ScRect registration;
  DynamicToCityPixel(sprite, &x, &y);
  sprite->sprite->GetRegistration(zoom, rotation, &registration);
  out->left = (i32)((u32)x - (u32)registration.left);
  out->right = (i32)((u32)out->left + (u32)registration.right + (u32)registration.left);
  out->top = (i32)((u32)y - (u32)registration.top);
  out->bottom = (i32)((u32)registration.top + (u32)registration.bottom + (u32)out->top);
  return out;
}

// Search actual cells in X-major, Z-minor and list order, returning the first matching sprite's stored micro position.
// A miss leaves all four caller outputs untouched.
bool SpCellMap::FindDynamicSprite(SpSprite *sprite, i32 *x, i32 *z, i32 *cell_x, u32 *cell_z) {
  for (u32 column = 0; column < actual_width; ++column)
    for (u32 row = 0; row < actual_height; ++row)
      for (SpDynamicSprite *record = cells[column][row].dynamic; record; record = record->next)
        if (record->sprite == sprite) {
          *x = record->x;
          *z = record->z;
          *cell_x = (i32)column;
          *cell_z = row;
          return true;
        }
  return false;
}

// Find the original cell-list link, using the full-map fallback only after the supplied cell misses.
static SpDynamicSprite **SpFindDynamicLink(SpCellMap *map, SpSprite *sprite, i32 *x, i32 *z) {
  i32 column = *x / 256;
  u32 row = (u32)(*z / 256);
  for (;;) {
    SpDynamicSprite **link = &map->cells[column][row].dynamic;
    while (*link && (*link)->sprite != sprite)
      link = &(*link)->next;
    if (*link)
      return link;
    if (!map->FindDynamicSprite(sprite, x, z, &column, &row))
      return nullptr;
  }
}
