#include "disasters/ds_track.h"

#include <stdlib.h>
#include <string.h>

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "sprites/sp_effect.h"
#include "terrain/api.h"
#include "terrain/tr_dirt_bag.h"

// Impact displays and their debris retain separate configurable lifetime defaults.
enum DsImpactTimingDefault : u32 {
  kDsImpactDefaultAverageDebrisTime = 0xb71b0,
  kDsImpactDefaultDisplayLifetime = 0x493e0,
};

// Earthquake severity selects destruction/ignition chance thresholds and the peak shake offset for phases three and five.
enum DsEarthquakeScatterShakeProperty : u32 {
  kDsTrackDestructionChance0PropertyId = 0x64237541,
  kDsTrackDestructionChance1PropertyId = 0x44237548,
  kDsTrackDestructionChance2PropertyId = 0x4423754e,
  kDsTrackIgnitionChance0PropertyId = 0x44237555,
  kDsTrackIgnitionChance1PropertyId = 0x6423755e,
  kDsTrackIgnitionChance2PropertyId = 0xa4237564,
  kDsTrackPhaseFivePeakShake0PropertyId = 0x442375af,
  kDsTrackPhaseFivePeakShake1PropertyId = 0x842375be,
  kDsTrackPhaseFivePeakShake2PropertyId = 0xc42375c5,
  kDsTrackPhaseThreePeakShake0PropertyId = 0x842375e4,
  kDsTrackPhaseThreePeakShake1PropertyId = 0xe42375e9,
  kDsTrackPhaseThreePeakShake2PropertyId = 0x842375ef,
};

// Tuning keys retain the roles of the named fields read from the variable manager.
enum DsImpactTrackPropertyId : u32 {
  kDsImpactMaximumActivePropertyId = 0x44860901,
  kDsImpactMinimumMonthsPropertyId = 0x44860902,
  kDsImpactMaximumMonthsPropertyId = 0x44860903,
  kDsImpactMinimumStrengthPropertyId = 0x44860904,
  kDsImpactMaximumStrengthPropertyId = 0x44860905,
  kDsImpactChancePropertyId = 0x44860906,
  kDsImpactFirstDelayPropertyId = 0x4486090e,
  kDsImpactSecondDelayPropertyId = 0x4486090f,
  kDsImpactThirdDelayPropertyId = 0x44860910,
  kDsImpactAttackDelayPropertyId = 0x44860911,
  kDsImpactEndDelayPropertyId = 0x44860912,
  kDsImpactAverageDebrisTimePropertyId = 0x44860913,
  kDsImpactParticleChancePropertyId = 0x44860914,
  kDsImpactDamageChancePropertyId = 0x44860915,
  kDsImpactDisplayLifetimePropertyId = 0x44860916,
  kDsImpactMaximumCornerDifferencePropertyId = 0x44860917,
  kDsImpactMinimumYearPropertyId = 0x44860918,
  kDsTrackMaximumActivePropertyId = 0xe423750f,
  kDsTrackMinimumMonthsPropertyId = 0xe4237518,
  kDsTrackMaximumMonthsPropertyId = 0x44237523,
  kDsTrackEarthquakePreparednessPercentagePropertyId = 0x84237585,
  kDsTrackLowIntensityGashWidthPropertyId = 0x2423760e,
  kDsTrackMediumIntensityGashWidthPropertyId = 0x64237616,
  kDsTrackHighIntensityGashWidthPropertyId = 0x8423761c,
  kDsTrackMinimumStrengthPropertyId = 0x423752b,
  kDsTrackMaximumStrengthPropertyId = 0x64237533,
  kDsTrackChancePropertyId = 0x6423753a,
  kDsTrackBorderRadii0PropertyId = 0x2423756a,
  kDsTrackBorderRadii1PropertyId = 0x24237579,
  kDsTrackBorderRadii2PropertyId = 0x6423757f,
  kDsTrackPhaseFiveDuration0PropertyId = 0xa423758d,
  kDsTrackPhaseFiveDuration1PropertyId = 0x24237593,
  kDsTrackPhaseFiveDuration2PropertyId = 0x442375aa,
  kDsTrackPhaseThreeDuration0PropertyId = 0xe42375ca,
  kDsTrackPhaseThreeDuration1PropertyId = 0x442375d3,
  kDsTrackPhaseThreeDuration2PropertyId = 0xc42375d9,
  kDsTrackPhaseDurations0PropertyId = 0x442375f6,
  kDsTrackPhaseDurations1PropertyId = 0x842375fb,
  kDsTrackPhaseDurations2PropertyId = 0xc4237602,
  kDsTrackPhaseDurations3PropertyId = 0x4237608,
  kDsTrackOuterDeltas0PropertyId = 0xc4237621,
  kDsTrackOuterDeltas1PropertyId = 0x64237626,
  kDsTrackOuterDeltas2PropertyId = 0x6423762b,
  kDsTrackInnerDeltas0PropertyId = 0x24295c2c,
  kDsTrackInnerDeltas1PropertyId = 0x44237637,
  kDsTrackInnerDeltas2PropertyId = 0x4423763d,
};

enum DsTrackArchiveKey : u32 {
  kDsImpactHistoryRecordGroup = 0x24889f78,
  kDsImpactInstancesRecordGroup = 0x24889f79,
  kDsTrackHistoryRecordGroup = 0xc336f77c,
  kDsTrackInstancesRecordGroup = 0xe300c976,
};

// Sample terrain before the retained occupant lookup and emit fifteen particles only for eligible geometry.
// Preserve the factory result independently of ignored emission results, releasing the effect before the occupant.
u8 DsImpactInstance::EmitTileParticles(i32 x, i32 z, f32) {
  u8 altitude, result = 0;
  u32 cell_x = u32(x / 256), cell_z = u32(z / 256);
  manager->terrain->GetVertexAltitude(cell_x, cell_z, &altitude);
  ScOccupant *occupant;
  if (manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
    if (occupant->GetGeomFlag() & kOcGeomPower) {
      void *created;
      result = DsGetCom()->GetClassObject(kSpParticleEffectClassId, IID_SpParticleEffect, &created);
      if (result) {
        auto *particle = static_cast<SpParticleEffect *>(created);
        particle->Init();
        particle->DoParticleGeneration(x, z, i32(altitude) << kCtMicroCoordinateShift, 1, 15);
        particle->Release();
      }
    }
    occupant->Release();
  }
  return result;
}

// Retain the occupant at the cell and pass it to the controller's borrowed fire manager without a new manager hold.
// Count a nonzero ignition result before releasing the occupant; failed lookup or ignition leaves the total unchanged.
u8 DsImpactInstance::IgniteAtTile(i32 x, i32 z) {
  ScOccupant *occupant;
  u8 result = 0;
  if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
    auto *fire = static_cast<DsFireManager *>(manager->layer->GetDisasterManager(kDsFireTypeId));
    if (fire->IgniteOccupant(occupant, 0)) {
      ++record.destroyed;
      result = 1;
    }
    occupant->Release();
  }
  return result;
}

void DsImpactManager::RecordInstance(DsImpactInstance *instance) {
  *history.Append() = instance->record;
}

// Capture the final record before shutdown, then notify the current controller of removal.
// Shutdown callbacks observe the appended history and complete before the controller's removal callback.
void DsImpactManager::FinishInstance(DsImpactInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

u32 DsImpactParticleChance = 30, DsImpactDamageChance = 80;

// Establish the three instance identities and original constructor-written fields. City dimensions and padding
// remain unwritten; location and the five transient words begin at the original all-ones sentinel.
DsImpactInstance::DsImpactInstance()
    : initialized(false), word_16(0), word_20(0), word_24(0), word_28(0), word_32(0), word_36(0), references(0),
      announcement(false), location_x(kDsInvalidCellCoordinate), location_z(kDsInvalidCellCoordinate),
      effects{kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId,
          kApInvalidNotificationId},
      severity(1), word_80(0), word_84(0), manager(nullptr), effect(nullptr), record{} {
}

DsImpactInstance::~DsImpactInstance() {
  Shutdown();
}

// Publish the common or private state identity and acquire one reference on success. Unsupported IDs clear the output;
// the message-target identity is used directly rather than advertised through this query.
bool DsImpactInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsImpactState, DsImpactState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsImpactInstance::AddRef() {
  return ++references;
}

// Delete at zero or one reference without changing that final stored count before destruction. Larger counts
// decrement and return directly; teardown therefore observes one when the final held reference is released.
u32 DsImpactInstance::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Report the initialized instance's full city rectangle. An inactive instance writes zeros in the original order
// without reading the constructor-unwritten dimensions.
bool DsImpactInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    output->left = 0;
    output->top = 0;
    output->right = columns - 1;
    output->bottom = rows - 1;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

// Copy both location words even before the phase makes them available. The unsigned phase gate affects only
// the Boolean result; aliased outputs retain the second coordinate.
bool DsImpactInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = location_x;
  *output_z = location_z;
  return record.phase >= 5;
}

// Return the shared null occupant result. This instance family has no single retained building occupant.
ScOccupant *DsImpactInstance::GetOccupant() {
  return nullptr;
}

// Borrow the owner's common manager identity, preserving null before the original base adjustment.
DsManager *DsImpactInstance::GetManager() {
  return manager;
}

u32 DsImpactInstance::TypeId() {
  return kDsImpactTypeId;
}

u32 DsImpactInstance::GetDestroyedCount() {
  return record.destroyed;
}

u32 DsImpactInstance::GetCost() {
  return record.cost;
}

u8 DsImpactInstance::GetSeverity() const {
  return severity;
}

void DsImpactInstance::GetRecord(DsImpactRecord *output) {
  *output = record;
}

// End one counted instance while preventing zero underflow. Clear activity only when the resulting count is zero.
void DsImpactManager::InstanceEnded() {
  if (!active_count)
    active_count = 1;
  if (active_count-- == 1)
    active = false;
}

// Mark inactive before changing the owner count, release the owned effect, then end any announcement and remove
// the three subscriptions through separate live service lookups. Clear the owner only after all callbacks finish.
u8 DsImpactInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    manager->InstanceEnded();
    if (effect) {
      static_cast<ScUnknown *>(effect)->Release();
      effect = nullptr;
    }
    if (announcement) {
      manager->box->Event(kAuEventEffectSlot52, kAuEffectDisasterSpaceJunk, 0, 0, 0);
      announcement = false;
    }
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_BuildingImpact);
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WaterImpact);
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_GroundImpact);
    manager = nullptr;
  }
  return 1;
}

// Form the size-dependent rectangle around signed fixed coordinates. Preserve the original right-edge test and
// clipping against both dimensions; the bottom edge is deliberately left unchecked and unclipped.
u8 DsImpactInstance::ComputePatternBounds(i32 x, i32 z, f32 size, ScRect *bounds, u8 *clipped) {
  i32 cell_x = x / 256, cell_z = z / 256;
  if (size > 6.7f) {
    bounds->left = cell_x - 2;
    bounds->top = cell_z - 2;
    bounds->right = cell_x + 3;
    bounds->bottom = cell_z + 3;
  } else {
    i32 inset = size > 3.3f ? 2 : 1;
    bounds->left = cell_x - inset;
    bounds->top = cell_z - inset;
    bounds->right = cell_x + 2;
    bounds->bottom = cell_z + 2;
  }
  if (bounds->left >= 0 && bounds->top >= 0 && bounds->right < i32(columns) && bounds->right < i32(rows)) {
    *clipped = 0;
    return 1;
  }
  if (bounds->left < 0)
    bounds->left = 0;
  if (bounds->top < 0)
    bounds->top = 0;
  if (bounds->right >= i32(columns))
    bounds->right = i32(columns - 1);
  if (bounds->right >= i32(rows))
    bounds->right = i32(rows - 1);
  *clipped = 1;
  return 1;
}

// Classify strength using ordered thresholds, then publish both class bytes and the matching tuning word.
// Unordered input follows the original small-strength branch.
void DsImpactInstance::ConfigureMagnitude(f32 strength) {
  if (strength > 4.0f) {
    if (strength > 6.0f) {
      severity = 3;
      word_77 = 3;
      word_80 = 10000;
    } else {
      severity = 2;
      word_77 = 2;
      word_80 = 5000;
    }
  } else {
    severity = 1;
    word_77 = 1;
    word_80 = 2000;
  }
}

// Clip a private copy of the rectangle to the instance dimensions and tally every remaining cell in X/Z order.
// Accumulate failure without stopping traversal, retaining the original occupied output at each tally call.
u8 DsImpactInstance::ClearPatternArea(const ScRect *bounds) {
  i32 left = bounds->left, top = bounds->top, right = bounds->right, bottom = bounds->bottom;
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right >= i32(columns))
    right = i32(columns - 1);
  if (bottom >= i32(rows))
    bottom = i32(rows - 1);
  u8 result = 1;
  for (i32 x = left; x <= right; x = i32(u32(x) + 1)) {
    for (i32 z = top; z <= bottom; z = i32(u32(z) + 1)) {
      u8 occupied;
      if (!TallyTile(x, z, &occupied, 1, 1, 0))
        result = 0;
    }
  }
  return result;
}

// Read later handles on arrival so provider callbacks can alter which remaining effects require removal.
void DsImpactInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Publish prior occupancy, then destroy the surface occupant and both underground layers in their original order.
// Count only accepted destruction, continue after underground refusals and clear zoning only when surface handling permits.
u8 DsImpactInstance::TallyTile(i32 x, i32 z, u8 *occupied, u8 destroy, u8 clear_zone, u8 special) {
  if (!manager)
    return 0;
  *occupied = manager->occupants->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_one->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_two->DoesOccupantExistAtLocation(u32(x), u32(z));
  u8 result = 1;
  if (*occupied) {
    if (!destroy)
      return result;
    u32 charge = 0;
    bool surface_ok = true;
    ScOccupant *occupant;
    if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      surface_ok = manager->layer->DestroyOccupant(occupant, special ? kDsDamageRubble : kDsDamageRemove, &charge);
      if (surface_ok) {
        ++record.destroyed;
        record.cost += charge;
      }
      occupant->Release();
    }
    if (!surface_ok)
      return 0;
    GeBounds first, second;
    for (u32 index = 0; index < 2; ++index) {
      ScOccupantManager *population = index ? manager->underground_two : manager->underground_one;
      if (population->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
        GeBounds *bounds = index ? &second : &first, *affected = index ? &first : &second;
        *affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
        i32 fixed_x = i32(u32(x) << kCtMicroCoordinateShift), fixed_z = i32(u32(z) << kCtMicroCoordinateShift);
        *bounds = {{fixed_x, fixed_z, 0}, {fixed_x, fixed_z, 0}};
        auto *demolition = static_cast<GeDemolitionView *>(manager->demolition);
        if (demolition->DemolishOccupants(index ? manager->underground_two : manager->underground_one, bounds, nullptr,
                0, true, &charge, affected)) {
          charge = manager->layer->GetDemolitionCost(occupant);
          ++record.destroyed;
          record.cost += charge;
        } else
          result = 0;
        occupant->Release();
      }
    }
  }
  if (clear_zone) {
    u8 zone;
    manager->zones->GetValue(u32(x), u32(z), &zone);
    if (zone) {
      u8 cleared = 0;
      manager->zones->SetValue(u32(x), u32(z), &cleared);
    }
  }
  return result;
}

// Select the phase resource with its original random draw, expand its text and send the mutable synchronous payload.
// Store the returned effect after releasing the message text, then release the current loaded resource identity.
void DsImpactInstance::PostPhase(u32 state) {
  i32 slot = -1;
  u32 resource;
  if (state == 2) {
    resource = manager->random.Next(2) + 137;
    slot = 0;
  } else if (state == 3) {
    resource = manager->random.Next(2) + 139;
    slot = 1;
  } else if (state == 4) {
    resource = manager->random.Next(4) + 137;
    slot = 2;
  } else if (state == 6)
    resource = manager->random.Next(2) + 143;
  else
    return;
  auto *text = new ScString;
  DsTextResource content(resource, kRsDisasterNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsDisasterNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (slot >= 0)
    effects[slot] = notice.effect;
}

// Select particle, ignition or terrain damage using the original draw and magnitude-adjusted thresholds.
// Preserve event ordering, the special sound's Z/25 coordinate, and conditional shaking before terrain changes.
u8 DsImpactInstance::ApplyPattern(i32 x, i32 z, u32 message) {
  u32 damage_chance = DsImpactDamageChance, particle_chance = DsImpactParticleChance;
  u32 draw = manager->random.Next(100), magnitude = record.magnitude;
  if (magnitude >= 80) {
    damage_chance -= 20;
    particle_chance -= 20;
  } else if (magnitude >= 60) {
    damage_chance -= 10;
    particle_chance -= 10;
  } else if (magnitude < 40) {
    damage_chance += 10;
    particle_chance += 10;
  }
  if (manager) {
    if (message == kSpMsg_WaterImpact) {
      manager->box->Event(kAuEventPlayEffect, kAuEffectSpaceJunkHitsWater, x / 256, z / 25, 0);
    } else if (draw < damage_chance) {
      bool ignite = draw > particle_chance;
      EmitTileParticles(x, z, ignite ? 5.0f : 3.0f);
      i32 cell_x = x / 256, cell_z = z / 256;
      manager->box->Event(kAuEventPlayEffect, kAuEffectSpaceJunkHits, cell_x, cell_z, 0);
      manager->box->Event(kAuEventPlayBuildingSound, cell_x, cell_z, 0, 0);
      if (ignite)
        IgniteAtTile(cell_x, cell_z);
    } else {
      f32 size = f32(f64(draw - damage_chance) / (f64(100u - damage_chance) * f64(0.1f)));
      EmitTileParticles(x, z, size);
      i32 cell_x = x / 256, cell_z = z / 256;
      manager->box->Event(kAuEventPlayEffect, kAuEffectSpaceJunkHits, cell_x, cell_z, 0);
      manager->box->Event(kAuEventPlayBuildingSound, cell_x, cell_z, 0, 0);
      if (size >= 9.0f) {
        SpViewInterface *view = DsGetCityView();
        if (view)
          static_cast<SpIsoView *>(view)->DoScreenShake(0, 1000, 1, 3, true, 50);
        manager->box->Event(kAuEventPlayEffect, kAuEffectSpaceJunkShakesScreen, 0, 0, 0);
      }
      ScRect bounds;
      u8 clipped;
      if (ComputePatternBounds(x, z, size, &bounds, &clipped) && ClearPatternArea(&bounds) &&
          !DsContainsWater(manager->terrain, &bounds)) {
        DeformTerrain(x, z, size);
        if (DsHasLevelCorners(manager->terrain, &bounds))
          EmitGroundEffect(x, z, size);
      }
    }
  }
  return 1;
}

f64 DsImpactInstance::GetMagnitude() const {
  return f64(record.magnitude) * f64(0.1f);
}

i8 DsImpactInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return record.phase >= 5 ? levels[distance] : 0;
}

u32 DsImpactInstance::PanicRadius() {
  return record.phase >= 5 ? 5 : 0;
}

bool DsImpactInstance::IsActive() {
  return record.phase >= 5;
}

// Create the real impact display only while the owning effect exists, then initialize its size and lifetime.
// Factory refusal returns zero; initialization acceptance is ignored before the display is released.
u8 DsImpactInstance::EmitGroundEffect(i32 x, i32 z, f32 size) {
  if (!effect)
    return 0;
  void *created = this;
  if (!DsGetCom()->GetClassObject(kSpImpactDisplayClassId, IID_SpImpactDisplay, &created))
    return 0;
  auto *display = static_cast<SpImpactDisplay *>(created);
  display->Init(x, z, i32(i64(f64(size) * f64(0.3921568691730499f))));
  display->SetTimeToLive(DsImpactDisplayLifetime);
  display->Release();
  return 1;
}

// Advance the original absolute phase deadlines, activate debris display and retire after its global trajectory count drains.
// Preserve callback-sensitive reads, signed manager activity and unsigned wrapping fade/timeout deadlines.
bool DsImpactInstance::Tick(DsTickType) {
  u32 phase = record.phase, elapsed = ++record.phase_ticks;
  switch (phase) {
  case 1:
  case 2:
  case 3: {
    u32 deadline = phase == 1 ? word_16 : phase == 2 ? word_20 : word_24;
    if (elapsed >= deadline) {
      record.phase = phase + 1;
      PostPhase(phase + 1);
    }
    break;
  }
  case 4:
    if (elapsed >= word_28) {
      record.phase = 5;
      PostPhase(5);
      if (i32(manager->active_count) <= 1) {
        manager->box->Event(kAuEventPlayEffect, kAuEffectDisasterSpaceJunk, 0, 0, 0);
        manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterSpaceJunk, 0, 75, 0);
        announcement = true;
      }
      CtDate date;
      manager->city->GetSimulationClock()->GetCurrentClockDate(&date);
      auto *display = static_cast<SpDebrisDisplay *>(effect);
      record.date = date.serial;
      if (display)
        display->Start();
      else
        record.phase = 6;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case 5:
    if (elapsed >= word_32) {
      record.phase = 6;
      PostPhase(6);
      if (effect)
        static_cast<SpDebrisDisplay *>(effect)->Stop();
    }
    break;
  case 6:
    if (!effect || !static_cast<SpDebrisDisplay *>(effect)->GetCountOfRemainingSpaceDebris() ||
        record.phase_ticks >= word_32 + 250u) {
      ClearEffects();
      manager->FinishInstance(this);
    }
    break;
  }
  return true;
}

// Bind city dimensions and absolute phase deadlines, publish saved magnitude and subscribe before configuring the display.
// Preserve the raw automatic gate, independent service lookups, direct COM output field and manual tick after layer insertion.
bool DsImpactInstance::Init(DsImpactManager *owner, f32 strength, u8 automatic) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    columns = owner->city->CellCountX();
    rows = manager->city->CellCountZ();
    word_16 = DsImpactFirstDelay;
    word_20 = DsImpactSecondDelay;
    word_24 = DsImpactThirdDelay;
    word_28 = DsImpactAttackDelay;
    word_32 = DsImpactEndDelay;
    word_36 = word_32 + 10;
    f64 rounded = f64(strength) * 10.0 + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    record.date = 0;
    record.magnitude = u32(bits) + kScDoubleIntegerLowWordBias;
    record.destroyed = 0;
    record.cost = 0;
    record.phase = 1;
    record.phase_ticks = 0;
    if (!automatic) {
      record.phase = 4;
      word_16 = 0;
      word_20 = 0;
      word_24 = 0;
      word_28 = 0;
    }
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_BuildingImpact);
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WaterImpact);
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_GroundImpact);
    ConfigureMagnitude(f32(f64(record.magnitude) * f64(0.1f)));
    if (!effect && DsGetCom()->GetClassObject(kSpDebrisDisplayClassId, IID_SpDebrisDisplay, &effect)) {
      static_cast<SpDebrisDisplay *>(effect)->Init();
      static_cast<SpDebrisDisplay *>(effect)->SetAverageDebrisTime(DsImpactAverageDebrisTime);
    }
    manager->layer->DisasterInstanceChange(this, 0);
    if (!automatic)
      Tick(DsDayTick);
  }
  return true;
}

// Replace history from its capped archive, then append restored instances from the separate pending archive.
// Close each archive before opening the next, seed missing history regardless of read failures.
bool DsImpactManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsImpactHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        if (count > 50000)
          count = 50000;
        for (u32 index = 0; index < count && result; ++index) {
          DsImpactRecord entry;
          result = stream->ReadBlock(&entry, 24) != 0;
          if (result)
            *history.Append() = entry;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsImpactInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        for (u32 index = 0; index < count && result; ++index) {
          DsImpactRecord entry;
          result = stream->ReadBlock(&entry, 24) != 0;
          if (result)
            *pending.Append() = entry;
        }
      }
    }
  }
  if (!history.size)
    SeedHistory();
  return true;
}

// Use the existing explicit date constructor to retain the zero seed without sampling the host wall clock.
void DsImpactManager::SeedHistory() {
  CtDate current(0u, 0, 0);
  city->GetSimulationClock()->GetCurrentClockDate(&current);
  DsImpactRecord entry = {current.serial, 0, 0, 0, 0, 0};
  *history.Append() = entry;
}

i32 DsImpactMaximumActive = 1, DsImpactMinimumMonths = 16;
u32 DsImpactChance = 83, DsImpactMinimumYear = 1960;
// The remaining unused tuning word keeps its neutral address-derived name.
static u32 DsImpactWordA90 = 96;
u32 DsImpactFirstDelay = 0, DsImpactSecondDelay = 60, DsImpactThirdDelay = 120;
u32 DsImpactAttackDelay = 180, DsImpactEndDelay = 750, DsImpactAverageDebrisTime = kDsImpactDefaultAverageDebrisTime;
u32 DsImpactDisplayLifetime = kDsImpactDefaultDisplayLifetime;

bool DsImpactManager::AttemptDisasterStart() {
  f32 strength = f32(random.NormalRange(f64(0.1f), f64(0.9f)));
  return Start(strength, u8(automatic_start));
}

bool DsImpactManager::StaticShutdown() {
  return true;
}

// Require room under the signed active limit and the configured calendar year before examining history.
// Draw only after the strict thirty-day-month delay, exposing automatic mode around the synchronous trigger.
bool DsImpactManager::Tick() {
  if (i32(active_count) < DsImpactMaximumActive) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    if (current.Year() >= DsImpactMinimumYear) {
      u32 today = current.serial;
      if (!history.size)
        SeedHistory();
      u32 previous = history.data[history.size - 1].date;
      if (today < previous)
        previous = today;
      if (today - previous > 30u * u32(DsImpactMinimumMonths) && random.Next(10000) < DsImpactChance) {
        automatic_start = true;
        AttemptDisasterStart();
        automatic_start = false;
      }
    }
  }
  return true;
}

// Load impact tuning in the original order, including the repeated active-limit and month-delay queries.
// Reuse one mutable variant, retain failed-key values and report success even when the variable service is absent.
bool DsImpactManager::StaticInit() {
  ScVariableManager *provider = DsGetPropertyProvider();
  if (provider) {
    ScValue value;
    if (provider->GetVariable(kDsImpactMaximumActivePropertyId, &value))
      DsImpactMaximumActive = i32(value.value.word);
    if (provider->GetVariable(kDsImpactMinimumMonthsPropertyId, &value))
      DsImpactMinimumMonths = i32(value.value.word);
    if (provider->GetVariable(kDsImpactMaximumActivePropertyId, &value))
      DsImpactMaximumActive = i32(value.value.word);
    if (provider->GetVariable(kDsImpactMinimumMonthsPropertyId, &value))
      DsImpactMinimumMonths = i32(value.value.word);
    if (provider->GetVariable(kDsImpactMaximumMonthsPropertyId, &value))
      DsImpactWordA90 = value.value.word;
    if (provider->GetVariable(kDsImpactMinimumStrengthPropertyId, &value))
      DsImpactMinimumStrength = value.value.word;
    if (provider->GetVariable(kDsImpactMaximumStrengthPropertyId, &value))
      DsImpactMaximumStrength = value.value.word;
    if (provider->GetVariable(kDsImpactChancePropertyId, &value))
      DsImpactChance = value.value.word;
    if (provider->GetVariable(kDsImpactFirstDelayPropertyId, &value))
      DsImpactFirstDelay = value.value.word;
    if (provider->GetVariable(kDsImpactSecondDelayPropertyId, &value))
      DsImpactSecondDelay = value.value.word;
    if (provider->GetVariable(kDsImpactThirdDelayPropertyId, &value))
      DsImpactThirdDelay = value.value.word;
    if (provider->GetVariable(kDsImpactAttackDelayPropertyId, &value))
      DsImpactAttackDelay = value.value.word;
    if (provider->GetVariable(kDsImpactEndDelayPropertyId, &value))
      DsImpactEndDelay = value.value.word;
    if (provider->GetVariable(kDsImpactAverageDebrisTimePropertyId, &value))
      DsImpactAverageDebrisTime = value.value.word;
    if (provider->GetVariable(kDsImpactParticleChancePropertyId, &value))
      DsImpactParticleChance = value.value.word;
    if (provider->GetVariable(kDsImpactDamageChancePropertyId, &value))
      DsImpactDamageChance = value.value.word;
    if (provider->GetVariable(kDsImpactDisplayLifetimePropertyId, &value))
      DsImpactDisplayLifetime = value.value.word;
    if (provider->GetVariable(kDsImpactMaximumCornerDifferencePropertyId, &value))
      DsImpactMaximumCornerDifference = value.value.word;
    if (provider->GetVariable(kDsImpactMinimumYearPropertyId, &value))
      DsImpactMinimumYear = value.value.word;
  }
  return true;
}

// Construct the actual impact manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateClass52FE8D52() {
  return static_cast<DsManager *>(new DsImpactManager);
}

f64 DsImpactManager::ActiveCountIsPositive() {
  return i32(active_count) > 0 ? 1.0 : 0.0;
}

// Accept the three subscribed pattern notifications only while initialized and for their exact source identity.
// Publish unsigned packed cell coordinates before forwarding their fixed cell centers and the original message ID.
bool DsImpactInstance::DoMessage(ScMessage *message) {
  if (initialized &&
      (message->type == kSpMsg_BuildingImpact || message->type == kSpMsg_WaterImpact ||
          message->type == kSpMsg_GroundImpact) &&
      u32(message->data3) == kSpSpaceDebrisTrajectorySignature) {
    location_x = u32(message->data2) >> 16;
    location_z = u32(message->data2) & 65535;
    ApplyPattern(i32((location_x << kCtMicroCoordinateShift) + 128), i32((location_z << kCtMicroCoordinateShift) + 128),
        u32(message->type));
  }
  return true;
}

// Construct the original four identities and inactive manager state, seeding its generator after retained pointers.
DsImpactManager::DsImpactManager()
    : initialized(false), automatic_start(false), layer(nullptr), demolition(nullptr), terrain(nullptr), zones(nullptr),
      simulation(nullptr), agent_types(nullptr), city(nullptr), occupants(nullptr), underground_one(nullptr),
      underground_two(nullptr), box(nullptr), active(false), random(kScRandomSeedFromClock), active_count(0),
      flags_108(0) {
}

DsImpactManager::~DsImpactManager() {
  DsImpactManager::Shutdown();
}

// Expose the common and impact-specific manager identities, retaining through the root agent operation.
// Other IDs use the shared root query, including its original unsupported-output clearing behavior.
bool DsImpactManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsImpactControl, DsImpactControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsImpactManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsImpactManager::Release() {
  return DsAgent::Release();
}

// Remove the load notification, then release city dependencies in their original non-field order.
// Clear pending before history and keep the initialized flag published until every callback and clear finishes.
bool DsImpactManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&demolition);
    ScRelease(&zones);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&underground_one);
    ScRelease(&underground_two);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&simulation);
    ScRelease(&city);
    pending.Clear();
    history.Clear();
    initialized = false;
  }
  return true;
}

// Replay each saved pending record when city loading finishes, rereading the vector storage between callbacks.
// Count every attempted initialization and clear the pending range only after all captured entries are processed.
bool DsImpactManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      DsImpactInstance *instance = new DsImpactInstance;
      instance->Init(this, pending.data + index);
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

void DsImpactManager::StopInstance(DsInstance *instance) {
  static_cast<DsImpactInstance *>(instance)->Shutdown();
}

// Report the history length without an output array, or copy up to the caller's requested count in stored order.
// Count is written before copying and reread during iteration, preserving genuine aliased output behavior.
u32 DsImpactManager::GetRecords(DsImpactRecord *records, u32 *count) const {
  if (!records) {
    *count = history.size;
    return *count;
  }
  u32 available = history.size;
  if (available > *count)
    available = *count;
  *count = available;
  for (u32 i = 0; i < *count; ++i)
    records[i] = history.data[i];
  return *count;
}

// Test whether a first history entry exists, optionally copying that entry. A null output still answers the query.
bool DsImpactManager::GetFirstRecord(DsImpactRecord *record) const {
  if (!history.size)
    return false;
  if (record)
    *record = history.data[0];
  return true;
}

// Restore saved state around live initialization, rereading the caller's record after that callback.
// Resume phase five through the original virtual tick, then classify the resulting magnitude.
bool DsImpactInstance::Init(DsImpactManager *owner, const DsImpactRecord *saved) {
  record = *saved;
  u32 magnitude = record.magnitude;
  if (magnitude < 10 || record.phase > 7)
    record.phase = 1;
  if (record.phase > 7)
    record.phase = 1;
  if (record.phase_ticks > 10000)
    record.phase_ticks = 0;
  Init(owner, f32(f64(magnitude) * f64(0.1f)), true);
  record.date = saved->date;
  record.magnitude = saved->magnitude;
  record.destroyed = saved->destroyed;
  record.cost = saved->cost;
  record.phase = saved->phase;
  record.phase_ticks = saved->phase_ticks;
  if (saved->phase == 5) {
    record.phase_ticks = word_28;
    record.phase = 4;
    Tick(DsDayTick);
  }
  ConfigureMagnitude(f32(f64(record.magnitude) * f64(0.1f)));
  return true;
}

// Write capped history before the separate live-instance archive. Preserve each archive's independent failure gate,
// the signed active-count cap and all matching records after its count write, ignoring individual block refusals.
bool DsImpactManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsImpactHistoryRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = history.size > 50000 ? 50000 : history.size;
      if (stream->WriteU32(count) && count) {
        for (u32 index = 0; index < count; ++index) {
          DsImpactRecord record = history.data[index];
          stream->WriteBlock(&record, 24);
        }
      }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsImpactInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (i32(active_count) > 50000)
        active_count = 50000;
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next()) {
          if (instance->TypeId() == kDsImpactTypeId) {
            DsImpactRecord record;
            static_cast<DsImpactInstance *>(instance)->GetRecord(&record);
            stream->WriteBlock(&record, 24);
          }
        }
        layer->DeleteDisasterListIterator(iterator);
      }
    }
  }
  return true;
}

u32 DsImpactMinimumStrength = 1, DsImpactMaximumStrength = 99;

// Original spinword3A9C8 serializes this start entry independently of the track family's3A6B4.
// Native runtime allocation replaces the unrelated stock allocator; retain the game's nonrecursive entry exclusion.
static ScGate DsImpactStartLock;

// Start below the signed active limit, normalizing supplied strength by its original ordered thresholds.
// Narrow the interpolated magnitude before construction, ignore live Init's result and count only after it returns.
u8 DsImpactManager::Start(f32 strength, u8 automatic) {
  DsImpactStartLock.Lock();
  u8 started = 0;
  if (i32(active_count) < DsImpactMaximumActive) {
    f64 normalized = strength;
    if (strength > 100.0f)
      normalized = 0.5;
    else if (strength > 10.0f)
      normalized *= f64(0.01f);
    else if (strength > 1.0f)
      normalized *= f64(0.1f);
    f32 magnitude = f32((f64(DsImpactMaximumStrength - DsImpactMinimumStrength) * normalized
        + i32(DsImpactMinimumStrength)) * f64(0.1f));
    auto *instance = new DsImpactInstance;
    instance->Init(this, magnitude, automatic);
    ++active_count;
    started = 1;
  }
  DsImpactStartLock.Unlock();
  return started;
}

// Deform the original large, medium or small vertex patterns using the current owner's exact random sequence.
u8 DsImpactInstance::DeformTerrain(i32 x, i32 z, f32 size) {
  if (manager) {
    ScDirtBag *ground = manager->terrain;
    u8 minimum = ground->MinAltitudeAllowed(), maximum = ground->MaxAltitudeAllowed();
    u8 sea_level = ground->GetGlobalSeaLevel(), clipped;
    ScRect bounds;
    ComputePatternBounds(x, z, size, &bounds, &clipped);
    f32 adjusted = size;
    if (size > 6.7f) {
      if (!clipped) {
        const i32 points[12][2] = {
            {2, 1}, {3, 1}, {4, 1}, {1, 2}, {5, 2}, {1, 3}, {5, 3}, {1, 4}, {5, 4}, {2, 5}, {3, 5}, {4, 5}};
        for (u32 index = 0; index < 12; ++index) {
          i32 delta = manager->random.Range(2, 4);
          DsAdjustDirtAltitude(u32(bounds.left) + u32(points[index][0]), u32(bounds.top) + u32(points[index][1]), delta,
              minimum, maximum, sea_level, ground);
        }
        i32 delta = manager->random.Range(-2, 0);
        DsAdjustDirtAltitude(u32(bounds.left) + 3, u32(bounds.top) + 3, delta, minimum, maximum, sea_level, ground);
        return 1;
      }
      adjusted = 4.0f;
    }
    if (adjusted > 3.3f) {
      if (!clipped) {
        const i32 outer[8][2] = {{2, 1}, {3, 1}, {1, 2}, {4, 2}, {1, 3}, {4, 3}, {2, 4}, {3, 4}};
        const i32 inner[4][2] = {{2, 2}, {3, 2}, {2, 3}, {3, 3}};
        for (u32 index = 0; index < 8; ++index) {
          i32 delta = manager->random.Range(1, 3);
          DsAdjustDirtAltitude(u32(bounds.left) + u32(outer[index][0]), u32(bounds.top) + u32(outer[index][1]), delta,
              minimum, maximum, sea_level, ground);
        }
        for (u32 index = 0; index < 4; ++index) {
          i32 delta = manager->random.Range(-2, 0);
          DsAdjustDirtAltitude(u32(bounds.left) + u32(inner[index][0]), u32(bounds.top) + u32(inner[index][1]), delta,
              minimum, maximum, sea_level, ground);
        }
        return 1;
      }
      adjusted = 0;
    }
    ComputePatternBounds(x, z, adjusted, &bounds, &clipped);
    if (!clipped && manager) {
      const i32 points[4][2] = {{2, 1}, {1, 2}, {3, 2}, {2, 3}};
      for (u32 index = 0; index < 4; ++index) {
        i32 delta = manager->random.Range(1, 3);
        DsAdjustDirtAltitude(u32(bounds.left) + u32(points[index][0]), u32(bounds.top) + u32(points[index][1]), delta,
            minimum, maximum, sea_level, ground);
      }
      i32 delta = manager->random.Range(-2, 0);
      DsAdjustDirtAltitude(u32(bounds.left) + 2, u32(bounds.top) + 2, delta, minimum, maximum, sea_level, ground);
    }
  }
  return 1;
}

// The original folded city initializer only shuts down an already-bound manager; fresh binding retains each service.
// Reset activity and mark initialized before registering the load notification so its callback sees the ready manager.
bool DsTrackManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(retained_city);
    terrain = ScAddRefNotNull(city->DirtBag());
    // The SDK getter remains pointer-sized; only the established Unknown retain/release contract is used here.
    demolition = ScAddRefNotNull(static_cast<ScUnknown *>(city->DemolitionLayer()));
    zones = ScAddRefNotNull(city->ZoneLayer());
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    underground_one = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
    underground_two = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
    layer = ScAddRefNotNull(retained_layer);
    active_count = 0;
    flags_108 = 0;
    active = false;
    initialized = true;
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  }
  return true;
}

// The original folded city initializer only shuts down an already-bound manager; fresh binding retains each service.
// Reset activity and mark initialized before registering the load notification so its callback sees the ready manager.
bool DsImpactManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(retained_city);
    terrain = ScAddRefNotNull(city->DirtBag());
    // The SDK getter remains pointer-sized; only the established Unknown retain/release contract is used here.
    demolition = ScAddRefNotNull(static_cast<ScUnknown *>(city->DemolitionLayer()));
    zones = ScAddRefNotNull(city->ZoneLayer());
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    underground_one = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
    underground_two = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
    layer = ScAddRefNotNull(retained_layer);
    active_count = 0;
    flags_108 = 0;
    active = false;
    initialized = true;
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  }
  return true;
}

// Read later handles on arrival so provider callbacks can alter which remaining effects require removal.
void DsTrackInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Select the phase resource with its original random draw, expand its text and send the mutable synchronous payload.
// Store the returned effect after releasing the message text, then release the current loaded resource identity.
void DsTrackInstance::PostPhase(u32 state) {
  i32 slot;
  u32 resource;
  if (state == 2) {
    resource = manager->random.Next(4) + 575;
    slot = 0;
  } else if (state == 3) {
    resource = manager->random.Next(4) + 579;
    slot = 1;
  } else if (state == 4) {
    resource = manager->random.Next(4) + 583;
    slot = 2;
  } else
    return;
  auto *text = new ScString;
  DsTextResource content(resource, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (slot >= 0)
    effects[slot] = notice.effect;
}

// Apply the original double-bias rounding without an out-of-range floating-to-integer cast. Valid city
// coordinates occupy the bias's exact range; the stored low word supplies the signed rounded result.
static i32 DsRoundCoordinate(f64 value) {
  f64 biased = value + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  return i32(u32(bits) + kScDoubleIntegerLowWordBias);
}

// Choose a cross-city segment with at least four cells of Z separation, clipping the far end with the original
// float slope and biased rounding. Query terrain for all three positions, preserving conditional RNG order.
bool DsTrackManager::ChooseEndpoints(ScVec3I *start, ScVec3I *end, ScVec3I *current) {
  i32 columns = i32(city->CellCountX() << kCtMicroCoordinateShift),
      rows = i32(city->CellCountZ() << kCtMicroCoordinateShift), radius = rows * 3 / 4;
  do {
    start->z = random.Range(0, rows);
    end->z = random.Range(start->z - radius, start->z + radius);
  } while (abs(end->z - start->z) < 1024);
  start->x = 0;
  end->x = columns - 1;
  f32 slope = f32(f64(end->x - start->x) / f64(end->z - start->z));
  if (end->z >= rows) {
    i32 distance = rows - start->z;
    end->z = rows - 1;
    end->x = DsRoundCoordinate(f64(distance) * slope);
  } else if (end->z < 0) {
    end->z = 0;
    end->x = DsRoundCoordinate(-f64(slope) * start->z);
  }
  if (start->x < 0)
    start->x = 0;
  else if (start->x >= columns)
    start->x = columns - 1;
  if (end->x < 0)
    end->x = 0;
  else if (end->x >= columns)
    end->x = columns - 1;
  start->y = DsFixedAltitude(city, start->x, start->z);
  end->y = DsFixedAltitude(city, end->x, end->z);
  i32 selected = start->z;
  if (selected < end->z)
    selected = random.Range(selected, end->z);
  current->z = selected;
  current->x = start->x + abs(DsRoundCoordinate(f64(selected - start->z) * slope));
  current->y = DsFixedAltitude(city, current->x, current->z);
  return true;
}

// Append the instance's nine actual saved words directly to manager history without dispatching a state-interface query.
// The original reaches these same contiguous words through instance+10388 and the manager's history vector.
void DsTrackManager::RecordInstance(DsTrackInstance *instance) {
  DsTrackRecord *record = history.Append();
  *record = {instance->date, instance->magnitude, instance->destroyed, instance->cost, instance->automatic,
      instance->packed_endpoints, instance->packed_location, instance->phase, instance->phase_ticks};
}

// Save the final record before ending instance ownership, then notify the current controller of removal.
// Shutdown callbacks therefore observe the appended history and the controller sees the cleared instance manager.
void DsTrackManager::FinishInstance(DsTrackInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

u32 DsTrackPhaseDurations[4] = {0, 60, 120, 180};

// Initialize one inactive swath, register its completion target and create the canonical path effect when absent.
// Publish initialized state before callbacks, retain prior path caches and notify the controller before an immediate tick.
bool DsTrackInstance::Init(DsTrackManager *owner, f32 strength, const ScVec3I *input_start, const ScVec3I *input_end,
    const ScVec3I *input_current, bool is_automatic) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    maximum_x = manager->city->CellCountX();
    maximum_z = manager->city->CellCountZ();
    start = *input_start;
    end = *input_end;
    current = *input_current;
    horizontal = abs(input_end->x - input_start->x) > abs(input_end->z - input_start->z);
    phase_durations[0] = DsTrackPhaseDurations[0];
    phase_durations[1] = DsTrackPhaseDurations[1];
    f64 scaled = f64(strength) * 10.0 + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &scaled, 8);
    phase_durations[2] = DsTrackPhaseDurations[2];
    phase_durations[3] = DsTrackPhaseDurations[3];
    date = 0;
    magnitude = u32(bits) + kScDoubleIntegerLowWordBias;
    automatic = manager->flag_73;
    destroyed = 0;
    cost = 0;
    phase = 1;
    phase_ticks = 0;
    packed_endpoints = (u32(u8(start.x)) << 24) | (u32(u8(start.z)) << 16) | (u32(u8(end.x)) << 8) | u8(end.z);
    packed_location = (u32(u16(current.x)) << 16) | u16(current.z);
    if (!is_automatic) {
      phase = 4;
      phase_durations[0] = 0;
      phase_durations[1] = 0;
      phase_durations[2] = 0;
      phase_durations[3] = 0;
    }
    ConfigureMagnitude(f32(f64(magnitude) * f64(0.1f)));
    AdjustWidth(automatic != 0);
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_EarthquakeEnded);
    if (!effect) {
      DsGetCom()->GetClassObject(kSpEarthquakeViewClassId, IID_SpEarthquakeView, &effect);
      static_cast<SpEarthquakeView *>(effect)->Init(&start, &end, &current, 1000, severity, -1, 0);
    }
    manager->layer->DisasterInstanceChange(this, 0);
    if (!is_automatic)
      Tick(DsDayTick);
  }
  return true;
}

DsTrackInstance::DsTrackInstance()
    : initialized(false), references(0), announcement(false), severity(1), tuning_64(0), tuning_68(0), start{0, 0, 0},
      end{0, 0, 0}, current{0, 0, 0}, horizontal(true), path_cursor(0), base_count(0), total_count(0), manager(nullptr),
      effect(nullptr), date(0), magnitude(0), destroyed(0), cost(0), automatic(0), packed_endpoints(0),
      packed_location(0), phase(0), phase_ticks(0) {
  for (u32 i = 0; i < 4; ++i)
    phase_durations[i] = 0;
  for (u32 i = 0; i < 5; ++i)
    effects[i] = kApInvalidNotificationId;
}

// Advertise the primary and motion identities, retaining a reference only on successful queries. The message target
// remains directly usable but is not exposed by QueryInterface; failures clear the output.
bool DsTrackInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsTornadoMotion, DsTrackState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsTrackInstance::AddRef() {
  return ++references;
}

DsTrackInstance::~DsTrackInstance() {
  Shutdown();
}

// Retain the receiver while more than one reference remains. A final or already-zero count deletes it without
// overwriting that count first, so shutdown observes one for a final held reference.
u32 DsTrackInstance::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// End an initialized track instance in original order: manager count, effect reference, UI announcement, then
// cleared receiver ownership and notification removal. Repeated shutdown succeeds without further service calls.
bool DsTrackInstance::Shutdown() {
  if (initialized) {
    manager->InstanceEnded();
    if (effect) {
      static_cast<ScUnknown *>(effect)->Release();
      effect = nullptr;
    }
    if (announcement) {
      manager->box->Event(kAuEventEffectSlot52, kAuEffectDisasterEarthquake, current.x, current.z, 0);
      announcement = false;
    }
    manager = nullptr;
    initialized = false;
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_EarthquakeEnded);
  }
  return true;
}

// Replace history from its capped archive, then append restored instances from the separate pending archive.
// Close each archive before opening the next, seed missing history and repair zones regardless of read failures.
bool DsTrackManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTrackHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        if (count > 50000)
          count = 50000;
        for (u32 index = 0; index < count && result; ++index) {
          DsTrackRecord entry;
          result = stream->ReadBlock(&entry, 36) != 0;
          if (result)
            *history.Append() = entry;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTrackInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        for (u32 index = 0; index < count && result; ++index) {
          DsTrackRecord entry;
          result = stream->ReadBlock(&entry, 36) != 0;
          if (result)
            *pending.Append() = entry;
        }
      }
    }
  }
  if (!history.size)
    SeedHistory();
  RepairZones();
  return true;
}

// Use the existing explicit date constructor to retain the zero seed without sampling the host wall clock.
void DsTrackManager::SeedHistory() {
  CtDate current(0u, 0, 0);
  city->GetSimulationClock()->GetCurrentClockDate(&current);
  DsTrackRecord entry = {current.serial, 0, 0, 0, 0, 0, 0, 0, 0};
  *history.Append() = entry;
}

i32 DsTrackTuning64[3] = {1000, 2500, 4000};
i32 DsTrackTuning68[3] = {1000, 2500, 4000};

// Select severity, initial swath width and both tuning words using strict magnitude thresholds at four and six.
i32 DsTrackInstance::ConfigureMagnitude(f32 strength) {
  if (strength > 4.0f) {
    if (strength > 6.0f) {
      severity = 3;
      width = 3;
      tuning_64 = DsTrackTuning64[2];
      tuning_68 = DsTrackTuning68[2];
    } else {
      severity = 2;
      width = 2;
      tuning_64 = DsTrackTuning64[1];
      tuning_68 = DsTrackTuning68[1];
    }
  } else {
    severity = 1;
    width = 1;
    tuning_64 = DsTrackTuning64[0];
    tuning_68 = DsTrackTuning68[0];
  }
  return tuning_68;
}

void DsTrackInstance::AdjustWidth(bool is_automatic) {
  if (is_automatic)
    width = i8(u8(width) - 1u);
}

i32 DsTrackMaximumActive = 1, DsTrackMinimumMonths = 16, DsTrackChance = 83;
i32 DsTrackPhaseThreeDuration[3] = {100, 500, 1000};
i32 DsTrackPhaseThreeArgument[3] = {0, 1, 1};
i32 DsTrackPhaseFiveDuration[3] = {1000, 3000, 5000};
i32 DsTrackPhaseFiveArgument[3] = {1, 2, 3};
// Original39528/3955C/39598..395A0 have no recovered consumers; preserve their words without invented meanings.
static u32 DsTrackWord528 = 96, DsTrackWord55C = 50, DsTrackWord598 = 1, DsTrackWord59C = 1, DsTrackWord5A0 = 1;

// End one active instance without underflowing an already-zero count. The active flag clears only when the
// resulting count is zero; no flag write occurs while another instance remains.
void DsTrackManager::InstanceEnded() {
  if (!active_count)
    active_count = 1;
  if (active_count-- == 1)
    active = false;
}

bool DsTrackManager::AttemptDisasterStart() {
  f32 strength = f32(random.InverseSqrtRange(0.0, 1.0));
  return Start(strength, automatic_start);
}

bool DsTrackManager::StaticShutdown() {
  return true;
}

// Check the signed active limit before querying the clock, seed missing history and require more than the configured
// thirty-day months since its last entry. Draw only after that delay and expose automatic mode around the trigger.
bool DsTrackManager::Tick() {
  if (i32(active_count) < DsTrackMaximumActive) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    u32 today = current.serial;
    if (!history.size)
      SeedHistory();
    u32 previous = history.data[history.size - 1].date;
    if (today < previous)
      previous = today;
    if (today - previous > 30u * u32(DsTrackMinimumMonths) && random.Next(10000) < u32(DsTrackChance)) {
      automatic_start = true;
      AttemptDisasterStart();
      automatic_start = false;
    }
  }
  return true;
}

// Load the moving-swath tuning words in their original order, retaining the repeated limit and month queries.
// Failed keys preserve existing values; one mutable variant serves every call and an absent service returns false.
bool DsTrackManager::StaticInit() {
  ScVariableManager *provider = DsGetPropertyProvider();
  if (!provider)
    return false;
  ScValue value;
  if (provider->GetVariable(kDsTrackMaximumActivePropertyId, &value))
    DsTrackMaximumActive = i32(value.value.word);
  if (provider->GetVariable(kDsTrackMinimumMonthsPropertyId, &value))
    DsTrackMinimumMonths = i32(value.value.word);
  if (provider->GetVariable(kDsTrackMaximumActivePropertyId, &value))
    DsTrackMaximumActive = i32(value.value.word);
  if (provider->GetVariable(kDsTrackMinimumMonthsPropertyId, &value))
    DsTrackMinimumMonths = i32(value.value.word);
  if (provider->GetVariable(kDsTrackMaximumMonthsPropertyId, &value))
    DsTrackWord528 = value.value.word;
  if (provider->GetVariable(kDsTrackMinimumStrengthPropertyId, &value))
    DsTrackMinimumStrength = value.value.word;
  if (provider->GetVariable(kDsTrackMaximumStrengthPropertyId, &value))
    DsTrackMaximumStrength = value.value.word;
  if (provider->GetVariable(kDsTrackChancePropertyId, &value))
    DsTrackChance = i32(value.value.word);
  if (provider->GetVariable(kDsTrackDestructionChance0PropertyId, &value))
    DsTrackTuning64[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackDestructionChance1PropertyId, &value))
    DsTrackTuning64[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackDestructionChance2PropertyId, &value))
    DsTrackTuning64[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackIgnitionChance0PropertyId, &value))
    DsTrackTuning68[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackIgnitionChance1PropertyId, &value))
    DsTrackTuning68[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackIgnitionChance2PropertyId, &value))
    DsTrackTuning68[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackBorderRadii0PropertyId, &value))
    DsTrackBorderRadii[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackBorderRadii1PropertyId, &value))
    DsTrackBorderRadii[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackBorderRadii2PropertyId, &value))
    DsTrackBorderRadii[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackEarthquakePreparednessPercentagePropertyId, &value))
    DsTrackWord55C = value.value.word;
  if (provider->GetVariable(kDsTrackPhaseFiveDuration0PropertyId, &value))
    DsTrackPhaseFiveDuration[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseFiveDuration1PropertyId, &value))
    DsTrackPhaseFiveDuration[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseFiveDuration2PropertyId, &value))
    DsTrackPhaseFiveDuration[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseFivePeakShake0PropertyId, &value))
    DsTrackPhaseFiveArgument[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseFivePeakShake1PropertyId, &value))
    DsTrackPhaseFiveArgument[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseFivePeakShake2PropertyId, &value))
    DsTrackPhaseFiveArgument[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreeDuration0PropertyId, &value))
    DsTrackPhaseThreeDuration[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreeDuration1PropertyId, &value))
    DsTrackPhaseThreeDuration[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreeDuration2PropertyId, &value))
    DsTrackPhaseThreeDuration[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreePeakShake0PropertyId, &value))
    DsTrackPhaseThreeArgument[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreePeakShake1PropertyId, &value))
    DsTrackPhaseThreeArgument[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseThreePeakShake2PropertyId, &value))
    DsTrackPhaseThreeArgument[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackPhaseDurations0PropertyId, &value))
    DsTrackPhaseDurations[0] = value.value.word;
  if (provider->GetVariable(kDsTrackPhaseDurations1PropertyId, &value))
    DsTrackPhaseDurations[1] = value.value.word;
  if (provider->GetVariable(kDsTrackPhaseDurations2PropertyId, &value))
    DsTrackPhaseDurations[2] = value.value.word;
  if (provider->GetVariable(kDsTrackPhaseDurations3PropertyId, &value))
    DsTrackPhaseDurations[3] = value.value.word;
  if (provider->GetVariable(kDsTrackLowIntensityGashWidthPropertyId, &value))
    DsTrackWord598 = value.value.word;
  if (provider->GetVariable(kDsTrackMediumIntensityGashWidthPropertyId, &value))
    DsTrackWord59C = value.value.word;
  if (provider->GetVariable(kDsTrackHighIntensityGashWidthPropertyId, &value))
    DsTrackWord5A0 = value.value.word;
  if (provider->GetVariable(kDsTrackOuterDeltas0PropertyId, &value))
    DsTrackOuterDeltas[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackOuterDeltas1PropertyId, &value))
    DsTrackOuterDeltas[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackOuterDeltas2PropertyId, &value))
    DsTrackOuterDeltas[2] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackInnerDeltas0PropertyId, &value))
    DsTrackInnerDeltas[0] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackInnerDeltas1PropertyId, &value))
    DsTrackInnerDeltas[1] = i32(value.value.word);
  if (provider->GetVariable(kDsTrackInnerDeltas2PropertyId, &value))
    DsTrackInnerDeltas[2] = i32(value.value.word);
  return true;
}

// Construct the actual moving-swath manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateTrackManager() {
  return static_cast<DsManager *>(new DsTrackManager);
}

// Clear a nonzero zone through its real byte-map getter and void setter, leaving zero zones untouched.
static void DsClearZone(CtZoneLayer *zones, i32 x, i32 z) {
  u8 value;
  zones->GetValue(u32(x), u32(z), &value);
  if (value) {
    u8 empty = 0;
    zones->SetValue(u32(x), u32(z), &empty);
  }
}

// Snapshot the three occupant interfaces before testing cells adjoining this vertex in their original order.
bool DsTrackInstance::HasAdjacentOccupant(i32 x, i32 z) {
  ScOccupantManager *surface = manager->occupants, *first = manager->underground_one,
                    *second = manager->underground_two;
  if (x < i32(maximum_x) && z < i32(maximum_z)) {
    if (surface->DoesOccupantExistAtLocation(u32(x), u32(z)) || first->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
        second->DoesOccupantExistAtLocation(u32(x), u32(z)))
      return true;
  }
  if (x <= 0) {
    if (z <= 0)
      return false;
    return surface->DoesOccupantExistAtLocation(u32(x), u32(z - 1)) ||
        first->DoesOccupantExistAtLocation(u32(x), u32(z - 1)) ||
        second->DoesOccupantExistAtLocation(u32(x), u32(z - 1));
  }
  if (z < i32(maximum_z)) {
    if (surface->DoesOccupantExistAtLocation(u32(x - 1), u32(z)) ||
        first->DoesOccupantExistAtLocation(u32(x - 1), u32(z)) ||
        second->DoesOccupantExistAtLocation(u32(x - 1), u32(z)))
      return true;
  }
  if (z <= 0)
    return false;
  if (x < i32(maximum_x)) {
    if (surface->DoesOccupantExistAtLocation(u32(x), u32(z - 1)) ||
        first->DoesOccupantExistAtLocation(u32(x), u32(z - 1)) ||
        second->DoesOccupantExistAtLocation(u32(x), u32(z - 1)))
      return true;
  }
  return surface->DoesOccupantExistAtLocation(u32(x - 1), u32(z - 1)) ||
      first->DoesOccupantExistAtLocation(u32(x - 1), u32(z - 1)) ||
      second->DoesOccupantExistAtLocation(u32(x - 1), u32(z - 1));
}

// Cache the actual zone interface, reject vertices outside inclusive bounds, then clear adjoining cells in order.
// Recheck current instance bounds after callbacks and preserve the original low-byte success result.
u8 DsTrackInstance::ClearAdjacentZones(i32 x, i32 z) {
  CtZoneLayer *zone_layer = manager->zones;
  if (x >= 0 && z >= 0 && x <= i32(maximum_x) && z <= i32(maximum_z)) {
    if (x < i32(maximum_x) && z < i32(maximum_z))
      DsClearZone(zone_layer, x, z);
    if (x <= 0) {
      if (z > 0)
        DsClearZone(zone_layer, x, z - 1);
    } else {
      if (z < i32(maximum_z))
        DsClearZone(zone_layer, x - 1, z);
      if (z > 0) {
        if (x < i32(maximum_x))
          DsClearZone(zone_layer, x, z - 1);
        DsClearZone(zone_layer, x - 1, z - 1);
      }
    }
  }
  return 1;
}

// Apply the original destructive tally to adjoining cells, preserving its asymmetric bounds and call order.
// Ignore each tile result and forward flags1/1/0 independently for every selected cell.
u8 DsTrackInstance::TallyAdjacentTiles(i32 x, i32 z) {
  if (x < i32(maximum_x) && z < i32(maximum_z))
    TallyTile(x, z, 1, 1, 0);
  if (x <= 0) {
    if (z > 0)
      TallyTile(x, z - 1, 1, 1, 0);
  } else {
    if (z < i32(maximum_z))
      TallyTile(x - 1, z, 1, 1, 0);
    if (z > 0) {
      if (x < i32(maximum_x))
        TallyTile(x, z - 1, 1, 1, 0);
      TallyTile(x - 1, z - 1, 1, 1, 0);
    }
  }
  return 1;
}

// Construct the original four identities and inactive manager state, seeding its generator after retained pointers.
DsTrackManager::DsTrackManager()
    : initialized(false), automatic_start(false), layer(nullptr), demolition(nullptr), terrain(nullptr), zones(nullptr),
      simulation(nullptr), agent_types(nullptr), city(nullptr), occupants(nullptr), underground_one(nullptr),
      underground_two(nullptr), box(nullptr), active(false), flag_73(false), random(kScRandomSeedFromClock),
      active_count(0), flags_108(0) {
}

DsTrackManager::~DsTrackManager() {
  DsTrackManager::Shutdown();
}

// Expose the common and track-specific manager identities, retaining through the root agent operation.
// Other IDs use the shared root query, including its original unsupported-output clearing behavior.
bool DsTrackManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsTrackControl, DsTrackControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsTrackManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsTrackManager::Release() {
  return DsAgent::Release();
}

// Remove the load notification, then release city dependencies in their original non-field order.
// Clear pending before history and keep the initialized flag published until every callback and clear finishes.
bool DsTrackManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&demolition);
    ScRelease(&zones);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&underground_one);
    ScRelease(&underground_two);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&simulation);
    ScRelease(&city);
    pending.Clear();
    history.Clear();
    initialized = false;
  }
  return true;
}

// Recreate each pending track in order when city loading finishes, ignoring individual initialization results.
// Count every attempted replay after Init, re-read pending storage each iteration, then retain its empty allocation.
bool DsTrackManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      DsTrackInstance *instance = new DsTrackInstance;
      instance->Init(this, pending.data + index);
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

void DsTrackManager::StopInstance(DsInstance *instance) {
  static_cast<DsTrackInstance *>(instance)->Shutdown();
}

// Generate the raster once, accumulating one X-jitter draw per original point before clipping it to the city.
// Append the perpendicular swath in positive/negative order, iterating only the original points rather than new ones.
bool DsTrackInstance::BuildPath() {
  if (total_count)
    return true;
  base_count = 1281;
  base_count = DsRasterPath(start.x / kCtMicroUnitsPerCell, start.z / kCtMicroUnitsPerCell,
      end.x / kCtMicroUnitsPerCell, end.z / kCtMicroUnitsPerCell, path, 1281);
  total_count = base_count;
  i32 drift = 0;
  for (u32 i = 0; i < base_count; ++i) {
    drift += manager->random.Range(-1, 2);
    path[i].x += drift;
    if (path[i].x < 0)
      path[i].x = 0;
    i32 bound = i32(manager->city->CellCountX());
    if (path[i].x >= bound)
      path[i].x = i32(manager->city->CellCountX() - 1);
  }
  if (width >= 2) {
    i32 radius = width - 1, columns = i32(manager->city->CellCountX()), rows = i32(manager->city->CellCountZ());
    u32 original_count = total_count;
    for (u32 i = 0; i < original_count; ++i) {
      for (i32 offset = 1; offset <= radius; ++offset) {
        if (horizontal) {
          i32 positive = path[i].y + offset, negative = path[i].y - offset;
          if (positive >= 0 && positive < rows)
            path[total_count++] = {path[i].x, positive};
          if (negative >= 0 && negative < rows)
            path[total_count++] = {path[i].x, negative};
        } else {
          i32 positive = path[i].x + offset, negative = path[i].x - offset;
          if (positive >= 0 && positive < columns)
            path[total_count++] = {positive, path[i].y};
          if (negative >= 0 && negative < columns)
            path[total_count++] = {negative, path[i].y};
        }
      }
    }
  }
  return true;
}

// Advance cumulative notice timing, start the real path effect and record the simulation date before activation.
// Terminal processing restores plot/flipping/cursor state before closing notices and letting the manager retire this.
bool DsTrackInstance::Tick(DsTickType) {
  u32 ticks = ++phase_ticks;
  switch (phase) {
  case 1:
    if (ticks >= phase_durations[0]) {
      phase = 2;
      PostPhase(2);
      manager->box->Event(kAuEventAmbience, 61, 1, 0, 0);
    }
    break;
  case 2:
    if (ticks >= phase_durations[1]) {
      phase = 3;
      PostPhase(3);
      manager->box->Event(kAuEventAmbience, 61, 2, 0, 0);
      if (effect) {
        i32 level = i8(severity), duration = 1000, argument = -1;
        if (level >= 1 && level <= 3) {
          duration = DsTrackPhaseThreeDuration[level - 1];
          argument = DsTrackPhaseThreeArgument[level - 1];
        }
        if (duration && argument)
          static_cast<SpEarthquakeView *>(effect)->DoEarthquakeEffect(level, duration, argument);
      }
    }
    break;
  case 3:
    if (ticks >= phase_durations[2]) {
      phase = 4;
      PostPhase(4);
      manager->box->Event(kAuEventAmbience, 61, 3, 0, 0);
    }
    break;
  case 4:
    if (ticks >= phase_durations[3]) {
      phase = 5;
      total_count = 0;
      BuildPath();
      if (i32(manager->active_count) <= 1) {
        manager->box->Event(kAuEventPlayEffect, kAuEffectDisasterEarthquake, current.x, current.z, 0);
        announcement = true;
      }
      PostPhase(phase);
      CtDate today;
      manager->city->GetSimulationClock()->GetCurrentClockDate(&today);
      date = today.serial;
      if (effect) {
        i32 level = i8(severity), duration = 1000, argument = -1;
        if (level >= 1 && level <= 3) {
          duration = DsTrackPhaseFiveDuration[level - 1];
          argument = DsTrackPhaseFiveArgument[level - 1];
        }
        if (duration && argument)
          static_cast<SpEarthquakeView *>(effect)->DoEarthquakeEffect(level, duration, argument);
      } else
        phase = 6;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case 6:
    manager->city->LockGraphics();
    {
      DsCursorScope cursor(14);
      DsGetCellMap()->InvalidateEntireScreen();
      DsGetCityView()->EnablePlot(false);
      DsGetGraphics()->ScreenBuffer()->DisableFlipping(1);
      manager->city->UnlockGraphics();
      RenderSwath(0, 100);
      ScatterTiles();
      manager->RepairZones();
      DsGetGraphics()->ScreenBuffer()->DisableFlipping(-1);
      DsGetCityView()->EnablePlot(true);
    }
    if (manager->active_count == 1) {
      manager->box->Event(kAuEventEffectSlot52, kAuEffectDisasterEarthquake, current.x, current.z, 0);
      announcement = false;
    }
    ClearEffects();
    manager->FinishInstance(this);
    break;
  }
  return true;
}

// Report the history length without an output array, or copy up to the caller's requested count in stored order.
// Count is written before copying and reread during iteration, preserving genuine aliased output behavior.
u32 DsTrackManager::GetRecords(DsTrackRecord *records, u32 *count) const {
  if (!records) {
    *count = history.size;
    return *count;
  }
  u32 available = history.size;
  if (available > *count)
    available = *count;
  *count = available;
  for (u32 i = 0; i < *count; ++i)
    records[i] = history.data[i];
  return *count;
}

// Test whether a first history entry exists, optionally copying that entry. A null output still answers the query.
bool DsTrackManager::GetFirstRecord(DsTrackRecord *record) const {
  if (!history.size)
    return false;
  if (record)
    *record = history.data[0];
  return true;
}

// Copy the nine saved words in their original order through the track-state interface record output. No path storage
// or unused native padding participates in the saved record.
void DsTrackInstance::GetRecord(DsTrackRecord *output) {
  output->date = date;
  output->magnitude = magnitude;
  output->destroyed = destroyed;
  output->cost = cost;
  output->automatic = automatic;
  output->packed_endpoints = packed_endpoints;
  output->packed_location = packed_location;
  output->phase = phase;
  output->phase_ticks = phase_ticks;
}

// Decode saved positions for live initialization, then reread the input record before rebuilding the path. Phase five
// resumes through the original tick call; final magnitude selection reads the resulting live fields.
bool DsTrackInstance::Init(DsTrackManager *owner, const DsTrackRecord *record) {
  date = record->date;
  magnitude = record->magnitude;
  destroyed = record->destroyed;
  cost = record->cost;
  automatic = record->automatic;
  packed_endpoints = record->packed_endpoints;
  packed_location = record->packed_location;
  phase = record->phase;
  phase_ticks = record->phase_ticks;
  u32 original_magnitude = magnitude;
  if (original_magnitude < 10 || phase > 7)
    phase = 1;
  if (phase > 7)
    phase = 1;
  if (phase_ticks > 10000)
    phase_ticks = 0;
  u32 endpoints = packed_endpoints, location = packed_location;
  ScVec3I decoded_start, decoded_end, decoded_current;
  decoded_start = {i32(endpoints >> 24), i32((endpoints >> 16) & 255), 0};
  decoded_end = {i32((endpoints >> 8) & 255), i32(endpoints & 255), 0};
  decoded_current = {i32(location >> 16), i32(location & 65535), 0};
  Init(owner, f32(f64(original_magnitude) * f64(0.1f)), &decoded_start, &decoded_end, &decoded_current, true);
  date = record->date;
  magnitude = record->magnitude;
  automatic = record->automatic;
  packed_endpoints = record->packed_endpoints;
  packed_location = record->packed_location;
  destroyed = record->destroyed;
  cost = record->cost;
  phase = record->phase;
  phase_ticks = record->phase_ticks;
  BuildPath();
  if (record->phase == 5) {
    phase_ticks = phase_durations[3];
    phase = 4;
    Tick(DsDayTick);
  }
  ConfigureMagnitude(f32(f64(magnitude) * f64(0.1f)));
  AdjustWidth(automatic != 0);
  return true;
}

// Write capped history before opening the live-instance archive, retaining each archive's independent failure gate.
// Serialize every matching live instance even when its count header was capped, ignoring individual block-write results.
bool DsTrackManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTrackHistoryRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = history.size > 50000 ? 50000 : history.size;
      if (stream->WriteU32(count) && count) {
        for (u32 index = 0; index < count; ++index) {
          DsTrackRecord record = history.data[index];
          stream->WriteBlock(&record, 36);
        }
      }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTrackInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (i32(active_count) > 50000)
        active_count = 50000;
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next()) {
          if (instance->TypeId() == kDsTrackTypeId) {
            DsTrackRecord record;
            static_cast<DsTrackInstance *>(instance)->GetRecord(&record);
            stream->WriteBlock(&record, 36);
          }
        }
        layer->DeleteDisasterListIterator(iterator);
      }
    }
  }
  return true;
}

i32 DsTrackBorderRadii[3] = {2, 4, 6};

// Visit the bounded vertical border around every cached path point, building that path first when necessary.
u8 DsTrackInstance::ScatterTiles() {
  if (width >= 1) {
    if (!total_count)
      BuildPath();
    i32 radius = DsTrackBorderRadii[width == 1 ? 0 : width == 2 ? 1 : 2];
    i32 columns = i32(manager->city->CellCountX()), rows = i32(manager->city->CellCountZ());
    for (u32 index = 0; index < total_count; ++index) {
      ScPoint point = path[index]; // The second word carries cell Z.
      i32 last_z = point.y + radius;
      if (point.x >= 0 && point.x < columns) {
        for (i32 z = point.y - radius; z <= last_z; ++z) {
          if (z >= 0 && z < rows) {
            if (manager->random.Next(10000) < u32(tuning_64))
              ApplyTile(point.x, z, 1);
            else if (manager->random.Next(10000) < u32(tuning_68))
              ApplyTile(point.x, z, 0);
          }
        }
      }
    }
  }
  return 1;
}

u32 DsTrackMinimumStrength = 25, DsTrackMaximumStrength = 85;

// The binary shares its allocator spinword with this game entry. Runtime allocation replaces that heap machinery;
// this native nonrecursive guard retains the start entry's exclusion without introducing an allocator dependency.
static ScGate DsTrackStartLock;

// Start below the signed active limit, querying the ordinance before endpoint selection and narrowing interpolated
// strength before construction. Ignore Init/event results, increment after the event, and release the entry guard.
bool DsTrackManager::Start(f32 strength, bool automatic) {
  DsTrackStartLock.Lock();
  bool started = false;
  if (i32(active_count) < DsTrackMaximumActive) {
    flag_73 = city->OrdinanceLayer()->QueryOrdinanceOn(kCtOrdinance_EarthquakeRetrofitting);
    ScVec3I start, end, current;
    ChooseEndpoints(&start, &end, &current);
    f32 magnitude = f32(
        (f64(DsTrackMaximumStrength - DsTrackMinimumStrength) * strength + i32(DsTrackMinimumStrength)) * f64(0.1f));
    auto *instance = new DsTrackInstance;
    instance->Init(this, magnitude, &start, &end, &current, automatic);
    box->Event(kAuEventStartDisaster, kAuEffectDisasterEarthquake, 0, 0, 0);
    ++active_count;
    started = true;
  }
  DsTrackStartLock.Unlock();
  return started;
}

// Report the full city rectangle for an initialized moving swath. An inactive receiver writes zeros without
// reaching its manager; city width is queried before height for the two inclusive upper bounds.
bool DsTrackInstance::GetBounds(GeCellRect *output) {
  if (!initialized) {
    output->bottom = 0;
    output->top = 0;
    output->right = 0;
    output->left = 0;
    return false;
  }
  output->left = 0;
  output->top = 0;
  output->right = manager->city->CellCountX() - 1;
  output->bottom = manager->city->CellCountZ() - 1;
  return true;
}

// Interpret the current coordinate words as unsigned fixed values before shifting to cells. Shared outputs therefore retain Z.
bool DsTrackInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = u32(current.x) >> kCtMicroCoordinateShift;
  *output_z = u32(current.z) >> kCtMicroCoordinateShift;
  return true;
}

// Moving-swath instances have no attached building occupant. This leaf is shared with other disaster classes.
ScOccupant *DsTrackInstance::GetOccupant() {
  return nullptr;
}

// Return the borrowed owning manager interface, preserving a null owner. Native C++ supplies its base adjustment.
DsManager *DsTrackInstance::GetManager() {
  return manager;
}

u32 DsTrackInstance::TypeId() {
  return kDsTrackTypeId;
}

u32 DsTrackInstance::GetDestroyedCount() {
  return destroyed;
}

u32 DsTrackInstance::GetCost() {
  return cost;
}

// Advance an active effect to its terminal phase only for that effect's completion message. Other senders,
// message types and phases are acknowledged without changing state; no ownership operations occur here.
bool DsTrackInstance::DoMessage(ScMessage *message) {
  if (message->data1 == reinterpret_cast<uptr>(effect) && message->type == kSpMsg_EarthquakeEnded && phase == 5)
    phase = 6;
  return true;
}

u8 DsTrackInstance::GetSeverity() const {
  return severity;
}

i8 DsTrackInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return phase >= 5 ? levels[distance] : 0;
}

u32 DsTrackInstance::PanicRadius() {
  return phase >= 5 ? 5 : 0;
}

bool DsTrackInstance::IsActive() {
  return phase >= 5;
}

f64 DsTrackInstance::GetMagnitude() const {
  return f64(magnitude) * f64(0.1f);
}

// Copy the current fixed-point position in X/Y/Z order, including the original behavior for aliased outputs.
i32 DsTrackInstance::GetPosition(i32 *x, i32 *y, i32 *z) const {
  *x = current.x;
  *y = current.y;
  *z = current.z;
  return current.z;
}

// Copy the first endpoint before the last through six separate outputs, preserving component and alias ordering.
i32 DsTrackInstance::GetEndpoints(
    i32 *first_x, i32 *first_y, i32 *first_z, i32 *last_x, i32 *last_y, i32 *last_z) const {
  *first_x = start.x;
  *first_y = start.y;
  *first_z = start.z;
  *last_x = end.x;
  *last_y = end.y;
  *last_z = end.z;
  return end.z;
}

i32 DsTrackOuterDeltas[3] = {1, 2, 4}, DsTrackInnerDeltas[3] = {-2, -4, -6};

// Deform the four horizontal bands with the original random cadence, occupant gates and asymmetric height writes.
// Batch terrain updates, then notify its actual change source and synchronously publish the clipped affected rectangle.
u8 DsTrackInstance::RenderSwath(i32, i32) {
  if (!base_count || i8(severity) <= 1)
    return 1;
  i32 columns = i32(manager->city->CellCountX()), rows = i32(manager->city->CellCountZ());
  ScDirtBag *terrain = manager->terrain;
  u8 minimum = terrain->MinAltitudeAllowed(), maximum = terrain->MaxAltitudeAllowed();
  u8 maximum_delta = terrain->MaxAltitudeDeltaAllowed(), sea_level = terrain->GetGlobalSeaLevel();
  ScRect city_bounds = {0, 0, i32(u32(columns) - 1), i32(u32(rows) - 1)};
  ScRect affected = {path[0].x, path[0].y, path[0].x, path[0].y};
  u32 next_crack = u32(manager->random.Range(3, 7));
  u32 tuning = severity == 3 ? 2 : (severity == 2 ? 1 : 0);
  i32 outer_base = DsTrackOuterDeltas[tuning], inner_base = DsTrackInnerDeltas[tuning];
  terrain->ResetChanges();
  terrain->LockUpdates(true);
  if (horizontal) {
    u32 count = base_count;
    for (u32 index = 0; index <= count; ++index) {
      ScPoint point = path[index < count ? index : index - 1];
      i32 x = index < count ? point.x : i32(u32(point.x) + 1);
      i32 bands[4] = {i32(u32(point.y) - 1), point.y, i32(u32(point.y) + 1), i32(u32(point.y) + 2)};
      ScRect strip = {x, bands[0], x, bands[3]};
      i32 outer, inner;
      if (index == next_crack) {
        outer = 1;
        inner = inner_base ? -i32(maximum_delta >> 1) : 0;
      } else if (index < next_crack + 1) {
        outer = manager->random.Range(outer_base, i32(u32(outer_base) + 1));
        inner = inner_base ? manager->random.Range(inner_base, i32(u32(inner_base) + 1)) : 0;
      } else {
        outer = 0;
        inner = inner_base ? -i32(maximum_delta >> 2) : 0;
        next_crack += u32(manager->random.Range(3, 7));
      }
      if (index < count) {
        i8 level = i8(severity);
        if (level < 3) {
          bool blocked = false;
          for (u32 band = 0; band < 4; ++band)
            if (bands[band] >= 0 && bands[band] <= i32(maximum_z) && HasAdjacentOccupant(x, bands[band]))
              blocked = true;
          if (blocked)
            continue;
          if (level >= 2 && bands[0] >= 0)
            for (u32 band = 0; band < 4; ++band)
              if (bands[band] <= i32(maximum_z))
                ClearAdjacentZones(x, bands[band]);
        } else {
          for (u32 band = 0; band < 4; ++band)
            if (bands[band] >= 0 && bands[band] <= i32(maximum_z))
              TallyAdjacentTiles(x, bands[band]);
        }
      }
      affected.Union(&affected, &strip);
      for (u32 band = 0; band < 4; ++band) {
        i32 z = bands[band];
        if (z < 0 || z > rows || HasAdjacentOccupant(x, z))
          continue;
        u8 old_height;
        terrain->GetVertexAltitudeDirt(u32(x), u32(z), &old_height);
        i32 delta = band == 0 || band == 3 ? outer : inner;
        i32 height = i32(u32(old_height) + u32(delta));
        if (height > maximum)
          height = maximum;
        else if (height < minimum)
          height = minimum;
        if (inner && old_height > sea_level && height - 10 <= sea_level) {
          i32 top = i32(u32(z) - 1), bottom = i32(u32(z) + 1);
          if (top < 0)
            top = 0;
          if (bottom > rows)
            bottom = rows;
          terrain->SetWaterTable(u32(x), u32(top), u32(x), u32(bottom), u8(height - 10));
        }
        terrain->SetVertexAltitude(u32(x), u32(z), u8(height + (band == 2 ? 1 : 0)));
      }
    }
  }
  terrain->LockUpdates(false);
  affected.left = i32(u32(affected.left) - 3);
  affected.right = i32(u32(affected.right) + 3);
  affected.top = i32(u32(affected.top) - 3);
  affected.bottom = i32(u32(affected.bottom) + 3);
  affected.IntersectInclusive(&affected, &city_bounds);
  void *queried = nullptr;
  terrain->QueryInterface(IID_CtChangeSource, &queried);
  auto *source = static_cast<TrChangeSource *>(queried);
  source->Notify(u32(affected.left), u32(affected.top), u32(affected.right), u32(affected.bottom));
  source->Release();
  ScMessage message(kTrMsg_TerrainChanged, reinterpret_cast<uptr>(&affected), 0, 0);
  DsGetMessageServer()->MessageSend(message);
  terrain->ResetChanges();
  return 1;
}

// Process only an occupant anchored at the requested cell, optionally spawning an effect before destruction tally.
// Preserve the conditional random draws and release the acquired occupant even when its anchor does not match.
u8 DsTrackInstance::ApplyTile(i32 x, i32 z, u8 destroy) {
  ScOccupant *occupant;
  if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
    u32 origin_x, origin_z;
    occupant->GetLocationInCellCoords(&origin_x, &origin_z);
    if (origin_x == u32(x) && origin_z == u32(z)) {
      u32 flags = occupant->GetGeomFlag();
      u8 size_x = occupant->SizeInCellsX();
      if (destroy) {
        if (!manager->random.Next(50)) {
          if ((flags & kOcGeomPower) && size_x == 1) {
            void *effect_interface;
            if (DsGetCom()->GetClassObject(kSpParticleEffectClassId, IID_SpParticleEffect, &effect_interface)) {
              i32 fixed_x = i32(origin_x << kCtMicroCoordinateShift),
                  fixed_z = i32(origin_z << kCtMicroCoordinateShift);
              u8 altitude;
              manager->terrain->GetVertexAltitude(origin_x, origin_z, &altitude);
              i32 fixed_y = (i32(altitude) + 5) << kCtMicroCoordinateShift;
              auto *particle = static_cast<SpParticleEffect *>(effect_interface);
              particle->Init();
              particle->DoParticleGeneration(fixed_x, fixed_z, fixed_y, 1, 10);
              particle->Release();
            }
          } else if (!(flags & 0x400u) && !(flags & kOcGeomNetwork) && !manager->random.Next(15)) {
            void *effect_interface;
            if (DsGetCom()->GetClassObject(kSpExplosionViewClassId, IID_SpExplosionView, &effect_interface)) {
              i32 fixed_x = i32(origin_x << kCtMicroCoordinateShift),
                  fixed_z = i32(origin_z << kCtMicroCoordinateShift);
              u8 altitude;
              manager->terrain->GetVertexAltitude(origin_x, origin_z, &altitude);
              static_cast<SpExplosionView *>(effect_interface)
                  ->Init(fixed_x, fixed_z, i32(altitude) << kCtMicroCoordinateShift, 3, 2, 0);
              static_cast<SpExplosionView *>(effect_interface)->Release();
            }
          }
        }
        TallyTile(x, z, 1, 0, 1);
      } else if (!(flags & 0x400u)) {
        auto *fire = static_cast<DsFireManager *>(manager->layer->GetDisasterManager(kDsFireTypeId));
        fire->IgniteOccupant(occupant, 0);
      }
    }
    occupant->Release();
  }
  return 0;
}

// Return prior occupancy while applying requested surface and underground destruction in original order.
// Surface refusal blocks later work; underground refusals do not alter the cached occupancy result or block zoning.
u8 DsTrackInstance::TallyTile(i32 x, i32 z, i32 first_flag, i32 second_flag, u8 special) {
  u8 occupied = manager->occupants->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_one->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_two->DoesOccupantExistAtLocation(u32(x), u32(z));
  if (u8(first_flag)) {
    u32 charge = 0;
    bool surface_ok = true;
    ScOccupant *occupant;
    if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      surface_ok = manager->layer->DestroyOccupant(occupant, special ? kDsDamageRubble : kDsDamageRemove, &charge);
      if (surface_ok) {
        ++destroyed;
        cost += charge;
      }
      occupant->Release();
    }
    if (!surface_ok)
      return occupied;
    GeBounds first, second;
    for (u32 index = 0; index < 2; ++index) {
      ScOccupantManager *population = index ? manager->underground_two : manager->underground_one;
      if (population->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
        GeBounds *bounds = index ? &second : &first, *affected = index ? &first : &second;
        *affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
        i32 fixed_x = i32(u32(x) << kCtMicroCoordinateShift), fixed_z = i32(u32(z) << kCtMicroCoordinateShift);
        *bounds = {{fixed_x, fixed_z, 0}, {fixed_x, fixed_z, 0}};
        auto *demolition = static_cast<GeDemolitionView *>(manager->demolition);
        if (demolition->DemolishOccupants(index ? manager->underground_two : manager->underground_one, bounds, nullptr,
                0, true, &charge, affected)) {
          charge = manager->layer->GetDemolitionCost(occupant);
          ++destroyed;
          cost += charge;
        }
        occupant->Release();
      }
    }
  }
  if (u8(second_flag) && (u8(first_flag) || !occupied)) {
    u8 zone;
    manager->zones->GetValue(u32(x), u32(z), &zone);
    if (zone) {
      u8 cleared = 0;
      manager->zones->SetValue(u32(x), u32(z), &cleared);
    }
  }
  return occupied;
}

// Scan columns then rows, leaving occupied or unzoned cells untouched and sampling every corner of other cells.
// Clear a zone when any corner differs, requerying row count per column and preserving the original count result.
i32 DsTrackManager::RepairZones() {
  i32 repaired = 0;
  u32 columns = city->CellCountX();
  for (u32 x = 0; x < columns; ++x) {
    u32 rows = city->CellCountZ();
    for (u32 z = 0; z < rows; ++z) {
      if (!occupants->DoesOccupantExistAtLocation(x, z)) {
        u8 zone;
        zones->GetValue(x, z, &zone);
        if (zone) {
          u8 first, second, third, fourth;
          terrain->GetVertexAltitude(x, z, &first);
          terrain->GetVertexAltitude(x + 1, z, &second);
          terrain->GetVertexAltitude(x, z + 1, &third);
          terrain->GetVertexAltitude(x + 1, z + 1, &fourth);
          if (first != second || first != third || first != fourth) {
            u8 empty = 0;
            repaired = i32(u32(repaired) + 1);
            zones->SetValue(x, z, &empty);
          }
        }
      }
    }
  }
  return repaired;
}
