#include "sprites/disaster_effects.h"
#include <string.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/ids.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_locust.h"
#include "geometry/ge_types.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "occupants/api.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "terrain/api.h"

// Seed the orbit and position from the supplied vectors, preserving the original random draw and bounds checks.
// On supported hardware, acquire the real shadow before selecting the frame interval and publishing animation occupancy.
u8 SpTornadoDebris::Init(ScVec3F *input_center, ScVec3F *motion, i32 input_variant, i8 style) {
  byte204 = u8(style);
  variant = i16(input_variant);
  center = *input_center;
  velocity = *motion;
  radius = 300.0f;
  radial_speed = 0.00003f;
  target_height = 160.0f;
  f64 initial_angle = SpEffects::random.RandomDoubleRangeUniform(0.0, f64(6.2831855f));
  angle = f32(initial_angle);
  angular_speed = 0.000015f;
  vertical_speed = 0.00013f;
  position.x = f32(std::cos(initial_angle) * radius + center.x);
  position.y = center.y;
  f64 z = std::sin(f64(angle)) * radius + center.z;
  position.z = f32(z);
  if (u32(SpDebrisCoordinate(position.x)) < u32(SpEffects::micro_width) &&
      u32(SpDebrisCoordinate(z)) < u32(SpEffects::micro_height) && SpEffects::animated) {
    if (SpEffects::performance->processor_power >= 4) {
      shadow = ScAddRefNotNull(new SpObjectShadow);
      shadow->position = position;
      shadow->mode = SpEffects::performance->processor_power >= 5;
    }
    frame_interval =
        u32(SpEffects::random.Range(kSpTornadoDebrisMinimumFrameInterval, kSpTornadoDebrisMaximumFrameInterval));
    SetSpanRects();
    AddToAnimationSystem();
  }
  return 1;
}

// Derive outward linear velocity from the current orbit and copy the current position through the original outputs.
void SpTornadoDebris::GetLinearVectorAndVelocity(ScVec3F *out_velocity, ScVec3F *out_position) {
  f64 magnitude = f64(angular_speed) * radius * 1000.0;
  f32 speed = f32(magnitude);
  if (magnitude < 0.0)
    speed = -speed;
  f64 heading = f64(angle) + f64(3.1415927f);
  out_velocity->x = f32(std::cos(heading) * speed);
  out_velocity->z = f32(std::sin(heading) * speed);
  out_velocity->y = f32(f64(vertical_speed) * 1100.0);
  out_position->x = position.x;
  out_position->y = position.y;
  out_position->z = position.z;
}

// Advance the orbit, animation frame and retained shadow using the real animation manager and terrain output.
// Once the radius exceeds its threshold, convert to ordinary linear debris; leaving world bounds removes occupancy.
i32 SpTornadoDebris::Tick(u32 delta) {
  if (!active) {
    SpDebris::Tick(delta);
    return 0;
  }
  elapsed += delta;
  f32 step = f32(delta);
  if (!settled_at && elapsed > frame_elapsed + frame_interval) {
    frame_elapsed = elapsed;
    ++frame;
    if (frame >= frame_count)
      frame = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      i32 offset = i32(u32(frame) * u32(width));
      origins[zoom].x = offset;
      if (shadow)
        shadow->origins[zoom].x = offset;
    }
  }
  f64 old_radius = radius;
  f64 new_radius = f64(step) * radial_speed + old_radius;
  radius = f32(new_radius);
  f64 new_angular_speed = old_radius / new_radius * angular_speed;
  angular_speed = f32(new_angular_speed);
  angle = f32(new_angular_speed * step + angle);
  center.x = f32(f64(step) * velocity.x * 0.001 + center.x);
  center.z = f32(f64(step) * velocity.z * 0.001 + center.z);
  i32 old_x = SpDebrisCoordinate(position.x), old_z = SpDebrisCoordinate(position.z);
  i32 old_y = SpDebrisCoordinate(position.y);
  i32 height_delta = SpDebrisCoordinate(f64(8.94f) * vertical_speed * step);
  position.y = f32(f64(height_delta) + position.y);
  position.x = f32(std::cos(f64(angle)) * radius + center.x);
  position.z = f32(std::sin(f64(angle)) * radius + center.z);
  if (radius > target_height) {
    GetLinearVectorAndVelocity(&velocity, &position);
    active = 0;
  }
  i32 x = SpDebrisCoordinate(position.x), z = SpDebrisCoordinate(position.z);
  if (u32(x) < u32(SpEffects::micro_width) && u32(z) < u32(SpEffects::micro_height)) {
    i32 y = SpDebrisCoordinate(position.y);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), old_x, old_z, old_y, x, z, y);
    if (shadow) {
      // The installed terrain output aliases the high byte of the initialized tick argument.
      u8 altitude = u8(delta >> 24);
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
      i32 old_shadow_y = SpDebrisCoordinate(shadow->position.y);
      shadow->position.x = f32(x);
      shadow->position.z = f32(z);
      shadow->position.y = f32(u32(altitude) << kCtMicroCoordinateShift);
      i32 new_shadow_y = SpDebrisCoordinate(shadow->position.y);
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(shadow), old_x, old_z, old_shadow_y,
          x, z, new_shadow_y);
    }
  } else {
    position.x = f32(old_x);
    position.z = f32(old_z);
    SpDebris::RemoveFromAnimationSystem();
  }
  return 0;
}

// Initialize the tornado's fade, timing, world and auxiliary-sprite state after the shared sprite construction.
// Its velocity is left for the original initialization path, which writes all three components before use.
SpTornado::SpTornado()
    : registered(0), fade_frame(0), fade_frames(8), fade_step(1), frame(0), frame_count(11), variant(0), state(1),
      next_state(3), graphic(0), notify(0), speed(0.02f), elapsed(0), last_frame(0), ticks(0),
      interval(kSpTornadoFrameInterval), surface_type(0), last_occupant(nullptr), cell_x(-1), cell_z(-1),
      vector_276{0.0f, 0.0f, 0.0f}, height(100000.0f), base_sprite(nullptr), overlay_sprite(nullptr) {
  geom_flags |= kSpEffectBaseGeometry;
  for (u32 index = 0; index < 3; ++index)
    other_sprites[index] = nullptr;
}

SpTornado::~SpTornado() {
  RemoveFromAnimationSystem();
  for (u32 index = 0; index < 3; ++index)
    if (other_sprites[index]) {
      other_sprites[index]->Release();
      other_sprites[index] = nullptr;
    }
  ScRelease(&base_sprite);
  ScRelease(&overlay_sprite);
}

bool SpTornado::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpTornadoSimulator, SpTornadoSimulator);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpTornado::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpTornado::Release() {
  return SpEffectOccupant::Release();
}

i32 SpTornado::GetSurfaceTypeForCell(i32 x, i32 z) {
  ScOccupant *found = nullptr;
  SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &found);
  if (!found)
    return SpEffects::dirt->IsWater(u32(x), u32(z)) ? 2 : 1;
  u8 type = found->MajorType();
  found->Release();
  return type == 2 ? 3 : 4;
}

void SpTornado::SendNotification(const ScMessage &message) {
  if (SpEffects::messages)
    SpEffects::messages->MessagePost(message, false);
}

// Populate tornado registration rectangles from the original zoom-indexed image metadata.
void SpTornado::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    u32 key = SpTornadoImageKey(zoom, variant, kSpTornadoFrameImageBase);
    SpImage *image = SpEffects::images->GetImageInfo(key, 0);
    if (image) {
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
      if (zoom >= 4) {
        registration[zoom].left = i32(u32(registration[zoom].left) * 2);
        registration[zoom].top = i32(u32(registration[zoom].top) * 2);
        registration[zoom].right = i32(u32(registration[zoom].right) * 2);
        registration[zoom].bottom = i32(u32(registration[zoom].bottom) * 2);
        shifts[zoom] = 1;
      }
    }
  }
}

// Prepare the three tornado animation pairs and any live base graphics in original cache-call order.
// Select the current animation's borrowed buffers before updating frame offsets and optional ground artwork.
u8 SpTornado::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  for (u32 index = 0; index < 3; ++index) {
    u32 base = kSpTornadoFrameImageBase + index * 0x200u;
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, base), 0, &image);
    other_sprites[index]->color[zoom] = image->buffer;
    other_sprites[index]->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (zoom >= 4) {
      ScRect *rect = &other_sprites[index]->registration[zoom];
      rect->left = i32(u32(rect->left) * 2);
      rect->top = i32(u32(rect->top) * 2);
      rect->right = i32(u32(rect->right) * 2);
      rect->bottom = i32(u32(rect->bottom) * 2);
      other_sprites[index]->shifts[zoom] = 1;
    }
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, base + 0x100), 0, &image);
    other_sprites[index]->mask[zoom] = image->buffer;
  }
  SpBasicSprite *selected = state == 1 || state == 2 ? other_sprites[0] : state == 3 ? other_sprites[1] :
      state == 4 ? other_sprites[2] : nullptr;
  if (selected) {
    color[zoom] = selected->color[zoom];
    mask[zoom] = selected->mask[zoom];
    registration[zoom] = selected->registration[zoom];
  }
  i32 width = GetW(zoom, 0);
  origins[zoom].x = i32(u32(i32(fade_frame)) * u32(width));
  if (base_sprite) {
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoBaseColorImageBase), 0, &image);
    base_sprite->color[zoom] = image->buffer;
    base_sprite->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoBaseMaskImageBase), 0, &image);
    base_sprite->mask[zoom] = image->buffer;
    width = base_sprite->GetW(zoom, 0);
    base_sprite->origins[zoom].x = i32(u32(i32(frame)) * u32(width));
  }
  if (overlay_sprite) {
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoOverlayColorImageBase), 0, &image);
    overlay_sprite->color[zoom] = image->buffer;
    overlay_sprite->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    SpEffects::images->PrepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoOverlayMaskImageBase), 0, &image);
    overlay_sprite->mask[zoom] = image->buffer;
    width = overlay_sprite->GetW(zoom, 0);
    overlay_sprite->origins[zoom].x = i32(u32(i32(frame)) * u32(width));
  }
  return 1;
}

// Release each retained animation and ground-image demand in original order, clearing buffers after their callbacks.
// The tornado's selected buffers are borrowed from the three animation sprites and are cleared without another release.
i32 SpTornado::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  for (u32 index = 0; index < 3; ++index) {
    u32 base = kSpTornadoFrameImageBase + index * 0x200u;
    if (other_sprites[index]->color[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, base), 0);
      other_sprites[index]->color[zoom] = nullptr;
    }
    if (other_sprites[index]->mask[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, base + 0x100), 0);
      other_sprites[index]->mask[zoom] = nullptr;
    }
  }
  color[zoom] = nullptr;
  mask[zoom] = nullptr;
  if (base_sprite) {
    if (base_sprite->color[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoBaseColorImageBase), 0);
      base_sprite->color[zoom] = nullptr;
    }
    if (base_sprite->mask[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoBaseMaskImageBase), 0);
      base_sprite->mask[zoom] = nullptr;
    }
  }
  if (overlay_sprite) {
    if (overlay_sprite->color[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoOverlayColorImageBase), 0);
      overlay_sprite->color[zoom] = nullptr;
    }
    if (overlay_sprite->mask[zoom]) {
      SpEffects::images->UnprepareImage(SpTornadoImageKey(zoom, variant, kSpTornadoOverlayMaskImageBase), 0);
      overlay_sprite->mask[zoom] = nullptr;
    }
  }
  return 0;
}

// Add the selected land or water base graphic at the tornado position, or remove the previously selected graphic.
void SpTornado::AddRemoveBaseGraphic(u8 enabled) {
  if (!enabled) {
    SpBasicSprite *sprite = graphic == 5 ? base_sprite : graphic == 6 ? overlay_sprite : nullptr;
    if (sprite && SpEffects::animated) {
      i32 y = SpDebrisCoordinate(sprite->position.y), z = SpDebrisCoordinate(sprite->position.z);
      i32 x = SpDebrisCoordinate(sprite->position.x);
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite), x, z, y);
    }
    graphic = 0;
    return;
  }
  bool water = surface_type == 2;
  SpBasicSprite *sprite = water ? overlay_sprite : base_sprite;
  if (sprite) {
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      sprite = water ? overlay_sprite : base_sprite;
      i32 width = sprite->GetW(zoom, 0);
      i8 selected = frame;
      sprite = water ? overlay_sprite : base_sprite;
      sprite->origins[zoom].x = i32(u32(i32(selected)) * u32(width));
    }
    sprite = water ? overlay_sprite : base_sprite;
    sprite->position.x = f32(SpDebrisCoordinate(position.x));
    sprite = water ? overlay_sprite : base_sprite;
    sprite->position.z = f32(SpDebrisCoordinate(position.z));
    sprite = water ? overlay_sprite : base_sprite;
    sprite->position.y = f32(i32(u32(SpDebrisCoordinate(position.y)) - 1));
    sprite = water ? overlay_sprite : base_sprite;
    i32 y = SpDebrisCoordinate(sprite->position.y), z = SpDebrisCoordinate(sprite->position.z);
    i32 x = SpDebrisCoordinate(sprite->position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite), x, z, y, 1, 0, true);
  }
  graphic = water ? 6 : 5;
}

// Insert the actual occupant once, then update an already selected auxiliary ground graphic if present.
void SpTornado::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 1, 0, true);
    if (graphic)
      AddRemoveBaseGraphic(1);
  }
}

// Clear occupancy first, retire the selected ground graphic, then remove the tornado through the live animation
// manager.
void SpTornado::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (graphic)
      AddRemoveBaseGraphic(0);
    if (SpEffects::animated) {
      i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y);
    }
  }
}

// Bind a tornado to a valid animation position and create its five retained basic or shadow sprites in original order.
// Sample the terrain or occupant surface, apply the virtual direction, load spans and then publish the tornado.
u8 SpTornado::Init(i32 x, i32 z, i32 y, const ScVec3F &direction, i8 severity) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  if (y < 4)
    y = 4;
  position.x = f32(x);
  mode = 2;
  variant = severity;
  position.y = f32(y);
  position.z = f32(z);
  velocity = {0.0f, 0.0f, 0.0f};
  if (severity)
    variant = 0;
  for (u32 index = 0; index < 3; ++index) {
    other_sprites[index] = new SpBasicSprite;
    other_sprites[index]->AddRef();
  }
  base_sprite = ScAddRefNotNull(new SpObjectShadow);
  base_sprite->position = {f32(x), f32(y - 1), f32(z)};
  base_sprite->mode = 2;
  overlay_sprite = ScAddRefNotNull(new SpObjectShadow);
  overlay_sprite->position = {f32(x), f32(y - 1), f32(z)};
  overlay_sprite->mode = 2;
  surface_type = GetSurfaceTypeForCell(x / 256, z / 256);
  SetDirection(direction);
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

u8 SpTornado::SetPosition(i32, i32, i32) {
  return 1;
}

// Write the live fixed X, Z and altitude coordinates in original order using the source's double-store conversion.
void SpTornado::GetPosition(i32 *x, i32 *z, i32 *y) {
  *x = SpDebrisCoordinate(position.x);
  *z = SpDebrisCoordinate(position.z);
  *y = SpDebrisCoordinate(position.y);
}

// Normalize the requested horizontal direction and scale it by the current speed, clearing vertical velocity.
// Capture both input components before the writes so passing the existing velocity remains well-defined.
u8 SpTornado::SetDirection(const ScVec3F &direction) {
  f32 x = direction.x, z = direction.z;
  f64 inverse = 1.0 / std::sqrt(f64(x) * x + f64(z) * z);
  f32 normalized_x = f32(f64(x) * inverse);
  velocity.x = f32(f64(normalized_x) * speed);
  velocity.z = f32(inverse * f64(z) * speed);
  velocity.y = 0.0f;
  return 1;
}

// Replace the speed and reapply direction through the actual virtual operation with the current velocity as input.
u8 SpTornado::SetSpeed(const f32 &value) {
  speed = value;
  SetDirection(velocity);
  return 1;
}

i32 SpTornado::GetCurrentSurfaceType() {
  return surface_type;
}

// Change the original fade state and recompute each zoom's horizontal frame offset in callback order.
void SpTornado::DoFade(u8 visible, u8 send_notice) {
  notify = send_notice;
  if (visible) {
    if (state == 1)
      return;
    if (state)
      next_state = 1;
    else {
      fade_frame = 0;
      state = 1;
      next_state = 3;
      fade_step = 1;
    }
  } else {
    if (state == 2)
      return;
    if (state)
      next_state = 2;
    else {
      next_state = 0;
      state = 2;
      fade_frame = i8(fade_frames - 1);
      fade_step = -1;
    }
  }
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    i32 width = GetW(zoom, 0);
    origins[zoom].x = i32(u32(i32(fade_frame)) * u32(width));
  }
}

// Create a tornado debris sprite only at the original visible zooms, drawing its variant before initialization.
void SpTornado::CreateTornadoDebris() {
  if (SpEffects::zoom >= 1) {
    SpTornadoDebris *debris = new SpTornadoDebris;
    debris->AddRef();
    i32 debris_variant = i32(SpEffects::random.Bounded(8));
    debris->Init(&position, &velocity, debris_variant, 0);
    debris->Release();
  }
}

// Advance the tornado's fade and artwork, then move it through the world while publishing its original notifications.
// Periodic surface and terrain sampling updates the auxiliary base graphic before both animation occupants are moved.
i32 SpTornado::Tick(u32 delta) {
  if (!state)
    return 0;
  ++ticks;
  elapsed += delta;
  if (elapsed > last_frame + interval) {
    last_frame = elapsed;
    if (graphic) {
      frame = i8(u8(frame) + 1);
      if (frame >= frame_count)
        frame = 0;
      bool land_graphic = graphic == 5;
      for (i32 zoom = 0; zoom < 5; ++zoom) {
        SpBasicSprite *sprite = land_graphic ? base_sprite : overlay_sprite;
        i32 width = sprite->GetW(zoom, 0);
        sprite = land_graphic ? base_sprite : overlay_sprite;
        sprite->origins[zoom].x = i32(u32(i32(frame)) * u32(width));
      }
    }
    fade_frame = i8(u8(fade_frame) + u8(fade_step));
    if (fade_frame >= i32(fade_frames) || fade_frame < 0) {
      fade_frame = 0;
      fade_step = 1;
      if (next_state != state) {
        if (state == 1 && next_state != 2 && !graphic)
          AddRemoveBaseGraphic(u8(state));
        if (next_state == 2) {
          fade_frame = i8(fade_frames - 1);
          fade_step = -1;
        } else if (!next_state) {
          RemoveFromAnimationSystem();
          return 0;
        }
        state = next_state;
        SpBasicSprite *selected = state == 1 || state == 2 ? other_sprites[0] :
            state == 3 ? other_sprites[1] : other_sprites[2];
        for (i32 zoom = 0; zoom < 5; ++zoom) {
          color[zoom] = selected->color[zoom];
          mask[zoom] = selected->mask[zoom];
          registration[zoom] = selected->registration[zoom];
        }
      }
      if (next_state == 1)
        next_state = 3;
      else if (next_state == 2)
        next_state = 0;
      else
        next_state = SpEffects::random.Bounded(10) > 6 ? 4 : 3;
    }
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(i32(fade_frame)) * u32(width));
    }
  }
  f64 step = f64(delta) * 0.001;
  ScVec3F previous = position;
  position.x = f32(step * velocity.x + position.x);
  f64 next_z = step * velocity.z + position.z;
  position.z = f32(next_z);
  i32 x = SpDebrisCoordinate(position.x), z = SpDebrisCoordinate(next_z), y = SpDebrisCoordinate(position.y);
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height)) {
    position = previous;
    SendNotification(
        ScMessage(kSpMsg_TornadoLeftMap, reinterpret_cast<uptr>(static_cast<SpTornadoSimulator *>(this)), 0, 0));
    RemoveFromAnimationSystem();
    return 0;
  }
  if (!(ticks & 3)) {
    SendNotification(ScMessage(kSpMsg_TornadoPositionChanged,
        reinterpret_cast<uptr>(static_cast<SpTornadoSimulator *>(this)), u32(x), u32(z)));
    i32 sample_x = x / 256, sample_z = z / 256;
    ScOccupant *found = nullptr;
    SpEffects::surface->GetOccupantAtCell(u32(sample_x), u32(sample_z), &found);
    bool changed = false;
    if (found) {
      surface_type = found->MajorType() == 3 ? 3 : 4;
      if (last_occupant != found) {
        changed = true;
        last_occupant = found;
      }
      found->Release();
    } else if (SpEffects::dirt->IsWater(u32(sample_x), u32(sample_z))) {
      if (surface_type != 2) {
        surface_type = 2;
        changed = true;
      }
    } else if (surface_type != 1) {
      surface_type = 1;
      changed = true;
    }
    if (changed) {
      if (surface_type == 2) {
        if (graphic == 5)
          AddRemoveBaseGraphic(0);
        if (graphic != 6) {
          graphic = 6;
          AddRemoveBaseGraphic(1);
        }
      } else {
        if (graphic == 6)
          AddRemoveBaseGraphic(0);
        if (graphic != 5) {
          graphic = 5;
          AddRemoveBaseGraphic(1);
        }
      }
    }
    if (!(ticks & 3)) {
      // A valid terrain query supplies this byte; a non-writing callback is an excluded original uninitialized read.
      u8 altitude = 0;
      SpEffects::dirt->GetVertexAltitude(u32(sample_x), u32(sample_z), &altitude);
      i32 ground_y = i32(u32(altitude) << kCtMicroCoordinateShift);
      if (y != ground_y) {
        y = i32(u32(y) + u32(i32(u32(ground_y) - u32(y)) / 8));
        if (y < 4)
          y = 4;
        position.y = f32(y);
        y = SpDebrisCoordinate(position.y);
      }
    }
  }
  i32 old_y = SpDebrisCoordinate(previous.y), old_z = SpDebrisCoordinate(previous.z);
  i32 old_x = SpDebrisCoordinate(previous.x);
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), old_x, old_z, old_y, x, z, y);
  SpBasicSprite *sprite = graphic == 5 ? base_sprite : graphic == 6 ? overlay_sprite : nullptr;
  if (sprite) {
    sprite->position.x = f32(x);
    sprite->position.z = f32(z);
    sprite->position.y = f32(i32(u32(y) - 1));
    i32 sprite_y = SpDebrisCoordinate(sprite->position.y), sprite_z = SpDebrisCoordinate(sprite->position.z);
    i32 sprite_x = SpDebrisCoordinate(sprite->position.x);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(sprite), old_x, old_z, old_y,
        sprite_x, sprite_z, sprite_y);
  }
  return 0;
}

// Construct the registered space-debris controller at its original zero reference count.
ScUnknown *SpCreateSpaceDebrisDisplay() {
  return static_cast<SpDebrisDisplay *>(new SpSpaceDebrisDisplay);
}

// Construct the registered impact controller with its original controller, sprite and occupant identities.
ScUnknown *SpCreateSpaceDebrisImpactDisplay() {
  return static_cast<SpImpactDisplay *>(new SpSpaceDebrisImpactDisplay);
}

// Construct the registered fire controller with its original zero-reference controller and tick identities.
ScUnknown *SpCreateFireSimulator() {
  return static_cast<SpFireSimulator *>(new SpFire);
}

// Construct the registered tornado controller with its original controller, sprite and occupant identities.
ScUnknown *SpCreateTornadoSimulator() {
  return static_cast<SpTornadoSimulator *>(new SpTornado);
}

// The original shared locust vector retains borrowed interface pointers; each locust registers its own lifetime.
static std::vector<SpLocustSimulator *> SpAllLocusts;
static SpLocust *SpBigLocustsBeingShown;

// Construct the original locust and return its primary simulator identity to the class factory.
ScUnknown *SpCreateLocustSimulator() {
  return static_cast<SpLocustSimulator *>(new SpLocust);
}

// Construct the original locust group and return its primary group identity to the class factory.
ScUnknown *SpCreateLocustGroupSimulator() {
  return static_cast<SpLocustGroupSimulator *>(new SpLocustGroup);
}

static void SpLockEffects() {
  while (SpEffects::thread_safe_flag.fetch_or(1, std::memory_order_acquire) & 1)
    ScYieldThread();
}

static void SpUnlockEffects() {
  SpEffects::thread_safe_flag.store(0, std::memory_order_release);
}

// Preserve the original biased double-store conversion used by locust position outputs.
static u32 SpLocustCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Initialize the death spray's original frame state while leaving its image variant until Init.
SpDeathSpray::SpDeathSpray()
    : elapsed(0), frame_elapsed(0), word216(0), registered(0), frame(0), frame_count(8),
      frame_interval(kSpDeathSprayFrameInterval) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpDeathSpray::~SpDeathSpray() {
  RemoveFromAnimationSystem();
}

bool SpDeathSpray::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpDeathSprayView, SpDeathSprayView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpDeathSpray::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpDeathSpray::Release() {
  return SpEffectOccupant::Release();
}

u32 SpDeathSpray::SetTimeBetweenFrames(u32 duration) {
  frame_interval = duration;
  return duration;
}

u32 SpDeathSpray::GetSpriteKey(i32 zoom, i32 value, u32 base) {
  return base + u32(zoom) + 5u * u32(value);
}

void SpDeathSpray::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, variant, kSpDeathSprayColorImageBase), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Insert this spray through the same original animation-registration body as smoke.
void SpDeathSpray::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(position.x)),
        i32(SpLocustCoordinate(position.z)), i32(SpLocustCoordinate(position.y)), 1, 0, true);
    frame_elapsed = 0;
  }
}

// Remove the registered spray through the original shared smoke-removal body.
void SpDeathSpray::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(position.x)),
          i32(SpLocustCoordinate(position.z)), i32(SpLocustCoordinate(position.y)));
  }
}

// Place an in-bounds spray, select its image variant and register its animation occupant.
u8 SpDeathSpray::Init(i32 x, i32 z, i32 altitude, i32 value) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  mode = 2;
  variant = value;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Demand the death-spray color and mask images and copy registration from the color image.
// Each returned image is consumed directly by the original; the output must be nonnull.
u8 SpDeathSpray::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, kSpDeathSprayColorImageBase), 0, &image);
  assert(image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, kSpDeathSprayMaskImageBase), 0, &image);
  assert(image);
  mask[zoom] = image->buffer;
  return 1;
}

// Release demanded spray images through the original cache, clearing each borrowed buffer afterward.
i32 SpDeathSpray::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, kSpDeathSprayColorImageBase), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, kSpDeathSprayMaskImageBase), 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Advance the spray through the original tick body shared with smoke, retiring it after its last frame.
i32 SpDeathSpray::Tick(u32 delta) {
  elapsed += delta;
  if (elapsed > frame_elapsed + kSpDeathSprayFrameInterval) {
    ++frame;
    frame_elapsed = elapsed;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(frame) * u32(width));
    }
    if (frame < frame_count) {
      if (SpEffects::zoom >= 2)
        SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this),
            i32(SpLocustCoordinate(position.x)), i32(SpLocustCoordinate(position.z)),
            i32(SpLocustCoordinate(position.y)), 2, nullptr);
    } else {
      RemoveFromAnimationSystem();
    }
  }
  return 0;
}

// Read a float from an instruction payload word without aliasing the word's storage.
static f32 SpLocustInstructionFloat(const SpLocustInstruction &instruction, u32 index) {
  f32 value;
  memcpy(&value, &instruction.payload[index], sizeof(value));
  return value;
}

static void SpLocustSetInstructionFloat(SpLocustInstruction *instruction, u32 index, f32 value) {
  memcpy(&instruction->payload[index], &value, sizeof(value));
}

// Match the original three-axis movement crossing checks, including their stored float intermediates.
static bool SpLocustReached(const ScVec3F &start, const ScVec3F &current, const ScVec3F &target) {
  f32 moved_x = f32(f64(current.x) - start.x), target_x = f32(f64(target.x) - start.x);
  if (std::fabs(f64(moved_x)) >= std::fabs(f64(target.x) - start.x) &&
      std::fabs(f64(current.z) - start.z) >= std::fabs(f64(target.z) - start.z) &&
      std::fabs(f64(current.y) - start.y) >= std::fabs(f64(target.y) - start.y))
    return true;
  if (!(std::fabs(f64(current.x) - target.x) >= std::fabs(f64(target_x))) ||
      !(std::fabs(f64(current.z) - target.z) >= std::fabs(f64(target.z) - start.z)))
    return false;
  f32 remaining_y = f32(f64(current.y) - target.y), target_y = f32(f64(target.y) - start.y);
  return std::fabs(f64(remaining_y)) >= std::fabs(f64(target_y));
}

// Instruction selectors occupy the original DWORD; group messages use a separate identity domain.
enum SpLocustOp : u32 {
  kSpLocustNoOp = 0,
  kSpLocustWait = 1,
  kSpLocustMigrate = 2,
  kSpLocustAttackCrops = 3,
  kSpLocustDisappear = 4,
  kSpLocustPoison = 5,
  kSpLocustSetSpeed = 6,
};

enum SpLocustMsg : u32 {
  kSpMsg_LocustInstructions = 0xe5351280,
  kSpMsg_LocustDestroyGroup = 0x85358975,
  kSpMsg_LocustDestroyAll = 0xc535897d,
};

SpLocust::SpLocust()
    : registered(0), instructions_changed(0), byte213(0), limit(5), state(0), word216(0), word220(1),
      vector224{0.0f, 0.0f, 0.0f}, vector236{0.0f, 1.0f, 0.0f}, speed(0.4f), word252(0), word256(0),
      word260(kSpLocustFrameInterval), shadow(nullptr), dependency(nullptr), group(nullptr), byte288(0), byte304(0) {
  SpLockEffects();
  SpAllLocusts.push_back(static_cast<SpLocustSimulator *>(this));
  SpUnlockEffects();
  geom_flags |= kSpEffectBaseGeometry;
  mode = 2;
}

SpLocust::~SpLocust() {
  Shutdown();
}

bool SpLocust::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpLocustSimulator, SpLocustSimulator);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpLocust::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpLocust::Release() {
  return SpEffectOccupant::Release();
}

// Initialize the locust's position, disaster dependency, random destination, optional shadow and animation membership.
u8 SpLocust::Init(const ScVec3F &input, i32 level, i8 initial_state) {
  position = input;
  i32 initial_z = i32(i64(f64(position.z) * 0.00390625));
  cell_x = i32(i64(f64(position.x) * 0.00390625));
  cell_y = 0;
  state = i8(level);
  cell_z = initial_z;
  word216 = u32(i32(initial_state));
  if (!SpEffects::animated)
    return 0;
  DsManager *manager = SpEffects::city->DisasterLayer()->GetDisasterManager(kDsLocustTypeId);
  if (!manager)
    return 0;
  void *object = dependency;
  bool found = manager->QueryInterface(IID_DsLocustControl, &object);
  dependency = static_cast<DsLocustControl *>(object);
  if (!found || !dependency)
    return 0;
  i32 ground = SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z);
  if (ground > i32(SpLocustCoordinate(position.y)))
    position.y = f32(SpEffects::random.RandomDoubleRangeUniform(ground + 5120, ground + 10240));
  word220 = SpEffects::random.Bounded(2) ? 1u : UINT32_MAX;
  GenerateNewRandomDestinationPosition();
  if (SpLocustCoordinate(position.x) >= u32(SpEffects::micro_width) ||
      SpLocustCoordinate(position.z) >= u32(SpEffects::micro_height)) {
    position.z = 0.0f;
    position.x = 0.0f;
    vector224.z = 256.0f;
    vector224.x = 256.0f;
  }
  if (SpEffects::performance->processor_power >= 4) {
    shadow = ScAddRefNotNull(new SpObjectShadow);
    shadow->position.x = f32(i32(SpLocustCoordinate(position.x)));
    shadow->position.z = f32(i32(SpLocustCoordinate(position.z)));
    shadow->position.y = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z));
    shadow->mode = 4;
  }
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Release the dependent service and group, unregister from the pool, then retire animation membership.
void SpLocust::Shutdown() {
  ScRelease(&dependency);
  if (group) {
    group->AddLocustsToGroup(static_cast<SpLocustSimulator *>(this), 0, 0);
    group->Release();
    group = nullptr;
  }
  SpLockEffects();
  for (usize index = 0; index < SpAllLocusts.size(); ++index) {
    if (SpAllLocusts[index] == static_cast<SpLocustSimulator *>(this)) {
      SpAllLocusts.erase(SpAllLocusts.begin() + isize(index));
      if (SpAllLocusts.empty() && SpEffects::messages) {
        ScMessage message(kSpMsg_AllLocustsRemoved);
        SpEffects::messages->MessagePost(message, false);
      }
      break;
    }
  }
  SpUnlockEffects();
  RemoveFromAnimationSystem();
}

// Retire large-locust overlays and image demands, then remove the locust and optional shadow from animation.
void SpLocust::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  SpEffects::cell_map->IncrementAlphaBlendCount(-1);
  for (usize index = 0; index < big_locusts.size(); ++index) {
    SpBigLocust *item = &big_locusts[index];
    if (item->color) {
      if (!byte288 && SpEffects::cell_map && item->overlay_active) {
        SpEffects::cell_map->RemoveOverlayBuffer(item->overlay);
        item->overlay_active = 0;
      }
      item->color->Release();
      item->color = nullptr;
      item->mask->Release();
      item->mask = nullptr;
      if (SpEffects::images) {
        SpEffects::images->UnprepareImage(item->variant + kSpLocustOverlayColorBase, 0);
        SpEffects::images->UnprepareImage(item->variant + kSpLocustOverlayMaskBase, 0);
      }
    }
  }
  u8 old_references = references;
  SpBasicSprite *old_shadow = shadow;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(position.x)),
        i32(SpLocustCoordinate(position.z)), i32(SpLocustCoordinate(position.y)));
  if (old_shadow) {
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(old_shadow),
          i32(SpLocustCoordinate(old_shadow->position.x)), i32(SpLocustCoordinate(old_shadow->position.z)),
          i32(SpLocustCoordinate(old_shadow->position.y)));
    old_shadow->Release();
    if (old_references > 1)
      shadow = nullptr;
  }
}

void SpLocust::ClearAllInstructions() {
  instructions.clear();
  instructions_changed = 1;
}

// Write the three position components in original X, altitude, Z order using the installed coordinate conversion.
void SpLocust::GetPosition(u32 *x, u32 *altitude, u32 *z) {
  *x = SpLocustCoordinate(position.x);
  *altitude = SpLocustCoordinate(position.y);
  *z = SpLocustCoordinate(position.z);
}

// Retain at most the active non-movement instruction, then append the original disappearance instruction.
void SpLocust::DestroySelfAsSoonAsPossible() {
  SpLocustInstruction instruction{kSpLocustDisappear, {}};
  if (instructions_changed && !instructions.empty()) {
    if (instructions.front().type == kSpLocustMigrate)
      instructions.clear();
    else
      while (instructions.size() > 1)
        instructions.pop_back();
  }
  AddInstruction(instruction);
}

// Store the new group and retain it if present; the original does not release the previous pointer here.
void SpLocust::SetGroup(SpLocustGroupSimulator *value) {
  group = value;
  if (value)
    value->AddRef();
}

// Change locust state, reloading already-demanded artwork and refreshing a registered animation occupant.
u8 SpLocust::SetState(SpLocustState value) {
  i32 requested = static_cast<i32>(value), previous = state;
  if (previous == requested)
    return 1;
  if (previous >= 0) {
    byte213 = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      if (color[zoom]) {
        state = i8(previous);
        ReleaseArt(zoom, 0);
        state = i8(requested);
        DemandArt(zoom, 0, true);
        i32 width = GetW(zoom, 0);
        origins[zoom].x = i32(u32(i32(i8(byte213))) * u32(width));
      }
    }
    if (registered)
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(position.x)),
          i32(SpLocustCoordinate(position.z)), i32(SpLocustCoordinate(position.y)), 2, nullptr);
  }
  state = i8(requested);
  return 1;
}

i32 SpLocust::GetState() const {
  return state;
}

u32 SpLocust::GetSpriteKey(i32 zoom, i32 value, u32 base) {
  return base + u32(zoom) + (u32(value) << 16);
}

// Acquire state-specific locust artwork and, when present, the shadow mask and its registration rectangle.
// The original assumes each demanded image exists before accessing its fields.
u8 SpLocust::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, state, kSpLocustColorImageBase), 0, &image);
  assert(image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  mode = 2;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, state, kSpLocustMaskImageBase), 0, &image);
  assert(image);
  mask[zoom] = image->buffer;
  if (shadow) {
    SpEffects::images->PrepareImage(u32(zoom) + kSpLocustShadowMaskImageBase, 0, &image);
    assert(image);
    shadow->mask[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  return 1;
}

// Release acquired state-specific locust artwork and the optional shadow mask through the shared image cache.
i32 SpLocust::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, state, kSpLocustColorImageBase), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, state, kSpLocustMaskImageBase), 0);
    mask[zoom] = nullptr;
  }
  if (shadow && shadow->mask[zoom]) {
    SpEffects::images->UnprepareImage(u32(zoom) + kSpLocustShadowMaskImageBase, 0);
    shadow->mask[zoom] = nullptr;
  }
  return 0;
}

// Run the selected large-locust overlay or advance instructions and move or refresh the locust and its shadow.
i32 SpLocust::Tick(u32 elapsed) {
  ScVec3F old = position;
  bool changed = false, moved = false;
  word252 += elapsed;
  if (!byte288) {
    if (SpEffects::view_id == 0) {
      if (!SpBigLocustsBeingShown)
        SpBigLocustsBeingShown = this;
      if (SpBigLocustsBeingShown == this) {
        DoBigLocustsSimulation(f32(f64(f32(elapsed)) * f64(0.001f)));
        return 0;
      }
    }
    byte288 = 1;
  }
  if (word252 > word256 + word260) {
    word256 = word252;
    byte213 = u8(byte213 + word220);
    if (i8(byte213) >= i32(limit))
      byte213 = 0;
    else if (i8(byte213) < 0)
      byte213 = u8(limit - 1);
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(i32(i8(byte213))) * u32(width));
    }
    changed = true;
  }
  if (instructions_changed) {
    if (!DoInstructionState(&moved, &changed, f32(f64(elapsed) * f64(0.001f))))
      return 0;
  } else {
    group->AddLocustsToGroup(this, 0, 0);
    DestroySelfAsSoonAsPossible();
    moved = true;
  }
  if (moved) {
    i32 x = i32(SpLocustCoordinate(position.x)), z = i32(SpLocustCoordinate(position.z));
    i32 y = i32(SpLocustCoordinate(position.y));
    if (x < 0) {
      x = 0;
      position.x = 0;
    }
    if (x >= SpEffects::micro_width) {
      x = i32(u32(SpEffects::micro_width) - 1u);
      position.x = f32(x);
    }
    if (z < 0) {
      z = 0;
      position.z = 0;
    }
    if (z >= SpEffects::micro_height) {
      z = i32(u32(SpEffects::micro_height) - 1u);
      position.z = f32(z);
    }
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(old.x)),
        i32(SpLocustCoordinate(old.z)), i32(SpLocustCoordinate(old.y)), x, z, y);
    if (shadow) {
      u8 altitude;
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
      shadow->position.y = f32(u32(altitude) << kCtMicroCoordinateShift);
      i32 shadow_y = i32(SpLocustCoordinate(shadow->position.y));
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(shadow),
          i32(SpLocustCoordinate(shadow->position.x)), i32(SpLocustCoordinate(shadow->position.z)), shadow_y,
          x, z, shadow_y);
      shadow->position.x = f32(x);
      shadow->position.z = f32(z);
    }
    CheckForDestroyedTrees();
  } else if (changed) {
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(old.x)),
        i32(SpLocustCoordinate(old.z)), i32(SpLocustCoordinate(old.y)), 2, nullptr);
  }
  return 0;
}

// Create the large-locust overlays on the first update, then move them until all leave the city buffer.
void SpLocust::DoBigLocustsSimulation(f32 elapsed) {
  const ScRect *screen = SpEffects::cell_map->GetCityBufferPtr()->GetBufferArea();
  if (!byte304) {
    u32 power = u32(i32(SpEffects::performance->processor_power));
    u32 count = power <= 3 ? 3 : power == 4 ? 8 : SpEffects::random.Bounded(10) + 10;
    if (SpEffects::box)
      SpEffects::box->Event(kAuEventPlayEffect, kAuEffectLocustFlyBy, 0, 0, 0);
    GxBuffer *overlay_color = nullptr, *overlay_mask = nullptr;
    for (u32 index = 0; index < count; ++index) {
      SpBigLocust locust{};
      locust.variant = index ? SpEffects::random.Bounded(3) + 1 : 0;
      SpImage *image = nullptr;
      if (SpEffects::images->PrepareImage(locust.variant + kSpLocustOverlayColorBase, 0, &image))
        overlay_color = ScAddRefNotNull(image->buffer);
      if (SpEffects::images->PrepareImage(locust.variant + kSpLocustOverlayMaskBase, 0, &image))
        overlay_mask = ScAddRefNotNull(image->buffer);
      assert(overlay_color && overlay_mask && "The original overlay buffers must be established before use");
      locust.color = overlay_color;
      locust.mask = overlay_mask;
      i32 minimum_x = i32(u32(screen->left) - u32(locust.color->GetBufferArea()->right));
      f32 overlay_speed = f32(SpEffects::random.RandomDoubleRangeUniform(f64(0.2f), f64(0.4f)));
      locust.position.x = SpEffects::random.Range(minimum_x, screen->right);
      locust.position.y = screen->bottom;
      static const f32 angles[5] = {30.0f, 60.0f, 20.0f, -20.0f, -60.0f};
      f64 angle = f64(angles[locust.variant]) * f64(0.0027777778f) * f64(6.2831855f);
      u32 vertical_distance = u32(locust.position.y) - u32(screen->top);
      i32 end_x = i32(i64(std::tan(angle) * f64(vertical_distance) + locust.position.x));
      i32 dx = i32(u32(end_x) - u32(locust.position.x));
      i32 dy = i32(u32(screen->top) - u32(locust.position.y));
      f32 x = f32(dx), y = f32(dy);
      f64 scale = f64(overlay_speed) / std::sqrt(f64(dy) * dy + f64(dx) * dx);
      locust.velocity.x = f32(f64(x) * scale);
      locust.velocity.y = f32(f64(y) * scale);
      if (SpEffects::cell_map) {
        locust.overlay = SpEffects::cell_map->AddOverlayBuffer(locust.position, locust.color, locust.mask);
        locust.overlay_active = 1;
      }
      big_locusts.push_back(locust);
    }
    byte304 = 1;
    return;
  }
  bool visible = false;
  for (SpBigLocust &locust : big_locusts) {
    locust.position.x = i32(u32(locust.position.x) + SpLocustCoordinate(f64(elapsed) * locust.velocity.x));
    locust.position.y = i32(u32(locust.position.y) + SpLocustCoordinate(f64(elapsed) * locust.velocity.y));
    ScRect bounds{locust.position.x, locust.position.y,
        i32(u32(locust.position.x) + locust.color->Width()),
        i32(u32(locust.position.y) + locust.color->Height())};
    if (bounds.IntersectInclusive(&bounds, screen)) {
      if (SpEffects::cell_map)
        SpEffects::cell_map->MoveOverlayBuffer(&locust.position, locust.overlay);
      visible = true;
    } else if (SpEffects::cell_map && locust.overlay_active) {
      SpEffects::cell_map->RemoveOverlayBuffer(locust.overlay);
      locust.overlay_active = 0;
    }
  }
  if (!visible) {
    byte288 = 1;
    SpBigLocustsBeingShown = nullptr;
  }
}

// Read locust and optional shadow registration rectangles from state-specific image metadata at each zoom.
void SpLocust::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, state, kSpLocustColorImageBase), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (shadow) {
      image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, state, kSpLocustShadowMaskImageBase), 0);
      if (image)
        shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    }
  }
}

// Register the locust and optional shadow once, after incrementing the cell map's alpha-blend count.
void SpLocust::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::cell_map->IncrementAlphaBlendCount(1);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), i32(SpLocustCoordinate(position.x)),
        i32(SpLocustCoordinate(position.z)), i32(SpLocustCoordinate(position.y)), 2, 0, true);
    if (shadow)
      SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(shadow),
          i32(SpLocustCoordinate(shadow->position.x)), i32(SpLocustCoordinate(shadow->position.z)),
          i32(SpLocustCoordinate(shadow->position.y)), 2, 0, true);
  }
}

// Set a destination and a speed-scaled direction, using the original upward default for nearby points.
void SpLocust::GenerateNewSpecificDestinationPosition(const ScVec3F &from, const ScVec3F &target) {
  f64 dx = f64(target.x) - from.x;
  if (std::fabs(dx) > 1.0 || std::fabs(f64(target.z) - from.z) > 1.0 ||
      std::fabs(f64(target.y) - from.y) > 1.0) {
    vector236.x = f32(dx);
    vector236.z = f32(f64(target.z) - from.z);
    vector236.y = f32(f64(target.y) - from.y);
    SpEffects::Scale3DVector(&vector236, speed);
  } else {
    vector236.x = 0.0f;
    vector236.z = 0.0f;
    vector236.y = 0.4f;
  }
  vector224 = target;
}

// Choose a nearby in-bounds random destination, select terrain-relative altitude, and update the travel direction.
void SpLocust::GenerateNewRandomDestinationPosition() {
  ScRect neighborhood{i32(SpLocustCoordinate(f64(position.x) - 1280.0)),
      i32(SpLocustCoordinate(f64(position.z) - 1280.0)), i32(SpLocustCoordinate(f64(position.x) + 1280.0)),
      i32(SpLocustCoordinate(f64(position.z) + 1280.0))};
  ScRect bounds{256, 256, i32(u32(SpEffects::micro_width) - 256u), i32(u32(SpEffects::micro_height) - 256u)};
  bounds.IntersectInclusive(&bounds, &neighborhood);
  vector224.x = f32(SpEffects::random.RandomDoubleRangeUniform(bounds.left, bounds.right));
  f64 target_z = SpEffects::random.RandomDoubleRangeUniform(bounds.top, bounds.bottom);
  vector224.z = f32(target_z);
  i32 x = i32(SpLocustCoordinate(vector224.x)) / kCtMicroUnitsPerCell,
      z = i32(SpLocustCoordinate(target_z)) / kCtMicroUnitsPerCell;
  u8 ground;
  SpEffects::dirt->GetVertexAltitude(u32(x), u32(z), &ground);
  i32 base = i32(ground) << kCtMicroCoordinateShift;
  vector224.y = f32(SpEffects::random.RandomDoubleRangeUniform(base + 5120, base + 10240));
  GenerateNewSpecificDestinationPosition(position, vector224);
}

// Append an instruction, mark the queue changed and prepare it immediately when it is the first entry.
u8 SpLocust::AddInstruction(const SpLocustInstruction &instruction) {
  instructions_changed = 1;
  instructions.push_back(instruction);
  if (instructions.size() == 1)
    PrepNewInstruction();
  return 1;
}

// Prepare the front instruction's target and state using the original obstacle, occupant and terrain lookups.
u8 SpLocust::PrepNewInstruction() {
  SpLocustInstruction &instruction = instructions.front();
  if (instruction.type == kSpLocustWait || instruction.type == kSpLocustPoison) {
    instruction.payload[1] = 0;
  } else if (instruction.type == kSpLocustMigrate) {
    FixFrontMoveInstructionToAvoidObjects();
    ScVec3F target{SpLocustInstructionFloat(instruction, 0), SpLocustInstructionFloat(instruction, 1),
        SpLocustInstructionFloat(instruction, 2)};
    GenerateNewSpecificDestinationPosition(position, target);
  } else if (instruction.type == kSpLocustAttackCrops) {
    i32 x = i32(i64(f64(SpLocustInstructionFloat(instruction, 0)) * 0.00390625));
    i32 z = i32(i64(f64(SpLocustInstructionFloat(instruction, 2)) * 0.00390625));
    ScOccupant *occupant = nullptr;
    SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant);
    u8 ground;
    if (occupant) {
      u32 occupant_x, occupant_z;
      occupant->GetLocationInCellCoords(&occupant_x, &occupant_z);
      i32 half = i32(occupant->SizeInCellsX()) << 7;
      x = i32((occupant_x << kCtMicroCoordinateShift) + u32(half));
      z = i32((occupant_z << kCtMicroCoordinateShift) + u32(half));
      SpLocustSetInstructionFloat(&instruction, 0, f32(x));
      SpLocustSetInstructionFloat(&instruction, 2, f32(z));
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &ground);
      i32 altitude = i32((u32(ground) + 6u) << kCtMicroCoordinateShift) + i32(occupant->SizeInAnimCellsY());
      SpLocustSetInstructionFloat(&instruction, 1, f32(altitude));
      occupant->Release();
    } else {
      SpEffects::dirt->GetVertexAltitude(u32(x), u32(z), &ground);
      SpLocustSetInstructionFloat(&instruction, 1, f32((u32(ground) + 6u) << kCtMicroCoordinateShift));
    }
    ScVec3F target{SpLocustInstructionFloat(instruction, 0), SpLocustInstructionFloat(instruction, 1),
        SpLocustInstructionFloat(instruction, 2)};
    GenerateNewSpecificDestinationPosition(position, target);
    instruction.payload[3] &= kCtMicroCellMask;
    instruction.payload[4] = 0;
  } else if (instruction.type == kSpLocustDisappear) {
    instruction.payload[0] = 0;
  }
  instructions_changed = 1;
  return 1;
}

// Split a distant movement into a 1200-unit step, maintaining the original terrain-clearance altitude.
i32 SpLocust::FixFrontMoveInstructionToAvoidObjects() {
  SpLocustInstruction &instruction = instructions.front();
  ScVec3F start = position;
  ScVec3F target{SpLocustInstructionFloat(instruction, 0), SpLocustInstructionFloat(instruction, 1),
      SpLocustInstructionFloat(instruction, 2)};
  ScVec3F step{f32(f64(target.x) - start.x), 0.0f, f32(f64(target.z) - start.z)};
  if (std::fabs(f64(step.x)) >= f64(0.01f) || std::fabs(f64(target.z) - start.z) >= f64(0.01f)) {
    SpEffects::Scale2DVector(&step, 1200.0f);
    step.x = f32(f64(step.x) + start.x);
    step.z = f32(f64(step.z) + start.z);
  } else {
    step.x = start.x;
    step.z = start.z;
  }
  if (std::fabs(f64(step.x) - start.x) >= std::fabs(f64(target.x) - start.x) &&
      std::fabs(f64(step.z) - start.z) >= std::fabs(f64(target.z) - start.z)) {
    i32 clearance = i32(u32(SpEffects::GetAltitudeOfTerrainInAnimUnits(target.x, target.z)) + 7680u);
    if (i32(SpLocustCoordinate(target.y)) < clearance)
      SpLocustSetInstructionFloat(&instruction, 1, f32(clearance));
    return 0;
  }
  auto after_front = instructions.begin();
  ++after_front;
  instructions.insert(after_front, instruction);
  ScVec3F distance{f32(f64(target.x) - start.x), 0.0f, f32(f64(target.z) - start.z)};
  f32 length = f32(SpEffects::LengthOf2DVector(distance));
  f64 change_y = f64(target.y) - start.y;
  if (length > 0.1f)
    change_y *= 1200.0 / length;
  step.y = f32(change_y + start.y);
  i32 clearance = i32(u32(SpEffects::GetAltitudeOfTerrainInAnimUnits(step.x, step.z)) + 7680u);
  if (i32(SpLocustCoordinate(step.y)) < clearance)
    step.y = f32(clearance);
  SpLocustSetInstructionFloat(&instruction, 0, step.x);
  SpLocustSetInstructionFloat(&instruction, 2, step.z);
  SpLocustSetInstructionFloat(&instruction, 1, step.y);
  return 1;
}

// Advance the front instruction, publish movement or effects changes, and prepare the next queued instruction.
u8 SpLocust::DoInstructionState(bool *moved, bool *changed, f32 elapsed) {
  ScVec3F start = position;
  SpLocustInstruction &instruction = instructions.front();
  bool complete = false;
  switch (instruction.type) {
  case kSpLocustNoOp:
    complete = true;
    break;
  case kSpLocustWait:
    instruction.payload[1] += SpLocustCoordinate(elapsed);
    complete = instruction.payload[1] > instruction.payload[0];
    break;
  case kSpLocustMigrate:
  case kSpLocustAttackCrops:
    if (instruction.type == kSpLocustAttackCrops && (instruction.payload[3] & 255u)) {
      instruction.payload[4] += SpLocustCoordinate(elapsed);
      if (SpEffects::messages) {
        u32 x = u32(i64(position.x)), z = u32(i64(position.z));
        ScMessage message(
            kSpMsg_LocustOccupantContact, reinterpret_cast<uptr>(this), ((x & 0xFF00u) << 8) | ((z & 0xFFFFu) >> 8), 0);
        SpEffects::messages->MessagePost(message, false);
      }
      if (state == 1) {
        auto next = instructions.begin();
        ++next;
        if (next != instructions.end() && next->type == 3) {
          f64 dx = f64(position.x) - SpLocustInstructionFloat(*next, 0);
          f64 dz = f64(position.z) - SpLocustInstructionFloat(*next, 2);
          if (std::sqrt(dx * dx + dz * dz) > 1536.0)
            SetState(static_cast<SpLocustState>(0));
        }
      }
      complete = true;
      break;
    }
    position.x = f32(f64(elapsed) * vector236.x + position.x);
    position.z = f32(f64(elapsed) * vector236.z + position.z);
    position.y = f32(f64(elapsed) * vector236.y + position.y);
    *moved = true;
    if (SpLocustReached(start, position, vector224)) {
      position = vector224;
      if (instruction.type == kSpLocustMigrate) {
        complete = true;
      } else {
        instruction.payload[3] = (instruction.payload[3] & kCtMicroCellMask) | 1u;
        if (SpEffects::messages) {
          u32 x = u32(i64(position.x)), z = u32(i64(position.z));
          ScMessage message(
              kSpMsg_LocustSprayCheck, reinterpret_cast<uptr>(this), ((x & 0xFF00u) << 8) | ((z & 0xFFFFu) >> 8), 0);
          SpEffects::messages->MessagePost(message, false);
        }
        SetState(static_cast<SpLocustState>(1));
      }
    }
    break;
  case kSpLocustDisappear:
    Shutdown();
    return 0;
  case kSpLocustPoison: {
    if (!(instruction.payload[0] & 255u))
      instruction.payload[0] = (instruction.payload[0] & kCtMicroCellMask) | 1u;
    instruction.payload[1] += SpLocustCoordinate(elapsed);
    mode = 5;
    f64 opacity = 1.0 - f64(i32(instruction.payload[1])) * f64(0.0001111f);
    f32 value = f32(opacity);
    memcpy(&effect_value, &value, sizeof(value));
    if (opacity <= 0.0) {
      effect_value = 0;
      if (SpEffects::box) {
        i32 x = i32(SpLocustCoordinate(position.x)) / kCtMicroUnitsPerCell,
            z = i32(SpLocustCoordinate(position.z)) / kCtMicroUnitsPerCell;
        SpEffects::box->Event(kAuEventPlayEffect, kAuEffectLocustsDie, uptr(iptr(x)), uptr(iptr(z)), 0);
      }
      Shutdown();
      return 0;
    }
    *changed = true;
    return 1;
  }
  case kSpLocustSetSpeed:
    speed = SpLocustInstructionFloat(instruction, 0);
    complete = true;
    break;
  default:
    complete = true;
    break;
  }
  if (complete) {
    instructions.pop_front();
    if (instructions.empty()) {
      instructions_changed = 0;
      GenerateNewRandomDestinationPosition();
    } else {
      PrepNewInstruction();
    }
  }
  return 1;
}

// On cell movement, destroy eligible tree occupants that leave the old five-by-five neighborhood.
void SpLocust::CheckForDestroyedTrees() {
  f64 grid_x = f64(position.x) * 0.00390625, grid_z = f64(position.z) * 0.00390625;
  i32 next_x = i32(i64(grid_x)), next_z = i32(i64(grid_z));
  if (next_x != cell_x || cell_y != 0 || next_z != cell_z) {
    i32 old_left = i32(u32(cell_x) - 2u), old_right = i32(u32(cell_x) + 2u);
    i32 old_top = i32(u32(cell_z) - 2u), old_bottom = i32(u32(cell_z) + 2u);
    i32 new_left = i32(i64(grid_x - 2.0)), new_right = i32(i64(grid_x + 2.0));
    i32 new_top = i32(i64(grid_z - 2.0)), new_bottom = i32(i64(grid_z + 2.0));
    for (i32 x = old_left; x <= old_right; x = i32(u32(x) + 1u)) {
      for (i32 z = old_top; z <= old_bottom; z = i32(u32(z) + 1u)) {
        if (z < new_top || x < new_left || z > new_bottom || x > new_right) {
          ScOccupant *occupant;
          if (SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
            if ((occupant->GetGeomFlag() & kOcGeomFlora) == kOcGeomFlora) {
              u32 cost = 0;
              SpEffects::city->DisasterLayer()->DestroyOccupant(occupant, kDsDamageLocust, &cost);
            }
            occupant->Release();
          }
        }
      }
    }
  }
  cell_x = next_x;
  cell_y = 0;
  cell_z = next_z;
}

SpLocustGroup::SpLocustGroup() : references(0), level(0) {
}

// Remove and shut down children from the back, then cancel pending targeted messages before retiring the queue.
SpLocustGroup::~SpLocustGroup() {
  while (!locusts.empty())
    SpLocustGroup::AddLocustsToGroup(locusts.back(), 0, 1);
  if (SpEffects::messages)
    SpEffects::messages->CancelGeneralMessagePostsToTarget(this);
}

bool SpLocustGroup::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, SpLocustGroupSimulator);
  HANDLE_QUERY_INTERFACE(IID_SpLocustGroupSimulator, SpLocustGroupSimulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpLocustGroup::AddRef() {
  return ++references;
}

// Delete the complete group on its final reference, otherwise decrement its unsigned local count.
u32 SpLocustGroup::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Replay queued instructions and deferred destruction in order.
bool SpLocustGroup::DoMessage(ScMessage *message) {
  if (message->type != kSpMsg_LocustInstructions)
    return true;
  while (instructions.GetMessageCount()) {
    ScMessage next;
    instructions.GetFrontMessage(&next);
    u32 first = (u32)next.data1, second = (u32)next.data2, third = (u32)next.data3;
    i32 index;
    memcpy(&index, &first, sizeof(index));
    switch (next.type) {
    case kSpLocustAttackCrops:
      AddInstructionAttackCrops((i32)(second >> 24), (i32)((second >> 16) & 255),
          (i32)((second >> 8) & 255), index, 0);
      break;
    case kSpLocustDisappear:
      AddInstructionDisappear(index, 0);
      break;
    case kSpLocustMigrate: {
      ScRect area{(i32)(second >> 16), (i32)(second & 65535), (i32)(third >> 16), (i32)(third & 65535)};
      AddInstructionMigrateToArea(area, index, 0);
      break;
    }
    case kSpLocustSetSpeed: {
      const f32 *payload = reinterpret_cast<const f32 *>(next.data2);
      assert(payload);
      f32 value;
      memcpy(&value, payload, sizeof(value));
      AddInstructionSetSpeed(value, index, 0);
      break;
    }
    case kSpMsg_LocustDestroyGroup:
      Shutdown();
      Release();
      return true;
    case kSpMsg_LocustDestroyAll:
      DestroyAllLocusts(0);
      return true;
    default:
      break;
    }
  }
  return true;
}

// Require the animation manager, publish the group level and request the initial child count through the live interface.
u8 SpLocustGroup::Init(u32 count, i32 value) {
  if (!SpEffects::animated)
    return 0;
  level = value;
  SetCount(count);
  return 1;
}

void SpLocustGroup::Shutdown() {
  SetCount(0);
}

// Clamp the requested size, create locusts at a random world edge or remove excess children from the back.
u8 SpLocustGroup::SetCount(u32 count) {
  if (count >= 256)
    count = 256;
  u32 edge = SpEffects::random.Bounded(4), x = 0, z = 0;
  if (edge == 1)
    x = u32(SpEffects::micro_width) - 1u;
  else if (edge == 3)
    z = u32(SpEffects::micro_height) - 1u;
  u32 current = (u32)locusts.size();
  if (current < count) {
    u32 remaining = count - current;
    do {
      SpLocust *locust = new SpLocust;
      if (edge <= 1)
        z = SpEffects::random.Bounded(u32(SpEffects::micro_height));
      else if (edge <= 3)
        x = SpEffects::random.Bounded(u32(SpEffects::micro_width));
      ScVec3F position{f32(x), -1.0f, f32(z)};
      locust->Init(position, level, 0);
      AddLocustsToGroup(locust, 1, 1);
    } while (--remaining);
  }
  current = u32(locusts.size());
  if (current > count)
    for (u32 remaining = current - count; remaining; --remaining)
      AddLocustsToGroup(locusts.back(), 0, 1);
  return 1;
}

u32 SpLocustGroup::GetCount() {
  return (u32)locusts.size();
}

// Retain and associate a new child, or optionally shut down and release a matching child before removing it.
u8 SpLocustGroup::AddLocustsToGroup(SpLocustSimulator *locust, u8 add, u8 destroy) {
  for (usize index = 0; index < locusts.size(); ++index) {
    if (locusts[index] != locust)
      continue;
    if (!add) {
      if (destroy)
        locust->Shutdown();
      locust->Release();
      locusts.erase(locusts.begin() + (isize)index);
    }
    return 0;
  }
  if (!add)
    return 0;
  locust->AddRef();
  locust->SetGroup(this);
  locusts.push_back(locust);
  return 1;
}

// Borrow a child at the requested index, or return null when it is outside the vector.
SpLocustSimulator *SpLocustGroup::GetLocusts(u32 index) {
  return index < locusts.size() ? locusts[index] : nullptr;
}

// Test the group vector for the exact child pointer without changing its ownership.
bool SpLocustGroup::DoesLocustsBelongToGroup(const SpLocustSimulator *locust) const {
  for (SpLocustSimulator *item : locusts)
    if (item == locust)
      return true;
  return false;
}

// Test whether any locust is registered in the global pool under the effects-wide flag.
bool SpLocustGroup::DoesAnyLocustsExistAnywhere() const {
  SpLockEffects();
  bool result = !SpAllLocusts.empty();
  SpUnlockEffects();
  return result;
}

// Search the global pool for the exact borrowed interface pointer while holding the shared effects flag.
bool SpLocustGroup::DoesLocustsExistAnywhere(const SpLocustSimulator *locust) const {
  SpLockEffects();
  bool found = false;
  for (SpLocustSimulator *item : SpAllLocusts) {
    if (item == locust) {
      found = true;
      break;
    }
  }
  SpUnlockEffects();
  return found;
}

// Report the global count or copy up to the caller's capacity of borrowed locust pointers under the shared flag.
u32 SpLocustGroup::GetAllLocustsAnywhere(SpLocustSimulator **objects, u32 *count) const {
  SpLockEffects();
  u32 available = u32(SpAllLocusts.size());
  if (objects) {
    if (*count > available)
      *count = available;
    for (u32 index = 0; index < *count; ++index)
      objects[index] = SpAllLocusts[index];
  } else {
    *count = available;
  }
  SpUnlockEffects();
  return *count;
}

// Copy up to the caller's capacity of global locusts within the original view-centered visibility radius.
u32 SpLocustGroup::GetListOfCurrentlyVisibleLocusts(SpLocustSimulator **objects, u32 *count) const {
  u32 capacity = *count;
  ScRect pixels, cells;
  SpEffects::city->LockGraphics();
  SpEffects::cell_map->GetVisibleAreaRectangle(&pixels, &cells);
  SpEffects::city->UnlockGraphics();
  i32 center_x = i32((u32(pixels.left) + u32(pixels.right)) << kCtMicroCoordinateShift) / 2;
  i32 center_z = i32((u32(pixels.top) + u32(pixels.bottom)) << kCtMicroCoordinateShift) / 2;
  f32 radius = f32(i32(((u32(pixels.right) + 3) << kCtMicroCoordinateShift) - u32(center_x)));
  SpLockEffects();
  u32 size = u32(SpAllLocusts.size());
  *count = 0;
  for (u32 index = 0; index < size && *count < capacity; ++index) {
    SpLocust *locust = static_cast<SpLocust *>(SpAllLocusts[index]);
    f64 dx = f64(locust->position.x) - center_x, dz = f64(locust->position.z) - center_z;
    if (!(std::sqrt(dx * dx + dz * dz) >= radius)) {
      if (objects)
        objects[*count] = SpAllLocusts[index];
      ++*count;
    }
  }
  SpUnlockEffects();
  return *count;
}

// Count all eating locusts and those within the same view-centered visibility radius.
u8 SpLocustGroup::GetEatingSwarmsCount(u32 *eating, u32 *visible) const {
  *eating = 0;
  *visible = 0;
  ScRect pixels, cells;
  SpEffects::city->LockGraphics();
  SpEffects::cell_map->GetVisibleAreaRectangle(&pixels, &cells);
  SpEffects::city->UnlockGraphics();
  i32 center_x = i32((u32(pixels.left) + u32(pixels.right)) << kCtMicroCoordinateShift) / 2;
  i32 center_z = i32((u32(pixels.top) + u32(pixels.bottom)) << kCtMicroCoordinateShift) / 2;
  f32 radius = f32(i32(((u32(pixels.right) + 3) << kCtMicroCoordinateShift) - u32(center_x)));
  SpLockEffects();
  u32 size = u32(SpAllLocusts.size());
  for (u32 index = 0; index < size; ++index) {
    SpLocustSimulator *item = SpAllLocusts[index];
    if (item->GetState() == 1) {
      ++*eating;
      SpLocust *locust = static_cast<SpLocust *>(item);
      f64 dx = f64(locust->position.x) - center_x, dz = f64(locust->position.z) - center_z;
      if (!(std::sqrt(dx * dx + dz * dz) >= radius))
        ++*visible;
    }
  }
  SpUnlockEffects();
  return 1;
}

// Ask the live visibility-list implementation for at most one child and report whether it found one.
i32 SpLocustGroup::IsAnyLocustsCurrentlyVisible() const {
  SpLocustSimulator *locust;
  u32 count = 1;
  return GetListOfCurrentlyVisibleLocusts(&locust, &count) != 0;
}

u32 SpLocustGroup::ReleaseAsynchronous() {
  ScMessage deferred(kSpMsg_LocustDestroyGroup);
  instructions.PushBack(deferred);
  if (SpEffects::messages) {
    ScMessage wake(kSpMsg_LocustInstructions);
    SpEffects::messages->GeneralMessagePostToTarget(wake, this);
  }
  return references - 1;
}

// Queue a destruction request or remove group children, then request destruction of a snapshot of the global pool.
u32 SpLocustGroup::DestroyAllLocusts(u8 deferred) {
  if (deferred) {
    ScMessage instruction(kSpMsg_LocustDestroyAll);
    instructions.PushBack(instruction);
    if (SpEffects::messages) {
      ScMessage wake(kSpMsg_LocustInstructions);
      SpEffects::messages->GeneralMessagePostToTarget(wake, this);
    }
    return 0;
  }
  while (!locusts.empty())
    AddLocustsToGroup(locusts.back(), 0, 1);
  SpLockEffects();
  std::vector<SpLocustSimulator *> snapshot(SpAllLocusts);
  SpUnlockEffects();
  u32 count = u32(snapshot.size());
  for (SpLocustSimulator *locust : snapshot)
    locust->DestroySelfAsSoonAsPossible();
  return count;
}

// Distribute nearest crop targets among the group's swarms, then queue a random target for every swarm.
u8 SpLocustGroup::DistributeTargetListAmongSwarms(ScVec3U *targets, u32 count) {
  std::vector<ScVec3U> pending, positions;
  for (u32 i = 0; i < count; ++i)
    pending.push_back(targets[i]);
  for (SpLocustSimulator *locust : locusts) {
    u32 x, y, z;
    locust->GetPosition(&x, &y, &z);
    positions.push_back({x >> kCtMicroCoordinateShift, y >> kCtMicroCoordinateShift, z >> kCtMicroCoordinateShift});
  }
  while (!pending.empty()) {
    for (u32 index = 0; index < positions.size(); ++index) {
      f32 closest = 3.402823466e38f;
      usize selected = 0;
      for (usize i = 0; i < pending.size(); ++i) {
        u32 dx = positions[index].x - pending[i].x, dz = positions[index].z - pending[i].z;
        u32 squared = dx * dx + dz * dz;
        f64 distance = std::sqrt(f64(squared));
        if (distance < closest) {
          closest = f32(distance);
          selected = i;
        }
      }
      ScVec3U target = pending[selected];
      pending.erase(pending.begin() + selected);
      AddInstructionAttackCrops(i32(target.x), i32(target.y), i32(target.z), i32(index), 1);
      positions[index] = target;
      if (pending.empty())
        break;
    }
  }
  u32 index = 0;
  for (SpLocustSimulator *locust : locusts) {
    u32 x, y, z;
    locust->GetPosition(&x, &y, &z);
    CtCity *city_z = SpGetCity(), *city_x = SpGetCity();
    u32 target_z = SpEffects::random.Bounded(city_z->CellCountZ());
    u32 target_x = SpEffects::random.Bounded(city_x->CellCountX());
    AddInstructionAttackCrops(i32(target_x), i32(target_z), -1, i32(index), 1);
    ++index;
  }
  return 1;
}

// Create a retained death-spray sprite at the requested coordinates, then release the caller's reference.
// The discarded scale of an unwritten temporary vector has no defined effect and is omitted.
void SpLocustGroup::DropDDTTrail(i32 x, i32 z, i32 altitude) {
  SpDeathSpray *spray = new SpDeathSpray;
  spray->AddRef();
  spray->Init(i32(SpLocustCoordinate(f32(x))), i32(SpLocustCoordinate(f32(z))),
      i32(SpLocustCoordinate(f32(altitude))), 0);
  spray->Release();
}

// Queue packed crop coordinates or add an attack instruction to the selected locust immediately.
u8 SpLocustGroup::AddInstructionAttackCrops(i32 x, i32 z, i32 altitude, i32 index, u8 deferred) {
  if (deferred) {
    u32 packed = (u32(u8(x)) << 24) | (u32(u8(z)) << 16) | (u32(u8(altitude)) << 8);
    ScMessage instruction(kSpLocustAttackCrops, u32(index), packed, 0);
    instructions.PushBack(instruction);
    if (SpEffects::messages) {
      ScMessage wake(kSpMsg_LocustInstructions);
      SpEffects::messages->GeneralMessagePostToTarget(wake, this);
    }
    return 1;
  }
  if (locusts.empty())
    return 0;
  i32 selected = index >= i32(locusts.size()) ? i32(locusts.size()) - 1 : index;
  if (selected < 0)
    selected = GetLocustsWithLowestInstructionLoad();
  if (selected == -1)
    return 0;
  SpLocustInstruction instruction{kSpLocustAttackCrops, {}};
  SpLocustSetInstructionFloat(&instruction, 0, f32(i32(u32(x) << kCtMicroCoordinateShift)));
  SpLocustSetInstructionFloat(&instruction, 1, f32(i32(u32(z) << kCtMicroCoordinateShift)));
  SpLocustSetInstructionFloat(&instruction, 2, f32(i32(u32(altitude) << kCtMicroCoordinateShift)));
  return static_cast<SpLocust *>(locusts[selected])->AddInstruction(instruction) != 0;
}

// Queue disappearance or add it to the selected child and remove that child from this group without shutdown.
u8 SpLocustGroup::AddInstructionDisappear(i32 index, u8 deferred) {
  if (deferred) {
    ScMessage instruction(kSpLocustDisappear, u32(index), 0, 0);
    instructions.PushBack(instruction);
    if (SpEffects::messages) {
      ScMessage wake(kSpMsg_LocustInstructions);
      SpEffects::messages->GeneralMessagePostToTarget(wake, this);
    }
    return 1;
  }
  if (locusts.empty())
    return 0;
  i32 selected = index >= i32(locusts.size()) ? i32(locusts.size()) - 1 : index;
  if (selected < 0)
    selected = GetLocustsWithLowestInstructionLoad();
  if (selected == -1)
    return 0;
  SpLocustSimulator *locust = locusts[selected];
  SpLocustInstruction instruction{kSpLocustDisappear, {}};
  if (!static_cast<SpLocust *>(locust)->AddInstruction(instruction))
    return 0;
  AddLocustsToGroup(locust, 0, 0);
  return 1;
}

// Queue a packed rectangle or select a random destination in it for the selected locust.
u8 SpLocustGroup::AddInstructionMigrateToArea(const ScRect &area, i32 index, u8 deferred) {
  if (deferred) {
    u32 first = (u32(u16(area.left)) << 16) | u16(area.top);
    u32 second = (u32(u16(area.right)) << 16) | u16(area.bottom);
    ScMessage instruction(kSpLocustMigrate, u32(index), first, second);
    instructions.PushBack(instruction);
    if (SpEffects::messages) {
      ScMessage wake(kSpMsg_LocustInstructions);
      SpEffects::messages->GeneralMessagePostToTarget(wake, this);
    }
    return 1;
  }
  if (locusts.empty())
    return 0;
  i32 selected = index >= i32(locusts.size()) ? i32(locusts.size()) - 1 : index;
  if (selected < 0)
    selected = GetLocustsWithLowestInstructionLoad();
  if (selected == -1)
    return 0;
  i32 maximum_x = area.right > area.left ? area.right : i32(u32(area.left) + 1u);
  i32 maximum_z = area.bottom > area.top ? area.bottom : i32(u32(area.top) + 1u);
  f32 x = f32(SpEffects::random.Range(area.left, maximum_x));
  f32 z = f32(SpEffects::random.Range(area.top, maximum_z));
  f32 altitude = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(x, z));
  SpLocustInstruction instruction{kSpLocustMigrate, {}};
  SpLocustSetInstructionFloat(&instruction, 0, x);
  SpLocustSetInstructionFloat(&instruction, 1, altitude);
  SpLocustSetInstructionFloat(&instruction, 2, z);
  return static_cast<SpLocust *>(locusts[selected])->AddInstruction(instruction) != 0;
}

// Preserve the deferred no-op or clear the specified child's queue before adding its poison instruction.
u8 SpLocustGroup::AddInstructionPoisonSwarm(i32, i32, SpLocustSimulator *swarm, u8 deferred) {
  if (deferred)
    return 1;
  swarm->ClearAllInstructions();
  SpLocustInstruction instruction{kSpLocustPoison, {}};
  return static_cast<SpLocust *>(swarm)->AddInstruction(instruction) != 0;
}

// Queue the speed's raw float word or add a speed instruction immediately.
u8 SpLocustGroup::AddInstructionSetSpeed(f32 speed, i32 index, u8 deferred) {
  if (deferred) {
    u32 bits;
    memcpy(&bits, &speed, sizeof(bits));
    ScMessage instruction(kSpLocustSetSpeed, u32(index), bits, 0);
    instructions.PushBack(instruction);
    if (SpEffects::messages) {
      ScMessage wake(kSpMsg_LocustInstructions);
      SpEffects::messages->GeneralMessagePostToTarget(wake, this);
    }
    return 1;
  }
  if (locusts.empty())
    return 0;
  i32 selected = index >= i32(locusts.size()) ? i32(locusts.size()) - 1 : index;
  if (selected < 0)
    selected = GetLocustsWithLowestInstructionLoad();
  if (selected == -1)
    return 0;
  SpLocustInstruction instruction{kSpLocustSetSpeed, {}};
  SpLocustSetInstructionFloat(&instruction, 0, speed);
  return static_cast<SpLocust *>(locusts[selected])->AddInstruction(instruction) != 0;
}

i32 SpLocustGroup::GetInstructionLoad() {
  u32 count = u32(locusts.size()), total = 0;
  if (!count)
    return 100;
  for (SpLocustSimulator *item : locusts)
    total += u32(static_cast<SpLocust *>(item)->instructions.size());
  i32 load = i32((100u * total) / (5u * count));
  return load <= 100 ? load : 100;
}

// Find the first locust with the fewest queued instructions, returning minus one for an empty group.
i32 SpLocustGroup::GetLocustsWithLowestInstructionLoad() {
  u32 lowest = UINT32_MAX;
  i32 selected = -1;
  for (u32 index = 0; index < locusts.size(); ++index) {
    u32 count = u32(static_cast<SpLocust *>(locusts[index])->instructions.size());
    if (count < lowest) {
      lowest = count;
      selected = i32(index);
    }
  }
  return selected;
}
