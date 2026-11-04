#include "sprites/ufo_effects.h"

#include <math.h>
#include <string.h>

#include <cassert>
#include <cstring>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "geometry/ge_types.h"
#include "graphics/image_codecs.h"
#include "occupants/api.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/disaster_effects.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "terrain/api.h"

// Small UFO frames advance after this elapsed-time interval.
enum SpUfoFrameTiming : u32 {
  kSpUfoFrameInterval = 0x186a0,
};

// Random plasma targets choose near and far signed offsets in either direction from the UFO origin.
enum SpUfoPlasmaTargetOffset : u32 {
  kSpUfoPlasmaNegativeNearOffset = 0xfffffd00,
  kSpUfoPlasmaNegativeFarOffset = 0xfffff600,
};

// Original effect families retain independent frame, scheduling and lifetime thresholds.
enum SpUfoTiming : i32 {
  kSpUfoMinimumShotDelay = 10000000,
  kSpUfoMaximumShotDelay = 20000000,
};

// The simulator posts this model notification when removal empties the shared UFO pool.
enum SpUfoNotification : u32 {
  kSpMsg_LastUfoRemoved = 0x63092d3c,
};

// Toxic clouds and UFOs retain independent color, mask, shadow and beam resource-key offsets.
enum SpUfoImageOffset : i32 {
  kSpToxicCloudColorKeyOffset = 0x3ec00,
  kSpToxicCloudMaskKeyOffset = 0x3eb00,
  kSpToxicCloudShadowKeyOffset = 0x3ea00,
  kSpUfoShadowKeyOffset = 0x1efb0,
  kSpUfoBeamMaskKeyOffset = 0x1ef90,
};

// Toxic-cloud instances begin with this original lifetime.
enum SpToxicCloudDuration : i32 {
  kSpToxicCloudDuration = 0x895440,
};

// The UFO prepares separate color and shadow families plus the buffer registered as an overlay.
enum SpUfoArtwork : u32 {
  kSpUfoColorImageBase = 0xfffe1040,
  kSpUfoShadowImageBase = 0xfffe1050,
  kSpUfoOverlayImageId = 0xfffe1060,
};

// Preserve the installed biased double-store coordinate conversion used by animation membership.
static i32 SpToxicCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return i32(u32(bits) + kScDoubleIntegerLowWordBias);
}

// Construct the cloud and return its original secondary display identity to the class factory.
ScUnknown *SpCreateToxicCloudDisplay() {
  return static_cast<SpToxicCloudDisplay *>(new SpToxicCloud);
}

SpToxicCloud::SpToxicCloud()
    : velocity{0.0f, 0.0f, 0.0f}, byte216(0), registered(0), word220(0), elapsed(0), duration(kSpToxicCloudDuration),
      fade_rate(0.00015000001f), notify(0), shadow(nullptr), rain(nullptr), stop_rain(0), owner_id(0) {
  effect_value = 0;
  mode = 6;
  geom_flags |= kSpEffectBaseGeometry;
}

SpToxicCloud::~SpToxicCloud() {
  RemoveFromAnimationSystem();
}

bool SpToxicCloud::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpToxicCloudDisplay, SpToxicCloudDisplay);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpToxicCloud::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpToxicCloud::Release() {
  return SpEffectOccupant::Release();
}

u32 SpToxicCloud::GetSpriteKey(i32 zoom, i32 variant, i32 base) {
  return u32(base) + u32(zoom) + (zoom < 4 ? 1u : 0u) + 5 * u32(variant);
}

// Refresh each cloud registration rectangle from cached color metadata, doubling the largest zoom's coordinates.
void SpToxicCloud::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, 0, -kSpToxicCloudColorKeyOffset), 0);
    if (image) {
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
      if (zoom >= 4) {
        registration[zoom].left *= 2;
        registration[zoom].top *= 2;
        registration[zoom].right *= 2;
        registration[zoom].bottom *= 2;
        shifts[zoom] = 1;
      }
    }
  }
}

// Acquire the cloud color and mask and optional shadow mask, then set the frame's horizontal origin.
// The original directly consumes each returned image, so nonnull outputs are postconditions.
u8 SpToxicCloud::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, 0, -kSpToxicCloudColorKeyOffset), 0, &image);
  assert(image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  if (zoom >= 4) {
    registration[zoom].left *= 2;
    registration[zoom].top *= 2;
    registration[zoom].right *= 2;
    registration[zoom].bottom *= 2;
    shifts[zoom] = 1;
  }
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, 0, -kSpToxicCloudMaskKeyOffset), 0, &image);
  assert(image);
  mask[zoom] = image->buffer;
  if (shadow) {
    SpEffects::images->PrepareImage(u32(zoom) - kSpToxicCloudShadowKeyOffset, 0, &image);
    assert(image);
    shadow->mask[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  i32 width = GetW(zoom, 0);
  origins[zoom].x = i32(u32(byte216) * u32(width));
  return 1;
}

// Release each acquired cloud or shadow image through the cache and clear its borrowed buffer pointer.
i32 SpToxicCloud::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, 0, -kSpToxicCloudColorKeyOffset), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, 0, -kSpToxicCloudMaskKeyOffset), 0);
    mask[zoom] = nullptr;
  }
  if (shadow && shadow->mask[zoom]) {
    SpEffects::images->UnprepareImage(u32(zoom) - kSpToxicCloudShadowKeyOffset, 0);
    shadow->mask[zoom] = nullptr;
  }
  return 0;
}

// Retire requested rain, advance the fade and notification, and keep the shadow on terrain.
i32 SpToxicCloud::Tick(u32 delta) {
  if (stop_rain && rain) {
    rain->StopRaining();
    rain->Release();
    rain = nullptr;
    stop_rain = 0;
  }
  if (fade_rate < 0.0f || fade_rate > 0.0f) {
    f32 previous;
    memcpy(&previous, &effect_value, sizeof(previous));
    elapsed += delta;
    f64 next = f64(elapsed) * fade_rate * f64(0.001f) + previous;
    bool refresh = false, finished = false;
    f32 result = previous;
    if (!(next >= 0.0)) {
      result = 0.0f;
      refresh = finished = true;
    } else if (next > 1.0) {
      result = 1.0f;
      refresh = finished = true;
    } else {
      f64 change = next - previous;
      if (change < 0.0)
        change = -change;
      if (change > f64(0.03f)) {
        result = f32(next);
        elapsed = 0;
        refresh = true;
      }
    }
    if (refresh) {
      memcpy(&effect_value, &result, sizeof(result));
      if (shadow)
        memcpy(&shadow->effect_value, &result, sizeof(result));
      if (finished) {
        mode = 2;
        fade_rate = 0.0f;
        if (result == 1.0f)
          word220 = SpEffects::random.Bounded(duration);
        if (notify) {
          ScMessage message(kSpMsg_ToxicCloudRetired, owner_id, reinterpret_cast<uptr>(&position), 0);
          SpEffects::messages->MessagePost(message, false);
          RemoveFromAnimationSystem();
          return 0;
        }
      }
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), i32(i64(position.x)),
          i32(i64(position.z)), i32(i64(position.y)), 2, nullptr);
      if (shadow)
        SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(shadow), i32(i64(shadow->position.x)),
            i32(i64(shadow->position.z)), i32(i64(shadow->position.y)), 2, nullptr);
    }
  }
  if (shadow) {
    i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z);
    if (shadow->position.y != f32(altitude)) {
      i32 x = SpToxicCoordinate(position.x), z = SpToxicCoordinate(position.z);
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(shadow), x, z, i32(i64(position.y)), x, z,
          altitude);
      shadow->position.y = f32(altitude);
    }
  }
  return 0;
}

// Place an in-bounds cloud, create its original shadow on higher-performance systems and register its animation.
// Keep the borrowed owner's full native address for the eventual retirement notification.
u8 SpToxicCloud::Init(uptr owner, i32 x, i32 z, i32 altitude) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height))
    return 0;
  owner_id = owner;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  if (SpEffects::performance->processor_power >= 4) {
    shadow = ScAddRefNotNull(new SpObjectShadow);
    shadow->position.x = f32(x);
    shadow->position.z = f32(z);
    shadow->position.y = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z));
    shadow->mode = 8;
    shadow->effect_value = 0;
  }
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Set the fade direction, restart elapsed time, select the cloud layer and preserve the notification byte.
u8 SpToxicCloud::DoFade(u8 visible, u8 value) {
  fade_rate = visible ? 0.00015000001f : -0.00015000001f;
  elapsed = 0;
  mode = 6;
  notify = value;
  return value;
}

void SpToxicCloud::SetVelocity(const ScVec3F &value) {
  velocity = value;
}

// Create and retain a rain display at the cloud's position, above the cloud's current altitude.
u8 SpToxicCloud::StartRaining() {
  if (rain)
    return 0;
  rain = ScAddRefNotNull(new SpAcidRain);
  rain->Init(i32(i64(position.x)), i32(i64(position.z)), i32(u32(i32(i64(position.y))) + 2400u));
  return 1;
}

u8 SpToxicCloud::StopRaining() {
  stop_rain = 1;
  return 1;
}

void SpToxicCloud::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::cell_map->IncrementAlphaBlendCount(1);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpToxicCoordinate(position.x),
        SpToxicCoordinate(position.z), SpToxicCoordinate(position.y), 5, 0, true);
    if (shadow)
      SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(shadow), SpToxicCoordinate(shadow->position.x),
          SpToxicCoordinate(shadow->position.z), SpToxicCoordinate(shadow->position.y), 1, 0, true);
  }
}

// Clear active membership before retiring rain and animation objects in their original order.
void SpToxicCloud::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (rain) {
      rain->StopRaining();
      rain->Release();
      rain = nullptr;
    }
    if (SpEffects::animated) {
      if (shadow) {
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(shadow), SpToxicCoordinate(shadow->position.x),
            SpToxicCoordinate(shadow->position.z), SpToxicCoordinate(shadow->position.y));
        shadow->Release();
        shadow = nullptr;
      }
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpToxicCoordinate(position.x),
          SpToxicCoordinate(position.z), SpToxicCoordinate(position.y));
      SpEffects::cell_map->IncrementAlphaBlendCount(-1);
    }
  }
}

// Deferred group operations wake a target queue; its private commands retire or destroy the group.
enum SpUfoQueueMessage : u32 {
  kSpMsg_ProcessUfoQueue = 0x83790460,
  kSpMsg_RetireUfoGroup = 0x1378f94a,
  kSpMsg_DestroyAllUfos = 0x2378f94a,
};

static ScArray<SpUfoSimulator *> SpAllUfos;
static SpUfo *SpBigUfoBeingShown;

static void SpLockUfos() {
  while (SpEffects::thread_safe_flag.fetch_or(1, std::memory_order_acquire) & 1)
    ScYieldThread();
}

static void SpUnlockUfos() {
  SpEffects::thread_safe_flag.store(0, std::memory_order_release);
}

// Construct the original UFO-group factory product and return its primary interface without acquiring a reference.
ScUnknown *SpCreateUfoGroup() {
  return static_cast<SpUfoGroupSimulator *>(new SpUfoGroup());
}

// Convert a stored float with the installed biased-double rounding operation without an invalid integer cast.
static u32 SpUfoCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

i32 SpUfoPassedPosition2D(const ScVec3F &start, const ScVec3F &current, const ScVec3F &target) {
  return fabs(f64(current.x) - start.x) >= fabs(f64(target.x) - start.x) &&
      fabs(f64(current.z) - start.z) >= fabs(f64(target.z) - start.z);
}

bool SpUfoPassedPosition3D(const ScVec3F &start, const ScVec3F &current, const ScVec3F &target) {
  f32 currentX = f32(f64(current.x) - start.x), targetX = f32(f64(target.x) - start.x);
  if (fabs(currentX) >= fabs(f64(target.x) - start.x) &&
      fabs(f64(current.z) - start.z) >= fabs(f64(target.z) - start.z) &&
      fabs(f64(current.y) - start.y) >= fabs(f64(target.y) - start.y))
    return true;
  if (fabs(f64(current.x) - target.x) < fabs(targetX) ||
      fabs(f64(current.z) - target.z) < fabs(f64(target.z) - start.z))
    return false;
  f32 remainingY = f32(fabs(f64(current.y) - target.y));
  f32 targetY = f32(f64(target.y) - start.y);
  return remainingY >= fabs(targetY);
}

SpUfo::SpUfo()
    : registered(0), automaticAttack(1), instructionsActive(0), state220(0), state221(5), level(0), state223(0),
      word224(0), turnDirection(1), destination{0.0f, 0.0f, 0.0f}, direction{0.0f, 1.0f, 0.0f}, movementSpeed(2.6f),
      word260(0), word264(0), word268(kSpUfoFrameInterval), word272(0), soundTimer(ScStopwatch::Milliseconds, 0),
      shadow(nullptr), state340(0), image344(nullptr), overlayPosition{0, 0}, vector356{0.0f, 0.0f, 0.0f} {
  SpLockUfos();
  *SpAllUfos.Append() = static_cast<SpUfoSimulator *>(this);
  SpUnlockUfos();
  geom_flags |= kSpEffectBaseGeometry;
}

SpUfo::~SpUfo() {
  Shutdown();
}

// Remove the simulator from the shared UFO pool, post the final-removal notification and leave the animation system.
void SpUfo::Shutdown() {
  SpLockUfos();
  for (u32 i = 0; i < SpAllUfos.size; ++i) {
    if (SpAllUfos.data[i] != static_cast<SpUfoSimulator *>(this))
      continue;
    for (u32 j = i + 1; j < SpAllUfos.size; ++j)
      SpAllUfos.data[j - 1] = SpAllUfos.data[j];
    --SpAllUfos.size;
    if (!SpAllUfos.size && SpEffects::messages) {
      ScMessage message(kSpMsg_LastUfoRemoved);
      SpEffects::messages->MessagePost(message, false);
    }
    break;
  }
  SpUnlockUfos();
  RemoveFromAnimationSystem();
}

// Register the UFO and optional shadow, then retain the overlay image if the simulator has not disabled it.
void SpUfo::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), i32(SpUfoCoordinate(position.x)),
      i32(SpUfoCoordinate(position.z)), i32(SpUfoCoordinate(position.y)), 1, 0, true);
  if (shadow)
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(shadow),
        i32(SpUfoCoordinate(shadow->position.x)), i32(SpUfoCoordinate(shadow->position.z)),
        i32(SpUfoCoordinate(shadow->position.y)), 1, 0, true);
  if (!image344 && !state340) {
    SpImage *image = nullptr;
    if (SpEffects::images->PrepareImage(kSpUfoOverlayImageId, 0, &image))
      image344 = ScAddRefNotNull(image->buffer);
  }
}

// Stop UFO audio and remove registered sprites, abductees, beam resources and overlays in the original order.
void SpUfo::RemoveFromAnimationSystem() {
  TurnOffAnyCurrentlyPlayingSounds(1);
  if (!registered)
    return;
  registered = 0;
  if (image344) {
    if (!state340 && SpEffects::cell_map)
      SpEffects::cell_map->RemoveOverlayBuffer(overlayId);
    image344->Release();
    image344 = nullptr;
    if (SpEffects::images)
      SpEffects::images->UnprepareImage(kSpUfoOverlayImageId, 0);
  }
  ClearAbductionList();
  if (instructionsActive && !instructions.empty())
    KillCurrentInstruction();
  u8 held = references;
  SpBasicSprite *currentShadow = shadow;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), i32(SpUfoCoordinate(position.x)),
        i32(SpUfoCoordinate(position.z)), i32(SpUfoCoordinate(position.y)));
  if (currentShadow) {
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(currentShadow),
          i32(SpUfoCoordinate(currentShadow->position.x)), i32(SpUfoCoordinate(currentShadow->position.z)),
          i32(SpUfoCoordinate(currentShadow->position.y)));
    currentShadow->Release();
    if (held > 1)
      shadow = nullptr;
  }
}

void SpUfo::TurnOffAnyCurrentlyPlayingSounds(u8 force) {
  if (soundTimer.IsTimerRunning() && (force || soundTimer.GetElapsedTime() > 3000)) {
    soundTimer.Reset();
    if (SpEffects::box)
      SpEffects::box->Event(kAuEventEffectSlot52, kAuEffectDisasterUfoSuckUpPeople,
          u32(i32(SpUfoCoordinate(position.x)) / 256),
          u32(i32(SpUfoCoordinate(position.z)) / 256), 0);
  }
}

bool SpUfo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpUfoSimulator, SpUfoSimulator);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpUfo::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpUfo::Release() {
  return SpEffectOccupant::Release();
}

// Advance the UFO, its animation frame, instructions and abducted occupants, then notify the animation manager.
// Movement and beam instructions retain their separate timing, animation and notification order.
i32 SpUfo::Tick(u32 elapsed) {
  ScVec3F oldPosition = position;
  u8 frameChanged = 0, moved = 0;
  word260 += elapsed;
  f32 delta = f32(f64(elapsed) * 0.001);
  TurnOffAnyCurrentlyPlayingSounds(0);
  if (!state340) {
    if (SpEffects::view_id == 0) {
      if (!SpBigUfoBeingShown)
        SpBigUfoBeingShown = this;
      if (SpBigUfoBeingShown == this) {
        DoBigUFOSimulation(delta);
        return 0;
      }
    }
    state340 = 1;
  }
  if (word260 > word264 + word268) {
    word264 = word260;
    state220 = i8(u8(state220) + u8(turnDirection));
    if (state220 >= i32(state221))
      state220 = 0;
    else if (state220 < 0)
      state220 = i8(state221 - 1);
    for (i32 zoom = 0; zoom < 5; ++zoom)
      origins[zoom].x = i32(state220) * GetW(zoom, 0);
    frameChanged = 1;
  }
  if (instructionsActive) {
    if (!DoInstructionState(&moved, &frameChanged, delta))
      return 0;
  } else {
    position.x = f32(f64(delta) * direction.x + position.x);
    position.z = f32(f64(delta) * direction.z + position.z);
    position.y = f32(f64(delta) * direction.y + position.y);
    if (SpUfoPassedPosition3D(oldPosition, position, destination)) {
      position = destination;
      GenerateNewRandomDestinationPosition();
      state223 = i8(u8(state223) + 1);
      if (state223 > 5) {
        SpUfoInstruction retirement{};
        retirement.opcode = kSpUfoOpFadeOut;
        AddInstruction(retirement);
        return 1;
      }
    }
    moved = 1;
  }
  if (automaticAttack && word260 > word272) {
    word272 = word260 + GetMicrosecondsTillNextPlasmaShot();
    if (!CurrentlyDoingBuildingVaporizationOrCropCircleProduction())
      ShootPlasmaBall(-1, -1, -1);
  }
  for (auto it = abducted.begin(); it != abducted.end();) {
    ScOccupant *occupant = *it;
    CtCoord before{INT32_MAX, INT32_MAX, INT32_MAX};
    occupant->GetLocation(&before);
    ScVec3F oldTarget{f32(before.x), f32(before.altitude), f32(before.z)};
    ScVec3F velocity{
        f32(f64(position.x) - oldTarget.x), f32(f64(position.y) - oldTarget.y), f32(f64(position.z) - oldTarget.z)};
    SpEffects::Scale3DVector(&velocity, 2.0f);
    CtCoord after{i32(f64(velocity.x) * delta + before.x), i32(f64(velocity.z) * delta + before.z),
        i32(f64(velocity.y) * delta + before.altitude)};
    ScVec3F newTarget{f32(after.x), f32(after.altitude), f32(after.z)};
    if (SpUfoPassedPosition3D(oldTarget, newTarget, position) || u32(after.x) >= u32(SpEffects::micro_width) ||
        u32(after.z) >= u32(SpEffects::micro_height)) {
      it = abducted.erase(it);
      SpEffects::animated->RemoveOccupant(occupant);
      occupant->Release();
    } else {
      ++it;
      occupant->SetLocation(after);
      SpEffects::animated->MoveOccupant(
          occupant, before.x, before.z, before.altitude, after.x, after.z, after.altitude);
    }
  }
  i32 oldX = i32(SpUfoCoordinate(oldPosition.x)), oldZ = i32(SpUfoCoordinate(oldPosition.z));
  i32 oldY = i32(SpUfoCoordinate(oldPosition.y));
  if (moved) {
    i32 x = i32(SpUfoCoordinate(position.x)), z = i32(SpUfoCoordinate(position.z));
    i32 altitude = i32(SpUfoCoordinate(position.y));
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), oldX, oldZ, oldY, x, z, altitude);
    if (shadow) {
      u8 terrain;
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &terrain);
      shadow->position.y = f32(u32(terrain) << 8);
      i32 shadowX = i32(SpUfoCoordinate(shadow->position.x)), shadowZ = i32(SpUfoCoordinate(shadow->position.z));
      i32 shadowY = i32(SpUfoCoordinate(shadow->position.y));
      SpEffects::animated->MoveOccupant(
          static_cast<SpEffectOccupant *>(shadow), shadowX, shadowZ, shadowY, x, z, shadowY);
      shadow->position.x = f32(x);
      shadow->position.z = f32(z);
    }
  } else if (frameChanged)
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), oldX, oldZ, oldY, 2, nullptr);
  return 0;
}

void SpUfo::DoBigUFOSimulation(f32 elapsed) {
  if (!image344) {
    state340 = 1;
    SpBigUfoBeingShown = nullptr;
    return;
  }
  const ScRect *view = SpEffects::cell_map->GetCityBufferPtr()->GetBufferArea();
  if (vector356.x == 0.0f && vector356.y == 0.0f) {
    const ScRect *image = image344->GetBufferArea();
    ScRect bounds{
        i32(u32(view->left) - u32(image->right)), i32(u32(view->top) - u32(image->bottom)), view->right, view->bottom};
    u32 side = SpEffects::random.Bounded(4);
    f32 speed = f32(SpEffects::random.RandomDoubleRangeUniform(f64(0.2f), f64(0.4f)));
    i32 targetX, targetY;
    if (side == 0) {
      overlayPosition.x = bounds.left;
      overlayPosition.y = SpEffects::random.Range(bounds.top, bounds.bottom);
      targetX = view->right;
      targetY = SpEffects::random.Range(view->top, view->bottom);
    } else if (side == 1) {
      overlayPosition.x = SpEffects::random.Range(bounds.left, bounds.right);
      overlayPosition.y = bounds.top;
      targetX = SpEffects::random.Range(view->left, view->right);
      targetY = view->bottom;
    } else if (side == 2) {
      overlayPosition.x = bounds.right;
      overlayPosition.y = SpEffects::random.Range(bounds.top, bounds.bottom);
      targetX = view->left;
      targetY = SpEffects::random.Range(view->top, view->bottom);
    } else {
      overlayPosition.x = SpEffects::random.Range(bounds.left, bounds.right);
      overlayPosition.y = bounds.bottom;
      targetX = SpEffects::random.Range(view->left, view->right);
      targetY = view->top;
    }
    f64 dx = f64(i32(u32(targetX) - u32(overlayPosition.x)));
    f64 dy = f64(i32(u32(targetY) - u32(overlayPosition.y)));
    vector356.x = f32(dx);
    vector356.y = f32(dy);
    f64 scale = f64(speed) / sqrt(dx * dx + dy * dy);
    vector356.x = f32(scale * vector356.x);
    vector356.y = f32(scale * vector356.y);
    if (SpEffects::cell_map)
      overlayId = SpEffects::cell_map->AddOverlayBuffer(overlayPosition, image344, nullptr);
    return;
  }
  overlayPosition.x = i32(u32(overlayPosition.x) + SpUfoCoordinate(f64(elapsed) * vector356.x));
  overlayPosition.y = i32(u32(overlayPosition.y) + SpUfoCoordinate(f64(elapsed) * vector356.y));
  ScRect area{overlayPosition.x, overlayPosition.y, i32(u32(overlayPosition.x) + image344->Width()),
      i32(u32(overlayPosition.y) + image344->Height())};
  if (area.IntersectInclusive(&area, view)) {
    if (SpEffects::cell_map)
      SpEffects::cell_map->MoveOverlayBuffer(&overlayPosition, overlayId);
  } else {
    state340 = 1;
    SpBigUfoBeingShown = nullptr;
    if (SpEffects::cell_map)
      SpEffects::cell_map->RemoveOverlayBuffer(overlayId);
  }
}

// Initialize flight, difficulty and optional shadow before registering the UFO in the animation system.
u8 SpUfo::Init(const ScVec3F &initial, u8 attack, i32 difficulty, i8 initialLevel) {
  position = initial;
  level = i8(difficulty);
  word224 = initialLevel;
  automaticAttack = attack;
  if (!SpEffects::animated)
    return 0;
  if (position.x == -1.0f) {
    position.x = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, i32(u32(SpEffects::micro_width) - 256)));
    position.z = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, i32(u32(SpEffects::micro_height) - 256)));
    i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z);
    position.y = f32(SpEffects::random.RandomDoubleRangeUniform(altitude + 5120, altitude + 25600));
  } else {
    i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z);
    if (altitude > i32(SpUfoCoordinate(position.y)))
      position.y = f32(SpEffects::random.RandomDoubleRangeUniform(altitude + 5120, altitude + 10240));
  }
  turnDirection = SpEffects::random.Bounded(2) ? 1 : -1;
  GenerateNewRandomDestinationPosition();
  if (SpUfoCoordinate(position.x) >= u32(SpEffects::micro_width) ||
      SpUfoCoordinate(position.z) >= u32(SpEffects::micro_height)) {
    position.x = position.z = 0.0f;
    destination.x = destination.z = 256.0f;
  }
  if (SpEffects::performance->processor_power >= 4) {
    shadow = ScAddRefNotNull(new SpBasicSprite());
    shadow->position.x = f32(i32(SpUfoCoordinate(position.x)));
    shadow->position.z = f32(i32(SpUfoCoordinate(position.z)));
    shadow->position.y = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z));
    shadow->mode = 4;
  }
  word272 = GetMicrosecondsTillNextPlasmaShot();
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Launch a plasma trajectory toward the requested or randomly selected city position and post its positional sound.
void SpUfo::ShootPlasmaBall(i32 x, i32 z, i32 altitude) {
  i32 originX = i32(SpUfoCoordinate(position.x)), originZ = i32(SpUfoCoordinate(position.z));
  i32 originAltitude = i32(SpUfoCoordinate(position.y));
  if (x == -1) {
    bool positiveX = SpEffects::random.Bounded(2) != 0;
    i32 x1 = i32(u32(originX) + (positiveX ? 768u : kSpUfoPlasmaNegativeNearOffset));
    i32 x2 = i32(u32(originX) + (positiveX ? 2560u : kSpUfoPlasmaNegativeFarOffset));
    x = SpEffects::random.Range(x1, x2);
    bool positiveZ = SpEffects::random.Bounded(2) != 0;
    i32 z1 = i32(u32(originZ) + (positiveZ ? 768u : kSpUfoPlasmaNegativeNearOffset));
    i32 z2 = i32(u32(originZ) + (positiveZ ? 2560u : kSpUfoPlasmaNegativeFarOffset));
    z = SpEffects::random.Range(z1, z2);
    altitude = 0;
  }
  if (x < 0)
    x = 0;
  if (x >= SpEffects::micro_width)
    x = i32(u32(SpEffects::micro_width) - 1);
  if (z < 0)
    z = 0;
  if (z >= SpEffects::micro_height)
    z = i32(u32(SpEffects::micro_height) - 1);
  ScVec3F target{f32(x), f32(altitude), f32(z)};
  ScVec3F velocity{f32(f64(target.x) - position.x), f32(f64(target.y) - position.y), f32(f64(target.z) - position.z)};
  SpEffects::Scale3DVector(&velocity, 0.6f);
  SpTrajectory *trajectory = new SpTrajectory();
  trajectory->AddRef();
  trajectory->EnableTrailingSmoke(1);
  trajectory->SetSourceSignature(kSpUfoPlasmaTrajectorySignature);
  trajectory->SetTrajectoryType(SpTrajectoryView::Motion::Linear, nullptr);
  trajectory->Init(originX, originZ, originAltitude, velocity, 0);
  trajectory->Release();
  if (SpEffects::box)
    SpEffects::box->Event(kAuEventPlayEffect, kAuEffectUfoFireLaserBomb, u32(i32(SpUfoCoordinate(position.x)) / 256),
        u32(i32(SpUfoCoordinate(position.z)) / 256), 0);
}

// Demand the UFO color image and optional shadow mask, then prepare instruction-specific sprites.
u8 SpUfo::DemandArt(i32 zoom, i32 rotation, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, level, kSpUfoColorImageBase), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  if (shadow) {
    SpEffects::images->PrepareImage(u32(zoom - kSpUfoShadowKeyOffset), 0, &image);
    shadow->mask[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  PrepareInstructionSpecificSprites(zoom, rotation);
  return 1;
}

i32 SpUfo::ReleaseArt(i32 zoom, i32 rotation) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, level, kSpUfoColorImageBase), 0);
    color[zoom] = nullptr;
  }
  if (shadow && shadow->mask[zoom]) {
    SpEffects::images->UnprepareImage(u32(zoom - kSpUfoShadowKeyOffset), 0);
    shadow->mask[zoom] = nullptr;
  }
  UnprepareInstructionSpecificSprites(zoom, rotation);
  return 0;
}

// Publish the UFO's X, Z and altitude in the original order using its effects-occupant position.
void SpUfo::GetPosition(u32 *x, u32 *z, u32 *altitude) {
  *x = SpUfoCoordinate(position.x);
  *z = SpUfoCoordinate(position.z);
  *altitude = SpUfoCoordinate(position.y);
}

u8 SpUfo::SetAutoAttack(u8 enabled) {
  automaticAttack = enabled;
  return enabled;
}

i32 SpUfo::CanOccupantBeAbducted(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomPerson) || occupant->IsGeomFlag(kOcGeomRoadVehicle);
}

// Identify an active building-vaporization or crop-circle instruction by its original opcode.
// An active call requires a queued instruction; the original reads the front without testing for an empty list.
u8 SpUfo::CurrentlyDoingBuildingVaporizationOrCropCircleProduction() {
  if (!instructionsActive)
    return 0;
  assert(!instructions.empty());
  u32 opcode = instructions.front().opcode;
  return opcode == kSpUfoOpVaporize || opcode == kSpUfoOpCropCircle;
}

u32 SpUfo::GetMicrosecondsTillNextPlasmaShot() {
  return (u32(SpEffects::random.Range(kSpUfoMinimumShotDelay, kSpUfoMaximumShotDelay)) * u32(8 - i32(level))) >> 3;
}

// Set the destination and normalize its direction at the current movement speed when the separation exceeds one unit.
// A near-identical position uses the original upward unit vector instead.
void SpUfo::GenerateNewSpecificDestinationPosition(const ScVec3F &from, const ScVec3F &to) {
  f64 dx = f64(to.x) - from.x, dy = f64(to.y) - from.y, dz = f64(to.z) - from.z;
  if (fabs(dx) > 1.0 || fabs(dz) > 1.0 || fabs(dy) > 1.0) {
    direction = {f32(dx), f32(dy), f32(dz)};
    SpEffects::Scale3DVector(&direction, movementSpeed);
  } else
    direction = {0.0f, 1.0f, 0.0f};
  destination = to;
}

// Choose a nearby destination clipped to the city, sample its terrain height and assign the resulting flight vector.
void SpUfo::GenerateNewRandomDestinationPosition() {
  ScRect nearby{i32(SpUfoCoordinate(f64(position.x) - 1280.0)), i32(SpUfoCoordinate(f64(position.z) - 1280.0)),
      i32(SpUfoCoordinate(f64(position.x) + 1280.0)), i32(SpUfoCoordinate(f64(position.z) + 1280.0))};
  ScRect bounds{256, 256, i32(u32(SpEffects::micro_width) - 256), i32(u32(SpEffects::micro_height) - 256)};
  bounds.IntersectInclusive(&bounds, &nearby);
  destination.x = f32(SpEffects::random.RandomDoubleRangeUniform(bounds.left, bounds.right));
  destination.z = f32(SpEffects::random.RandomDoubleRangeUniform(bounds.top, bounds.bottom));
  i32 x = i32(SpUfoCoordinate(destination.x)), z = i32(SpUfoCoordinate(destination.z));
  u8 altitude;
  SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
  i32 base = i32(altitude) << 8;
  destination.y = f32(SpEffects::random.RandomDoubleRangeUniform(base + 5120, base + 10240));
  GenerateNewSpecificDestinationPosition(position, destination);
}

u32 SpUfo::GetSpriteKey(i32 zoom, i32 difficulty, u32 base) {
  return base + u32(zoom) + 5u * u32(difficulty);
}

// Read available image registrations for every UFO zoom and its optional shadow without demanding image buffers.
void SpUfo::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, level, kSpUfoColorImageBase), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (shadow) {
      image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, level, kSpUfoShadowImageBase), 0);
      if (image)
        shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    }
  }
}

// Adjust the active movement for terrain clearance and insert the original destination after a short intermediate step.
// The original byte result says whether an extra movement was inserted.
u8 SpUfo::FixFrontMoveInstructionToAvoidObjects() {
  SpUfoInstruction &front = instructions.front();
  ScVec3F start = position, target = front.position;
  ScVec3F step{f32(f64(target.x) - start.x), 0.0f, f32(f64(target.z) - start.z)};
  if (fabs(step.x) >= 0.0099999998 || fabs(step.z) >= 0.0099999998) {
    SpEffects::Scale2DVector(&step, 1200.0f);
    step.x = f32(f64(step.x) + start.x);
    step.z = f32(f64(step.z) + start.z);
  } else {
    step.x = start.x;
    step.z = start.z;
  }
  if (SpUfoPassedPosition2D(start, step, target)) {
    i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(target.x, target.z) + 7680;
    if (i32(SpUfoCoordinate(target.y)) < altitude)
      front.position.y = f32(altitude);
    return 0;
  }
  auto next = instructions.begin();
  ++next;
  instructions.insert(next, front);
  ScVec3F difference{f32(f64(target.x) - start.x), 0.0f, f32(f64(target.z) - start.z)};
  f32 distance = f32(SpEffects::LengthOf2DVector(difference));
  f64 elevation = f64(target.y) - start.y;
  if (distance > 0.1)
    elevation *= 1200.0 / distance;
  step.y = f32(elevation + start.y);
  i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(step.x, step.z) + 7680;
  if (i32(SpUfoCoordinate(step.y)) < altitude)
    step.y = f32(altitude);
  front.position = step;
  return 1;
}

// Prepare the first instruction, updating its destination and inserting a raised approach when a target is occupied.
void SpUfo::PrepNewInstruction() {
  SpUfoInstruction &front = instructions.front();
  switch (front.opcode) {
  case kSpUfoOpWait:
    front.wait.elapsed = 0;
    break;
  case kSpUfoOpMove:
    FixFrontMoveInstructionToAvoidObjects();
    GenerateNewSpecificDestinationPosition(position, front.position);
    break;
  case kSpUfoOpVaporize:
  case kSpUfoOpCropCircle: {
    i32 x = i32(f64(front.position.x) / 256.0), z = i32(f64(front.position.z) / 256.0);
    ScOccupant *occupant = nullptr;
    SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant);
    if (occupant) {
      u32 cellX, cellZ;
      occupant->GetLocationInCellCoords(&cellX, &cellZ);
      u32 half = u32(occupant->SizeInCellsX()) << 7;
      x = i32((cellX << 8) + half);
      z = i32((cellZ << 8) + half);
      front.position.x = f32(x);
      front.position.z = f32(z);
    }
    u8 altitude;
    SpEffects::dirt->GetVertexAltitude(u32(occupant ? x / 256 : x), u32(occupant ? z / 256 : z), &altitude);
    u32 height = (u32(altitude) + 16) << 8;
    if (occupant) {
      height += occupant->SizeInAnimCellsY();
      front.position.y = f32(height);
      occupant->Release();
      SpUfoInstruction approach{};
      approach.opcode = kSpUfoOpMove;
      approach.position = front.position;
      approach.position.y = f32(f64(front.position.y) + 5120.0);
      auto next = instructions.begin();
      ++next;
      instructions.insert(next, approach);
    } else
      front.position.y = f32(height);
    GenerateNewSpecificDestinationPosition(position, front.position);
    front.effectState = 0;
    front.elapsed = 0;
    front.beam = nullptr;
    front.word28 = front.word32 = 0;
    break;
  }
  case kSpUfoOpFadeOut:
    front.wait.duration = 0;
    break;
  default:
    break;
  }
  instructionsActive = 1;
}

// Advance the active instruction, including movement, beam effects, abduction, spawning and retirement.
// Remove completed instructions and prepare the next instruction or select a fresh random destination.
u8 SpUfo::DoInstructionState(u8 *moved, u8 *frameChanged, f32 elapsed) {
  SpUfoInstruction &front = instructions.front();
  ScVec3F before = position;
  switch (front.opcode) {
  case kSpUfoOpWait:
    front.wait.elapsed += SpUfoCoordinate(elapsed);
    if (front.wait.elapsed <= front.wait.duration)
      return 1;
    break;
  case kSpUfoOpMove:
  case kSpUfoOpVaporize:
  case kSpUfoOpCropCircle:
    if (front.opcode == kSpUfoOpMove || !front.effectState) {
      position.x = f32(f64(elapsed) * direction.x + position.x);
      position.z = f32(f64(elapsed) * direction.z + position.z);
      position.y = f32(f64(elapsed) * direction.y + position.y);
      *moved = 1;
      if (!SpUfoPassedPosition3D(before, position, destination))
        return 1;
      position = destination;
      if (front.opcode == kSpUfoOpMove)
        break;
      front.effectState = 1;
      if (SpEffects::box)
        SpEffects::box->Event(kAuEventPlayEffect,
            front.opcode == kSpUfoOpVaporize ? kAuEffectUfoVaporizeBuilding : kAuEffectUfoMakeCropCircle,
            u32(i32(SpUfoCoordinate(position.x)) / 256), u32(i32(SpUfoCoordinate(position.z)) / 256), 0);
      return 1;
    }
    if (front.opcode == kSpUfoOpVaporize ? (front.word32 != 0 || front.beam != nullptr) :
        (front.word32 != 0 && front.beam != nullptr)) {
      front.word32 += SpUfoCoordinate(elapsed);
      if (front.beam && i32(front.word32) > 300) {
        ++front.word28;
        front.word32 = 1;
        if (i32(front.word28) >= 5)
          front.word28 = 0;
        for (i32 zoom = 0; zoom < 5; ++zoom)
          front.beam->origins[zoom].x = i32(front.word28) * front.beam->GetW(zoom, 0);
        SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(front.beam),
            i32(SpUfoCoordinate(front.beam->position.x)), i32(SpUfoCoordinate(front.beam->position.z)),
            i32(SpUfoCoordinate(front.beam->position.y)), 2, nullptr);
      }
    } else {
      front.word32 = 1;
      front.beam = new SpBasicSprite();
      front.beam->AddRef();
      front.beam->position = {position.x, f32(f64(position.y) - 5.0), position.z};
      front.beam->mode = 4;
      PrepareBeamSprite(SpEffects::zoom, SpEffects::rotation, i32(front.opcode));
      SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(front.beam),
          i32(SpUfoCoordinate(front.beam->position.x)), i32(SpUfoCoordinate(front.beam->position.z)),
          i32(SpUfoCoordinate(front.beam->position.y)), 1, 0, true);
    }
    front.elapsed += SpUfoCoordinate(elapsed);
    if (i32(front.elapsed) <= 5000)
      return 1;
    KillCurrentInstruction();
    if (SpEffects::messages) {
      u32 x = u16(i64(position.x)), z = u16(i64(position.z));
      u32 cell = ((x & 0xff00) << 8) | (z >> 8);
      ScMessage notification(
          front.opcode == kSpUfoOpVaporize ? kSpMsg_UfoBuildingVaporized : kSpMsg_UfoCropCircleCreated,
          reinterpret_cast<uptr>(static_cast<SpUfoSimulator *>(this)), cell, 0);
      SpEffects::messages->MessagePost(notification, false);
    }
    word272 = word260 + GetMicrosecondsTillNextPlasmaShot();
    break;
  case kSpUfoOpExplode: {
    SpExplosion *explosion = new SpExplosion();
    explosion->AddRef();
    explosion->Init(i32(SpUfoCoordinate(position.x)), i32(SpUfoCoordinate(position.z)),
        i32(SpUfoCoordinate(position.y)), u8(level), 5, 0);
    explosion->Release();
    if (SpEffects::box)
      SpEffects::box->Event(kAuEventPlayEffect, kAuEffectUfoExplodes, u32(i32(SpUfoCoordinate(position.x)) / 256),
          u32(i32(SpUfoCoordinate(position.z)) / 256), 0);
    Shutdown();
    return 0;
  }
  case kSpUfoOpFadeOut: {
    front.wait.duration += SpUfoCoordinate(elapsed);
    mode = 5;
    f64 alpha = 1.0 - f64(i32(front.wait.duration)) * f64(0.00033330001f);
    f32 stored = f32(alpha);
    memcpy(&effect_value, &stored, sizeof(stored));
    if (alpha > 0.0) {
      *frameChanged = 1;
      return 1;
    }
    stored = 0.0f;
    memcpy(&effect_value, &stored, sizeof(stored));
    Shutdown();
    return 0;
  }
  case kSpUfoOpDropBomb:
    ShootPlasmaBall(i32(SpUfoCoordinate(front.position.x)), i32(SpUfoCoordinate(front.position.z)),
        i32(SpUfoCoordinate(front.position.y)));
    break;
  case kSpUfoOpAbduct: {
    ScOccupant *occupant = front.occupant;
    if (SpEffects::messages) {
      ScMessage notification(kSpMsg_UfoAbduction, reinterpret_cast<uptr>(static_cast<SpUfoSimulator *>(this)),
          reinterpret_cast<uptr>(&position), reinterpret_cast<uptr>(occupant));
      SpEffects::messages->MessagePost(notification, false);
    }
    if (occupant)
      AddToAbductionList(occupant);
    else
      GetAndAddMiscellaneousObjectsToAbductionList(1);
    soundTimer.Restart();
    if (SpEffects::box)
      SpEffects::box->Event(kAuEventPlayEffect, kAuEffectDisasterUfoSuckUpPeople,
          u32(i32(SpUfoCoordinate(position.x)) / 256), u32(i32(SpUfoCoordinate(position.z)) / 256), 0);
    break;
  }
  case kSpUfoOpSetSpeed:
    movementSpeed = front.speed;
    break;
  case kSpUfoOpSpawn: {
    SpUfo *child = new SpUfo();
    child->Init(position, automaticAttack, level, i8(word224));
    if (front.group)
      front.group->AddUFOToGroup(child, 1, 1);
    else {
      SpUfoInstruction movement{};
      movement.opcode = kSpUfoOpMove;
      movement.position.x = f32(SpEffects::random.Range(100, i32(u32(SpEffects::micro_width) - 100)));
      movement.position.z = f32(SpEffects::random.Range(100, i32(u32(SpEffects::micro_height) - 100)));
      movement.position.y = f32(f64(f32(8000.0 * f64(8.94f))) + position.y);
      child->AddInstruction(movement);
      movement.opcode = kSpUfoOpFadeOut;
      child->AddInstruction(movement);
    }
    break;
  }
  default:
    break;
  }
  instructions.pop_front();
  if (instructions.empty()) {
    instructionsActive = 0;
    GenerateNewRandomDestinationPosition();
  } else
    PrepNewInstruction();
  return 1;
}

void SpUfo::PrepareInstructionSpecificSprites(i32 zoom, i32 rotation) {
  if (!instructions.empty()) {
    SpUfoInstruction &front = instructions.front();
    if ((front.opcode == kSpUfoOpVaporize || front.opcode == kSpUfoOpCropCircle) && front.beam)
      PrepareBeamSprite(zoom, rotation, i32(front.opcode));
  }
}

// Unprepare the active instruction's beam sprite while the shared image manager is available.
void SpUfo::UnprepareInstructionSpecificSprites(i32 zoom, i32 rotation) {
  if (SpEffects::images && !instructions.empty()) {
    SpUfoInstruction &front = instructions.front();
    if ((front.opcode == kSpUfoOpVaporize || front.opcode == kSpUfoOpCropCircle) && front.beam)
      UnprepareBeamSprite(zoom, rotation, i32(front.opcode));
  }
}

// Demand the front instruction's beam mask and copy its registration into the owned beam sprite.
// Shade drawing uses the same mask plane as the UFO shadow rather than a color source.
void SpUfo::PrepareBeamSprite(i32 zoom, i32, i32) {
  SpBasicSprite *beam = instructions.front().beam;
  if (beam) {
    SpImage *image = nullptr;
    SpEffects::images->PrepareImage(u32(zoom - kSpUfoBeamMaskKeyOffset), 0, &image);
    beam->mask[zoom] = image->buffer;
    beam->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    beam->origins[zoom].x = 0;
  }
}

// Release one beam mask or all five zoom masks while the shared image manager is available.
void SpUfo::UnprepareBeamSprite(i32 zoom, i32, i32) {
  if (!SpEffects::images)
    return;
  SpBasicSprite *beam = instructions.front().beam;
  if (!beam)
    return;
  if (zoom == 5) {
    for (i32 i = 0; i < 5; ++i)
      if (beam->mask[i]) {
        SpEffects::images->UnprepareImage(u32(i - kSpUfoBeamMaskKeyOffset), 0);
        beam->mask[i] = nullptr;
      }
  } else {
    SpEffects::images->UnprepareImage(u32(zoom - kSpUfoBeamMaskKeyOffset), 0);
    beam->mask[zoom] = nullptr;
  }
}

void SpUfo::KillCurrentInstruction() {
  SpUfoInstruction &front = instructions.front();
  if ((front.opcode != kSpUfoOpVaporize && front.opcode != kSpUfoOpCropCircle) || !front.beam)
    return;
  SpBasicSprite *beam = front.beam;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(beam), i32(SpUfoCoordinate(beam->position.x)),
        i32(SpUfoCoordinate(beam->position.z)), i32(SpUfoCoordinate(beam->position.y)));
  UnprepareBeamSprite(5, 4, i32(front.opcode));
  beam->Release();
  front.beam = nullptr;
}

// Retain a newly abducted occupant, change its geometry flags and insert it into the animation manager without ticking.
u8 SpUfo::AddToAbductionList(ScOccupant *occupant) {
  SpLockUfos();
  for (ScOccupant *item : abducted)
    if (item == occupant) {
      SpUnlockUfos();
      return 0;
    }
  abducted.push_back(occupant);
  SpUnlockUfos();
  occupant->AddRef();
  occupant->ClearGeomFlag(kOcAllGeometryFlags);
  occupant->SetGeomFlag(kSpAbductedOccupantGeometry);
  SpEffects::animated->InsertOccupant(occupant, 1, 0, true);
  SpEffects::animated->SetShouldTick(occupant, false);
  return 1;
}

u8 SpUfo::GetAndAddMiscellaneousObjectsToAbductionList(u32 count) {
  CtCoord first{512, 512, 0};
  CtCoord last{i32(u32(SpEffects::micro_width) - 512), i32(u32(SpEffects::micro_height) - 512), 0x10000};
  GeBounds bounds(first, last);
  ScOccupant *occupants[1024];
  u32 available = 1024, added = 0;
  if (!SpEffects::animated->GetOccupantsInVolume(bounds, occupants, &available))
    return 0;
  u32 total = available;
  while (available && added < count) {
    u32 selected = SpEffects::random.Bounded(available);
    ScOccupant *occupant = occupants[selected];
    if (CanOccupantBeAbducted(occupant)) {
      AddToAbductionList(occupant);
      ++added;
    }
    --available;
    occupants[selected] = occupants[available];
    occupants[available] = occupant;
  }
  for (u32 i = 0; i < total; ++i)
    occupants[i]->Release();
  return 1;
}

// Remove retained abductees in list order, unlocking while each animation removal and release is performed.
void SpUfo::ClearAbductionList() {
  SpLockUfos();
  while (!abducted.empty()) {
    ScOccupant *occupant = abducted.front();
    abducted.pop_front();
    SpUnlockUfos();
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(occupant);
    occupant->Release();
    SpLockUfos();
  }
  SpUnlockUfos();
}

void SpUfo::ClearAllInstructions() {
  instructions.clear();
  instructionsActive = 1;
}

// Append a UFO instruction, inserting a preliminary elevated movement for building vaporization or crop circles.
// Prepare a newly nonempty queue immediately, preserving the preliminary movement and requested instruction order.
u8 SpUfo::AddInstruction(const SpUfoInstruction &instruction) {
  instructionsActive = 1;
  if (instructions.size() >= 256)
    return 0;
  if (instruction.opcode == kSpUfoOpVaporize || instruction.opcode == kSpUfoOpCropCircle) {
    SpUfoInstruction movement{};
    movement.opcode = kSpUfoOpMove;
    movement.position = instruction.position;
    movement.position.y += 2560.0f;
    instructions.push_back(movement);
    if (instructions.size() == 1)
      PrepNewInstruction();
  }
  instructions.push_back(instruction);
  if (instructions.size() == 1)
    PrepNewInstruction();
  return 1;
}

// Reduce an active queue to its current instruction, or clear it when that instruction is a movement.
// Then enqueue the original explosion instruction.
void SpUfo::DestroySelfAsSoonAsPossible() {
  if (instructionsActive && !instructions.empty()) {
    if (instructions.front().opcode == kSpUfoOpMove)
      instructions.clear();
    else
      while (instructions.size() > 1)
        instructions.pop_back();
  }
  SpUfoInstruction retirement{};
  retirement.opcode = kSpUfoOpExplode;
  AddInstruction(retirement);
}

SpUfoGroup::SpUfoGroup() : references(0), level(0), automaticAttack(0) {
}

// Grow the group with newly initialized UFOs or shut down trailing members, limiting the requested count to 256.
u8 SpUfoGroup::SetCount(u32 count) {
  if (count >= 256)
    count = 256;
  if (members.size < count) {
    u32 remaining = count - members.size;
    while (remaining--) {
      SpUfo *ufo = new SpUfo();
      ScVec3F initial{-1.0f, -1.0f, -1.0f};
      ufo->Init(initial, automaticAttack, level, 0);
      AddUFOToGroup(ufo, 1, 1);
    }
  }
  if (members.size > count) {
    u32 remaining = members.size - count;
    while (remaining--)
      AddUFOToGroup(members.data[members.size - 1], 0, 1);
  }
  return 1;
}

// Remove and shut down the last UFO repeatedly before canceling queued target messages.
// The queue and member storage are then destroyed by their real owning fields.
SpUfoGroup::~SpUfoGroup() {
  while (members.size)
    AddUFOToGroup(members.data[members.size - 1], 0, 1);
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(this);
}

// Publish the same primary identity for the UFO-group and message-target IIDs and retain it on success.
bool SpUfoGroup::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, SpUfoGroupSimulator);
  HANDLE_QUERY_INTERFACE(IID_SpUfoGroupSimulator, SpUfoGroupSimulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpUfoGroup::AddRef() {
  return ++references;
}

// Destroy the group when its previous reference count was one; otherwise return the decremented word.
u32 SpUfoGroup::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Drain queued UFO instructions when the target notification arrives and dispatch their original packed payloads.
// Retirement and global-destruction messages return immediately after invoking their respective operations.
bool SpUfoGroup::DoMessage(ScMessage *message) {
  if (message->type != kSpMsg_ProcessUfoQueue)
    return true;
  while (messages.GetMessageCount()) {
    ScMessage event;
    messages.GetFrontMessage(&event);
    u32 second = u32(event.data2), third = u32(event.data3);
    i32 index = i32(u32(event.data1));
    switch (event.type) {
    case kSpUfoOpVaporize:
      AddInstructionVaporizeBuilding(i32(second >> 24), i32((second >> 16) & 255), index, 0);
      break;
    case kSpUfoOpCropCircle:
      AddInstructionMakeCropCircle(i32(second >> 24), i32((second >> 16) & 255), index, 0);
      break;
    case kSpUfoOpExplode:
    case kSpUfoOpFadeOut:
      AddInstructionSelfDestruct(second != 0, index, 0);
      break;
    case kSpUfoOpAbduct:
      AddInstructionSuckUpPerson(reinterpret_cast<ScOccupant *>(event.data2), index, 0);
      break;
    case kSpUfoOpSpawn:
      AddInstructionSpawnUFO(second != 0, index, 0);
      break;
    case kSpUfoOpMove: {
      ScRect bounds{i32(second >> 16), i32(second & 0xffff), i32(third >> 16), i32(third & 0xffff)};
      AddInstructionMigrateToArea(bounds, index, 0);
      break;
    }
    case kSpUfoOpSetSpeed:
      AddInstructionSetSpeed(*reinterpret_cast<const f32 *>(event.data2), index, 0);
      break;
    case kSpUfoOpDropBomb:
      AddInstructionDropBomb(i32(second), i32(third), i32(second), 0);
      break;
    case kSpMsg_RetireUfoGroup:
      Shutdown();
      Release();
      return true;
    case kSpMsg_DestroyAllUfos:
      DestroyAllUFOs(0);
      return true;
    default:
      break;
    }
  }
  return true;
}

// Queue asynchronous group retirement and post the corresponding target notification when the server exists.
// Return the original reference-count-minus-one word without releasing the group synchronously.
u32 SpUfoGroup::ReleaseAsynchronous() {
  ScMessage retirement(kSpMsg_RetireUfoGroup);
  messages.PushBack(retirement);
  if (SpEffects::messages) {
    ScMessage notify(kSpMsg_ProcessUfoQueue);
    SpEffects::messages->GeneralMessagePostToTarget(notify, this);
  }
  return references - 1;
}

// Queue global destruction or remove group members and retire a locked snapshot of the global UFO collection.
u32 SpUfoGroup::DestroyAllUFOs(u8 deferred) {
  if (deferred) {
    ScMessage request(kSpMsg_DestroyAllUfos);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 0;
  }
  while (members.size)
    AddUFOToGroup(members.data[members.size - 1], 0, 1);
  ScArray<SpUfoSimulator *> snapshot;
  SpLockUfos();
  snapshot.Reserve(SpAllUfos.size);
  for (u32 i = 0; i < SpAllUfos.size; ++i)
    *snapshot.Append() = SpAllUfos.data[i];
  SpUnlockUfos();
  for (u32 i = 0; i < snapshot.size; ++i)
    snapshot.data[i]->DestroySelfAsSoonAsPossible();
  return snapshot.size;
}

// Set the group's level and attack mode before requesting its initial UFO count.
// Refuse initialization when the shared animation occupant manager is unavailable.
u8 SpUfoGroup::Init(u8 enabled, u32 count, i32 requestedLevel) {
  if (!SpEffects::animated)
    return 0;
  level = requestedLevel;
  automaticAttack = enabled;
  SetCount(count);
  return 1;
}

u8 SpUfoGroup::Shutdown() {
  return SetCount(0);
}

// Add a previously absent UFO with one retained reference, or remove a matching member in its existing order.
// The removal path optionally shuts the UFO down before releasing it and returns the original zero byte.
u8 SpUfoGroup::AddUFOToGroup(SpUfoSimulator *ufo, u8 add, u8 shutdown) {
  u32 index = 0;
  while (index < members.size && members.data[index] != ufo)
    ++index;
  if (add) {
    if (index != members.size)
      return 0;
    ufo->AddRef();
    *members.Append() = ufo;
    return 1;
  }
  if (index < members.size) {
    if (shutdown)
      members.data[index]->Shutdown();
    members.data[index]->Release();
    for (u32 i = index + 1; i < members.size; ++i)
      members.data[i - 1] = members.data[i];
    --members.size;
  }
  return 0;
}

u32 SpUfoGroup::GetCount() {
  return members.size;
}

// Return the borrowed UFO interface at the requested index, or null beyond the current group count.
SpUfoSimulator *SpUfoGroup::GetUFO(u32 index) {
  return index < members.size ? members.data[index] : nullptr;
}

// Search the group's live interfaces for the specified UFO without changing its ownership.
bool SpUfoGroup::DoesUFOBelongToGroup(const SpUfoSimulator *ufo) const {
  for (u32 i = 0; i < members.size; ++i)
    if (members.data[i] == ufo)
      return true;
  return false;
}

// Find the first group member with the smallest instruction-list size, or return minus one for an empty group.
i32 SpUfoGroup::GetUFOWithLowestInstructionLoad() {
  u32 minimum = UINT32_MAX;
  i32 selected = -1;
  for (u32 i = 0; i < members.size; ++i) {
    auto *ufo = static_cast<SpUfo *>(members.data[i]);
    u32 count = u32(ufo->instructions.size());
    if (count < minimum) {
      minimum = count;
      selected = i32(i);
    }
  }
  return selected;
}

u32 SpUfoGroup::GetInstructionLoad() {
  u32 count = members.size, total = 0;
  if (!count)
    return 100;
  for (u32 i = 0; i < count; ++i)
    total += u32(static_cast<SpUfo *>(members.data[i])->instructions.size());
  u32 percent = (100u * total) / (5u * count);
  return i32(percent) <= 100 ? percent : 100;
}

// Check the shared UFO collection while holding the original simulation lock.
bool SpUfoGroup::DoesAnyUFOExistAnywhere() const {
  SpLockUfos();
  bool found = SpAllUfos.size != 0;
  SpUnlockUfos();
  return found;
}

// Search the shared UFO collection under the simulation lock without acquiring another UFO reference.
bool SpUfoGroup::DoesUFOExistAnywhere(const SpUfoSimulator *ufo) const {
  SpLockUfos();
  u32 index = 0;
  while (index < SpAllUfos.size && SpAllUfos.data[index] != ufo)
    ++index;
  bool found = index != SpAllUfos.size;
  SpUnlockUfos();
  return found;
}

// Return the global count, or copy up to the caller's capacity of borrowed UFO pointers while locked.
u32 SpUfoGroup::GetAllUFOsAnywhere(SpUfoSimulator **objects, u32 *count) const {
  SpLockUfos();
  if (objects) {
    if (*count > SpAllUfos.size)
      *count = SpAllUfos.size;
    for (u32 i = 0; i < *count; ++i)
      objects[i] = SpAllUfos.data[i];
  } else
    *count = SpAllUfos.size;
  SpUnlockUfos();
  return *count;
}

// Read the visible cell rectangle under the graphics lock, then copy nearby UFOs up to the caller's capacity.
u32 SpUfoGroup::GetListOfCurrentlyVisibleUFOs(SpUfoSimulator **objects, u32 *count) const {
  ScRect pixels, cells;
  SpEffects::city->LockGraphics();
  SpEffects::cell_map->GetVisibleAreaRectangle(&pixels, &cells);
  SpEffects::city->UnlockGraphics();
  i32 centerX = i32((u32(cells.left) + u32(cells.right)) << 8) / 2;
  i32 centerZ = i32((u32(cells.top) + u32(cells.bottom)) << 8) / 2;
  f32 radius = f32(i32((u32(cells.right) - u32(centerX / 256) + 3) << 8));
  u32 capacity = *count;
  SpLockUfos();
  *count = 0;
  for (u32 i = 0; i < SpAllUfos.size && *count < capacity; ++i) {
    auto *ufo = static_cast<SpUfo *>(SpAllUfos.data[i]);
    f64 dx = f64(ufo->position.x) - centerX, dz = f64(ufo->position.z) - centerZ;
    if (sqrt(dx * dx + dz * dz) < radius) {
      if (objects)
        objects[*count] = ufo;
      ++*count;
    }
  }
  SpUnlockUfos();
  return *count;
}

// Ask the original visibility query for one entry and report its full-word success result as zero or one.
i32 SpUfoGroup::IsAnyUFOCurrentlyVisible() const {
  SpUfoSimulator *one;
  u32 count = 1;
  return GetListOfCurrentlyVisibleUFOs(&one, &count) != 0;
}

// Queue a packed vaporization request or immediately insert it into the chosen UFO's instruction list.
// A negative index selects the first UFO with the least queued work; oversized indexes select the last member.
u8 SpUfoGroup::AddInstructionVaporizeBuilding(i32 x, i32 z, i32 index, u8 deferred) {
  if (deferred) {
    u32 packed = ((u32(x) & 255) << 24) | ((u32(z) & 255) << 16);
    ScMessage request(kSpUfoOpVaporize, u32(index), packed, 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpVaporize;
  instruction.position = {f32(i32(u32(x) << 8)), 0.0f, f32(i32(u32(z) << 8))};
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue a packed crop-circle request or immediately insert it into the selected UFO's instruction list.
// Preserve the original index clamping and least-loaded selection order.
u8 SpUfoGroup::AddInstructionMakeCropCircle(i32 x, i32 z, i32 index, u8 deferred) {
  if (deferred) {
    u32 packed = ((u32(x) & 255) << 24) | ((u32(z) & 255) << 16);
    ScMessage request(kSpUfoOpCropCircle, u32(index), packed, 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpCropCircle;
  instruction.position = {f32(i32(u32(x) << 8)), 0.0f, f32(i32(u32(z) << 8))};
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue or immediately insert the requested self-destruction instruction for one UFO.
// On immediate success remove its retained group membership without shutting down the simulator.
u8 SpUfoGroup::AddInstructionSelfDestruct(u8 explode, i32 index, u8 deferred) {
  if (deferred) {
    ScMessage request(explode ? kSpUfoOpExplode : kSpUfoOpFadeOut, u32(index), explode ? explode : 0, 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoSimulator *ufo = members.data[index];
  SpUfoInstruction instruction{};
  instruction.opcode = explode ? kSpUfoOpExplode : kSpUfoOpFadeOut;
  if (!static_cast<SpUfo *>(ufo)->AddInstruction(instruction))
    return 0;
  AddUFOToGroup(ufo, 0, 0);
  return 1;
}

// Queue an occupant-abduction request or retain the occupant and insert the request into one UFO's instruction list.
// Keep the original retained reference even if immediate instruction insertion fails.
u8 SpUfoGroup::AddInstructionSuckUpPerson(ScOccupant *occupant, i32 index, u8 deferred) {
  if (deferred) {
    ScMessage request(kSpUfoOpAbduct, u32(index), reinterpret_cast<uptr>(occupant), 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpAbduct;
  instruction.occupant = occupant;
  if (occupant)
    occupant->AddRef();
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue a spawn request or insert an instruction carrying this group when spawning is enabled.
u8 SpUfoGroup::AddInstructionSpawnUFO(u8 enabled, i32 index, u8 deferred) {
  if (deferred) {
    ScMessage request(kSpUfoOpSpawn, u32(index), enabled, 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpSpawn;
  instruction.group = enabled ? this : nullptr;
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue a bomb request or insert a terrain-height position into the chosen UFO's instruction list.
u8 SpUfoGroup::AddInstructionDropBomb(i32 x, i32 z, i32 index, u8 deferred) {
  if (deferred) {
    ScMessage request(kSpUfoOpDropBomb, u32(index), u32(x), u32(z));
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpDropBomb;
  instruction.position.x = f32(x);
  instruction.position.z = f32(z);
  instruction.position.y =
      f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(instruction.position.x, instruction.position.z));
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue the packed rectangle or draw an immediate migration destination using the shared effects random stream.
u8 SpUfoGroup::AddInstructionMigrateToArea(const ScRect &bounds, i32 index, u8 deferred) {
  if (deferred) {
    u32 first = (u32(bounds.left) << 16) | (u32(bounds.top) & 0xffff);
    u32 second = (u32(bounds.right) << 16) | (u32(bounds.bottom) & 0xffff);
    ScMessage request(kSpUfoOpMove, u32(index), first, second);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  i32 right = bounds.right > bounds.left ? bounds.right : i32(u32(bounds.left) + 1);
  i32 bottom = bounds.bottom > bounds.top ? bounds.bottom : i32(u32(bounds.top) + 1);
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpMove;
  instruction.position.x = f32(SpEffects::random.Range(bounds.left, right));
  instruction.position.z = f32(SpEffects::random.Range(bounds.top, bottom));
  instruction.position.y =
      f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(instruction.position.x, instruction.position.z));
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}

// Queue the original speed word or insert the float directly into the selected UFO's instruction list.
u8 SpUfoGroup::AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) {
  if (deferred) {
    u32 bits;
    memcpy(&bits, &speed, sizeof(bits));
    ScMessage request(kSpUfoOpSetSpeed, u32(index), bits, 0);
    messages.PushBack(request);
    if (SpEffects::messages) {
      ScMessage notify(kSpMsg_ProcessUfoQueue);
      SpEffects::messages->GeneralMessagePostToTarget(notify, this);
    }
    return 1;
  }
  if (!members.size)
    return 0;
  if (index >= i32(members.size))
    index = i32(members.size - 1);
  if (index < 0)
    index = GetUFOWithLowestInstructionLoad();
  if (index == -1)
    return 0;
  SpUfoInstruction instruction{};
  instruction.opcode = kSpUfoOpSetSpeed;
  instruction.speed = speed;
  return static_cast<SpUfo *>(members.data[index])->AddInstruction(instruction) != 0;
}
