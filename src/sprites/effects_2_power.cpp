#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "economy/api.h"
#include "environment/api.h"
#include "geometry/ge_types.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "occupants/api.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "sprites/sp_effect.h"
#include "sprites/sp_tick_manager.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"
#include "terrain/api.h"

// Advance debris frames and ballistic motion, preserving distinct building, ground-bounce and water-splash paths.
// Settled debris hides its shadow and remains registered for the original twenty-second retirement interval.
i32 SpDebris::Tick(u32 delta) {
  elapsed += delta;
  if (settled_at) {
    if (elapsed - settled_at > kSpDebrisSettledLifetime)
      RemoveFromAnimationSystem();
    return 0;
  }
  if (elapsed > frame_elapsed + frame_interval) {
    frame_elapsed = elapsed;
    if (++frame >= frame_count)
      frame = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 offset = i32(u32(frame) * u32(GetW(zoom, 0)));
      origins[zoom].x = offset;
      if (shadow)
        shadow->origins[zoom].x = offset;
    }
  }
  f32 step = f32(f64(delta) * f64(0.001f));
  i32 old_x = SpEffectCoordinate(position.x), old_z = SpEffectCoordinate(position.z);
  i32 old_y = SpEffectCoordinate(position.y);
  position.x = f32(f64(step) * velocity.x + position.x);
  position.z = f32(f64(step) * velocity.z + position.z);
  f64 vertical_speed = f64(velocity.y) - f64(0.0003f) * step;
  velocity.y = f32(vertical_speed);
  f64 new_altitude = vertical_speed * f64(8.94f) * step + position.y;
  position.y = f32(new_altitude);
  i32 x = SpEffectCoordinate(position.x), z = SpEffectCoordinate(position.z), y = SpEffectCoordinate(new_altitude);
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height)) {
    position.x = f32(old_x);
    position.z = f32(old_z);
    RemoveFromAnimationSystem();
    return 0;
  }
  u8 terrain;
  SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &terrain);
  bool test_ground = true;
  if (!hit_building && SpEffects::surface->DoesOccupantExistAtLocation(u32(x / 256), u32(z / 256))) {
    ScOccupant *occupant;
    if (SpEffects::surface->GetOccupantAtCell(u32(x / 256), u32(z / 256), &occupant)) {
      u32 height = occupant->SizeInAnimCellsY();
      occupant->Release();
      if (i32(height + (u32(terrain) << kCtMicroCoordinateShift)) > y) {
        hit_building = 1;
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        velocity.y = f32(f64(velocity.y) * f64(0.2f));
        if (SpEffects::messages)
          SpEffects::messages->MessagePost(ScMessage(kSpMsg_DebrisHitBuilding, reinterpret_cast<uptr>(this),
                                               u32(x) >> kCtMicroCoordinateShift, u32(z) >> kCtMicroCoordinateShift),
              false);
      }
    }
    test_ground = false;
  }
  if (test_ground && y <= (i32(terrain) << kCtMicroCoordinateShift)) {
    if (SpEffects::dirt->IsWater(u32(x / 256), u32(z / 256))) {
      SpSplash *splash = new SpSplash;
      splash->AddRef();
      splash->Init(x, z, y, 0, 0, 0);
      splash->Release();
      position.x = f32(old_x);
      position.z = f32(old_z);
      RemoveFromAnimationSystem();
      return 0;
    }
    position.y = f32(u32(terrain) << kCtMicroCoordinateShift);
    velocity.x = f32(f64(velocity.x) * 0.25);
    velocity.z = f32(f64(velocity.z) * 0.25);
    velocity.y = f32(f64(velocity.y) * f64(-0.4f));
    frame_interval = (24u * frame_interval) >> 4;
    f64 speed = std::fabs(f64(velocity.z)) + std::fabs(f64(velocity.y)) + std::fabs(f64(velocity.x));
    if (!(speed >= f64(0.02f))) {
      gravity = 0.0f;
      velocity.z = 0.0f;
      velocity.y = 0.0f;
      velocity.x = 0.0f;
      settled_at = elapsed;
      if (shadow)
        SpEffects::animated->ShowOccupant(static_cast<SpEffectOccupant *>(shadow), false);
    }
  }
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), old_x, old_z, old_y, x, z, y);
  if (shadow) {
    SpBasicSprite *current = shadow;
    i32 old_altitude = SpEffectCoordinate(current->position.y);
    current->position.x = f32(x);
    current->position.z = f32(z);
    current->position.y = f32(u32(terrain) << kCtMicroCoordinateShift);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(current), old_x, old_z, old_altitude, x, z,
        SpEffectCoordinate(current->position.y));
  }
  return 0;
}

u8 SpWeatherSim::ShowPrecipitation(u8 enabled) {
  return enabled ? AttachPrecipitationToCloud(precipitation_type, 0, 0) : u8(DestroyAllPrecipitation());
}

// Seed a bird trajectory at the selected incoming edge, preserving the independent integer and floating random calls.
// Terrain clearance and the cell map's highest altitude determine its height before real trajectory initialization.
void SpWeatherSim::CreateNewBirdFlock(i32 edge) {
  i32 maximum_altitude = SpEffects::cell_map->GetHighestTerrainAltitude(true);
  ScVec3F point, direction;
  direction.y = 0.0f;
  if (edge) {
    if (SpEffects::random.Bounded(2)) {
      point.x = 4.0f;
      point.z = f32(SpEffects::random.Range(SpEffects::micro_height / 4, SpEffects::micro_height));
    } else {
      point.x = f32(SpEffects::random.Range(SpEffects::micro_width / 4, SpEffects::micro_width));
      point.z = 4.0f;
    }
    direction.x = f32(SpEffects::random.RandomDoubleRangeUniform(0.4f, 0.6f));
    direction.z = f32(SpEffects::random.RandomDoubleRangeUniform(0.4f, 0.6f));
  } else {
    if (SpEffects::random.Bounded(2)) {
      point.x = f32(i32(u32(SpEffects::micro_width) - 4));
      point.z = f32(SpEffects::random.Range(SpEffects::micro_height / 4, SpEffects::micro_height));
    } else {
      point.x = f32(SpEffects::random.Range(SpEffects::micro_width / 4, SpEffects::micro_width));
      point.z = f32(i32(u32(SpEffects::micro_height) - 4));
    }
    direction.x = f32(SpEffects::random.RandomDoubleRangeUniform(-0.4f, -0.6f));
    direction.z = f32(SpEffects::random.RandomDoubleRangeUniform(-0.4f, -0.6f));
  }
  f32 speed = f32(SpEffects::random.RandomDoubleRangeUniform(0.12f, 0.2f));
  SpEffects::Scale2DVector(&direction, speed);
  i32 x = SpEffectCoordinate(point.x), z = SpEffectCoordinate(point.z);
  u8 terrain;
  SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &terrain);
  point.y = f32(ScIntMax(i32(u32(terrain) << kCtMicroCoordinateShift) + 8000, maximum_altitude));
  SpTrajectory *bird = new SpTrajectory;
  bird->AddRef();
  bird->Init(x, z, SpEffectCoordinate(point.y), direction, i32(SpEffects::random.Bounded(3)) + 3);
  bird->Release();
}

// Move at most one due cloud, then maintain precipitation frames, lightning and seasonal bird scheduling.
// Actual animation/disaster calls retain their original ordering, including wrapped-cloud detachment and diagonal fire.
void SpWeatherSim::DoAnimationTick(u32 delta) {
  clock += delta;
  bool precipitation_moved = false;
  SpCloud *removed = nullptr;
  for (u32 index = 0; index < clouds.size; ++index) {
    SpCloud *cloud = clouds.data[index];
    if (clock <= cloud->elapsed + cloud->step_time)
      continue;
    ScVec3F previous = cloud->position;
    f64 step = f64(clock - cloud->elapsed) * f64(0.001f);
    cloud->position.x = f32(step * cloud->velocity.x + cloud->position.x);
    cloud->position.z = f32(step * cloud->velocity.z + cloud->position.z);
    bool inside = SpEffects::Position3DIsIn2DBounds(cloud->position) != 0;
    if (inside || clouds.size <= cloud_limit) {
      if (!inside)
        WrapPositionToOtherSideOfMap(&cloud->position);
      i32 old_x = SpEffectCoordinate(previous.x), old_z = SpEffectCoordinate(previous.z);
      i32 old_y = SpEffectCoordinate(previous.y);
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(cloud), old_x, old_z, old_y,
          SpEffectCoordinate(cloud->position.x), SpEffectCoordinate(cloud->position.z),
          SpEffectCoordinate(cloud->position.y));
      if (cloud->shadow) {
        cloud->shadow->position.x = f32(SpEffectCoordinate(cloud->position.x));
        cloud->shadow->position.z = f32(SpEffectCoordinate(cloud->position.z));
        cloud->shadow->position.y =
            f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(cloud->position.x, cloud->position.z));
        SpBasicSprite *shadow = cloud->shadow;
        SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(shadow), old_x, old_z, old_y,
            i32(i64(shadow->position.x)), i32(i64(shadow->position.z)), i32(i64(shadow->position.y)));
      }
      if (precipitation_cloud == i32(index)) {
        if (inside) {
          ScVec3F old = precipitation->position;
          GetPrecipitationPositionForCloud(index, &precipitation->position);
          SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(precipitation), SpEffectCoordinate(old.x),
              SpEffectCoordinate(old.z), SpEffectCoordinate(old.y), SpEffectCoordinate(precipitation->position.x),
              SpEffectCoordinate(precipitation->position.z), SpEffectCoordinate(precipitation->position.y));
          precipitation_moved = true;
        } else {
          if (SpEffects::animated)
            SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(precipitation),
                SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
                SpEffectCoordinate(precipitation->position.y));
          precipitation_cloud = -1;
        }
      }
      cloud->elapsed = clock;
    } else {
      if (SpEffects::animated)
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(cloud), SpEffectCoordinate(previous.x),
            SpEffectCoordinate(previous.z), SpEffectCoordinate(previous.y));
      if (cloud->shadow) {
        if (SpEffects::animated)
          SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(cloud->shadow),
              SpEffectCoordinate(cloud->shadow->position.x), SpEffectCoordinate(cloud->shadow->position.z),
              SpEffectCoordinate(cloud->shadow->position.y));
        cloud->shadow->Release();
        cloud->shadow = nullptr;
      }
      removed = cloud;
      u32 remaining = cloud->Release();
      if (precipitation_cloud == i32(index)) {
        if (SpEffects::animated)
          SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(precipitation),
              SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
              SpEffectCoordinate(precipitation->position.y));
        precipitation_cloud = -1;
      }
      // The original store after final Release uses freed memory. Preserve the store only for a surviving object.
      if (remaining)
        cloud->elapsed = clock;
    }
    break;
  }
  if (removed)
    for (u32 index = 0; index < clouds.size; ++index)
      if (clouds.data[index] == removed) {
        memmove(clouds.data + index, clouds.data + index + 1, (clouds.size - index - 1) * sizeof(*clouds.data));
        --clouds.size;
        break;
      }
  if (precipitation_flags == 15) {
    if (precipitation_cloud == -1) {
      if (clock > next_precipitation) {
        AttachPrecipitationToCloud(precipitation_type, SpEffects::random.Bounded(clouds.size), clock);
        SetNewPrecipitationToggleTime(clock);
      }
    } else if (clock > next_precipitation) {
      if (SpEffects::animated)
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(precipitation),
            SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
            SpEffectCoordinate(precipitation->position.y));
      precipitation_cloud = -1;
    } else if (clock > precipitation_since + kSpPrecipitationMinimumDelay && clouds.size >= 2 &&
        SpEffects::random.Bounded(10000) <= 20) {
      u32 index, attempts = 0;
      do {
        ++attempts;
        index = SpEffects::random.Bounded(clouds.size);
      } while (precipitation_cloud == i32(index) && attempts < 10);
      AttachPrecipitationToCloud(precipitation_type, index, clock);
    } else if (clock >= precipitation->elapsed + precipitation->step_time) {
      precipitation->elapsed = clock;
      if (++precipitation->phase >= 10)
        precipitation->phase = 0;
      for (i32 zoom = 0; zoom < 5; ++zoom)
        if (precipitation->color[zoom])
          precipitation->origins[zoom].x = i32(u32(precipitation->phase) * u32(precipitation->GetW(zoom, 0)));
      if (!precipitation_moved)
        SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(precipitation),
            SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
            SpEffectCoordinate(precipitation->position.y), 2, nullptr);
    }
  }
  if (lightning->position.y >= f32(kSpVisibleAnimationAltitudeLimit)) {
    if (strike_enabled && precipitation_cloud != -1 && clock > next_lightning) {
      ScVec3F point;
      GetNewLightningStrikePosition(clouds.data[precipitation_cloud]->position, &point);
      lightning->MoveToPosition(point);
      lightning->word204 = 0;
      last_lightning = clock;
      if (SpEffects::box)
        SpEffects::box->Event(kAuEventPlayEffect, kAuEffectThunder, 0, 0, 0);
      if (!SpEffects::random.Bounded(50)) {
        u32 cell = u32(i64(point.x)) >> kCtMicroCoordinateShift;
        if (SpGetApplication()->GetAppPreferences()->SimPrefs()->enable_disasters &&
            cell < u32(SpEffects::micro_width / 256) && cell < u32(SpEffects::micro_height / 256)) {
          ScOccupant *occupant;
          if (SpEffects::surface->GetOccupantAtCell(cell, cell, &occupant)) {
            if (occupant->IsGeomFlag(1024)) {
              DsManager *manager = SpEffects::city->DisasterLayer()->GetDisasterManager(kDsFireTypeId);
              DsFireControl *fire;
              if (manager && manager->QueryInterface(IID_DsFireControl, reinterpret_cast<void **>(&fire))) {
                fire->IgniteCell(cell, cell);
                fire->Release();
              }
            }
            occupant->Release();
          }
        }
      }
    }
  } else {
    lightning->word204 += delta;
    if (lightning->word204 > kSpLightningRetirementAge)
      lightning->HideFromView();
    SetNewLightningStrikeTime(clock);
  }
  if (birds_enabled && clock > next_bird) {
    EnWeatherLayer *weather = SpEffects::city->WeatherLayer();
    i32 season = weather->GetCurrentSeason();
    auto hemisphere = weather->GetHemisphere();
    bool edge = ((season == 3 || season == 0) && hemisphere == EnWeatherLayer::EnHemisphere::Northern) ||
        ((season != 3 && season != 0) && hemisphere != EnWeatherLayer::EnHemisphere::Northern);
    CreateNewBirdFlock(edge ? 4 : 0);
    SetNewCreateBirdFlockTime(clock);
  }
}

// Find the original signed z-then-x insertion position in the native trivial array.
// This replaces only generic sorted-row lookup, without introducing another game service or receiver.
static u32 SpCellIconLowerBound(const ScArray<SpCellIcon> &icons, i32 x, i32 z) {
  u32 first = 0, last = icons.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    const SpCellIcon &entry = icons.data[middle];
    if (entry.z < z || (entry.z == z && entry.x < x))
      first = middle + 1;
    else
      last = middle;
  }
  return first;
}

// Borrow an icon at the requested cell without changing its reference count.
// Missing entries return a null pointer, matching the original sparse lookup's absent-output path.
static SpBasicSprite *SpFindCellIcon(const ScArray<SpCellIcon> &icons, i32 x, i32 z) {
  u32 index = SpCellIconLowerBound(icons, x, z);
  return index < icons.size && icons.data[index].x == x && icons.data[index].z == z ? icons.data[index].sprite
                                                                                    : nullptr;
}

// Store an icon at the original traversal position without another retain or replacement release.
// Preserve the sparse container's two reserved lower keys and overwrite semantics; array growth remains geometric.
static void SpInsertCellIcon(ScArray<SpCellIcon> *icons, i32 x, i32 z, SpBasicSprite *sprite) {
  if (z < INT32_MIN + 2 || x < INT32_MIN + 2)
    return;
  u32 index = SpCellIconLowerBound(*icons, x, z), previous_size = icons->size;
  if (index < previous_size && icons->data[index].x == x && icons->data[index].z == z) {
    icons->data[index].sprite = sprite;
    return;
  }
  icons->Append();
  if (index < previous_size)
    memmove(icons->data + index + 1, icons->data + index, (previous_size - index) * sizeof(*icons->data));
  icons->data[index] = {x, z, sprite};
}

// Remove one cell entry and return its still-owned sprite for the caller's actual cleanup protocol.
// Compaction preserves the sorted callback order of the original collection.
static SpBasicSprite *SpEraseCellIcon(ScArray<SpCellIcon> *icons, i32 x, i32 z) {
  u32 index = SpCellIconLowerBound(*icons, x, z);
  if (index == icons->size || icons->data[index].x != x || icons->data[index].z != z)
    return nullptr;
  SpBasicSprite *sprite = icons->data[index].sprite;
  if (index + 1 < icons->size)
    memmove(icons->data + index, icons->data + index + 1, (icons->size - index - 1) * sizeof(*icons->data));
  --icons->size;
  return sprite;
}

// Snapshot icon records before callback iteration, retaining the original nonowning pointer-copy semantics.
// No icon is retained or released by this native replacement for the generic range enumeration.
static void SpCopyCellIcons(const ScArray<SpCellIcon> &icons, ScArray<SpCellIcon> *out) {
  out->Reserve(icons.size);
  for (u32 index = 0; index < icons.size; ++index)
    *out->Append() = icons.data[index];
}

// Demand the radiation icon for the actual zoom and camera rotation, then copy its registration extents.
// The basic sprite performs the real draw; this method owns only the cache demand.
u8 SpRadIcon::DemandArt(i32 zoom, i32 rotation, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(u32(zoom) + 5 * u32(rotation) - kSpRadioactivityIconKeyOffset, 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  return 1;
}

// Release the original radiation resource whenever the cache exists, even if the stored color pointer is null.
// Clear the pointer after its cache callback and preserve the full-word zero result.
i32 SpRadIcon::ReleaseArt(i32 zoom, i32 rotation) {
  if (SpEffects::images) {
    SpEffects::images->UnprepareImage(u32(zoom) + 5 * u32(rotation) - kSpRadioactivityIconKeyOffset, 0);
    color[zoom] = nullptr;
  }
  return 0;
}

// Choose the neighbor tile's artwork orientation from the camera and stored edge rotation.
// Odd and even camera rotations use the original separate resource groups and modulo-four arithmetic.
u32 SpNeighborIcon::GetSpriteKey(i32 zoom, i32 camera_rotation) {
  if (camera_rotation & 1)
    return ((u32(camera_rotation) - u32(rotation) - 2) & 3) + 4 * u32(zoom) + kSpNeighborOddCameraImageBase;
  return ((UINT32_MAX - u32(rotation) - u32(camera_rotation)) & 3) + 4 * u32(zoom) + kSpNeighborEvenCameraImageBase;
}

// Demand the orientation-specific neighbor tile from original image group twenty.
// The produced color buffer and registration extents remain owned through the cache demand.
u8 SpNeighborIcon::DemandArt(i32 zoom, i32 camera_rotation, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, camera_rotation), 20, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  return 1;
}

// Release the actual neighbor tile's group-twenty cache entry whenever the service exists.
// Preserve unconditional release and callback-before-clear behavior, returning the installed DWORD zero.
i32 SpNeighborIcon::ReleaseArt(i32 zoom, i32 camera_rotation) {
  if (SpEffects::images) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, camera_rotation), 20);
    color[zoom] = nullptr;
  }
  return 0;
}

SpRadiation::SpRadiation() : references(0), visible(1), registered(0) {
}

// Remove active animation and cancel queued target messages before the icon collection releases its storage.
// The original destructor does not replace Shutdown's separate icon-reference cleanup.
SpRadiation::~SpRadiation() {
  RemoveFromAnimationSystem();
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(this);
}

// Publish the primary radiation or adjusted message identity and acquire its virtual reference afterward. Unknown
// identifiers clear the output.
bool SpRadiation::QueryInterface(u32 iid, void **out) {
  if (iid == IID_ScMessageTarget)
    *out = static_cast<ScMessageTarget *>(this);
  else if (iid == IID_ScUnknown || iid == IID_SpRadiationDisplay)
    *out = static_cast<SpRadiationDisplay *>(this);
  else
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

u32 SpRadiation::AddRef() {
  return ++references;
}

// Delete the complete display only when its previous count is one; otherwise decrement with DWORD wrap.
u32 SpRadiation::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Bind the actual current city, optionally publish animation, then subscribe and post the original full refresh message.
// The primary and message receivers retain their separate compiler-adjusted identities.
u8 SpRadiation::Init(u8 post) {
  SpEffects::city = SpGetCity();
  if (post)
    AddToAnimationSystem();
  SpGetMessageServer()->AddNotification(this, kTrMsg_TerrainChanged);
  SpGetMessageServer()->AddNotification(this, kEcMsg_RadioactivityChanged);
  SpGetMessageServer()->MessagePost(ScMessage(kEcMsg_RadioactivityChanged, 0, UINT32_MAX, 0), false);
  return 1;
}

// Remove both subscriptions and animation membership before releasing the stored icon ownership references.
// The callback snapshot preserves the original traversal even if layer callbacks mutate the collection.
void SpRadiation::Shutdown() {
  SpGetMessageServer()->RemoveNotification(this, kTrMsg_TerrainChanged);
  SpGetMessageServer()->RemoveNotification(this, kEcMsg_RadioactivityChanged);
  RemoveFromAnimationSystem();
  ScArray<SpCellIcon> snapshot;
  SpCopyCellIcons(icons, &snapshot);
  for (u32 index = 0; index < snapshot.size; ++index)
    snapshot.data[index].sprite->Release();
  icons.Clear();
}

// Store the complete visibility byte before publishing or removing actual animation membership.
void SpRadiation::ShowOrHide(u8 enabled) {
  if (visible == enabled)
    return;
  visible = enabled;
  if (enabled)
    AddToAnimationSystem();
  else
    RemoveFromAnimationSystem();
}

void SpRadiation::GetIconPositionFromCell(u32 x, u32 z, u32 *icon_x, u32 *icon_z) {
  *icon_x = (x << kCtMicroCoordinateShift) + 128;
  *icon_z = (z << kCtMicroCoordinateShift) + 128;
}

// Maintain the real per-cell radiation icon ownership, moving an existing visible icon or constructing a missing one.
// Removal erases the cell entry before layer removal and the final ownership release.
void SpRadiation::UpdateCell(u32 x, u32 z, u8 radiation) {
  if (radiation) {
    SpBasicSprite *sprite = SpFindCellIcon(icons, i32(x), i32(z));
    if (sprite) {
      if (registered) {
        u32 px, pz;
        u8 terrain;
        GetIconPositionFromCell(x, z, &px, &pz);
        SpEffects::dirt->GetVertexAltitude(px / 256, pz / 256, &terrain);
        ScVec3F previous = sprite->position;
        sprite->position = {f32(px), f32(u32(terrain) << kCtMicroCoordinateShift), f32(pz)};
        SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(sprite), SpEffectCoordinate(previous.x),
            SpEffectCoordinate(previous.z), SpEffectCoordinate(previous.y), SpEffectCoordinate(sprite->position.x),
            SpEffectCoordinate(sprite->position.z), SpEffectCoordinate(sprite->position.y));
      }
    } else {
      sprite = ScAddRefNotNull(new SpRadIcon);
      SpInsertCellIcon(&icons, i32(x), i32(z), sprite);
      if (registered) {
        u32 px, pz;
        u8 terrain;
        GetIconPositionFromCell(x, z, &px, &pz);
        SpEffects::dirt->GetVertexAltitude(px / 256, pz / 256, &terrain);
        sprite->position = {f32(px), f32(u32(terrain) << kCtMicroCoordinateShift), f32(pz)};
        SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite),
            SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
            SpEffectCoordinate(sprite->position.y), 3, 2, true);
      }
    }
  } else {
    SpBasicSprite *sprite = SpEraseCellIcon(&icons, i32(x), i32(z));
    if (sprite) {
      if (registered && SpEffects::animated)
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
            SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
            SpEffectCoordinate(sprite->position.y));
      sprite->Release();
    }
  }
}

// Refresh all even-coordinate radiation cells when either selector is all-ones, or align one requested cell downward.
// The actual pollution service supplies the radiation predicate; no synthetic simulation callback is added.
u8 SpRadiation::UpdateRadiationDisplay(const EcLayer *pollution, u32 x, u32 z) {
  if (!pollution)
    pollution = SpEffects::city->PollutionLayer();
  if (x == UINT32_MAX || z == UINT32_MAX) {
    for (u32 px = 0; px < u32(SpEffects::micro_width / 256); px += 2)
      for (u32 pz = 0; pz < u32(SpEffects::micro_height / 256); pz += 2)
        UpdateCell(px, pz, pollution->IsRadioactive(px, pz));
  } else {
    x &= ~1u;
    z &= ~1u;
    UpdateCell(x, z, pollution->IsRadioactive(x, z));
  }
  return 1;
}

// Publish active membership before inserting the current sorted icon snapshot into animation group two.
// Snapshotting keeps callback-driven changes from altering this original traversal.
void SpRadiation::AddToAnimationSystem() {
  if (registered || !visible)
    return;
  registered = 1;
  ScArray<SpCellIcon> snapshot;
  SpCopyCellIcons(icons, &snapshot);
  for (u32 index = 0; index < snapshot.size; ++index) {
    SpBasicSprite *sprite = snapshot.data[index].sprite;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite), SpEffectCoordinate(sprite->position.x),
        SpEffectCoordinate(sprite->position.z), SpEffectCoordinate(sprite->position.y), 2, 2, true);
  }
}

// Clear active membership before removing the sorted snapshot from the real animation layer.
// The cell collection retains its icon references for later visibility changes.
void SpRadiation::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  ScArray<SpCellIcon> snapshot;
  SpCopyCellIcons(icons, &snapshot);
  for (u32 index = 0; index < snapshot.size; ++index) {
    SpBasicSprite *sprite = snapshot.data[index].sprite;
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
          SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
          SpEffectCoordinate(sprite->position.y));
  }
}

// Reposition an existing active radiation icon after a terrain-height change.
// The old point is captured after the original terrain query and remains distinct from the new layer coordinates.
void SpRadiation::AdjustCellIconIfItHasRadiation(u32 x, u32 z) {
  if (!registered)
    return;
  SpBasicSprite *sprite = SpFindCellIcon(icons, i32(x), i32(z));
  if (!sprite)
    return;
  u32 px, pz;
  u8 terrain;
  GetIconPositionFromCell(x, z, &px, &pz);
  SpEffects::dirt->GetVertexAltitude(px / 256, pz / 256, &terrain);
  ScVec3F previous = sprite->position;
  sprite->position = {f32(px), f32(u32(terrain) << kCtMicroCoordinateShift), f32(pz)};
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(sprite), SpEffectCoordinate(previous.x),
      SpEffectCoordinate(previous.z), SpEffectCoordinate(previous.y), SpEffectCoordinate(sprite->position.x),
      SpEffectCoordinate(sprite->position.z), SpEffectCoordinate(sprite->position.y));
}

// Route radiation refreshes and terrain changes through the original target-message and cell-update protocols.
// Terrain notifications post only even-coordinate cells while the display is active, preserving x-then-z post order.
bool SpRadiation::DoMessage(ScMessage *message) {
  if (message->type == kTrMsg_TerrainChanged) {
    auto rectangle = reinterpret_cast<const ScRect *>(message->data1);
    if (registered)
      for (i32 x = rectangle->left; x <= rectangle->right; ++x)
        if (!(u32(x) & 1))
          for (i32 z = rectangle->top; z <= rectangle->bottom; ++z)
            if (!(u32(z) & 1) && SpEffects::messages)
              SpEffects::messages->GeneralMessagePostToTarget(
                  ScMessage(kSpMsg_RefreshRadiationCellIcon, u32(x), u32(z), 0), this);
  } else if (message->type == kEcMsg_RadioactivityChanged) {
    if (message->data1)
      UpdateRadiationDisplay(
          reinterpret_cast<const EcLayer *>(message->data1), u32(message->data2) >> 16, u16(message->data2));
    else
      UpdateRadiationDisplay(nullptr, UINT32_MAX, UINT32_MAX);
  } else if (message->type == kSpMsg_RefreshRadiationCellIcon) {
    AdjustCellIconIfItHasRadiation(u32(message->data1), u32(message->data2));
  }
  return true;
}

SpNeighbors::SpNeighbors() : references(0), visible(1), registered(0), geom_mask(0) {
}

// Remove active icons and cancel queued target messages before the native cell storage is destroyed.
// This preserves the original distinction between destruction and explicit Shutdown.
SpNeighbors::~SpNeighbors() {
  RemoveFromAnimationSystem();
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(this);
}

// Publish the original neighbor-display primary or adjusted message-target identity before retaining it. Unsupported
// identifiers clear the output and return false.
bool SpNeighbors::QueryInterface(u32 iid, void **out) {
  if (iid == IID_ScMessageTarget)
    *out = static_cast<ScMessageTarget *>(this);
  else if (iid == IID_ScUnknown || iid == IID_SpNeighborDisplay)
    *out = static_cast<SpNeighborDisplay *>(this);
  else
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

u32 SpNeighbors::AddRef() {
  return ++references;
}

// Destroy the complete neighbor display only when its previous count is one.
// Otherwise preserve the original DWORD decrement, including wrap from an initially unretained object.
u32 SpNeighbors::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Bind the borrowed manager and original layer-specific notification IDs and geometry flag.
// The post byte controls initial publication without inventing another manager acquisition or result wrapper.
u8 SpNeighbors::Init(ScOccupantManager *source, i32 value, u8 post) {
  manager = source;
  layer = value;
  switch (value) {
  case 0:
    geom_mask = 0;
    inserted_message = kOcMsg_SurfaceInserted;
    removed_message = kOcMsg_SurfaceRemoved;
    changed_message = kOcMsg_SurfaceChanged;
    break;
  case 1:
    inserted_message = kOcMsg_PipeInserted;
    removed_message = kOcMsg_PipeRemoved;
    changed_message = kOcMsg_PipeChanged;
    geom_mask = kOcGeomPipe;
    break;
  case 2:
    inserted_message = kOcMsg_SubwayInserted;
    removed_message = kOcMsg_SubwayRemoved;
    changed_message = kOcMsg_SubwayChanged;
    geom_mask = kOcGeomSubway;
    break;
  }
  if (post)
    AddToAnimationSystem();
  return 1;
}

// Remove published icons before releasing any ownership remaining in an inactive cell collection.
// The original ordered snapshot prevents callback mutations from changing this cleanup traversal.
void SpNeighbors::Shutdown() {
  RemoveFromAnimationSystem();
  ScArray<SpCellIcon> snapshot;
  SpCopyCellIcons(icons, &snapshot);
  for (u32 index = 0; index < snapshot.size; ++index)
    snapshot.data[index].sprite->Release();
  icons.Clear();
}

// Store the complete visibility byte before publishing or retiring the actual neighbor icons.
void SpNeighbors::ShowOrHide(u8 enabled) {
  if (visible == enabled)
    return;
  visible = enabled;
  if (enabled)
    AddToAnimationSystem();
  else
    RemoveFromAnimationSystem();
}

// Publish membership before subscribing to the three layer notifications, then perform the original full refresh.
// Unlike radiation icons, the neighbor collection is rebuilt by UpdateAll when animation becomes active.
void SpNeighbors::AddToAnimationSystem() {
  if (registered || !visible)
    return;
  registered = 1;
  SpGetMessageServer()->AddNotification(this, inserted_message);
  SpGetMessageServer()->AddNotification(this, removed_message);
  SpGetMessageServer()->AddNotification(this, changed_message);
  UpdateAll();
}

// Clear membership and subscriptions before removing and releasing the original sorted icon snapshot.
// Neighbor visibility teardown clears its collection, unlike the radiation display's retained hidden icons.
void SpNeighbors::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  SpGetMessageServer()->RemoveNotification(this, inserted_message);
  SpGetMessageServer()->RemoveNotification(this, removed_message);
  SpGetMessageServer()->RemoveNotification(this, changed_message);
  ScArray<SpCellIcon> snapshot;
  SpCopyCellIcons(icons, &snapshot);
  for (u32 index = 0; index < snapshot.size; ++index) {
    SpBasicSprite *sprite = snapshot.data[index].sprite;
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
          SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
          SpEffectCoordinate(sprite->position.y));
    sprite->Release();
  }
  icons.Clear();
}

// Retain occupants from real layer notifications before deferring their processing to this display.
// Targeted follow-up messages consume those exact holds through the immediate insertion/removal/change paths.
bool SpNeighbors::DoMessage(ScMessage *message) {
  auto occupant = reinterpret_cast<ScOccupant *>(message->data1);
  if (message->type == inserted_message) {
    if (occupant) {
      occupant->AddRef();
      OccupantInserted(occupant, 1);
    }
  } else if (message->type == removed_message) {
    if (occupant) {
      occupant->AddRef();
      OccupantRemoved(occupant, 1);
    }
  } else if (message->type == changed_message) {
    if (occupant) {
      occupant->AddRef();
      OccupantChanged(occupant, 1);
    }
  } else if (message->type == kSpMsg_NeighborOccupantInserted) {
    OccupantInserted(occupant, 0);
  } else if (message->type == kSpMsg_NeighborOccupantRemoved) {
    OccupantRemoved(occupant, 0);
  } else if (message->type == kSpMsg_NeighborOccupantChanged) {
    OccupantChanged(occupant, 0);
  }
  return true;
}

// Determine the original edge orientation, preferring exact world edges before the surrounding water-count heuristic.
// Position and top altitude are published through their separate outputs with the original width-based edge quirk.
void SpNeighbors::GetIconPositionFromOccupant(
    ScOccupant *occupant, SpNeighborIcon::Rotation *rotation, u32 *x, u32 *z, u32 *y) {
  u32 width = occupant->SizeInCellsX();
  occupant->GetLocationInCellCoords(x, z);
  u32 height = occupant->SizeInCellsY();
  *y = height + occupant->GetAlt() - 1;
  if (!*x) {
    *rotation = SpNeighborIcon::Rotation(0);
  } else if (!*z) {
    *rotation = SpNeighborIcon::Rotation(3);
  } else if (*x + width == u32(SpEffects::micro_width) >> kCtMicroCoordinateShift) {
    *rotation = SpNeighborIcon::Rotation(2);
  } else if (*z + width == u32(SpEffects::micro_height) >> kCtMicroCoordinateShift) {
    *rotation = SpNeighborIcon::Rotation(1);
  } else {
    u32 water[3][3];
    for (u32 px = 0; px < 3; ++px)
      for (u32 pz = 0; pz < 3; ++pz)
        water[px][pz] = SpEffects::dirt->IsWater(*x + px - 1, *z + pz - 1);
    u32 scores[4] = {water[0][0] + water[0][2] + water[0][1], water[0][2] + water[2][2] + water[1][2],
        water[2][2] + water[2][0] + water[2][1], water[0][0] + water[2][0] + water[1][0]};
    u32 highest = 0;
    *rotation = SpNeighborIcon::Rotation(3);
    for (u32 index = 0; index < 4; ++index)
      if (scores[index] > highest) {
        highest = scores[index];
        *rotation = SpNeighborIcon::Rotation(index);
      }
  }
  *x <<= 8;
  *z <<= 8;
  *y <<= 8;
  *x += width << 7;
  *z += width << 7;
}

// Check the occupant footprint for the actual surface, pipe or subway neighbor-connection kinds.
// The original location outputs remain the footprint root while temporary coordinates walk its cells.
u8 SpNeighbors::DoesOccupantConnectToNeighbor(ScOccupant *occupant, u32 *x, u32 *z, i32 value) {
  occupant->GetLocationInCellCoords(x, z);
  u32 end_x = *x + occupant->SizeInCellsX(), end_z = *z + occupant->SizeInCellsZ();
  for (u32 px = *x; px < end_x; ++px)
    for (u32 pz = *z; pz < end_z; ++pz) {
      if (value == 0) {
        if (SpEffects::neighbors->HasConnection(1, i32(px), i32(pz)) ||
            SpEffects::neighbors->HasConnection(2, i32(px), i32(pz)) ||
            SpEffects::neighbors->HasConnection(3, i32(px), i32(pz)) ||
            SpEffects::neighbors->HasConnection(4, i32(px), i32(pz)) ||
            SpEffects::neighbors->HasConnection(7, i32(px), i32(pz)))
          return 1;
      } else if (value == 1) {
        if (SpEffects::neighbors->HasConnection(5, i32(px), i32(pz)))
          return 1;
      } else if (value == 2 && SpEffects::neighbors->HasConnection(6, i32(px), i32(pz))) {
        return 1;
      }
    }
  return 0;
}

// Return the actual manager-acquired occupant pointer only when the cell lookup reports success.
// The caller receives the original retained reference and must release it through its own operation's cleanup.
ScOccupant *SpNeighbors::GetOccupantForCell(u32 x, u32 z) {
  ScOccupant *occupant = nullptr;
  return manager->GetOccupantAtCell(x, z, &occupant) ? occupant : nullptr;
}

// Defer insertion with its existing occupant hold, or create the missing connected-cell icon immediately.
// Immediate processing releases the original hold even if no neighbor connection exists.
u8 SpNeighbors::OccupantInserted(ScOccupant *occupant, u8 post) {
  if (!occupant)
    return 1;
  if (post) {
    if (SpEffects::messages)
      SpEffects::messages->GeneralMessagePostToTarget(
          ScMessage(kSpMsg_NeighborOccupantInserted, reinterpret_cast<uptr>(occupant), 0, 0), this);
  } else {
    u32 x, z;
    if (DoesOccupantConnectToNeighbor(occupant, &x, &z, layer) && !SpFindCellIcon(icons, i32(x), i32(z))) {
      auto sprite = new SpNeighborIcon;
      sprite->AddRef();
      sprite->SetGeomFlag(geom_mask);
      SpInsertCellIcon(&icons, i32(x), i32(z), sprite);
      if (registered) {
        u32 px, pz, py;
        GetIconPositionFromOccupant(occupant, &sprite->rotation, &px, &pz, &py);
        sprite->position = {f32(px), f32(py), f32(pz)};
        SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite),
            SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
            SpEffectCoordinate(sprite->position.y), 3, 2, true);
      }
    }
    occupant->Release();
  }
  return 1;
}

// Defer removal or retire the matching cell icon while it is still visible to layer callbacks in the collection.
// Erase follows layer removal, then icon ownership and the incoming occupant hold are released in order.
u8 SpNeighbors::OccupantRemoved(ScOccupant *occupant, u8 post) {
  if (!occupant)
    return 1;
  if (post) {
    if (SpEffects::messages)
      SpEffects::messages->GeneralMessagePostToTarget(
          ScMessage(kSpMsg_NeighborOccupantRemoved, reinterpret_cast<uptr>(occupant), 0, 0), this);
  } else {
    u32 x, z;
    occupant->GetLocationInCellCoords(&x, &z);
    SpBasicSprite *sprite = SpFindCellIcon(icons, i32(x), i32(z));
    if (sprite) {
      if (registered && SpEffects::animated)
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
            SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
            SpEffectCoordinate(sprite->position.y));
      SpEraseCellIcon(&icons, i32(x), i32(z));
      sprite->Release();
    }
    occupant->Release();
  }
  return 1;
}

// Maintain connection-dependent icon existence after an occupant change, preserving unchanged icons without a new move.
// Deferred calls transport the existing hold; immediate calls release it after the original create/remove protocol.
u8 SpNeighbors::OccupantChanged(ScOccupant *occupant, u8 post) {
  if (!occupant)
    return 1;
  if (post) {
    if (SpEffects::messages)
      SpEffects::messages->GeneralMessagePostToTarget(
          ScMessage(kSpMsg_NeighborOccupantChanged, reinterpret_cast<uptr>(occupant), 0, 0), this);
  } else {
    u32 x, z;
    if (DoesOccupantConnectToNeighbor(occupant, &x, &z, layer)) {
      if (!SpFindCellIcon(icons, i32(x), i32(z))) {
        auto sprite = new SpNeighborIcon;
        sprite->AddRef();
        sprite->SetGeomFlag(geom_mask);
        SpInsertCellIcon(&icons, i32(x), i32(z), sprite);
        if (registered) {
          u32 px, pz, py;
          GetIconPositionFromOccupant(occupant, &sprite->rotation, &px, &pz, &py);
          sprite->position = {f32(px), f32(py), f32(pz)};
          SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite),
              SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
              SpEffectCoordinate(sprite->position.y), 3, 2, true);
        }
      }
    } else {
      occupant->GetLocationInCellCoords(&x, &z);
      SpBasicSprite *sprite = SpFindCellIcon(icons, i32(x), i32(z));
      if (sprite) {
        if (registered && SpEffects::animated)
          SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
              SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
              SpEffectCoordinate(sprite->position.y));
        SpEraseCellIcon(&icons, i32(x), i32(z));
        sprite->Release();
      }
    }
    occupant->Release();
  }
  return 1;
}

// Rescan cells in original x-then-z order, creating connected root icons and retiring stale/nonroot cell entries.
// Each successful manager lookup supplies the reference released after that cell's complete processing.
void SpNeighbors::UpdateAll() {
  u32 width = u32(SpEffects::micro_width / 256);
  for (u32 x = 0; x < width; ++x) {
    u32 height = u32(SpEffects::micro_height / 256);
    for (u32 z = 0; z < height; ++z) {
      ScOccupant *occupant = GetOccupantForCell(x, z);
      if (!occupant)
        continue;
      u32 root_x, root_z;
      occupant->GetLocationInCellCoords(&root_x, &root_z);
      bool remove = true;
      if (root_x == x && root_z == z && DoesOccupantConnectToNeighbor(occupant, &root_x, &root_z, layer)) {
        auto sprite = new SpNeighborIcon;
        sprite->AddRef();
        sprite->SetGeomFlag(geom_mask);
        SpInsertCellIcon(&icons, i32(root_x), i32(root_z), sprite);
        if (registered) {
          u32 px, pz, py;
          GetIconPositionFromOccupant(occupant, &sprite->rotation, &px, &pz, &py);
          sprite->position = {f32(px), f32(py), f32(pz)};
          SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite),
              SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
              SpEffectCoordinate(sprite->position.y), 3, 2, true);
          remove = false;
        }
      }
      if (remove) {
        SpBasicSprite *sprite = SpEraseCellIcon(&icons, i32(x), i32(z));
        if (sprite) {
          if (SpEffects::animated)
            SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite),
                SpEffectCoordinate(sprite->position.x), SpEffectCoordinate(sprite->position.z),
                SpEffectCoordinate(sprite->position.y));
          sprite->Release();
        }
      }
      occupant->Release();
    }
  }
}

SpPowerIcon::SpPowerIcon()
    : occupant(nullptr), registered(0), frame(0), frame_step(1), elapsed(0), next_blink(UINT32_MAX) {
}

SpPowerIcon::~SpPowerIcon() {
  RemoveFromAnimationSystem();
}

// Load all five registration rectangles before inserting the icon into animation group three.
// The first blink deadline is drawn from the original shared random stream.
u8 SpPowerIcon::AddToAnimationSystem() {
  if (registered)
    return 0;
  registered = 1;
  SetSpanRects();
  next_blink = u32(SpEffects::random.RandomSint32RangeGaussian(i32(blink_time), i32(2 * blink_time)));
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 3, 2, true);
  return 1;
}

// Clear membership and the borrowed occupant before removing the sprite from the animation layer.
// An already detached icon has the original zero result.
u8 SpPowerIcon::RemoveFromAnimationSystem() {
  if (!registered)
    return 0;
  registered = 0;
  occupant = nullptr;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
  return 1;
}

// Step at most one frame after the strict blink deadline, bouncing between three horizontal frames.
// Clock wrap chooses another deadline; active images and the actual animation layer receive the frame change.
i32 SpPowerIcon::Tick(u32 delta) {
  if (registered) {
    if (elapsed + delta < elapsed)
      next_blink = u32(SpEffects::random.RandomSint32RangeGaussian(i32(blink_time), i32(2 * blink_time)));
    elapsed += delta;
    i32 overdue = i32(elapsed - next_blink - blink_time);
    if (overdue > 0) {
      if (overdue > i32(blink_time))
        overdue = i32(u32(overdue) % blink_time);
      next_blink = elapsed - u32(overdue);
      frame += frame_step;
      if (frame >= 3) {
        frame -= 2;
        frame_step = -frame_step;
      } else if (frame < 0) {
        frame += 2;
        frame_step = -frame_step;
      }
      for (i32 zoom = 0; zoom < 5; ++zoom)
        if (color[zoom])
          origins[zoom].x = frame * GetW(zoom, 0);
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
          SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 0, nullptr);
    }
  }
  return 0;
}

void SpPowerIcon::MoveToNewPosition(i32 x, i32 z, i32 y, u8 insert, u8 reinsert) {
  if (registered) {
    i32 old_x = SpEffectCoordinate(position.x), old_z = SpEffectCoordinate(position.z);
    i32 old_y = SpEffectCoordinate(position.y);
    if (reinsert) {
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), old_x, old_z, old_y);
      SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 1, 0, true);
    } else
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), old_x, old_z, old_y, x, z, y);
    position.x = f32(x);
    position.z = f32(z);
    position.y = f32(y);
  } else {
    position.x = f32(x);
    position.z = f32(z);
    position.y = f32(y);
    if (insert)
      AddToAnimationSystem();
  }
}

// Recompute an active icon's point from its occupant or from the terrain of its bare zoned cell.
void SpPowerIcon::UpdatePosition(i32 zoom, i32 rotation) {
  if (!registered)
    return;
  i32 x, z, y;
  if (uptr(occupant) == 1) {
    u8 altitude;
    SpEffects::dirt->GetVertexAltitude(cell_x, cell_z, &altitude);
    x = i32((cell_x << kCtMicroCoordinateShift) + 128);
    z = i32((cell_z << kCtMicroCoordinateShift) + 128);
    y = i32(u32(altitude) << kCtMicroCoordinateShift);
  } else
    GetPositionForOccupant(occupant, zoom, rotation, &x, &z, &y);
  MoveToNewPosition(x, z, y, 1, 0);
}

// Transfer a pooled icon to an occupant or bare zoned cell and synchronize its three layer geometry bits.
// Geometry changes request remove/reinsert; clearing the association retires animation membership first.
void SpPowerIcon::MoveToNewOccupant(ScOccupant *source, i32 zoom, i32 rotation, i32 x, i32 z) {
  if (source) {
    i32 point_x, point_z, point_y;
    if (uptr(source) == 1) {
      u8 altitude;
      SpEffects::dirt->GetVertexAltitude(u32(x), u32(z), &altitude);
      u32 old_flags = GetGeomFlag(), new_flags = (old_flags & kSpPowerIconWithoutUndergroundGeometry) | 0x100;
      geom_flags = new_flags;
      MoveToNewPosition(i32((u32(x) << kCtMicroCoordinateShift) + 128), i32((u32(z) << kCtMicroCoordinateShift) + 128),
          i32(u32(altitude) << kCtMicroCoordinateShift), 1, old_flags != new_flags);
    } else {
      u32 layer = source->GetOccLayerFlag(), old_flags = GetGeomFlag(), new_flags = old_flags;
      new_flags = layer & 1 ? new_flags | 0x100 : new_flags & ~0x100u;
      new_flags = layer & 2 ? new_flags | kOcGeomPipe : new_flags & ~kOcGeomPipe;
      new_flags = layer & 4 ? new_flags | kOcGeomSubway : new_flags & ~kOcGeomSubway;
      geom_flags = new_flags;
      GetPositionForOccupant(source, zoom, rotation, &point_x, &point_z, &point_y);
      MoveToNewPosition(point_x, point_z, point_y, 1, old_flags != new_flags);
    }
  } else {
    ClearGeomFlag(kSpPowerIconLayerGeometryMask);
    RemoveFromAnimationSystem();
  }
  occupant = source;
  cell_x = x;
  cell_z = z;
}

// Place the indicator eight animation units above the occupant and near the corner selected by compass.
// The footprint uses the original X dimension; zoom is unused and invalid compass values leave the root unchanged.
void SpPowerIcon::GetPositionForOccupant(ScOccupant *source, i32, i32 rotation, i32 *x, i32 *z, i32 *y) {
  i32 size = source->SizeInCellsX();
  source->GetLocationInAnimCellCoords(reinterpret_cast<u32 *>(x), reinterpret_cast<u32 *>(z));
  *y = i32(u32(source->GetAltAnimCells()) + 8);
  i32 edge_offset = (size << kCtMicroCoordinateShift) - 64;
  switch (rotation) {
  case 0:
    *x = i32(u32(*x) + u32(edge_offset));
    *z = i32(u32(*z) + u32(edge_offset));
    break;
  case 1:
    *x = i32(u32(*x) + u32(edge_offset));
    *z = i32(u32(*z) + 64);
    break;
  case 2:
    *x = i32(u32(*x) + 64);
    *z = i32(u32(*z) + 64);
    break;
  case 3:
    *x = i32(u32(*x) + 64);
    *z = i32(u32(*z) + u32(edge_offset));
    break;
  }
}

// Demand the original per-zoom power icon without changing its cached registration rectangle.
// The rotation and quality operands are ignored by the installed producer.
u8 SpPowerIcon::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(u32(zoom) - kSpPowerIconKeyOffset, 0, &image);
  color[zoom] = image->buffer;
  return 1;
}

// Release a live color demand when the image cache still exists, then clear that borrowed image pointer.
// The installed method returns DWORD zero even when there was nothing to release.
i32 SpPowerIcon::ReleaseArt(i32 zoom, i32) {
  if (SpEffects::images) {
    if (color[zoom]) {
      SpEffects::images->UnprepareImage(u32(zoom) - kSpPowerIconKeyOffset, 0);
      color[zoom] = nullptr;
    }
  }
  return 0;
}

// Copy all five registration rectangles from image metadata without acquiring color demands.
// Frame offsets remain owned by the blink update.
void SpPowerIcon::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(u32(zoom) - kSpPowerIconKeyOffset, 0);
    registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Allocate the original independent bit columns, rounding the stored row count up to whole DWORDs.
// Fill every allocated bit, including the unused tail of the last word.
SpPowerBits::SpPowerBits(u32 columns_x, u32 rows_z, const bool *fill)
    : width(columns_x), height(rows_z), words((rows_z >> 5) + ((rows_z & 31) != 0)) {
  columns = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    columns[x] = new u32[words];
  SetAllCells(fill);
}

// Release each original bit column before its pointer array. No game objects are owned by these cells.
SpPowerBits::~SpPowerBits() {
  for (u32 x = 0; x < width; ++x)
    delete[] columns[x];
  delete[] columns;
}

// Fill all stored words from the Boolean input.
void SpPowerBits::SetAllCells(const bool *fill) {
  u32 value = *fill ? UINT32_MAX : 0;
  for (u32 x = 0; x < width; ++x)
    for (u32 word = 0; word < words; ++word)
      columns[x][word] = value;
}

// Replace one bit without changing any other cell in its column. Trusted display callers supply the original valid cell.
void SpPowerBits::SetValue(u32 x, u32 z, const bool *value) {
  u32 mask = 1u << (z & 31);
  if (*value)
    columns[x][z >> 5] |= mask;
  else
    columns[x][z >> 5] &= ~mask;
}

// Establish the original three interface identities, display flags, pool limit and viewport cache.
// The per-zoom margins control how far pooled indicators may sit beyond the visible cell rectangle.
SpPower::SpPower()
    : references(0), initialized(0), visible(1), registered(0), power_updated(0), update_pending(0), city(nullptr),
      zones(nullptr), change_sender(0), pool_limit(100), power_bits(nullptr), center_x(5000), center_z(5000),
      view_bounds{}, zoom(0), rotation(0), margins{24, 14, 8, 5, 3} {
}

// Detach active indicators, release the retained pool and cancel queued messages before storage is destroyed.
// This destructor does not call Shutdown or release the separately retained city and zone layer.
SpPower::~SpPower() {
  RemoveFromAnimationSystem();
  for (u32 index = 0; index < pool.size; ++index)
    pool.data[index]->Release();
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(static_cast<ScMessageTarget *>(this));
}

// Publish the primary power-display or message identity and retain through the controlling reference counter.
// Unsupported identifiers clear the output, including the separate city-change IID.
bool SpPower::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpPowerDisplay);
  HANDLE_QUERY_INTERFACE(IID_SpPowerDisplay, SpPowerDisplay);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpPower::AddRef() {
  return ++references;
}

// Destroy the nonvirtual concrete display when its old reference count is one.
// Other counts preserve the original unsigned decrement, including zero wrapping.
u32 SpPower::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Retain the real city and zoning layer, initialize the power bits and subscribe through the zone change source.
// Hardware rank selects the original pool limit before animation setup; repeated initialization returns one unchanged.
u8 SpPower::Init(u32 width, u32 height) {
  if (initialized)
    return 1;
  city = ScAddRefNotNull(SpGetCity());
  zones = ScAddRefNotNull(city->ZoneLayer());
  if (!power_bits) {
    bool fill = true;
    power_bits = new SpPowerBits(width, height, &fill);
  }
  ApPerformanceInfo *performance = SpEffects::performance;
  if (performance->processor_power >= 7 || performance->processor_mhz >= 400)
    pool_limit = 200;
  else if (performance->processor_power >= 5 || performance->processor_mhz >= 200)
    pool_limit = 150;
  else if (performance->processor_power >= 3 || performance->processor_mhz >= 150)
    pool_limit = 100;
  else
    pool_limit = 50;
  cell_order.mode = 1;
  cell_order.left = 0;
  cell_order.top = 0;
  cell_order.right = ScIntMax(i32(width - 1), 0);
  cell_order.bottom = ScIntMax(i32(height - 1), 0);
  cell_order.dirty = 1;
  pool_order.mode = 1;
  pool_order.dirty = 1;
  pool_order.count = i32(width * height);
  AddToAnimationSystem();
  CtChangeSource *source;
  if (zones->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source))) {
    source->AddReceiver(static_cast<CtChangeReceiver *>(this), &change_sender);
    source->Release();
  }
  initialized = 1;
  return 1;
}

void SpPower::ShowOrHide(u8) {
}

// Cancel posts and animation before destroying power bits, then release city and zoning registrations in source order.
// Retained pool objects remain available until the actual concrete destructor.
void SpPower::Shutdown() {
  if (!initialized)
    return;
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(static_cast<ScMessageTarget *>(this));
  RemoveFromAnimationSystem();
  ScDelete(&power_bits);
  ScRelease(&city);
  if (zones) {
    CtChangeSource *source;
    if (zones->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source))) {
      source->RemoveReceiver(static_cast<CtChangeReceiver *>(this));
      source->Release();
    }
    zones->Release();
    zones = nullptr;
  }
  initialized = 0;
}

bool SpPower::OccupantInserted(u32, ScOccupant *) {
  return true;
}

bool SpPower::OccupantRemoved(u32, ScOccupant *) {
  return true;
}

bool SpPower::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool SpPower::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool SpPower::CellChangedAll(u32 sender) {
  return CellChanged(sender, 0, 0, power_bits->width - 1, power_bits->height - 1);
}

bool SpPower::CellChanged(u32 sender, u32 x, u32 z) {
  return CellChanged(sender, x, z, x, z);
}

// Post matching zone-source ranges to the message facet, packing the original four low coordinate bytes.
bool SpPower::CellChanged(u32 sender, u32 left, u32 top, u32 right, u32 bottom) {
  if (sender == change_sender && SpEffects::messages) {
    u32 packed = ((left & 255) << 24) | ((top & 255) << 16) | ((right & 255) << 8) | (bottom & 255);
    SpEffects::messages->GeneralMessagePostToTarget(
        ScMessage(uptr(kSpMsg_PowerCellRangeChanged), packed, 0, 0), static_cast<ScMessageTarget *>(this));
  }
  return true;
}

// Read the actual zone byte and exclude the three original non-zone values.
i32 SpPower::IsCellZoned(u32 x, u32 z) {
  u8 zone;
  zones->GetValue(x, z, &zone);
  return zone != 0 && zone != 22 && zone != 17;
}

bool SpPower::IsOccupantEligibleForPowerSprite(ScOccupant *occupant) {
  void *object;
  if (occupant->MajorType() != 1 || !occupant->QueryInterface(IID_BdBuilding, &object))
    return false;
  bool eligible = static_cast<BdBuilding *>(object)->PowerRequired() != 0;
  occupant->Release();
  return eligible;
}

void SpPower::SetBlinkTimeForZoom(i32 value) {
  SpPowerIcon::blink_time = kSpPowerZoomBlinkBase - kSpPowerZoomBlinkStep * u32(value);
}

// Set membership before rebuilding indicators, then cache the actual view cell rectangle and shared zoom/compass.
// Register the four original message types only after setup and viewport clipping.
void SpPower::AddToAnimationSystem() {
  if (registered || !visible)
    return;
  registered = 1;
  SetupDisplayCompletely(1);
  ScRect micro_bounds;
  SpEffects::view->GetCameraHint(&micro_bounds, &view_bounds);
  view_bounds.right = ScIntMin(view_bounds.right, (SpEffects::micro_width - 256) / 256);
  view_bounds.bottom = ScIntMin(view_bounds.bottom, (SpEffects::micro_height - 256) / 256);
  zoom = SpEffects::zoom;
  rotation = SpEffects::rotation;
  SetBlinkTimeForZoom(zoom);
  SpGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kOcMsg_SurfaceInserted);
  SpGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kOcMsg_SurfaceRemoved);
  SpGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_PowerMapUpdated);
}

// Remove the four subscriptions and clear the displayed indicators before clearing active membership.
// The icon pool and initialized city references have separate original lifetimes.
void SpPower::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  SpGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kOcMsg_SurfaceInserted);
  SpGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kOcMsg_SurfaceRemoved);
  SpGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_PowerMapUpdated);
  ClearDisplayCompletely(1);
  registered = 0;
}

// Locate the original sparse cell position in signed Z-then-X order. No occupant or sprite lifetime changes here.
static u32 SpPowerCellIndex(const ScArray<SpPowerCell> &cells, i32 x, i32 z) {
  u32 first = 0, last = cells.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    const SpPowerCell &cell = cells.data[middle];
    if (cell.z < z || (cell.z == z && cell.x < x))
      first = middle + 1;
    else
      last = middle;
  }
  return first;
}

// Borrow a matching association without retaining either object. Failed searches leave the actual collection untouched.
static SpPowerCell *SpFindPowerCell(ScArray<SpPowerCell> *cells, i32 x, i32 z) {
  u32 at = SpPowerCellIndex(*cells, x, z);
  return at < cells->size && cells->data[at].x == x && cells->data[at].z == z ? &cells->data[at] : nullptr;
}

// Insert or replace the original matrix payload without adding or releasing references.
// The source sparse matrix's two sentinel coordinates are outside its accepted key range.
static void SpPutPowerCell(ScArray<SpPowerCell> *cells, i32 x, i32 z, ScOccupant *occupant, SpPowerIcon *sprite) {
  if (x < INT32_MIN + 2 || z < INT32_MIN + 2)
    return;
  u32 at = SpPowerCellIndex(*cells, x, z);
  if (at == cells->size || cells->data[at].x != x || cells->data[at].z != z) {
    cells->Append();
    memmove(cells->data + at + 1, cells->data + at, (cells->size - at - 1) * sizeof(SpPowerCell));
  }
  cells->data[at] = {x, z, occupant, sprite};
}

// Erase one association before returning its original payload to the caller.
static bool SpErasePowerCell(ScArray<SpPowerCell> *cells, i32 x, i32 z, SpPowerCell *out) {
  u32 at = SpPowerCellIndex(*cells, x, z);
  if (at == cells->size || cells->data[at].x != x || cells->data[at].z != z)
    return false;
  if (out)
    *out = cells->data[at];
  memmove(cells->data + at, cells->data + at + 1, (cells->size - at - 1) * sizeof(SpPowerCell));
  --cells->size;
  return true;
}

// Snapshot the same inclusive city rectangle enumerated by the original matrix helper.
// Records copy raw borrowed pointers without changing callback-visible reference counts.
static void SpCopyPowerCells(const ScArray<SpPowerCell> &cells, u32 width, u32 height, ScArray<SpPowerCell> *out) {
  out->Reserve(cells.size);
  for (u32 at = 0; at < cells.size; ++at) {
    const SpPowerCell &cell = cells.data[at];
    if (cell.x >= 0 && cell.z >= 0 && u32(cell.x) <= width && u32(cell.z) <= height)
      *out->Append() = cell;
  }
}

// Copy the matrix's inclusive rectangle in its original Z-major traversal order.
// Snapshot pointers are borrowed, so animation callbacks do not acquire extra occupant references.
static void SpCopyPowerRect(const ScArray<SpPowerCell> &cells, const ScRect &bounds, ScArray<SpPowerCell> *out) {
  out->Reserve(cells.size);
  for (u32 at = 0; at < cells.size; ++at) {
    const SpPowerCell &cell = cells.data[at];
    if (cell.x >= bounds.left && cell.z >= bounds.top && cell.x <= bounds.right && cell.z <= bounds.bottom)
      *out->Append() = cell;
  }
}

// Test the original occupant membership set without acquiring another hold.
// Its iteration order is not used by the display's retained-object traversals.
static bool SpHasPowerOccupant(const ScArray<ScOccupant *> &occupants, ScOccupant *occupant) {
  for (u32 at = 0; at < occupants.size; ++at)
    if (occupants.data[at] == occupant)
      return true;
  return false;
}

// A missing key leaves both storage and ownership unchanged.
static void SpErasePowerOccupant(ScArray<ScOccupant *> *occupants, ScOccupant *occupant) {
  for (u32 at = 0; at < occupants->size; ++at) {
    if (occupants->data[at] == occupant) {
      memmove(occupants->data + at, occupants->data + at + 1, (occupants->size - at - 1) * sizeof(ScOccupant *));
      --occupants->size;
      return;
    }
  }
}

// Preserve the installed adjacent-unique compaction, including its ignored returned end.
// The vector's live size and untouched tail values deliberately remain unchanged.
static void SpCompactPowerAvailable(ScArray<SpPowerIcon *> *available) {
  if (!available->size)
    return;
  u32 output = 0;
  for (u32 at = 1; at < available->size; ++at) {
    if (available->data[output] != available->data[at]) {
      ++output;
      if (output != at)
        available->data[output] = available->data[at];
    }
  }
}

// Grow the retained icon pool up to its hardware limit, or delete only icons from the available tail.
// A pool hold is acquired once per new sprite; the available vector merely borrows that same pointer.
i32 SpPower::GrowSpritePool(i32 count) {
  if (count > 0) {
    if (pool.size >= pool_limit)
      return 0;
    i32 added_count = i32(UintMin(u32(count), pool_limit - pool.size));
    for (i32 at = 0; at < added_count; ++at) {
      SpPowerIcon *sprite = new SpPowerIcon;
      sprite->AddRef();
      *pool.Append() = sprite;
      *available.Append() = sprite;
    }
    return added_count;
  }
  if (!count)
    return 0;
  u32 removed_count = UintMin(0u - u32(count), available.size);
  u32 first = available.size - removed_count, old_end = available.size;
  for (u32 at = first; at < old_end; ++at) {
    SpPowerIcon *sprite = available.data[at];
    u32 index = 0;
    while (index < pool.size && pool.data[index] != sprite)
      ++index;
    if (index < pool.size) {
      memmove(pool.data + index, pool.data + index + 1, (pool.size - index - 1) * sizeof(SpPowerIcon *));
      --pool.size;
    }
    if (sprite)
      delete sprite;
  }
  available.size = first;
  return i32(0u - removed_count);
}

// Pop one real pool object after applying the original bare-cell pattern and optional expanded-view clip.
// Grow before testing the clip, and transfer the selected icon only when the original move byte requests it.
SpPowerIcon *SpPower::AddUnpoweredOccupantSprite(i32 x, i32 z, ScOccupant *occupant, u8 move, u8 clip) {
  if (uptr(occupant) == 1 && ((x & 1) || (z - x / 2) % 3))
    return nullptr;
  if (!available.size && (pool.size >= pool_limit || GrowSpritePool(10) <= 0))
    return nullptr;
  if (clip) {
    i32 margin = margins[zoom];
    if (x < view_bounds.left - margin || z < view_bounds.top - margin || x > view_bounds.right + margin ||
        z > view_bounds.bottom + margin)
      return nullptr;
  }
  SpPowerIcon *sprite = available.data[--available.size];
  if (move)
    sprite->MoveToNewOccupant(occupant, zoom, rotation, x, z);
  return sprite;
}

// Retire matrix indicators from sorted snapshots, preserving release-before-animation callback order.
// A full reset also releases building ownership and fills the source power map; a view-only reset retains buildings.
void SpPower::ClearDisplayCompletely(u8 reset) {
  if (building_cells.size) {
    ScArray<SpPowerCell> snapshot;
    SpCopyPowerCells(building_cells, power_bits->width, power_bits->height, &snapshot);
    for (u32 at = 0; at < snapshot.size; ++at) {
      SpPowerCell cell = snapshot.data[at];
      if (cell.occupant) {
        if (reset)
          cell.occupant->Release();
        else if (cell.sprite)
          SpPutPowerCell(&building_cells, cell.x, cell.z, cell.occupant, nullptr);
        if (cell.sprite) {
          cell.sprite->RemoveFromAnimationSystem();
          *available.Append() = cell.sprite;
        }
      }
    }
  }
  if (zone_cells.size) {
    ScArray<SpPowerCell> snapshot;
    SpCopyPowerCells(zone_cells, power_bits->width, power_bits->height, &snapshot);
    for (u32 at = 0; at < snapshot.size; ++at) {
      const SpPowerCell &cell = snapshot.data[at];
      if (cell.occupant && cell.sprite) {
        cell.sprite->RemoveFromAnimationSystem();
        *available.Append() = cell.sprite;
      }
    }
    zone_cells.Clear();
  }
  if (available.size < pool.size) {
    for (u32 at = 0, end = pool.size; at < end; ++at) {
      SpPowerIcon *sprite = pool.data[at];
      if (sprite->occupant) {
        sprite->MoveToNewOccupant(nullptr, 0, 0, 0, 0);
        *available.Append() = sprite;
      }
    }
    if (available.size < pool.size) {
      for (u32 at = 0, end = pool.size; at < end; ++at) {
        SpPowerIcon *sprite = pool.data[at];
        u32 index = 0;
        while (index < available.size && available.data[index] != sprite)
          ++index;
        if (index == available.size) {
          sprite->MoveToNewOccupant(nullptr, 0, 0, 0, 0);
          *available.Append() = sprite;
        }
      }
    }
  }
  if (available.size > pool.size)
    SpCompactPowerAvailable(&available);
  if (reset) {
    building_cells.Clear();
    occupants.Clear();
    bool fill = true;
    power_bits->SetAllCells(&fill);
  }
}

// Return the initialized occupant output from the original surface lookup, independently of its Boolean result.
// The real producer acquires the returned object's hold.
static ScOccupant *SpEffectOccupantAtCell(i32 x, i32 z) {
  ScOccupant *occupant = nullptr;
  SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant);
  return occupant;
}

// Defer an incoming held occupant or mark its original footprint for the next power-map notification.
// Active insertion removes overlapping bare-cell indicators before consuming the incoming hold.
u8 SpPower::OccupantInserted(ScOccupant *occupant, u8 post) {
  if (!registered) {
    occupant->Release();
    return 1;
  }
  if (post) {
    if (SpEffects::messages)
      SpEffects::messages->GeneralMessagePostToTarget(
          ScMessage(kSpMsg_PowerOccupantInserted, uptr(occupant), 0, 0), static_cast<ScMessageTarget *>(this));
    return 1;
  }
  if (occupant->IsInCity()) {
    GeCellRect bounds;
    occupant->GetBoundingRectInCellCoords(&bounds);
    if (IsOccupantEligibleForPowerSprite(occupant)) {
      bool powered = true;
      for (u32 x = bounds.left; x <= bounds.right; ++x)
        for (u32 z = bounds.top; z <= bounds.bottom; ++z)
          power_bits->SetValue(x, z, &powered);
    }
    for (u32 x = bounds.left; x <= bounds.right; ++x) {
      for (u32 z = bounds.top; z <= bounds.bottom; ++z) {
        SpPowerCell cell;
        if (SpErasePowerCell(&zone_cells, i32(x), i32(z), &cell) && cell.sprite) {
          *available.Append() = cell.sprite;
          cell.sprite->RemoveFromAnimationSystem();
        }
      }
    }
  }
  occupant->Release();
  return 1;
}

// Remove a matching retained building association, repopulate eligible bare cells, then consume both actual holds.
// Unknown bounds use the original root-cell fallback; a mismatching erased record is not silently restored.
u8 SpPower::OccupantRemoved(ScOccupant *occupant, u8 post) {
  if (!registered) {
    occupant->Release();
    return 1;
  }
  if (post) {
    if (SpEffects::messages)
      SpEffects::messages->GeneralMessagePostToTarget(
          ScMessage(kSpMsg_PowerOccupantRemoved, uptr(occupant), 0, 0), static_cast<ScMessageTarget *>(this));
    return 1;
  }
  GeCellRect bounds;
  bounds.left = UINT32_MAX;
  occupant->GetBoundingRectInCellCoords(&bounds);
  if (bounds.left == UINT32_MAX) {
    occupant->GetLocationInCellCoords(&bounds.left, &bounds.top);
    bounds.right = bounds.left;
    bounds.bottom = bounds.top;
    if (bounds.left == UINT32_MAX)
      bounds = {0, 0, 0, 0};
  }
  SpPowerCell cell;
  if (SpErasePowerCell(&building_cells, i32(bounds.left), i32(bounds.top), &cell) && cell.occupant == occupant) {
    if (cell.sprite) {
      cell.sprite->RemoveFromAnimationSystem();
      *available.Append() = cell.sprite;
      for (u32 x = bounds.left; x <= bounds.right; ++x) {
        for (u32 z = bounds.top; z <= bounds.bottom; ++z) {
          if (!(x & 1) && (i32(z) - i32(x) / 2) % 3 == 0 && IsCellZoned(x, z)) {
            SpPowerIcon *sprite =
                AddUnpoweredOccupantSprite(i32(x), i32(z), reinterpret_cast<ScOccupant *>(uptr(1)), 1, 0);
            if (sprite)
              SpPutPowerCell(&zone_cells, i32(x), i32(z), reinterpret_cast<ScOccupant *>(uptr(1)), sprite);
          }
        }
      }
    }
    SpErasePowerOccupant(&occupants, occupant);
    occupant->Release();
  }
  occupant->Release();
  return 1;
}

// Reconcile an inclusive zone-change rectangle against the existing bare-cell associations.
// Capture each old sprite before querying zoning, preserving callback-visible erase and available-pool order.
u8 SpPower::OccupantCellRangeChanged(u32 left, u32 top, u32 right, u32 bottom) {
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      SpPowerCell *cell = SpFindPowerCell(&zone_cells, i32(x), i32(z));
      bool found = cell != nullptr;
      SpPowerIcon *sprite = found ? cell->sprite : nullptr;
      if (IsCellZoned(x, z)) {
        if (!found) {
          bool powered = true;
          power_bits->SetValue(x, z, &powered);
        }
      } else if (found) {
        SpErasePowerCell(&zone_cells, i32(x), i32(z), nullptr);
        if (sprite) {
          *available.Append() = sprite;
          sprite->RemoveFromAnimationSystem();
        }
      }
    }
  }
  return 1;
}

// Dispatch original view, power-map, occupant and packed-zone notifications through the message facet.
// Incoming occupant messages acquire one hold; targeted deferred messages consume that already held pointer.
bool SpPower::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_ViewInfoUpdated:
    if (u32(message->data1) == kSpViewUpdate_Snapshot) {
      GeBounds bounds;
      ScRect old_bounds = view_bounds;
      SpIsoViewInfo *info = reinterpret_cast<SpIsoViewInfo *>(message->data2);
      i32 new_zoom, new_rotation;
      info->GetInfo(&bounds);
      info->GetZoom(&new_zoom);
      info->GetCompass(&new_rotation);
      bounds.minimum.x = ScIntMax(bounds.minimum.x, 0);
      bounds.minimum.z = ScIntMax(bounds.minimum.z, 0);
      bounds.maximum.x = ScIntMin(bounds.maximum.x, SpEffects::micro_width - kCtMicroUnitsPerCell);
      bounds.maximum.z = ScIntMin(bounds.maximum.z, SpEffects::micro_height - kCtMicroUnitsPerCell);
      ScRect cells = {bounds.minimum.x / kCtMicroUnitsPerCell, bounds.minimum.z / kCtMicroUnitsPerCell,
          bounds.maximum.x / kCtMicroUnitsPerCell, bounds.maximum.z / kCtMicroUnitsPerCell};
      DoViewChange(zoom, new_zoom, rotation, new_rotation, old_bounds, cells);
    }
    break;
  case kCtMsg_PowerMapUpdated:
    power_updated = 1;
    if (update_pending)
      UpdatePowerMap(reinterpret_cast<const ScCellMap<bool> *>(message->data1));
    else
      update_pending = 1;
    break;
  case kOcMsg_SurfaceInserted:
    if (ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1)) {
      occupant->AddRef();
      OccupantInserted(occupant, 1);
    }
    break;
  case kOcMsg_SurfaceRemoved:
    if (ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1)) {
      occupant->AddRef();
      OccupantRemoved(occupant, 1);
    }
    break;
  case kSpMsg_PowerOccupantInserted:
    OccupantInserted(reinterpret_cast<ScOccupant *>(message->data1), 0);
    break;
  case kSpMsg_PowerOccupantRemoved:
    OccupantRemoved(reinterpret_cast<ScOccupant *>(message->data1), 0);
    break;
  case kSpMsg_PowerCellRangeChanged: {
    u32 packed = u32(message->data1);
    OccupantCellRangeChanged(packed >> 24, (packed >> 16) & 255, (packed >> 8) & 255, packed & 255);
    break;
  }
  }
  return true;
}

// Cache the new view before repositioning active icons and applying the original zoom/translation gates.
// A changed rotation alone does not force a rebuild; source margins and Manhattan center thresholds decide it.
void SpPower::DoViewChange(
    i32 old_zoom, i32 new_zoom, i32 old_rotation, i32 new_rotation, const ScRect &old_bounds, const ScRect &bounds) {
  zoom = new_zoom;
  rotation = new_rotation;
  view_bounds = bounds;
  if (!registered)
    return;
  if (old_rotation != new_rotation) {
    for (u32 at = 0; at < pool.size; ++at)
      if (pool.data[at]->registered)
        pool.data[at]->UpdatePosition(new_zoom, new_rotation);
    i32 margin = margins[new_zoom];
    if (old_zoom == new_zoom &&
        !(old_bounds.left - bounds.left > margin && old_bounds.top - bounds.top > margin &&
            old_bounds.right - bounds.right > margin && old_bounds.bottom - bounds.bottom > margin))
      return;
  }
  if (old_zoom != new_zoom) {
    SetBlinkTimeForZoom(zoom);
    ClearDisplayCompletely(0);
    SetupDisplayCompletely(0);
    return;
  }
  if (old_bounds.left != bounds.left || old_bounds.top != bounds.top || old_bounds.right != bounds.right ||
      old_bounds.bottom != bounds.bottom) {
    i32 x = i32(u32(bounds.left) + u32(bounds.right)) / 2;
    i32 z = i32(u32(bounds.top) + u32(bounds.bottom)) / 2;
    static const i32 thresholds[] = {60, 30, 15, 7, 4};
    if (abs(center_x - x) + abs(center_z - z) > thresholds[new_zoom]) {
      center_x = x;
      center_z = z;
      DoSpriteChangeForTranslation(old_bounds, bounds);
    }
  }
}

// Apply changed powered bits in the original randomized order while transferring real occupant holds to the matrices.
// A null map deliberately reuses prior delta lists; the final viewport scan fills eligible unoccupied outage cells.
u8 SpPower::UpdatePowerMap(const ScCellMap<bool> *map) {
  if (map) {
    u32 width = map->CellCountX(), height = map->CellCountZ();
    added.Clear();
    removed.Clear();
    for (u32 x = 0; x < width; ++x) {
      for (u32 z = 0; z < height; ++z) {
        bool old_power = (power_bits->columns[x][z >> 5] & (1u << (z & 31))) != 0, powered;
        map->GetValue(x, z, &powered);
        if (old_power && !powered)
          *added.Append() = {x, z};
        else if (!old_power && powered)
          *removed.Append() = {x, z};
        power_bits->SetValue(x, z, &powered);
      }
    }
  }
  if (!registered)
    return 1;
  if (added.size || removed.size) {
    for (u32 at = 0, end = removed.size; at < end; ++at) {
      const ScPointXZ *point = removed.data + at;
      SpPowerCell cell;
      if (SpErasePowerCell(&building_cells, i32(point->x), i32(point->z), &cell) && cell.occupant) {
        SpErasePowerOccupant(&occupants, cell.occupant);
        cell.occupant->Release();
        if (cell.sprite) {
          *available.Append() = cell.sprite;
          cell.sprite->RemoveFromAnimationSystem();
        }
      }
      if (SpErasePowerCell(&zone_cells, i32(point->x), i32(point->z), &cell) && cell.occupant && cell.sprite) {
        *available.Append() = cell.sprite;
        cell.sprite->RemoveFromAnimationSystem();
      }
    }
    pool_order.count = i32(added.size);
    pool_order.dirty = 1;
    pool_order.Begin(1);
    i32 budget = ScIntMax(i32(pool_limit + available.size - pool.size), 1), index;
    u32 placed = 0;
    while (pool_order.Next(&index)) {
      const ScPointXZ &point = added.data[index];
      ScOccupant *occupant = SpEffectOccupantAtCell(i32(point.x), i32(point.z));
      if (occupant) {
        u32 root_x, root_z;
        occupant->GetLocationInCellCoords(&root_x, &root_z);
        if (root_x == point.x && root_z == point.z && IsOccupantEligibleForPowerSprite(occupant) &&
            !SpHasPowerOccupant(occupants, occupant)) {
          SpPowerIcon *sprite = nullptr;
          if (placed < u32(budget)) {
            sprite = AddUnpoweredOccupantSprite(i32(point.x), i32(point.z), occupant, 1, 0);
            if (sprite)
              ++placed;
          }
          SpPutPowerCell(&building_cells, i32(point.x), i32(point.z), occupant, sprite);
          if (!SpHasPowerOccupant(occupants, occupant))
            *occupants.Append() = occupant;
        } else
          occupant->Release();
      } else if (!(point.x & 1) && (i32(point.z) - i32(point.x) / 2) % 3 == 0 && IsCellZoned(point.x, point.z)) {
        SpPowerIcon *sprite =
            AddUnpoweredOccupantSprite(i32(point.x), i32(point.z), reinterpret_cast<ScOccupant *>(uptr(1)), 1, 1);
        if (sprite)
          SpPutPowerCell(&zone_cells, i32(point.x), i32(point.z), reinterpret_cast<ScOccupant *>(uptr(1)), sprite);
      }
    }
  }
  cell_order.SetBounds(view_bounds);
  cell_order.Begin(1);
  i32 x, z;
  while (cell_order.Next(&x, &z)) {
    if (!(x & 1) && (z - x / 2) % 3 == 0 && !(power_bits->columns[u32(x)][u32(z) >> 5] & (1u << (u32(z) & 31))) &&
        IsCellZoned(u32(x), u32(z)) && !SpFindPowerCell(&zone_cells, x, z) &&
        !SpEffects::surface->DoesOccupantExistAtLocation(u32(x), u32(z))) {
      SpPowerIcon *sprite = AddUnpoweredOccupantSprite(x, z, reinterpret_cast<ScOccupant *>(uptr(1)), 1, 1);
      if (sprite)
        SpPutPowerCell(&zone_cells, x, z, reinterpret_cast<ScOccupant *>(uptr(1)), sprite);
    }
  }
  return 1;
}

// Reassign offscreen building indicators before consuming available sprites for the new view.
// Bare-cell indicators use the original exclusive far scan edges and a single offscreen reclamation pass.
void SpPower::DoSpriteChangeForTranslation(const ScRect &old_bounds, const ScRect &bounds) {
  if (!zoom)
    return;
  u32 needed = 0, moved = 0, next = 0;
  {
    ScArray<SpPowerCell> snapshot;
    SpCopyPowerRect(building_cells, bounds, &snapshot);
    for (u32 at = 0; at < snapshot.size; ++at) {
      const SpPowerCell &cell = snapshot.data[at];
      if (!cell.sprite &&
          (cell.x < old_bounds.left || cell.z < old_bounds.top || cell.x > old_bounds.right ||
              cell.z > old_bounds.bottom))
        ++needed;
    }
    for (u32 at = 0, end = pool.size; at < end && moved < needed; ++at) {
      SpPowerIcon *sprite = pool.data[at];
      if (uptr(sprite->occupant) >= 2 &&
          (sprite->cell_x < bounds.left || sprite->cell_z < bounds.top || sprite->cell_x > bounds.right ||
              sprite->cell_z > bounds.bottom) &&
          next < snapshot.size) {
        while (next < snapshot.size && snapshot.data[next].sprite)
          ++next;
        if (next == snapshot.size)
          continue;
        SpPutPowerCell(&building_cells, sprite->cell_x, sprite->cell_z, sprite->occupant, nullptr);
        const SpPowerCell &cell = snapshot.data[next];
        pool.data[at]->MoveToNewOccupant(cell.occupant, zoom, rotation, cell.x, cell.z);
        SpPutPowerCell(&building_cells, cell.x, cell.z, cell.occupant, pool.data[at]);
        ++moved;
        ++next;
      }
    }
    if (moved < needed) {
      if (available.size < needed - moved && pool.size < pool_limit)
        GrowSpritePool(i32(needed - available.size - moved));
      i32 index = i32(available.size);
      while (--index >= 0 && moved < needed) {
        if (next >= snapshot.size)
          continue;
        while (next < snapshot.size && snapshot.data[next].sprite)
          ++next;
        if (next == snapshot.size)
          continue;
        const SpPowerCell &cell = snapshot.data[next];
        available.data[index]->MoveToNewOccupant(cell.occupant, zoom, rotation, cell.x, cell.z);
        SpPutPowerCell(&building_cells, cell.x, cell.z, cell.occupant, available.data[index]);
        ++moved;
        ++next;
        --available.size;
      }
    }
  }
  u32 budget = u32(i32((u32(bounds.bottom) - u32(bounds.top)) * (u32(bounds.right) - u32(bounds.left))) / 4) + 4;
  bool reclaimed = false;
  ScRect scan = {bounds.left, bounds.top, i32(u32(bounds.right) - 1), i32(u32(bounds.bottom) - 1)};
  cell_order.SetBounds(scan);
  cell_order.Begin(1);
  i32 x, z;
  while (cell_order.Next(&x, &z)) {
    if ((x & 1) || (z - x / 2) % 3 || (power_bits->columns[u32(x)][u32(z) >> 5] & (1u << (u32(z) & 31))) ||
        !IsCellZoned(u32(x), u32(z)) || SpFindPowerCell(&zone_cells, x, z) ||
        SpEffects::surface->DoesOccupantExistAtLocation(u32(x), u32(z)))
      continue;
    SpPowerIcon *sprite = AddUnpoweredOccupantSprite(x, z, reinterpret_cast<ScOccupant *>(uptr(1)), 1, 0);
    if (!sprite) {
      if (!reclaimed) {
        reclaimed = true;
        for (u32 at = 0, end = pool.size; at < end; ++at) {
          if (moved >= budget)
            break;
          SpPowerIcon *candidate = pool.data[at];
          if (candidate->occupant &&
              (candidate->cell_x < bounds.left || candidate->cell_z < bounds.top || candidate->cell_x > bounds.right ||
                  candidate->cell_z > bounds.bottom)) {
            SpPowerCell *cell = SpFindPowerCell(&building_cells, candidate->cell_x, candidate->cell_z);
            if (cell)
              cell->sprite = nullptr;
            else
              SpErasePowerCell(&zone_cells, pool.data[at]->cell_x, pool.data[at]->cell_z, nullptr);
            pool.data[at]->RemoveFromAnimationSystem();
            *available.Append() = pool.data[at];
          }
        }
      }
      sprite = AddUnpoweredOccupantSprite(x, z, reinterpret_cast<ScOccupant *>(uptr(1)), 1, 0);
    }
    if (sprite)
      SpPutPowerCell(&zone_cells, x, z, reinterpret_cast<ScOccupant *>(uptr(1)), sprite);
  }
}

// Restore borrowed matrix records in order, or sample them at the original single-precision interval.
// The biased rounding conversion chooses each sampled record before the pool's callback-producing move.
static void SpRestorePowerCells(
    SpPower *power, const ScArray<SpPowerCell> &snapshot, ScArray<SpPowerCell> *cells, bool sample) {
  if (sample && snapshot.size > power->available.size) {
    f32 limit = f32(snapshot.size), stride = f32(f64(snapshot.size) / f64(power->available.size)), phase = 0;
    f64 next;
    do {
      u32 at = u32(SpEffectCoordinate(phase));
      if (at >= snapshot.size)
        break;
      const SpPowerCell &cell = snapshot.data[at];
      SpPowerIcon *sprite = power->AddUnpoweredOccupantSprite(cell.x, cell.z, cell.occupant, 1, 0);
      if (!sprite)
        break;
      SpPutPowerCell(cells, cell.x, cell.z, cell.occupant, sprite);
      next = f64(phase) + stride;
      phase = f32(next);
    } while (next < limit);
  } else {
    for (u32 at = 0; at < snapshot.size; ++at) {
      const SpPowerCell &cell = snapshot.data[at];
      SpPowerIcon *sprite = power->AddUnpoweredOccupantSprite(cell.x, cell.z, cell.occupant, 1, 0);
      if (!sprite)
        break;
      SpPutPowerCell(cells, cell.x, cell.z, cell.occupant, sprite);
    }
  }
}

// Rebuild pooled indicators from retained building and zone associations after a reset or zoom change.
// A reset reapplies the city's borrowed Boolean power map after prior power notifications.
void SpPower::SetupDisplayCompletely(u8 reset) {
  if (!registered)
    return;
  if (reset) {
    ClearDisplayCompletely(1);
    bool powered = true;
    power_bits->SetAllCells(&powered);
    if (power_updated)
      UpdatePowerMap(city->PowerLayer());
    return;
  }
  u32 total = building_cells.size + zone_cells.size, width = power_bits->width, height = power_bits->height;
  if (available.size != pool.size)
    ClearDisplayCompletely(0);
  if (total) {
    if (total > available.size && pool.size < pool_limit)
      GrowSpritePool(i32(total - available.size));
    ScArray<SpPowerCell> snapshot;
    snapshot.Reserve(total);
    if (total > available.size) {
      SpCopyPowerRect(building_cells, view_bounds, &snapshot);
      SpRestorePowerCells(this, snapshot, &building_cells, true);
      snapshot.Clear();
      SpCopyPowerRect(zone_cells, view_bounds, &snapshot);
      SpRestorePowerCells(this, snapshot, &zone_cells, true);
    } else {
      SpCopyPowerCells(building_cells, width, height, &snapshot);
      SpRestorePowerCells(this, snapshot, &building_cells, false);
      snapshot.Clear();
      SpCopyPowerCells(zone_cells, width, height, &snapshot);
      SpRestorePowerCells(this, snapshot, &zone_cells, false);
    }
  }
  cell_order.SetBounds(view_bounds);
  cell_order.Begin(1);
  i32 x, z;
  while (cell_order.Next(&x, &z)) {
    if ((x & 1) || (z - x / 2) % 3 || (power_bits->columns[u32(x)][u32(z) >> 5] & (1u << (u32(z) & 31))) ||
        !IsCellZoned(u32(x), u32(z)) || SpFindPowerCell(&zone_cells, x, z) ||
        SpEffects::surface->DoesOccupantExistAtLocation(u32(x), u32(z)))
      continue;
    SpPowerIcon *sprite = AddUnpoweredOccupantSprite(x, z, reinterpret_cast<ScOccupant *>(uptr(1)), 1, 1);
    if (sprite)
      SpPutPowerCell(&zone_cells, x, z, reinterpret_cast<ScOccupant *>(uptr(1)), sprite);
  }
  if (available.size > pool_limit >> 1)
    GrowSpritePool(i32(0u - pool_limit) >> 1);
}

// Initialize the original particle scalars before its empty trail-list lifetime begins.
// The list owns its nodes; its particle pointers do not acquire references or imply recursive deletion.
SpLightParticle::SpLightParticle() {
  alive = 0;
  trailing = 1;
  lifetime = trail_interval = trail_lifetime = word14 = 0;
  kind = motion = color_index = 0;
  pixel = {0, 0};
  position = velocity = {0.0f, 0.0f, 0.0f};
  age = color = last_trail_age = word54 = word68 = word70 = 0;
  float56 = float60 = float64 = 0.0f;
}

// Retire only the original trail-list nodes. Particle reclamation is a separate explicit operation.
// Standard list destruction preserves the original non-owning pointer payloads.
SpLightParticle::~SpLightParticle() = default;

// Keep a lead alive while either it or any listed trail remains active.
// The original test does not remove dead list entries or inspect nested trails.
u8 SpLightParticle::IsCompletelyDead() {
  if (alive)
    return 0;
  for (SpLightParticle *trail : trails)
    if (trail->alive)
      return 0;
  return 1;
}

// Test rounded horizontal micro-coordinates against the cached world and altitude against its original ceiling.
i32 SpLightParticle::PositionIsInWorldBounds(const ScVec3F &point) {
  u32 x = u32(SpEffectCoordinate(point.x));
  i32 altitude = SpEffectCoordinate(point.y);
  u32 z = u32(SpEffectCoordinate(point.z));
  return x < u32(SpEffects::micro_width) && z < u32(SpEffects::micro_height) && altitude >= 0 &&
      f64(altitude) < 131072.0;
}

// Append a fresh stationary trail particle at the lead's current position with its original lifetime and color.
// The lead records the current age only after the list insertion completes.
void SpLightParticle::GenerateTrailingParticle() {
  SpLightParticle *trail = new SpLightParticle;
  trail->alive = 1;
  trail->lifetime = trail_lifetime;
  trail->trailing = 0;
  trail->trail_interval = trail->trail_lifetime = 0;
  trail->kind = trail->motion = trail->color_index = 0;
  trail->word14 = 0;
  trail->position = position;
  trail->age = 0;
  trail->color = color;
  trail->float56 = 1.0f;
  trails.push_back(trail);
  last_trail_age = age;
}

// Delete inactive trail payloads before unlinking their list nodes, preserving the original cleanup order.
// Active trails retain their existing order and ownership.
void SpLightParticle::RemoveDeadParticles() {
  for (auto it = trails.begin(); it != trails.end();) {
    SpLightParticle *trail = *it;
    if (trail->alive)
      ++it;
    else {
      delete trail;
      it = trails.erase(it);
    }
  }
}

i32 SpLightParticle::GetColorForAge(i32) {
  return -1;
}

// Publish the rounded cell's fixed terrain altitude before classifying contact as ground or water.
// An above-ground particle returns zero without asking the water predicate.
i32 SC3_STDCALL SpLightParticle::GetGroundTypeForPosition(const ScVec3F &point, i32 *altitude) {
  i32 x = SpEffectCoordinate(point.x) / kCtMicroUnitsPerCell, y = SpEffectCoordinate(point.y);
  i32 z = SpEffectCoordinate(point.z) / kCtMicroUnitsPerCell;
  u8 terrain;
  SpEffects::dirt->GetVertexAltitude(u32(x), u32(z), &terrain);
  *altitude = i32(terrain) << kCtMicroCoordinateShift;
  if (y > *altitude)
    return 0;
  return (SpEffects::dirt->IsWater(u32(x), u32(z)) != 0) + 1;
}

// Advance the wrapping age word, then generate trails and move only a living lead with trailing enabled.
// Existing trails tick in list order before dead payloads are removed; a stationary trail updates only its color.
void SpLightParticle::DoTick(i32 elapsed) {
  age = u16(u32(age) + u32(elapsed));
  if (age > lifetime)
    alive = 0;
  if (trailing) {
    if (alive) {
      if (trail_interval && i32(age) - i32(last_trail_age) > trail_interval)
        GenerateTrailingParticle();
      CalculateNextPosition(elapsed);
      color = u16(GetColorForAge(age));
    }
    if (!trails.empty()) {
      for (SpLightParticle *trail : trails)
        trail->DoTick(elapsed);
      RemoveDeadParticles();
    }
  } else if (alive)
    color = u16(GetColorForAge(age));
}

// Apply the original ballistic or vertical-only motion policy and retire contacts with water or a spent bounce.
void SpLightParticle::CalculateNextPosition(i32 elapsed) {
  static constexpr f32 gravity = 0.0003f * 0.5f, vertical_scale = 8.94f;
  f64 delta = f64(elapsed);
  if (motion != 1) {
    if (motion == 4 || motion == 5)
      position.y = f32(f64(f32(delta)) * velocity.y * vertical_scale + position.y);
    return;
  }
  position.x = f32(delta * velocity.x + position.x);
  position.z = f32(delta * velocity.z + position.z);
  f64 next_velocity = f64(velocity.y) - gravity * delta;
  velocity.y = f32(next_velocity);
  position.y = f32(next_velocity * vertical_scale * delta + position.y);
  i32 terrain_altitude, ground = GetGroundTypeForPosition(position, &terrain_altitude);
  if (ground == 1) {
    position.y = f32(terrain_altitude);
    velocity.x = f32(f64(velocity.x) * 0.25);
    velocity.z = f32(f64(velocity.z) * 0.25);
    f64 bounced = f64(velocity.y) * -0.4f;
    velocity.y = f32(bounced);
    if (!(bounced >= 0.008))
      alive = 0;
  } else if (ground == 2)
    alive = 0;
  if (!PositionIsInWorldBounds(position))
    alive = 0;
}

// Copy the real particle prototype and append a new position/velocity sample to the caller's borrowed vector.
// Scalar jitter consumes the original shared integer stream only when the prototype's word14 is nonzero.
static void SpAppendBlastParticle(
    SpParticleBlast *blast, ScArray<SpLightParticle *> *out, f32 x, f32 y, f32 z, f32 speed) {
  SpLightParticle *particle = new SpLightParticle(blast->prototype);
  particle->position = {x + blast->origin.x, y + blast->origin.y, z + blast->origin.z};
  particle->velocity = {x, y, z};
  SpEffects::Scale3DVector(&particle->velocity, speed);
  u16 scale = particle->word14;
  if (scale)
    particle->word14 = u16(SpEffects::random.Range(230 * scale / 256, 280 * scale / 256));
  *out->Append() = particle;
}

// Configure the reusable blast prototype and invoke its actual generator with the caller's output vector.
// Only the original all-ones defaults are filled; vertical ring shapes disable trail generation intervals.
void SpParticleBlast::Generate(ScArray<SpLightParticle *> *out, i32 requested, i32 requested_shape, i16 color) {
  count = u32(requested);
  shape = requested_shape;
  prototype.alive = prototype.trailing = 1;
  if (prototype.trail_interval == 0xffff)
    prototype.trail_interval = 0;
  if (prototype.trail_lifetime == 0xffff)
    prototype.trail_lifetime = 500;
  if (prototype.kind == -1)
    prototype.kind = 4;
  if (prototype.motion == -1)
    prototype.motion = 1;
  if (prototype.color_index == -1)
    prototype.color_index = 0;
  if (prototype.word14 == 0xffff)
    prototype.word14 = 0;
  prototype.age = 0;
  prototype.color = u16(color);
  prototype.last_trail_age = prototype.word54 = 0;
  if (requested_shape == 4 || requested_shape == 5)
    prototype.trail_interval = 0;
  DoGeneration(out);
}

// Generate the original spherical, narrow-cone or flattened-ring samples using the shared random streams.
// Shape five appends its fixed ten inner samples even when the requested outer count is zero.
void SpParticleBlast::DoGeneration(ScArray<SpLightParticle *> *out) {
  if (shape == 0 || shape == 3) {
    for (u32 at = 0; at < count; ++at) {
      f64 theta = SpEffects::random.RandomDoubleRangeUniform(0.0, 6.28318530718);
      f64 phi = SpEffects::random.RandomDoubleRangeUniform(0.0, 6.28318530718);
      f64 speed = SpEffects::random.RandomDoubleRangeUniform(0.2 * 0.75, 0.2 * 1.25);
      f64 cosine = std::cos(phi);
      f32 x = f32(std::sin(theta) * cosine * 10.0);
      f64 sine = std::sin(phi);
      f32 y = f32(std::sin(theta) * sine * 10.0), z = f32(std::cos(theta) * 10.0);
      SpAppendBlastParticle(this, out, x, y, z, f32(speed));
    }
    return;
  }
  if (shape == 1) {
    for (u32 at = 0; at < count; ++at) {
      f64 theta = SpEffects::random.RandomDoubleRangeUniform(0.0, 6.28318530718);
      f64 phi = SpEffects::random.RandomDoubleRangeUniform(-0.628318530718, 0.628318530718);
      f64 speed = SpEffects::random.RandomDoubleRangeUniform(0.2 * 0.5, 0.2 * 1.5);
      f64 cosine = std::cos(phi);
      f32 x = f32(std::sin(theta) * cosine * 10.0), y = f32(std::cos(theta) * 10.0);
      f64 sine = std::sin(phi);
      f32 z = f32(std::sin(theta) * sine * 10.0);
      SpAppendBlastParticle(this, out, x, y, z, f32(speed));
    }
    return;
  }
  if (shape != 4 && shape != 5)
    return;
  f64 angle = 0.0, step = 6.28318530718 / f64(count);
  f64 speed = SpEffects::random.RandomDoubleRangeUniform(0.1 * 0.95, 0.1 * 1.05);
  f64 theta = SpEffects::random.RandomDoubleRangeUniform(0.0, 3.14159265359);
  f64 phi = SpEffects::random.RandomDoubleRangeUniform(0.0, 3.14159265359);
  (void)std::sin(theta);
  f64 sine = std::sin(phi), cosine_x = std::cos(theta), cosine_z = std::cos(phi);
  f64 squash = SpEffects::random.RandomDoubleRangeUniform(shape == 5 ? 0.9 : 0.5, 1.0);
  for (u32 at = 0; at < count; ++at) {
    f32 x = f32(std::cos(angle) * 10.0);
    f64 perpendicular = std::sin(angle) * squash * 10.0;
    x = f32(x * cosine_x);
    f32 y = f32(perpendicular * sine), z = f32(perpendicular * cosine_z);
    SpAppendBlastParticle(this, out, x, y, z, f32(speed));
    angle += step;
  }
  if (shape == 5) {
    static constexpr f64 angles[10] = {0.0, 1.570796326795, 2.74889357189125, 3.0787608005182,
        3.432189974047075, 3.762057202674025, 4.091924431300975, 4.421791659927925,
        4.7752208334568, 5.10508806208375};
    for (u32 at = 0; at < 10; ++at) {
      angle = angles[at];
      f32 x = f32(std::cos(angle) * 5.0);
      f64 perpendicular = std::sin(angle) * squash * 5.0;
      x = f32(x * cosine_x);
      f32 y = f32(perpendicular * sine), z = f32(perpendicular * cosine_z);
      SpAppendBlastParticle(this, out, x, y, z, f32(speed * 0.5));
    }
  }
}

SpParticleSimulator::SpParticleSimulator() {
  references = 0;
}

// Destroy the output vector's storage before the generator's prototype trail-list nodes.
// Generated particles have already been adopted by their sprites and are not deleted by this vector lifetime.
SpParticleSimulator::~SpParticleSimulator() = default;

// Expose the original Unknown and particle-simulator identities and retain the receiver through its virtual method.
// Unsupported requests clear the output pointer.
bool SpParticleSimulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpParticleEffect);
  HANDLE_QUERY_INTERFACE(IID_SpParticleEffect, SpParticleEffect);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpParticleSimulator::AddRef() {
  return ++references;
}

// Destroy the simulator exactly at one reference; other values are decremented and returned directly.
// A zero counter retains the installed wrapping result rather than becoming a fabricated failure.
u32 SpParticleSimulator::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

void SpParticleSimulator::Init() {
}

// Configure and generate the selected blast, then transfer each particle into a newly registered sprite.
// The random-mode-zero path deliberately emits nothing after choosing a mode, as in the installed branch.
u32 SpParticleSimulator::DoParticleGeneration(i32 x, i32 z, i32 y, i32 mode, i32 count) {
  if (count == -1)
    count = 20;
  generated.Clear();
  generator.prototype.lifetime = 4000;
  generator.origin.x = f32(x);
  generator.prototype.trail_interval = 0;
  generator.prototype.trail_lifetime = 500;
  generator.prototype.kind = 4;
  generator.prototype.motion = 1;
  generator.origin.y = f32(y);
  generator.prototype.color_index = 0;
  generator.prototype.word14 = 0;
  generator.origin.z = f32(z);
  switch (mode) {
  case 0:
    mode = i32(SpEffects::random.Bounded(5)) + 1;
    break;
  case 2:
    generator.prototype.kind = SpEffects::random.Bounded(2) ? 2 : 6;
    generator.Generate(&generated, count, 0, 0);
    break;
  case 1:
    generator.prototype.kind = 2;
    generator.prototype.color_index = 0;
    generator.Generate(&generated, count, 0, 0);
    break;
  case 4: {
    u32 shape = SpEffects::random.Bounded(6);
    static constexpr i32 shapes[6] = {0, 4, 1, 2, 5, 0};
    generator.Generate(&generated, count, shapes[shape], 0);
    break;
  }
  }
  u32 result = generated.size, end = result;
  for (u32 at = 0; at < end; ++at) {
    SpParticleSprite *sprite = new SpParticleSprite;
    sprite->AddRef();
    u16 lifetime = generated.data[at]->lifetime;
    f64 duration = SpEffects::random.RandomDoubleRangeUniform(f64(lifetime) * 0.85, f64(lifetime) * 1.15);
    generated.data[at]->lifetime = u16(SpEffectCoordinate(duration));
    if (mode == 2 || mode == 4)
      generated.data[at]->color_index = i16(SpEffects::random.Bounded(7));
    sprite->Init(generated.data[at]);
    sprite->Release();
    result = at + 1;
  }
  return result;
}

// Construct the registered particle-simulator primary identity at the original zero-reference creation boundary.
// Its constructor owns the generator and output storage; the director performs the requested interface query.
ScUnknown *SpCreateParticleSimulator() {
  return static_cast<SpParticleEffect *>(new SpParticleSimulator);
}

// Kind and source rectangles are written by Init before their defined use.
SpParticleSprite::SpParticleSprite() {
  age_frame = 0xffff;
  registered = 0;
  frame = 0;
  frame_started = 0;
  particle = nullptr;
  elapsed = 0;
  area = {0, 0, 0, 0};
  color_table = nullptr;
  for (u32 at = 0; at < 5; ++at)
    particle_color[at] = particle_mask[at] = nullptr;
}

SpParticleSprite::~SpParticleSprite() {
  ScDelete(&particle);
  RemoveFromAnimationSystem();
}

i32 SpParticleSprite::GetW(i32, i32) const {
  return area.right;
}

i32 SpParticleSprite::GetH(i32, i32) const {
  return area.bottom;
}

u8 SpParticleSprite::GetRegistration(i32, i32, ScRect *out) const {
  *out = area;
  return 1;
}

// Expand an anchor using the current outline's separate origin and far extents.
// Unsigned intermediate arithmetic retains defined DWORD wrapping at the original stores.
u8 SpParticleSprite::GetAreaFromPt(i32, i32, ScRect *out, i32 x, i32 y) const {
  i32 bottom = i32(u32(y) + u32(area.bottom)), right = i32(u32(x) + u32(area.right));
  i32 left = i32(u32(x) - u32(area.left));
  out->top = i32(u32(y) - u32(area.top));
  out->left = left;
  out->bottom = bottom;
  out->right = right;
  return 1;
}

// Animation membership is established only after resetting the basic occupant position.
void SpParticleSprite::Init(SpLightParticle *lead) {
  particle = lead;
  if (lead->kind == 6)
    frame = i16(SpEffects::random.Bounded(4));
  kind = particle->kind;
  for (u32 at = 0; at < 5; ++at)
    source[at] = {0, 0, 0, 0};
  position = {0.0f, 0.0f, 0.0f};
  AddToAnimationSystem();
}

// Set registration before invoking the borrowed animation layer, preserving callback-visible membership.
// Position is rounded in the original X/Z/altitude order and inserted with the original layer arguments.
void SpParticleSprite::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  if (SpEffects::animated)
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 1, 0, true);
}

// Clear registration before asking the original layer to remove this occupant.
// A missing layer changes only the registration byte and does not fabricate a result.
void SpParticleSprite::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
}

// Rotate fixed micro-coordinates about the original 65535 square before pixel projection.
// Unknown compass values leave both genuine outputs unchanged.
void SC3_STDCALL SpParticleSprite::MicroActualGridToMicroDrawGrid(i32 rotation, i32 x, i32 z, i32 *out_x, i32 *out_z) {
  switch (rotation) {
  case 0:
    *out_x = x;
    *out_z = z;
    break;
  case 1:
    *out_x = i32(65535u - u32(z));
    *out_z = x;
    break;
  case 2:
    *out_x = i32(65535u - u32(x));
    *out_z = i32(65535u - u32(z));
    break;
  case 3:
    *out_x = z;
    *out_z = i32(65535u - u32(x));
    break;
  }
}

// Project rounded world coordinates through the cached compass and the original three zoom scales.
// Write horizontal position before vertical position so aliased genuine outputs retain the original final store.
void SC3_STDCALL SpParticleSprite::ConvertPositionToCityPixel(const ScVec3F &point, i32 *x, i32 *y) {
  static constexpr i32 half_x[5] = {4, 8, 16, 32, 64}, half_z[5] = {2, 4, 8, 16, 32};
  static constexpr i32 adjustment_y[5] = {1, 2, 4, 8, 16};
  i32 micro_x = SpEffectCoordinate(point.x), altitude = SpEffectCoordinate(point.y);
  i32 micro_z = SpEffectCoordinate(point.z);
  MicroActualGridToMicroDrawGrid(SpEffects::rotation, micro_x, micro_z, &micro_x, &micro_z);
  i32 sum = i32(u32(micro_z) + u32(micro_x)), difference = i32(u32(micro_z) - u32(micro_x));
  *x = i32(u32(difference) * u32(half_x[SpEffects::zoom])) / 256;
  i32 projected = i32(u32(sum) * u32(half_z[SpEffects::zoom])) / 256;
  *y = projected;
  i32 height = i32(u32(altitude) * u32(adjustment_y[SpEffects::zoom])) / 256;
  *y = i32(u32(projected) - u32(height));
}

// Choose the source frame for the two animated particle artwork kinds.
// Other kinds retain the rectangle written by Init or their existing preparation path.
void SpParticleSprite::SetInitialSourceRectForSprite(i32 zoom) {
  u32 age = age_frame;
  if (particle->kind == 6) {
    u32 width = particle_color[zoom]->Width() >> 2, height = particle_color[zoom]->Height() >> 2;
    i32 left = i32(width * u32(i32(frame))), top = i32(height * (age / 16));
    source[zoom].left = left;
    source[zoom].right = i32(width + u32(left));
    source[zoom].top = top;
    source[zoom].bottom = i32(height + u32(top));
  } else if (particle->kind == 2) {
    u32 height = particle_color[zoom]->Height() >> 2;
    source[zoom].left = 0;
    source[zoom].right = i32(particle_color[zoom]->Width());
    i32 top = i32(height * (age / 16));
    source[zoom].top = top;
    source[zoom].bottom = i32(height + u32(top));
  }
}

// Select the installed resource family and add the zoom word with DWORD wrapping.
// Unsupported kinds retain the original kind-as-base fallback rather than acquiring substitute artwork.
u32 SpParticleSprite::GetSpriteKey(i32 zoom, i32 kind) {
  u32 base;
  if (u32(kind) < 2 || kind == 3)
    base = kSpParticleKindsZeroOneThreeImageBase;
  else if (kind == 2)
    base = kSpParticleKindTwoImageBase;
  else if (kind == 4 || kind == 5)
    base = kSpParticleKindsFourFiveImageBase;
  else if (kind == 6)
    base = kSpParticleKindSixImageBase;
  else
    base = u32(kind);
  return u32(zoom) + base;
}

// Demand color, mask and the shared age-color table in the original sequence, retaining partial demands on failure.
// The shared table is pixel-locked before the source frame is selected.
u8 SpParticleSprite::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  u32 key = GetSpriteKey(zoom, kind);
  if (!SpEffects::images->PrepareImage(key, 0, &image) || !image)
    return 0;
  particle_color[zoom] = image->buffer;
  if (!SpEffects::images->PrepareImage(key + 256, 0, &image) || !image)
    return 0;
  particle_mask[zoom] = image->buffer;
  if (!SpEffects::images->PrepareImage(kSpParticleAgeColorTableImageId, 0, &image) || !image)
    return 0;
  color_table = image->buffer;
  color_table->Lock(GxLockFlags::GetSetPixel);
  SetInitialSourceRectForSprite(zoom);
  return 1;
}

i32 SpParticleSprite::ReleaseArt(i32 zoom, i32) {
  if (SpEffects::images) {
    u32 key = GetSpriteKey(zoom, kind);
    SpEffects::images->UnprepareImage(key, 0);
    particle_color[zoom] = nullptr;
    SpEffects::images->UnprepareImage(key + 256, 0);
    particle_mask[zoom] = nullptr;
    color_table->Unlock(GxLockFlags::GetSetPixel);
    SpEffects::images->UnprepareImage(kSpParticleAgeColorTableImageId, 0);
    color_table = nullptr;
  }
  return 0;
}

// Paint a particle through its age-color value and color artwork, then apply its optional bright mask.
// The same source rectangle and anchor feed both original blend-service operations.
void SpParticleSprite::DrawParticle(
    SpLightParticle *lead, GxBuffer *target, i32 zoom, i32 x, i32 y, ScRect *clip) const {
  ScPoint anchor = {x, y};
  SpGetBlendService()->AlphaBltConstSource(lead->color, target, particle_color[zoom], source[zoom], anchor, clip);
  if (particle_mask[zoom])
    SpGetBlendService()->BrightBlitConstAlpha(target, particle_mask[zoom], source[zoom], anchor, clip);
}

// Draw a standalone lead directly, or active trails followed by the active lead when a trail list exists.
// Listed particle pixel offsets are relative to the original destination anchor.
u8 SpParticleSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32, ScRect *clip) const {
  if (particle->trails.empty())
    DrawParticle(particle, target, zoom, x, y, clip);
  else {
    for (SpLightParticle *trail : particle->trails)
      if (trail->alive)
        DrawParticle(trail, target, zoom, i32(u32(x) + u32(trail->pixel.x)), i32(u32(y) + u32(trail->pixel.y)), clip);
    SpLightParticle *lead = particle;
    if (lead->alive)
      DrawParticle(lead, target, zoom, i32(u32(x) + u32(lead->pixel.x)), i32(u32(y) + u32(lead->pixel.y)), clip);
  }
  return 1;
}

// Use the rectangle's left/top as the anchor for the same original lead-and-trails draw order.
// The remaining rectangle words do not request stretching in this particle renderer.
u8 SpParticleSprite::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32, ScRect *clip) const {
  if (particle->trails.empty())
    DrawParticle(particle, target, zoom, rect.left, rect.top, clip);
  else {
    i32 x = rect.left, y = rect.top;
    for (SpLightParticle *trail : particle->trails)
      if (trail->alive)
        DrawParticle(trail, target, zoom, i32(u32(x) + u32(trail->pixel.x)), i32(u32(y) + u32(trail->pixel.y)), clip);
    SpLightParticle *lead = particle;
    if (lead->alive)
      DrawParticle(lead, target, zoom, i32(u32(x) + u32(lead->pixel.x)), i32(u32(y) + u32(lead->pixel.y)), clip);
  }
  return 1;
}

// Advance the lead and trail simulation, update age-color artwork and move the actual animation occupant.
// Retirement deletes the lead before removing membership; the original unwritten old-altitude word is not read.
i32 SpParticleSprite::Tick(u32 delta) {
  elapsed += delta;
  if (!particle)
    return 0;
  if (particle->IsCompletelyDead()) {
    delete particle;
    particle = nullptr;
    RemoveFromAnimationSystem();
    return 0;
  }
  particle->DoTick(i32(delta >> 10));
  ScVec3F previous = position;
  CalculateCurrentOutlineAndPosition(SpEffects::zoom);
  if (!SpLightParticle::PositionIsInWorldBounds(position)) {
    delete particle;
    particle = nullptr;
    position.x = previous.x;
    position.z = previous.z;
    RemoveFromAnimationSystem();
    return 0;
  }
  SpLightParticle *lead = particle;
  if (lead->age >= lead->lifetime)
    lead->age = u16(u32(lead->lifetime) - 1);
  assert(lead->lifetime);
  u32 fraction = (u32(lead->age) << 6) / lead->lifetime;
  if (color_table)
    particle->color = u16(color_table->GetPixel(fraction, u32(i32(lead->color_index))));
  else
    lead->color = 0xffff;
  if (particle->kind == 6) {
    u32 age_row = fraction >> 4;
    bool change_row = age_row != u32(age_frame >> 4), change_column = false;
    if (frame_started + kSpParticleFrameInterval < elapsed) {
      frame = i16(u16(frame) + 1);
      frame_started = elapsed;
      if (frame >= 4)
        frame = 0;
      change_column = true;
    }
    for (u32 at = 0; at < 5; ++at) {
      if (!particle_color[at])
        continue;
      if (change_column) {
        u32 width = particle_color[at]->Width() >> 2;
        i32 left = i32(width * u32(i32(frame)));
        source[at].left = left;
        source[at].right = i32(width + u32(left));
      }
      if (change_row) {
        u32 height = particle_color[at]->Height() >> 2;
        source[at].top = i32(height * age_row);
        source[at].bottom = i32(height + height * age_row);
      }
    }
  } else if (particle->kind == 2) {
    u32 age_row = fraction >> 4;
    if (age_row != u32(age_frame >> 4)) {
      for (u32 at = 0; at < 5; ++at) {
        if (particle_color[at]) {
          u32 height = particle_color[at]->Height() >> 2;
          source[at].top = i32(height * age_row);
          source[at].bottom = i32(height + height * age_row);
        }
      }
    }
  }
  age_frame = u16(fraction);
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(previous.x),
      SpEffectCoordinate(previous.z), SpEffectCoordinate(previous.y), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
  return 0;
}

// Union the active lead/trail pixel outlines, then move their offsets relative to the resulting sprite anchor.
// Native pointer storage replaces the original fixed temporary array without reproducing its out-of-bounds writes.
void SpParticleSprite::CalculateCurrentOutlineAndPosition(i32 zoom) {
  SpLightParticle *lead = particle;
  if (lead->trails.empty()) {
    area = source[zoom];
    i32 left = area.left;
    area.left = 0;
    area.right = i32(u32(area.right) - u32(left));
    area.bottom = i32(u32(area.bottom) - u32(area.top));
    area.top = 0;
    position = lead->position;
    return;
  }
  ScArray<ScPoint *> points;
  ScRect bounds = {0, 0, 0, 0};
  ScVec3F anchor = {0.0f, 0.0f, 0.0f};
  if (lead->alive) {
    ConvertPositionToCityPixel(lead->position, &lead->pixel.x, &lead->pixel.y);
    *points.Append() = &lead->pixel;
    bounds = {i32(u32(lead->pixel.x) - 1), i32(u32(lead->pixel.y) - 1), i32(u32(lead->pixel.x) + 2),
        i32(u32(lead->pixel.y) + 2)};
    anchor = lead->position;
  }
  for (SpLightParticle *trail : lead->trails) {
    if (!trail->alive)
      continue;
    ConvertPositionToCityPixel(trail->position, &trail->pixel.x, &trail->pixel.y);
    *points.Append() = &trail->pixel;
    ScRect rectangle = {i32(u32(trail->pixel.x) - 1), i32(u32(trail->pixel.y) - 1), i32(u32(trail->pixel.x) + 2),
        i32(u32(trail->pixel.y) + 2)};
    if (points.size == 1) {
      bounds = rectangle;
      anchor = trail->position;
    } else {
      bounds.left = ScIntMin(bounds.left, rectangle.left);
      bounds.top = ScIntMin(bounds.top, rectangle.top);
      bounds.right = ScIntMax(bounds.right, rectangle.right);
      bounds.bottom = ScIntMax(bounds.bottom, rectangle.bottom);
    }
  }
  area.left = area.top = 0;
  if (!points.size) {
    // No original active point initializes the remaining rectangle or anchor; exclude those undefined reads.
    area.right = area.bottom = 0;
    return;
  }
  area.right = i32(u32(bounds.right) - u32(bounds.left));
  area.bottom = i32(u32(bounds.bottom) - u32(bounds.top));
  i32 horizontal = 128 / (4 << SpEffects::zoom), vertical = 128 / (1 << SpEffects::zoom);
  i32 x = SpEffectCoordinate(anchor.x), altitude = SpEffectCoordinate(anchor.y), z = SpEffectCoordinate(anchor.z);
  for (u32 at = 0; at < points.size; ++at) {
    points.data[at]->x = i32(u32(points.data[at]->x) - u32(bounds.left));
    points.data[at]->y = i32(u32(points.data[at]->y) - u32(bounds.top));
  }
  MicroActualGridToMicroDrawGrid(SpEffects::rotation, x, z, &x, &z);
  x = i32(u32(x) + u32(horizontal) * u32(points.data[0]->x));
  z = i32(u32(z) - u32(horizontal) * u32(points.data[0]->x));
  altitude = i32(u32(vertical) * u32(points.data[0]->y) + u32(altitude));
  MicroActualGridToMicroDrawGrid(SpEffects::rotation, x, z, &x, &z);
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
}

SpEarthquake::SpEarthquake()
    : references(0), severity(1), registered(0), elapsed(0), word20(0), duration(5000), word28(0),
      first{}, second{}, third{} {
}

// Clear animation membership before removing the secondary tick identity from an available manager.
// The original destructor tolerates shutdown after the shared manager has already been released.
SpEarthquake::~SpEarthquake() {
  if (registered) {
    registered = 0;
    if (SpEffects::tick_manager)
      SpEffects::tick_manager->RemoveTickableObject(static_cast<SpTickable *>(this));
  }
}

// Publish only the primary Unknown or earthquake identity and retain it through the receiver's virtual method. Rejected
// interface requests clear the output.
bool SpEarthquake::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpEarthquakeView);
  HANDLE_QUERY_INTERFACE(IID_SpEarthquakeView, SpEarthquakeView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpEarthquake::AddRef() {
  return ++references;
}

// Delete the complete simulator at one reference; otherwise decrement and return the wrapping DWORD count.
// Animation removal happens through the real destructor before storage reclamation.
u32 SpEarthquake::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Copy the three original point triples and timing inputs only when the shared animation-occupant service exists.
u8 SpEarthquake::Init(const ScVec3I *start, const ScVec3I *end, ScVec3I *current, i32 new_duration,
    i8 new_severity, i32 argument, bool activate) {
  if (!SpEffects::animated)
    return 0;
  first = *start;
  second = *end;
  third = *current;
  duration = u32(new_duration);
  severity = new_severity;
  if (activate)
    DoEarthquakeEffect(new_severity, new_duration, argument);
  return 1;
}

// Copy the stored first, second and current positions into the three genuine outputs in that order.
void SpEarthquake::GetEarthquakePosition(ScVec3I *start, ScVec3I *end, ScVec3I *current) {
  *start = first;
  *end = second;
  *current = third;
}

// Publish animation membership before adding the actual secondary tick identity to the shared manager.
// Repeated activation does not acquire another manager registration.
void SpEarthquake::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::tick_manager->AddTickableObject(static_cast<SpTickable *>(this));
  }
}

// Clear animation membership before asking the shared manager to remove the secondary tick identity.
// This ordinary removal path retains the original required-manager invariant.
void SpEarthquake::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    SpEffects::tick_manager->RemoveTickableObject(static_cast<SpTickable *>(this));
  }
}

// Post the original full severity notification before registering ticks and resetting the phase counters.
void SpEarthquake::DoEarthquakeEffect(i32 new_severity, i32 new_duration, i32 argument) {
  i8 value = i8(new_severity);
  ScMessage message(kSpMsg_EarthquakeStarted, uptr(this), u32(new_severity), 0);
  SpEffects::messages->MessagePost(message, false);
  AddToAnimationSystem();
  word20 = 0;
  elapsed = 0;
  if (new_severity < 1)
    value = 1;
  if (new_duration < 1000)
    new_duration = 1000;
  severity = value;
  duration = u32(new_duration);
  word28 = 0;
  if (argument < 0)
    argument = 2 * i32(value);
  SpEffects::view->DoScreenShake(0, new_duration, 1, argument, true, 65);
}

// Advance the original wrapping microsecond counter and post completion at the duration threshold.
// Posting completes before membership is re-read and the secondary tick identity is removed.
void SpEarthquake::DoAnimationTick(u32 delta) {
  elapsed += delta;
  if (elapsed >= 1000u * duration) {
    ScMessage message(kSpMsg_EarthquakeEnded, uptr(this), u32(i32(severity)), 0);
    SpEffects::messages->MessagePost(message, false);
    RemoveFromAnimationSystem();
  }
}

void SpEarthquake::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 50;
  *interval = 4;
}

// Construct the registered earthquake simulator and return its unretained primary identity.
// The director's later interface query supplies the caller's actual ownership reference.
ScUnknown *SpCreateEarthquakeSimulator() {
  return static_cast<SpEarthquakeView *>(new SpEarthquake);
}

// Construct the original water-debris state with an empty animation clock and a ten-thousand-unit fade deadline. Its
// public frame interval starts at one million.
SpWaterDebris::SpWaterDebris()
    : elapsed(0), frame_elapsed(0), word216(0), registered(0), frame(0), frame_count(8),
      frame_interval(kSpWaterDebrisFrameInterval), fade_elapsed(0), time_to_live(10000) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpWaterDebris::~SpWaterDebris() {
  RemoveFromAnimationSystem();
}

bool SpWaterDebris::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpWaterDebrisView, SpWaterDebrisView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpWaterDebris::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpWaterDebris::Release() {
  return SpEffectOccupant::Release();
}

u32 SpWaterDebris::SetTimeBetweenFrames(u32 duration) {
  frame_interval = duration;
  return duration;
}

u32 SpWaterDebris::SetTimeToLive(u32 duration) {
  time_to_live = duration;
  return duration;
}

u32 SpWaterDebris::GetSpriteKey(i32 zoom, i32 value, u32 base) {
  return base + u32(zoom) + 5u * u32(value);
}

// Demand only the water color resource and copy its signed registration extents into the selected zoom.
// The original dereferences the acquired image directly; missing initialized artwork is outside its defined path.
u8 SpWaterDebris::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, UINT32_MAX), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  return 1;
}

// Release a held water color demand before clearing its borrowed buffer pointer.
// There is no separate mask demand, and the ordinary return is the original full zero word.
i32 SpWaterDebris::ReleaseArt(i32 zoom, i32) {
  if (SpEffects::images && color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, UINT32_MAX), 0);
    color[zoom] = nullptr;
  }
  return 0;
}

// Clear water-debris membership before removing its actual effects-occupant identity from the animation layer.
// The folded body tolerates a released global manager and does not touch members after removal.
void SpWaterDebris::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
}

// Advance the wrapping clocks and one cyclic frame only when the fixed million-unit interval has elapsed.
// After the fade deadline, decrease the stored float alpha and retire only when the unrounded result reaches zero.
i32 SpWaterDebris::Tick(u32 delta) {
  elapsed += delta;
  fade_elapsed += delta << 10;
  u32 frame_delta = elapsed - frame_elapsed;
  if (frame_delta <= kSpWaterDebrisFrameInterval)
    return 0;
  ++frame;
  frame_elapsed = elapsed;
  if (frame >= frame_count)
    frame = 0;
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    i32 width = GetW(zoom, 0);
    origins[zoom].x = i32(u32(frame) * u32(width));
  }
  if (fade_elapsed < time_to_live) {
    if (SpEffects::zoom >= 2)
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
          SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 2, nullptr);
  } else {
    mode = 5;
    f32 previous, stored;
    std::memcpy(&previous, &effect_value, sizeof(previous));
    f64 next = f64(previous) - f64(frame_delta) * 0.00000025;
    stored = f32(next);
    std::memcpy(&effect_value, &stored, sizeof(stored));
    if (next <= 0.0) {
      effect_value = 0;
      RemoveFromAnimationSystem();
    }
  }
  return 0;
}

// Construct the original nuclear clocks, ten-frame sequence and borrowed frame-pointer rows.
SpNuclear::SpNuclear()
    : first_type(0), debris_time(0), elapsed(0), last_frame(0), frame_interval(kSpNuclearInitialFrameInterval),
      debris_spawned(0), frame_count(10), current_frame(0), registered(0), frame_color{}, frame_mask{} {
  for (u32 i = 0; i < 27; ++i)
    frame_registration[0][i] = {};
  frame_registration[0][27].left = frame_registration[0][27].top = 0;
  geom_flags |= kSpEffectBaseGeometry;
}

SpNuclear::~SpNuclear() {
  RemoveFromAnimationSystem();
}

bool SpNuclear::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpNuclearView, SpNuclearView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpNuclear::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpNuclear::Release() {
  return SpEffectOccupant::Release();
}

// Initialize a nuclear effect inside the actual animation world, preserving the two signed-byte configuration words.
// Register occupancy before starting the original one-second view shake; existing frame clocks survive reinitialization.
u8 SpNuclear::Init(i32 x, i32 z, i32 altitude, i8 first, i8 second) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  position.x = f32(x);
  first_type = first;
  mode = 2;
  position.z = f32(z);
  second_type = second;
  position.y = f32(altitude);
  AddToAnimationSystem();
  SpEffects::view->DoScreenShake(0, 1000, 2, 2, true, 30);
  return 1;
}

// Publish nuclear animation membership before inserting the actual occupant into animation group two.
// The insertion uses rounded X/Z/altitude and acquires no synthetic controller reference.
void SpNuclear::AddToAnimationSystem() {
  if (!registered) {
    i32 altitude = SpEffectCoordinate(position.y);
    registered = 1;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), altitude, 2, 0, true);
  }
}

// Clear membership before removing the actual occupant from an available animation service.
// No member access follows removal, which may release the complete receiver.
void SpNuclear::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
}

// Choose one of the original two power-plant offsets and publish X/Z before reading terrain height.
// The altitude output is ten terrain levels above the sampled cell; the ordinary raw result byte is one.
u8 SpNuclear::GetCoordinatesForPowerPlantExplosion(i32 cell_x, i32 cell_z, i32 *x, i32 *z, i32 *altitude) {
  i32 offset_x, offset_z;
  if (SpEffects::random.Bounded(2)) {
    offset_x = 722;
    offset_z = 310;
  } else {
    offset_x = 306;
    offset_z = 666;
  }
  *x = i32((u32(cell_x) << kCtMicroCoordinateShift) + u32(offset_x));
  *z = i32((u32(cell_z) << kCtMicroCoordinateShift) + u32(offset_z));
  u8 terrain;
  SpEffects::dirt->GetVertexAltitude(u32(*x / 256), u32(*z / 256), &terrain);
  *altitude = (i32(terrain) + 10) << kCtMicroCoordinateShift;
  return 1;
}

u32 SpNuclear::GetSpriteKey(u32 base, i32 zoom, i32 frame) {
  return base + 32u * u32(zoom) + u32(frame);
}

// Demand the entire original color/mask frame row, caching signed registrations separately for the selected zoom.
u8 SpNuclear::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  for (i32 frame = 0; frame < frame_count; ++frame) {
    SpEffects::images->PrepareImage(GetSpriteKey(kSpNuclearColorImageBase, zoom, frame), 0, &image);
    frame_color[zoom][frame] = image->buffer;
    frame_registration[zoom][frame] = {image->left, image->top, image->right, image->bottom};
    SpEffects::images->PrepareImage(GetSpriteKey(kSpNuclearMaskImageBase, zoom, frame), 0, &image);
    frame_mask[zoom][frame] = image->buffer;
    if (frame == current_frame) {
      color[zoom] = frame_color[zoom][frame];
      mask[zoom] = frame_mask[zoom][frame];
      registration[zoom] = frame_registration[zoom][frame];
    }
  }
  return 1;
}

// Release every original frame demand in color-then-mask order and clear only the stored row pointers.
i32 SpNuclear::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  for (i32 frame = 0; frame < frame_count; ++frame) {
    SpEffects::images->UnprepareImage(GetSpriteKey(kSpNuclearColorImageBase, zoom, frame), 0);
    frame_color[zoom][frame] = nullptr;
    SpEffects::images->UnprepareImage(GetSpriteKey(kSpNuclearMaskImageBase, zoom, frame), 0);
    frame_mask[zoom][frame] = nullptr;
  }
  return 1;
}

// Spawn the original performance-scaled debris burst once, then advance at most one nuclear artwork frame per tick.
// Publish each available frame row before refresh; the last frame waits four million units before retiring occupancy.
i32 SpNuclear::Tick(u32 delta) {
  elapsed += delta;
  if (!debris_spawned && elapsed - debris_time > 2u * frame_interval) {
    debris_spawned = 1;
    if (SpEffects::zoom >= 2) {
      i32 count = 2 * SpEffects::performance->processor_power;
      while (count-- > 0) {
        SpDebris *debris = new SpDebris;
        debris->AddRef();
        debris->create_shadow = 0;
        i32 radius = SpEffects::random.Range(115, 135);
        f64 angle = SpEffects::random.RandomDoubleRangeUniform(0.0, 6.2831853);
        i32 offset_x = SpEffectCoordinate(std::cos(angle) * f64(radius));
        ScVec3F velocity;
        velocity.x = f32(offset_x);
        i32 x = SpEffectCoordinate(position.x);
        i32 offset_z = SpEffectCoordinate(std::sin(angle) * f64(radius));
        velocity.z = f32(offset_z);
        i32 z = SpEffectCoordinate(position.z);
        i32 vertical_offset = SpEffects::random.Range(-2000, -1000);
        velocity.y = f32(SpEffects::random.RandomDoubleRangeUniform(-10.0, 40.0));
        i32 altitude = SpEffectCoordinate(position.y);
        f32 speed = f32(SpEffects::random.RandomDoubleRangeUniform(1.5, f64(1.9f)));
        SpEffects::Scale3DVector(&velocity, speed);
        debris->Init(i32(u32(x) + u32(offset_x)), i32(u32(z) + u32(offset_z)),
            i32(u32(altitude) + u32(vertical_offset)), velocity, 1002, 0);
        debris->Release();
      }
    }
  }
  if (current_frame < 9) {
    if (elapsed - last_frame > frame_interval) {
      last_frame = elapsed;
      ++current_frame;
      for (i32 zoom = 0; zoom < 5; ++zoom) {
        GxBuffer *next = frame_color[zoom][current_frame];
        if (next) {
          color[zoom] = next;
          mask[zoom] = frame_mask[zoom][current_frame];
          registration[zoom] = frame_registration[zoom][current_frame];
        }
      }
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
          SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 2, nullptr);
    }
  } else if (elapsed - last_frame > kSpNuclearLaterFrameInterval)
    RemoveFromAnimationSystem();
  return 0;
}
