#define _CRT_SECURE_NO_WARNINGS
#include "network/nw_transit_products.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cassert>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_residential.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/utilities.h"
#include "graphics/image.h"
#include "misc/api.h"
#include "misc/world.h"
#include "network/api.h"
#include "network/nw_manager.h"
#include "network/nw_module.h"
#include "network/nw_records.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/module.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// The transit history uses this original display color for traffic values.
enum NwTrafficHistoryPresentation : u32 {
  kNwTrafficHistoryColor = 0x8f708f,
};

// The upkeep descriptor persists configured and currently funded monthly amounts separately.
enum NwUpkeepPersistenceKey : u32 {
  kNwOptimalMonthlyUpkeepKey = 0x6352cccf,
  kNwMonthlyUpkeepKey = 0x6352ccd0,
};

// Station contributions written by StampNetwork and removed by ClearNetwork from the same availability grid.
enum NwStationAccess : u16 {
  kNwBusStationAccess = 0x11,
  kNwRailStationAccess = 0x14,
  kNwSubwayStationAccess = 0x18,
  kNwSubwayRailStationAccess = 0x3c,
};

static ScLock NwTransitOccupantLock;

static u8 NwRailUpkeepLoaded, NwSubwayRailUpkeepLoaded, NwSubwayUpkeepLoaded, NwBusUpkeepLoaded;
static i32 NwRailUpkeep = 10, NwSubwayRailUpkeep = 10, NwSubwayUpkeep = 10, NwBusUpkeep = 5;

// Read a station type's genuine one-time tuning value through the existing packaged INI implementation.
static void NwLoadStationUpkeep(const char *section, u8 *loaded, i32 *upkeep) {
  if (*loaded)
    return;
  ScString path, value;
  GeIni ini;
  NwGetTransitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Tune.INI", 16);
  ini.SetPath(path);
  NwGetTransitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  bool found;
  {
    ScString key("OptimalMonthlyUpkeep"), name(section);
    found = ini.ReadEntry(name, key, &value);
  }
  if (found)
    *upkeep = atoi(value.ToChar());
  *loaded = 1;
}

OcTransitOccupant::OcTransitOccupant()
    : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

OcTransitBuilding::OcTransitBuilding()
    : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

NwUpkeep::NwUpkeep() : optimalMonthlyUpkeep(0), monthlyUpkeep(0), fundingPercentage(100) {
}

NwStation::NwStation() {
}

// Publish the station primary or upkeep view before using the same qualified building retain as the original.
// Other identifiers keep the real building base's independent interface behavior.
bool NwStation::QueryInterface(u32 iid, void **out) {
  if (iid == IID_NwStation)
    *out = static_cast<BdBuilding *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<NwUpkeep *>(this);
  else
    return OcTransitBuilding::QueryInterface(iid, out);
  OcTransitBuilding::AddRef();
  return true;
}

u32 NwStation::AddRef() {
  return OcTransitBuilding::AddRef();
}

u32 NwStation::Release() {
  return OcTransitBuilding::Release();
}

// Borrow the actual occupant view at primary8 without acquiring another reference.
ScOccupant *NwStation::AsISC3Occupant() {
  return static_cast<OcTransitOccupant *>(this);
}

// Borrow the zero-offset building primary from the station's separate conversion interface at44.
BdBuilding *NwStation::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the actual city-agent view at primary4 without retaining it.
CtAgent *NwStation::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

i32 NwUpkeep::GetOptimalMonthlyUpkeep() {
  return optimalMonthlyUpkeep;
}

// Resolve rail tuning once, then copy the live optimal value into both original cost words.
NwRailStation::NwRailStation() {
  NwLoadStationUpkeep("RailStation", &NwRailUpkeepLoaded, &NwRailUpkeep);
  i32 value = NwRailUpkeep;
  optimalMonthlyUpkeep = value;
  monthlyUpkeep = value;
}

// Clear the separate underground sprite before resolving subway-rail tuning and publishing both costs.
NwSubwayRailStation::NwSubwayRailStation() : subwaySprite(nullptr) {
  NwLoadStationUpkeep("SubwayRailStation", &NwSubwayRailUpkeepLoaded, &NwSubwayRailUpkeep);
  i32 value = NwSubwayRailUpkeep;
  optimalMonthlyUpkeep = value;
  monthlyUpkeep = value;
}

// Clear the separate underground sprite before resolving subway tuning and publishing both costs.
NwSubwayStation::NwSubwayStation() : subwaySprite(nullptr) {
  NwLoadStationUpkeep("SubwayStation", &NwSubwayUpkeepLoaded, &NwSubwayUpkeep);
  i32 value = NwSubwayUpkeep;
  optimalMonthlyUpkeep = value;
  monthlyUpkeep = value;
}

// Resolve bus-stop tuning once and copy the live value into the optimal and funded cost words.
NwBusStop::NwBusStop() {
  NwLoadStationUpkeep("BusStop", &NwBusUpkeepLoaded, &NwBusUpkeep);
  i32 value = NwBusUpkeep;
  optimalMonthlyUpkeep = value;
  monthlyUpkeep = value;
}

// Allocate the real rail-station receiver and return its unretained building primary.
ScUnknown *NwCreateRailStation() {
  return static_cast<BdBuilding *>(new NwRailStation());
}

// Allocate the real subway-rail receiver and return its unretained building primary.
ScUnknown *NwCreateSubwayRailStation() {
  return static_cast<BdBuilding *>(new NwSubwayRailStation());
}

// Allocate the real subway-station receiver and return its unretained building primary.
ScUnknown *NwCreateSubwayStation() {
  return static_cast<BdBuilding *>(new NwSubwayStation());
}

// Allocate the real bus-stop receiver and return its unretained building primary.
ScUnknown *NwCreateBusStop() {
  return static_cast<BdBuilding *>(new NwBusStop());
}

static u8 NwTransitTripThreshold = 25, NwTransitRoadDistanceRes = 5, NwTransitRoadDistanceCom = 4;
static u8 NwTransitRoadDistanceInd = 6, NwTransitRoadDistance200 = 6, NwTransitMassTransitChance = 50;
static f32 NwTransitCoefficient204 = 0.01f, NwTransitCoefficient208 = 0.01f, NwTransitCoefficient212 = 0.001f;

// Preserve the original funding byte before calculating the signed monthly upkeep with its f32 scale and rounding
// trick.
void NwUpkeep::SetFundingPercentage(u8 percentage) {
  fundingPercentage = percentage;
  f64 rounded = f64(percentage) * f64(0.01f) * f64(optimalMonthlyUpkeep) + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  monthlyUpkeep = i32(u32(bits) + kScDoubleIntegerLowWordBias);
}

NwRouteOptions::NwRouteOptions()
    : state(1), mode(1), dirty(1), x0(0), z0(0), x1(0), z1(0), count(0), index(0), position(0), stride(1) {
}

NwUpkeepList::NwUpkeepList() : head(new NwUpkeepLink) {
  head->next = head;
  head->previous = head;
}

NwUpkeepList::~NwUpkeepList() {
  Clear();
  delete head;
}

// Clear only actual list links, leaving payload references to the original layer lifecycle operations.
void NwUpkeepList::Clear() {
  NwUpkeepLink *link = head->next;
  while (link != head) {
    NwUpkeepLink *next = link->next;
    delete link;
    link = next;
  }
  head->next = head;
  head->previous = head;
}

// Construct the lock, routing state and empty upkeep list, retaining the original tuning bytes and scalar defaults.
// Neighbors is intentionally unwritten until Init assigns it; the original constructor does not initialize that field.
NwTransitLayer::NwTransitLayer()
    : lock(new ScLock()), references(0), initialized(0), flag17(0), width(0), height(0), drivingSide(1),
      maxDimension(0), grid92(nullptr), grid96(nullptr), grid100(nullptr), grid104(nullptr), grid108(nullptr),
      grid112(nullptr), grid116(nullptr), grid120(nullptr), grid124(nullptr), grid128(nullptr), grid132(nullptr),
      grid136(nullptr), grid140(nullptr), city(nullptr), surface(nullptr), underground(nullptr), valves(nullptr),
      trafficValve(nullptr), ordinances(nullptr), network(nullptr), history(nullptr), roadTotal(0), roadUsage(0),
      trafficRatio(0.0f), railTotal(0), railUsage(0), tripThresholdPercent(NwTransitTripThreshold),
      roadDistanceRes(NwTransitRoadDistanceRes), roadDistanceCom(NwTransitRoadDistanceCom),
      roadDistanceInd(NwTransitRoadDistanceInd), roadDistance200(NwTransitRoadDistance200),
      massTransitChance(NwTransitMassTransitChance), coefficient204(NwTransitCoefficient204),
      coefficient208(NwTransitCoefficient208), coefficient212(NwTransitCoefficient212), count220(0), count224(0),
      roadFunding(100), railFunding(100), roadBudget(nullptr), railBudget(nullptr), budget(nullptr),
      railMultiplier(1.0f), roadMultiplier(1.0f), value256(0.0f), fareIncome(0.0f), phase(2), value268(0) {
}

NwTransitLayer::~NwTransitLayer() {
  delete lock;
}

// Publish the shared traffic/layer/message/query primary for the five original identifiers, then retain virtually.
bool NwTransitLayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwTrafficLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, NwTrafficLayer);
  HANDLE_QUERY_INTERFACE(IID_NwTrafficLayer, NwTrafficLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, NwTrafficLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, NwTrafficLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the layer count before retaining its actual module director, then return the live count after that callback.
u32 NwTransitLayer::AddRef() {
  ++references;
  NwGetTransitDirector()->AddRef();
  return references;
}

// Drop a nonzero layer hold before the module hold; a live zero count dispatches final virtual destruction.
u32 NwTransitLayer::Release() {
  if (references) {
    --references;
    NwGetTransitDirector()->RemoveRef();
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Dispatch the actual query family and publish the real graph model through the reached history receiver interface.
// History requests are acknowledged even when the source is absent or rejects that interface.
bool NwTransitLayer::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return HandleQueryMessage(message, source) != 0;
  if (message->type == kWnMsg_QueryClosed || message->type == kWnMsg_QueryRefresh)
    return true;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(history);
      static_cast<ScUnknown *>(selected)->Release();
    }
    return true;
  }
  return false;
}

bool NwTransitLayer::SimulationEnd() {
  return true;
}

u32 NwTransitLayer::GetLayerType() {
  return kNwTransitLayerType;
}

bool NwTransitLayer::DebugClassTag(ScStringView *) {
  return false;
}

bool NwTransitLayer::DebugTypeTag(ScStringView *) {
  return false;
}

// Allocate the actual traffic-layer product and return its unretained primary interface to the module director.
ScUnknown *NwCreateTransitLayer() {
  return static_cast<NwTrafficLayer *>(new NwTransitLayer());
}

OcTransitOccupant::~OcTransitOccupant() {
  if (attributes)
    OcTransitOccupant::Shutdown();
}

// Publish the common occupant primary for the four original identifiers before retaining through the live override.
// Unsupported queries clear the output on rejection.
bool OcTransitOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the real module before locking and incrementing the packed sixteen-bit occupant count.
// Preserve the original wrap and return the captured count after unlocking.
u32 OcTransitOccupant::AddRef() {
  NwGetTransitDirector()->AddRef();
  NwTransitOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  NwTransitOccupantLock.Unlock();
  return count;
}

// Release the module while the recursive count lock is held, then update the live packed count.
// Unlock before zero-count virtual shutdown and destruction; nonzero returns reread the live count after unlocking.
u32 OcTransitOccupant::Release() {
  NwTransitOccupantLock.Lock();
  bool last = u16(flags) == 0;
  if (!last) {
    NwGetTransitDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    last = u16(flags) == 0;
  }
  NwTransitOccupantLock.Unlock();
  if (last) {
    Shutdown();
    delete this;
    return 0;
  }
  return u16(flags);
}

// Clear the output before resolving the real city-agent/building identities or forwarding to the occupant base.
// Successful building queries retain through the live most-derived override after publication.
bool OcTransitBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  return OcTransitOccupant::QueryInterface(iid, out);
}

u32 OcTransitBuilding::AddRef() {
  return OcTransitOccupant::AddRef();
}

u32 OcTransitBuilding::Release() {
  return OcTransitOccupant::Release();
}

OcTransitBuilding::~OcTransitBuilding() {
  ScRelease(&agent_type);
}

NwStation::~NwStation() {
}

NwRailStation::~NwRailStation() {
}

NwSubwayRailStation::~NwSubwayRailStation() {
}

NwSubwayStation::~NwSubwayStation() {
}

NwBusStop::~NwBusStop() {
}

// Restore building state first, then read the two actual upkeep keys directly into their live fields.
// Stop on the first failed lookup, preserving any output already published by the descriptor.
bool NwStation::Init(CtCity *city, OcPersistInfo *info) {
  if (!OcTransitBuilding::Init(city, info))
    return false;
  if (!info->GetIntData(kNwOptimalMonthlyUpkeepKey, reinterpret_cast<u32 *>(&optimalMonthlyUpkeep)))
    return false;
  return info->GetIntData(kNwMonthlyUpkeepKey, reinterpret_cast<u32 *>(&monthlyUpkeep)) != 0;
}

// Save the building descriptor before adding the two signed cost words through its genuine DWORD storage interface.
// A refused first cost prevents the second write, retaining the original partial-publication behavior.
bool NwStation::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcTransitBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kNwOptimalMonthlyUpkeepKey, u32(optimalMonthlyUpkeep)))
    return false;
  return info->SetIntData(kNwMonthlyUpkeepKey, u32(monthlyUpkeep)) != 0;
}

bool NwStation::Init(GeAttrib *value) {
  return OcTransitBuilding::Init(value);
}

// Borrow the station's retained agent type without acquiring a second reference.
CtAgentType *NwStation::AgentType() {
  return agent_type;
}

// Bypass the building's city-default cache and borrow the original occupant attributes.
GeAttrib *NwStation::GetBaseAttribData() {
  return OcTransitOccupant::GetAttribData();
}

u32 NwStation::GetGeomFlag() {
  return 0x4104;
}

// Bind the primary station sprite before replacing its separate underground instance.
// Failed base initialization performs the original additional virtual shutdown; failed underground creation is returned.
bool NwSubwayRailStation::Init(GeAttrib *value) {
  if (!NwStation::Init(value)) {
    Shutdown();
    return false;
  }
  if (subwaySprite)
    DestroySprites(&subwaySprite);
  return CreateSprite(4, &subwaySprite);
}

// Bind the primary station sprite before replacing its separate underground instance.
// Preserve the same shared installed body and failure order as the subway-rail variant.
bool NwSubwayStation::Init(GeAttrib *value) {
  if (!NwStation::Init(value)) {
    Shutdown();
    return false;
  }
  if (subwaySprite)
    DestroySprites(&subwaySprite);
  return CreateSprite(4, &subwaySprite);
}

void NwSubwayRailStation::Shutdown() {
  OcTransitBuilding::Shutdown();
  DestroySprites(&subwaySprite);
}

void NwSubwayStation::Shutdown() {
  OcTransitBuilding::Shutdown();
  DestroySprites(&subwaySprite);
}

u32 NwSubwayRailStation::GetGeomFlag() {
  return NwStation::GetGeomFlag() | kOcGeomSubway;
}

u32 NwSubwayStation::GetGeomFlag() {
  return NwStation::GetGeomFlag() | kOcGeomSubway;
}

// Select the surface or underground sprite for the two supported layers.
// Layer4 repeats the original live occupancy query before choosing the underground instance.
SpSprite *NwSubwayRailStation::GetSpriteInst(u32 layer) {
  if (layer == 1)
    return sprite;
  if (layer == 4) {
    if (GetOccupancyState() != 16 && GetOccupancyState() != 8)
      return subwaySprite;
    return sprite;
  }
  return nullptr;
}

// Use the shared installed subway selector, preserving its two live occupancy tests and borrowed results.
SpSprite *NwSubwayStation::GetSpriteInst(u32 layer) {
  if (layer == 1)
    return sprite;
  if (layer == 4) {
    if (GetOccupancyState() != 16 && GetOccupancyState() != 8)
      return subwaySprite;
    return sprite;
  }
  return nullptr;
}

// Bind generic occupant data, then invalidate the building's borrowed caches and acquire its default agent type.
// Failed occupant binding preserves the original second virtual shutdown call.
bool OcTransitBuilding::Init(GeAttrib *value) {
  if (!OcTransitOccupant::Init(value)) {
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

void OcTransitBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  OcTransitOccupant::Shutdown();
}

// Borrow the generic occupant's retained attributes without applying building-default selection.
GeAttrib *OcTransitOccupant::GetAttribData() {
  return attributes;
}

// Replace retained attributes and the primary sprite chain before creating the new default-layer instance.
// Preserve virtual failure cleanup and derive the subtype only when its packed byte is still zero.
bool OcTransitOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  u32 layer = GetDefaultLayerFlag();
  if (!CreateSprite(layer, &sprite)) {
    Shutdown();
    return false;
  }
  if (u8(flags >> kOcSubtypeShift) == 0)
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(attributes->SubTypeInfo()) << kOcSubtypeShift);
  return true;
}

// Release attributes before virtual primary-chain cleanup, then retain only the original permission/count flag bits.
void OcTransitOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Capture each next link before releasing the current sprite, then clear the caller's live chain output.
// Null chains remain untouched until the original nonnull cleanup path is entered.
void OcTransitOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (!current)
    return;
  for (;;) {
    SpSprite *next = current->GetNext();
    current->Release();
    if (!next)
      break;
    current = next;
  }
  *out = nullptr;
}

// Create the requested attribute sprite directly into the real output, then apply rotation and virtual geometry flags.
// Failure preserves the original attribute-key access and any output effects of the definition's failed creation.
bool OcTransitOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (definition && definition->CreateInstance(out)) {
    (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
    (*out)->SetGeomFlag(GetGeomFlag());
    return true;
  }
  attributes->Key();
  return false;
}

// Module-local copy of the original five-word localized-text handle, with its actual retained String and resource key.
class RsTransitText {
public:
  RsTransitText(u32 instance, u32 group);
  virtual ~RsTransitText();
  ScString GetText();

private:
  ScStringView *text;
  RsKey key;
};

RsTransitText::RsTransitText(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  if (!NwGetTransitResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

RsTransitText::~RsTransitText() {
  ScRelease(&text);
}

// Copy the retained String or construct the original empty result when the resource was absent.
ScString RsTransitText::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Copy station query text before retiring its module-local resource hold, matching the original temporary lifetime.
static ScString NwTransitQueryText(u32 instance) {
  RsTransitText resource(instance, kRsCityServiceTextGroup);
  ScString text = resource.GetText(), copy(text);
  return copy;
}

u32 NwStation::QueryValueCount() {
  return 2;
}

// Supply the actual localized funding status or formatted monthly upkeep, including the original warning output.
// Unsupported indices and an absent language utility return without touching the title or value.
u8 NwStation::QueryValue(ScStringView *label, ScStringView *out, u32 index, u8 *warning) {
  ScString title, value, money;
  ScLanguageUtility *language = NwGetTransitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return 0;
  if (warning)
    *warning = 0;
  if (index == 1) {
    title = NwTransitQueryText(kRsCityServiceFundingCostText);
    label->Copy(title);
    ScString format = NwTransitQueryText(kRsCityServiceMonthlyValueFormatText);
    language->MakeMoneyString(i64(monthlyUpkeep), &money);
    value.Sprintf(format.ToChar(), money.ToChar());
    out->Copy(value);
    return 1;
  }
  if (index != 0)
    return 0;
  title = NwTransitQueryText(kRsCityServiceServiceQualityText);
  label->Copy(title);
  NwTrafficLayer *traffic = NwGetTransitCity()->TrafficLayer();
  if ((traffic && traffic->Flag108IsOne()) || fundingPercentage < 40) {
    title = NwTransitQueryText(kRsCityServicePoorText);
    if (warning)
      *warning = 1;
  } else {
    u32 text = fundingPercentage >= 100 ? 386 : fundingPercentage >= 80 ? 387 : 388;
    title = NwTransitQueryText(text);
  }
  out->Copy(title);
  return 1;
}

// Restore generic occupant data before requiring the descriptor occupancy word.
// Optional names and dates preserve their original ignored failure results and year/day output order.
bool OcTransitBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcTransitOccupant::Init(city, info) || !info->HasLayerData1())
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

// Save generic fields before optional instance names and dates, then publish the live occupancy word.
// A name is stored only when the original generic name exists and differs from the instance name.
bool OcTransitBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcTransitOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString original;
      if (attributes && attributes->GetName(&original) && !name.IsEqual(&original))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    EcDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.DayOfYear(), year = date.Year();
      info->SetSimCreateDate(i32(year), i32(day));
    }
  }
  info->SetLayerData1(occupancy_flags);
  return true;
}

u32 OcTransitBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

// Replace the full state word with the original input byte and refresh only when the state changes.
void OcTransitBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancy_flags != state) {
    occupancy_flags = state;
    RefreshSprite();
  }
}

// Add the requested bits, refreshing only for a changed word whose input includes a low-byte bit.
void OcTransitBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Remove the selected bits while preserving the original rule that high-only changes do not refresh sprites.
void OcTransitBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

bool OcTransitBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (occupancy_flags & mask) != 0;
}

// Capture the real graphics critical section while updating a building's visible instance.
class ScTransitGraphicsLock {
public:
  // Lock the captured receiver after publishing it into the scope, preserving exception cleanup ownership.
  explicit ScTransitGraphicsLock(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Unlock the same receiver after sprite callbacks and any exception unwind.
  ~ScTransitGraphicsLock() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Invalidate borrowed attributes before selecting the virtual definition; failed creation keeps its live output
// effects.
void OcTransitBuilding::RefreshSprite() {
  ScTransitGraphicsLock scope(NwGetTransitCity()->GetGraphicsCriticalSection());
  cached_attributes = nullptr;
  SpDefinition *definition = GetSpriteAttrib();
  if (!definition)
    return;
  if (sprite) {
    definition->DestroyInstance(&sprite);
    sprite = nullptr;
  }
  if (definition->CreateInstance(&sprite))
    sprite->SetRotationOffset(u8(position >> kOcPositionRotationShift));
}

bool OcTransitBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 OcTransitBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool OcTransitBuilding::IsAgentLocking() {
  return true;
}

u32 OcTransitBuilding::TypeId() const {
  return OcTransitOccupant::TypeId();
}

u32 OcTransitOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

u32 OcTransitOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

// Borrow the primary sprite without changing chain ownership or reference counts.
SpSprite *OcTransitOccupant::GetSpriteInst() {
  return sprite;
}

// Compare the requested layer against the live default, then use the virtual primary getter on a match.
SpSprite *OcTransitOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

// Publish the installed transit-phase warning before the qualified generic building status/options query.
u8 NwStation::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  NwTrafficLayer *traffic = NwGetTransitCity()->TrafficLayer();
  if (traffic && traffic->Flag108IsOne()) {
    status = NwTransitQueryText(kRsCityServiceWorkersOnStrikeText);
    query->SetStatusText(status);
  }
  return OcTransitBuilding::QueryBasic(message, query);
}

// Request the two actual label/value pairs through the station's virtual formatter, preserving each category and warning.
// A missing query skips rows but still reaches the qualified generic building query phase.
u8 NwStation::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  u8 warning = 0;
  if (query) {
    if (QueryValue(&label, &value, 0, &warning))
      query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, warning);
    if (QueryValue(&label, &value, 1, &warning))
      query->AddText(&label, &value, kUiQueryMonthlyCostCategory, warning);
  }
  return OcTransitBuilding::QueryTile(message, query);
}

u8 NwStation::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcTransitBuilding::QueryDetail(message, query);
}

u32 NwRailStation::GetGeomFlag() {
  return NwStation::GetGeomFlag();
}

u32 NwBusStop::GetGeomFlag() {
  return NwStation::GetGeomFlag();
}

bool OcTransitOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool OcTransitOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 OcTransitOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void OcTransitOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void OcTransitOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

bool OcTransitOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 OcTransitOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 OcTransitOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 OcTransitOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 OcTransitOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void OcTransitOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 OcTransitOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u8 OcTransitOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

u32 OcTransitOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 OcTransitOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

void OcTransitOccupant::AnimStart() {
}

void OcTransitOccupant::AnimStop() {
}

bool OcTransitOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void OcTransitOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool OcTransitOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void OcTransitOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

bool OcTransitOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Select the actual primary sprite-definition or agent-type subsystem through virtual getters.
// Acquire a reference before publishing the selected identity.
bool OcTransitOccupant::GetSubSystemData(u32 id, void **out) {
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

// Borrow the generic attribute agent type without acquiring a reference. Unbound occupants have no type.
CtAgentType *OcTransitOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

u32 OcTransitOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Borrow the default sprite definition from generic attributes without acquiring it.
SpDefinition *OcTransitOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *OcTransitOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

u8 OcTransitOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

void OcTransitOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool OcTransitOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude to micro coordinates. Exclude orientation bits from altitude.
void OcTransitOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void OcTransitOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 OcTransitOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void OcTransitOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u16 OcTransitOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Leave output untouched without attributes. Otherwise capture the minimum before callbacks and compute Y, Z then X
// extents, reloading the packed position after each virtual size getter before publishing maximum coordinates.
void OcTransitOccupant::GetBounds(GeBounds *out) const {
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

// Publish the live cell rectangle only when attributes exist. Size callbacks run X then Z and may change position
// before each corresponding maximum is computed and immediately stored.
void OcTransitOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

u8 OcTransitOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 OcTransitOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 OcTransitOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 OcTransitOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << kCtMicroCoordinateShift;
}

u32 OcTransitOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << kCtMicroCoordinateShift;
}

u32 OcTransitOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << kCtMicroCoordinateShift;
}

// Replace cell coordinates and altitude from micro coordinates in source store order. Preserve orientation.
void OcTransitOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void OcTransitOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void OcTransitOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool OcTransitOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void OcTransitOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

i32 OcTransitOccupant::Tick(u32) {
  return 0;
}

void OcTransitOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void OcTransitOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Replace the packed orientation only when its full input differs.
u8 OcTransitOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  u32 layer = GetDefaultLayerFlag();
  CreateSprite(layer, &sprite);
  return 1;
}

u32 OcTransitOccupant::GetMemUse() const {
  return 20;
}

bool OcTransitOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcTransitOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool OcTransitOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

void OcTransitOccupant::SetAltAnimCells(u16 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude & 0xff00) << kOcAltitudeFromMicroShift);
}

bool OcTransitOccupant::DoMessage(ScMessage *) {
  return false;
}

// Borrow the adjusted occupant interface without acquiring another reference.
ScOccupant *OcTransitBuilding::AsISC3Occupant() {
  return static_cast<OcTransitOccupant *>(this);
}

// Borrow the city-agent subobject without a query or reference acquisition.
CtAgent *OcTransitBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool OcTransitBuilding::Init(RsKey *key) {
  return OcTransitOccupant::Init(key);
}

// Borrow the retained agent type without changing its reference count.
CtAgentType *OcTransitBuilding::AgentType() {
  return agent_type;
}

bool OcTransitBuilding::SetAgentType(CtAgentType *) {
  return false;
}

bool OcTransitBuilding::SetName(const ScStringView &) {
  return false;
}

bool OcTransitBuilding::IsNameChangeable() {
  return false;
}

bool OcTransitBuilding::HasBirthdate() const {
  return false;
}

bool OcTransitBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool OcTransitBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// Query the specialized building attribute view from the virtual generic getter. Preserve its raw power byte
// before releasing the acquired interface; absent attributes or a failed query produce zero.
u8 OcTransitBuilding::PowerRequired() {
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

// Borrow the original generic attribute pointer directly, bypassing the city-specific default-data selection.
GeAttrib *OcTransitBuilding::GetBaseAttribData() {
  return OcTransitOccupant::GetAttribData();
}

bool OcTransitBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return OcTransitOccupant::DoQueryInfo(message, sender);
}

u32 OcTransitBuilding::GetGeomFlag() {
  return 260;
}

// Borrow cached attributes when present. Otherwise select the city's building default for inactive occupancy states,
// falling back to generic attributes on absent defaults and publishing that borrowed result after callbacks.
GeAttrib *OcTransitBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = NwGetTransitCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = OcTransitOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before invoking this building's qualified indexed getter.
SpDefinition *OcTransitBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return OcTransitBuilding::GetSpriteAttrib(layer);
}

// Reuse the borrowed sprite cache for every layer. Active states use the generic indexed definition; other states
// ask the city's building defaults and fall back to the generic default definition when no result is available.
SpDefinition *OcTransitBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = OcTransitOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = NwGetTransitCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = OcTransitOccupant::GetSpriteAttrib();
  return cached_sprite;
}

u32 OcTransitBuilding::GetMemUse() const {
  return 44;
}

bool OcTransitBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcTransitBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool OcTransitBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

// Keep an existing attribute binding unchanged. Otherwise acquire through the application cache, invoke the virtual
// attribute initializer and release its live temporary output; failed cache lookup clears the instance pointer.
bool OcTransitOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!NwGetTransitAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// Write generic occupant properties to the actual descriptor in source order. The original invokes the X-size
// getter twice: first for the second size argument, then for the first; preserve these distinct virtual calls.
bool OcTransitOccupant::Save(CtCity *, OcPersistInfo *info) {
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

// Clear the real query-window output before testing attributes. Resolve the configured class through this module's
// actual COM service, passing the caller's live output directly across that external interface boundary.
bool OcTransitOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  return NwGetTransitDirector()->GZCOM()->GetClassObject(class_id, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

// Route the four query notifications through their actual interfaces and private virtual handlers. Always return
// false after releasing the live QI output, including when a handler successfully populates the query.
bool OcTransitOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  ScMsgCode type = message->type;
  if (type != kWnMsg_QueryOpened && type != kWnMsg_QueryClosed)
    return false;
  u32 kind = u32(message->data1);
  if (kind == kUiQueryTagDialog) {
    UiQueryWindow *query = nullptr;
    if (sender && sender->QueryInterface(IID_UiQueryWindow, reinterpret_cast<void **>(&query))) {
      if (type == kWnMsg_QueryOpened)
        QueryBasic(message, query);
      else
        QueryResponse(message, query);
      query->Release();
    }
  } else if (type == kWnMsg_QueryOpened && (kind == kUiQueryTagDebug || kind == kUiQueryTagStandard)) {
    UiQuery *query = nullptr;
    if (sender && sender->QueryInterface(IID_UiQuerySection, reinterpret_cast<void **>(&query))) {
      if (kind == kUiQueryTagDebug)
        QueryDetail(message, query);
      else
        QueryTile(message, query);
      query->Release();
    }
  }
  return false;
}

u8 OcTransitOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 OcTransitOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 OcTransitOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Preserve low-bit removal replies rather than normalizing arbitrary descriptor bytes to Boolean values.
bool OcTransitOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool valid = true;
  if (!attributes) {
    RsKey key;
    info->GetAttribKey(&key);
    valid = Init(&key);
    u32 type = 0;
    if (valid)
      valid = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (valid)
    valid = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (valid)
    valid = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (valid) {
    u8 simulation = info->GetCanSimRemoveFlag();
    flags = (flags & kOcFlagsWithoutSimRemove) | (u32(simulation & 1) << kOcSimRemoveShift);
    u8 user = info->GetCanUserRemoveFlag();
    flags = (flags & kOcFlagsWithoutUserRemove) | (u32(user & 1) << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return valid;
}

// Publish a nonempty borrowed override architect, ignoring the original name getter's reply.
// Copy its localized label before retiring the resource and emitting the all-category query row.
u8 OcTransitOccupant::QueryTile(ScMessage *, UiQuery *query) {
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
  ScString label;
  {
    RsTransitText resource(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString text = resource.GetText(), copy(text);
    label = copy;
  }
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

// Other consumed native temporaries are initialized solely to exclude undefined original reads after incomplete
// transport.
bool OcTransitOccupant::ReadState(CtCity *, RsStream *record) {
  RsKey key{};
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 unusedType;
  record->ReadU32(&unusedType);
  u8 unused[5];
  for (u32 index = 0; index < 5; ++index)
    record->ReadU8(&unused[index]);
  u8 subtype = 0, marked = 0, memberships = 0, simulation = 0, user = 0;
  record->ReadU8(&subtype);
  SetSubTypeInfo(subtype);
  record->ReadU8(&marked);
  SetMarkBit(marked != 0);
  record->ReadU8(&memberships);
  SetIsInCityFlag(memberships);
  record->ReadU8(&simulation);
  SetSimRemove(simulation != 0);
  record->ReadU8(&user);
  SetUserRemove(user != 0);
  CtCoord coordinate = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  coordinate.x = i32(x << kCtMicroCoordinateShift);
  coordinate.z = i32(z << kCtMicroCoordinateShift);
  coordinate.altitude = i32(altitude << kCtMicroCoordinateShift);
  SetLocation(coordinate);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return true;
}

// Failed resource queries retain their output effects; the successful acquired output is cleared after Release.
bool OcTransitOccupant::WriteState(CtCity *, RsStream *record) {
  GeAttrib *value = OcTransitOccupant::GetAttribData();
  RsResource *resource = nullptr;
  if (!value || !value->QueryInterface(IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  record->WriteU32(*static_cast<const RsResource *>(resource)->Type());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Group());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Instance());
  resource->Release();
  resource = nullptr;
  record->WriteU32(TypeId());
  record->WriteU8(MajorType());
  record->WriteU8(AnimType());
  record->WriteU8(SizeInCellsX());
  record->WriteU8(SizeInCellsZ());
  record->WriteU8(SizeInCellsY());
  record->WriteU8(SubTypeInfo());
  record->WriteU8(GetMarkBit());
  record->WriteU8(u8(GetIsInCityFlag()));
  record->WriteU8(CanSimRemove());
  record->WriteU8(CanUserRemove());
  CtCoord coordinate = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GetLocation(&coordinate);
  record->WriteU32(u32(coordinate.x >> kCtMicroCoordinateShift));
  record->WriteU32(u32(coordinate.z >> kCtMicroCoordinateShift));
  record->WriteU32(u32(coordinate.altitude >> kCtMicroCoordinateShift));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return true;
}

bool OcTransitBuilding::ReadState(CtCity *city, RsStream *record) {
  if (!OcTransitOccupant::ReadState(city, record))
    return false;
  i32 state = i32(u32(uptr(record)));
  bool valid = record->ReadI32(&state);
  if (!state)
    state = 1;
  SetOccupancyStateFlag(u32(state));
  return valid;
}

bool OcTransitBuilding::WriteState(CtCity *city, RsStream *record) {
  if (!OcTransitOccupant::WriteState(city, record))
    return false;
  return record->WriteI32(i32(occupancy_flags));
}

// Publish the first matching occupancy status, then the original option caption, checked bit and borrowed font.
// Resource holds retire before query callbacks; the checked bit is captured before font lookup.
u8 OcTransitBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  u32 instance = 0;
  if (GetOccupancyStateFlag(kGeBuildingCoveredInGarbage))
    instance = 198;
  else if (GetOccupancyStateFlag(kGeBuildingAbandoned))
    instance = 197;
  else if (GetOccupancyStateFlag(kGeBuildingUnderConstruction))
    instance = 196;
  else if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16))
    instance = 199;
  if (instance) {
    status = NwTransitQueryText(instance);
    query->SetStatusText(status);
  }
  ScString caption = NwTransitQueryText(kRsCityServiceMakeHistoricalText);
  WnResources *resources = NwGetTransitWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return OcTransitOccupant::QueryBasic(message, query);
}

// For inactive occupancy flags, format the wrapped signed footprint cost through the real language utility.
// Missing utility stops this phase; missing budget still allows the qualified base architect row.
u8 OcTransitBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *language = NwGetTransitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!language)
      return 0;
    EcBudgetLayer *budget = NwGetTransitCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label = NwTransitQueryText(kRsCityServiceCleanUpCostText),
               formatted = NwTransitQueryText(kRsCityServiceCleanUpCostFormatText), number;
      language->MakeMoneyString(i32(amount), &number);
      ScString format(formatted);
      formatted.Sprintf(format.ToChar(), number.ToChar());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return OcTransitOccupant::QueryTile(message, query);
}

u8 OcTransitBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcTransitOccupant::QueryDetail(message, query);
}

// Apply the checked option through the occupancy mutators before the qualified empty base response.
u8 OcTransitBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return OcTransitOccupant::QueryResponse(message, query);
}

// Allocate the actual x-major byte columns in reverse order and initialize each cell from the live input byte.
NwByteGrid::NwByteGrid(u32 x, u32 z, const u8 *initial) : width(x), height(z), cells(new u8 *[x]) {
  for (u32 column = width; column-- > 0;) {
    cells[column] = new u8[height];
    for (u32 row = height; row-- > 0;)
      cells[column][row] = *initial;
  }
}

// Retire byte columns in the original reverse allocation order before deleting their pointer array.
NwByteGrid::~NwByteGrid() {
  if (cells) {
    for (u32 column = width; column-- > 0;)
      delete[] cells[column];
    delete[] cells;
  }
}

void NwByteGrid::Fill(const u8 *value) {
  u32 length = height;
  for (u32 column = 0; column < width; ++column)
    memset(cells[column], *value, length);
}

// Allocate the network flag grid as real sixteen-bit columns; the installed element allocation is twice the height.
NwWordGrid::NwWordGrid(u32 x, u32 z, const u16 *initial) : width(x), height(z), cells(new u16 *[x]) {
  for (u32 column = width; column-- > 0;) {
    cells[column] = new u16[height];
    for (u32 row = height; row-- > 0;)
      cells[column][row] = *initial;
  }
}

// Delete the actual word columns in reverse order before their pointer array.
NwWordGrid::~NwWordGrid() {
  if (cells) {
    for (u32 column = width; column-- > 0;)
      delete[] cells[column];
    delete[] cells;
  }
}

NwBitGrid::NwBitGrid(u32 x, u32 z, const u8 *initial)
    : width(x), height(z), wordsPerColumn((z >> 5) + ((z & 31) != 0)), words(new u32 *[x]) {
  for (u32 column = 0; column < width; ++column)
    words[column] = new u32[wordsPerColumn];
  Fill(initial);
}

// Retire bitmap columns in their original forward order, then delete the column-pointer array.
NwBitGrid::~NwBitGrid() {
  if (words) {
    for (u32 column = 0; column < width; ++column)
      delete[] words[column];
    delete[] words;
  }
}

void NwBitGrid::Fill(const u8 *value) {
  u32 bits = *value ? kScFilledBitGridWord : 0;
  for (u32 column = 0; column < width; ++column)
    for (u32 word = 0; word < wordsPerColumn; ++word)
      words[column][word] = bits;
}

void NwBitGrid::Set(u32 x, u32 z, const u8 *value) {
  u32 *word = &words[x][z >> 5], bit = 1u << (z & 31);
  if (*value)
    *word |= bit;
  else
    *word &= ~bit;
}

u32 NwTransitLayer::GetMaxDimension() {
  return maxDimension;
}

u8 NwTransitLayer::GetRoadDistance() {
  return NwTransitRoadDistance200;
}

f64 NwTransitLayer::GetTrafficRatio() {
  return trafficRatio;
}

u32 NwTransitLayer::GetRailTotal() {
  return railTotal;
}

u32 NwTransitLayer::GetRailUsage() {
  return railUsage;
}

u32 NwTransitLayer::GetCount220() {
  return count220;
}

u32 NwTransitLayer::GetCount224() {
  return count224;
}

i64 NwTransitLayer::GetFareIncome() {
  return i64(fareIncome);
}

i64 NwTransitLayer::GetValue256() {
  return i64(value256);
}

f64 NwTransitLayer::GetRailMultiplier() {
  return railMultiplier;
}

f64 NwTransitLayer::GetRoadMultiplier() {
  return roadMultiplier;
}

u8 NwTransitLayer::Flag108IsOne() {
  return phase == 1;
}

u8 NwTransitLayer::SetFlag17(u8 value) {
  flag17 = value;
  return value;
}

u8 NwTransitLayer::GetFlag17() {
  return flag17;
}

// Exact zero denotes a blocked trip; an unset first plane produces the distinct byte two.
u8 NwTransitLayer::IsTripAvailable(u32 x, u32 z) {
  u32 bit = 1u << (z & 31), word = z >> 5;
  if (!(grid124->words[x][word] & bit))
    return 2;
  return !(grid128->words[x][word] & bit);
}

// Empty rectangles return zero and byte two remains distinct from a blocked cell.
u8 NwTransitLayer::HasBlockedTrip(u32 x0, u32 z0, u32 x1, u32 z1) {
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      if (IsTripAvailable(x, z) == 0)
        return 1;
  return 0;
}

void NwTransitLayer::SetTripAvailability(u32 x, u32 z, u8 state) {
  u8 first = state < 2, second = state == 0;
  grid124->Set(x, z, &first);
  grid128->Set(x, z, &second);
}

void NwTransitLayer::FillTripAvailability(u32 x0, u32 z0, u32 x1, u32 z1, u8 state) {
  u8 first = state < 2, second = state == 0;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid124->Set(x, z, &first);
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid128->Set(x, z, &second);
}

void NwTransitLayer::GetVolume(u32 x, u32 z, u8 *volume, u8 mask) {
  *volume = 0;
  u16 flags = grid92->cells[x][z];
  if (!(mask & flags))
    return;
  if ((mask & flags & 0x35) != 0)
    *volume = grid96->cells[x][z];
  if ((mask & flags & 2) != 0) {
    u8 value = grid100->cells[x][z];
    if (value > *volume)
      *volume = value;
  }
  if ((mask & flags & 8) != 0) {
    u8 value = grid104->cells[x][z];
    if (value > *volume)
      *volume = value;
  }
}

// The unused residual register value is not asserted as a source return declaration.
void NwTransitLayer::GetModeVolume(u32 x, u32 z, u32 *out, u8 mode) {
  NwByteGrid *grid;
  if (mode == 2)
    grid = grid100;
  else if (mode == 8)
    grid = grid104;
  else if (mode == 21)
    grid = grid96;
  else
    return;
  *out = grid->cells[x][z];
}

void NwTransitLayer::SumVolume(u32 x0, u32 z0, u32 x1, u32 z1, u32 *total, u32 *count, u8 mask) {
  *total = 0;
  *count = 0;
  for (u32 x = x0; x <= x1; ++x) {
    for (u32 z = z0; z <= z1; ++z) {
      u16 flags = grid92->cells[x][z];
      if (!(mask & flags))
        continue;
      u8 value = 0;
      if (mask & flags & 0x35)
        value = grid96->cells[x][z];
      if ((mask & flags & 2) && grid100->cells[x][z] > value)
        value = grid100->cells[x][z];
      if ((mask & flags & 8) && grid104->cells[x][z] > value)
        value = grid104->cells[x][z];
      *total += value;
      ++*count;
    }
  }
}

u32 NwTransitLayer::AverageVolume(u32 x0, u32 z0, u32 x1, u32 z1, u8 *out, u8 mask) {
  u32 total = 0, area = (z1 - z0 + 1) * (x1 - x0 + 1);
  for (u32 x = x0; x <= x1; ++x) {
    for (u32 z = z0; z <= z1; ++z) {
      u16 flags = grid92->cells[x][z];
      if (!(mask & flags))
        continue;
      u8 value = 0;
      if (mask & flags & 0x35)
        value = grid96->cells[x][z];
      if ((mask & flags & 2) && grid100->cells[x][z] > value)
        value = grid100->cells[x][z];
      if ((mask & flags & 8) && grid104->cells[x][z] > *out)
        value = grid104->cells[x][z];
      total += value;
    }
  }
  assert(area != 0);
  u32 average = total / area;
  *out = u8(average);
  return average;
}

void NwTransitLayer::ClearDensity() {
  u8 zero = 0;
  grid96->Fill(&zero);
  grid100->Fill(&zero);
  grid104->Fill(&zero);
  grid108->Fill(&zero);
  grid112->Fill(&zero);
  grid116->Fill(&zero);
  grid120->Fill(&zero);
  grid128->Fill(&zero);
  grid124->Fill(&zero);
}

static u8 NwTransitOrdinanceChance = 50, NwTransitOrdinanceScale = 50, NwTransitOrdinanceDistance = 1;
static f32 NwTransitRoadFundingLimit = 1.2f, NwTransitRoadFundingCurve = 30.0f;
static f32 NwTransitRailFundingLimit = 1.2f, NwTransitRailFundingCurve = 30.0f;
static u8 NwTransitMinimumRailFunding = 100;
static f32 NwTransitDeficitScale = 0.75f;
static NwTransitLayer *NwTransitActiveLayer;

// Road-distance bytes wrap; mass-transit chance saturates before applying the actual rail-funding multiplier.
void NwTransitLayer::ApplyTunables() {
  tripThresholdPercent = NwTransitTripThreshold;
  roadDistanceRes = NwTransitRoadDistanceRes;
  roadDistanceCom = NwTransitRoadDistanceCom;
  roadDistanceInd = NwTransitRoadDistanceInd;
  roadDistance200 = NwTransitRoadDistance200;
  massTransitChance = NwTransitMassTransitChance;
  coefficient212 = NwTransitCoefficient212;
  coefficient204 = NwTransitCoefficient204;
  coefficient208 = NwTransitCoefficient208;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_ShuttleService)) {
    roadDistanceRes += NwTransitOrdinanceDistance;
    roadDistanceCom += NwTransitOrdinanceDistance;
    roadDistanceInd += NwTransitOrdinanceDistance;
    roadDistance200 += NwTransitOrdinanceDistance;
  }
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_SubsidizedMassTransit)) {
    u32 chance = u32(massTransitChance) + NwTransitOrdinanceChance;
    massTransitChance = chance >= 255 ? 255 : u8(chance);
    coefficient212 = f32(f64(NwTransitOrdinanceScale) * f64(coefficient212) * f64(0.01f));
    coefficient204 = f32(f64(NwTransitOrdinanceScale) * f64(coefficient204) * f64(0.01f));
    coefficient208 = f32(f64(NwTransitOrdinanceScale) * f64(coefficient208) * f64(0.01f));
  }
  massTransitChance = u8(i64(f64(massTransitChance) * f64(railMultiplier)));
}

// Publish the road funding byte and its initial scale, then preserve the original nonlinear overfunding curve.
void NwTransitLayer::SetRoadFundingPercentage(u8 value) {
  roadFunding = value;
  f64 scale = f64(value) * f64(0.01f);
  roadMultiplier = f32(scale);
  if (scale > 1.0) {
    f64 limit = NwTransitRoadFundingLimit, difference = 1.0 - limit;
    f64 curve = (f64(1.2f) / difference + f64(NwTransitRoadFundingCurve)) * f64(4.999999f);
    roadMultiplier = f32(limit - 1.0 / ((curve - 1.0 / difference) * scale - curve));
  }
}

// Publish rail funding before locking and forwarding the same byte through every actual upkeep interface.
// Unlock before updating the rail multiplier with the installed overfunding curve.
void NwTransitLayer::SetRailFundingPercentage(u8 value) {
  railFunding = value;
  lock->Lock();
  NwUpkeepLink *head = upkeep.head;
  for (NwUpkeepLink *link = head->next; link != head; link = link->next)
    link->value->SetFundingPercentage(value);
  lock->Unlock();
  f64 scale = f64(value) * f64(0.01f);
  railMultiplier = f32(scale);
  if (scale > 1.0) {
    f64 limit = NwTransitRailFundingLimit, difference = 1.0 - limit;
    f64 curve = (f64(1.2f) / difference + f64(NwTransitRailFundingCurve)) * f64(4.999999f);
    railMultiplier = f32(limit - 1.0 / ((curve - 1.0 / difference) * scale - curve));
  }
}

// Return only the original defined low DWORD funding deficit; the zero path does not fabricate an EDX half.
i32 NwTransitLayer::GetRailFundingDeficit() {
  if (railFunding < NwTransitMinimumRailFunding && i32(count224) > 0)
    return i32(i64(f64(NwTransitMinimumRailFunding - railFunding) * f64(NwTransitDeficitScale)));
  return 0;
}

void NwTransitLayer::BeginFlag108Phase() {
  if (phase == 2)
    return;
  ScMessage message(kNwMsg_TransitStrikeStarted);
  NwGetTransitMessageServer()->MessagePost(message, false);
  value268 = 0;
  phase = 1;
}

// Enumerate borrowed occupant views only in phase one, retaining each before publication count is stored.
// Capacity limits traversal while the actual lock is held; rejected phases leave both outputs untouched.
u8 NwTransitLayer::EnumerateUpkeepOccupants(ScOccupant **out, u32 capacity, u32 *count) {
  if (phase != 1)
    return 0;
  lock->Lock();
  NwUpkeepLink *head = upkeep.head;
  u32 available = 0;
  for (NwUpkeepLink *link = head->next; link != head; link = link->next)
    ++available;
  if (available > capacity)
    available = capacity;
  u32 used = 0;
  for (NwUpkeepLink *link = head->next; used < available && link != head; link = link->next) {
    ScOccupant *occupant = link->value->AsISC3Occupant();
    out[used++] = occupant;
    occupant->AddRef();
  }
  *count = used;
  lock->Unlock();
  return 1;
}

// Acquire the actual road/rail budget views and valve, then subscribe the layer and post its retained phase notification.
// Preserve partial acquisition on early refusal and repeat the module service lookup before each original callback.
bool NwTransitLayer::SimulationBegin() {
  if (!budget->GetDepartmentBudget(kEcRoadDepartmentId, &roadBudget))
    return false;
  roadBudget->AddRef();
  if (!budget->GetDepartmentBudget(kEcRailDepartmentId, &railBudget))
    return false;
  railBudget->AddRef();
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (layer)
    layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  if (valves)
    valves->GetValvePointer(kEcWorkforceValveId, IID_EcWorkforce, reinterpret_cast<void **>(&trafficValve));
  ApplyTunables();
  NwGetTransitMessageServer()->AddNotification(this, kOcMsg_SurfaceInserted);
  NwGetTransitMessageServer()->AddNotification(this, kOcMsg_SurfaceRemoved);
  NwGetTransitMessageServer()->AddNotification(this, kOcMsg_SubwayInserted);
  NwGetTransitMessageServer()->AddNotification(this, kOcMsg_SubwayRemoved);
  surface->PostOccupantAll(static_cast<ScMessageTarget *>(this));
  underground->PostOccupantAll(static_cast<ScMessageTarget *>(this));
  if (phase == 0) {
    ScMessage message(kNwMsg_TransitStrikePending);
    NwGetTransitMessageServer()->MessagePost(message, false);
  } else if (phase == 1) {
    ScMessage message(kNwMsg_TransitStrikeStarted);
    NwGetTransitMessageServer()->MessagePost(message, false);
  }
  return true;
}

// Unregister notifications before releasing the actual retained services and deleting grids in their installed order.
// The upkeep list erases links without inventing payload releases; its separate scalar fields remain untouched.
bool NwTransitLayer::Shutdown() {
  initialized = 0;
  NwGetTransitApplication()->GetCheatCodeManager()->RemoveNotification(this, kNwCheat_TrafficLights);
  NwGetTransitApplication()->GetCheatCodeManager()->UnregisterCheatCode(kNwCheat_TrafficLights);
  NwGetTransitMessageServer()->RemoveNotification(this, kOcMsg_SurfaceInserted);
  NwGetTransitMessageServer()->RemoveNotification(this, kOcMsg_SurfaceRemoved);
  NwGetTransitMessageServer()->RemoveNotification(this, kOcMsg_SubwayInserted);
  NwGetTransitMessageServer()->RemoveNotification(this, kOcMsg_SubwayRemoved);
  ScRelease(&budget);
  ScRelease(&neighbors);
  ScRelease(&roadBudget);
  ScRelease(&railBudget);
  ScRelease(&surface);
  ScRelease(&underground);
  ScRelease(&trafficValve);
  ScRelease(&valves);
  ScRelease(&ordinances);
  ScRelease(&network);
  ScRelease(&city);
  ScDelete(&grid96);
  ScDelete(&grid100);
  ScDelete(&grid104);
  ScDelete(&grid108);
  ScDelete(&grid112);
  ScDelete(&grid116);
  ScDelete(&grid92);
  ScDelete(&grid120);
  ScDelete(&grid128);
  ScDelete(&grid124);
  ScDelete(&grid132);
  ScDelete(&grid136);
  ScDelete(&grid140);
  ScRelease(&history);
  lock->Lock();
  upkeep.Clear();
  lock->Unlock();
  NwTransitActiveLayer = nullptr;
  return true;
}

// Build the original single-cell trip aggregate with its actual mode and lifetime flags before routing it.
u8 NwTransitLayer::RouteCellTrip(u32 x, u32 z, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) {
  NwTripData trip;
  trip.occupant = nullptr;
  trip.x0 = x;
  trip.z0 = z;
  trip.x1 = x;
  trip.z1 = z;
  trip.mode = 0x80;
  trip.value21 = 0;
  trip.value24 = 0;
  trip.value28 = 0;
  trip.value29 = 1;
  return RouteTrip(&trip, kind, amount, flag, first, second, last);
}

// Obtain the occupant's genuine bounds before retaining it and building the same route aggregate used by cell trips.
u8 NwTransitLayer::RouteOccupantTrip(
    ScOccupant *occupant, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) {
  GeBounds bounds = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  occupant->AddRef();
  NwTripData trip;
  trip.occupant = occupant;
  trip.x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
  trip.z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  trip.x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
  trip.z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  trip.mode = 0x80;
  trip.value21 = 0;
  trip.value24 = 0;
  trip.value28 = 0;
  trip.value29 = 1;
  return RouteTrip(&trip, kind, amount, flag, first, second, last);
}

NwTrafficStatsManipulator::NwTrafficStatsManipulator() : references(0) {
}

NwTrafficStatsManipulator::~NwTrafficStatsManipulator() {
  NwTransitActiveLayer = nullptr;
}

// Clear the genuine output before resolving the two real manipulator identities and retaining virtually.
bool NwTrafficStatsManipulator::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the count before the actual module callback and return its live value afterward.
u32 NwTrafficStatsManipulator::AddRef() {
  ++references;
  NwGetTransitDirector()->AddRef();
  return references;
}

// Drop a nonzero count and module hold, then return the live count without deleting this module-owned receiver.
u32 NwTrafficStatsManipulator::Release() {
  if (references) {
    --references;
    NwGetTransitDirector()->RemoveRef();
  }
  return references;
}

bool NwTrafficStatsManipulator::IsLockingCity() {
  return false;
}

bool NwTrafficStatsManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwTrafficStatsManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool NwTrafficStatsManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

NwTripManipulator::NwTripManipulator(u32 value, u32 index)
    : references(0), kind(value), flag12(1), flag13(value < 2), flag14(value < 2), flag15(value < 2), phase(index) {
}

NwTripManipulator::~NwTripManipulator() {
  NwTransitActiveLayer = nullptr;
}

// Resolve the same two actual manipulator identities, clearing output before unsupported requests.
bool NwTripManipulator::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the actual transit director after incrementing, preserving callback-visible count order.
u32 NwTripManipulator::AddRef() {
  ++references;
  NwGetTransitDirector()->AddRef();
  return references;
}

// Release only an existing count and module hold; explicit static shutdown owns final destruction.
u32 NwTripManipulator::Release() {
  if (references) {
    --references;
    NwGetTransitDirector()->RemoveRef();
  }
  return references;
}

bool NwTripManipulator::IsLockingCity() {
  return false;
}

bool NwTripManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwTripManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool NwTripManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Retain the actual city and owner before capturing dimensions in Z-then-X order and selecting the traffic raster kind.
NwTrafficRaster::NwTrafficRaster(CtCity *value, NwTransitLayer *owner) : ready(0), layer(nullptr), city(nullptr) {
  if (value) {
    city = value;
    value->AddRef();
  }
  if (owner) {
    layer = owner;
    owner->AddRef();
  }
  u32 z = city->CellCountZ(), x = city->CellCountX();
  SetId(kUiTrafficMapRasterId);
  SetBaseSize(x, z);
  if (city && layer)
    ready = 1;
}

NwTrafficRaster::~NwTrafficRaster() {
  ScRelease(&layer);
  ScRelease(&city);
}

// Read traffic only for actual road/rail cells, then use the live palette count and original231-density scale.
// Palette absence or color lookup refusal remains the original recoverable failure.
bool NwTrafficRaster::GetColor(u32 x, u32 z, u32 *out) {
  if (!ready || (!layer->HasNetworkMode(x, z, 21) && !layer->HasNetworkMode(x, z, 2)))
    return false;
  u8 volume;
  layer->GetVolume(x, z, &volume, 23);
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i32 count = colors->GetNumberEntries();
  i64 index = i64(f64(count) * (f64(volume) * 0.004329004329004329));
  return colors->GetColor(i32(index), out);
}
