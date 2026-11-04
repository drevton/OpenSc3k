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

// Locust sprite frames advance after this elapsed-time interval.
enum SpLocustFrameTiming : u32 {
  kSpLocustFrameInterval = 0x186a0,
};

// Whirlpool capture accepts the water-vehicle or air-vehicle geometry masks after the terrain-water test.
enum SpWhirlpoolVehicleGeometry : u32 {
  kSpWhirlpoolWaterVehicleMask = 0x2001000,
  kSpWhirlpoolAirVehicleMask = 0x4001000,
};

// Fade-in and fade-out share one artwork sequence; the active phase uses the second sequence.
enum SpWhirlpoolPhase : i32 {
  kSpWhirlpoolInactive = 0,
  kSpWhirlpoolFadeIn = 1,
  kSpWhirlpoolActive = 2,
  kSpWhirlpoolFadeOut = 3,
};

// Keep movement notifications, terrain sampling and the two low-byte cycle phases on their original tick schedules.
enum SpWhirlpoolSchedule : u32 {
  kSpWhirlpoolMovementNotifyMask = 3,
  kSpWhirlpoolTerrainSampleMask = 15,
  kSpWhirlpoolLastWarmupTick = 15,
  kSpWhirlpoolAbductionTickPhase = 0,
  kSpWhirlpoolBridgeTickPhase = 128,
};

// Each toxic-steam animation stage has separate color and mask artwork before the zoom offset.
enum SpSteamArtwork : u32 {
  kSpSteamStageZeroColorImageBase = 0x48580000,
  kSpSteamStageOneColorImageBase = 0x4858200a,
  kSpSteamStageTwoColorImageBase = 0x48584014,
  kSpSteamStageZeroMaskImageBase = 0x48581005,
  kSpSteamStageOneMaskImageBase = 0x4858300f,
  kSpSteamStageTwoMaskImageBase = 0x48585019,
};

// Original effect families retain independent frame, scheduling and lifetime thresholds.
enum SpDisasterEffectTiming : i32 {
  kSpSteamFrameInterval = 200000,
  kSpSpaceDebrisDefaultInterval = 750000,
  kSpImpactInitialLifetime = 300000,
  kSpImpactInitialFrameInterval = 2000000,
  kSpImpactZoomTwoLateFrameInterval = 1300000,
  kSpImpactZoomTwoEarlyFrameInterval = 300000,
  kSpImpactDistantLateFrameInterval = 3000000,
  kSpImpactDistantEarlyFrameInterval = 1000000,
  kSpImpactNearFrameInterval = 250000,
  kSpFireFrameIntervalJitter = 100000,
  kSpFireMinimumFrameInterval = 400000,
  kSpWhirlpoolPieceFrameInterval = 250000,
  kSpWhirlpoolFrameInterval = 250000,
  kSpWhirlpoolAbductedDebrisLifetime = 3600000,
  kSpTornadoDebrisMinimumFrameInterval = 200000,
  kSpTornadoDebrisMaximumFrameInterval = 300000,
  kSpTornadoFrameInterval = 200000,
  kSpDeathSprayFrameInterval = 125000,
};

// Whirlpool pieces select artwork by their original variant dispatch; the fallback keeps its own image family.
enum SpWhirlpoolArtwork : u32 {
  kSpWhirlpoolVariant0ImageBase = 0x485b0000,
  kSpWhirlpoolVariant1ImageBase = 0x485c0000,
  kSpWhirlpoolVariant2ImageBase = 0x485d0000,
  kSpWhirlpoolVariant3ImageBase = 0x485e0000,
  kSpWhirlpoolVariant4ImageBase = 0x48620000,
  kSpWhirlpoolVariant5ImageBase = 0x48630000,
  kSpWhirlpoolVariant6ImageBase = 0x485f0000,
  kSpWhirlpoolVariant7ImageBase = 0x48610000,
  kSpWhirlpoolDefaultImageBase = 0x48600000,
};

// A stopped debris schedule and an indefinite debris lifetime use separate state sentinels.
enum SpDebrisTimeSentinel : u32 {
  kSpDebrisNoLifetime = 0xffffffff,
  kSpDebrisScheduleStopped = 0xffffffff,
};

// Large locust overlays select a variant from separate contiguous color and mask image families.
enum SpLocustOverlayArtwork : u32 {
  kSpLocustOverlayColorBase = 0x3fb10000,
  kSpLocustOverlayMaskBase = 0x3fb10005,
};

// Tornado image families retain separate animated frames, base color/mask and overlay color/mask resource ranges.
enum SpTornadoArtwork : u32 {
  kSpTornadoFrameImageBase = 0xfffe5000,
  kSpTornadoBaseColorImageBase = 0xfffe5600,
  kSpTornadoBaseMaskImageBase = 0xfffe5700,
  kSpTornadoOverlayColorImageBase = 0xfffe5800,
  kSpTornadoOverlayMaskImageBase = 0xfffe5900,
};

// Installed artwork identities retain each effect's color, mask, overlay and shadow roles.
enum SpDisasterEffectImageId : u32 {
  kSpAcidRainTopImageBase = 0x48590005,
  kSpAcidRainMiddleImageBase = 0x4859000a,
  kSpAcidRainBottomImageBase = 0x4859000f,
  kSpDeathSprayColorImageBase = 0x3fb90000,
  kSpDeathSprayMaskImageBase = 0x3fb90005,
  kSpLocustColorImageBase = 0x3fac0000,
  kSpLocustMaskImageBase = 0x3fac0005,
  kSpLocustShadowMaskImageBase = 0x3fcb0000,
  kSpFireColorImageBase = 0xfffe1000,
  kSpFireMaskImageBase = 0xfffe1020,
  kSpSpaceDebrisShadowImageId = 0xfffe1050,
  kSpImpactTwoCellColorImageBase = 0x3fc80000,
  kSpImpactThreeCellColorImageBase = 0x3fc90000,
  kSpImpactFourCellColorImageBase = 0x3fca0000,
  kSpImpactFiveCellColorImageBase = 0x3fc50000,
  kSpWhirlpoolOccupantColorImageBase = 0x48640000,
  kSpWhirlpoolOccupantMaskImageBase = 0x48640005,
  kSpWhirlpoolDebrisColorImageBase = 0x2d490000,
  kSpWhirlpoolDebrisMaskImageBase = 0x2d490005,
  kSpWhirlpoolPrimaryColorImageBase = 0x48590000,
  kSpWhirlpoolPrimaryMaskImageBase = 0x48591005,
  kSpWhirlpoolSecondaryColorImageBase = 0x4859200a,
  kSpWhirlpoolSecondaryMaskImageBase = 0x4859300f,
};

// Preserve the biased double-store conversion used by the steam occupant's animation callbacks.
static i32 SpSteamCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return i32(u32(bits) + kScDoubleIntegerLowWordBias);
}

// Construct the rain and return its original secondary display identity to the class factory.
ScUnknown *SpCreateAcidRainDisplay() {
  return static_cast<SpAcidRainDisplay *>(new SpAcidRain);
}

SpToxicSteam::SpToxicSteam() : registered(0), frame(0), elapsed(0), stage(-1) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpToxicSteam::~SpToxicSteam() {
  RemoveFromAnimationSystem();
}

// Select the color or mask resource for a steam stage and the original five zooms.
u32 SpToxicSteam::GetSpriteKey(i32 value_stage, i32 zoom, bool is_mask) {
  static const u32 kSpSteamStageColorImages[3] = {
      kSpSteamStageZeroColorImageBase, kSpSteamStageOneColorImageBase, kSpSteamStageTwoColorImageBase};
  static const u32 kSpSteamStageMaskImages[3] = {
      kSpSteamStageZeroMaskImageBase, kSpSteamStageOneMaskImageBase, kSpSteamStageTwoMaskImageBase};
  return (is_mask ? kSpSteamStageMaskImages[value_stage] : kSpSteamStageColorImages[value_stage]) +
      u32(zoom < 4 ? zoom + 1 : zoom);
}

// Demand the stage's color and mask images and copy their registration, doubling the last zoom.
// The original immediately consumes both returned images; nonnull outputs are postconditions.
u8 SpToxicSteam::DemandArt(i32 zoom, i32, bool) {
  if (!color[zoom]) {
    SpImage *image = nullptr;
    SpEffects::images->PrepareImage(GetSpriteKey(stage, zoom, false), 0, &image);
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
    SpEffects::images->PrepareImage(GetSpriteKey(stage, zoom, true), 0, &image);
    assert(image);
    mask[zoom] = image->buffer;
  }
  return 1;
}

i32 SpToxicSteam::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(stage, zoom, false), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(stage, zoom, true), 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Read registration rectangles for the active stage and double the last zoom's coordinates.
void SpToxicSteam::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(stage, zoom, false), 0);
    if (image)
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

// Swap stage art at every zoom, reset the horizontal frames and restart the stage animation.
u8 SpToxicSteam::SetSteamStage(i32 next) {
  i32 previous = stage;
  if (previous == next)
    return 1;
  stage = next;
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    if (color[zoom]) {
      stage = previous;
      ReleaseArt(zoom, 0);
      stage = next;
    }
    DemandArt(zoom, 0, true);
    origins[zoom].x = 0;
  }
  frame = 0;
  return 1;
}

u8 SpToxicSteam::StopSteaming() {
  return SetSteamStage(2);
}

void SpToxicSteam::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpSteamCoordinate(position.x),
        SpSteamCoordinate(position.z), SpSteamCoordinate(position.y), 5, 0, true);
  }
}

// Remove a registered steam occupant using the shared original particle-sprite removal body.
void SpToxicSteam::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpSteamCoordinate(position.x),
          SpSteamCoordinate(position.z), SpSteamCoordinate(position.y));
  }
}

// Place an in-bounds steam sprite at terrain height, acquire its first stage and register it for animation.
u8 SpToxicSteam::Init(i32 x, i32 z) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  position.x = f32(x);
  position.z = f32(z);
  if (!SpEffects::dirt)
    return 0;
  u8 altitude;
  SpEffects::dirt->GetVertexAltitude(u32(x) >> kCtMicroCoordinateShift, u32(z) >> kCtMicroCoordinateShift, &altitude);
  mode = 2;
  position.y = f32(u32(altitude) << kCtMicroCoordinateShift);
  SetSteamStage(0);
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Advance steam frames and stages, refresh the occupant and follow changes in the terrain altitude.
i32 SpToxicSteam::Tick(u32 delta) {
  elapsed += delta;
  if (elapsed > kSpSteamFrameInterval) {
    ++frame;
    elapsed = 0;
    if (stage == 1) {
      if (frame == 4)
        frame = 0;
    } else if (stage == 2 && frame == 4) {
      frame = 0;
      RemoveFromAnimationSystem();
      return 0;
    }
    if (stage == 0 && frame == 4) {
      SetSteamStage(1);
    } else {
      for (i32 zoom = 0; zoom < 5; ++zoom) {
        i32 width = GetW(zoom, 0);
        origins[zoom].x = i32(u32(frame) * u32(width));
      }
    }
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpSteamCoordinate(position.x),
        SpSteamCoordinate(position.z), SpSteamCoordinate(position.y), 2, nullptr);
  }
  i32 altitude = SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z);
  i32 old_altitude = i32(i64(position.y));
  if (old_altitude != altitude) {
    i32 x = SpSteamCoordinate(position.x), z = SpSteamCoordinate(position.z);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, old_altitude, x, z, altitude);
    position.y = f32(altitude);
  }
  return 0;
}

SpAcidRain::SpAcidRain()
    : steam(nullptr), x(0), altitude(0), z(0), stopping(0), registered(0), middle{}, bottom{}, height(0),
      current_height(0.0f), stopping_height(0.0f), offset(0.0f), speed(0.000512f), byte284{}, band_fourth{} {
  mode = 2;
  geom_flags |= kSpEffectBaseGeometry;
}

SpAcidRain::~SpAcidRain() {
  RemoveFromAnimationSystem();
}

bool SpAcidRain::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpAcidRainDisplay, SpAcidRainDisplay);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpAcidRain::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpAcidRain::Release() {
  return SpEffectOccupant::Release();
}

u32 SpAcidRain::GetSpriteKey(i32 zoom, u32 base) {
  return base + u32(zoom) + (zoom < 4 ? 1u : 0u);
}

u8 SpAcidRain::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, kSpAcidRainTopImageBase), 0, &image);
  color[zoom] = image->buffer;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, kSpAcidRainMiddleImageBase), 0, &image);
  middle[zoom] = image->buffer;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, kSpAcidRainBottomImageBase), 0, &image);
  bottom[zoom] = image->buffer;
  return 1;
}

// Release only the three image demands whose borrowed buffers are present for this zoom.
i32 SpAcidRain::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, kSpAcidRainTopImageBase), 0);
    color[zoom] = nullptr;
  }
  if (middle[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, kSpAcidRainMiddleImageBase), 0);
    middle[zoom] = nullptr;
  }
  if (bottom[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, kSpAcidRainBottomImageBase), 0);
    bottom[zoom] = nullptr;
  }
  return 0;
}

// Intersect the visible rain bands with the requested clip and draw them through the source span interface.
u8 SpAcidRain::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32, ScRect *clip) const {
  ScRect area = rect;
  if (band_fourth[zoom] > 0) {
    i32 first = clip ? clip->top - area.top : 0;
    i32 last = (clip ? clip->bottom : area.bottom) - area.top;
    void *object = nullptr;
    if (color[zoom]->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      i32 lower = band_third[zoom] > first ? band_third[zoom] : first;
      i32 upper = band_fourth[zoom] < last ? band_fourth[zoom] : last;
      if (upper > lower)
        DrawRainBottom(lower, upper, target, span, area, zoom, clip);
      lower = band_first[zoom] > first ? band_first[zoom] : first;
      upper = band_second[zoom] < last ? band_second[zoom] : last;
      if (upper > lower)
        DrawRainTop(lower, upper, target, span, area, zoom, clip);
      if (byte284[zoom]) {
        lower = band_second[zoom] > first ? band_second[zoom] : first;
        upper = band_third[zoom] < last ? band_third[zoom] : last;
        if (upper > lower)
          DrawRainMiddle(lower, upper, target, span, area, zoom, clip);
      }
      span->Release();
    }
  }
  return 1;
}

// Convert the requested anchor to the zoom's registration rectangle, then dispatch to the live rectangle draw.
u8 SpAcidRain::Draw(GxBuffer *target, i32 draw_x, i32 draw_y, i32 zoom, i32 rotation, ScRect *clip) const {
  const ScRect &extent = registration[zoom];
  ScRect rect{draw_x - extent.left, draw_y - extent.top, draw_x + extent.right, draw_y + extent.bottom};
  Draw(target, rect, zoom, rotation, clip);
  return 1;
}

// Draw one rain strip, splitting the source at its wrapping boundary and preserving the two alpha rectangles.
void SpAcidRain::BlitWithSourceWrapAround(GxBuffer *target, GxImageBuilder *span, GxBuffer *, GxBuffer *piece,
    const ScRect &rect, i32 zoom, ScRect *clip, i32 length, i32 source_y, i32 piece_y, i32 target_y) const {
  u32 shift = shifts[zoom] & 31;
  i32 rows = length >> shift, boundary = top_height[zoom];
  GxBuffer *source = span->AsIGZBuffer();
  ScRect dest = rect;
  dest.top += target_y;
  if (rows + source_y > boundary) {
    i32 first = boundary - source_y;
    ScRect source_rect{0, source_y, i32(source->Width()), boundary};
    ScRect mask_rect{0, piece_y, i32(piece->Width()), piece_y + first};
    dest.bottom = dest.top + (first << shift);
    if (shift)
      span->SpanBufferAlphaBltWithSeparateAlphaRectStretch(target, piece, source_rect, mask_rect, dest, clip);
    else
      span->SpanBufferAlphaBltWithSeparateAlphaRect(target, piece, source_rect, mask_rect,
          ScPoint{dest.left, dest.top}, clip);
    dest.top = dest.bottom;
    mask_rect.top = mask_rect.bottom;
    i32 rest = rows - first;
    mask_rect.bottom += rest;
    source_rect = {0, 0, i32(source->Width()), rest};
    if (shift) {
      dest.bottom = rect.top + target_y + length;
      span->SpanBufferAlphaBltWithSeparateAlphaRectStretch(target, piece, source_rect, mask_rect, dest, clip);
    } else {
      span->SpanBufferAlphaBltWithSeparateAlphaRect(target, piece, source_rect, mask_rect,
          ScPoint{dest.left, dest.top}, clip);
    }
  } else {
    ScRect source_rect{0, source_y, i32(source->Width()), source_y + rows};
    ScRect mask_rect{0, piece_y, i32(piece->Width()), piece_y + rows};
    if (shift) {
      dest.bottom = dest.top + length;
      span->SpanBufferAlphaBltWithSeparateAlphaRectStretch(target, piece, source_rect, mask_rect, dest, clip);
    } else {
      span->SpanBufferAlphaBltWithSeparateAlphaRect(target, piece, source_rect, mask_rect,
          ScPoint{dest.left, dest.top}, clip);
    }
  }
}

// Align and wrap the lower rain image against the scrolling primary image before drawing the requested band.
void SpAcidRain::DrawRainBottom(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
    i32 zoom, ScRect *clip) const {
  span->AsIGZBuffer();
  u32 shift = shifts[zoom] & 31;
  i32 piece_y = bottom_height[zoom] - ((band_fourth[zoom] - first) >> shift);
  i32 source_y = ((first >> shift) + band_fifth[zoom]) % top_height[zoom];
  if (source_y < 0)
    source_y += top_height[zoom];
  BlitWithSourceWrapAround(target, span, mask[zoom], bottom[zoom], rect, zoom, clip, last - first, source_y,
      piece_y, first);
}

void SpAcidRain::DrawRainMiddle(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
    i32 zoom, ScRect *clip) const {
  GxBuffer *source = span->AsIGZBuffer();
  ScRect dest = rect;
  i32 end = last + dest.top, boundary = top_height[zoom];
  u32 shift = shifts[zoom] & 31;
  i32 source_y = ((first >> shift) + band_fifth[zoom]) % boundary;
  if (source_y < 0)
    source_y += boundary;
  ScRect source_rect{0, source_y, i32(source->Width()), 0};
  dest.top += first;
  while (dest.top < end) {
    source_rect.bottom = source_rect.top + ((end - dest.top) >> shift);
    if (source_rect.bottom > boundary) {
      source_rect.bottom = boundary;
      dest.bottom = dest.top + ((boundary - source_rect.top) << shift);
    } else {
      dest.bottom = end;
    }
    if (shift)
      source->BltStretchSrc(target, source_rect, dest, clip);
    else
      source->BltSrc(target, source_rect, dest, clip);
    source_rect.top = 0;
    dest.top = dest.bottom;
  }
}

// Align and wrap the upper rain image against the scrolling primary image before drawing the requested band.
void SpAcidRain::DrawRainTop(i32 first, i32 last, GxBuffer *target, GxImageBuilder *span, const ScRect &rect,
    i32 zoom, ScRect *clip) const {
  span->AsIGZBuffer();
  u32 shift = shifts[zoom] & 31;
  i32 piece_y = (first - band_first[zoom]) >> shift;
  i32 source_y = ((first >> shift) + band_fifth[zoom]) % top_height[zoom];
  if (source_y < 0)
    source_y += top_height[zoom];
  BlitWithSourceWrapAround(target, span, mask[zoom], middle[zoom], rect, zoom, clip, last - first, source_y,
      piece_y, first);
}

// Populate rain registration and image heights from the original cache and cell-map height conversion.
void SpAcidRain::InitializeBufferSizeInfo() {
  SpImage *image = nullptr;
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, kSpAcidRainTopImageBase), 0);
    if (image) {
      registration[zoom].left = image->left;
      registration[zoom].top = 0;
      registration[zoom].right = image->right;
      registration[zoom].bottom = SpEffects::cell_map->MicroYToPixelUnits(height, zoom);
      top_height[zoom] = image->top + image->bottom;
    }
    if (zoom < 4) {
      shifts[zoom] = 0;
    } else {
      registration[zoom].left *= 2;
      registration[zoom].right *= 2;
      shifts[zoom] = 1;
    }
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, kSpAcidRainMiddleImageBase), 0, &image);
    middle_height[zoom] = image->top + image->bottom;
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, kSpAcidRainBottomImageBase), 0, &image);
    bottom_height[zoom] = image->top + image->bottom;
  }
}

void SpAcidRain::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    SpEffects::cell_map->IncrementAlphaBlendCount(1);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, altitude, 1, 0, true);
  }
}

// Reject out-of-bounds or terrain-level rain, otherwise initialize its artwork and animation membership.
u8 SpAcidRain::Init(i32 value_x, i32 value_z, i32 value_altitude) {
  if (u32(value_x) >= u32(SpEffects::micro_width) || u32(value_z) >= u32(SpEffects::micro_height))
    return 0;
  x = value_x;
  z = value_z;
  altitude = value_altitude;
  u8 ground;
  SpEffects::dirt->GetVertexAltitude(u32(x) >> kCtMicroCoordinateShift, u32(z) >> kCtMicroCoordinateShift, &ground);
  i32 base = (i32(ground) + 4) << kCtMicroCoordinateShift;
  if (base >= altitude)
    return 0;
  height = altitude - base;
  current_height = 0.0f;
  InitializeBufferSizeInfo();
  AddToAnimationSystem();
  return 1;
}

// Advance the rain column, create steam at ground contact and refresh the zoom-specific animation bands.
i32 SpAcidRain::Tick(u32 elapsed) {
  f64 advance = f64(elapsed) * speed;
  if (stopping) {
    f64 next = f64(stopping_height) + advance;
    stopping_height = f32(next);
    if (next >= current_height) {
      RemoveFromAnimationSystem();
      return 0;
    }
  }
  if (f64(height) > current_height)
    current_height = f32(advance + current_height);
  else if (f64(height) < current_height)
    current_height = f32(height);
  if (f64(height) > current_height) {
    offset = 0;
  } else {
    offset = f32(f64(offset) - advance);
    if (!steam) {
      steam = ScAddRefNotNull(new SpToxicSteam);
      steam->Init(x, z);
    }
  }
  i32 current = i32(current_height), stopped = i32(stopping_height);
  band_fifth[0] = SpEffects::cell_map->MicroYToPixelUnits(i32(f64(offset) - current_height), 0);
  band_fifth[0] %= top_height[0];
  if (band_fifth[0] < 0)
    band_fifth[0] += top_height[0];
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    band_fourth[zoom] = SpEffects::cell_map->MicroYToPixelUnits(current, zoom);
    band_first[zoom] = SpEffects::cell_map->MicroYToPixelUnits(stopped, zoom);
    if (zoom > 0) {
      band_fifth[zoom] = band_fifth[zoom - 1];
      if (zoom < 4) {
        band_fifth[zoom] = i32(u32(band_fifth[zoom]) * 2u) % top_height[zoom];
        if (band_fifth[zoom] < 0)
          band_fifth[zoom] += top_height[zoom];
      }
    }
    i32 bottom_size = i32(u32(bottom_height[zoom]) << shifts[zoom]);
    i32 middle_size = i32(u32(middle_height[zoom]) << shifts[zoom]);
    if (i32(u32(bottom_size) + u32(middle_size) + u32(band_first[zoom])) <= band_fourth[zoom]) {
      byte284[zoom] = 1;
      band_second[zoom] = i32(u32(middle_size) + u32(band_first[zoom]));
      band_third[zoom] = i32(u32(band_fourth[zoom]) - u32(bottom_size));
    } else {
      byte284[zoom] = 0;
      i32 span = i32(u32(band_fourth[zoom]) - u32(band_first[zoom]));
      band_second[zoom] = i32(u32(band_first[zoom]) +
          u32(i32(u32(middle_size) * u32(span)) / i32(u32(middle_size) + u32(bottom_size))));
      band_third[zoom] = band_second[zoom];
    }
  }
  SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), x, z, altitude, 2, nullptr);
  return 0;
}

// Request rain shutdown and retire the steam object if one exists.
u8 SpAcidRain::StopRaining() {
  stopping = 1;
  if (steam) {
    steam->StopSteaming();
    steam->Release();
    steam = nullptr;
  }
  return 1;
}

// Remove the rain occupant after retiring its optional steam and clearing animation membership.
void SpAcidRain::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (steam) {
      steam->StopSteaming();
      steam->Release();
      steam = nullptr;
    }
    if (SpEffects::animated) {
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, altitude);
      SpEffects::cell_map->IncrementAlphaBlendCount(-1);
    }
  }
}

// Recover the original biased double-store coordinate conversion without aliasing or out-of-range integer casts.
static i32 SpDebrisCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return i32(u32(bits) + kScDoubleIntegerLowWordBias);
}

static u32 SpTornadoImageKey(i32 zoom, i8 variant, u32 base) {
  return base + u32(zoom < 4 ? zoom + 1 : zoom) + u32(variant) * 5u;
}

static void SpAcquireEffectsFlag() {
  while (SpEffects::thread_safe_flag.fetch_or(1) & 1)
    ScYieldThread();
}

SpSpaceDebrisDisplay::SpSpaceDebrisDisplay()
    : references(0), registered(0), elapsed(0), next_time(kSpDebrisScheduleStopped),
      average_time(kSpSpaceDebrisDefaultInterval) {
}

SpSpaceDebrisDisplay::~SpSpaceDebrisDisplay() {
  SpSpaceDebrisDisplay::Shutdown();
}

// Publish the primary message/display identity or the adjusted animation-tick identity and retain the complete object.
bool SpSpaceDebrisDisplay::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, SpDebrisDisplay);
  HANDLE_QUERY_INTERFACE(IID_SpDebrisDisplay, SpDebrisDisplay);
  HANDLE_QUERY_INTERFACE(IID_SpTickable, SpTickable);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpSpaceDebrisDisplay::AddRef() {
  return ++references;
}

// Destroy on the last hold without first clearing the count; otherwise return the decremented original word.
u32 SpSpaceDebrisDisplay::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

bool SpSpaceDebrisDisplay::DoMessage(ScMessage *) {
  return true;
}

// Register the tick identity once, then subscribe the primary message identity in the original order.
u8 SpSpaceDebrisDisplay::Init() {
  if (!registered) {
    registered = 1;
    SpEffects::tick_manager->AddTickableObject(static_cast<SpTickable *>(this));
    SpGetMessageServer()->AddNotification(this, kSpMsg_BlastTargets);
    SpGetMessageServer()->AddNotification(this, kSpMsg_BuildingImpact);
    SpGetMessageServer()->AddNotification(this, kSpMsg_WaterImpact);
    SpGetMessageServer()->AddNotification(this, kSpMsg_GroundImpact);
  }
  return 1;
}

// Unsubscribe in reverse message order before removing the animation-tick identity; repeated shutdown does nothing.
u8 SpSpaceDebrisDisplay::Shutdown() {
  if (registered) {
    registered = 0;
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_GroundImpact);
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_WaterImpact);
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_BuildingImpact);
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_BlastTargets);
    SpEffects::tick_manager->RemoveTickableObject(static_cast<SpTickable *>(this));
  }
  return 1;
}

// Reset the active animation clock and select the first debris deadline from the shared random stream.
u8 SpSpaceDebrisDisplay::Start() {
  elapsed = 0;
  SetNextDebrisTime();
  return 1;
}

u8 SpSpaceDebrisDisplay::Stop() {
  next_time = kSpDebrisScheduleStopped;
  return 1;
}

void SpSpaceDebrisDisplay::SetAverageDebrisTime(u32 duration) {
  average_time = duration;
}

u32 SpSpaceDebrisDisplay::GetCountOfRemainingSpaceDebris() {
  return SpTrajectory::GetCountOfTrajectoryObjectsStatic(kSpSpaceDebrisTrajectorySignature);
}

void SpSpaceDebrisDisplay::SetNextDebrisTime() {
  u32 scaled = average_time >> 10;
  next_time = u32(SpEffects::random.Range(i32(768u * scaled), i32(1536u * scaled)));
}

// The tick caller discards the final release result, so only the observed object and ownership effects are retained.
void SpSpaceDebrisDisplay::CreateNewRandomDebris() {
  f32 x = f32(SpEffects::random.RandomDoubleRangeUniform(512.0, f64(i32(u32(SpEffects::micro_width) - 512))));
  f32 z = f32(SpEffects::random.RandomDoubleRangeUniform(512.0, f64(i32(u32(SpEffects::micro_height) - 512))));
  ScVec3F velocity{0.64999998f, -1.0f, 0.0f};
  i32 start_x = SpDebrisCoordinate(f32(x - 5000.0f)), start_z = SpDebrisCoordinate(z);
  i32 altitude = SpDebrisCoordinate(f64(SpEffects::GetAltitudeOfTerrainInAnimUnits(x, z)) + 80000.0);
  i32 kind = i32(SpEffects::random.Bounded(4) + 6);
  SpTrajectory *trajectory = new SpTrajectory;
  trajectory->AddRef();
  trajectory->EnableCreationOutsideWorldBoundaries(1);
  trajectory->SetSourceSignature(kSpSpaceDebrisTrajectorySignature);
  trajectory->SetTrajectoryType(SpTrajectoryView::Motion::Linear, nullptr);
  trajectory->AddShadow(kSpSpaceDebrisShadowImageId, 0);
  trajectory->Init(start_x, start_z, altitude, velocity, kind);
  trajectory->Release();
}

void SpSpaceDebrisDisplay::DoAnimationTick(u32 delta) {
  elapsed += delta;
  if (next_time != kSpDebrisScheduleStopped && elapsed > next_time) {
    elapsed = 0;
    SetNextDebrisTime();
    CreateNewRandomDebris();
  }
}

void SpSpaceDebrisDisplay::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 160;
  *interval = 4;
}

// Construct the shared sprite, reset the impact and artwork state, and mark the original effects geometry bit.
SpSpaceDebrisImpactDisplay::SpSpaceDebrisImpactDisplay()
    : size_kind(0), registered(0), color_base(0), mask_base(0), age(0), frame_interval(kSpImpactInitialFrameInterval),
      time_to_live(kSpImpactInitialLifetime), elapsed(0), last_frame(0), fade_direction(0) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpSpaceDebrisImpactDisplay::~SpSpaceDebrisImpactDisplay() {
  RemoveFromAnimationSystem();
}

bool SpSpaceDebrisImpactDisplay::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpImpactDisplay, SpImpactDisplay);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpSpaceDebrisImpactDisplay::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpSpaceDebrisImpactDisplay::Release() {
  return SpEffectOccupant::Release();
}

// Fill each zoom's rectangle from the actual cached color metadata, leaving missing entries unchanged.
void SpSpaceDebrisImpactDisplay::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(color_base + u32(zoom), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Publish the effect's occupant at its current rounded coordinates once, then reset its frame clock.
void SpSpaceDebrisImpactDisplay::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 1, 0, true);
    last_frame = 0;
  }
}

// Clear the registration flag before removing the effect through the live animation manager, if it exists.
void SpSpaceDebrisImpactDisplay::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (SpEffects::animated) {
      i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y);
    }
  }
}

// Bind a valid impact position to terrain, select its original artwork and publish the actual animation occupant.
u8 SpSpaceDebrisImpactDisplay::Init(i32 x, i32 z, i32 size) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  position.x = f32(x);
  position.z = f32(z);
  // The original output aliases the high byte of the initialized X argument if the terrain call leaves it untouched.
  u8 altitude = u8(u32(x) >> 24);
  SpEffects::dirt->GetVertexAltitude(u32(x) / 256, u32(z) / 256, &altitude);
  position.y = f32(u32(altitude) << kCtMicroCoordinateShift);
  size_kind = u32(size);
  mode = 2;
  f32 opacity = 1.0f;
  memcpy(&effect_value, &opacity, sizeof(opacity));
  if (size == 0)
    color_base = kSpImpactTwoCellColorImageBase;
  else if (size == 1)
    color_base = kSpImpactThreeCellColorImageBase;
  else if (size == 2)
    color_base = kSpImpactFourCellColorImageBase;
  else
    color_base = kSpImpactFiveCellColorImageBase;
  mask_base = color_base + 5;
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Compare the changed inclusive cell area with the impact footprint and remove overlapping animation occupancy.
void SpSpaceDebrisImpactDisplay::DoTerrainChanged(const ScRect &rectangle) {
  u32 diameter = size_kind == 0 ? 512 : size_kind == 1 ? 768 : size_kind == 2 ? 1024 : 1280;
  u32 half = diameter >> 1;
  i32 x = SpDebrisCoordinate(position.x), z = SpDebrisCoordinate(position.z);
  ScRect terrain{i32(u32(rectangle.left) << kCtMicroCoordinateShift),
      i32(u32(rectangle.top) << kCtMicroCoordinateShift), i32((u32(rectangle.right) + 1) << kCtMicroCoordinateShift),
      i32((u32(rectangle.bottom) + 1) << kCtMicroCoordinateShift)};
  ScRect impact{i32(u32(x) - half), i32(u32(z) - half), i32(u32(x) - half + diameter),
      i32(u32(z) - half + diameter)};
  ScRect intersection;
  if (intersection.IntersectInclusive(&terrain, &impact))
    RemoveFromAnimationSystem();
}

// Advance lifetime and fading using the original zoom-dependent update interval, notifying the animation manager
// only when the strict frame deadline has passed. A fully faded impact removes its occupant and reports zero.
i32 SpSpaceDebrisImpactDisplay::Tick(u32 delta) {
  f32 milliseconds = f32(f64(delta) * 0.001);
  if (time_to_live < INT32_MAX) {
    time_to_live -= delta >> 10;
    if (time_to_live > INT32_MAX) {
      time_to_live = kSpDebrisNoLifetime;
      fade_direction = -1;
      mode = 6;
    }
  }
  age += u32(SpDebrisCoordinate(milliseconds));
  if (u32(SpEffects::zoom) < 3) {
    if (SpEffects::zoom == 2)
      frame_interval = age >= 5000 ? kSpImpactZoomTwoLateFrameInterval : kSpImpactZoomTwoEarlyFrameInterval;
    else
      frame_interval = age >= 5000 ? kSpImpactDistantLateFrameInterval : kSpImpactDistantEarlyFrameInterval;
  } else {
    frame_interval = kSpImpactNearFrameInterval;
  }
  elapsed += delta;
  if (elapsed > last_frame + frame_interval) {
    last_frame = elapsed;
    f32 opacity;
    memcpy(&opacity, &effect_value, sizeof(opacity));
    if (fade_direction) {
      f64 updated = f64(opacity) + (fade_direction > 0 ? 1.0 : -1.0) * f64(0.00033f) * f64(milliseconds);
      opacity = f32(updated);
      if (!(updated > 0.0 && updated < 1.0)) {
        opacity = fade_direction > 0 ? 1.0f : 0.0f;
        memcpy(&effect_value, &opacity, sizeof(opacity));
        if (fade_direction < 0) {
          RemoveFromAnimationSystem();
          return 0;
        }
        mode = 0;
      }
      memcpy(&effect_value, &opacity, sizeof(opacity));
    }
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 2, nullptr);
  }
  return 0;
}

void SpSpaceDebrisImpactDisplay::SetTimeToLive(u32 duration) {
  time_to_live = duration;
}

u8 SpSpaceDebrisImpactDisplay::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(color_base + u32(zoom), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(mask_base + u32(zoom), 0, &image);
  mask[zoom] = image->buffer;
  return 1;
}

// Release each held color and mask image through the live cache, clearing its pointer after the callback.
i32 SpSpaceDebrisImpactDisplay::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(color_base + u32(zoom), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(mask_base + u32(zoom), 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Construct the original fire sprite with an unselected size, invalid position and fifteen animation frames.
// Preserve the original span initialization before applying the requested size.
SpFireSprite::SpFireSprite(i32 input)
    : size(-1), variation(0), registered(0), byte_203(0), frame(0), frame_count(15), frame_interval(0),
      last_frame(0) {
  position.x = position.z = position.y = -1.0f;
  geom_flags |= kSpEffectBaseGeometry;
  SetSpanRects();
  SetSize(input);
}

SpFireSprite::~SpFireSprite() {
}

u8 SpFireSprite::DemandArt(i32 zoom, i32, bool) {
  u32 key = u32(i32(size) * 5 + zoom);
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(kSpFireColorImageBase + key, 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(kSpFireMaskImageBase + key, 0, &image);
  mask[zoom] = image->buffer;
  return 1;
}

// Release the color and mask records independently, clearing each pointer after its corresponding cache callback.
i32 SpFireSprite::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  u32 key = u32(i32(size) * 5 + zoom);
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(kSpFireColorImageBase + key, 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(kSpFireMaskImageBase + key, 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Resize existing artwork in zoom order and refresh a currently registered sprite after the last replacement.
u8 SpFireSprite::SetSize(i32 input) {
  if (input > 3)
    input = 3;
  i8 previous = size;
  if (previous == input)
    return 1;
  if (previous >= 0) {
    frame = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      if (color[zoom]) {
        size = previous;
        ReleaseArt(zoom, 0);
        size = i8(input);
        DemandArt(zoom, 0, true);
        i32 width = GetW(zoom, 0);
        origins[zoom].x = i32(u32(frame) * u32(width));
      }
    }
    if (registered) {
      i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 2, nullptr);
    }
  }
  size = i8(input);
  return 1;
}

// Populate each zoom's fire rectangle from the current image metadata when a size has been selected.
void SpFireSprite::SetSpanRects() {
  if (size >= 0) {
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      u32 key = kSpFireColorImageBase + u32(i32(size) * 5 + zoom);
      SpImage *image = SpEffects::images->GetImageInfo(key, 0);
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
    }
  }
}

// Each DLL must link the queue implementation in its own module so its original private instance count stays local.
SpFire::SpFire()
    : references(0), tick_time(0), enabled(1), registered(0), occupant(nullptr), level(0), cell_x(-1), cell_z(-1),
      cell_y(0), position{-1.0f, -1.0f, -1.0f} {
  for (u32 index = 0; index < 3; ++index)
    sprites[index] = nullptr;
}

SpFire::~SpFire() {
  ScRelease(&occupant);
  RemoveFromAnimationSystem();
  DestroySprites(0);
}

// Register the actual animation identity only when enabled and not already registered.
void SpFire::AddToAnimationSystem() {
  if (!registered && enabled) {
    registered = 1;
    SpEffects::tick_manager->AddTickableObject(static_cast<SpTickable *>(this));
  }
}

// Remove each live fire occupant, re-reading the module animation manager after every callback.
void SpFire::RemoveSprites() {
  if (SpEffects::animated) {
    for (u32 index = 0; index < 3; ++index) {
      SpFireSprite *sprite = sprites[index];
      if (sprite && SpEffects::animated) {
        i32 y = SpDebrisCoordinate(sprite->position.y), z = SpDebrisCoordinate(sprite->position.z);
        i32 x = SpDebrisCoordinate(sprite->position.x);
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite), x, z, y);
      }
    }
  }
}

// Unregister the tick identity before clearing its flag and removing the current fire occupants.
void SpFire::RemoveFromAnimationSystem() {
  if (registered) {
    if (SpEffects::tick_manager)
      SpEffects::tick_manager->RemoveTickableObject(static_cast<SpTickable *>(this));
    registered = 0;
    RemoveSprites();
  }
}

// Remove one active fire occupant, clear its membership flag and release the retained sprite.
void SpFire::DestroySprite(u32 index) {
  SpFireSprite *sprite = sprites[index];
  if (sprite) {
    if (enabled) {
      if (SpEffects::animated) {
        i32 y = SpDebrisCoordinate(sprite->position.y), z = SpDebrisCoordinate(sprite->position.z);
        i32 x = SpDebrisCoordinate(sprite->position.x);
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(sprite), x, z, y);
      }
      sprites[index]->registered = 0;
    }
    sprites[index]->Release();
    sprites[index] = nullptr;
  }
}

void SpFire::DestroySprites(u32 first) {
  for (u32 index = first; index < 3; ++index)
    if (sprites[index])
      DestroySprite(index);
}

// Construct or resize one retained fire sprite at the original fixed coordinates and publish its animation occupant.
void SpFire::CreateFireSprite(u32 index, u32 wanted_size, i32 x, i32 z, i32 y) {
  if (sprites[index]) {
    if (sprites[index]->size != i32(wanted_size))
      sprites[index]->SetSize(i32(wanted_size));
    return;
  }
  sprites[index] = new SpFireSprite(i32(wanted_size));
  sprites[index]->AddRef();
  sprites[index]->position.x = f32(x);
  sprites[index]->position.z = f32(z);
  sprites[index]->position.y = f32(y);
  position = sprites[index]->position;
  sprites[index]->last_frame = 0;
  sprites[index]->frame_interval = SpEffects::random.Bounded(kSpFireFrameIntervalJitter) + kSpFireMinimumFrameInterval;
  sprites[index]->mode = 2;
  if (enabled) {
    sprites[index]->registered = 1;
    SpFireSprite *sprite = sprites[index];
    i32 altitude = SpDebrisCoordinate(sprite->position.y), z_pos = SpDebrisCoordinate(sprite->position.z);
    i32 x_pos = SpDebrisCoordinate(sprite->position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(sprite), x_pos, z_pos, altitude, 2, 2, true);
  }
}

// Publish the fire controller or its adjusted animation-tick identity before retaining the shared object.
bool SpFire::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpFireSimulator);
  HANDLE_QUERY_INTERFACE(IID_SpFireSimulator, SpFireSimulator);
  HANDLE_QUERY_INTERFACE(IID_SpTickable, SpTickable);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpFire::AddRef() {
  return ++references;
}

// Destroy at a reference count of zero or one; larger counts decrement without another ownership transition.
u32 SpFire::Release() {
  if (references <= 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Apply an immediate fire level or queue the original deferred change. New occupant fire samples the real cell artwork
// under the city's graphics lock, retains the original random-draw order, and creates its animation sprite afterward.
u8 SpFire::SetLevel(i8 input, u8 deferred) {
  if (level == u8(input))
    return 1;
  if (deferred) {
    messages.PushBack(ScMessage(kSpMsg_SetFireLevel, u32(i32(input)), 0, 0));
    return 1;
  }
  level = u8(input);
  if (!input) {
    DestroySprites(0);
    return 1;
  }
  if (sprites[0]) {
    sprites[0]->SetSize(i32(input) - 1);
    return 1;
  }
  if (!occupant) {
    CreateFireSprite(0, u32(i32(input) - 1), i32((u32(cell_x) << kCtMicroCoordinateShift) + 128),
        i32((u32(cell_z) << kCtMicroCoordinateShift) + 128), i32(u32(cell_y) << kCtMicroCoordinateShift));
    return 1;
  }
  u8 major = occupant->MajorType();
  u32 cell_x_pos, cell_z_pos;
  occupant->GetLocationInCellCoords(&cell_x_pos, &cell_z_pos);
  SpEffects::city->LockGraphics();
  i32 grid_x = i32(cell_x_pos), grid_z = i32(cell_z_pos);
  SpEffects::cell_map->GetRegCellForSprite(grid_x, grid_z, &grid_x, &grid_z);
  u32 width, height;
  SpEffects::cell_map->GetPixelSizeOfSprite(grid_x, grid_z, &width, &height);
  i32 pixel_x, pixel_y;
  u32 attempts = 0;
  do {
    ++attempts;
    pixel_x = i32((width >> 3) + u32(SpEffects::random.RandomSint32RangeGaussian(0, i32((3u * width) >> 2))));
    pixel_y = i32(SpEffects::random.Bounded(height - 4u * u32(SpEffects::zoom)));
  } while (attempts < 32 &&
      !SpEffects::cell_map->IsCityPixelSolidForSprite(grid_x, grid_z, pixel_x, pixel_y, false));
  i32 fire_x, fire_z, fire_y;
  SpEffects::cell_map->SpritePixelToMicroActualGrid(grid_x, grid_z, u32(pixel_x), u32(pixel_y), &fire_x, &fire_z,
      &fire_y);
  SpEffects::city->UnlockGraphics();
  u32 flag = major == 1 || major == 3 ? 256 : major == 5 ? 1024 : major == 2 ? 0x4000 : major == 4 ? kOcGeomPipe : 0;
  if (flag)
    for (u32 index = 0; index < 3; ++index)
      if (sprites[index])
        sprites[index]->SetGeomFlag(flag);
  CreateFireSprite(0, u32(i32(input) - 1), fire_x, fire_z, fire_y);
  return 1;
}

// Retain the supplied occupant before replacing the previous one, sample its live position and request deferred level one.
// The original unused mode byte does not alter setup or the animation registration.
u8 SpFire::Init(ScOccupant *input, i8) {
  if (!SpEffects::animated)
    return 0;
  if (input) {
    input->AddRef();
    CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
    input->GetLocation(&point);
    position.x = f32(point.x);
    position.z = f32(point.z);
    position.y = f32(point.altitude);
  }
  if (occupant)
    occupant->Release();
  occupant = input;
  enabled = 1;
  SetLevel(1, 1);
  AddToAnimationSystem();
  return 1;
}

// Store the three original cell coordinates before the enabled gate and lookup, then delegate to occupant setup.
// Release the acquired occupant after the virtual initialization returns, including when initialization refuses.
u8 SpFire::Init(i32 x, i32 z, i32 y, i8 mode, u8 allow) {
  cell_x = x;
  cell_z = z;
  cell_y = y;
  if (!allow)
    return 0;
  ScOccupant *found;
  if (!SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &found))
    return 0;
  position.x = f32(i32(u32(cell_x) << kCtMicroCoordinateShift));
  position.z = f32(i32(u32(cell_z) << kCtMicroCoordinateShift));
  position.y = f32(i32(u32(cell_y) << kCtMicroCoordinateShift));
  u8 result = Init(found, mode);
  found->Release();
  return result;
}

u32 SpFire::ReleaseAsynchronous() {
  messages.PushBack(ScMessage(kSpMsg_ReleaseFire));
  return references - 1;
}

// Write the live fixed-coordinate outputs in X, Z, altitude order using the original double-store rounding.
void SpFire::GetPositionForExplosion(i32 *x, i32 *z, i32 *y) {
  *x = SpDebrisCoordinate(position.x);
  *z = SpDebrisCoordinate(position.z);
  *y = SpDebrisCoordinate(position.y);
}

// Drain deferred level and release messages before advancing each consecutive active fire sprite.
// A release message ends the tick immediately; sprite and image callbacks retain their original live ordering.
void SpFire::DoAnimationTick(u32 delta) {
  tick_time += delta;
  while (messages.GetMessageCount()) {
    ScMessage message;
    messages.GetFrontMessage(&message);
    if (message.type == kSpMsg_SetFireLevel)
      SetLevel(i8(message.data1), 0);
    else if (message.type == kSpMsg_ReleaseFire) {
      Release();
      return;
    }
  }
  for (u32 index = 0; index < 3; ++index) {
    SpFireSprite *sprite = sprites[index];
    if (!sprite)
      break;
    if (tick_time - sprite->last_frame > sprite->frame_interval) {
      sprite->last_frame = tick_time;
      ++sprite->frame;
      if (sprite->frame >= sprite->frame_count) {
        if (sprite->size == 0)
          sprite->SetSize(1);
        sprites[index]->frame = 0;
      }
      for (i32 zoom = 0; zoom < 5; ++zoom) {
        sprite = sprites[index];
        if (sprite->color[zoom]) {
          i32 width = sprite->GetW(zoom, 0);
          sprite->origins[zoom].x = i32(u32(sprite->frame) * u32(width));
        }
      }
      sprite = sprites[index];
      i32 y = SpDebrisCoordinate(sprite->position.y), z = SpDebrisCoordinate(sprite->position.z);
      i32 x = SpDebrisCoordinate(sprite->position.x);
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(sprite), x, z, y, 2, nullptr);
    }
  }
}

void SpFire::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 160;
  *interval = 4;
}

SpObjectShadow::~SpObjectShadow() {
}

SpTornadoDebris::SpTornadoDebris()
    : active(1), center{0.0f, 0.0f, 0.0f}, radius(0.0f), radial_speed(0.0f), target_height(0.0f), scale(1.05f),
      angle(0.0f), angular_speed(0.0f), vertical_speed(0.0f) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpTornadoDebris::~SpTornadoDebris() {
}

// Construct the whirlpool-debris sprite with no active occupancy, an unlimited lifetime and one animation frame.
SpWhirlpoolPiece::SpWhirlpoolPiece()
    : variant(0), image_base(0), registered(0), frame(0), frame_count(1),
      frame_interval(kSpWhirlpoolPieceFrameInterval), time_to_live(kSpDebrisNoLifetime), elapsed(0), last_frame(0),
      fade_direction(0), word_236(0) {
  geom_flags |= kSpWhirlpoolPieceGeometry;
}

SpWhirlpoolPiece::~SpWhirlpoolPiece() {
  RemoveFromAnimationSystem();
}

bool SpWhirlpoolPiece::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpWhirlpoolDebris, SpWhirlpoolDebris);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpWhirlpoolPiece::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpWhirlpoolPiece::Release() {
  return SpEffectOccupant::Release();
}

// Load the selected artwork's registration rectangle for each zoom, leaving missing metadata unchanged.
void SpWhirlpoolPiece::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(image_base + u32(zoom), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Publish the original occupant at its rounded position once, then reset its previous animation-frame time.
void SpWhirlpoolPiece::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 2, 0, true);
    last_frame = 0;
  }
}

// Clear the live registration flag before removing the occupant from an available animation manager.
void SpWhirlpoolPiece::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    if (SpEffects::animated) {
      i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y);
    }
  }
}

// Select the original debris artwork and frame count at a valid world position, then insert its animation occupant.
u8 SpWhirlpoolPiece::Init(i32 x, i32 z, i32 altitude, i32 input_variant) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  mode = 0;
  variant = input_variant;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  f32 opacity = 1.0f;
  memcpy(&effect_value, &opacity, sizeof(opacity));
  frame_count = input_variant == 3 || input_variant == 4 || input_variant == 5 ? 4 : 1;
  switch (input_variant) {
  case 0:
    image_base = kSpWhirlpoolVariant0ImageBase;
    break;
  case 1:
    image_base = kSpWhirlpoolVariant1ImageBase;
    break;
  case 2:
    image_base = kSpWhirlpoolVariant2ImageBase;
    break;
  case 3:
    image_base = kSpWhirlpoolVariant3ImageBase;
    break;
  case 4:
    image_base = kSpWhirlpoolVariant4ImageBase;
    break;
  case 5:
    image_base = kSpWhirlpoolVariant5ImageBase;
    break;
  case 6:
    image_base = kSpWhirlpoolVariant6ImageBase;
    break;
  case 7:
    image_base = kSpWhirlpoolVariant7ImageBase;
    break;
  default:
    image_base = kSpWhirlpoolDefaultImageBase;
    break;
  }
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Move valid debris to the supplied fixed coordinates, forwarding both old and new coordinates to the live manager.
u8 SpWhirlpoolPiece::ChangePosition(CtCoord *input) {
  i32 x = input->x, z = input->z, y = input->altitude;
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height))
    return 0;
  f32 old_x = position.x, old_y = position.y, old_z = position.z;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(y);
  return SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), SpDebrisCoordinate(old_x),
      SpDebrisCoordinate(old_z), SpDebrisCoordinate(old_y), x, z, y);
}

void SpWhirlpoolPiece::SetTimeBetweenFrames(u32 duration) {
  frame_interval = duration;
}

void SpWhirlpoolPiece::SetTimeToLive(u32 duration) {
  time_to_live = duration;
}

// Construct another whirlpool-debris controller at the original zero reference count.
SpWhirlpoolDebris *SpWhirlpoolPiece::MakeNewWhirlpoolDebris() {
  return static_cast<SpWhirlpoolDebris *>(new SpWhirlpoolPiece);
}

u8 SpWhirlpoolPiece::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(image_base + u32(zoom), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  return 1;
}

// Release the selected color demand through the available image cache and clear it after the callback.
i32 SpWhirlpoolPiece::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(image_base + u32(zoom), 0);
    color[zoom] = nullptr;
  }
  return 0;
}

void SpWhirlpoolPiece::SetFadeDirection(i32 direction) {
  fade_direction = direction;
  mode = direction ? 5 : 0;
}

// Advance lifetime, fading and any multi-frame artwork at the original strict animation deadline.
// Notify the live animation manager only when fading or frame progression changed the sprite.
i32 SpWhirlpoolPiece::Tick(u32 delta) {
  f32 milliseconds = f32(f64(delta) * 0.001);
  if (time_to_live < INT32_MAX) {
    time_to_live -= delta >> 10;
    if (time_to_live > INT32_MAX) {
      time_to_live = kSpDebrisNoLifetime;
      SetFadeDirection(-1);
    }
  }
  elapsed += delta;
  if (elapsed <= last_frame + frame_interval)
    return 0;
  last_frame = elapsed;
  bool changed = false;
  if (fade_direction) {
    f32 opacity;
    memcpy(&opacity, &effect_value, sizeof(opacity));
    f64 updated = f64(opacity) + (fade_direction > 0 ? 1.0 : -1.0) * f64(0.00033f) * milliseconds;
    opacity = f32(updated);
    if (!(updated > 0.0 && updated < 1.0)) {
      opacity = fade_direction > 0 ? 1.0f : 0.0f;
      memcpy(&effect_value, &opacity, sizeof(opacity));
      if (fade_direction < 0) {
        RemoveFromAnimationSystem();
        return 0;
      }
      mode = 0;
    }
    memcpy(&effect_value, &opacity, sizeof(opacity));
    changed = true;
  }
  if (frame_count > 1) {
    ++frame;
    if (frame >= frame_count)
      frame = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(frame) * u32(width));
    }
    changed = true;
  }
  if (changed) {
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 2, nullptr);
  }
  return 0;
}

// Start fading in with the active artwork queued next and no elapsed ticks or held occupants.
// Initialize movement and frame timing before the whirlpool is registered for animation.
SpWhirlpool::SpWhirlpool()
    : registered(0), notify(0), frame(0), frame_count(11), fade_frame_count(11), fade_step(1), variant(0), size_kind(0),
      state(kSpWhirlpoolFadeIn), next_state(kSpWhirlpoolActive), ticks(0), fastest_speed(0.0000025f),
      outer_distance(2400.0f), speed(0.0f),
      velocity{0.0f, 0.0f, 0.0f}, elapsed(0), last_frame(0), frame_interval(kSpWhirlpoolFrameInterval),
      artwork{nullptr, nullptr} {
  geom_flags |= kSpEffectBaseGeometry;
}

// Construct the registered whirlpool controller with the original zero initial occupant reference count.
ScUnknown *SpCreateWhirlpoolSimulator() {
  return static_cast<SpWhirlpoolSimulator *>(new SpWhirlpool);
}

// Remove active occupancy before releasing both owned artwork sprites and the abduction list.
SpWhirlpool::~SpWhirlpool() {
  RemoveFromAnimationSystem();
  for (u32 index = 0; index < 2; ++index)
    if (artwork[index]) {
      artwork[index]->Release();
      artwork[index] = nullptr;
    }
}

bool SpWhirlpool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpWhirlpoolSimulator, SpWhirlpoolSimulator);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpWhirlpool::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpWhirlpool::Release() {
  return SpEffectOccupant::Release();
}

void SpWhirlpool::AddToAnimationSystem() {
  if (!registered) {
    registered = 1;
    i32 y = SpDebrisCoordinate(position.y), z = SpDebrisCoordinate(position.z), x = SpDebrisCoordinate(position.x);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, 1, 0, true);
  }
}

// Clear abductions before removing the occupant, then release the controller if a completion notification was
// requested.
void SpWhirlpool::RemoveFromAnimationSystem() {
  if (registered) {
    registered = 0;
    ClearAbductionList();
    if (SpEffects::animated) {
      i32 z = SpDebrisCoordinate(position.z), y = SpDebrisCoordinate(position.y), x = SpDebrisCoordinate(position.x);
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y);
    }
    if (notify)
      Release();
  }
}

// Remove each abduction record under the shared flag, then restore or remove its occupant and release its identities.
void SpWhirlpool::ClearAbductionList() {
  SpAcquireEffectsFlag();
  while (!abducted.empty()) {
    AbductedObject item = abducted.front();
    abducted.pop_front();
    SpEffects::thread_safe_flag.store(0);
    if (item.opacity == 255) {
      if (SpEffects::animated)
        SpEffects::animated->ShowOccupant(item.occupant, true);
      if (item.debris)
        item.debris->SetTimeToLive(60000);
      else if (item.object_interface && SpEffects::animated)
        SpEffects::animated->RemoveOccupant(item.occupant);
    } else if (SpEffects::animated) {
      SpEffects::animated->RemoveOccupant(item.occupant);
    }
    item.occupant->Release();
    if (item.object_interface)
      item.object_interface->Release();
    if (item.debris)
      item.debris->Release();
    SpAcquireEffectsFlag();
  }
  SpEffects::thread_safe_flag.store(0);
}

// Position the whirlpool above terrain, create its two retained artwork sprites and register its animation occupant.
u8 SpWhirlpool::Init(const ScVec3F &input, i32, i8 input_mode, f32 input_speed) {
  position = input;
  velocity.x = f32(f64(input_speed) * 0.5);
  velocity.z = f32(f64(input_speed) * 0.125);
  variant = 0;
  size_kind = input_mode;
  mode = 2;
  speed = input_speed;
  position.y = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(position.x, position.z) + 1536);
  if (u32(SpDebrisCoordinate(position.x)) >= u32(SpEffects::micro_width) ||
      u32(SpDebrisCoordinate(position.z)) >= u32(SpEffects::micro_height)) {
    position.z = 0.0f;
    position.x = 0.0f;
  }
  for (u32 index = 0; index < 2; ++index) {
    artwork[index] = new SpBasicSprite;
    artwork[index]->AddRef();
  }
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

void SpWhirlpool::SetMicrosecondsBetweenFrames(u32 duration) {
  frame_interval = duration;
}

void SpWhirlpool::SetObjectFastestSpeedTimes100000000(u32 input_speed) {
  fastest_speed = f32(f64(input_speed) * f64(0.00000001f));
}

void SpWhirlpool::SetOuterGravityDistance(u32 distance) {
  outer_distance = f32(distance);
}

i32 SpWhirlpool::GetWhirlpoolDiameterAnimUnits() {
  return 2304;
}

void SpWhirlpool::GetLocation(i32 *x, i32 *z) {
  *x = SpDebrisCoordinate(position.x);
  *z = SpDebrisCoordinate(position.z);
}

// Set the notification byte and, when requested, queue or start the original fade-out under the shared effects flag.
void SpWhirlpool::DoFade(u8 visible, u8 should_notify) {
  SpAcquireEffectsFlag();
  notify = should_notify;
  if (!visible && state != kSpWhirlpoolFadeOut) {
    if (state != kSpWhirlpoolInactive)
      next_state = kSpWhirlpoolFadeOut;
    else {
      next_state = kSpWhirlpoolInactive;
      state = kSpWhirlpoolFadeOut;
      fade_step = -1;
      frame = u8(fade_frame_count - 1);
    }
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(frame) * u32(width));
    }
  }
  SpEffects::thread_safe_flag.store(0);
}

// Require the original nine-by-nine cell footprint to remain inside the square map and entirely on water.
u8 SpWhirlpool::WouldWhirlpoolFitInLocation(ScVec3F *input) {
  i32 first_x = i32(u32(SpDebrisCoordinate(input->x)) - 1152u) / 256;
  i32 first_z = i32(u32(SpDebrisCoordinate(input->z)) - 1152u) / 256;
  i32 limit = SpEffects::micro_width / 256;
  for (i32 x = first_x; x < first_x + 9; ++x)
    for (i32 z = first_z; z < first_z + 9; ++z)
      if (x < 0 || x >= limit || z < 0 || z >= limit || !SpEffects::dirt->IsWater(u32(x), u32(z)))
        return 0;
  return 1;
}

bool SpWhirlpool::GetNewVelocityForWhirlpool(const ScVec3F &input_position, const ScVec3F &input_velocity,
    ScVec3F *out_velocity, f32 input_speed, f32 distance) {
  f64 initial = SpEffects::AngleOf2DVector(input_velocity);
  f32 end = f32(initial + f64(6.2831855f));
  *out_velocity = input_velocity;
  f64 candidate = f64(6.2831855f) * f64(0.071428575f) + initial;
  f32 saved = f32(candidate);
  if (!(candidate < end))
    return false;
  do {
    f64 angle = candidate;
    out_velocity->x = f32(std::cos(angle) * input_speed);
    f64 new_z = std::sin(angle) * input_speed;
    out_velocity->z = f32(new_z);
    ScVec3F location = {f32(f64(distance) * out_velocity->x + input_position.x), 0.0f,
        f32(new_z * distance + input_position.z)};
    if (WouldWhirlpoolFitInLocation(&location))
      return saved < end;
    candidate = f64(6.2831855f) * f64(0.071428575f) + saved;
    saved = f32(candidate);
  } while (candidate < end);
  return false;
}

// Accept waterborne vehicles with either original geometry flag or an identified whirlpool-debris view.
u8 SpWhirlpool::CanOccupantBeAbducted(ScOccupant *occupant) {
  if (!occupant)
    return 0;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  if (!SpEffects::dirt->IsWater(x, z))
    return 0;
  void *view;
  if (occupant->QueryInterface(IID_NwVehicle, &view)) {
    NwVehicle *object_interface = static_cast<NwVehicle *>(view);
    u8 accepted = occupant->IsGeomFlag(kSpWhirlpoolWaterVehicleMask);
    if (!accepted)
      accepted = occupant->IsGeomFlag(kSpWhirlpoolAirVehicleMask);
    object_interface->Release();
    return accepted;
  }
  if (occupant->QueryInterface(IID_SpWhirlpoolDebris, &view)) {
    SpWhirlpoolDebris *debris = static_cast<SpWhirlpoolDebris *>(view);
    debris->Release();
    return 1;
  }
  return 0;
}

// Collect up to 128 occupants in the clamped gravity volume and offer eligible waterborne occupants for abduction.
// Every returned occupant is released after its eligibility and insertion callbacks, including rejected occupants.
u8 SpWhirlpool::GetAndAddMiscellaneousObjectsToAbductionList() {
  i32 x = i32(position.x), z = i32(position.z);
  i32 minimum_x = i32(u32(i64(f64(x) - outer_distance)));
  i32 minimum_z = i32(u32(i64(f64(z) - outer_distance)));
  i32 maximum_x = i32(u32(i64(f64(x) + outer_distance)));
  i32 maximum_z = i32(u32(i64(f64(z) + outer_distance)));
  GeBounds bounds({minimum_x, minimum_z, 0}, {maximum_x, maximum_z, 0x10000});
  if (bounds.minimum.x < 0)
    bounds.minimum.x = 0;
  if (bounds.minimum.z < 0)
    bounds.minimum.z = 0;
  if (bounds.maximum.x >= SpEffects::micro_width)
    bounds.maximum.x = i32(u32(SpEffects::micro_width) - 1);
  if (bounds.maximum.z >= SpEffects::micro_height)
    bounds.maximum.z = i32(u32(SpEffects::micro_height) - 1);
  ScOccupant *occupants[128];
  u32 count = 128;
  if (!SpEffects::animated->GetOccupantsInVolume(bounds, occupants, &count))
    return 0;
  for (u32 index = 0; index < count; ++index) {
    ScOccupant *occupant = occupants[index];
    if (CanOccupantBeAbducted(occupant))
      AddToAbductionList(occupant);
    occupant->Release();
  }
  return 1;
}

// Add a new held occupant and its original vehicle or debris identities, then suspend its normal visibility and wake.
u8 SpWhirlpool::AddToAbductionList(ScOccupant *occupant) {
  SpAcquireEffectsFlag();
  for (const AbductedObject &item : abducted)
    if (item.occupant == occupant) {
      SpEffects::thread_safe_flag.store(0);
      return 0;
    }
  AbductedObject item = {occupant, nullptr, nullptr, 255, 3, 0};
  void *view;
  if (occupant->QueryInterface(IID_NwVehicle, &view))
    item.object_interface = static_cast<NwVehicle *>(view);
  item.flags = occupant->GetGeomFlag();
  if (occupant->QueryInterface(IID_SpWhirlpoolDebris, &view)) {
    item.debris = static_cast<SpWhirlpoolDebris *>(view);
    item.debris->SetTimeToLive(kSpWhirlpoolAbductedDebrisLifetime);
  }
  abducted.push_back(item);
  SpEffects::thread_safe_flag.store(0);
  occupant->AddRef();
  occupant->ClearGeomFlag(kOcAllGeometryFlags);
  occupant->SetGeomFlag(kSpAbductedOccupantGeometry);
  SpEffects::animated->InsertOccupant(occupant, 1, 0, true);
  SpEffects::animated->ShowOccupant(occupant, false);
  if (occupant->QueryInterface(IID_NwBoat, &view)) {
    NwBoat *boat = static_cast<NwBoat *>(view);
    boat->EnableWake(false);
    boat->Release();
  } else if (occupant->QueryInterface(IID_NwShip, &view)) {
    NwShip *ship = static_cast<NwShip *>(view);
    ship->EnableWake(false);
    ship->Release();
  }
  return 1;
}

// Search the original 1200-unit radius in cell order and post one bridge-destruction message for a waterborne bridge.
void SpWhirlpool::DestroyAnyLocalBridges() {
  i32 first_x = i32(i64(f64(position.x) - 1200.0)) / kCtMicroUnitsPerCell;
  i32 first_z = i32(i64(f64(position.z) - 1200.0)) / kCtMicroUnitsPerCell;
  i32 last_x = i32(i64(f64(position.x) + 1200.0)) / kCtMicroUnitsPerCell;
  i32 last_z = i32(i64(f64(position.z) + 1200.0)) / kCtMicroUnitsPerCell;
  for (i32 x = first_x; x < last_x; ++x) {
    for (i32 z = first_z; z < last_z; ++z) {
      ScOccupant *occupant = nullptr;
      SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant);
      if (occupant) {
        bool destroy = occupant->MajorType() == 2 && SpEffects::dirt->IsWater(u32(x), u32(z));
        if (destroy && SpEffects::messages)
          SpEffects::messages->MessagePost(ScMessage(kSpMsg_WhirlpoolPathChanged, uptr(this), u32(x), u32(z)), false);
        occupant->Release();
        if (destroy)
          return;
      }
    }
  }
}

// Create and initialize the original water splash at an abducted object's last position, then release the temporary hold.
void SpWhirlpool::DoDisappearanceSplash(AbductedObject *object, ScVec3F *input) {
  i32 x = SpDebrisCoordinate(input->x), z = SpDebrisCoordinate(input->z), y = SpDebrisCoordinate(input->y);
  i32 color_key = object->object_interface ? kSpWhirlpoolOccupantColorImageBase : kSpWhirlpoolDebrisColorImageBase;
  i32 mask_key = object->object_interface ? kSpWhirlpoolOccupantMaskImageBase : kSpWhirlpoolDebrisMaskImageBase;
  i8 frames = object->object_interface ? 10 : 9;
  SpSplash *splash = new SpSplash;
  splash->AddRef();
  splash->Init(x, z, y, color_key, mask_key, frames);
  splash->Release();
}

// Pull held vehicles and debris toward the vortex, rotate and fade them, and release objects that disappear or leave
// bounds.
void SpWhirlpool::SimulateAbductedOccupants(u32 delta) {
  f32 micros = f32(delta), milliseconds = f32(f64(delta) * 0.001);
  auto iterator = abducted.begin();
  while (iterator != abducted.end()) {
    AbductedObject &item = *iterator;
    if (!item.occupant || (!item.object_interface && !item.debris)) {
      ++iterator;
      continue;
    }
    u32 x, z;
    item.occupant->GetLocationInAnimCellCoords(&x, &z);
    u16 altitude = item.occupant->GetAltAnimCells();
    ScVec3F current = {f32(x), f32(altitude), f32(z)};
    ScPointXZSigned center_point = {i32(x), i32(z)};
    if (item.object_interface)
      item.object_interface->GetCenterOfMass(&center_point);
    ScVec3F center = {f32(center_point.x), f32(item.occupant->GetAltAnimCells()), f32(center_point.z)};
    ScVec3F vortex = position;
    vortex.x = f32(f64(current.x) - center.x + vortex.x);
    vortex.z = f32(f64(current.z) - center.z + vortex.z);
    ScVec3F toward = {vortex.x - current.x, vortex.y - current.y, vortex.z - current.z};
    f64 measured_distance = SpEffects::LengthOf3DVector(toward);
    f32 old_distance = f32(measured_distance);
    if (measured_distance < 80.0 || item.phase == 0) {
      SpSprite *sprite = item.occupant->GetSpriteInst();
      u32 fade = u32(i64(f64(0.0853f) * milliseconds));
      item.opacity -= fade;
      if (!item.opacity || item.opacity > 255) {
        if (SpEffects::messages)
          SpEffects::messages->MessagePost(
              ScMessage(kSpMsg_WhirlpoolOccupantConsumed, uptr(this), uptr(item.occupant), 0), false);
        AbductedObject saved = item;
        iterator = abducted.erase(iterator);
        SpEffects::animated->RemoveOccupant(saved.occupant);
        saved.occupant->Release();
        if (saved.object_interface)
          saved.object_interface->Release();
        if (saved.debris)
          saved.debris->Release();
        DoDisappearanceSplash(&saved, &center);
        continue;
      }
      f64 angle = SpEffects::AngleOf2DVector(toward) + f64(micros) * fastest_speed;
      current.x = f32(std::cos(angle) * old_distance + vortex.x);
      current.z = f32(std::sin(angle) * old_distance + vortex.z);
      CtCoord next = {i32(current.x), i32(current.z), i32(current.y)};
      if (item.object_interface)
        item.object_interface->ChangePosition(&next);
      else
        item.debris->ChangePosition(&next);
      sprite->SetEffect(item.opacity | kSpDrawOpacity);
    } else {
      if (measured_distance < 800.0 || item.phase == 1) {
        ScVec3F initial_toward = toward;
        f64 inward = f64(0.06f) * milliseconds;
        if (inward < 2.5)
          inward = 2.5;
        SpEffects::Scale3DVector(&toward, f32(inward));
        current.x += toward.x;
        current.z += toward.z;
        current.y += toward.y;
        ScVec3F radial = {vortex.x - current.x, vortex.y - current.y, vortex.z - current.z};
        f64 measured_radius = SpEffects::LengthOf3DVector(radial);
        f32 radius = f32(measured_radius);
        f64 ratio = 1.0 - (measured_radius - 80.0f) / 720.0f;
        if (ratio < f64(0.05f))
          ratio = f64(0.05f);
        if (ratio > 1.0)
          ratio = 1.0;
        f32 angular_speed = f32((f64(fastest_speed) - f64(0.0000004f)) * ratio + f64(0.0000004f));
        f64 angle = SpEffects::AngleOf2DVector(initial_toward) + f64(angular_speed) * micros;
        current.x = f32(std::cos(angle) * radius + vortex.x);
        current.z = f32(std::sin(angle) * radius + vortex.z);
        radial = {vortex.x - current.x, vortex.y - current.y, vortex.z - current.z};
        f64 distance = SpEffects::LengthOf3DVector(radial);
        f64 difference = f64(old_distance) - distance;
        if (difference <= 0.0) {
          SpEffects::Scale3DVector(&radial, f32(1.0 - difference));
          current.x += radial.x;
          current.z += radial.z;
          current.y += radial.y;
        }
        if (u32(SpDebrisCoordinate(current.x)) >= u32(SpEffects::micro_width) ||
            u32(SpDebrisCoordinate(current.z)) >= u32(SpEffects::micro_height)) {
          AbductedObject saved = item;
          iterator = abducted.erase(iterator);
          SpEffects::animated->RemoveOccupant(saved.occupant);
          saved.occupant->Release();
          if (saved.object_interface)
            saved.object_interface->Release();
          if (saved.debris)
            saved.debris->Release();
          continue;
        }
      } else {
        f64 inward = f64(0.12f) * milliseconds;
        if (inward < 1.0)
          inward = 1.0;
        SpEffects::Scale3DVector(&toward, f32(inward));
        current.x += toward.x;
        current.z += toward.z;
        current.y += toward.y;
      }
      CtCoord next = {i32(current.x), i32(current.z), i32(current.y)};
      if (item.object_interface)
        item.object_interface->ChangePosition(&next);
      else
        item.debris->ChangePosition(&next);
    }
    ++iterator;
  }
}

void SpWhirlpool::SimulateRotatingDebris(u32) {
}

// Select the installed whirlpool artwork key, including its shifted low zooms and five-image variant stride.
// Retail's two scale configuration bytes are initialized to zero and one respectively and have no writers.
u32 SpWhirlpool::GetSpriteKey(i32 zoom, i32 variant_index, u32 base) {
  if (zoom < 4)
    ++zoom;
  return base + u32(zoom) + u32(variant_index) * 5u;
}

// Load available registration rectangles and double the largest zoom, retaining existing data for missing images.
void SpWhirlpool::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, variant, kSpWhirlpoolPrimaryColorImageBase), 0);
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

// Prepare both color and mask pairs, then expose the pair selected by the original size mode.
u8 SpWhirlpool::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  for (i32 index = 0; index < 2; ++index) {
    u32 color_base = index ? kSpWhirlpoolSecondaryColorImageBase : kSpWhirlpoolPrimaryColorImageBase;
    u32 mask_base = index ? kSpWhirlpoolSecondaryMaskImageBase : kSpWhirlpoolPrimaryMaskImageBase;
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, color_base), 0, &image);
    artwork[index]->color[zoom] = image->buffer;
    artwork[index]->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (zoom >= 4) {
      artwork[index]->registration[zoom].left *= 2;
      artwork[index]->registration[zoom].top *= 2;
      artwork[index]->registration[zoom].right *= 2;
      artwork[index]->registration[zoom].bottom *= 2;
      artwork[index]->shifts[zoom] = 1;
    }
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, mask_base), 0, &image);
    artwork[index]->mask[zoom] = image->buffer;
  }
  if (size_kind == 1 || size_kind == 3 || size_kind == 2) {
    SpBasicSprite *selected = artwork[size_kind == 2 ? 1 : 0];
    color[zoom] = selected->color[zoom];
    mask[zoom] = selected->mask[zoom];
    registration[zoom] = selected->registration[zoom];
  }
  i32 width = GetW(zoom, 0);
  origins[zoom].x = i32(u32(frame) * u32(width));
  return 1;
}

// Release each prepared artwork demand in original order and clear the controller's borrowed buffers.
i32 SpWhirlpool::ReleaseArt(i32 zoom, i32) {
  if (SpEffects::images) {
    for (i32 index = 0; index < 2; ++index) {
      u32 color_base = index ? kSpWhirlpoolSecondaryColorImageBase : kSpWhirlpoolPrimaryColorImageBase;
      u32 mask_base = index ? kSpWhirlpoolSecondaryMaskImageBase : kSpWhirlpoolPrimaryMaskImageBase;
      if (artwork[index]->color[zoom]) {
        SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, color_base), 0);
        artwork[index]->color[zoom] = nullptr;
      }
      if (artwork[index]->mask[zoom]) {
        SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, mask_base), 0);
        artwork[index]->mask[zoom] = nullptr;
      }
    }
    color[zoom] = nullptr;
    mask[zoom] = nullptr;
  }
  return 0;
}

// Advance animation, movement and the wrapping tick schedules before updating retained occupants.
// The abduction simulation excludes the original node's post-free reads.
i32 SpWhirlpool::Tick(u32 delta) {
  if (state == kSpWhirlpoolInactive)
    return 0;
  ++ticks;
  elapsed += delta;
  bool updated = false;
  if (elapsed > last_frame + frame_interval) {
    SpAcquireEffectsFlag();
    last_frame = elapsed;
    frame = u8(frame + fade_step);
    updated = true;
    if (frame >= fade_frame_count) {
      frame = 0;
      fade_step = 1;
      if (next_state != state) {
        if (next_state == kSpWhirlpoolFadeOut) {
          frame = u8(fade_frame_count - 1);
          fade_step = -1;
        } else if (next_state == kSpWhirlpoolInactive) {
          SpEffects::thread_safe_flag.store(0);
          if (SpEffects::messages)
            SpEffects::messages->MessagePost(ScMessage(kSpMsg_WhirlpoolEnded, uptr(this), uptr(&position), 0), false);
          RemoveFromAnimationSystem();
          return 0;
        }
        state = next_state;
        if (state == kSpWhirlpoolFadeIn || state == kSpWhirlpoolActive || state == kSpWhirlpoolFadeOut) {
          SpBasicSprite *selected = artwork[state == kSpWhirlpoolActive ? 1 : 0];
          for (i32 zoom = 0; zoom < 5; ++zoom) {
            color[zoom] = selected->color[zoom];
            mask[zoom] = selected->mask[zoom];
            registration[zoom] = selected->registration[zoom];
          }
        }
      }
      if (next_state == kSpWhirlpoolFadeIn)
        next_state = kSpWhirlpoolActive;
      else if (next_state == kSpWhirlpoolFadeOut)
        next_state = kSpWhirlpoolInactive;
    }
    for (i32 zoom = 0; zoom < 5; ++zoom) {
      i32 width = GetW(zoom, 0);
      origins[zoom].x = i32(u32(frame) * u32(width));
    }
    SpEffects::thread_safe_flag.store(0);
  }
  f32 milliseconds = f32(f64(delta) * 0.001);
  ScVec3F previous = position;
  if (speed != 0.0f) {
    position.x = f32(f64(milliseconds) * velocity.x + position.x);
    position.z = f32(f64(milliseconds) * velocity.z + position.z);
    if (!WouldWhirlpoolFitInLocation(&position)) {
      position = previous;
      GetNewVelocityForWhirlpool(position, velocity, &velocity, speed, milliseconds);
    }
  }
  if (u8(ticks) == kSpWhirlpoolBridgeTickPhase && SpEffects::messages)
    SpEffects::messages->MessagePost(
        ScMessage(kSpMsg_WhirlpoolCellChanged, uptr(this), u32(SpDebrisCoordinate(position.x) / kCtMicroUnitsPerCell),
            u32(SpDebrisCoordinate(position.z) / kCtMicroUnitsPerCell)),
        false);
  if (previous.x != position.x || previous.z != position.z) {
    i32 x = SpDebrisCoordinate(position.x), z = SpDebrisCoordinate(position.z);
    i32 y = SpDebrisCoordinate(position.y);
    if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height)) {
      position = previous;
      if (SpEffects::messages)
        SpEffects::messages->MessagePost(
            ScMessage(kSpMsg_WhirlpoolEnded, uptr(this), u32(x / 256), u32(z / 256)), false);
      RemoveFromAnimationSystem();
      return 0;
    }
    if ((ticks & kSpWhirlpoolMovementNotifyMask) == 0 && ticks > kSpWhirlpoolLastWarmupTick && SpEffects::messages)
      SpEffects::messages->MessagePost(
          ScMessage(kSpMsg_WhirlpoolCellChanged, uptr(this), u32(x / 256), u32(z / 256)), false);
    if ((ticks & kSpWhirlpoolTerrainSampleMask) == 0) {
      u8 altitude = 0;
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
      i32 terrain_y = i32((u32(altitude) + 6) << kCtMicroCoordinateShift);
      if (y != terrain_y) {
        position.y = f32(terrain_y);
        y = SpDebrisCoordinate(position.y);
      }
    }
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), SpDebrisCoordinate(previous.x),
        SpDebrisCoordinate(previous.z), SpDebrisCoordinate(previous.y), x, z, y);
  } else if (updated) {
    SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), 2, nullptr);
  }
  if (u8(ticks) == kSpWhirlpoolAbductionTickPhase && ticks > kSpWhirlpoolLastWarmupTick)
    GetAndAddMiscellaneousObjectsToAbductionList();
  if (u8(ticks) == kSpWhirlpoolBridgeTickPhase && ticks > kSpWhirlpoolLastWarmupTick)
    DestroyAnyLocalBridges();
  SimulateAbductedOccupants(delta);
  SimulateRotatingDebris(delta);
  return 0;
}
