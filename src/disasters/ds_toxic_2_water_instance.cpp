#include "disasters/ds_toxic.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"
#include "disasters/oc_disaster_building.h"
#include "disasters/oc_disaster_occupant.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "terrain/api.h"
#include "ui/api.h"

DsWaterInstance::DsWaterInstance()
    : initialized(false), timing{0, 0, 0, 0, 30}, references(0), announcement(false), flag_41(0),
      effects{kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId,
          kApInvalidNotificationId},
      severity(1), radius(0), position{0, 0, 0}, word_84(0), manager(nullptr), effect(nullptr), state{} {
}

DsWaterInstance::~DsWaterInstance() {
  Shutdown();
}

// Expose the actual common or private state view and retain it through the primary identity. Unsupported IDs clear the
// output, including the unadvertised message-target identity.
bool DsWaterInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsWaterState, DsWaterState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsWaterInstance::AddRef() {
  return ++references;
}

// Delete at zero or one references while preserving the final count through teardown; larger counts decrement first.
u32 DsWaterInstance::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Surround the live reported cell with the original five-cell radius and clip to captured city dimensions.
// An inactive instance clears the caller's rectangle without reading uninitialized dimensions.
bool DsWaterInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    u32 x, z;
    GetLocation(&x, &z);
    output->left = x - 5;
    if (x < 5)
      output->left = 0;
    output->top = z - 5;
    if (z < 5)
      output->top = 0;
    output->right = x + 5;
    if (x + 5 > columns - 1)
      output->right = columns - 1;
    output->bottom = z + 5;
    if (z + 5 > rows - 1)
      output->bottom = rows - 1;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

// Return the shared null occupant result; this moving effect does not own one particular building.
ScOccupant *DsWaterInstance::GetOccupant() {
  return nullptr;
}

// Borrow the owner's common manager identity, retaining null before the native base conversion.
DsManager *DsWaterInstance::GetManager() {
  return manager;
}

u32 DsWaterInstance::TypeId() {
  return kDsWaterTypeId;
}

u32 DsWaterInstance::GetDestroyedCount() {
  return state.destroyed;
}

u32 DsWaterInstance::GetCost() {
  return state.cost;
}

f64 DsWaterInstance::GetMagnitude() const {
  return f64(state.magnitude) * f64(0.1f);
}

u8 DsWaterInstance::GetSeverity() const {
  return severity;
}

void DsWaterInstance::GetRecord(DsWaterRecord *record) {
  *record = state;
}

// Classify the strength at the original strict four/six thresholds and publish severity, width and radius together.
void DsWaterInstance::ConfigureMagnitude(f32 strength) {
  if (strength > 4.0f) {
    if (strength > 6.0f) {
      severity = 3;
      width = 3;
      radius = 60;
    } else {
      severity = 2;
      width = 2;
      radius = 40;
    }
  } else {
    severity = 1;
    width = 1;
    radius = 20;
  }
}

void DsWaterInstance::AdjustWidth(u8 enabled) {
  if (enabled)
    --width;
}

// Reject the five protected terrain operations when their grid-aligned bounds overlap the moving five-cell radius.
static bool DsWaterTerrainFilter(const GeBounds &bounds, u32, i32 operation, void *context) {
  if (operation == 1 || operation == 2 || operation == 4 || operation == 8 || operation == 16) {
    auto *instance = static_cast<DsWaterInstance *>(context);
    i32 x, y, z;
    instance->GetPosition(&x, &y, &z);
    i32 left = i32(u32(bounds.minimum.x) & kCtMicroCellMask), right = i32(u32(bounds.maximum.x) & kCtMicroCellMask);
    i32 top = i32(u32(bounds.minimum.z) & kCtMicroCellMask), bottom = i32(u32(bounds.maximum.z) & kCtMicroCellMask);
    if (right >= i32(u32(x) - 1280) && left <= i32(u32(x) + 1280) && bottom >= i32(u32(z) - 1280) &&
        top <= i32(u32(z) + 1280))
      return false;
  }
  return true;
}

// Bind original dimensions, position and randomized timing before subscribing notices and acquiring the effect controller.
// Register the terrain filter even when effect creation fails; readiness remains published and the full-word result reports it.
i32 DsWaterInstance::Init(DsWaterManager *owner, f32 strength, const ScVec3I *start, u8 automatic) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    columns = owner->city->CellCountX();
    rows = manager->city->CellCountZ();
    position = *start;
    timing[0] = DsWaterFirstDelay;
    timing[1] = DsWaterSecondDelay;
    timing[2] = DsWaterThirdDelay;
    timing[3] = DsWaterAttackDelay;
    timing[4] = u32(manager->random.SqrtRange(i32((8u * DsWaterDuration) / 10u), i32((10u * DsWaterDuration) >> 3)));
    state.serial = 0;
    f64 rounded = f64(strength) * 10.0 + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    state.magnitude = u32(bits) + kScDoubleIntegerLowWordBias;
    state.narrow_width = manager->flag_73;
    state.destroyed = 0;
    state.packed_location = u32(u16(position.z)) | (u32(u16(position.x)) << 16);
    state.cost = 0;
    state.phase_ticks = 0;
    state.phase = 1;
    if (!automatic) {
      state.phase = 4;
      timing[0] = 0;
      timing[1] = 0;
      timing[2] = 0;
      timing[3] = 0;
    }
    ConfigureMagnitude(f32(f64(state.magnitude) * f64(0.1f)));
    AdjustWidth(state.narrow_width != 0);
    ScMessageServer *server = DsGetMessageServer();
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolEnded);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolCellChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolPathChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolOccupantConsumed);
    if (!effect)
      DsGetCom()->GetClassObject(kSpWhirlpoolSimulatorClassId, IID_SpWhirlpoolSimulator, &effect);
    if (effect) {
      manager->layer->DisasterInstanceChange(this, 0);
      if (!automatic)
        Tick(DsDayTick);
    }
    manager->terrain->InstallFunctionFilter(DsWaterTerrainFilter, this, true);
  }
  return effect != nullptr;
}

// Temporarily repair the saved phase for live initialization, then reload all raw saved words in their original order.
// Saved phase five restarts at phase four's deadline and ticks immediately before magnitude and width are reapplied.
u8 DsWaterInstance::Init(DsWaterManager *owner, const DsWaterRecord *record) {
  state = *record;
  u32 magnitude = state.magnitude;
  if (magnitude < 10 || state.phase > 7)
    state.phase = 1;
  if (state.phase > 7)
    state.phase = 1;
  if (state.phase_ticks > 10000)
    state.phase_ticks = 0;
  ScVec3I start = {i32(state.packed_location >> 16), i32(u16(state.packed_location)), 0};
  Init(owner, f32(f64(magnitude) * f64(0.1f)), &start, 1);
  state.serial = record->serial;
  state.magnitude = record->magnitude;
  state.narrow_width = record->narrow_width;
  state.packed_location = record->packed_location;
  state.destroyed = record->destroyed;
  state.cost = record->cost;
  state.phase = record->phase;
  state.phase_ticks = record->phase_ticks;
  if (record->phase == 5) {
    state.phase_ticks = timing[3];
    state.phase = 4;
    Tick(DsDayTick);
  }
  ConfigureMagnitude(f32(f64(state.magnitude) * f64(0.1f)));
  AdjustWidth(state.narrow_width != 0);
  return 1;
}

// Remove the terrain filter before decrementing manager activity, releasing the controller and ending any announcement.
// Remove all four subscriptions through one captured server, then clear ownership and readiness after callbacks finish.
u8 DsWaterInstance::Shutdown() {
  if (initialized) {
    manager->terrain->InstallFunctionFilter(DsWaterTerrainFilter, this, false);
    manager->InstanceEnded();
    if (effect) {
      static_cast<ScUnknown *>(effect)->Release();
      effect = nullptr;
    }
    if (announcement) {
      manager->box->Event(kAuEventEffectSlot52, kAuEffectDisasterWhirlpool, position.x, position.z, 0);
      announcement = false;
    }
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolEnded);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolCellChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolPathChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_WhirlpoolOccupantConsumed);
    manager = nullptr;
    initialized = false;
  }
  return 1;
}

void DsWaterInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index)
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
}

// Publish localized notices for phases two, three, four and six, retaining the actual String across the synchronous message.
// The terminal random notice deliberately leaves its returned handle untracked; other handles publish after text release.
void DsWaterInstance::PostPhase(u32 phase) {
  u32 resource, *handle = nullptr;
  switch (phase) {
  case 2:
    resource = 112;
    handle = &effects[0];
    break;
  case 3:
    resource = 113;
    handle = &effects[1];
    break;
  case 4:
    resource = 114;
    handle = &effects[2];
    break;
  case 6:
    resource = manager->random.Next(2) + 118;
    break;
  default:
    return;
  }
  auto *text = new ScString;
  DsTextResource content(resource, kRsDisasterNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsDisasterNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (handle)
    *handle = notice.effect;
}

// Report prior occupancy independently of demolition success while tallying surface and both underground removals.
// A refused surface demolition skips underground work and zone clearing; individual underground refusals do not stop later work.
u8 DsWaterInstance::TallyTile(i32 x, i32 z, u8 destroy, u8 clear_zone, u8 special) {
  u8 occupied = manager->occupants->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_one->DoesOccupantExistAtLocation(u32(x), u32(z)) ||
      manager->underground_two->DoesOccupantExistAtLocation(u32(x), u32(z));
  if (destroy) {
    u32 charge = 0;
    bool surface_ok = true;
    ScOccupant *occupant;
    if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      surface_ok = manager->layer->DestroyOccupant(occupant, special ? kDsDamageRubble : kDsDamageRemove, &charge);
      if (surface_ok) {
        ++state.destroyed;
        state.cost += charge;
      }
      occupant->Release();
    }
    if (surface_ok) {
      GeBounds first, second;
      for (u32 index = 0; index < 2; ++index) {
        ScOccupantManager *population = index ? manager->underground_two : manager->underground_one;
        if (population->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
          GeBounds *requested = index ? &second : &first, *affected = index ? &first : &second;
          *affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
          i32 fixed_x = i32(u32(x) << kCtMicroCoordinateShift), fixed_z = i32(u32(z) << kCtMicroCoordinateShift);
          *requested = {{fixed_x, fixed_z, 0}, {fixed_x, fixed_z, 0}};
          auto *demolition = static_cast<GeDemolitionView *>(manager->demolition);
          if (demolition->DemolishOccupants(index ? manager->underground_two : manager->underground_one, requested,
                  nullptr, 0, true, &charge, affected)) {
            charge = manager->layer->GetDemolitionCost(occupant);
            ++state.destroyed;
            state.cost += charge;
          }
          occupant->Release();
        }
      }
    }
    if (clear_zone && surface_ok) {
      u8 zone;
      manager->zones->GetValue(u32(x), u32(z), &zone);
      if (zone) {
        u8 empty = 0;
        manager->zones->SetValue(u32(x), u32(z), &empty);
      }
    }
  } else if (!occupied && clear_zone) {
    u8 zone;
    manager->zones->GetValue(u32(x), u32(z), &zone);
    if (zone) {
      u8 empty = 0;
      manager->zones->SetValue(u32(x), u32(z), &empty);
    }
  }
  return occupied;
}

// Accept only captured city cells containing a retained major-type-two occupant over actual water.
// Release the occupant after the terrain predicate, including nonmatching major types.
bool DsWaterInstance::IsWaterNetwork(ScOccupantManager *population, u32 x, u32 z) {
  bool result = false;
  if (x < columns && z < rows) {
    ScOccupant *occupant;
    if (population->GetOccupantAtCell(x, z, &occupant)) {
      if (occupant->MajorType() == 2)
        result = manager->terrain->IsWater(x, z);
      occupant->Release();
    }
  }
  return result;
}

// Append the qualifying water-network line using the original two-cell axis probes and positive-X-first priority.
// Reverse the entire existing output after the forward walk, then append the opposite walk without clearing prior points.
u8 DsWaterInstance::BuildPath(u32 x, u32 z, ScArray<ScPointXZ> *points) {
  ScOccupantManager *population = manager->occupants;
  if (!population || !IsWaterNetwork(population, x, z))
    return points->size != 0;
  u32 step_x = 0, step_z = 0;
  if (IsWaterNetwork(population, x + 1, z) && IsWaterNetwork(population, x + 2, z))
    step_x = 1;
  else if (IsWaterNetwork(population, x - 1, z) && IsWaterNetwork(population, x - 2, z))
    step_x = kScBackwardGridStep;
  else if (IsWaterNetwork(population, x, z + 1) && IsWaterNetwork(population, x, z + 2))
    step_z = 1;
  else if (IsWaterNetwork(population, x, z - 1) && IsWaterNetwork(population, x, z - 2))
    step_z = kScBackwardGridStep;
  else {
    *points->Append() = {x, z};
    return 1;
  }
  for (u32 column = x, row = z; IsWaterNetwork(population, column, row); column += step_x, row += step_z)
    *points->Append() = {column, row};
  for (u32 first = 0, last = points->size; first < last; ++first) {
    --last;
    ScPointXZ point = points->data[first];
    points->data[first] = points->data[last];
    points->data[last] = point;
  }
  for (u32 column = x - step_x, row = z - step_z; IsWaterNetwork(population, column, row);
      column -= step_x, row -= step_z)
    *points->Append() = {column, row};
  return points->size != 0;
}

// Collect up to sixty-four retained animated occupants from the original fixed-unit volume and append their positions.
// Preserve endpoint order, the one-cell upper expansion and the caller's existing output; release each occupant after append.
u8 DsWaterInstance::CollectPathOccupants(const ScRect *bounds, ScArray<ScPointXZ> *points) {
  GeBounds volume = {
      {bounds->left, bounds->top, 0}, {i32(u32(bounds->right) + 256), i32(u32(bounds->bottom) + 256), 65536}};
  ScOccupant *occupants[64];
  u32 count = 64;
  OcAnimManager *population = manager->city->AnimOccupantManager();
  if (!population->GetOccupantsInVolume(volume, occupants, &count) || !count)
    return 0;
  for (u32 index = 0; index < count; ++index) {
    ScOccupant *occupant = occupants[index];
    ScPointXZ point;
    occupant->GetLocationInAnimCellCoords(&point.x, &point.z);
    *points->Append() = point;
    occupant->Release();
  }
  return 1;
}

u32 DsWaterInstance::SelectFrame() {
  return manager->random.Next(5);
}

i8 DsWaterInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return state.phase >= 5 ? levels[distance] : 0;
}

u32 DsWaterInstance::PanicRadius() {
  return state.phase >= 5 ? 5 : 0;
}

bool DsWaterInstance::IsActive() {
  return state.phase >= 5;
}

// Read live fixed coordinates from the whirlpool when present, otherwise use the stored position.
// Publish unsigned cell X before Z using the original logical shifts, including before initialization.
bool DsWaterInstance::GetLocation(u32 *x, u32 *z) {
  u32 fixed_z;
  if (effect) {
    i32 live_x, live_z;
    static_cast<SpWhirlpoolSimulator *>(effect)->GetLocation(&live_x, &live_z);
    *x = u32(live_x) >> kCtMicroCoordinateShift;
    fixed_z = u32(live_z);
  } else {
    *x = u32(position.x) >> kCtMicroCoordinateShift;
    fixed_z = u32(position.z);
  }
  *z = fixed_z >> kCtMicroCoordinateShift;
  return true;
}

// Copy live or stored fixed X/Z before publishing the stored altitude and returning that same full signed word.
// Read altitude after the earlier outputs so aliased outputs or synchronous callbacks retain their original order.
i32 DsWaterInstance::GetPosition(i32 *x, i32 *y, i32 *z) const {
  if (effect)
    static_cast<SpWhirlpoolSimulator *>(effect)->GetLocation(x, z);
  else {
    *x = position.x;
    *z = position.z;
  }
  i32 result = position.y;
  *y = result;
  return result;
}

// Scan the original half-open damage square, retaining each preferred occupant through tally and both sprite calls.
// Re-read radius after callbacks, preserve signed cell tests and keep fixed-point updates wrapping by DWORD.
u8 DsWaterInstance::ApplyDamage(i32 x, i32 z) {
  if (manager && manager->occupants) {
    i32 damage_radius = DsWaterDamageRadius, cell_z = i32(u32(z) - u32(damage_radius));
    if (cell_z < i32(u32(damage_radius) + u32(z))) {
      i32 fixed_z = i32((u32(cell_z) << kCtMicroCoordinateShift) + 128);
      do {
        i32 cell_x = i32(u32(x) - u32(damage_radius));
        if (cell_x < i32(u32(damage_radius) + u32(x))) {
          i32 fixed_x = i32((u32(cell_x) << kCtMicroCoordinateShift) + 128);
          do {
            ScOccupant *occupant;
            if (manager->occupants->GetOccupantAtCell(u32(cell_x), u32(cell_z), &occupant)) {
              if (DsWaterPrefersOccupant(occupant) && TallyTile(cell_x, cell_z, 1, 0, 0)) {
                manager->box->Event(kAuEventPlayEffect, kAuEffectWhirlpoolDestroysMarina, cell_x, cell_z, 0);
                void *splash_object, *debris_object;
                if (DsGetCom()->GetClassObject(kSpWaterSplashClassId, IID_SpWaterSplash, &splash_object)) {
                  auto *splash = static_cast<SpWaterSplash *>(splash_object);
                  splash->AddRef();
                  u16 altitude = occupant->GetAltAnimCells();
                  splash->Init(fixed_x, fixed_z, altitude, 0, 0, 0);
                  splash->Release();
                }
                if (DsGetCom()->GetClassObject(kSpWhirlpoolDebrisClassId, IID_SpWhirlpoolDebris, &debris_object)) {
                  auto *debris = static_cast<SpWhirlpoolDebris *>(debris_object);
                  debris->AddRef();
                  debris->SetTimeToLive(DsWaterDebrisLifetime);
                  u8 altitude;
                  manager->terrain->GetVertexAltitude(u32(fixed_x / 256), u32(fixed_z / 256), &altitude);
                  debris->Init(fixed_x, fixed_z, i32(u32(altitude) << kCtMicroCoordinateShift), 8);
                  debris->Release();
                }
              }
              occupant->Release();
            }
            damage_radius = DsWaterDamageRadius;
            fixed_x = i32(u32(fixed_x) + 256);
            cell_x = i32(u32(cell_x) + 1);
          } while (cell_x < i32(u32(DsWaterDamageRadius) + u32(x)));
        }
        fixed_z = i32(u32(fixed_z) + 256);
        cell_z = i32(u32(cell_z) + 1);
      } while (cell_z < i32(u32(damage_radius) + u32(z)));
    }
  }
  return 1;
}

// Render the damaged water path with the original splash variants, random sequence and ten-object debris cap.
// Preserve the raw midpoint fallback, first-clone ownership and reused altitude of the optional third clone.
u8 DsWaterInstance::RenderPath(u32 x, u32 z) {
  ScArray<ScPointXZ> path;
  if (BuildPath(x, z, &path)) {
    u32 path_count = path.size;
    if (path_count) {
      ScRect bounds{i32(path.data[0].x << kCtMicroCoordinateShift), i32(path.data[0].z << kCtMicroCoordinateShift),
          i32(path.data[path_count - 1].x << kCtMicroCoordinateShift),
          i32(path.data[path_count - 1].z << kCtMicroCoordinateShift)};
      ScArray<ScPointXZ> objects;
      CollectPathOccupants(&bounds, &objects);
      if (TallyTile(i32(x), i32(z), 1, 0, 0)) {
        manager->box->Event(kAuEventPlayEffect, kAuEffectWhirlpoolDestroysBridge, i32(x), i32(z), 0);
        if (!objects.size)
          *objects.Append() = path.data[path_count >> 1];
        u32 object_count = objects.size, maximum_objects = object_count > 10 ? 10 : object_count;
        ScCom *com = DsGetCom();
        void *prototype_object;
        if (object_count && com->GetClassObject(kSpWhirlpoolDebrisClassId, IID_SpWhirlpoolDebris, &prototype_object)) {
          auto *prototype = static_cast<SpWhirlpoolDebris *>(prototype_object);
          u32 splash_count = path_count < 3 ? path_count : path_count >> 2, path_index = 2;
          for (u32 i = 0; i < splash_count; ++i, path_index += 4) {
            void *splash_object;
            if (path_index < path_count &&
                com->GetClassObject(kSpWaterSplashClassId, IID_SpWaterSplash, &splash_object)) {
              u32 kind = manager->random.Next(3);
              i32 fixed_x = i32((path.data[path_index].x << kCtMicroCoordinateShift) + 128);
              i32 fixed_z = i32((path.data[path_index].z << kCtMicroCoordinateShift) + 128);
              u8 altitude;
              manager->terrain->GetVertexAltitude(u32(fixed_x / 256), u32(fixed_z / 256), &altitude);
              auto *splash = static_cast<SpWaterSplash *>(splash_object);
              splash->AddRef();
              i32 first = 0, last = 0;
              i8 frames = 0;
              if (kind == 1) {
                first = kSpTenFrameSplashColorBase;
                last = kSpTenFrameSplashMaskBase;
                frames = 10;
              } else if (kind) {
                first = kSpNineFrameSplashColorBase;
                last = kSpNineFrameSplashMaskBase;
                frames = 9;
              }
              splash->Init(fixed_x, fixed_z, i32(u32(altitude) << kCtMicroCoordinateShift), first, last, frames);
              splash->Release();
            }
          }
          i32 first_x = i32(objects.data[0].x), first_z = i32(objects.data[0].z);
          SpWhirlpoolDebris *first = prototype->MakeNewWhirlpoolDebris();
          if (first) {
            first->SetTimeToLive(DsWaterDebrisLifetime);
            u32 frame = SelectFrame();
            u8 altitude;
            manager->terrain->GetVertexAltitude(u32(first_x / 256), u32(first_z / 256), &altitude);
            first->Init(first_x, first_z, i32(u32(altitude) << kCtMicroCoordinateShift), i32(frame));
            u32 chosen = manager->random.Next(path_count);
            ScPointXZ *point = &path.data[chosen];
            SpWhirlpoolDebris *second = prototype->MakeNewWhirlpoolDebris();
            second->AddRef();
            second->SetTimeToLive(DsWaterDebrisLifetime);
            frame = manager->random.Next(5);
            i32 second_x = i32((point->x << kCtMicroCoordinateShift) + 128),
                second_z = i32((point->z << kCtMicroCoordinateShift) + 128);
            manager->terrain->GetVertexAltitude(u32(second_x / 256), u32(second_z / 256), &altitude);
            i32 secondary_height = i32(u32(altitude) << kCtMicroCoordinateShift);
            second->Init(second_x, second_z, secondary_height, i32(frame));
            second->Release();
            if (object_count > 5) {
              chosen = manager->random.Next(path_count);
              point = &path.data[chosen];
              SpWhirlpoolDebris *third = prototype->MakeNewWhirlpoolDebris();
              third->AddRef();
              third->SetTimeToLive(DsWaterDebrisLifetime);
              frame = manager->random.Next(5);
              third->Init(i32((point->x << kCtMicroCoordinateShift) + 128),
                  i32((point->z << kCtMicroCoordinateShift) + 128), secondary_height, i32(frame));
              third->Release();
            }
            for (u32 i = 1; i < maximum_objects; ++i) {
              i32 fixed_x = i32(objects.data[i].x), fixed_z = i32(objects.data[i].z);
              i32 cell_x = fixed_x / 256, cell_z = fixed_z / 256;
              if (manager->terrain->IsWater(u32(cell_x), u32(cell_z))) {
                SpWhirlpoolDebris *debris = prototype->MakeNewWhirlpoolDebris();
                if (debris) {
                  debris->AddRef();
                  debris->SetTimeToLive(DsWaterDebrisLifetime);
                  frame = SelectFrame();
                  manager->terrain->GetVertexAltitude(u32(cell_x), u32(cell_z), &altitude);
                  i32 height = i32(u32(altitude) << kCtMicroCoordinateShift);
                  debris->Init(fixed_x, fixed_z, height, i32(frame));
                  debris->Release();
                  void *splash_object;
                  if (DsGetCom()->GetClassObject(kSpWaterSplashClassId, IID_SpWaterSplash, &splash_object)) {
                    u32 kind = manager->random.Next(3);
                    auto *splash = static_cast<SpWaterSplash *>(splash_object);
                    splash->AddRef();
                    i32 first_art = 0, last_art = 0;
                    i8 frames = 0;
                    if (kind == 1) {
                      first_art = kSpTenFrameSplashColorBase;
                      last_art = kSpTenFrameSplashMaskBase;
                      frames = 10;
                    } else if (kind) {
                      first_art = kSpNineFrameSplashColorBase;
                      last_art = kSpNineFrameSplashMaskBase;
                      frames = 9;
                    }
                    splash->Init(fixed_x, fixed_z, height, first_art, last_art, frames);
                    splash->Release();
                  }
                }
              }
            }
          }
          prototype->Release();
        }
      }
    }
  }
  return 1;
}

// Dispatch only messages from the current whirlpool, preserving release publication and captured damage coordinates.
// Update camera visibility after damage callbacks and report signed cell coordinates for the live-location notice.
bool DsWaterInstance::DoMessage(ScMessage *message) {
  auto *simulator = static_cast<SpWhirlpoolSimulator *>(effect);
  if (simulator && message->data1 == uptr(simulator)) {
    switch (message->type) {
    case kSpMsg_WhirlpoolEnded:
      simulator->Release();
      effect = nullptr;
      break;
    case kSpMsg_WhirlpoolCellChanged: {
      u32 x = u32(message->data2), z = u32(message->data3);
      ApplyDamage(i32(x), i32(z));
      u8 visible = u8(DsController::IsInVisibleRect(x, z));
      if (flag_41 != visible) {
        flag_41 = visible;
        manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterWhirlpool, 0, visible, 0);
      }
      break;
    }
    case kSpMsg_WhirlpoolPathChanged:
      RenderPath(u32(message->data2), u32(message->data3));
      break;
    case kSpMsg_WhirlpoolOccupantConsumed: {
      i32 x, z;
      simulator->GetLocation(&x, &z);
      manager->box->Event(kAuEventPlayEffect, kAuEffectWhirlpoolSucksUpBoat, x / 256, z / 256, 0);
      break;
    }
    }
  }
  return true;
}

// Advance the saved phase clock, publishing notices and activating the real whirlpool at its attack deadline.
// Preserve unsigned deadlines, callback-sensitive field reads and the original delayed retirement after fading.
bool DsWaterInstance::Tick(DsTickType) {
  u32 phase = state.phase, ticks = ++state.phase_ticks;
  switch (phase) {
  case 1:
  case 2:
  case 3:
    if (ticks >= timing[phase - 1]) {
      state.phase = phase + 1;
      PostPhase(phase + 1);
    }
    break;
  case 4:
    if (ticks >= timing[3]) {
      state.phase = 5;
      if (i32(manager->active_count) <= 1) {
        manager->box->Event(kAuEventPlayEffect, kAuEffectDisasterWhirlpool, position.x, position.z, 0);
        manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterWhirlpool, 0, 75, 0);
        announcement = true;
      }
      PostPhase(state.phase);
      CtDate date;
      manager->city->GetSimulationClock()->GetCurrentClockDate(&date);
      state.serial = date.serial;
      if (effect) {
        f32 speed = f32(f64(DsWaterSpeedTimes1000) * f64(0.001f));
        ScVec3F start{f32(position.x), f32(position.y), f32(position.z)};
        static_cast<SpWhirlpoolSimulator *>(effect)->Init(start, i32(i8(severity)), 0, speed);
        static_cast<SpWhirlpoolSimulator *>(effect)->SetMicrosecondsBetweenFrames(DsWaterFrameMicroseconds);
        static_cast<SpWhirlpoolSimulator *>(effect)->SetObjectFastestSpeedTimes100000000(
            DsWaterFastestSpeedTimes100000000);
        static_cast<SpWhirlpoolSimulator *>(effect)->SetOuterGravityDistance(DsWaterOuterGravityDistance);
      } else
        state.phase = 6;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case 5:
    if (ticks > timing[4]) {
      auto *simulator = static_cast<SpWhirlpoolSimulator *>(effect);
      state.phase = 6;
      if (simulator)
        simulator->DoFade(0, 0);
    }
    break;
  case 6:
    if (!effect || ticks > timing[3] + 600u) {
      PostPhase(6);
      ClearEffects();
      manager->FinishInstance(this);
    }
    break;
  }
  return true;
}

namespace DsDisaster {

OcBuilding::OcBuilding() : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

OcBuilding::~OcBuilding() {
  ScRelease(&agent_type);
}

// Clear the output and acquire either building or city-agent identity, delegating other IDs to the occupant view.
bool OcBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return OcOccupant::QueryInterface(iid, out);
}

u32 OcBuilding::AddRef() {
  return OcOccupant::AddRef();
}

u32 OcBuilding::Release() {
  return OcOccupant::Release();
}

bool OcBuilding::Init(RsKey *key) {
  return OcOccupant::Init(key);
}

// Bind the occupant attributes before clearing borrowed caches and acquiring the default agent type.
// If the occupant binding fails, invoke the building's virtual shutdown again and report failure.
bool OcBuilding::Init(GeAttrib *value) {
  if (!OcOccupant::Init(value)) {
    Shutdown();
    return false;
  }
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  if (!agent_type) {
    agent_type = GetBaseAttribData()->CityAgentType();
    if (agent_type)
      agent_type->AddRef();
  }
  return true;
}

// Restore the occupant and required occupancy state before applying optional name and construction date fields.
bool OcBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcOccupant::Init(city, info) || !info->HasLayerData1())
    return false;
  u32 state;
  if (!info->GetLayerData1(&state))
    return false;
  SetOccupancyStateFlag(state);
  if (info->HasNameField()) {
    ScString name;
    if (info->GetNameField(&name))
      SetName(name);
  }
  if (info->HasSimCreateDate()) {
    i32 year = 0, day = 0;
    if (info->GetSimCreateDate(&year, &day)) {
      EcDate date(day, year);
      SetBirthdate(date);
    }
  }
  return true;
}

// Save occupant data, then optional changed name and construction date, and finally the live occupancy word.
bool OcBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString default_name;
      if (attributes && attributes->GetName(&default_name) && !name.IsEqual(&default_name))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    EcDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.DayOfYear(), year = date.Year();
      info->SetSimCreateDate(year, day);
    }
  }
  info->SetLayerData1(occupancy_flags);
  return true;
}

// Release the retained agent type and clear borrowed caches before the occupant performs its own cleanup.
void OcBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  OcOccupant::Shutdown();
}

bool OcBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return OcOccupant::DoQueryInfo(message, sender);
}

// Borrow the occupant subobject without acquiring another reference.
ScOccupant *OcBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the city-agent subobject without acquiring another reference.
CtAgent *OcBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool OcBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 OcBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool OcBuilding::IsAgentLocking() {
  return true;
}

u32 OcBuilding::TypeId() const {
  return OcOccupant::TypeId();
}

// Borrow the retained agent type without changing its reference count.
CtAgentType *OcBuilding::AgentType() {
  return agent_type;
}

bool OcBuilding::SetAgentType(CtAgentType *) {
  return false;
}

bool OcBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

u32 OcBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

bool OcBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (u32(mask) & occupancy_flags) != 0;
}

u32 OcBuilding::GetGeomFlag() {
  return 260;
}

u32 OcBuilding::GetMemUse() const {
  return 44;
}

// Borrow the generic occupant attributes without consulting the building's city-specific cache.
GeAttrib *OcBuilding::GetBaseAttribData() {
  return OcOccupant::GetAttribData();
}

// Cache the city's default attributes for nonactive occupancy states, falling back to the generic occupant binding.
GeAttrib *OcBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = DsGetCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = OcOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before calling the building's indexed sprite-definition getter.
SpDefinition *OcBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return OcBuilding::GetSpriteAttrib(layer);
}

// Cache the city's default sprite for nonactive states and otherwise use the occupant's matching definition.
SpDefinition *OcBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = OcOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = DsGetCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = OcOccupant::GetSpriteAttrib();
  return cached_sprite;
}

// Query the specialized building attributes, preserve their power byte, and release the acquired interface.
u8 OcBuilding::PowerRequired() {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  void *building;
  if (!value->QueryInterface(IID_GeBuildingType, &building))
    return 0;
  u8 power = static_cast<GeBuildingType *>(building)->PowerRequired();
  static_cast<GeBuildingType *>(building)->Release();
  return power;
}

bool OcBuilding::SetName(const ScStringView &) {
  return false;
}

bool OcBuilding::IsNameChangeable() {
  return false;
}

bool OcBuilding::HasBirthdate() const {
  return false;
}

bool OcBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool OcBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// Replace the occupancy word with the supplied low byte. A changed value dispatches the real sprite-refresh hook.
void OcBuilding::SetOccupancyState(u32 state) {
  u32 value = u8(state);
  if (occupancy_flags != value) {
    occupancy_flags = value;
    OnOccupancyStateChanged();
  }
}

// Add all requested occupancy bits. Refresh only when the word changes and the mask touches its low byte.
void OcBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

// Clear the requested occupancy bits. High-byte-only changes retain the original sprite instance.
void OcBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

// Refresh the live sprite under the city graphics lock after invalidating the cached attributes.
// Capture the selected definition across destruction callbacks, then apply the current packed rotation on creation.
void OcBuilding::OnOccupancyStateChanged() {
  ScLock *lock = DsGetCity()->GetGraphicsCriticalSection();
  lock->Lock();
  cached_attributes = nullptr;
  SpDefinition *definition = GetSpriteAttrib();
  if (definition) {
    if (sprite) {
      definition->DestroyInstance(&sprite);
      sprite = nullptr;
    }
    if (definition->CreateInstance(&sprite))
      sprite->SetRotationOffset(u8(position >> kOcPositionRotationShift));
  }
  lock->Unlock();
}

// Restore the base occupant, then read and apply the signed occupancy mask even after a primitive failure.
// Zero becomes the active-state bit; preserve the read reply across the virtual state-change callbacks.
u8 OcBuilding::SerialRead(CtCity *city, RsStream *record) {
  u8 result = OcOccupant::SerialRead(city, record);
  if (result) {
    i32 state = 0;
    result = record->ReadI32(&state);
    if (!state)
      state = 1;
    SetOccupancyStateFlag(u32(state));
  }
  return result;
}

// Write the base occupant before the complete signed occupancy word. Return the final stream reply unchanged.
u8 OcBuilding::SerialWrite(CtCity *city, RsStream *record) {
  u8 result = OcOccupant::SerialWrite(city, record);
  if (result)
    result = record->WriteI32(i32(occupancy_flags));
  return result;
}

} // namespace DsDisaster

namespace DsDisaster {

// The original module-local recursive lock protects the packed occupant reference count.
static ScLock OcOccupantLock;

OcOccupant::OcOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

// Retire the generic occupant's retained attribute and sprite chain when attributes were initialized.
// This base cleanup preserves the original release order and leaves other packed fields intact.
OcOccupant::~OcOccupant() {
  if (attributes) {
    attributes->Release();
    attributes = nullptr;
    SpSprite *current = sprite;
    while (current) {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    }
    sprite = nullptr;
    flags &= kOcFlagsWithoutSubtypeOrMembership;
  }
}

// Publish the shared primary identity for its four supported interfaces, then retain it virtually. Unsupported
// interface requests clear the output.
bool OcOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the disaster director before incrementing the packed sixteen-bit count under the module lock.
u32 OcOccupant::AddRef() {
  DsGetDirector()->AddRef();
  OcOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  OcOccupantLock.Unlock();
  return count;
}

// Release the director hold while locked, then decrement the live packed count.
// At zero, unlock before virtual shutdown and deletion through the actual destructor.
u32 OcOccupant::Release() {
  OcOccupantLock.ScLock::Lock();
  if (u16(flags)) {
    DsGetDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      OcOccupantLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  OcOccupantLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Retain an existing binding or acquire the requested attribute, invoke the virtual initializer, and release it.
bool OcOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  ApGameInterface *application = DsGetApplication();
  OcAttribCache *cache = application ? application->GetAppOccupantAttribCache() : nullptr;
  GeAttrib *value = nullptr;
  if (!cache->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// On incomplete descriptor input, enable both removal permissions while returning the failed result.
bool OcOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool result = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    result = Init(&key);
    u32 type = 0;
    if (result)
      result = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (result)
    result = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (result)
    result = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (result) {
    u32 simulation = info->GetCanSimRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutSimRemove) | (simulation << kOcSimRemoveShift);
    u32 user = info->GetCanUserRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutUserRemove) | (user << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return result;
}

// Store the occupant's descriptor fields in original order, calling the X dimension getter twice.
bool OcOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  u32 type = TypeId();
  info->SetTypeId(type);
  info->SetLocationInCells(position & kOcPositionCellValueMask,
      (position >> kOcPositionZShift) & kOcPositionCellValueMask, u8(position >> kOcPositionAltitudeShift));
  u32 second = SizeInCellsX();
  u32 first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  info->SetRemoveFlags(u8((flags >> kOcSimRemoveShift) & 1), u8((flags >> kOcUserRemoveShift) & 1));
  return true;
}

bool OcOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool OcOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 OcOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void OcOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void OcOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

u8 OcOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool OcOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void OcOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool OcOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void OcOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

void OcOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool OcOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude into their micro-coordinate outputs.
void OcOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void OcOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 OcOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Replace X, Z and altitude from the supplied micro coordinates while retaining orientation.
void OcOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void OcOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void OcOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool OcOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void OcOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void OcOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void OcOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Borrow the stored occupant attributes without acquiring another reference.
GeAttrib *OcOccupant::GetAttribData() {
  return attributes;
}

// Borrow the stored sprite instance without altering its ownership.
SpSprite *OcOccupant::GetSpriteInst() {
  return sprite;
}

bool OcOccupant::DoMessage(ScMessage *) {
  return false;
}

u32 OcOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

bool OcOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 OcOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 OcOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 OcOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 OcOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void OcOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 OcOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u8 OcOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

// Borrow the default sprite definition from generic attributes without acquiring it.
SpDefinition *OcOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *OcOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Compare against the virtual default layer before fetching the virtual instance. Other layers have no instance.
SpSprite *OcOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 OcOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 OcOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 OcOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 OcOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 OcOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << kCtMicroCoordinateShift;
}

u32 OcOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << kCtMicroCoordinateShift;
}

u32 OcOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << kCtMicroCoordinateShift;
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void OcOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u32 OcOccupant::GetMemUse() const {
  return 20;
}

u32 OcOccupant::GetSubTypeData(u32) {
  return 0;
}

void OcOccupant::AnimStart() {
}

void OcOccupant::AnimStop() {
}

i32 OcOccupant::Tick(u32) {
  return 0;
}

bool OcOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Select the sprite definition or city-agent type and retain the borrowed interface before publishing it.
bool OcOccupant::GetSubSystemData(u32 id, void **out) {
  if (!attributes)
    return false;
  ScUnknown *selected;
  if (id == IID_SpDefView)
    selected = static_cast<SpDefView *>(GetSpriteAttrib());
  else if (id == IID_CtAgentType)
    selected = CityAgentType();
  else
    return false;
  if (!selected)
    return false;
  selected->AddRef();
  *out = selected;
  return true;
}

// Borrow the attribute's city-agent type, or report no type when unbound.
CtAgentType *OcOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

// Clear the caller's output and ask the module COM service for the attribute's configured query-window class.
bool OcOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  return DsGetCom()->GetClassObject(class_id, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

u32 OcOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

u8 OcOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

u16 OcOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Write the minimum and inclusive maximum in animation cells using the live virtual dimensions.
void OcOccupant::GetBounds(GeBounds *out) const {
  if (!attributes)
    return;
  u32 value = position;
  out->minimum.x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.altitude =
      i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
  u32 ymax = SizeInCellsY();
  ymax += ((position >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) - 1;
  u32 zmax = SizeInCellsZ();
  zmax += ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
  u32 xmax = SizeInCellsX();
  xmax += (position & kOcPositionCellValueMask) - 1;
  out->maximum.x = i32(xmax << kCtMicroCoordinateShift);
  out->maximum.z = i32(zmax << kCtMicroCoordinateShift);
  out->maximum.altitude = i32(ymax << kCtMicroCoordinateShift);
}

// Write the inclusive cell rectangle, reading each live dimension after the minimum coordinates.
void OcOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

bool OcOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

// Replace the retained attributes before creating the default-layer sprite. Failed creation dispatches live shutdown.
bool OcOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  if (!CreateSprite(GetDefaultLayerFlag(), &sprite)) {
    Shutdown();
    return false;
  }
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Release attributes before destroying the live sprite chain, then clear the original packed-state bits.
void OcOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Replace a changed packed rotation and recreate its sprite without rolling back a failed creation. Return the raw byte one.
u8 OcOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  CreateSprite(GetDefaultLayerFlag(), &sprite);
  return 1;
}

void OcOccupant::SetAltAnimCells(u16 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude & 0xff00) << kOcAltitudeFromMicroShift);
}

// Create the indexed attribute sprite, then apply rotation and live geometry flags in callback order. Preserve producer outputs.
bool OcOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (definition && definition->CreateInstance(out)) {
    (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
    u32 geometry = GetGeomFlag();
    (*out)->SetGeomFlag(geometry);
    return true;
  }
  attributes->Key();
  return false;
}

// Capture each next link before releasing the current sprite. Clear the published head after the final release.
void OcOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (current) {
    do {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    } while (current);
    *out = nullptr;
  }
}

// Read saved fields in their original order and apply virtual mutations. Primitive read replies and key-init results are ignored.
u8 OcOccupant::SerialRead(CtCity *, RsStream *record) {
  RsKey key{};
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 type;
  u8 major, animation, sizeX, sizeZ, sizeY, subtype = 0, mark = 0, membership = 0, simRemove = 0, userRemove = 0;
  record->ReadU32(&type);
  record->ReadU8(&major);
  record->ReadU8(&animation);
  record->ReadU8(&sizeX);
  record->ReadU8(&sizeZ);
  record->ReadU8(&sizeY);
  record->ReadU8(&subtype);
  SetSubTypeInfo(subtype);
  record->ReadU8(&mark);
  SetMarkBit(mark != 0);
  record->ReadU8(&membership);
  SetIsInCityFlag(membership);
  record->ReadU8(&simRemove);
  SetSimRemove(simRemove != 0);
  record->ReadU8(&userRemove);
  SetUserRemove(userRemove != 0);
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  point.x = i32(x << kCtMicroCoordinateShift);
  point.z = i32(z << kCtMicroCoordinateShift);
  point.altitude = i32(altitude << kCtMicroCoordinateShift);
  SetLocation(point);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return 1;
}

// Write the retained attribute key before live virtual occupant fields. Release the live resource publication after key writes.
u8 OcOccupant::SerialWrite(CtCity *, RsStream *record) {
  GeAttrib *value = OcOccupant::GetAttribData();
  void *publication = nullptr;
  if (!value || !value->QueryInterface(IID_RsResource, &publication))
    return 0;
  record->WriteU32(*static_cast<RsResource *>(publication)->Type());
  record->WriteU32(*static_cast<RsResource *>(publication)->Group());
  record->WriteU32(*static_cast<RsResource *>(publication)->Instance());
  static_cast<RsResource *>(publication)->Release();
  publication = nullptr;
  record->WriteU32(TypeId());
  record->WriteU8(MajorType());
  record->WriteU8(AnimType());
  record->WriteU8(SizeInCellsX());
  record->WriteU8(SizeInCellsZ());
  record->WriteU8(SizeInCellsY());
  record->WriteU8(SubTypeInfo());
  record->WriteU8(u8(GetMarkBit()));
  record->WriteU8(u8(GetIsInCityFlag()));
  record->WriteU8(u8(CanSimRemove()));
  record->WriteU8(u8(CanUserRemove()));
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GetLocation(&point);
  record->WriteU32(u32(point.x >> kCtMicroCoordinateShift));
  record->WriteU32(u32(point.z >> kCtMicroCoordinateShift));
  record->WriteU32(u32(point.altitude >> kCtMicroCoordinateShift));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return 1;
}

// Route recognized query messages through the acquired window or query identity. Release the live output afterward and return false.
bool OcOccupant::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  u32 route, iid;
  if (message->type == kWnMsg_QueryOpened) {
    switch (u32(message->data1)) {
    case kUiQueryTagDialog:
      route = 0;
      iid = 1401;
      break;
    case kUiQueryTagStandard:
      route = 1;
      iid = 1411;
      break;
    case kUiQueryTagDebug:
      route = 2;
      iid = 1411;
      break;
    default:
      return false;
    }
  } else if (message->type == kWnMsg_QueryClosed && u32(message->data1) == kUiQueryTagDialog) {
    route = 3;
    iid = 1401;
  } else
    return false;
  void *selected = nullptr;
  if (!source || !source->QueryInterface(iid, &selected))
    return false;
  switch (route) {
  case 0:
    DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
    break;
  case 1:
    DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
    break;
  case 2:
    DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
    break;
  case 3:
    DoExitQueryWindow(message, static_cast<UiQueryWindow *>(selected));
    break;
  }
  static_cast<ScUnknown *>(selected)->Release();
  return false;
}

u8 OcOccupant::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Submit a nonempty override architect name with the localized module label and original row flags. Empty names leave the query unchanged.
u8 OcOccupant::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  OcOverride *overrideAttrib = value->GetOverRideAttrib();
  if (!overrideAttrib)
    return 0;
  ScString architect;
  overrideAttrib->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  // Build the independent row label before retiring its temporary resource and intermediate copies.
  ScString label = []() {
    DsTextResource text(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString copy = text.GetText();
    ScString intermediate(copy);
    return ScString(intermediate);
  }();
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

u8 OcOccupant::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u8 OcOccupant::DoExitQueryWindow(ScMessage *, UiQueryWindow *) {
  return 0;
}

} // namespace DsDisaster
