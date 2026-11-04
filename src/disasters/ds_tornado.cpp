#include "disasters/ds_tornado.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"
#include "disasters/ds_toxic.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "misc/sc_graph_info.h"
#include "network/api.h"
#include "network/street.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/sp_effect.h"
#include "terrain/api.h"

// Riot fireball limits and water animation intervals retain independent configurable defaults.
enum DsEffectTimingDefault : u32 {
  kDsRiotDefaultMinimumFireballTime = 0x186a0,
  kDsRiotDefaultMaximumFireballTime = 0x493e0,
  kDsWaterDefaultFrameMicroseconds = 0x3d090,
};

// The axis remains unselected when neither sampled network direction is accepted.
enum DsTornadoAxis : u32 {
  kDsTornadoNoNetworkAxis = 0xffffffff,
};

// Tuning keys retain the roles of the named fields read from the variable manager.
enum DsWeatherPropertyId : u32 {
  kDsRiotMinimumUnemploymentPropertyId = 0x6484a25d,
  kDsRiotMinimumIntervalPropertyId = 0x2484a264,
  kDsRiotMaximumIntervalPropertyId = 0x6484a26a,
  kDsRiotMaximumAuraPropertyId = 0x4484a270,
  kDsRiotSearchRadiusPropertyId = 0x4484a278,
  kDsRiotMaximumDurationPropertyId = 0xe484a27d,
  kDsRiotMinimumDurationPropertyId = 0xa484a283,
  kDsRiotMinimumFireballTimePropertyId = 0x2484a289,
  kDsRiotMaximumFireballTimePropertyId = 0x2484a290,
  kDsRiotPremonitionTicksPropertyId = 0x2484a29e,
  kDsRiotInitialDelayPropertyId = 0x2484a2a5,
  kDsRiotCostPerTickPropertyId = 0x4484a2ac,
  kDsTornadoMinimumIntervalPropertyId = 0xe48381bc,
  kDsTornadoMaximumIntervalPropertyId = 0x48381c8,
  kDsTornadoMinimumSpeedPropertyId = 0xa48381d2,
  kDsTornadoMaximumSpeedPropertyId = 0x84838207,
  kDsTornadoDamageChance0PropertyId = 0x4483820f,
  kDsTornadoDamageChance1PropertyId = 0x84838216,
  kDsTornadoDamageChance2PropertyId = 0x8483821d,
  kDsTornadoMinimumDurationPropertyId = 0x24838224,
  kDsTornadoMaximumDurationPropertyId = 0x483822a,
  kDsTornadoFirstDelayPropertyId = 0x44838230,
  kDsTornadoSecondDelayPropertyId = 0xc4838239,
  kDsTornadoThirdDelayPropertyId = 0x483823f,
  kDsTornadoAttackDelayPropertyId = 0x6483824b,
  kDsTornadoFormationTicksPropertyId = 0xe4838253,
  kDsWaterMaximumActivePropertyId = 0x24860901,
  kDsWaterMinimumMonthsPropertyId = 0x24860902,
  kDsWaterMinimumStrengthPropertyId = 0x24860904,
  kDsWaterMaximumStrengthPropertyId = 0x24860905,
  kDsWaterChancePropertyId = 0x24860906,
  kDsWaterDamageRadiusPropertyId = 0x24860907,
  kDsWaterFirstDelayPropertyId = 0x2486090e,
  kDsWaterSecondDelayPropertyId = 0x2486090f,
  kDsWaterThirdDelayPropertyId = 0x24860910,
  kDsWaterAttackDelayPropertyId = 0x24860911,
  kDsWaterDurationPropertyId = 0x24860912,
  kDsWaterDebrisLifetimePropertyId = 0x24860913,
  kDsWaterSpeedTimes1000PropertyId = 0x24860914,
  kDsWaterFrameMicrosecondsPropertyId = 0x24860915,
  kDsWaterFastestSpeedTimes100000000PropertyId = 0x24860916,
  kDsWaterOuterGravityDistancePropertyId = 0x24860917,
};

enum DsUfoWaterArchiveKey : u32 {
  kDsUfoHistoryRecordGroup = 0x0336f76a,
  kDsUfoInstancesRecordGroup = 0xa303b249,
  kDsWaterHistoryRecordGroup = 0xc446f87c,
  kDsWaterInstancesRecordGroup = 0xe311c076,
};

// Original eight-byte block description; payload roles remain pending recovery of active parade construction.
struct DsParadeBlock {
  u32 first, second;
};

// Original 48-byte country event contains a packed calendar position and three owned vectors of block identifiers.
struct DsParadeEvent {
  u16 date_index, petition_month;
  u32 first, second;
  ScArray<u32> parts[3];
};

// Native owner for actual event records, replacing only the original vector's storage/destruction machinery.
class DsParadeEventList {
public:
  ~DsParadeEventList();
  void Clear();
  ScArray<DsParadeEvent *> entries;
};

static DsParadeEventList DsParadeEvents;
static ScArray<DsParadeBlock> DsParadeBlocks;
i32 DsParadeMaximumInstances = 1, DsParadeMinimumAura = 10;
u32 DsParadeRoadTilesRequired = 5, DsParadeMaximumDuration = 1000, DsParadeMinimumPopulation = 100;
u32 DsParadeMinimumCommercial = 10, DsParadeSpectatorDensity = 3, DsParadePetitionerTiming = 4;
u32 DsParadeMinimumMonths = 11, DsParadeEventProbability = 100, DsParadeUiId = 0;
i64 DsParadeCost = 100;

DsParadeEventList::~DsParadeEventList() {
  Clear();
}

// Destroy live records in source order, releasing each record's part vectors in reverse member order.
// Retain pointer-array capacity, as the original vector's erase phase does.
void DsParadeEventList::Clear() {
  for (u32 index = 0; index < entries.size; ++index)
    delete entries.data[index];
  entries.size = 0;
}

DsParadeManager::DsParadeManager()
    : initialized(false), layer(nullptr), aura(nullptr), city(nullptr), occupants(nullptr),
      random(kScRandomSeedFromClock), active_count(0), box(nullptr), event_index(0), state(1), last_event_month(0) {
}

DsParadeManager::~DsParadeManager() {
  DsParadeManager::Shutdown();
}

// Expose common-manager or parade-private identity and acquire the shared agent reference on success.
// Other queries retain the agent's original output-clearing behavior.
bool DsParadeManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsParadeControl, DsParadeControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsParadeManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsParadeManager::Release() {
  return DsAgent::Release();
}

// Bind the actual street-simulation, optional aura, application, occupant and controller services before subscribing.
// An initialized manager only shuts down and returns, deliberately skipping the original else branch's rebinding.
bool DsParadeManager::Init(DsLayer *retained_layer, CtCity *owner) {
  if (initialized) {
    Shutdown();
    return true;
  }
  city = ScAddRefNotNull(owner);
  columns = city->CellCountX();
  rows = city->CellCountZ();
  simulation = ScAddRefNotNull(city->StrtSimLayer());
  CtLayer *specific = city->GetSpecificLayer(kCtAuraLayerType);
  if (specific)
    specific->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
  agent_types = ScAddRefNotNull(DsGetAgentTree());
  box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
  occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  layer = ScAddRefNotNull(retained_layer);
  state = 1;
  active_count = 0;
  effects_count = 0;
  visible_count = 0;
  event_index = 0;
  start_x = 0;
  start_z = 0;
  mode = 0;
  resume = 0;
  initialized = true;
  ScMessageServer *server = DsGetMessageServer();
  server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  server->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_CitySaveStarting);
  server->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_CitySaveFinished);
  return true;
}

// Remove all three subscriptions through one server, then release retained services in original order.
// Keep pointers and readiness published through callbacks, clearing readiness only after the final city release.
bool DsParadeManager::Shutdown() {
  if (initialized) {
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_CitySaveStarting);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_CitySaveFinished);
    ScRelease(&layer);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&simulation);
    ScRelease(&aura);
    ScRelease(&city);
    initialized = false;
  }
  return true;
}

i32 DsParadeManager::AcceptsKind(u32 kind) {
  return kind == 29 || kind == 39 || kind == 43;
}

i32 DsParadeManager::IsReady() {
  return state == 1;
}

i32 DsParadeManager::IsEnding() {
  return state == 19;
}

// Move only active states13 through18 to19, retaining the observed prior-state DWORD return.
u32 DsParadeManager::RequestEnd() {
  u32 previous = state;
  if (previous >= 13 && previous <= 18)
    state = 19;
  return previous;
}

// Read scalar state directly into the manager in original order, preserving partial writes and raw saved bytes.
// Version1 leaves the final word untouched; version2 and later read it after the four signed bounds fields.
bool DsParadeManager::Init(CtCity *owner, RsDatabaseSegment *segment) {
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsParadeRecordGroup, 0});
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  u8 result = stream->ReadU32(&state);
  result = result && stream->ReadU8(&resume) && stream->ReadU32(&event_index) && stream->ReadU32(&start_x) &&
      stream->ReadU32(&start_z) && stream->ReadU8(&mode) && stream->ReadI32(&bounds.left) &&
      stream->ReadI32(&bounds.top) && stream->ReadI32(&bounds.right) && stream->ReadI32(&bounds.bottom);
  if (result && archive.version > 1)
    result = stream->ReadU32(&last_event_month);
  return result;
}

// Save version2 with the temporary live-instance byte published across its write and cleared immediately afterward.
// Preserve callback ordering, raw mode, scalar failures and the original signed write of the final unsigned-read word.
bool DsParadeManager::Save(CtCity *owner, RsDatabaseSegment *segment) {
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsParadeRecordGroup, 0}, 2, 0);
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  u8 result = stream->WriteU32(state);
  if (!result)
    return false;
  resume = u8(i32(active_count) > 0);
  result = stream->WriteU8(resume);
  resume = 0;
  result = result && stream->WriteU32(event_index) && stream->WriteU32(start_x) && stream->WriteU32(start_z) &&
      stream->WriteU8(mode) && stream->WriteI32(bounds.left) && stream->WriteI32(bounds.top) &&
      stream->WriteI32(bounds.right) && stream->WriteI32(bounds.bottom) && stream->WriteI32(i32(last_event_month));
  return result;
}

// Clear a pending replay before the lookup and forward the held occupant and saved raw mode to the original start.
// Preserve the absence of a release here; the actual start dependency controls the acquired occupant's lifetime.
u8 DsParadeManager::Resume() {
  if (!resume)
    return 1;
  resume = 0;
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(start_x, start_z, &occupant))
    return StartOccupant(occupant, mode);
  return 0;
}

// Dispatch load replay unconditionally and occupant-protection transitions only while a signed-positive parade runs.
bool DsParadeManager::DoMessage(ScMessage *message) {
  if (message->type == kCtMsg_CitySaveStarting) {
    if (i32(active_count) > 0)
      RestoreOccupants();
  } else if (message->type == kCtMsg_CitySaveFinished) {
    if (i32(active_count) > 0)
      ProtectOccupants();
  } else if (message->type == kWnMsg_CityViewReady) {
    Resume();
  } else {
    return false;
  }
  return true;
}

bool DsParadeManager::Tick() {
  return true;
}

// Extract the parade parser's single-character prefix and remove it with its delimiter from the working copy.
// A missing delimiter returns an empty field and leaves the input unchanged, matching the original String operation.
static ScString DsParadeTakeField(ScString *source, char separator) {
  u32 length = source->Strlen(), position = 0;
  while (position < length && source->begin[position] != separator)
    ++position;
  if (position == length)
    return ScString();
  ScString field(source->begin, position);
  source->Erase(0, position + 1);
  return field;
}

// Resolve a one-based block key against records appended by configuration enumeration.
// The original accesses the selected record directly; debug builds check its storage invariant.
static DsParadeBlock *DsParadeBlockSlot(u32 key) {
  u32 index = key - 1;
  assert(index < DsParadeBlocks.size);
  return DsParadeBlocks.data + index;
}

// Append nonzero block identifiers with the original empty-prefix fallback, preserving leading-comma behavior.
// A parsed zero ends the list even if the working string still contains later fields.
static void DsParadeReadParts(ScString *source, ScArray<u32> *parts) {
  while (true) {
    ScString field = DsParadeTakeField(source, ',');
    if (!field.Strlen()) {
      field = *source;
      source->FromChar("");
    }
    u32 value = DsParseNumber(field.ToChar());
    if (!value)
      break;
    *parts->Append() = value;
  }
}

static void DsParadeReadMaximum(const ScString &, const ScString &value, void *) {
  ScString copy(value);
  DsParadeMaximumInstances = i32(DsParseNumber(copy.ToChar()));
}

// Preserve empty first fields when no comma exists; this callback has no list-parser fallback.
static void DsParadeReadBlock(const ScString &key, const ScString &value, void *) {
  ScString copy(value);
  u32 index = DsParseNumber(key.ToChar());
  DsParadeBlocks.Append();
  ScString first = DsParadeTakeField(&copy, ',');
  DsParadeBlock *block = DsParadeBlockSlot(index);
  block->first = DsParseNumber(first.ToChar());
  block = DsParadeBlockSlot(index);
  block->second = DsParseNumber(copy.ToChar());
}

// Accept only the current country's exact ISO key, split the six original fields and append one real event record.
// Preserve packed month/day arithmetic, the early-month petition adjustment and all three zero-terminated block lists.
static void DsParadeReadEvent(const ScString &key, const ScString &value, void *) {
  ScString country;
  ApPreferences *preferences = DsGetPreferences();
  ScLanguageManager *language = DsGetLanguageManager();
  language->GetISO3166CountryCodeFromCountryID(&country, u32(preferences->GetCountry()));
  if (!country.IsEqual(&key))
    return;
  ScString copy(value);
  ScString date = DsParadeTakeField(&copy, ':');
  ScString scalar = DsParadeTakeField(&copy, ':');
  ScString pair = DsParadeTakeField(&copy, ':');
  ScString front = DsParadeTakeField(&copy, ':');
  ScString middle = DsParadeTakeField(&copy, ':');
  ScString tail(copy);
  auto *event = new DsParadeEvent;
  *DsParadeEvents.entries.Append() = event;
  ScString month_field = DsParadeTakeField(&date, '/');
  u32 month = DsParseNumber(month_field.ToChar()), day = DsParseNumber(date.ToChar());
  event->date_index = u16(31u * month + day - 32);
  u32 petition_month = month;
  if (day <= 3)
    petition_month = month == 1 ? 12 : month - 1;
  event->petition_month = u16(petition_month);
  DsParadeUiId = DsParseNumber(scalar.ToChar());
  ScString first = DsParadeTakeField(&pair, ',');
  event->first = DsParseNumber(first.ToChar());
  event->second = DsParseNumber(pair.ToChar());
  DsParadeReadParts(&front, &event->parts[0]);
  DsParadeReadParts(&middle, &event->parts[1]);
  DsParadeReadParts(&tail, &event->parts[2]);
}

// Read maximum instances, ordered scalar tunables, block descriptions and country events through the real INI reader.
// Retain missing values and existing records; the original initialization performs no implicit configuration clear.
bool DsParadeManager::StaticInit() {
  DsIni reader;
  ScString path, value;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  {
    ScString section("ParadeEvent");
    reader.EnumEntries(section, DsParadeReadMaximum, nullptr);
  }
  const char *keys[] = {"RoadTilesRequired", "MaxParadeDuration", "ParadeCost", "MinPopulation", "MinCommercial",
      "MinAura", "SpectatorDensity", "PetitionerTiming", "MinMonthsBetweenEvents", "EventProbability"};
  for (u32 index = 0; index < 10; ++index) {
    u8 found;
    {
      ScString key(keys[index]), section("ParadeEventTunables");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      u32 parsed = DsParseNumber(value.ToChar());
      switch (index) {
      case 0:
        DsParadeRoadTilesRequired = parsed;
        break;
      case 1:
        DsParadeMaximumDuration = parsed;
        break;
      case 2:
        DsParadeCost = parsed;
        break;
      case 3:
        DsParadeMinimumPopulation = parsed;
        break;
      case 4:
        DsParadeMinimumCommercial = parsed;
        break;
      case 5:
        DsParadeMinimumAura = i32(parsed);
        break;
      case 6:
        DsParadeSpectatorDensity = parsed;
        break;
      case 7:
        DsParadePetitionerTiming = parsed;
        break;
      case 8:
        DsParadeMinimumMonths = parsed;
        break;
      case 9:
        DsParadeEventProbability = parsed;
        break;
      }
    }
  }
  {
    ScString section("ParadeEventParadeBlocks");
    reader.EnumEntries(section, DsParadeReadBlock, nullptr);
  }
  {
    ScString section("ParadeEventParadeConfigs");
    reader.EnumEntries(section, DsParadeReadEvent, nullptr);
  }
  return true;
}

// Clear block descriptions before destroying event-owned part vectors, retaining both top-level array capacities.
bool DsParadeManager::StaticShutdown() {
  DsParadeBlocks.size = 0;
  DsParadeEvents.Clear();
  return true;
}

// Select the nearest configured calendar event, allowing later equal-distance rows to replace earlier choices.
// Require strictly more funds than the cost, release the selected occupant after starting and charge only on success.
bool DsParadeManager::AttemptDisasterStart() {
  if (i32(active_count) >= DsParadeMaximumInstances || !DsParadeEvents.entries.size)
    return false;
  if (DsGetCity()->BudgetLayer()->GetTotalFunds() <= DsParadeCost)
    return false;
  u32 month, day, year;
  DsGetCity()->GetDate(&month, &day, &year);
  u32 calendar = 31u * month + day - 32, closest = 366;
  event_index = 0;
  for (u32 index = 0; index < DsParadeEvents.entries.size; ++index) {
    i32 difference = i32(u32(DsParadeEvents.entries.data[index]->date_index) - calendar);
    u32 distance = difference < 0 ? 0u - u32(difference) : u32(difference);
    if (distance > 183)
      distance = 366u - distance;
    if (distance <= closest) {
      closest = distance;
      event_index = index;
    }
  }
  ScOccupant *occupant;
  u8 direction;
  if (!FindStart(&occupant, &direction)) {
    PostNoRoute();
    return false;
  }
  u8 result = StartOccupant(occupant, direction);
  occupant->Release();
  event_index = 0;
  if (result)
    DsGetCity()->BudgetLayer()->WithdrawFunds(u32(DsParadeCost));
  return result;
}

// Try the original centered random origin before falling back to half of each city dimension.
// Forward the route search byte and genuine occupant/direction outputs without initializing them in this wrapper.
u8 DsParadeManager::FindStart(ScOccupant **occupant, u8 *direction) {
  DsTileScan scan;
  u32 width = city->CellCountX(), height = city->CellCountZ();
  u32 origin_x = u32(random.Centered(0, i32(width - 1)));
  u32 origin_z = u32(random.Centered(0, i32(height - 1)));
  u8 result = FindRoute(occupant, direction, origin_x, origin_z);
  if (!result)
    result = FindRoute(occupant, direction, width >> 1, height >> 1);
  return result;
}

void DsParadeManager::PostNoRoute() {
  auto *text = new ScString;
  DsTextResource content(kRsNewsParadeCancelledNoStreetLongEnoughForText, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, 1005, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
}

// Accept an axis1 road cell only when its connection sample and the manager's piece-kind predicate both allow it.
// Retire the queried connection before releasing the retained occupant, preserving the original short-circuit calls.
u8 DsParadeManager::WalkAxis1(u32 x, u32 z) {
  ScOccupant *occupant = nullptr;
  u8 result = 0;
  if (occupants->GetOccupantAtCell(x, z, &occupant)) {
    if ((occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad) {
      NwConnection *connection = nullptr;
      if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
        u8 first, second, fourth;
        connection->GetConnectionSamples(0, 1, 0, &first, &second, &result, &fourth);
        result = result && u8(AcceptsKind(connection->TileId()));
        connection->Release();
      }
    }
    occupant->Release();
  }
  return result;
}

// Test the axis2 sample from side3 before applying the same piece-kind filter used by axis1.
// Preserve the original connection/occupant acquisition order and normalize only the returned predicate byte.
u8 DsParadeManager::WalkAxis2(u32 x, u32 z) {
  ScOccupant *occupant = nullptr;
  u8 result = 0;
  if (occupants->GetOccupantAtCell(x, z, &occupant)) {
    if ((occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad) {
      NwConnection *connection = nullptr;
      if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
        u8 first, third, fourth;
        connection->GetConnectionSamples(0, 1, 3, &first, &result, &third, &fourth);
        result = result && u8(AcceptsKind(connection->TileId()));
        connection->Release();
      }
    }
    occupant->Release();
  }
  return result;
}

// Spiral over the inner city, requiring aura, neighboring road tiles and an eligible straight connection run. Publish
// the selected direction, route bounds and start cell together.
u8 DsParadeManager::FindRoute(ScOccupant **occupant, u8 *direction, u32 origin_x, u32 origin_z) {
  DsTileScan scan;
  u32 width = city->CellCountX(), height = city->CellCountZ();
  scan.left = 1;
  scan.top = 1;
  scan.right = i32(width - 2);
  scan.bottom = i32(height - 2);
  scan.Reset(i32(origin_x), i32(origin_z));
  i32 output_x, output_z;
  while (scan.Next(&output_x, &output_z)) {
    if (!occupants->GetOccupantAtCell(u32(output_x), u32(output_z), occupant))
      continue;
    if (((*occupant)->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad) {
      CtLayer *rating_layer = DsGetCity()->GetSpecificLayer(kCtAuraLayerType);
      if (!rating_layer)
        goto rejected;
      CtAuraLayer *rating = nullptr;
      if (!rating_layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&rating)))
        goto rejected;
      i8 value = 0;
      rating->GetValue(u32(output_x), u32(output_z), &value);
      bool aura_allowed = u32(i32(value)) >= u32(DsParadeMinimumAura);
      rating->Release();
      if (!aura_allowed)
        goto rejected;
      CtZoneLayer *zones = DsGetCity()->ZoneLayer();
      if (!zones)
        goto rejected;
      u32 road_tiles = 0, x = u32(output_x), z = u32(output_z);
      if (x - 1 > x + 1)
        goto rejected;
      for (u32 neighbor_x = x - 1; neighbor_x <= x + 1; ++neighbor_x) {
        for (u32 neighbor_z = z - 1; neighbor_z <= z + 1; ++neighbor_z) {
          u8 zone;
          zones->GetValue(neighbor_x, neighbor_z, &zone);
          if (zone == 6)
            ++road_tiles;
        }
      }
      if (road_tiles < 3)
        goto rejected;
      NwConnection *connection = nullptr;
      if ((*occupant)->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
        u8 axis1 = 0, axis2 = 0, first, second, third, fourth;
        connection->GetConnectionSamples(0, 1, 0, &first, &second, &axis1, &fourth);
        connection->GetConnectionSamples(0, 1, 3, &first, &axis2, &third, &fourth);
        axis1 = axis1 && u8(AcceptsKind(connection->TileId()));
        axis2 = axis2 && u8(AcceptsKind(connection->TileId()));
        connection->Release();
        u32 axis = kDsTornadoNoNetworkAxis;
        if (axis1)
          axis = axis2 ? random.Next(2) : 0;
        else if (axis2)
          axis = 1;
        u32 choice = random.Next(2), positive = 0, negative = 0;
        if (axis == 1) {
          for (u32 distance = 1; z + distance <= height - 2; ++distance) {
            if (!WalkAxis2(x, z + distance))
              break;
            ++positive;
          }
          for (u32 distance = 1; z - distance != 0; ++distance) {
            if (!WalkAxis2(x, z - distance))
              break;
            ++negative;
          }
          if ((choice != 0 || negative + 1 >= DsParadeRoadTilesRequired) &&
              (choice != 1 || positive + 1 >= DsParadeRoadTilesRequired)) {
            if (choice == 0) {
              *direction = 8;
              bounds.left = output_x;
              bounds.right = output_x;
              bounds.top = i32(z - negative);
              bounds.bottom = output_z;
            } else {
              *direction = 24;
              bounds.left = output_x;
              bounds.right = output_x;
              bounds.top = output_z;
              bounds.bottom = i32(z + positive);
            }
            start_x = x;
            start_z = z;
            mode = *direction;
            return 1;
          }
        } else if (axis == 0) {
          for (u32 distance = 1; x - distance != 0; ++distance) {
            if (!WalkAxis1(x - distance, z))
              break;
            ++negative;
          }
          for (u32 distance = 1; x + distance <= width - 2; ++distance) {
            if (!WalkAxis1(x + distance, z))
              break;
            ++positive;
          }
          if ((choice != 0 || positive + 1 >= DsParadeRoadTilesRequired) &&
              (choice != 1 || negative + 1 >= DsParadeRoadTilesRequired)) {
            if (choice == 0) {
              *direction = 16;
              bounds.left = output_x;
              bounds.right = i32(x + positive);
            } else {
              *direction = 0;
              bounds.left = i32(x - negative);
              bounds.right = output_x;
            }
            bounds.top = output_z;
            bounds.bottom = output_z;
            start_x = x;
            start_z = z;
            mode = *direction;
            return 1;
          }
        }
      }
    }
  rejected:
    (*occupant)->Release();
    *occupant = nullptr;
  }
  return 0;
}

void DsParadeManager::InstanceStarted() {
  ++active_count;
}

// Drop an active instance only while the signed interpretation of the count is positive.
// Return the resulting original DWORD count, including unchanged nonpositive signed representations.
u32 DsParadeManager::InstanceStopped() {
  if (i32(active_count) > 0)
    --active_count;
  return active_count;
}

void DsParadeManager::EffectsStarted() {
  if (++effects_count == 1)
    box->Event(kAuEventPlayEffect, i32(DsParadeUiId), i32(u32(reinterpret_cast<uptr>(this))), 0, 0);
}

// Retire a positive signed effect count and notify the UI when its last live effect disappears.
void DsParadeManager::EffectsStopped() {
  if (i32(effects_count) > 0 && --effects_count == 0) {
    box->Event(kAuEventEffectSlot52, i32(DsParadeUiId), 0, 0, 0);
    RestoreOccupants();
  }
}

// Notify removal before the instance's real teardown and final reference release.
// Preserve the release-count transport without inventing a Boolean stop result.
u32 DsParadeManager::FinishInstance(DsParadeInstance *instance) {
  layer->DisasterInstanceChange(instance, 4);
  instance->Shutdown();
  return instance->Release();
}

// Protect the selected route, construct the real first parade block and choose its count and front-part configuration.
// Hold the new instance across initialization; a refused initialization releases it, while success receives its first tick.
u8 DsParadeManager::StartOccupant(ScOccupant *occupant, u8 direction) {
  ProtectOccupants();
  auto *instance = new DsParadeInstance;
  instance->AddRef();
  u32 cell_x, cell_z;
  occupant->GetLocationInCellCoords(&cell_x, &cell_z);
  DsParadeEvent *event = DsParadeEvents.entries.data[event_index];
  instance->remaining = u32(random.Range(i32(event->first), i32(event->second + 1))) - 1;
  u32 index = random.Next(event->parts[0].size), key = event->parts[0].data[index];
  DsParadeBlock *block = DsParadeBlockSlot(key);
  instance->sprite = block->first;
  instance->delay = block->second;
  if (!instance->Init(this, cell_x, cell_z, direction)) {
    instance->Release();
    return 0;
  }
  instance->Tick(DsDayTick);
  return 1;
}

// Construct a following block at the captured starting cell, choosing a middle part while blocks remain or the tail otherwise.
// Populate the selected real sprite/delay fields before initialization and preserve the same success/first-tick sequence.
u8 DsParadeManager::StartNext(u32 cell_x, u32 cell_z, u8 direction, u32 remaining) {
  auto *instance = new DsParadeInstance;
  instance->AddRef();
  instance->remaining = remaining;
  DsParadeEvent *event = DsParadeEvents.entries.data[event_index];
  ScArray<u32> *parts = &event->parts[remaining ? 1 : 2];
  u32 index = random.Next(parts->size), key = parts->data[index];
  DsParadeBlock *block = DsParadeBlockSlot(key);
  instance->sprite = block->first;
  instance->delay = block->second;
  if (!instance->Init(this, cell_x, cell_z, direction)) {
    instance->Release();
    return 0;
  }
  instance->Tick(DsDayTick);
  return 1;
}

// Advance petition eligibility, the original waiting states and the month-based cooldown using the first configured
// event.
u8 DsParadeManager::AdvanceSchedule() {
  if (i32(active_count) >= DsParadeMaximumInstances || !DsParadeEvents.entries.size)
    return 1;
  if (state == 1 && random.Next(100) <= DsParadeEventProbability) {
    u32 month, day, year;
    DsGetCity()->GetDate(&month, &day, &year);
    u32 months = u32(DsParadeEvents.entries.data[0]->petition_month) - month;
    if (i32(months) < 0)
      months += 12;
    if (months <= DsParadePetitionerTiming && day < 7) {
      bool eligible = false;
      if (DsGetCity()->BudgetLayer()->GetTotalFunds() > DsParadeCost &&
          u32(DsGetCity()->ResidentialLayer()->GetPopulation()) > DsParadeMinimumPopulation &&
          u32(DsGetCity()->BuildingLayer()->CheckOccupantTracking(kCtAgentCommercial)) >= DsParadeMinimumCommercial)
        eligible = true;
      CtLayer *rating_layer = DsGetCity()->GetSpecificLayer(kCtAuraLayerType);
      if (rating_layer) {
        CtAuraLayer *rating = nullptr;
        if (rating_layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&rating))) {
          eligible = eligible && u32(i32(rating->GetMayorRating())) >= u32(DsParadeMinimumAura);
          rating->Release();
        }
      }
      if (eligible) {
        ScOccupant *occupant = nullptr;
        u8 direction;
        if (FindStart(&occupant, &direction)) {
          occupant->Release();
          ScMessage message = {kAdMsg_QueuePetition, kAdParadePetitionerId, DsParadePetitionerTiming, 0};
          DsGetMessageServer()->MessageSend(message);
          state = 13;
        }
      }
    }
    return 1;
  }
  if (state == 19) {
    u32 month, day, year;
    DsGetCity()->GetDate(&month, &day, &year);
    CtDate current(month, day, year), event;
    u32 packed = DsParadeEvents.entries.data[0]->date_index;
    u32 event_month = packed / 31 + 1, event_day = packed % 31 + 1, event_year = year;
    if (month == 1 && event_month == 12)
      --event_year;
    // Inline the original date setter's single serial write through the established calendar encoder.
    event.serial = CtCalendarEncode(event_month, event_day, event_year);
    i32 difference = i32(current.serial - event.serial);
    u32 distance = difference < 0 ? 0u - u32(difference) : u32(difference);
    if (distance < 7) {
      AttemptDisasterStart();
      u32 event_year_value = event.Year();
      last_event_month = event.Month() + 12u * event_year_value;
    }
    return 1;
  }
  if (!state) {
    u32 month, day, year;
    DsGetCity()->GetDate(&month, &day, &year);
    if (i32(month + 12u * year - last_event_month) < i32(DsParadeMinimumMonths - DsParadePetitionerTiming))
      return 1;
  } else if (state >= 13 && state < 18) {
    ++state;
    return 1;
  }
  state = 1;
  return 1;
}

// Shut down the supplied parade before releasing its instance reference, preserving the original callback order.
void DsParadeManager::StopInstance(DsInstance *instance) {
  auto *parade = static_cast<DsParadeInstance *>(instance);
  parade->Shutdown();
  parade->Release();
}

// Construct the actual manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateClass84C92CBE() {
  return static_cast<DsManager *>(new DsParadeManager);
}

// Protect occupied cells in the inclusive unsigned route rectangle and stop new vehicle entrance on their network blocks.
// The original geometry provider initializes every genuine output, so the ignored lookup result does not expose an unwritten pointer.
void DsParadeManager::ProtectOccupants() {
  for (u32 x = u32(bounds.left); x <= u32(bounds.right); ++x) {
    for (u32 z = u32(bounds.top); z <= u32(bounds.bottom); ++z) {
      ScOccupant *occupant;
      occupants->GetOccupantAtCell(x, z, &occupant);
      if (occupant) {
        occupant->SetSimRemove(false);
        occupant->SetUserRemove(false);
        NwConnection *connection = nullptr;
        if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
          connection->GetTrafficBlock()->DisallowVehicleEntrance();
          connection->Release();
          connection = nullptr;
        }
        occupant->Release();
      }
    }
  }
}

// Restore both occupant removal permissions and vehicle entrance in the same inclusive route scan.
// A refused cell lookup ends the entire original scan; release each queried connection before its retained occupant.
void DsParadeManager::RestoreOccupants() {
  for (u32 x = u32(bounds.left); x <= u32(bounds.right); ++x) {
    for (u32 z = u32(bounds.top); z <= u32(bounds.bottom); ++z) {
      ScOccupant *occupant;
      if (!occupants->GetOccupantAtCell(x, z, &occupant))
        return;
      occupant->SetSimRemove(true);
      occupant->SetUserRemove(true);
      NwConnection *connection = nullptr;
      if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
        connection->GetTrafficBlock()->AllowVehicleEntrance();
        connection->Release();
        connection = nullptr;
      }
      occupant->Release();
    }
  }
}

extern u32 DsParadeUiId, DsParadeSpectatorDensity, DsParadeMaximumDuration;

// Original3A2A0 scales the wrapping unsigned animation clock into parade time units.
static u32 DsParadeTickScale = 20000;

DsParadeInstance::DsParadeInstance()
    : initialized(false), in_camera(false), references(0), manager(nullptr), effect(nullptr), elapsed(0), x(0), z(0),
      phase(0), effects{kApInvalidNotificationId, kApInvalidNotificationId}, running(0), retarget(false),
      flag_74(false), direction(0xff), remaining(0), sprite(0), delay(0) {
}

DsParadeInstance::~DsParadeInstance() {
  Shutdown();
}

// Expose primary Unknown/disaster or the actual private parade motion identity and retain exactly once on success. A
// refused query clears the output.
bool DsParadeInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsParadeMotion, DsParadeMotion);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsParadeInstance::AddRef() {
  return ++references;
}

// Decrement a held reference without wrapping zero, deleting the complete instance when no references remain.
// Teardown occurs through the actual destructor before the native object allocation retires.
u32 DsParadeInstance::Release() {
  if (references)
    --references;
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Preserve both original city-dimension calls before publishing the initialized single-cell bounds.
// An inactive receiver clears bottom/top/right/left without accessing its manager.
bool DsParadeInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    manager->city->CellCountX();
    manager->city->CellCountZ();
    output->right = x;
    output->left = x;
    output->bottom = z;
    output->top = z;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

// Publish the current cell in X/Z order without a readiness test, preserving the shared original leaf.
bool DsParadeInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = x;
  *output_z = z;
  return true;
}

// Preserve the common null occupant result; the parade owns a specialized crowd rather than one occupant reference.
ScOccupant *DsParadeInstance::GetOccupant() {
  return nullptr;
}

// Borrow the common identity of the retained manager relationship, including its pre-initialization null value.
DsManager *DsParadeInstance::GetManager() {
  return manager;
}

u32 DsParadeInstance::TypeId() {
  return kDsParadeTypeId;
}

u32 DsParadeInstance::GetDestroyedCount() {
  return 0;
}

u32 DsParadeInstance::GetCost() {
  return 0;
}

bool DsParadeInstance::Tick(DsTickType tick) {
  return Advance(tick, 1);
}

// Publish current and starting coordinates, owner and phase before registration and the first direct phase step.
// Keep normal tick progression gated until both synchronous callbacks have returned.
u8 DsParadeInstance::Init(DsParadeManager *owner, u32 cell_x, u32 cell_z, u8 initial_direction) {
  x = cell_x;
  start_x = cell_x;
  manager = owner;
  z = cell_z;
  start_z = cell_z;
  phase = 0;
  initialized = true;
  manager->InstanceStarted();
  running = 0;
  direction = initial_direction;
  manager->layer->DisasterInstanceChange(this, 0);
  Advance(0, 0);
  running = 1;
  return 1;
}

// Publish the current cell and increment an existing visit count, or record its first successful occupant lookup.
// Release that lookup before inserting count1 and requesting real street-simulation spectators.
void DsParadeInstance::EnterCell(u32 cell_x, u32 cell_z) {
  if (initialized) {
    x = cell_x;
    z = cell_z;
    u32 key = cell_x + (cell_z << 16), index = 0;
    while (index < cells.size && cells.data[index].key < key)
      ++index;
    if (index < cells.size && cells.data[index].key == key) {
      cells.data[index].count = i32(u32(cells.data[index].count) + 1);
    } else {
      ScOccupant *occupant;
      if (manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
        occupant->Release();
        index = 0;
        while (index < cells.size && cells.data[index].key < key)
          ++index;
        if (index == cells.size || cells.data[index].key != key) {
          cells.Append();
          for (u32 move = cells.size - 1; move > index; --move)
            cells.data[move] = cells.data[move - 1];
        }
        cells.data[index] = {key, 1};
        manager->simulation->CreateParadeSpectators(cell_x, cell_z, DsParadeSpectatorDensity);
      }
    }
  }
}

// Decrement the cell's signed-tested count, then erase its entry even when the result remains positive.
void DsParadeInstance::LeaveCell(u32 cell_x, u32 cell_z) {
  if (initialized) {
    u32 key = cell_x + (cell_z << 16), index = 0;
    while (index < cells.size && cells.data[index].key < key)
      ++index;
    if (index < cells.size && cells.data[index].key == key) {
      cells.data[index].count = i32(u32(cells.data[index].count) - 1);
      for (u32 move = index + 1; move < cells.size; ++move)
        cells.data[move - 1] = cells.data[move];
      --cells.size;
    }
  }
}

void DsParadeInstance::RequestRetarget() {
  retarget = true;
}

i32 DsParadeInstance::AcceptsKind(u32 kind) {
  return manager && u8(manager->AcceptsKind(kind));
}

// Look up and retain the current cell's occupant, requiring the common riot/parade geometry predicate before attachment.
// Release it after the attachment callback and preserve its raw byte result.
u8 DsParadeInstance::TryStartCell(u32 cell_x, u32 cell_z) {
  ScOccupant *occupant;
  if (!manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant))
    return 0;
  u8 result = u8(DsRiotAcceptsOccupant(occupant));
  if (result)
    result = AttachToOccupant(occupant);
  occupant->Release();
  return result;
}

// Start the actual crowd on phase0, notify the first effect and register the active transition before resetting timers.
// Later phase1 steps refresh camera visibility after the original unsigned four-tick threshold.
u8 DsParadeInstance::Advance(i32, u8 tick_running) {
  if (!initialized)
    return 0;
  if (!tick_running || running) {
    if (!phase) {
      TryStartCell(x, z);
      phase = 1;
      if (!manager->effects_count)
        PostPhase(1);
      manager->EffectsStarted();
      manager->layer->DisasterInstanceChange(this, 1);
      elapsed = 0;
      camera_ticks = 5;
    } else if (phase == 1 && ++camera_ticks > 4u) {
      camera_ticks = 0;
      RefreshVisibility();
    }
  }
  return 1;
}

// Publish phase2 and the final-effect notice before asking the real manager to remove and release this instance.
u32 DsParadeInstance::Finish() {
  phase = 2;
  if (manager->effects_count == 1)
    PostPhase(2);
  return manager->FinishInstance(this);
}

// Phase2 performs no resource lookup, random draw or notice; preserve that original empty branch.
void DsParadeInstance::PostPhase(u32 state) {
  if (state != 1)
    return;
  u32 resource = 1002u - (manager->random.Next(2) != 0);
  DsNoticePayload notice;
  {
    auto *text = new ScString;
    DsTextResource content(resource, kRsNewsTextGroup);
    content.CopyTo(text);
    notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
    text->AddRef();
    ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
    DsGetMessageServer()->MessageSend(message);
    text->Release();
  }
  effects[0] = notice.effect;
}

// Remove both transient notices in order, publishing the invalid sentinel only after each synchronous send returns.
// The ignored residual message result is not promoted to a normal game return contract.
void DsParadeInstance::ClearEffects() {
  for (u32 index = 0; index < 2; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Compare the actual location with camera containment and publish transitions only when the raw result changes.
// Update the manager's visible count and UI event before recording the new local visibility byte.
void DsParadeInstance::RefreshVisibility() {
  u32 cell_x, cell_z;
  if (GetLocation(&cell_x, &cell_z)) {
    u8 visible = u8(DsController::IsInVisibleRect(cell_x, cell_z));
    if (visible != in_camera) {
      if (visible) {
        if (++manager->visible_count == 1)
          manager->box->Event(kAuEventEffectSlot56, i32(DsParadeUiId), 0, 1, 0);
      } else if (--manager->visible_count == 0) {
        manager->box->Event(kAuEventEffectSlot56, i32(DsParadeUiId), 0, 0, 0);
      }
      in_camera = visible;
    }
  }
}

i8 DsParadeInstance::PanicEffect(u32) {
  return 0;
}

u32 DsParadeInstance::PanicRadius() {
  return 0;
}

bool DsParadeInstance::IsActive() {
  return effect != nullptr;
}

// Publish the current tile and retained parade block before binding its actual private movement callback.
// The captured FF direction selects default placement; all other raw directions use the explicit overload.
u8 DsParadeInstance::AttachToOccupant(ScOccupant *occupant) {
  occupant->GetLocationInCellCoords(&x, &z);
  effect = manager->simulation->CreateParadeBlock(sprite);
  if (!effect)
    return 0;
  effect->SetAssociatedParadeInstance(static_cast<DsParadeMotion *>(this));
  u8 path_direction = direction;
  NwParadeBlock *block = effect;
  if (path_direction == 0xff)
    return block->PlaceOnTransitTile(occupant);
  return block->PlaceOnTransitTile(occupant, path_direction);
}

// Clear readiness before camera, effect and instance retirement, preserving the original signed phase gate.
// Disburse the retained live block and release its reread field before clearing the visited-cell records.
u8 DsParadeInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    if (in_camera && --manager->visible_count == 0)
      manager->box->Event(kAuEventEffectSlot56, i32(DsParadeUiId), 0, 0, 0);
    if (phase >= 1)
      manager->EffectsStopped();
    manager->InstanceStopped();
    ClearEffects();
    if (effect) {
      effect->Disburse();
      effect->Release();
      effect = nullptr;
    }
    cells.Clear();
    initialized = false;
  }
  return 1;
}

// Wrap the unsigned clock, finish at the duration limit, start one delayed follower, then poll the current block.
void DsParadeInstance::AdvanceTime(u32 delta) {
  elapsed += delta;
  u32 time = elapsed / DsParadeTickScale;
  if (time >= DsParadeMaximumDuration) {
    Finish();
    return;
  }
  if (remaining && time >= delay) {
    manager->StartNext(start_x, start_z, direction, remaining - 1);
    remaining = 0;
  }
  if (effect && effect->IsBored())
    Finish();
}

i32 DsRiotMaximumInstances = 1, DsRiotMinimumInterval = 0, DsRiotMinimumDuration = 0;
u32 DsRiotMaximumInterval = 0;
f32 DsRiotIntervalScale = 0.35f;
u32 DsRiotSearchRadius = 40;
i8 DsRiotMaximumAura = 60;
// Original39A18/39A1C are signed interval operands of the actual crowd fireball configuration.
i32 DsRiotMinimumFireballTime = kDsRiotDefaultMinimumFireballTime,
    DsRiotMaximumFireballTime = kDsRiotDefaultMaximumFireballTime;

// Establish the four actual identities and inactive state, preserving constructor-unwritten service and counter fields.
// Seed the original generator before the timing defaults and empty pending-record storage.
DsRiotManager::DsRiotManager()
    : initialized(false), layer(nullptr), aura(nullptr), world(nullptr), police(nullptr), city(nullptr),
      occupants(nullptr), random(kScRandomSeedFromClock), active_count(0), box(nullptr), fire(nullptr), elapsed(0),
      interval(10) {
}

DsRiotManager::~DsRiotManager() {
  DsRiotManager::Shutdown();
}

// Publish the common or riot-private view before retaining through the actual shared agent count.
// Delegate all other identities, including Unknown, to the agent's original output-clearing query.
bool DsRiotManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsRiotControl, DsRiotControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsRiotManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsRiotManager::Release() {
  return DsAgent::Release();
}

bool DsRiotManager::StaticShutdown() {
  return true;
}

// Remove the load-completion subscription before releasing retained services in their original order.
// Each pointer is cleared before Release, and readiness stays true until the final city release returns.
bool DsRiotManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&fire);
    ScRelease(&layer);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&simulation);
    ScRelease(&aura);
    ScRelease(&police);
    ScRelease(&world);
    ScRelease(&city);
    initialized = false;
  }
  return true;
}

// Replace pending records and read the signed count followed by five original scalar fields per record.
// Append only complete records, preserving partial-stream failure without inspecting an unwritten count.
bool DsRiotManager::Init(CtCity *owner, RsDatabaseSegment *segment) {
  pending.size = 0;
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsRiotRecordGroup, 0});
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  i32 count;
  u8 result = stream->ReadI32(&count);
  for (i32 index = 0; result && index < count; ++index) {
    DsRiotRecord record;
    result = stream->ReadU32(&record.x);
    result = result && stream->ReadU32(&record.z) && stream->ReadI32(&record.phase) && stream->ReadU32(&record.cost) &&
        stream->ReadU32(&record.destroyed);
    if (result)
      *pending.Append() = record;
  }
  return result;
}

// Replay the captured pending range only for load completion, then clear the current record range after callbacks.
bool DsRiotManager::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityViewReady)
    return false;
  DsRiotRecord *records = pending.data;
  u32 count = pending.size;
  for (u32 index = 0; index < count; ++index)
    Restore(records + index);
  pending.size = 0;
  return true;
}

// An already initialized receiver only shuts down and returns; the original else branch deliberately skips rebinding.
bool DsRiotManager::Init(DsLayer *retained_layer, CtCity *owner) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(owner);
    columns = city->CellCountX();
    rows = city->CellCountZ();
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    world = ScAddRefNotNull(city->WorldLayer());
    police = ScAddRefNotNull(city->PoliceLayer());
    CtLayer *specific = city->GetSpecificLayer(kCtAuraLayerType);
    if (specific)
      specific->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    layer = ScAddRefNotNull(retained_layer);
    active_count = 0;
    effect_count = 0;
    camera_count = 0;
    initialized = true;
    fire = ScAddRefNotNull(static_cast<DsFireManager *>(layer->GetDisasterManager(kDsFireTypeId)));
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  }
  return true;
}

// Scale the signed wrapped remaining capacity by the mayor rating, then apply the original lower and upper clamps.
// Read the global limit after the rating callback and preserve signed FIMUL semantics for the capacity difference.
i32 DsRiotManager::GetStartCount() {
  i8 rating = aura->GetMayorRating();
  i32 maximum = DsRiotMaximumInstances, available = i32(u32(maximum) - active_count);
  i32 count = i32(i64((1.0 - f64(i32(rating) + 128) * 0.00390625) * f64(available)));
  if (count <= 0)
    count = 1;
  if (count > maximum)
    count = maximum;
  return count;
}

void DsRiotManager::InstanceStarted() {
  ++active_count;
}

i32 DsRiotAcceptsOccupant(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad;
}

// Spiral outward within current city bounds until a retained surface occupant has the actual riot geometry flag.
// Release rejected occupants before continuing and preserve the caller's genuine output destination throughout.
u8 DsRiotManager::FindOccupant(ScOccupant **output, i32 origin_x, i32 origin_z) {
  DsTileScan scan;
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(city->CellCountX() - 1);
  scan.bottom = i32(city->CellCountZ() - 1);
  scan.Reset(origin_x, origin_z);
  i32 cell_x, cell_z;
  while (scan.Next(&cell_x, &cell_z)) {
    if (occupants->GetOccupantAtCell(u32(cell_x), u32(cell_z), output)) {
      if (DsRiotAcceptsOccupant(*output))
        return 1;
      (*output)->Release();
    }
  }
  return 0;
}

// Draw two centered coordinates after querying current city dimensions, then use the retained spiral scan.
// This is the installed centered generator rather than a uniform cell selection.
u8 DsRiotManager::FindRandomOccupant(ScOccupant **output) {
  u32 width = city->CellCountX(), height = city->CellCountZ();
  i32 origin_x = random.Centered(0, i32(width - 1)), origin_z = random.Centered(0, i32(height - 1));
  return FindOccupant(output, origin_x, origin_z);
}

// Restore one actual instance with the saved location/accounting and the original phase<2 initialization flag.
// Keep the acquired constructor reference on success and release it only when initialization refuses.
u8 DsRiotManager::Restore(const DsRiotRecord *record) {
  auto *instance = new DsRiotInstance;
  instance->AddRef();
  if (instance->Init(this, record->x, record->z, record->cost, record->destroyed, u8(record->phase < 2)))
    return 1;
  instance->Release();
  return 0;
}

// Construct a retained riot, obtain the occupant's live origin and initialize it with zero accounting.
// Execute the additional virtual tick only after successful initialization, then return the held primary object.
DsRiotInstance *DsRiotManager::StartOccupant(ScOccupant *occupant, u8 automatic_start) {
  auto *instance = new DsRiotInstance;
  instance->AddRef();
  u32 cell_x, cell_z;
  occupant->GetLocationInCellCoords(&cell_x, &cell_z);
  if (!instance->Init(this, cell_x, cell_z, 0, 0, automatic_start)) {
    instance->Release();
    return nullptr;
  }
  instance->Tick(DsDayTick);
  return instance;
}

// Find a retained eligible occupant near the supplied origin, start its riot and release the lookup reference.
// Return the actual private movement identity without adding another instance reference.
DsRiotState *DsRiotManager::Start(u32 cell_x, u32 cell_z, u8 automatic_start) {
  ScOccupant *occupant;
  if (!FindOccupant(&occupant, i32(cell_x), i32(cell_z)))
    return nullptr;
  DsRiotInstance *instance = StartOccupant(occupant, automatic_start);
  occupant->Release();
  return instance;
}

// Attempt the requested number of riots under the original signed limit, releasing each selected occupant immediately.
// Announce success only after the final requested start succeeds; selection failure leaves prior starts alive.
bool DsRiotManager::AttemptDisasterStart() {
  if (i32(active_count) < DsRiotMaximumInstances) {
    i32 count = GetStartCount();
    if (count > 0) {
      for (i32 index = 0; index < count; ++index) {
        ScOccupant *occupant;
        if (!FindRandomOccupant(&occupant))
          return false;
        DsRiotInstance *instance = StartOccupant(occupant, 0);
        occupant->Release();
        if (index == count - 1) {
          if (!instance)
            return false;
          box->Event(kAuEventStartDisaster, kAuEffectDisasterRiot, 0, 0, 0);
          return true;
        }
      }
    }
  }
  return false;
}

// Count matching listed riots, reset the same iterator and write location, signed phase, cost and destruction total.
// Advance before testing a prior write refusal and delete through the live controller before archive cleanup.
bool DsRiotManager::Save(CtCity *owner, RsDatabaseSegment *segment) {
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsRiotRecordGroup, 0}, 1, 0);
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  DsListIterator *iterator = layer->CreateDisasterListIterator();
  if (!iterator)
    return false;
  u32 count = 0;
  for (DsInstance *instance = iterator->Next(); instance; instance = iterator->Next())
    if (instance->TypeId() == kDsRiotTypeId)
      ++count;
  u8 result = stream->WriteU32(count);
  iterator->Rewind();
  for (DsInstance *instance = iterator->Next(); instance && result; instance = iterator->Next()) {
    if (instance->TypeId() == kDsRiotTypeId) {
      u32 cell_x, cell_z;
      instance->GetLocation(&cell_x, &cell_z);
      result = stream->WriteU32(cell_x);
      result = result && stream->WriteU32(cell_z) && stream->WriteI32(static_cast<DsRiotInstance *>(instance)->phase) &&
          stream->WriteU32(instance->GetCost()) && stream->WriteU32(instance->GetDestroyedCount());
    }
  }
  layer->DeleteDisasterListIterator(iterator);
  return result;
}

void DsRiotManager::InstanceEnded() {
  if (i32(active_count) > 0)
    --active_count;
}

void DsRiotManager::EffectsStarted() {
  if (++effect_count == 1)
    box->Event(kAuEventPlayEffect, DsRiotUiId, i32(reinterpret_cast<uptr>(this)), 0, 0);
}

// Drop one positive signed effect count and close the notice only after storing the final zero.
void DsRiotManager::EffectsEnded() {
  if (i32(effect_count) > 0) {
    if (--effect_count == 0)
      box->Event(kAuEventEffectSlot52, DsRiotUiId, 0, 0, 0);
  }
}

void DsRiotManager::EnterCamera() {
  if (++camera_count == 1 && i32(effect_count) > 0)
    box->Event(kAuEventEffectSlot56, DsRiotUiId, 0, 1, 0);
}

// Drop any nonzero camera count and clear the notice mode on final departure while a positive effect count remains.
void DsRiotManager::LeaveCamera() {
  if (camera_count) {
    if (--camera_count == 0 && i32(effect_count) > 0)
      box->Event(kAuEventEffectSlot56, DsRiotUiId, 0, 0, 0);
  }
}

// Publish controller removal before the instance's real teardown, then release its held reference.
u32 DsRiotManager::FinishInstance(DsRiotInstance *instance) {
  layer->DisasterInstanceChange(instance, 4);
  instance->Shutdown();
  return instance->Release();
}

// Choose a uniform origin and scan its bounded neighborhood for low-aura eligible occupants under the signed limit.
// Preserve unsigned lower-edge tests, signed upper-edge clipping and success even when the attempted start refuses.
u8 DsRiotManager::StartRandom() {
  DsTileScan scan;
  if (i32(active_count) < DsRiotMaximumInstances) {
    u32 origin_x = random.Next(columns), origin_z = random.Next(rows);
    scan.left = origin_x > DsRiotSearchRadius ? i32(origin_x - DsRiotSearchRadius) : 0;
    scan.right = i32(DsRiotSearchRadius + origin_x);
    if (scan.right >= i32(columns))
      scan.right = i32(columns - 1);
    scan.top = origin_z > DsRiotSearchRadius ? i32(origin_z - DsRiotSearchRadius) : 0;
    scan.bottom = i32(DsRiotSearchRadius + origin_z);
    if (scan.bottom >= i32(rows))
      scan.bottom = i32(rows - 1);
    scan.Reset(i32(origin_x), i32(origin_z));
    i32 cell_x, cell_z;
    while (scan.Next(&cell_x, &cell_z)) {
      i8 value;
      aura->GetValue(u32(cell_x), u32(cell_z), &value);
      ScOccupant *occupant;
      if (value < DsRiotMaximumAura && occupants->GetOccupantAtCell(u32(cell_x), u32(cell_z), &occupant)) {
        if (DsRiotAcceptsOccupant(occupant)) {
          StartOccupant(occupant, 1);
          occupant->Release();
          return 1;
        }
        occupant->Release();
      }
    }
  }
  return 0;
}

// Advance cadence even when capacity or unemployment blocks a start, redrawing the interval on each eligible update.
// Preserve unsigned unemployment subtraction, signed clamping/jitter bounds and the captured number of start attempts.
bool DsRiotManager::Tick() {
  elapsed = i32(u32(elapsed) + 1);
  if (i32(active_count) < DsRiotMaximumInstances) {
    u32 unemployment = GetUnemployment();
    if (unemployment >= DsRiotMinimumUnemployment) {
      i32 candidate = i32(u32(i64(f64(100u - unemployment) * f64(DsRiotIntervalScale))) + u32(DsRiotMinimumInterval));
      if (candidate < DsRiotMinimumInterval)
        candidate = DsRiotMinimumInterval;
      i32 minimum = i32(i64(f64(candidate) * 0.8)), maximum = i32(i64(f64(candidate) * 1.2));
      if (minimum >= maximum)
        interval = candidate;
      else
        interval = random.Range(minimum, maximum);
      if (elapsed >= interval) {
        elapsed = 0;
        for (i32 remaining = GetStartCount(); remaining > 0; --remaining)
          StartRandom();
      }
    }
  }
  return true;
}

// Derive the interval slope from unsigned wrapped differences, clearing it only for the original zero denominator.
// Preserve the duration calculation's unsigned maximum, signed minimum and stored single-precision reciprocal of 255.
void DsUpdateRiotScales() {
  f64 denominator = f64(100u - DsRiotMinimumUnemployment);
  if (denominator <= 0.0)
    DsRiotIntervalScale = 0.0f;
  else
    DsRiotIntervalScale = f32(f64(DsRiotMaximumInterval - u32(DsRiotMinimumInterval)) / denominator);
  DsRiotDurationScale = f32((f64(DsRiotMaximumDuration) - f64(DsRiotMinimumDuration)) * f64(0.0039215689f));
}

// Shut down the supplied riot before releasing its instance reference. This path does not synthesize a Boolean result.
void DsRiotManager::StopInstance(DsInstance *instance) {
  auto *riot = static_cast<DsRiotInstance *>(instance);
  riot->Shutdown();
  riot->Release();
}

// Read the newest unemployment sample through the world history view, returning zero when that history is absent.
// The graph writes the original unsigned output; its acceptance byte is deliberately ignored.
u32 DsRiotManager::GetUnemployment() {
  ScGraphInfoView *history = world->GetUnemploymentHistory();
  if (!history)
    return 0;
  u32 value;
  history->GetMonthData(&value);
  return value;
}

// Consume each riot description value as comma-separated instance limit and UI ID, without inspecting the key.
// Destroy the prefix before parsing the remainder; a missing comma keeps that remainder intact and the prefix empty.
void DsRiotReadEntry(const ScString &, const ScString &value, void *) {
  ScString remaining(value);
  {
    ScString first;
    u32 length = remaining.Strlen(), split = 0;
    while (split < length && remaining.begin[split] != ',')
      ++split;
    if (split < length) {
      first.FromChar(remaining.begin, split);
      remaining.Erase(0, split + 1);
    }
    DsRiotMaximumInstances = i32(DsParseNumber(first.ToChar()));
  }
  DsRiotUiId = i32(DsParseNumber(remaining.ToChar()));
}

// Read riot descriptions before loading the original ordered unemployment, timing, aura and swarm properties.
// Missing keys preserve existing values; recompute interval and duration scales only with an available service.
bool DsRiotManager::StaticInit() {
  DsIni reader;
  ScString path, unused;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  {
    ScString section("RiotDisaster");
    reader.EnumEntries(section, DsRiotReadEntry, nullptr);
  }
  ScVariableManager *provider = DsGetPropertyProvider();
  if (!provider)
    return false;
  ScValue value;
  if (provider->GetVariable(kDsRiotMinimumUnemploymentPropertyId, &value))
    DsRiotMinimumUnemployment = value.value.word;
  if (provider->GetVariable(kDsRiotMinimumIntervalPropertyId, &value))
    DsRiotMinimumInterval = i32(value.value.word);
  if (provider->GetVariable(kDsRiotMaximumIntervalPropertyId, &value))
    DsRiotMaximumInterval = value.value.word;
  if (provider->GetVariable(kDsRiotMaximumAuraPropertyId, &value))
    DsRiotMaximumAura = i8(value.value.word);
  if (provider->GetVariable(kDsRiotSearchRadiusPropertyId, &value))
    DsRiotSearchRadius = value.value.word;
  if (provider->GetVariable(kDsRiotMaximumDurationPropertyId, &value))
    DsRiotMaximumDuration = value.value.word;
  if (provider->GetVariable(kDsRiotMinimumDurationPropertyId, &value))
    DsRiotMinimumDuration = i32(value.value.word);
  if (provider->GetVariable(kDsRiotMinimumFireballTimePropertyId, &value))
    DsRiotMinimumFireballTime = i32(value.value.word);
  if (provider->GetVariable(kDsRiotMaximumFireballTimePropertyId, &value))
    DsRiotMaximumFireballTime = i32(value.value.word);
  if (provider->GetVariable(kDsRiotPremonitionTicksPropertyId, &value))
    DsRiotPremonitionTicks = i32(value.value.word);
  if (provider->GetVariable(kDsRiotInitialDelayPropertyId, &value))
    DsRiotInitialDelay = i32(value.value.word);
  if (provider->GetVariable(kDsRiotCostPerTickPropertyId, &value))
    DsRiotCostPerTick = value.value.word;
  DsUpdateRiotScales();
  return true;
}

// Construct the actual riot manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateClassE2FE8D38() {
  return static_cast<DsManager *>(new DsRiotManager);
}

i32 DsRiotManager::GetRecords(DsRiotHistory *, u32 *) const {
  return 0;
}

i32 DsRiotInitialDelay = 80, DsRiotPremonitionTicks = 0, DsRiotUiId = 0;
u32 DsRiotMinimumUnemployment = 10, DsRiotMaximumDuration = 180, DsRiotCostPerTick = 5;
f32 DsRiotDurationScale = 0.63f;
