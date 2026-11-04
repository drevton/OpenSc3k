#include "misc/aura.h"

#include <string.h>

#include "application/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sc_graph_info.h"
#include "misc/sp_map_sprite.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"

// The aura history graph uses the original pink series color.
enum CtAuraGraphColor : u32 {
  kCtAuraGraphColor = 0xdf60a0,
};

// Serialized keys retain their record-format role independently of live class and interface identities.
enum CtAuraArchiveGroup : u32 {
  kCtAuraRecordGroup = 0xc259c02d,
};

// Construct the shifted maps and lock before clearing the original service, rating and history fields.
// Explicit zeroes for otherwise unwritten subscription/scan state exclude indeterminate native reads.
CtAura::CtAura()
    : CtSignedMap(2), layer_initialized(false), new_city(false), city(nullptr), agent_tree(nullptr), modifiers_map(2) {
  rating_history = nullptr;
  rating_sum = 0;
  rating_count = 0;
  mayor_rating = 0;
  occupants = nullptr;
  crime = nullptr;
  police = nullptr;
  zone = nullptr;
  ordinances = nullptr;
  fire = nullptr;
  pollution = nullptr;
  residential = nullptr;
  cells_per_day = 0;
  scan_step = 0;
  scan_x = 0;
  scan_z = 0;
}

CtAura::~CtAura() {
  CtAura::Shutdown();
}

// Publish the original primary or secondary identity before retaining through the complete object. Delegate unknown map
// identities to the inherited map query and clear the output.
bool CtAura::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  HANDLE_QUERY_INTERFACE(IID_CtAuraLayer, CtAuraLayer);
  return CtSignedMap::QueryInterface(iid, out);
}

u32 CtAura::AddRef() {
  return CtSignedMap::AddRef();
}

u32 CtAura::Release() {
  return CtSignedMap::Release();
}

u32 CtAura::CellCountX() const {
  return CtSignedMap::CellCountX();
}

u32 CtAura::CellCountZ() const {
  return CtSignedMap::CellCountZ();
}

// Read the signed coarse aura byte and expand it by the original 8421504 multiplier into the genuine DWORD output.
// Preserve the low32-bit product without introducing signed-overflow behavior.
void CtAura::GetValueSint32(u32 x, u32 z, i32 *out) const {
  i8 value;
  CtSignedMap::GetValue(x, z, &value);
  *out = i32(u32(i32(value)) * kCtByteMapSint32Scale);
}

bool CtAura::InBounds(u32 x, u32 z) const {
  return CtSignedMap::InBounds(x, z);
}

bool CtAura::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSignedMap::InBounds(x0, z0, x1, z1);
}

bool CtAura::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSignedMap::ValidRect(x0, z0, x1, z1);
}

bool CtAura::SerialRead(RsStream *record) {
  return CtSignedMap::SerialRead(record);
}

bool CtAura::SerialWrite(RsStream *record) const {
  return CtSignedMap::SerialWrite(record);
}

bool CtAura::Init(CtCity *owner, const i8 *fill) {
  return CtSignedMap::Init(owner, fill);
}

void CtAura::GetValue(u32 x, u32 z, i8 *out) const {
  CtSignedMap::GetValue(x, z, out);
}

void CtAura::SetValue(u32 x, u32 z, const i8 *value) {
  CtSignedMap::SetValue(x, z, value);
}

void CtAura::SetAllCells(const i8 *value) {
  CtSignedMap::SetAllCells(value);
}

void CtAura::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i8 *value) {
  CtSignedMap::SetValue(x0, z0, x1, z1, value);
}

i8 CtAura::GetMayorRating() {
  return mayor_rating;
}

bool CtAura::DoMessage(ScMessage *) {
  return false;
}

bool CtAura::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

u32 CtAura::GetLayerType() {
  return kCtAuraLayerType;
}

i32 CtAura::MinimumExecutionInterval() {
  return 2;
}

bool CtAura::IsAgentLocking() {
  return true;
}

u32 CtAura::TypeId() const {
  return 0;
}

// Return the original absent agent-type pointer without acquiring a tree node.
CtAgentType *CtAura::AgentType() {
  return nullptr;
}

bool CtAura::SetAgentType(CtAgentType *) {
  return false;
}

bool CtAura::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtAura::DebugClassTag(ScStringView *) {
  return false;
}

bool CtAura::DebugTypeTag(ScStringView *) {
  return false;
}

bool CtAura::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool CtAura::CellChangedAll(u32) {
  return true;
}

bool CtAura::CellChanged(u32, u32, u32) {
  return true;
}

bool CtAura::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

bool CtAura::OccupantInserted(u32, ScOccupant *occupant) {
  return OccupantChanged(0, occupant) != 0;
}

bool CtAura::OccupantRemoved(u32, ScOccupant *occupant) {
  return OccupantChanged(1, occupant) != 0;
}

bool CtAura::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

// Resolve an exact or first ordered ancestor modifier, caching misses, then apply it to every expanded footprint cell.
u8 CtAura::OccupantChanged(u32 change, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  if (change > 1)
    return 1;
  BdBuilding *building;
  if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    bool eligible = building->GetOccupancyState() == 1;
    building->Release();
    if (!eligible)
      return 1;
  }
  u32 id = occupant->TypeId();
  i16 effect = 0;
  u8 radius = 0;
  lock.Lock();
  auto found = modifiers.find(id);
  if (found == modifiers.end()) {
    for (auto entry = modifiers.begin(); entry != modifiers.end(); ++entry)
      if (agent_tree->IsAncestor(entry->first, id)) {
        effect = entry->second.effect;
        radius = entry->second.radius;
        break;
      }
    modifiers[id].radius = radius;
    modifiers[id].effect = effect;
  } else {
    radius = found->second.radius;
    effect = found->second.effect;
  }
  lock.Unlock();
  i32 adjustment = change == 1 ? -i32(effect) : i32(effect);
  if (effect) {
    GeCellRect bounds;
    occupant->GetBoundingRectInCellCoords(&bounds);
    bounds.left = bounds.left >= radius ? bounds.left - radius : 0;
    bounds.top = bounds.top >= radius ? bounds.top - radius : 0;
    u32 right = bounds.right + radius;
    if (right < CellCountX())
      bounds.right = right;
    else
      bounds.right = CellCountX() - 1;
    u32 bottom = bounds.bottom + radius;
    if (bottom < CellCountZ())
      bounds.bottom = bottom;
    else
      bounds.bottom = CellCountZ() - 1;
    lock.Lock();
    for (u32 x = bounds.left; i32(x) <= i32(bounds.right); ++x)
      for (u32 z = bounds.top; i32(z) <= i32(bounds.bottom); ++z) {
        i16 value;
        modifiers_map.GetValue(x, z, &value);
        // Only the low WORD is defined in the original DWORD scratch; retain it without reading unwritten high bytes.
        value = i16(u16(value) + u16(adjustment));
        modifiers_map.SetValue(x, z, &value);
      }
    lock.Unlock();
  }
  return 1;
}

std::map<u32, CtAura::CtModifier> CtAura::modifiers;
u16 CtAura::update_period = 50;
u8 CtAura::optimal_police = 85;
i8 CtAura::optimal_police_effect = 10;
i8 CtAura::oppressive_police_effect = -25;
i8 CtAura::inadequate_police_effect = -35;
i8 CtAura::crime_factor = -4;
i8 CtAura::fire_factor = 10;
i16 CtAura::air_factor = -4;
i16 CtAura::water_factor = -1;
i8 CtAura::garbage_factor = -15;
i8 CtAura::radiation_factor = -60;
i8 CtAura::high_eq_factor = 10;
i8 CtAura::low_eq_factor = -20;
u8 CtAura::high_eq_threshold = 100;
u8 CtAura::low_eq_threshold = 80;
i8 CtAura::high_le_factor = 15;
i8 CtAura::low_le_factor = -30;
u8 CtAura::high_le_threshold = 75;
u8 CtAura::low_le_threshold = 55;
i8 CtAura::parking_effect = -4;
i8 CtAura::alternate_driving_effect = -3;
i8 CtAura::nuclear_free_effect = 6;

static u8 CtReadAuraSetting(EcIni *ini, const char *name, ScString *out) {
  ScString key(name), section("TuningParameters");
  return ini->GetValue(&section, &key, out);
}

// Copy both enumeration inputs, split the radius at the first comma, and sign-extend the effect's low byte.
// Only nonzero effects replace a modifier; write the defined fields without reading the original padding byte.
void CtAura::RegisterAgentModifier(const ScString &key, const ScString &value, void *) {
  ScString identifier(key), remaining(value), token;
  u32 id = ScParseMiscNumber(identifier.Data());
  token = remaining.SplitToken(",");
  if (token.Strlen()) {
    u8 radius = u8(ScParseMiscNumber(token.Data()));
    i8 effect = i8(ScParseMiscNumber(remaining.Data()));
    if (effect) {
      CtModifier *modifier = &modifiers[id];
      modifier->effect = effect;
      modifier->radius = radius;
    }
  }
}

// Clear the modifier roster, select the loose and packaged Aura files, then read tuning values in original order.
// Missing keys preserve previous values; only a truncated zero period or crime factor receives its original repair.
bool CtAura::StaticInit(CtDefinition *) {
  ScString path, value;
  EcIni ini;
  modifiers.clear();
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Aura.INI");
  ini.SelectFile(&path);
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  {
    ScString section("AgentIDModifiers");
    ini.EnumerateSection(&section, CtAura::RegisterAgentModifier, this);
  }
  if (CtReadAuraSetting(&ini, "UpdatePeriod", &value)) {
    update_period = u16(ScParseMiscNumber(value.Data()));
    if (!update_period)
      update_period = 1;
  }
  if (CtReadAuraSetting(&ini, "OptimalPoliceCoverage", &value))
    optimal_police = u8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "OptimalPoliceCoverageEffect", &value))
    optimal_police_effect = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "OppressivePoliceCoverageEffect", &value))
    oppressive_police_effect = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "InadequatePoliceCoverageEffect", &value))
    inadequate_police_effect = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "CrimeFactor", &value)) {
    crime_factor = i8(ScParseMiscNumber(value.Data()));
    if (!crime_factor)
      crime_factor = -1;
  }
  if (CtReadAuraSetting(&ini, "FireCoverageFactor", &value))
    fire_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "AirPollutionFactor", &value))
    air_factor = i16(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "WaterPollutionFactor", &value))
    water_factor = i16(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "StandingGarbageFactor", &value))
    garbage_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "RadiationFactor", &value))
    radiation_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "HighEQFactor", &value))
    high_eq_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "LowEQFactor", &value))
    low_eq_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "HighEQThreshold", &value))
    high_eq_threshold = u8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "LowEQThreshold", &value))
    low_eq_threshold = u8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "HighLEFactor", &value))
    high_le_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "LowLEFactor", &value))
    low_le_factor = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "HighLEThreshold", &value))
    high_le_threshold = u8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "LowLEThreshold", &value))
    low_le_threshold = u8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "ParkingFineOrdEffect", &value))
    parking_effect = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "AltDrivingOrdEffect", &value))
    alternate_driving_effect = i8(ScParseMiscNumber(value.Data()));
  if (CtReadAuraSetting(&ini, "NukeFreeOrdEffect", &value))
    nuclear_free_effect = i8(ScParseMiscNumber(value.Data()));
  return true;
}

bool CtAura::StaticShutdown() {
  modifiers.clear();
  return true;
}

u32 CtAura::GetMapSpriteCount() {
  return 1;
}

// Publish the aura sprite identifier only for index zero, leaving rejected outputs untouched.
bool CtAura::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10012;
  return true;
}

// Allocate the actual aura sprite before obtaining its zone view, then publish through its real interface query.
// The original failed-query raw free omits both releases; detach those pointers before ordinary delete to retain them.
bool CtAura::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10012)
    return false;
  SpAuraMapSprite *source = new SpAuraMapSprite(static_cast<CtAuraLayer *>(this), city->ZoneLayer());
  if (source->QueryInterface(iid, out))
    return true;
  source->aura = nullptr;
  source->zone = nullptr;
  delete source;
  return false;
}

const u32 CtAuraScanOrder[16][2] = {{4, 8}, {0, 12}, {4, 0}, {8, 12}, {12, 8}, {0, 8}, {0, 4}, {12, 4}, {4, 12}, {4, 4},
    {12, 0}, {8, 8}, {8, 4}, {12, 12}, {8, 0}, {0, 0}};
static const u32 CtAuraSelectionKey = kCtAuraLayerType;

// Retain the original city services in acquisition order, then initialize the modifier and signed aura maps.
// Build the real history even after a failed map initialization, preserving its original partial-state behavior.
bool CtAura::Init(CtCity *owner) {
  if (layer_initialized || !owner)
    return false;
  city = ScAddRefNotNull(owner);
  occupants = ScAddRefNotNull(owner->SurfaceOccupantManager());
  crime = ScAddRefNotNull(owner->CrimeLayer());
  police = ScAddRefNotNull(owner->PoliceLayer());
  pollution = ScAddRefNotNull(owner->PollutionLayer());
  fire = ScAddRefNotNull(owner->FireLayer());
  zone = ScAddRefNotNull(owner->ZoneLayer());
  ordinances = ScAddRefNotNull(owner->OrdinanceLayer());
  residential = ScAddRefNotNull(owner->ResidentialLayer());
  agent_tree = ScAddRefNotNull(ScGetMiscAgentTree());
  i16 zero_short = 0;
  modifiers_map.Init(city, &zero_short);
  new_city = true;
  i8 zero_byte = 0;
  layer_initialized = Init(city, &zero_byte);
  if (!layer_initialized)
    city->Release();
  rating_sum = 0;
  rating_count = 0;
  mayor_rating = 0;
  rating_history = ScAddRefNotNull(new ScGraphInfo(127));
  rating_history->SetMaxValue(260);
  for (u32 index = 0; index < 4; ++index) {
    CtMiscTextResource resource(kRsGameApprovalRatingText, kRsGameTextGroup);
    ScString text = resource.GetText(), copy(text), label(copy);
    if (!index)
      rating_history->SetSelectionLabel(label);
    else
      rating_history->SetCurrentValueLabel(ScGraphDataSet(index - 1), label);
  }
  rating_history->SetSelectionLabelID(CtAuraSelectionKey);
  u32 value_key = kCtMayorRatingGraphValueId;
  rating_history->SetValueLabelID(value_key);
  rating_history->SetColor(kCtAuraGraphColor);
  u32 z = CellCountZ(), x = CellCountX();
  cells_per_day = x * z / (16u * update_period);
  scan_step = 0;
  scan_x = CtAuraScanOrder[0][0];
  scan_z = CtAuraScanOrder[0][1];
  return layer_initialized;
}

// Release initialized services in the original order, clearing each live pointer after its release callback.
// Shut down the short map before clearing layer state and retiring the inherited signed map.
bool CtAura::Shutdown() {
  if (!layer_initialized)
    return true;
  ScRelease(&city);
  if (occupants)
    occupants->Release();
  occupants = nullptr;
  if (agent_tree)
    agent_tree->Release();
  agent_tree = nullptr;
  ScRelease(&crime);
  ScRelease(&police);
  ScRelease(&pollution);
  ScRelease(&fire);
  ScRelease(&zone);
  ScRelease(&ordinances);
  ScRelease(&residential);
  if (rating_history)
    rating_history->Release();
  rating_history = nullptr;
  modifiers_map.Shutdown();
  layer_initialized = false;
  return CtSignedMap::Shutdown();
}

// Subscribe to occupant changes before obtaining the current city's simulator and adding this agent.
// Registration refusal stops immediately; the simulator's add reply is deliberately ignored.
bool CtAura::SimulationBegin() {
  u32 subscription;
  if (!occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &subscription, new_city))
    return false;
  ScGetMiscCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  return true;
}

// Capture the current simulator before withdrawing the optional occupant subscription.
// Return the captured simulator's removal result when present, otherwise retain the original successful result.
bool CtAura::SimulationEnd() {
  CtSimulatorInterface *simulator = ScGetMiscCity()->CitySimulator();
  if (occupants)
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  return simulator ? simulator->RemoveAgent(static_cast<CtAgent *>(this)) : true;
}

bool CtAura::Init(CtCity *, ScImporter *) {
  new_city = true;
  return true;
}

// Read the unframed aura record, retaining each partial field update and clamping scan coordinates after failed reads.
// History is still read after payload failure; its reply determines the result, and the opened record always closes.
bool CtAura::Init(CtCity *load_city, RsDatabaseSegment *segment) {
  RsKey key(kRsCityLayerRecordType, kCtAuraRecordGroup, 0);
  RsRecord *record;
  u8 result = 0;
  if (segment->OpenRecord(key, &record)) {
    RsStream *stream;
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      bool valid = SerialRead(stream);
      if (valid)
        valid = stream->ReadU8(&scan_step);
      if (scan_step > 15)
        scan_step = 0;
      if (valid)
        valid = stream->ReadU32(&scan_x);
      u32 limit_x = CellCountX();
      if (scan_x >= limit_x)
        scan_x = 0;
      if (valid)
        valid = stream->ReadU32(&scan_z);
      u32 limit_z = CellCountZ();
      if (scan_z >= limit_z)
        scan_z = 0;
      u32 sum = 0;
      result = 0;
      if (valid)
        valid = stream->ReadU32(&sum);
      rating_sum = i32(sum);
      // The original unsigned transport entry writes the corresponding signed counter's complete DWORD bits.
      if (valid && stream->ReadU32(reinterpret_cast<u32 *>(&rating_count)))
        stream->ReadU8(&result);
      mayor_rating = i8(result);
      new_city = true;
      u8 history_result = rating_history->SerialRead(load_city, stream);
      stream->Release();
      result = history_result;
    }
    segment->CloseRecord(&record);
  }
  return result != 0;
}

// Write the unframed signed map, scan state, rating counters and graph in original short-circuit order.
// Release the acquired stream before closing the record even on failure; the original never aborts this partial write.
bool CtAura::Save(CtCity *save_city, RsDatabaseSegment *segment) {
  RsKey key(kRsCityLayerRecordType, kCtAuraRecordGroup, 0);
  RsRecord *record;
  u8 result = 0;
  if (segment->OpenRecordForWrite(key, &record)) {
    RsStream *stream;
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u8 valid = SerialWrite(stream);
      valid = valid && stream->WriteU8(scan_step) && stream->WriteU32(scan_x) && stream->WriteU32(scan_z) &&
          stream->WriteU32(u32(rating_sum)) && stream->WriteU32(u32(rating_count)) && stream->WriteU8(u8(mayor_rating));
      if (valid)
        valid = rating_history->SerialWrite(save_city, stream);
      stream->Release();
      result = valid;
    }
    segment->CloseRecord(&record);
  }
  return result != 0;
}

// Allocate columns and initialize scalar cells in descending order, retaining the actual pointed fill value.
ScSignedGrid::ScSignedGrid(u32 width, u32 height, const i8 *fill) : width(width), height(height) {
  columns = new i8 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new i8[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Retire every column in descending order before deleting the pointer table, preserving the original null-table guard.
ScSignedGrid::~ScSignedGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

// Fill the existing columns with the input's low byte, using the original height captured before the traversal.
void ScSignedGrid::SetAllCells(const i8 *fill) {
  u32 bytes = 1 * height;
  for (u32 x = 0; x < width; ++x)
    memset(columns[x], *fill, bytes);
}

// Read every descending column even after a failed raw-block operation, retaining aggregate failure.
bool ScSignedGrid::SerialRead(RsStream *record) {
  bool valid = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadBlock(columns[x], 1 * height))
      valid = false;
  return valid;
}

// Write every descending column and reload the live height before each external stream call.
bool ScSignedGrid::SerialWrite(RsStream *record) const {
  bool valid = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteBlock(columns[x], 1 * height))
      valid = false;
  return valid;
}

CtSignedMap::CtSignedMap(i32 shift)
    : references(0), initialized(false), width(0), height(0), coordinate_shift(shift), grid(nullptr) {
}

CtSignedMap::~CtSignedMap() {
}

// Publish the same map for Unknown, the base map IID and this specialization's IID, retaining through virtual AddRef.
// Unsupported queries clear the output.
bool CtSignedMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtSignedMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, CtSignedMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapI8, CtSignedMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining its module, then return the count as observed after the external callback.
u32 CtSignedMap::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

u32 CtSignedMap::Release() {
  if (references) {
    ScGetMiscDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Retire initialized state before reading city width then height and allocating the shifted coarse grid.
// Publish initialized only after the owned allocation has been assigned.
bool CtSignedMap::Init(CtCity *city, const i8 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  grid = new ScSignedGrid(width >> coordinate_shift, height >> coordinate_shift, fill);
  initialized = true;
  return true;
}

// Delete a present grid only when initialized, clear that pointer after destruction, and clear initialized last.
bool CtSignedMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

u32 CtSignedMap::CellCountX() const {
  return width;
}

u32 CtSignedMap::CellCountZ() const {
  return height;
}

void CtSignedMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

bool CtSignedMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtSignedMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtSignedMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void CtSignedMap::GetValue(u32 x, u32 z, i8 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

void CtSignedMap::SetValue(u32 x, u32 z, const i8 *value) {
  grid->columns[x >> coordinate_shift][z >> coordinate_shift] = *value;
}

// Fill the inclusive coarse rectangle with full scalar stores in X-major order.
void CtSignedMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i8 *value) {
  x0 >>= coordinate_shift;
  z0 >>= coordinate_shift;
  x1 >>= coordinate_shift;
  z1 >>= coordinate_shift;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid->columns[x][z] = *value;
}

void CtSignedMap::SetAllCells(const i8 *value) {
  grid->SetAllCells(value);
}

bool CtSignedMap::SerialRead(RsStream *record) {
  return grid->SerialRead(record);
}

bool CtSignedMap::SerialWrite(RsStream *record) const {
  return grid->SerialWrite(record);
}

// Allocate columns and initialize scalar cells in descending order, retaining the actual pointed fill value.
ScShortGrid::ScShortGrid(u32 width, u32 height, const i16 *fill) : width(width), height(height) {
  columns = new i16 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new i16[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Retire every column in descending order before deleting the pointer table, preserving the original null-table guard.
ScShortGrid::~ScShortGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

// Fill the existing columns with the input's low byte, using the original height captured before the traversal.
void ScShortGrid::SetAllCells(const i16 *fill) {
  u32 bytes = 2 * height;
  for (u32 x = 0; x < width; ++x)
    memset(columns[x], *fill, bytes);
}

// Read every descending column even after a failed raw-block operation, retaining aggregate failure.
bool ScShortGrid::SerialRead(RsStream *record) {
  bool valid = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadBlock(columns[x], 2 * height))
      valid = false;
  return valid;
}

// Write every descending column and reload the live height before each external stream call.
bool ScShortGrid::SerialWrite(RsStream *record) const {
  bool valid = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteBlock(columns[x], 2 * height))
      valid = false;
  return valid;
}

CtShortMap::CtShortMap(i32 shift)
    : references(0), initialized(false), width(0), height(0), coordinate_shift(shift), grid(nullptr) {
}

CtShortMap::~CtShortMap() {
}

// Publish the same map for Unknown, the base map IID and this specialization's IID, retaining through virtual AddRef.
// Unsupported queries clear the output.
bool CtShortMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtShortMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, CtShortMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapI16, CtShortMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining its module, then return the count as observed after the external callback.
u32 CtShortMap::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

u32 CtShortMap::Release() {
  if (references) {
    ScGetMiscDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Retire initialized state before reading city width then height and allocating the shifted coarse grid.
// Publish initialized only after the owned allocation has been assigned.
bool CtShortMap::Init(CtCity *city, const i16 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  grid = new ScShortGrid(width >> coordinate_shift, height >> coordinate_shift, fill);
  initialized = true;
  return true;
}

// Delete a present grid only when initialized, clear that pointer after destruction, and clear initialized last.
bool CtShortMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

u32 CtShortMap::CellCountX() const {
  return width;
}

u32 CtShortMap::CellCountZ() const {
  return height;
}

void CtShortMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

bool CtShortMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtShortMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtShortMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void CtShortMap::GetValue(u32 x, u32 z, i16 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

void CtShortMap::SetValue(u32 x, u32 z, const i16 *value) {
  grid->columns[x >> coordinate_shift][z >> coordinate_shift] = *value;
}

// Fill the inclusive coarse rectangle with full scalar stores in X-major order.
void CtShortMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value) {
  x0 >>= coordinate_shift;
  z0 >>= coordinate_shift;
  x1 >>= coordinate_shift;
  z1 >>= coordinate_shift;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid->columns[x][z] = *value;
}

void CtShortMap::SetAllCells(const i16 *value) {
  grid->SetAllCells(value);
}

bool CtShortMap::SerialRead(RsStream *record) {
  return grid->SerialRead(record);
}

bool CtShortMap::SerialWrite(RsStream *record) const {
  return grid->SerialWrite(record);
}

// Dispatch original window queries and deliver the current rating history to a compatible model receiver.
// A model notification is acknowledged even when the source cannot receive the model.
bool CtAura::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(rating_history);
      static_cast<ScModelReceiver *>(selected)->Release();
    }
    return true;
  }
  return false;
}

// Deliver the original history/raster query outputs, releasing each acquired UI view after its callbacks.
u8 CtAura::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  u8 result = 1;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (source && source->QueryInterface(IID_UiGraphsViewIdentity, &selected)) {
      static_cast<ScObjectCollection *>(selected)->Add(rating_history);
      static_cast<ScObjectCollection *>(selected)->Release();
    }
    break;
  case kUiQueryTagDialog:
    if (source && source->QueryInterface(IID_UiQueryWindow, &selected)) {
      result = DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
      static_cast<UiQueryWindow *>(selected)->Release();
    }
    break;
  case kUiQueryTagDebug:
    if (source && source->QueryInterface(IID_UiQuerySection, &selected)) {
      result = DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<UiQuery *>(selected)->Release();
    }
    break;
  case kUiQueryTagMaps:
    if (source && source->QueryInterface(IID_UiRasterQuery, &selected)) {
      void *raster = nullptr;
      result = static_cast<SpMapSpriteFactory *>(this)->CreateMapSprite(10012, IID_GxRasterSource, &raster);
      if (result)
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster) {
        static_cast<GxRasterSource *>(raster)->Release();
        raster = nullptr;
      }
      static_cast<UiRasterQuery *>(selected)->Release();
    }
    break;
  case kUiQueryTagStandard:
    if (source && source->QueryInterface(IID_UiQuerySection, &selected)) {
      result = DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<UiQuery *>(selected)->Release();
    }
    break;
  default:
    break;
  }
  return result;
}

u8 CtAura::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 CtAura::DoStandardQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u8 CtAura::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

// Run the original daily phase at2 and the history update at4; other phases are acknowledged without work.
bool CtAura::Simulate(i32 phase) {
  if (phase == 2)
    return DoDailySim() != 0;
  if (phase == 4)
    EndOfMonth();
  return true;
}

// Average the accumulated eligible-cell ratings into the stored mayor byte, then clear both counters.
void CtAura::EndOfCycle() {
  if (rating_count)
    mayor_rating = i8(rating_sum / rating_count);
  else
    mayor_rating = 0;
  rating_sum = 0;
  rating_count = 0;
}

void CtAura::EndOfMonth() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  rating_history->SetMonthData(u32(i32(mayor_rating) + 127));
  if (month == 1) {
    u32 average = rating_history->GetMonthAverage();
    rating_history->SetYearData(average);
    if (!(year % 10)) {
      u32 yearly = rating_history->GetYearAverage();
      rating_history->SetDecadeData(yearly);
    }
  }
}

// Sample each scheduled4x4 block using the original live services, modifier sum and signed tuning arithmetic.
u8 CtAura::DoDailySim() {
  bool parking = ordinances->QueryOrdinanceOn(kCtOrdinance_ParkingFines);
  bool alternate = ordinances->QueryOrdinanceOn(kCtOrdinance_AlternateDayDriving);
  bool nuclear_free = ordinances->QueryOrdinanceOn(kCtOrdinance_NuclearFreeZone);
  f64 oppressive_slope =
      f64(i32(oppressive_police_effect) - i32(optimal_police_effect)) / f64(255 - i32(optimal_police));
  f64 inadequate_slope = f64(i32(optimal_police_effect) - i32(inadequate_police_effect)) / f64(optimal_police);
  if (!cells_per_day)
    return 1;
  u32 sample = 0;
  lock.Lock();
  for (;;) {
    i16 modifier;
    modifiers_map.GetValue(scan_x, scan_z, &modifier);
    i32 total = modifier, count = 0;
    for (u32 index = 0; index < 16; ++index) {
      u32 x = scan_x + (index & 3), z = scan_z + (index >> 2);
      u8 zoning;
      zone->GetValue(x, z, &zoning);
      ScOccupant *occupant = nullptr;
      if (!occupants->GetOccupantAtCell(x, z, &occupant))
        continue;
      if (occupant) {
        u8 major = occupant->MajorType();
        occupant->Release();
        occupant = nullptr;
        if (major != 1)
          continue;
      }
      u8 coverage;
      police->GetValue(x, z, &coverage);
      f64 police_effect;
      if (coverage <= optimal_police)
        police_effect = f64(optimal_police_effect) - f64(i32(optimal_police) - i32(coverage)) * inadequate_slope;
      else
        police_effect = f64(i32(coverage) - i32(optimal_police)) * oppressive_slope + f64(optimal_police_effect);
      volatile f64 addition = police_effect + kScDoubleIntegerBias;
      f64 rounded = addition;
      u64 bits;
      memcpy(&bits, &rounded, sizeof(bits));
      total = i32(u32(total) + u32(bits) + kScDoubleIntegerLowWordBias);
      fire->GetValue(x, z, &coverage);
      total = i32(u32(total) + u32(i32(coverage) / fire_factor));
      crime->GetValue(x, z, &coverage);
      total = i32(u32(total) + u32(i32(coverage) / crime_factor));
      u32 pollution_value;
      pollution->GetAirValue(x, z, &pollution_value);
      total = i32(u32(total) + u32(i32(pollution_value) / air_factor));
      pollution->GetWaterValue(x, z, &pollution_value);
      total = i32(u32(total) + u32(i32(pollution_value) / water_factor));
      pollution->GetGarbageValue(x, z, &pollution_value);
      if (pollution_value)
        total = i32(u32(total) + u32(i32(garbage_factor)));
      if (pollution->IsRadioactive(x, z))
        total = i32(u32(total) + u32(i32(radiation_factor)));
      u8 eq = residential->GetAgeCohortEQ(0, 89);
      if (eq >= high_eq_threshold)
        total = i32(u32(total) +
            u32(i32(high_eq_factor) * (i32(eq) - i32(high_eq_threshold)) / (150 - i32(high_eq_threshold))));
      else if (eq <= low_eq_threshold)
        total = i32(u32(total) + u32(i32(low_eq_factor) * (i32(low_eq_threshold) - i32(eq)) / i32(low_eq_threshold)));
      u8 le = residential->GetAgeCohortLE(0, 89);
      if (le >= high_le_threshold)
        total = i32(
            u32(total) + u32(i32(high_le_factor) * (i32(le) - i32(high_le_threshold)) / (89 - i32(high_le_threshold))));
      else if (le <= low_le_threshold)
        total = i32(u32(total) + u32(i32(low_le_factor) * (i32(low_le_threshold) - i32(le)) / i32(low_le_threshold)));
      if (parking)
        total = i32(u32(total) + u32(i32(parking_effect)));
      if (alternate)
        total = i32(u32(total) + u32(i32(alternate_driving_effect)));
      if (nuclear_free)
        total = i32(u32(total) + u32(i32(nuclear_free_effect)));
      ++count;
    }
    i32 average = count > 0 ? total / count : 0;
    if (average > 127)
      average = 127;
    else if (average < -127)
      average = -127;
    i8 value = i8(average);
    SetValue(scan_x, scan_z, &value);
    rating_sum = i32(u32(rating_sum) + u32(count) * u32(i32(value)));
    rating_count = i32(u32(rating_count) + u32(count));
    scan_x += 16;
    u32 limit_x = CellCountX();
    if (scan_x >= limit_x) {
      scan_z += 16;
      u32 limit_z = CellCountZ();
      if (scan_z >= limit_z) {
        ++scan_step;
        if (scan_step >= 16) {
          scan_step = 0;
          EndOfCycle();
        }
        scan_z = CtAuraScanOrder[scan_step][1];
      }
      scan_x = CtAuraScanOrder[scan_step][0];
    }
    lock.Unlock();
    if (++sample >= cells_per_day)
      return 1;
    lock.Lock();
  }
}
