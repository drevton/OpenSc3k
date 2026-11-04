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

// ScMessageServer routes these subscriptions to the corresponding toxic-cloud and UFO instance handlers.
// They retain the handlers' ordinary fallback paths; their notification purposes remain unresolved.
enum DsUnresolvedToxicUfoMessage : u32 {
  kDsMsg_UnresolvedToxicNotification = 0x247e319d,
  kDsMsg_UnresolvedUfoNotification = 0x03092d3c,
};

// The classifier returns these mutually exclusive buckets after walking the occupant's agent ancestors.
// Unrecognized occupants remain in the low-tech bucket; callers retain their additional geometry exclusions.
enum DsUfoTargetKind : i32 {
  kDsUfoTargetLowTech = 0,
  kDsUfoTargetLandmark = 1,
  kDsUfoTargetSpaceport = 2,
  kDsUfoTargetHighTech = 3,
  kDsUfoTargetPower = 4,
};

u32 DsToxicMaximumActive = 1;
f32 DsToxicMinimumSpeed = 0.004269999917596579f, DsToxicMaximumSpeed = 0.00853000022470951f;
u32 DsToxicMinimumClouds = 10;
u32 DsToxicMaximumClouds = 20;
u32 DsToxicFirstNoticeTime = 0;
u32 DsToxicSecondNoticeTime = 20;
u32 DsToxicThirdNoticeTime = 40;
u32 DsToxicVisibleTime = 80;
u32 DsToxicMinimumHeight = 8960;
u32 DsToxicMaximumHeight = 11520;
u32 DsToxicMinimumDuration = 20;
u32 DsToxicMaximumDuration = 40;
u32 DsToxicAverageInterval = 2;
u32 DsToxicPollutionWeight = 200;
u32 DsToxicCleanIndustryWeight = 30;
u32 DsToxicPollutantFeeWeight = 30;
u32 DsToxicWasteDisposalWeight = 30;
u32 DsToxicWastePlantWeight = 60;
u32 DsToxicThreshold = 160;
u32 DsToxicScorePerLevel = 20;
u32 DsToxicMinimumRainStart = 5;
u32 DsToxicMaximumRainStart = 10;
u32 DsToxicMinimumRainDuration = 10;
u32 DsToxicMaximumRainDuration = 30;
u32 DsToxicMinimumRainGap = 15;
u32 DsToxicAbandonRadius = 6;
u32 DsToxicDestructionRadius = 3;
u32 DsToxicDestructionChance = 50;
u32 DsToxicAbandonChance = 50;

// Load toxic-cloud tunables in original order through the real loose/package reader, retaining missing values.
// Preserve conditional cloud clamps, unconditional rain/score repairs and the original unit conversions.
bool DsToxicManager::StaticInit() {
  DsIni reader;
  ScString path, value;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  const char *keys[] = {"MinCloudsPerInstance", "MaxCloudsPerInstance", "MinToxicCloudSpeed", "MaxToxicCloudSpeed",
      "TimeBeforeFirstPremonition", "TimeBeforeSecondPremonition", "TimeBeforeThirdPremonition",
      "TimeBeforeToxicCloudVisible", "MinHeightAboveTerrain", "MaxHeightAboveTerrain", "MinCloudDuration",
      "MaxCloudDuration", "AverageDurationBetweenOccurrences", "WeightForPollution", "WeightForCleanIndustryOrdinance",
      "WeightForIndPollutantFeeOrdinance", "WeightForWasteDisposalOrdinance", "WeightForEachToxicWastePlant",
      "ThresholdScoreForToxicCloud", "ScorePerLevelIncrement", "MinTimeForRainStart", "MaxTimeForRainStart",
      "MinRainDuration", "MaxRainDuration", "MinGapBetweenRains", "AbandonRadiusIncrementPerLevel",
      "DestructionRadiusIncrementPerLevel", "DestructionLikelihood", "AbandonLikelihood"};
  u32 *targets[] = {&DsToxicMinimumClouds, &DsToxicMaximumClouds, nullptr, nullptr, &DsToxicFirstNoticeTime,
      &DsToxicSecondNoticeTime, &DsToxicThirdNoticeTime, &DsToxicVisibleTime, &DsToxicMinimumHeight,
      &DsToxicMaximumHeight, &DsToxicMinimumDuration, &DsToxicMaximumDuration, &DsToxicAverageInterval,
      &DsToxicPollutionWeight, &DsToxicCleanIndustryWeight, &DsToxicPollutantFeeWeight, &DsToxicWasteDisposalWeight,
      &DsToxicWastePlantWeight, &DsToxicThreshold, &DsToxicScorePerLevel, &DsToxicMinimumRainStart,
      &DsToxicMaximumRainStart, &DsToxicMinimumRainDuration, &DsToxicMaximumRainDuration, &DsToxicMinimumRainGap,
      &DsToxicAbandonRadius, &DsToxicDestructionRadius, &DsToxicDestructionChance, &DsToxicAbandonChance};
  for (u32 index = 0; index < 29; ++index) {
    u8 found;
    {
      ScString key(keys[index]), section("ToxicCloudDisasterTunables");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      u32 parsed = DsParseNumber(value.ToChar());
      if (index == 2)
        DsToxicMinimumSpeed = f32(f64(parsed) * 256.0 * f64(0.000016666667f));
      else if (index == 3)
        DsToxicMaximumSpeed = f32(f64(parsed) * 256.0 * f64(0.000016666667f));
      else {
        if (index == 0 && i32(parsed) < 1)
          parsed = 1;
        else if (index == 8 || index == 9)
          parsed <<= 8;
        else if (index == 10 && !parsed)
          parsed = 10;
        else if (index == 11 && parsed <= DsToxicMinimumDuration)
          parsed = DsToxicMinimumDuration + 1;
        *targets[index] = parsed;
      }
    }
    if (index == 19 && i32(DsToxicScorePerLevel) <= 0)
      DsToxicScorePerLevel = 10;
    else if (index == 21 && DsToxicMaximumRainStart <= DsToxicMinimumRainStart)
      DsToxicMaximumRainStart = DsToxicMinimumRainStart + 1;
    else if (index == 23 && DsToxicMaximumRainDuration <= DsToxicMinimumRainDuration)
      DsToxicMaximumRainDuration = DsToxicMinimumRainDuration + 1;
  }
  return true;
}

static i32 DsToxicDivide(i32 value, i32 divisor) {
  assert(divisor && (value != i32(kScIntegerSignBit) || divisor != -1));
  return value / divisor;
}

// Scale average air pollution by its current maximum, subtract three active ordinance weights in order,
// then add the tracked toxic-waste-plant count using the original wrapped arithmetic.
i32 DsToxicManager::ComputeScore() {
  i32 average = pollution->GetAverageAirValue(), maximum = pollution->GetMaxExpectedAirValue();
  u32 score = u32(DsToxicDivide(i32(u32(average) * DsToxicPollutionWeight), maximum));
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_CleanIndustryAssociation))
    score -= DsToxicCleanIndustryWeight;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_IndustrialPollutionFee))
    score -= DsToxicPollutantFeeWeight;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_IndustrialWasteDisposal))
    score -= DsToxicWasteDisposalWeight;
  i32 count = buildings->CheckOccupantTracking(kCtAgentToxicWastePlant);
  return i32(score + u32(count) * DsToxicWastePlantWeight);
}

// Offer the original unit eligibility score only below the unsigned instance cap and after eighty percent of the delay.
// Preserve the floating threshold's truncation before evaluating the actual pollution-based score.
f64 DsToxicManager::IsTriggerEligible() {
  if (active_count < DsToxicMaximumActive && elapsed >= u32(f64(interval) * f64(0.8f)) &&
      ComputeScore() > i32(DsToxicThreshold))
    return 1.0;
  return 0.0;
}

// Advance the idle delay only when no instance is active, allowing eligible concurrent starts without another delay.
// Derive and cap the signed severity increment from the actual score, then invoke the original automatic start.
bool DsToxicManager::Tick() {
  if (active_count < DsToxicMaximumActive && (active_count || ++elapsed >= interval)) {
    i32 score = ComputeScore();
    if (score > i32(DsToxicThreshold)) {
      i32 severity = DsToxicDivide(i32(u32(score) - DsToxicThreshold), i32(DsToxicScorePerLevel));
      if (severity > 5)
        severity = 5;
      Start(u32(severity), 1);
    }
  }
  return true;
}

bool DsToxicManager::AttemptDisasterStart() {
  return Start(random.Next(5), 0) != 0;
}

bool DsToxicManager::StaticShutdown() {
  return true;
}

void DsToxicManager::ResetInterval() {
  elapsed = 0;
  interval = u32(random.Triangular(0, i32(DsToxicAverageInterval + DsToxicAverageInterval)));
}

// Construct four identities and empty pending storage, seeding the generator between retained services and counters.
// Keep terrain, agent tree, city dimensions and animation dimensions unwritten until city binding.
DsToxicManager::DsToxicManager()
    : initialized(false), layer(nullptr), city(nullptr), occupants(nullptr), random(kScRandomSeedFromClock),
      active_count(0), notice_175_count(0), notice_231_count(0), notice_231_mode_count(0), camera_count(0),
      box(nullptr), pollution(nullptr), ordinances(nullptr), buildings(nullptr), zones(nullptr), elapsed(0),
      interval(0) {
}

DsToxicManager::~DsToxicManager() {
  if (initialized)
    DsToxicManager::Shutdown();
}

// Publish common or toxic-private identity before virtual acquisition, delegating all other IDs to the shared agent.
// The actual message-target base is not independently advertised by this query.
bool DsToxicManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsToxicControl, DsToxicControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsToxicManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsToxicManager::Release() {
  return DsAgent::Release();
}

// Replace city ownership in original order, retaining terrain, type, UI, occupant and simulation layers.
// Register load completion and reset scheduling before readiness, preserving both unreinitialized notice counters.
bool DsToxicManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized)
    Shutdown();
  city = ScAddRefNotNull(retained_city);
  animation_x = city->AnimCellCountX();
  animation_z = city->AnimCellCountZ();
  terrain = ScAddRefNotNull(city->DirtBag());
  agent_types = ScAddRefNotNull(DsGetAgentTree());
  maximum_x = city->CellCountX() - 1;
  maximum_z = city->CellCountZ() - 1;
  box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
  occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  ordinances = ScAddRefNotNull(city->OrdinanceLayer());
  pollution = ScAddRefNotNull(city->PollutionLayer());
  buildings = ScAddRefNotNull(city->BuildingLayer());
  zones = ScAddRefNotNull(city->ZoneLayer());
  layer = ScAddRefNotNull(retained_layer);
  active_count = 0;
  notice_175_count = 0;
  camera_count = 0;
  DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  ResetInterval();
  initialized = true;
  return true;
}

// Remove load completion before releasing retained services in their original non-field order.
// Keep services and readiness published through release callbacks; pending records retain storage and contents.
bool DsToxicManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&zones);
    ScRelease(&buildings);
    ScRelease(&pollution);
    ScRelease(&ordinances);
    ScRelease(&layer);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&city);
    initialized = false;
  }
  return true;
}

// Replace pending cloud records from their eight original scalar fields, stopping after the first refused read.
// Signed negative counts retain the original empty iteration, and incomplete records never enter pending storage.
bool DsToxicManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  pending.Clear();
  DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsToxicRecordGroup, 0});
  RsStream *stream = archive.Stream();
  if (!archive.valid || !stream)
    return false;
  i32 count;
  u8 result = stream->ReadI32(&count);
  for (i32 index = 0; result && index < count; ++index) {
    DsToxicRecord saved;
    result = stream->ReadU32(&saved.x);
    result = result && stream->ReadU32(&saved.z) && stream->ReadI32(&saved.severity) &&
        stream->ReadF32(&saved.velocity.x) && stream->ReadF32(&saved.velocity.y) && stream->ReadF32(&saved.velocity.z) &&
        stream->ReadU32(&saved.cost) && stream->ReadU32(&saved.destroyed);
    if (result)
      *pending.Append() = saved;
  }
  return result != 0;
}

// Count matching live instances before resetting the same iterator and writing their eight original fields.
// Preserve unconditional position/velocity queries and the final extra iterator advance after a refused scalar write.
bool DsToxicManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsToxicRecordGroup, 0}, 1, 0);
  RsStream *stream = archive.Stream();
  if (!archive.valid || !stream)
    return false;
  DsListIterator *iterator = layer->CreateDisasterListIterator();
  if (!iterator)
    return false;
  u32 count = 0;
  while (DsInstance *instance = iterator->Next())
    if (instance->TypeId() == kDsToxicTypeId)
      ++count;
  u8 result = stream->WriteU32(count);
  iterator->Rewind();
  for (DsInstance *base = iterator->Next(); base && result; base = iterator->Next()) {
    if (base->TypeId() == kDsToxicTypeId) {
      auto *instance = static_cast<DsToxicInstance *>(base);
      u32 x, z;
      instance->GetLocation(&x, &z);
      result = stream->WriteU32(x);
      result = result && stream->WriteU32(z) && stream->WriteI32(i32(instance->severity));
      ScVec3F velocity;
      instance->GetVelocity(&velocity);
      result = result && stream->WriteF32(velocity.x) && stream->WriteF32(velocity.y) && stream->WriteF32(velocity.z) &&
          stream->WriteU32(instance->GetCost()) && stream->WriteU32(instance->GetDestroyedCount());
    }
  }
  layer->DeleteDisasterListIterator(iterator);
  return result != 0;
}

// Replay the original captured pending range after loading, then discard its records while retaining capacity.
// Other messages return false and do not alter pending state.
bool DsToxicManager::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityViewReady)
    return false;
  DsToxicRecord *records = pending.data;
  u32 count = pending.size;
  for (u32 index = 0; index < count; ++index)
    Restore(records + index);
  pending.Clear();
  return true;
}

// Restore through the actual private spawn interface, then reread damage totals after initialization callbacks.
DsToxicInstance *DsToxicManager::Restore(const DsToxicRecord *saved) {
  DsToxicMotion *motion =
      Start(saved->x, saved->z, &saved->velocity, u32(saved->severity), saved->cost, saved->destroyed, 0);
  auto *instance = static_cast<DsToxicInstance *>(motion);
  if (instance) {
    instance->cost = saved->cost;
    instance->destroyed = saved->destroyed;
  }
  return instance;
}

// Construct below the unsigned active cap and acquire the reference returned through the real private motion identity.
// Initialization results do not change the returned identity and the original hold is not released here.
DsToxicMotion *DsToxicManager::Start(
    u32 x, u32 z, const ScVec3F *velocity, u32 severity, u32 cost, u32 destroyed, u8 automatic) {
  if (active_count >= DsToxicMaximumActive)
    return nullptr;
  auto *instance = new DsToxicInstance;
  instance->AddRef();
  instance->Init(this, x, z, velocity, severity, cost, destroyed, automatic);
  return instance;
}

void DsToxicManager::InstanceStarted() {
  ++active_count;
}

// Decrement a positive active count and reset the original idle schedule whenever it is zero, including initially zero.
void DsToxicManager::InstanceEnded() {
  if (active_count)
    --active_count;
  if (!active_count)
    ResetInterval();
}

void DsToxicManager::EndNotice175() {
  if (notice_175_count && !--notice_175_count)
    box->Event(kAuEventEffectSlot52, kAuEffectDisasterToxicCloud, 0, 0, 0);
}

void DsToxicManager::EndNotice231() {
  if (notice_231_count && !--notice_231_count)
    box->Event(kAuEventEffectSlot52, kAuEffectDisasterToxicRain, 0, 0, 0);
}

void DsToxicManager::EndNotice231Mode() {
  if (notice_231_mode_count && !--notice_231_mode_count && notice_231_count)
    box->Event(kAuEventEffectSlot56, kAuEffectDisasterToxicRain, 0, 0, 0);
}

void DsToxicManager::LeaveCamera() {
  u32 previous = camera_count--;
  if (previous == 1 && active_count)
    box->Event(kAuEventEffectSlot56, kAuEffectDisasterToxicCloud, 0, 0, 0);
}

// Notify removal before qualified active teardown, then drop the instance's original final hold.
u32 DsToxicManager::FinishInstance(DsToxicInstance *instance) {
  layer->DisasterInstanceChange(instance, 4);
  instance->Shutdown();
  return instance->Release();
}

DsToxicClouds::DsToxicClouds() : anchor(new DsToxicLink) {
  anchor->next = anchor;
  anchor->previous = anchor;
}

DsToxicClouds::~DsToxicClouds() {
  Clear();
  delete anchor;
}

// Free nodes in forward order without releasing their separately managed external cloud identities.
void DsToxicClouds::Clear() {
  DsToxicLink *node = anchor->next;
  while (node != anchor) {
    DsToxicLink *retired = node;
    node = node->next;
    delete retired;
  }
  anchor->next = anchor;
  anchor->previous = anchor;
}

DsToxicInstance::DsToxicInstance()
    : initialized(false), raining(0), manager(nullptr), position{0, 0, 0}, in_camera(false), phase(0), phase_ticks(0),
      references(0), cost(0), destroyed(0), enabled(0), notifications_registered(false), severity(0), next_rain_tick(0),
      rain_ticks(0), rain_duration(0), active_clouds(0) {
  for (u32 index = 0; index < 6; ++index)
    effects[index] = kApInvalidNotificationId;
  bounds.top = 0;
  bounds.left = 0;
  bounds.bottom = 0;
  bounds.right = 0;
  destruction_bounds.right = 0;
  destruction_bounds.left = 0;
  destruction_bounds.bottom = 0;
  destruction_bounds.top = 0;
  camera_bounds.bottom = 0;
  camera_bounds.top = 0;
  camera_bounds.right = 0;
  camera_bounds.left = 0;
}

DsToxicInstance::~DsToxicInstance() {
  if (initialized)
    Shutdown();
}

// Publish the common or actual private motion identity before virtual acquisition, clearing the output.
bool DsToxicInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsToxicMotion, DsToxicMotion);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsToxicInstance::AddRef() {
  return ++references;
}

// Decrement a held count before destruction, deleting zero-reference instances even when the initial count was zero.
// Active teardown observes the already-decremented count, matching the original final-release order.
u32 DsToxicInstance::Release() {
  if (references)
    --references;
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Publish the cell rectangle only while initialized, retaining the inactive bottom/top/right/left clear order.
bool DsToxicInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    output->left = bounds.left;
    output->top = bounds.top;
    output->right = bounds.right;
    output->bottom = bounds.bottom;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

// Copy signed fixed coordinates to cell outputs with the original arithmetic shifts, independent of initialization.
// Output aliasing retains the later Z write.
bool DsToxicInstance::GetLocation(u32 *x, u32 *z) {
  *x = u32(position.x >> kCtMicroCoordinateShift);
  *z = u32(position.z >> kCtMicroCoordinateShift);
  return true;
}

// Return the shared null occupant identity because this instance owns a list of external clouds.
ScOccupant *DsToxicInstance::GetOccupant() {
  return nullptr;
}

// Borrow the owner's common manager identity, preserving null rather than applying the original base adjustment.
DsManager *DsToxicInstance::GetManager() {
  return manager;
}

u32 DsToxicInstance::TypeId() {
  return kDsToxicTypeId;
}

u32 DsToxicInstance::GetDestroyedCount() {
  return destroyed;
}

u32 DsToxicInstance::GetCost() {
  return cost;
}

// Copy the actual fixed position in X/altitude/Z order, preserving defined aliases through the original ordered stores.
u8 DsToxicInstance::GetPosition(i32 *x, i32 *y, i32 *z) const {
  *x = position.x;
  *y = position.y;
  *z = position.z;
  return 1;
}

// Copy current velocity into the caller's real triple and return that same output identity.
ScVec3F *DsToxicInstance::GetVelocity(ScVec3F *output) const {
  output->x = velocity.x;
  output->y = velocity.y;
  output->z = velocity.z;
  return output;
}

i32 DsToxicInstance::Contains(u32 x, u32 z) const {
  return x >= u32(bounds.left) && x <= u32(bounds.right) && z >= u32(bounds.top) && z <= u32(bounds.bottom);
}

// Publish the subscription flag before acquiring one borrowed server and registering the three messages in order.
void DsToxicInstance::RegisterNotifications() {
  if (!notifications_registered) {
    notifications_registered = true;
    ScMessageServer *server = DsGetMessageServer();
    server->AddNotification(static_cast<ScMessageTarget *>(this), kDsMsg_UnresolvedToxicNotification);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_ToxicCloudRetired);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
  }
}

// Clear subscriptions before service lookup and remove the final registration first, followed by the first two.
void DsToxicInstance::RemoveNotifications() {
  if (notifications_registered) {
    notifications_registered = false;
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kDsMsg_UnresolvedToxicNotification);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_ToxicCloudRetired);
  }
}

// Remove six transient handles in order, invalidating each only after its synchronous message callback.
// Later handles are read when reached so callbacks can replace their remaining values.
void DsToxicInstance::ClearEffects() {
  for (u32 index = 0; index < 6; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Bind fixed position, velocity and damage state before announcing the instance and subscribing notifications.
// Preserve terrain's high-Z-byte fallback and publish the running gate only after immediate progression.
u8 DsToxicInstance::Init(
    DsToxicManager *owner, u32 x, u32 z, const ScVec3F *motion, u32 level, u32 charge, u32 count, u8 automatic) {
  if (!initialized) {
    severity = level;
    position.x = i32(x << kCtMicroCoordinateShift);
    manager = owner;
    position.z = i32(z << kCtMicroCoordinateShift);
    u8 altitude = u8(z >> 24);
    manager->terrain->GetVertexAltitude(x, z, &altitude);
    position.y = i32((u32(altitude) << kCtMicroCoordinateShift) +
        u32(manager->random.Range(i32(DsToxicMinimumHeight), i32(DsToxicMaximumHeight))));
    velocity.x = motion->x;
    velocity.y = motion->y;
    velocity.z = motion->z;
    if (!automatic) {
      phase = 3;
      phase_ticks = DsToxicVisibleTime;
    } else {
      phase = 0;
      phase_ticks = 0;
    }
    cost = charge;
    destroyed = count;
    initialized = true;
    manager->InstanceStarted();
    RegisterNotifications();
    enabled = 0;
    manager->layer->DisasterInstanceChange(this, 0);
    Advance(0, 0);
    enabled = 1;
  }
  return 1;
}

// Mark inactive before notifications/effects and release UI modes before external cloud identities.
// Read next nodes after release callbacks, clear the current list afterward and reset the owner's idle schedule last.
u8 DsToxicInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    RemoveNotifications();
    ClearEffects();
    if (raining) {
      if (in_camera)
        manager->EndNotice231Mode();
      manager->EndNotice231();
    }
    if (in_camera)
      manager->LeaveCamera();
    if (clouds.anchor->next != clouds.anchor) {
      manager->EndNotice175();
      DsToxicLink *anchor = clouds.anchor;
      for (DsToxicLink *node = clouds.anchor->next; node != anchor; node = node->next)
        node->cloud->Release();
      clouds.Clear();
    }
    manager->InstanceEnded();
  }
  return 1;
}

bool DsToxicInstance::Tick(DsTickType tick) {
  return Advance(tick, 1) != 0;
}

// Accept the three industrial zone bytes with their original weights, adding four only when an occupant is present.
// Other zones explicitly publish zero and never query occupancy.
u8 DsToxicManager::GetSpawnWeight(u32 x, u32 z, u32 *weight) {
  u8 zone;
  zones->GetValue(x, z, &zone);
  switch (zone) {
  case 9:
    *weight = 2;
    break;
  case 10:
    *weight = 3;
    break;
  case 11:
    *weight = 4;
    break;
  default:
    *weight = 0;
    return 0;
  }
  if (occupants->DoesOccupantExistAtLocation(x, z))
    *weight += 4;
  return 1;
}

// Spiral from two triangular draws, summing eligible industrial rays in left/up/right/down order.
// Prefer the first weight of at least eighty, otherwise the best center, falling back to the origin if out of bounds.
void DsToxicManager::FindSpawnSite(u32 *x, u32 *z) {
  DsTileScan scan;
  const i32 dx[] = {-1, 0, 1, 0}, dz[] = {0, -1, 0, 1};
  u32 best = 0;
  i32 origin_x = random.Triangular(0, i32(maximum_x)), origin_z = random.Triangular(0, i32(maximum_z));
  i32 cell_x = origin_x, cell_z = origin_z;
  *x = u32(origin_x);
  *z = u32(origin_z);
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(maximum_x);
  scan.bottom = i32(maximum_z);
  scan.Reset(origin_x, origin_z);
  while (scan.Next(&cell_x, &cell_z)) {
    u32 total;
    if (GetSpawnWeight(u32(cell_x), u32(cell_z), &total)) {
      i32 lengths[4];
      for (u32 direction = 0; direction < 4; ++direction) {
        u32 ray_x = u32(cell_x) + u32(dx[direction]), ray_z = u32(cell_z) + u32(dz[direction]);
        lengths[direction] = 0;
        while (ray_x <= maximum_x && ray_z <= maximum_z) {
          u32 weight;
          if (!GetSpawnWeight(ray_x, ray_z, &weight))
            break;
          ++lengths[direction];
          total += weight;
          ray_x += u32(dx[direction]);
          ray_z += u32(dz[direction]);
        }
      }
      if (total >= 80) {
        *x = u32(cell_x) + u32(i32(u32(lengths[2]) - u32(lengths[0])) / 2);
        *z = u32(cell_z) + u32(i32(u32(lengths[3]) - u32(lengths[1])) / 2);
        if (*x > maximum_x || *z > maximum_z) {
          *x = u32(cell_x);
          *z = u32(cell_z);
        }
        return;
      }
      if (total > best) {
        best = total;
        *x = u32(cell_x) + u32(i32(u32(lengths[2]) - u32(lengths[0])) / 2);
        *z = u32(cell_z) + u32(i32(u32(lengths[3]) - u32(lengths[1])) / 2);
      }
    }
  }
  if (*x > maximum_x || *z > maximum_z) {
    *x = u32(origin_x);
    *z = u32(origin_z);
  }
}

// Cap unsigned severity, choose a real industrial spawn cell and draw speed before the independent direction angle.
// Pass the resulting float velocity and raw low automatic byte through the actual private spawn interface.
i32 DsToxicManager::Start(u32 level, i32 automatic) {
  if (level >= 5)
    level = 5;
  u32 x, z;
  FindSpawnSite(&x, &z);
  f64 speed = random.TriangularReal(DsToxicMinimumSpeed, DsToxicMaximumSpeed);
  f64 angle = random.RealRange(0.0, f64(3.1415927f) + f64(3.1415927f));
  ScVec3F velocity = {f32(sin(angle) * speed), 0.0f, f32(cos(angle) * speed)};
  return Start(x, z, &velocity, level, 0, 0, u8(automatic)) != nullptr;
}

void DsToxicManager::EnterCamera() {
  if (++camera_count == 1 && active_count)
    box->Event(kAuEventEffectSlot56, kAuEffectDisasterToxicCloud, 0, 1, 0);
}

void DsToxicManager::EnterNotice231Mode() {
  if (++notice_231_mode_count == 1 && notice_231_count)
    box->Event(kAuEventEffectSlot56, kAuEffectDisasterToxicRain, 0, 1, 0);
}

bool DsToxicInstance::IsActive() {
  return initialized && clouds.anchor->next != clouds.anchor;
}

// Recompute camera intersection only while cloud ownership is active and update UI reference modes on transitions.
// Publish the resulting camera flag after all synchronous owner callbacks complete.
void DsToxicInstance::RefreshVisibility() {
  if (IsActive()) {
    bool visible = DsController::IntersectsVisibleRect(camera_bounds) != 0;
    if (visible != in_camera) {
      if (visible) {
        manager->EnterCamera();
        if (raining)
          manager->EnterNotice231Mode();
      } else {
        manager->LeaveCamera();
        if (raining)
          manager->EndNotice231Mode();
      }
    }
    in_camera = visible;
  }
}

// Hold the instance through notification processing, refreshing camera state or acknowledging its own pending message.
// The hold is released on every path; cloud retirement compares the complete owner identity used by display Init.
bool DsToxicInstance::DoMessage(ScMessage *message) {
  AddRef();
  if (initialized) {
    if (message->type == kWnMsg_ViewInfoUpdated) {
      if (u32(message->data1) == kSpViewUpdate_Snapshot)
        RefreshVisibility();
    } else if (message->type == kSpMsg_ToxicCloudRetired && message->data1 == reinterpret_cast<uptr>(this))
      --active_clouds;
  }
  Release();
  return true;
}

// Apply separate thousand-way destruction and abandonment chances, preserving draws, lookup holds and accounting order.
// Count every successful lookup even when removal is refused or the abandonment filters reject the occupant.
void DsToxicInstance::ApplyRainDamage() {
  if (raining && manager->random.Next(1000) < DsToxicDestructionChance) {
    i32 x = manager->random.Range(destruction_bounds.left, destruction_bounds.right);
    i32 z = manager->random.Range(destruction_bounds.top, destruction_bounds.bottom);
    ScOccupant *occupant;
    if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      u32 charge;
      manager->layer->DestroyOccupant(occupant, kDsDamageBurn, &charge);
      cost += charge;
      occupant->Release();
      ++destroyed;
    }
  }
  if (manager->random.Next(1000) < DsToxicAbandonChance) {
    i32 x = manager->random.Range(bounds.left, bounds.right);
    i32 z = manager->random.Range(bounds.top, bounds.bottom);
    ScOccupant *occupant;
    if (manager->occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      if (occupant->MajorType() == 1) {
        u8 zone;
        manager->zones->GetValue(u32(x), u32(z), &zone);
        if (zone != 22) {
          u32 type = occupant->TypeId();
          if (manager->agent_types->IsAncestor(kCtAgentResidential, type) ||
              manager->agent_types->IsAncestor(kCtAgentCommercial, type) ||
              manager->agent_types->IsAncestor(kCtAgentIndustrial, type)) {
            u32 charge;
            manager->layer->DestroyOccupant(occupant, kDsDamageAbandon, &charge);
          }
        }
      }
      occupant->Release();
      ++destroyed;
    }
  }
}

void DsToxicManager::BeginNotice175() {
  if (++notice_175_count == 1)
    box->Event(kAuEventPlayEffect, kAuEffectDisasterToxicCloud, 0, 0, 0);
}

void DsToxicManager::BeginNotice231() {
  if (++notice_231_count == 1)
    box->Event(kAuEventPlayEffect, kAuEffectDisasterToxicRain, 0, 0, 0);
}

// Release text before storing the returned effect, then retire the current resource identity after callbacks finish.
void DsToxicInstance::PostPhase(u32 state) {
  i32 slot = -1;
  u32 resource;
  if (state == 1) {
    resource = manager->random.Next(3) + 100;
    slot = 0;
  } else if (state == 2) {
    resource = manager->random.Next(3) + 106;
    slot = 1;
  } else if (state == 3) {
    resource = manager->random.Next(3) + 109;
    slot = 2;
  } else if (state == 6)
    resource = manager->random.Next(2) + 200;
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

// Remove notifications before terminal phase six and post its notice. The current owner then removes and releases this.
u32 DsToxicInstance::Finish() {
  RemoveNotifications();
  phase = 6;
  PostPhase(6);
  return manager->FinishInstance(this);
}

void DsToxicManager::StopInstance(DsInstance *instance) {
  FinishInstance(static_cast<DsToxicInstance *>(instance));
}

// Construct the actual manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateClassC2FE8D43() {
  return static_cast<DsManager *>(new DsToxicManager);
}

i8 DsToxicInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return phase == 4 && distance <= 5 ? levels[distance] : 0;
}

u32 DsToxicInstance::PanicRadius() {
  return phase == 4 ? 5 : 0;
}

// End rain notices before walking the captured cloud list, stopping current rain before each display's fade request.
// Re-read the raw raining flag after callbacks and clear it only after all retained display identities are visited.
void DsToxicInstance::StopClouds() {
  if (raining) {
    if (in_camera)
      manager->EndNotice231Mode();
    manager->EndNotice231();
  }
  DsToxicLink *anchor = clouds.anchor;
  for (DsToxicLink *node = anchor->next; node != anchor; node = node->next) {
    if (raining)
      node->cloud->StopRaining();
    node->cloud->DoFade(0, 1);
  }
  raining = 0;
}

// Spiral through the interior with the original per-cell threshold draws and jitter, retaining each successful display once.
// Preserve signed cap division, unsigned animation bounds, arithmetic cell shifts and both aliased surround-rectangle calls.
void DsToxicInstance::SpawnClouds() {
  DsTileScan scan;
  i32 scaled = i32((severity + 1) * (DsToxicMaximumClouds - DsToxicMinimumClouds));
  u32 maximum = DsToxicMinimumClouds + u32(scaled / 5), attempts = 0;
  camera_bounds = {UINT32_MAX, UINT32_MAX, 0, 0};
  scan.left = 6;
  scan.top = 6;
  scan.right = i32(manager->maximum_x - 6);
  scan.bottom = i32(manager->maximum_z - 6);
  scan.Reset(position.x >> kCtMicroCoordinateShift, position.z >> kCtMicroCoordinateShift);
  i32 cell_x, cell_z;
  while (scan.Next(&cell_x, &cell_z)) {
    ++attempts;
    if (i32(attempts) >= manager->random.Range(8, 16)) {
      u32 origin_x = u32(cell_x) << kCtMicroCoordinateShift, origin_z = u32(cell_z) << kCtMicroCoordinateShift;
      i32 fixed_x = manager->random.Range(i32(origin_x - 1536), i32(origin_x + 1536));
      i32 fixed_z = manager->random.Range(i32(origin_z - 1536), i32(origin_z + 1536));
      if (u32(fixed_x) < manager->animation_x && u32(fixed_z) < manager->animation_z) {
        u32 x = u32(fixed_x >> kCtMicroCoordinateShift), z = u32(fixed_z >> kCtMicroCoordinateShift);
        u8 altitude;
        manager->terrain->GetVertexAltitude(x, z, &altitude);
        i32 height = i32((u32(altitude) << kCtMicroCoordinateShift) +
            u32(manager->random.Range(i32(DsToxicMinimumHeight), i32(DsToxicMaximumHeight))));
        void *created;
        if (DsGetCom()->GetClassObject(kSpToxicCloudDisplayClassId, IID_SpToxicCloudDisplay, &created)) {
          auto *cloud = static_cast<SpToxicCloudDisplay *>(created);
          cloud->Init(reinterpret_cast<uptr>(this), fixed_x, fixed_z, height);
          cloud->SetVelocity(velocity);
          DsToxicLink *node = new DsToxicLink{clouds.anchor, clouds.anchor->previous, cloud};
          clouds.anchor->previous->next = node;
          clouds.anchor->previous = node;
          ++active_clouds;
          if (camera_bounds.left > x)
            camera_bounds.left = x;
          if (camera_bounds.right < x)
            camera_bounds.right = x;
          if (camera_bounds.top > z)
            camera_bounds.top = z;
          if (camera_bounds.bottom < z)
            camera_bounds.bottom = z;
          u32 count = 0;
          for (DsToxicLink *entry = clouds.anchor->next; entry != clouds.anchor; entry = entry->next)
            ++count;
          if (count >= maximum)
            break;
          attempts = 0;
        }
      }
    }
  }
  position.x = i32(((camera_bounds.left + camera_bounds.right) << 7) & kCtMicroCellMask);
  position.z = i32(((camera_bounds.top + camera_bounds.bottom) << 7) & kCtMicroCellMask);
  bounds = {camera_bounds.left, camera_bounds.top, camera_bounds.right, camera_bounds.bottom};
  manager->layer->ComputeSurroundRect(bounds, &bounds, DsToxicAbandonRadius * (severity + 1));
  destruction_bounds = {camera_bounds.left, camera_bounds.top, camera_bounds.right, camera_bounds.bottom};
  manager->layer->ComputeSurroundRect(
      destruction_bounds, &destruction_bounds, DsToxicDestructionRadius * (severity + 1));
}

// Advance premonitions, create the cloud field, alternate original rain intervals and retire after all fades finish.
// Preserve readiness/running gates, unsigned rain deadlines, signed final cloud-count test and callback publication order.
u8 DsToxicInstance::Advance(i32, u8 running) {
  if (!initialized || (running && !enabled))
    return 1;
  u32 current_phase = phase, elapsed = ++phase_ticks;
  switch (current_phase) {
  case 0:
  case 1:
  case 2: {
    u32 deadline = current_phase == 0 ? DsToxicFirstNoticeTime
        : current_phase == 1          ? DsToxicSecondNoticeTime
                                      : DsToxicThirdNoticeTime;
    if (elapsed >= deadline) {
      phase = current_phase + 1;
      PostPhase(current_phase + 1);
      phase_ticks = 0;
    }
    break;
  }
  case 3:
    if (elapsed > DsToxicVisibleTime) {
      phase = 4;
      ClearEffects();
      PostPhase(phase);
      phase_ticks = 0;
      u32 delay = u32(manager->random.Range(i32(DsToxicMinimumRainStart), i32(DsToxicMaximumRainStart)));
      next_rain_tick = phase_ticks + delay;
      SpawnClouds();
      RefreshVisibility();
      duration = u32(manager->random.Range(i32(DsToxicMinimumDuration), i32(DsToxicMaximumDuration)));
      manager->layer->DisasterInstanceChange(this, 1);
      manager->BeginNotice175();
    }
    break;
  case 4:
    if (active_clouds && elapsed <= duration) {
      if (raining) {
        if (++rain_ticks <= rain_duration)
          ApplyRainDamage();
        else {
          raining = 0;
          u32 delay = u32(manager->random.Range(i32(DsToxicMinimumRainStart), i32(DsToxicMaximumRainStart)));
          next_rain_tick = DsToxicMinimumRainGap + phase_ticks + delay;
          DsToxicLink *anchor = clouds.anchor;
          for (DsToxicLink *node = anchor->next; node != anchor; node = node->next)
            node->cloud->StopRaining();
          if (in_camera)
            manager->EndNotice231Mode();
          manager->EndNotice231();
        }
      } else if (elapsed > next_rain_tick) {
        raining = 1;
        rain_ticks = 0;
        rain_duration = u32(manager->random.Range(i32(DsToxicMinimumRainDuration), i32(DsToxicMaximumRainDuration)));
        DsToxicLink *anchor = clouds.anchor;
        for (DsToxicLink *node = anchor->next; node != anchor; node = node->next)
          node->cloud->StartRaining();
        manager->BeginNotice231();
        if (in_camera)
          manager->EnterNotice231Mode();
      }
    } else {
      phase_ticks = 0;
      StopClouds();
      phase = 5;
    }
    break;
  case 5:
    if (i32(active_clouds) <= 0)
      Finish();
    break;
  }
  return 1;
}

extern u32 DsUfoFirstPremonitionTicks, DsUfoSecondPremonitionTicks, DsUfoThirdPremonitionTicks;
extern u32 DsUfoAttackTicks, DsUfoEndTicks, DsUfoMaximumCheatShips, DsUfoMaximumShips;
extern u32 DsUfoExitExplosionChance;

extern u32 DsUfoMaximumCropCircles, DsUfoMaximumLowTech;

extern u32 DsUfoMaximumDestruction, DsUfoMaximumLandmarks, DsUfoMaximumSpaceports;
extern u32 DsUfoMaximumHighTech, DsUfoMaximumPower;
extern u32 DsUfoFireChance, DsUfoDestructionChance;

// Actual56-byte target scanner owns either packed coordinates or a CRT-random arithmetic permutation cursor.
class DsTargetScan {
public:
  DsTargetScan();
  // Only the original packed coordinate storage retires; scalar traversal state has no independent ownership.
  ~DsTargetScan() = default;
  void Begin(u8 shuffle);
  u8 Next(i32 *x, i32 *z);
  u8 state;
  i32 mode;
  u8 dirty;
  i32 left, top, right, bottom, count;
  ScArray<u16> cells;
  i32 cursor, index, stride;
};

// Original signed16-bit step table at3336C; the explicit recovered range ends before the next vtable at333F8.
static const i16 kDsTargetSteps[] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79,
    83, 89, 97, 101, 103, 107, 109, 113, 179, 211, 257, 307, 373, 479, 577, 641, 719, 857, 977, 1063, 1237, 1471, 1663,
    1993, 2221, 2539, 2909, 3259, 3733, 4507, 5281, 5861, 6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431, 19801,
    21569, 23633, 26003, 27529, 29683, 32051, 32611};

// Shuffle packed coordinates in forward insertion order using the original independent CRT random stream.
// Empty ranges perform no pointer arithmetic or draws; each later element consumes exactly one rand result.
static void DsShuffleCells(u16 *begin, u16 *end) {
  if (begin != end) {
    for (u16 *cell = begin + 1; cell != end; ++cell) {
      i32 draw = ScRand();
      u16 value = *cell, *other = begin + u32(draw) % (u32(cell - begin) + 1);
      *cell = *other;
      *other = value;
    }
  }
}

DsTargetScan::DsTargetScan()
    : state(1), mode(1), dirty(1), left(0), top(0), right(0), bottom(0), count(0), cursor(0), index(0), stride(1) {
}

// Reset packed or arithmetic traversal using the original prime table and independent CRT random stream.
void DsTargetScan::Begin(u8 shuffle) {
  state = 1;
  cursor = 0;
  count = i32((u32(right) - u32(left) + 1) * (u32(bottom) - u32(top) + 1));
  if (!mode) {
    if (i32(cells.size) != count) {
      if (count < 0)
        ScFatal(SC_ERROR_INVALID, "Negative packed UFO scan length.");
      u32 old_size = cells.size;
      cells.Reserve(u32(count));
      cells.size = u32(count);
      for (u32 offset = old_size; offset < cells.size; ++offset)
        cells.data[offset] = 0;
      dirty = 1;
    }
    if (dirty) {
      u32 offset = 0;
      for (i32 x = left; x <= right; x = i32(u32(x) + 1))
        for (i32 z = top; z <= bottom; z = i32(u32(z) + 1))
          cells.data[offset++] = u16((u32(u8(x)) << 8) | u8(z));
    }
    if (shuffle)
      DsShuffleCells(cells.data, cells.size ? cells.data + cells.size : cells.data);
  } else {
    u32 step = 0;
    if (count >= 12227) {
      if (count >= kScPermutationMaximumCount)
        count = kScPermutationMaximumCount;
      step = 60;
    } else if (count >= 3259) {
      step = 50;
    } else if (count >= 857) {
      step = 40;
    } else if (count >= 113) {
      step = 30;
    } else if (count >= 71) {
      step = 20;
    } else if (count >= 29) {
      step = 10;
    }
    // The original final remainder requires a nonzero traversal count.
    assert(count != 0);
    while (kDsTargetSteps[step] < count / 4) {
      ++step;
      assert(step < sizeof(kDsTargetSteps) / sizeof(*kDsTargetSteps));
    }
    while (count % kDsTargetSteps[step] == 0) {
      ++step;
      assert(step < sizeof(kDsTargetSteps) / sizeof(*kDsTargetSteps));
    }
    stride = kDsTargetSteps[step];
    index = ScRand() % count;
  }
}

// Advance before reporting each arithmetic-permutation cell, or decode X from the high packed byte and Z from the low byte.
// Mark the last successful output exhausted; later calls leave both coordinate outputs untouched.
u8 DsTargetScan::Next(i32 *output_x, i32 *output_z) {
  if (state == 2)
    return 0;
  if (mode) {
    cursor = i32(u32(cursor) + 1);
    if (cursor > count) {
      state = 2;
      return 0;
    }
    index = i32(u32(index) + u32(stride)) % count;
    *output_x = i32(u32(left) + u32(index % i32(u32(right) - u32(left) + 1)));
    *output_z = i32(u32(top) + u32(index / i32(u32(right) - u32(left) + 1)));
    if (cursor == count)
      state = 2;
  } else {
    i32 total = i32(cells.size);
    if (cursor >= total) {
      state = 2;
      return 0;
    }
    u32 previous = u32(cursor);
    cursor = i32(u32(cursor) + 1);
    u16 packed = cells.data[previous];
    *output_x = u8(packed >> 8);
    *output_z = u8(packed);
    if (cursor == total)
      state = 2;
  }
  return 1;
}

// Actual32-byte row scanner used by the crop-site search; all bounds and stepping comparisons are signed.
class DsRowScan {
public:
  DsRowScan();
  i32 Reset(i32 x, i32 z);
  u8 Next(i32 *x, i32 *z);
  u8 state;
  i32 x, z, stride, left, top, right, bottom;
};

// Actual20-byte packed column map tracks already-selected crop footprints, not initialized-memory instrumentation.
class DsCropMask {
public:
  DsCropMask(u32 width, u32 height, const u8 *value);
  virtual ~DsCropMask();
  u32 *Set(u32 x, u32 z, const u8 *value);
  // Complete fill effects; original incidental loop/pointer return remains unresolved.
  void Fill(const u8 *value);
  u32 width, height, words;
  u32 **columns;
};

DsRowScan::DsRowScan() : stride(1), left(-INT32_MAX), top(-INT32_MAX), right(INT32_MAX), bottom(INT32_MAX) {
  Reset(0, 0);
}

// Publish the requested origin and mark out-of-bounds starts exhausted, retaining the original X return word.
i32 DsRowScan::Reset(i32 origin_x, i32 origin_z) {
  state = 0;
  x = origin_x;
  z = origin_z;
  if (x < left || z < top || x > right || z > bottom)
    state = 2;
  return origin_x;
}

// Report the initial coordinate once, then step along X and wrap to the next Z row at the inclusive right bound.
// Exhausted scans leave both genuine outputs untouched; register arithmetic retains the source DWORD wrapping.
u8 DsRowScan::Next(i32 *output_x, i32 *output_z) {
  if (state == 2)
    return 0;
  if (!state) {
    state = 1;
  } else {
    x = i32(u32(x) + u32(stride));
    if (x > right) {
      z = i32(u32(z) + u32(stride));
      x = left;
      if (z > bottom) {
        state = 2;
        return 0;
      }
    }
  }
  *output_x = x;
  *output_z = z;
  return 1;
}

// Allocate one packed-bit column per X coordinate, then fill all columns only after their allocations complete.
// Keep the original row rounding and separate constructor operands rather than inventing a generic bitmap framework.
DsCropMask::DsCropMask(u32 count_x, u32 count_z, const u8 *value)
    : width(count_x), height(count_z), words((count_z >> 5) + ((count_z & 31) != 0)), columns(new u32 *[count_x]) {
  for (u32 index = 0; index < width; ++index)
    columns[index] = new u32[words];
  Fill(value);
}

// Retire the packed columns in ascending order before releasing their owning pointer array.
DsCropMask::~DsCropMask() {
  for (u32 index = 0; index < width; ++index)
    delete[] columns[index];
  delete[] columns;
}

// Set or clear the selected footprint bit and return the actual modified word pointer.
u32 *DsCropMask::Set(u32 cell_x, u32 cell_z, const u8 *value) {
  u32 *word = columns[cell_x] + (cell_z >> 5);
  if (*value)
    *word |= 1u << (cell_z & 31);
  else
    *word &= ~(1u << (cell_z & 31));
  return word;
}

// Fill every allocated word from the single source byte, retaining the original column-major write order.
// The ignored original loop/pointer residual is not asserted as a source return contract.
void DsCropMask::Fill(const u8 *value) {
  u32 fill = *value ? kScFilledBitGridWord : 0;
  for (u32 cell_x = 0; cell_x < width; ++cell_x)
    for (u32 index = 0; index < words; ++index)
      columns[cell_x][index] = fill;
}

// Walk borrowed agent ancestors until an actual category-ID table matches, preserving category priority and one ID query per node.
// Return zero when the complete parent chain has no recognized UFO target kind.
i32 DsUfoClassifyTarget(ScOccupant *occupant) {
  CtAgentType *type = occupant->CityAgentType();
  while (type) {
    u32 id = type->Id();
    if (id == kCtAgentPowerPlant)
      return kDsUfoTargetPower;
    if (id == kCtAgentUniversity || id == kCtAgentArcology || id == kCtAgentCleanHighDensityIndustry ||
        id == kCtAgentAirport || id == kCtAgentSeaport || id == kCtAgentToxicWastePlant)
      return kDsUfoTargetHighTech;
    if (id == kCtAgentSpaceport)
      return kDsUfoTargetSpaceport;
    if (id == kCtAgentHistoricBuilding || id == kCtAgentLandmark)
      return kDsUfoTargetLandmark;
    type = type->Parent();
  }
  return kDsUfoTargetLowTech;
}

// Require a one-cell-wide occupant and a9300 ancestor whose specific building attribute has flag100.
// Re-query attributes for each matching ancestor, releasing the acquired view before testing its captured flags.
static u8 DsUfoCropCandidate(ScOccupant *occupant) {
  if (occupant->SizeInCellsX() == 1) {
    CtAgentType *type = occupant->CityAgentType();
    while (type) {
      if (type->Id() == kCtAgentAgriculturalIndustry) {
        GeAttrib *attributes = occupant->GetAttribData();
        GeBuildingType *building;
        if (attributes && attributes->QueryInterface(IID_GeBuildingType, reinterpret_cast<void **>(&building))) {
          u32 flags = building->Flags();
          building->Release();
          if (flags & 0x100u)
            return 1;
        }
      }
      type = type->Parent();
    }
  }
  return 0;
}

// Test a retained cell occupant through the actual crop predicate, then release it before returning the captured byte.
static u8 DsUfoCropCell(ScOccupantManager *occupants, u32 x, u32 z) {
  ScOccupant *occupant;
  if (!occupants->GetOccupantAtCell(x, z, &occupant))
    return 0;
  u8 result = DsUfoCropCandidate(occupant);
  occupant->Release();
  return result;
}

DsUfoInstance::DsUfoInstance()
    : initialized(false), manager(nullptr), effect(nullptr), flag_24(0), references(0), flag_32(1), flag_33(0),
      location_index(0), timing{}, effects{kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId,
                                       kApInvalidNotificationId, kApInvalidNotificationId},
      state{} {
}

DsUfoInstance::~DsUfoInstance() {
  Shutdown();
}

// Publish primary Unknown/disaster or private snapshot identity and retain once after the output is written. Refused
// queries clear the output; the message subobject is deliberately not exposed by this query.
bool DsUfoInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsUfoMotion, DsUfoMotion);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsUfoInstance::AddRef() {
  return ++references;
}

// Delete the complete instance when its held count is zero or one; otherwise return the decremented original word.
// Preserve the original zero-count deletion case without introducing a decrement underflow.
u32 DsUfoInstance::Release() {
  if (references <= 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Bound active phase4-or-later instances to the original square city extent, querying X dimension twice as installed.
// Earlier phases clear bottom/right/top/left in the original order and report false.
bool DsUfoInstance::GetBounds(GeCellRect *output) {
  if (initialized && state.phase >= kDsUfoAttacking) {
    output->left = 0;
    output->top = 0;
    output->right = manager->city->CellCountX() - 1;
    output->bottom = manager->city->CellCountX() - 1;
    return true;
  }
  output->bottom = 0;
  output->right = 0;
  output->top = 0;
  output->left = 0;
  return false;
}

// Preserve the shared null occupant result; a UFO instance owns a specialized effect controller instead.
ScOccupant *DsUfoInstance::GetOccupant() {
  return nullptr;
}

// Borrow the actual common manager identity, preserving null before initialization.
DsManager *DsUfoInstance::GetManager() {
  return manager;
}

u32 DsUfoInstance::TypeId() {
  return kDsUfoTypeId;
}

u32 DsUfoInstance::GetDestroyedCount() {
  return state.landmarks + state.spaceports + state.low_tech + state.advanced;
}

u32 DsUfoInstance::GetCost() {
  return state.cost;
}

u32 DsUfoInstance::GetRecords(DsUfoRecord *records, u32 *count) const {
  return manager->GetRecords(records, count);
}

void DsUfoInstance::CopyRecord(DsUfoRecord *record) {
  memcpy(record, &state, sizeof(DsUfoRecord));
}

// Publish ownership and original timings before subscribing the message identity and registering the new instance.
// Manual starts skip premonitions and receive an immediate tick; an already initialized entry returns without rebinding.
u8 DsUfoInstance::Init(DsUfoManager *owner, u32 kind, u8 flag, u8 automatic) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    flag_24 = flag;
    state.kind = kind;
    state.phase = kDsUfoInitialDelay;
    state.elapsed = 0;
    timing[0] = DsUfoFirstPremonitionTicks;
    timing[1] = DsUfoSecondPremonitionTicks;
    timing[2] = DsUfoThirdPremonitionTicks;
    timing[3] = DsUfoAttackTicks;
    timing[4] = DsUfoEndTicks;
    if (!automatic) {
      state.phase = kDsUfoAttackPending;
      timing[0] = 0;
      timing[1] = 0;
      timing[2] = 0;
      timing[3] = 0;
      timing[4] = DsUfoEndTicks - DsUfoAttackTicks;
    }
    ScMessageServer *server = DsGetMessageServer();
    server->AddNotification(static_cast<ScMessageTarget *>(this), kDsMsg_UnresolvedUfoNotification);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoAbduction);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoBuildingVaporized);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoCropCircleCreated);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_BuildingImpact);
    manager->layer->DisasterInstanceChange(this, 0);
    if (!automatic)
      Tick(DsDayTick);
  }
  return 1;
}

// Apply the original temporary saved-state clamps before fresh initialization, then restore the first eleven raw words.
// Later phases restart at attack phase3 and its configured deadline before the original immediate tick; cost retains the first copy.
u8 DsUfoInstance::Init(DsUfoManager *owner, const DsUfoRecord *record) {
  state = *record;
  if (state.ships > DsUfoMaximumCheatShips)
    state.ships = DsUfoMaximumCheatShips;
  if (state.kind > kDsUfoAttack)
    state.kind = kDsUfoFlyby;
  if (state.phase > kDsUfoDeparting)
    state.phase = kDsUfoInitialDelay;
  if (state.elapsed > 10000)
    state.elapsed = 0;
  Init(owner, state.kind, 0, 1);
  state.serial = record->serial;
  state.ships = record->ships;
  state.kind = record->kind;
  state.abductions = record->abductions;
  state.crop_circles = record->crop_circles;
  state.landmarks = record->landmarks;
  state.spaceports = record->spaceports;
  state.low_tech = record->low_tech;
  state.advanced = record->advanced;
  state.phase = record->phase;
  state.elapsed = record->elapsed;
  if (state.phase >= kDsUfoAttacking) {
    state.elapsed = timing[3];
    state.phase = kDsUfoAttackPending;
    Tick(DsDayTick);
  }
  return 1;
}

// Retire each of the five transient notices in order, clearing its sentinel only after the synchronous callback returns.
// Keep later handles live across earlier callbacks, preserving callback-time replacement of later entries.
void DsUfoInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Select one original localized phase resource and hold its String identity across the mutable synchronous notice.
// Publish the returned handle after releasing text and before retiring the resource; phase4 deliberately emits nothing.
void DsUfoInstance::PostPhase(u32 phase) {
  u32 resource, index;
  switch (phase) {
  case kDsUfoFirstPremonition:
    index = 0;
    resource = manager->random.Next(4) + 549;
    break;
  case kDsUfoSecondPremonition:
    index = 1;
    resource = manager->random.Next(4) + 553;
    break;
  case kDsUfoAttackPending:
    index = 2;
    resource = manager->random.Next(4) + 557;
    break;
  case kDsUfoDeparting:
    index = 4;
    resource = manager->random.Next(3) + 567;
    break;
  default:
    return;
  }
  auto *text = new ScString;
  DsTextResource content(resource, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  effects[index] = notice.effect;
}

// Gather eligible four-cell crop sites, greedily exclude overlapping footprints and sample the retained list in scan order.
// Preserve early scan resets, omitted last-column checks, persistent overlap state and the original low-tech stride cap.
void DsUfoInstance::SelectOpenSites(ScPointXZ *sites, u32 *count) {
  DsRowScan scan;
  ScArray<ScOccupant *> candidates;
  u32 limit = DsUfoMaximumCropCircles < *count ? DsUfoMaximumCropCircles : *count;
  *count = 0;
  u32 width = manager->city->CellCountX(), height = manager->city->CellCountZ();
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(width - 4);
  scan.bottom = i32(height - 4);
  scan.Reset(0, 0);
  i32 scan_x, scan_z;
  while (scan.Next(&scan_x, &scan_z)) {
    u32 x = u32(scan_x), z = u32(scan_z);
    ScOccupant *occupant;
    if (!manager->occupants->GetOccupantAtCell(x, z, &occupant))
      continue;
    bool accepted = false;
    if (DsUfoCropCandidate(occupant)) {
      scan.Reset(i32(x + (x + 4 > width - 5 ? 1 : 4)), i32(z));
      if (DsUfoCropCell(manager->occupants, x + 3, z + 3)) {
        accepted = true;
        for (u32 cell_x = x; cell_x < x + 4 && accepted; ++cell_x) {
          for (u32 cell_z = z; cell_z < z + 4; ++cell_z) {
            if ((cell_x != x || cell_z != z) && (cell_x != x + 3 || cell_z == z + 3) &&
                !DsUfoCropCell(manager->occupants, cell_x, cell_z)) {
              accepted = false;
              break;
            }
          }
        }
      }
    }
    if (accepted)
      *candidates.Append() = occupant;
    else
      occupant->Release();
  }
  if (candidates.size) {
    ScArray<ScOccupant *> retained;
    retained.Reserve(candidates.size);
    for (u32 index = 0; index < candidates.size; ++index)
      *retained.Append() = candidates.data[index];
    u8 bit = 0;
    DsCropMask mask(width, height, &bit);
    bool blocked = false;
    candidates.Clear();
    ScOccupant *first = retained.data[0];
    *candidates.Append() = first;
    u32 x, z;
    first->GetLocationInCellCoords(&x, &z);
    bit = 1;
    if (x < x + 3)
      for (u32 cell_x = x; cell_x <= x + 3; ++cell_x)
        for (u32 cell_z = z; cell_z <= z + 3; ++cell_z)
          mask.Set(cell_x, cell_z, &bit);
    for (u32 index = 0; index < retained.size; ++index) {
      ScOccupant *occupant = retained.data[index];
      occupant->GetLocationInCellCoords(&x, &z);
      if (x < x + 3) {
        for (u32 cell_x = x; cell_x <= x + 3; ++cell_x) {
          for (u32 cell_z = z; cell_z <= z + 3; ++cell_z) {
            blocked = (mask.columns[cell_x][cell_z >> 5] & (1u << (cell_z & 31))) != 0;
            if (blocked)
              break;
          }
          if (blocked)
            break;
        }
      }
      if (!blocked) {
        *candidates.Append() = occupant;
        bit = 1;
        for (u32 cell_x = x; cell_x <= x + 3; ++cell_x)
          for (u32 cell_z = z; cell_z <= z + 3; ++cell_z)
            mask.Set(cell_x, cell_z, &bit);
      }
    }
    u32 total = candidates.size, sampled = total < DsUfoMaximumLowTech ? total : DsUfoMaximumLowTech;
    if (sampled) {
      for (u32 index = 0; index < total;) {
        if (*count >= limit)
          break;
        candidates.data[index]->GetLocationInCellCoords(&sites[*count].x, &sites[*count].z);
        u32 stride = total / sampled;
        ++*count;
        index += stride;
      }
    }
    for (u32 index = 0; index < retained.size; ++index)
      retained.data[index]->Release();
  }
}

// Scan building origins in the actual CRT permutation, retain five classification buckets and output their capped centers.
// Preserve the independent output/release orders, occupancy exclusions and X-size centering on both axes without extra draws.
void DsUfoInstance::SelectTargetBuildings(ScPointXZ *sites, u32 *count) {
  DsTargetScan scan;
  ScArray<ScOccupant *> landmarks, spaceports, low_tech, high_tech, power;
  u32 limit = DsUfoMaximumDestruction < *count ? DsUfoMaximumDestruction : *count;
  *count = 0;
  u32 width = manager->city->CellCountX(), height = manager->city->CellCountZ();
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(width - 1);
  scan.bottom = i32(height - 1);
  if (scan.right < scan.left)
    scan.right = scan.left;
  if (scan.bottom < scan.top)
    scan.bottom = scan.top;
  scan.dirty = 1;
  scan.Begin(1);
  i32 scan_x, scan_z;
  while (scan.Next(&scan_x, &scan_z)) {
    ScOccupant *occupant;
    if (!manager->occupants->GetOccupantAtCell(u32(scan_x), u32(scan_z), &occupant))
      continue;
    u32 x, z;
    occupant->GetLocationInCellCoords(&x, &z);
    bool retained = false;
    if (x == u32(scan_x) && z == u32(scan_z)) {
      BdBuilding *building;
      if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
        u32 occupancy = building->GetOccupancyState();
        building->Release();
        if (!(occupancy & (kDsDamageAbandon | kDsDamageRubble | kDsDamageBurn))) {
          switch (DsUfoClassifyTarget(occupant)) {
          case kDsUfoTargetLandmark:
            *landmarks.Append() = occupant;
            retained = true;
            break;
          case kDsUfoTargetSpaceport:
            *spaceports.Append() = occupant;
            retained = true;
            break;
          case kDsUfoTargetHighTech:
            *high_tech.Append() = occupant;
            retained = true;
            break;
          case kDsUfoTargetPower:
            *power.Append() = occupant;
            retained = true;
            break;
          default:
            if (!(occupant->GetGeomFlag() & (kOcGeomFlora | kOcGeomNetwork)) && low_tech.size < DsUfoMaximumLowTech) {
              *low_tech.Append() = occupant;
              retained = true;
            }
          }
        }
      }
    }
    if (!retained)
      occupant->Release();
  }
  ScArray<ScOccupant *> *output_order[] = {&low_tech, &landmarks, &spaceports, &high_tech, &power};
  u32 *maxima[] = {
      &DsUfoMaximumLowTech, &DsUfoMaximumLandmarks, &DsUfoMaximumSpaceports, &DsUfoMaximumHighTech, &DsUfoMaximumPower};
  for (u32 group = 0; group < 5; ++group) {
    ScArray<ScOccupant *> *bucket = output_order[group];
    u32 selected = bucket->size < *maxima[group] ? bucket->size : *maxima[group];
    for (u32 index = 0; index < selected; ++index) {
      if (*count >= limit)
        break;
      bucket->data[index]->GetLocationInCellCoords(&sites[*count].x, &sites[*count].z);
      u32 half = bucket->data[index]->SizeInCellsX() >> 1;
      sites[*count].x += half;
      sites[*count].z += half;
      ++*count;
    }
  }
  ScArray<ScOccupant *> *release_order[] = {&landmarks, &spaceports, &low_tech, &high_tech, &power};
  for (u32 group = 0; group < 5; ++group) {
    ScArray<ScOccupant *> *bucket = release_order[group];
    u32 held = bucket->size;
    for (u32 index = 0; index < held; ++index)
      bucket->data[index]->Release();
  }
}

// The original stack scope locks the graphics critical section and unlocks it on every exit.
class DsGraphicsScope {
public:
  // Enter the actual graphics lock before inspecting or placing crop cells.
  explicit DsGraphicsScope(ScLock *lock) : lock(lock) {
    lock->Lock();
  }

  // Release the held graphics lock after placement, rejection or exception unwinding.
  virtual ~DsGraphicsScope() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Validate the four-by-four crop footprint under the city's graphics lock, then place the original resource.
// Unsigned wrapped ranges retain the original empty-loop behavior; only successful placements advance the counter.
u8 DsUfoInstance::PlaceCrop(u32 x, u32 z) {
  DsGraphicsScope scope(manager->city->GetGraphicsCriticalSection());
  for (u32 cell_x = x; cell_x < x + 4; ++cell_x)
    for (u32 cell_z = z; cell_z < z + 4; ++cell_z)
      if (!DsUfoCropCell(manager->occupants, cell_x, cell_z))
        return 0;
  RsKey key = {kRsBuildingDataResourceType, kRsBuildingDataGroup, 5070};
  ScPointXZ point = {x, z};
  u8 result = manager->city->ZoneLayer()->PlaceBuilding(key, point, 1, 0, true);
  if (result)
    ++state.crop_circles;
  return result;
}

// Destroy a retained cell occupant and account for its captured target category before adding demolition cost.
// The original output aliases the initialized X operand, preserving its prior value until demolition writes it.
u8 DsUfoInstance::DestroyCell(u32 x, u32 z) {
  ScOccupant *occupant;
  u8 result = 0;
  if (manager->occupants->GetOccupantAtCell(x, z, &occupant)) {
    i32 kind = DsUfoClassifyTarget(occupant);
    u32 demolition_cost = x;
    result = manager->layer->DestroyOccupant(occupant, kDsDamageRubble, &demolition_cost);
    if (result) {
      switch (kind) {
      case kDsUfoTargetLandmark:
        ++state.landmarks;
        break;
      case kDsUfoTargetSpaceport:
        ++state.spaceports;
        break;
      case kDsUfoTargetHighTech:
      case kDsUfoTargetPower:
        ++state.advanced;
        break;
      default:
        ++state.low_tech;
        break;
      }
      state.cost += demolition_cost;
    }
    occupant->Release();
  }
  return result;
}

// Emit the original particle/fire effects before attempting cell demolition, independently of its success.
// Successful demolition adds cost before category accounting; the original helper always returns byte one.
u8 DsUfoInstance::BombCell(u32 x, u32 z) {
  ScOccupant *occupant;
  if (manager->occupants->GetOccupantAtCell(x, z, &occupant)) {
    i32 kind = DsUfoClassifyTarget(occupant);
    u16 altitude = occupant->GetAltAnimCells();
    i32 fixed_x = i32((x << kCtMicroCoordinateShift) + 128), fixed_z = i32((z << kCtMicroCoordinateShift) + 128);
    if (occupant->GetGeomFlag() & kOcGeomPower) {
      void *created;
      if (DsGetCom()->GetClassObject(kSpParticleEffectClassId, IID_SpParticleEffect, &created)) {
        auto *particle = static_cast<SpParticleEffect *>(created);
        particle->Init();
        particle->DoParticleGeneration(fixed_x, fixed_z, altitude + 2560, 1, 10);
        particle->Release();
      }
    }
    void *created;
    if (DsGetCom()->GetClassObject(kSpExplosionViewClassId, IID_SpExplosionView, &created)) {
      auto *fire = static_cast<SpExplosionView *>(created);
      fire->Init(fixed_x, fixed_z, altitude, 3, 2, 0);
      manager->box->Event(kAuEventPlayBuildingSound, i32(x), i32(z), 0, 0);
      fire->Release();
    }
    u32 demolition_cost;
    if (manager->layer->DestroyOccupant(occupant, kDsDamageRubble, &demolition_cost)) {
      state.cost += demolition_cost;
      switch (kind) {
      case kDsUfoTargetLandmark:
        ++state.landmarks;
        break;
      case kDsUfoTargetSpaceport:
        ++state.spaceports;
        break;
      case kDsUfoTargetHighTech:
      case kDsUfoTargetPower:
        ++state.advanced;
        break;
      default:
        ++state.low_tech;
        break;
      }
    }
    occupant->Release();
  }
  return 1;
}

// Count a permitted low-tech ignition attempt before forwarding it to the actual fire manager.
// The retained occupant is released after the fire callback, and the original result is always byte zero.
u8 DsUfoInstance::IgniteCell(u32 x, u32 z) {
  ScOccupant *occupant;
  if (manager->occupants->GetOccupantAtCell(x, z, &occupant)) {
    if (DsUfoClassifyTarget(occupant) == kDsUfoTargetLowTech && state.low_tech < DsUfoMaximumLowTech) {
      ++state.low_tech;
      auto *fire = static_cast<DsFireManager *>(manager->layer->GetDisasterManager(kDsFireTypeId));
      fire->IgniteOccupant(occupant, 0);
    }
    occupant->Release();
  }
  return 0;
}

// Consume active UFO effect notifications, preserving the independent random draws and payload acknowledgements.
// Other non-bomb messages with a nonzero first payload are acknowledged even when their type is unrecognized.
bool DsUfoInstance::DoMessage(ScMessage *message) {
  if (state.phase == kDsUfoAttacking || state.phase == kDsUfoDeparting) {
    if (message->type == kSpMsg_BuildingImpact) {
      if (u32(message->data3) == kSpUfoPlasmaTrajectorySignature) {
        u32 packed = u32(message->data2), x = packed >> 16, z = packed & 0xffffu;
        if (manager->random.Next(100) <= DsUfoFireChance)
          IgniteCell(x, z);
        else if (manager->random.Next(100) <= DsUfoDestructionChance)
          BombCell(x, z);
        message->data3 = 0;
      }
    } else if (message->data1) {
      switch (message->type) {
      case kSpMsg_UfoAbduction:
        ++state.abductions;
        break;
      case kSpMsg_UfoBuildingVaporized:
        DestroyCell(u32(message->data2) >> 16, u32(message->data2) & 0xffffu);
        break;
      case kSpMsg_UfoCropCircleCreated:
        PlaceCrop(u32(message->data2) >> 16, u32(message->data2) & 0xffffu);
        break;
      }
      message->data1 = 0;
    }
  }
  return true;
}

i8 DsUfoInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return state.phase >= kDsUfoAttacking ? levels[distance] : 0;
}

u32 DsUfoInstance::PanicRadius() {
  return state.phase >= kDsUfoAttacking ? 5 : 0;
}

u32 DsUfoInstance::GetCount() {
  return effect ? static_cast<SpUfoGroupSimulator *>(effect)->GetCount() : 0;
}

// Cycle through owned fleet children, falling back to the global fifty-ship query only when the owned count is zero.
// Preserve preincremented selection, borrowed child lifetime, logical output shifts and Z-before-X failure writes.
bool DsUfoInstance::GetLocation(u32 *x, u32 *z) {
  if (effect) {
    u32 count = static_cast<SpUfoGroupSimulator *>(effect)->GetCount();
    if (count) {
      ++location_index;
      if (location_index >= count)
        location_index = 0;
      SpUfoSimulator *simulator = static_cast<SpUfoGroupSimulator *>(effect)->GetUFO(location_index);
      if (simulator) {
        u32 altitude;
        simulator->GetPosition(x, z, &altitude);
        *x >>= 8;
        *z >>= 8;
        return true;
      }
    } else {
      SpUfoSimulator *objects[50];
      count = 50;
      static_cast<SpUfoGroupSimulator *>(effect)->GetAllUFOsAnywhere(objects, &count);
      if (count) {
        ++location_index;
        if (location_index >= count)
          location_index = 0;
        u32 altitude;
        objects[location_index]->GetPosition(x, z, &altitude);
        *x >>= 8;
        *z >>= 8;
        return true;
      }
    }
  }
  *z = kDsInvalidCellCoordinate;
  *x = kDsInvalidCellCoordinate;
  return false;
}

bool DsUfoInstance::IsActive() {
  return effect && static_cast<SpUfoGroupSimulator *>(effect)->GetUFO(0) != nullptr;
}

// Decrement manager activity and request asynchronous fleet retirement before clearing the stored controller.
// Clear UI state before its notices and remove the five subscriptions through one captured message server.
u8 DsUfoInstance::Shutdown() {
  if (initialized) {
    manager->InstanceEnded();
    if (effect) {
      static_cast<SpUfoGroupSimulator *>(effect)->ReleaseAsynchronous();
      effect = nullptr;
    }
    if (flag_33) {
      flag_33 = 0;
      manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterUfo, 0, 0, 0);
      manager->box->Event(kAuEventEffectSlot84, kAuEffectDisasterUfo, 0, 0, 0);
    }
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kDsMsg_UnresolvedUfoNotification);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoAbduction);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoBuildingVaporized);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_UfoCropCircleCreated);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_BuildingImpact);
    manager = nullptr;
    initialized = false;
  }
  return 1;
}

// Create the real fleet and queue the original building, crop, abduction and repeated migration instructions.
// Preserve callback-sensitive kind reads, signed level arithmetic, city-size floor, cheat override and final saved date.
u8 DsUfoInstance::ExecuteAttack() {
  ScPointXZ open_sites[256], target_sites[256];
  u32 open_count = 256, target_count = 256, ships = 1;
  if (!DsGetCom()->GetClassObject(kSpUfoGroupSimulatorClassId, IID_SpUfoGroupSimulator, &effect))
    return 0;
  u32 kind = state.kind;
  if (kind == kDsUfoAttack || kind == kDsUfoCropCircles) {
    SelectOpenSites(open_sites, &open_count);
    if (state.kind == kDsUfoAttack) {
      SelectTargetBuildings(target_sites, &target_count);
      ships = u32(3 * (i32(i8(flag_24)) + 2));
    } else {
      target_count = 0;
      ships = u32(3 * i32(i8(flag_24)) + 5);
    }
    u32 city_ships = (manager->city->CellCountX() >> 4) + 1;
    if (ships < city_ships)
      ships = city_ships;
  } else {
    target_count = 0;
    open_count = 0;
  }
  if (ships > DsUfoMaximumShips)
    ships = DsUfoMaximumShips;
  if (manager->cheats->GetCheatHasBeenSet(kDsUfoSwarmCheatId))
    ships = DsUfoMaximumCheatShips;
  static_cast<SpUfoGroupSimulator *>(effect)->Init(1, ships, i32(i8(flag_24)));
  if (state.kind != kDsUfoAttack) {
    u32 count = static_cast<SpUfoGroupSimulator *>(effect)->GetCount();
    for (u32 index = 0; index < count; ++index)
      static_cast<SpUfoGroupSimulator *>(effect)->GetUFO(index)->SetAutoAttack(0);
  }
  if (state.kind != kDsUfoFlyby)
    for (u32 count = 5; count; --count)
      static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionSuckUpPerson(nullptr, -1, 1);
  for (u32 index = 0; index < target_count; ++index)
    static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionVaporizeBuilding(
        i32(target_sites[index].x), i32(target_sites[index].z), -1, 1);
  for (u32 index = 0; index < open_count; ++index)
    static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionMakeCropCircle(
        i32(open_sites[index].x), i32(open_sites[index].z), -1, 1);
  if (ships == DsUfoMaximumCheatShips || state.kind == kDsUfoFlyby || target_count <= 2 || open_count <= 2) {
    i32 bottom = i32((manager->city->CellCountZ() << kCtMicroCoordinateShift) - 1);
    i32 right = i32((manager->city->CellCountX() << kCtMicroCoordinateShift) - 1);
    ScRect bounds{0, 0, right, bottom};
    for (u32 index = 0; index < ships; ++index) {
      static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionMigrateToArea(bounds, i32(index), 1);
      static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionMigrateToArea(bounds, i32(index), 1);
    }
  }
  CtDate date;
  manager->city->GetSimulationClock()->GetCurrentClockDate(&date);
  state.serial = date.serial;
  state.ships = ships;
  return 1;
}

// Queue one deferred departure per owned ship, descending from the captured fleet count.
// Each ship draws its original explosion decision before the current controller receives the instruction.
u8 DsUfoInstance::QueueDeparture() {
  if (effect) {
    u32 count = static_cast<SpUfoGroupSimulator *>(effect)->GetCount();
    while (count) {
      --count;
      u8 explode = manager->random.Next(10000) < DsUfoExitExplosionChance;
      static_cast<SpUfoGroupSimulator *>(effect)->AddInstructionSelfDestruct(explode, i32(count), 1);
    }
  }
  return 1;
}

// Advance premonitions, execute the attack, queue departures and retire after the global fleet has gone or timed out.
// Preserve current-controller reads and publish visibility only after its synchronous UI callback.
bool DsUfoInstance::Tick(DsTickType) {
  u32 phase = state.phase, elapsed = ++state.elapsed;
  bool update_visibility = false;
  switch (phase) {
  case kDsUfoInitialDelay:
  case kDsUfoFirstPremonition:
  case kDsUfoSecondPremonition:
    if (elapsed >= timing[phase]) {
      state.phase = phase + 1;
      PostPhase(phase + 1);
    }
    break;
  case kDsUfoAttackPending:
    if (elapsed >= timing[3]) {
      state.phase = kDsUfoAttacking;
      PostPhase(kDsUfoAttacking);
      if (ExecuteAttack()) {
        flag_33 = 1;
        manager->box->Event(kAuEventPlayEffect, kAuEffectDisasterUfo, 0, 0, 0);
        manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterUfo, 0, 1, 0);
      } else
        state.phase = kDsUfoAttacking;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case kDsUfoAttacking:
    if (elapsed >= timing[4]) {
      state.phase = kDsUfoDeparting;
      QueueDeparture();
    }
    update_visibility = effect != nullptr;
    break;
  case kDsUfoDeparting: {
    auto *simulator = static_cast<SpUfoGroupSimulator *>(effect);
    if (!simulator) {
      manager->FinishInstance(this);
      return true;
    }
    if (!simulator->DoesAnyUFOExistAnywhere()) {
      PostPhase(state.phase);
      ClearEffects();
      manager->FinishInstance(this);
      return true;
    }
    if (state.elapsed >= timing[3] + 600u) {
      PostPhase(state.phase);
      ClearEffects();
      manager->FinishInstance(this);
      if (effect)
        static_cast<SpUfoGroupSimulator *>(effect)->DestroyAllUFOs(1);
      return true;
    }
    update_visibility = true;
    break;
  }
  }
  if (update_visibility) {
    u8 visible = u8(static_cast<SpUfoGroupSimulator *>(effect)->IsAnyUFOCurrentlyVisible());
    if (flag_32 != visible)
      manager->box->Event(kAuEventEffectSlot56, kAuEffectDisasterUfo, 0, visible, 0);
    flag_32 = visible;
  }
  return true;
}

u32 DsWaterFirstDelay = 0, DsWaterSecondDelay = 60, DsWaterThirdDelay = 120;
u32 DsWaterAttackDelay = 180, DsWaterDuration = 300;
