#include "buildings/bd_power.h"
#include <math.h>
#include <string.h>
#include <map>
#include "advisors/ad_module.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "buildings/bd_utility_graph.h"
#include "buildings/bd_water.h"
#include "city/api.h"
#include "city/ct_scenario.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "geometry/utilities.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "misc/api.h"
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
#include "window/api.h"

void BdMicrowavePlant::LoadTunableParameters() {
  BdReadPlantTuning("MicroWavePowerPlant", &BdMicrowaveMaximum, &BdMicrowaveDecline, &BdMicrowaveVariation);
  BdMicrowaveLoaded = 1;
}

// Initialize microwave tuning and the live city capacity before applying the shared lifespan variation.
BdMicrowavePlant::BdMicrowavePlant() {
  if (!BdMicrowaveLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12114, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdMicrowaveMaximum, BdMicrowaveDecline, BdMicrowaveVariation);
}

BdMicrowavePlant::~BdMicrowavePlant() {
}

void BdNuclearPlant::LoadTunableParameters() {
  BdReadPlantTuning("NuclearPowerPlant", &BdNuclearMaximum, &BdNuclearDecline, &BdNuclearVariation);
  BdNuclearLoaded = 1;
}

// Initialize nuclear tuning and the live city capacity before applying the shared lifespan variation.
BdNuclearPlant::BdNuclearPlant() {
  if (!BdNuclearLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12115, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdNuclearMaximum, BdNuclearDecline, BdNuclearVariation);
}

BdNuclearPlant::~BdNuclearPlant() {
}

// Start the nuclear effect, replace the plant with rubble and mark nearby pollution cells radioactive.
// uses rounded radial distance and consumes a random draw only for the outer ring.
void BdNuclearPlant::Explode() {
  void *created = nullptr;
  if (!BdGetPowerCom()->GetClassObject(kSpNuclearViewClassId, IID_SpNuclearView, &created) || !created)
    return;
  SpNuclearExplosionSimulator *effect = static_cast<SpNuclearExplosionSimulator *>(created);
  ScGetSimutilApplication()->GetBoxX()->Event(kAuEventPlayBuildingSound, 0, 0, 0, 0);
  u32 x, z;
  GetLocationInCellCoords(&x, &z);
  u8 altitude;
  BdPowerTerrain->GetVertexAltitude(x, z, &altitude);
  effect->Init(i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift),
      i32(u32(altitude) << kCtMicroCoordinateShift), 0, 0);
  effect->Release();
  u32 type = static_cast<CtAgent *>(this)->TypeId();
  BdBuilding *rubble = GetCorrespondingBuildingRubble();
  BdPowerOccupants->RemoveOccupant(static_cast<OcUtilityOccupant *>(this));
  if (rubble) {
    BdPowerBuildings->InsertBuilding(rubble);
    rubble->Release();
  }
  ScMessage message(kBdMsg_PowerPlantDestroyed, type, x, z);
  ScGetSimutilMessageServer()->MessagePost(message, false);
  u32 firstX = x <= 20 ? 0 : x - 20, firstZ = z <= 20 ? 0 : z - 20;
  CtCity *city = ScGetSimutilCity();
  u32 width = city->CellCountX(), depth = city->CellCountZ();
  u32 lastX = x + 20, lastZ = z + 20;
  if (lastX >= width)
    lastX = width - 1;
  if (lastZ >= depth)
    lastZ = depth - 1;
  EcLayer *pollution = city->PollutionLayer();
  if (pollution) {
    for (u32 row = firstZ; row <= lastZ; ++row) {
      for (u32 column = firstX; column <= lastX; ++column) {
        u32 dx = column - x, dz = row - z;
        i32 squared = i32(dx * dx + dz * dz);
        u32 distance = u32(sqrt(f64(squared)) + 0.5);
        if (distance <= 20 && (distance <= 10 || BdPowerRandom.Bounded(10)))
          pollution->SetIsRadioactive(column, row, true);
      }
    }
  }
}

void BdOilPlant::LoadTunableParameters() {
  BdReadPlantTuning("OilPowerPlant", &BdOilMaximum, &BdOilDecline, &BdOilVariation);
  BdOilLoaded = 1;
}

// Initialize oil tuning and the live city capacity before applying the shared lifespan variation.
BdOilPlant::BdOilPlant() {
  if (!BdOilLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12112, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdOilMaximum, BdOilDecline, BdOilVariation);
}

BdOilPlant::~BdOilPlant() {
}

void BdSolarPlant::LoadTunableParameters() {
  BdReadPlantTuning("SolarPowerPlant", &BdSolarMaximum, &BdSolarDecline, &BdSolarVariation);
  BdSolarLoaded = 1;
}

// Initialize solar tuning and the live city capacity before applying the shared lifespan variation.
BdSolarPlant::BdSolarPlant() {
  if (!BdSolarLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12117, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdSolarMaximum, BdSolarDecline, BdSolarVariation);
}

BdSolarPlant::~BdSolarPlant() {
}

void BdWastePowerPlant::LoadTunableParameters() {
  GeIni ini;
  ScString path, value;
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SC3PowerLayer.INI", 22);
  ini.SetPath(path);
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  ScString section("WasteToEnergyPowerPlant");
  if (ini.ReadEntry(section, ScString("OptimalGarbageCap"), &value))
    BdWasteOptimal = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("MaxLifeSpan"), &value))
    BdWasteMaximum = GeParseNumber(value.ToChar());
  if (!BdWasteMaximum)
    BdWasteMaximum = 1;
  if (ini.ReadEntry(section, ScString("DeclineAge"), &value))
    BdWasteDecline = GeParseNumber(value.ToChar());
  if (BdWasteDecline >= BdWasteMaximum)
    BdWasteDecline = BdWasteMaximum >> 1;
  if (ini.ReadEntry(section, ScString("PercentageVariationInLongevity"), &value))
    BdWasteVariation = f32(f64(GeParseNumber(value.ToChar())) * 0.01);
  BdWasteLoaded = 1;
}

BdWastePowerPlant::BdWastePowerPlant() {
  if (!BdWasteLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12853, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdWasteMaximum, BdWasteDecline, BdWasteVariation);
  dailyGarbageCapacity = 0;
  previousMonthGarbageConsumed = 0;
  garbageConsumed = 0;
}

BdWastePowerPlant::~BdWastePowerPlant() {
}

// Restore the animated power building before reading the three waste-capacity fields in descriptor order.
// A null descriptor fails before the base initializer; later reads stop at the first refusal.
bool BdWastePowerPlant::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !BdPowerBuildingAnimated::Init(city, info))
    return false;
  return info->GetIntData(kBdWastePlantDailyGarbageCapacityKey, &dailyGarbageCapacity) &&
      info->GetIntData(kBdWastePlantGarbageConsumedKey, &garbageConsumed) &&
      info->GetIntData(kBdWastePlantPreviousMonthGarbageConsumedKey, &previousMonthGarbageConsumed);
}

bool BdWastePowerPlant::Save(CtCity *city, OcPersistInfo *info) {
  return BdPowerBuildingAnimated::Save(city, info) &&
      info->SetIntData(kBdWastePlantDailyGarbageCapacityKey, dailyGarbageCapacity) &&
      info->SetIntData(kBdWastePlantGarbageConsumedKey, garbageConsumed) &&
      info->SetIntData(kBdWastePlantPreviousMonthGarbageConsumedKey, previousMonthGarbageConsumed);
}

// Publish the adjusted incinerator view for its private identity; other requests use the shared power-building query.
// The original clears the output before either route and retains the complete receiver on success.
bool BdWastePowerPlant::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid != IID_EcCombustion)
    return BdPowerBuildingAnimated::QueryInterface(iid, out);
  *out = static_cast<EcCombustion *>(this);
  BdPowerBuildingAnimated::AddRef();
  return true;
}

u32 BdWastePowerPlant::AddRef() {
  return BdPowerBuildingAnimated::AddRef();
}

u32 BdWastePowerPlant::Release() {
  return BdPowerBuildingAnimated::Release();
}

void BdWastePowerPlant::SetMaximumAge(u32 value) {
  BdPowerPlant::SetMaximumAge(value);
}

u32 BdWastePowerPlant::GetMaximumAge() {
  return BdPowerPlant::GetMaximumAge();
}

void BdWastePowerPlant::SetDeclineAge(u32 value) {
  BdPowerPlant::SetDeclineAge(value);
}

u32 BdWastePowerPlant::GetDeclineAge() {
  return BdPowerPlant::GetDeclineAge();
}

u32 BdWastePowerPlant::GetAge() {
  return BdPowerPlant::GetAge();
}

bool BdWastePowerPlant::IsAging() {
  return BdPowerPlant::IsAging();
}

void BdWastePowerPlant::Retire() {
  BdPowerPlant::Retire();
}

u8 BdWastePowerPlant::GetRetired() {
  return BdPowerPlant::GetRetired();
}

// Borrow the building subobject through the incinerator mortality interface's original slot44.
BdBuilding *BdWastePowerPlant::AsISC3Building() {
  return BdPowerBuildingAnimated::AsISC3Building();
}

// Borrow the occupant subobject through the original adjusted building view.
ScOccupant *BdWastePowerPlant::GetOccupant() {
  return BdPowerBuildingAnimated::AsISC3Occupant();
}

u32 BdWastePowerPlant::GetProduction() {
  return dailyGarbageCapacity;
}

u32 BdWastePowerPlant::GetCapacity() {
  u32 monthly = 30 * dailyGarbageCapacity;
  return monthly > garbageConsumed ? monthly - garbageConsumed : 0;
}

u32 BdWastePowerPlant::Consume(u32 amount) {
  garbageConsumed += amount;
  return amount;
}

// Move the accumulated garbage consumption into the preceding-month field and reset the current total.
void BdWastePowerPlant::DoMonthlySim() {
  u32 previous = garbageConsumed;
  garbageConsumed = 0;
  previousMonthGarbageConsumed = previous;
}

// Recalculate daily garbage capacity from the operating state, traffic route and age-related decline.
// preserves the previous value when the current city is unavailable.
void BdWastePowerPlant::DoWeeklySim() {
  CtCity *city = ScGetSimutilCity();
  if (!city)
    return;
  if (GetOccupancyState() != 1) {
    dailyGarbageCapacity = 0;
    return;
  }
  NwTrafficLayer *traffic = city->TrafficLayer();
  u32 x, z;
  GetLocationInCellCoords(&x, &z);
  if (traffic) {
    u32 maximum = traffic->GetMaxDimension();
    if (!traffic->RouteCellTrip(x, z, 3, i32(maximum), 0, 1, 0, 0)) {
      dailyGarbageCapacity = 0;
      return;
    }
  }
  u32 age = GetAge();
  if (age <= declineAge)
    dailyGarbageCapacity = BdWasteOptimal;
  else if (age >= maximumAge)
    dailyGarbageCapacity = 0;
  else
    dailyGarbageCapacity = BdWasteOptimal - BdWasteOptimal * (age - declineAge) / (maximumAge - declineAge);
}

// Run the building's normal simulation, then update waste capacity weekly or roll consumption over monthly.
bool BdWastePowerPlant::Simulate(i32 phase) {
  bool result = OcUtilityBuilding::Simulate(phase);
  if (phase == 3)
    DoWeeklySim();
  else if (phase == 4)
    DoMonthlySim();
  return result;
}

i32 BdWastePowerPlant::MinimumExecutionInterval() {
  i32 interval = OcUtilityBuilding::MinimumExecutionInterval();
  return interval >= 3 ? 3 : interval;
}

// Add the original waste-plant status after the generic building options have been populated.
// Operational plants report an available trip before aging; retired inactive plants report their own status.
u8 BdWastePowerPlant::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (GetOccupancyState() == 1) {
    NwTrafficLayer *traffic = ScGetSimutilCity()->TrafficLayer();
    if (traffic &&
        traffic->IsTripAvailable(
            position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask) == 1)
      instance = 440;
    else if (IsAging())
      instance = 406;
  } else if (GetRetired()) {
    instance = 407;
  }
  if (instance) {
    ScString status = ScUtilityQueryText(instance);
    query->SetStatusText(status);
  }
  return 1;
}

// Add the scaled monthly maximum, current capacity and previous consumption rows before the shared power rows.
// The original converts wrapped products as signed words and rounds prior consumption through a float temporary.
u8 BdWastePowerPlant::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, formatted, pattern, number;
  ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  EcLayer *pollution = ScGetSimutilCity()->PollutionLayer();
  if (query && pollution) {
    f64 scale = pollution->GetGarbageScalingFactor();
    u32 amount = BdWasteQueryRound(floor(scale * i32(30u * BdWasteOptimal)));
    label = ScUtilityQueryText(kRsCityServiceMaximumDisposalCapacityText);
    pattern = ScUtilityQueryText(kRsCityServiceTonsMonthFormatText);
    utility->MakeNumberString(i32(amount), &number);
    formatted.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, 0);

    label = ScUtilityQueryText(kRsCityServiceCurrentDisposalCapacityText);
    pattern = ScUtilityQueryText(kRsCityServiceTonsMonthFormatText);
    scale = pollution->GetGarbageScalingFactor();
    amount = BdWasteQueryRound(floor(scale * i32(30u * dailyGarbageCapacity)));
    utility->MakeNumberString(i32(amount), &number);
    u8 warning = dailyGarbageCapacity == 0;
    formatted.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, warning);

    label = ScUtilityQueryText(120);
    scale = pollution->GetGarbageScalingFactor();
    f32 previous = f32(scale * i32(previousMonthGarbageConsumed));
    pattern = ScUtilityQueryText(kRsCityServiceTonsMonthFormatText);
    utility->MakeNumberString(i32(BdWasteQueryRound(previous)), &number);
    formatted.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, 0);
  }
  return BdPowerBuildingAnimated::QueryTile(message, query);
}

void BdWindPlant::LoadTunableParameters() {
  BdReadPlantTuning("WindPowerPlant", &BdWindMaximum, &BdWindDecline, &BdWindVariation);
  BdWindLoaded = 1;
}

// Initialize wind tuning and the live city capacity before applying the shared lifespan variation.
BdWindPlant::BdWindPlant() {
  if (!BdWindLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12118, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdWindMaximum, BdWindDecline, BdWindVariation);
}

BdWindPlant::~BdWindPlant() {
}

void BdWindPlant::DetermineActualCapacity() {
  u32 limitX = BdPowerTerrain->VertexCountX() - 2, limitZ = BdPowerTerrain->VertexCountZ() - 2;
  u32 x, z;
  u8 center, neighbor;
  OcUtilityOccupant::GetLocationInCellCoords(&x, &z);
  BdPowerTerrain->GetVertexAltitude(x, z, &center);
  u32 sum = center;
  if (x < limitX) {
    BdPowerTerrain->GetVertexAltitude(x + 2, z, &neighbor);
    sum = neighbor;
  }
  if (z < limitZ) {
    BdPowerTerrain->GetVertexAltitude(x, z + 2, &neighbor);
    sum += neighbor;
  } else {
    sum += center;
  }
  if (x) {
    BdPowerTerrain->GetVertexAltitude(x - 1, z, &neighbor);
    sum += neighbor;
  } else {
    sum += center;
  }
  if (z) {
    BdPowerTerrain->GetVertexAltitude(x, z - 1, &neighbor);
    sum += neighbor;
  } else {
    sum += center;
  }
  f64 gradient = (f64(center) - f64(sum) * 0.25) / 2.5;
  if (gradient > 1.0)
    gradient = 1.0;
  else if (gradient < -1.0)
    gradient = -1.0;
  u32 minimum = u32(i64(f64(capacity) * f64(0.4f)));
  u32 adjusted = u32(i64(f64(capacity - minimum) * (gradient + 1.0) * 0.5 + f64(i32(minimum))));
  output = CalculateCapacityBasedOnAge(adjusted);
}

u32 BdCoalPlant::PowerSourceType() {
  return 1;
}

u32 BdFusionPlant::PowerSourceType() {
  return 2;
}

u32 BdGasPlant::PowerSourceType() {
  return 3;
}

u32 BdMicrowavePlant::PowerSourceType() {
  return 4;
}

u32 BdNuclearPlant::PowerSourceType() {
  return 5;
}

u32 BdOilPlant::PowerSourceType() {
  return 6;
}

u32 BdSolarPlant::PowerSourceType() {
  return 7;
}

u32 BdWastePowerPlant::PowerSourceType() {
  return 8;
}

u32 BdWindPlant::PowerSourceType() {
  return 9;
}

static ScLock OcUtilityLock;

// Create the query dialog selected by the bound occupant attributes and publish its actual window interface.
// A missing attribute leaves the cleared output and returns false without consulting COM.
bool OcUtilityOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 clsid = attributes->GetQueryDialogClass();
  void *created = nullptr;
  bool result = ScGetSimutilDirector()->GZCOM()->GetClassObject(clsid, IID_UiQueryWindow, &created);
  *out = static_cast<UiQueryWindow *>(created);
  return result;
}

OcUtilityOccupant::OcUtilityOccupant()
    : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

OcUtilityOccupant::~OcUtilityOccupant() {
  if (attributes)
    OcUtilityOccupant::Shutdown();
}

// Publish the occupant primary for the four original identities and retain it through the live override. Refused
// queries clear the output.
bool OcUtilityOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the module, then increment the occupant's packed sixteen-bit count under its original lock.
u32 OcUtilityOccupant::AddRef() {
  ScGetSimutilDirector()->AddRef();
  OcUtilityLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  OcUtilityLock.Unlock();
  return count;
}

// Release a held module reference under the count lock, then unlock before virtual shutdown and deletion.
u32 OcUtilityOccupant::Release() {
  OcUtilityLock.Lock();
  bool last = u16(flags) == 0;
  if (!last) {
    ScGetSimutilDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    last = u16(flags) == 0;
  }
  OcUtilityLock.Unlock();
  if (last) {
    Shutdown();
    delete this;
    return 0;
  }
  return u16(flags);
}

bool OcUtilityOccupant::DoMessage(ScMessage *) {
  return false;
}

// Dispatch the original query messages through the matching acquired query interface and release it afterward.
bool OcUtilityOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
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

// Resolve the key through the application's real occupant cache and bind the returned attribute virtually.
// A previously bound attribute is retained without another cache lookup.
bool OcUtilityOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!ScGetSimutilAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// Replace the attribute and sprite chain, then create the default-layer sprite and initialize an unset subtype.
// A failed sprite creation invokes the live virtual shutdown before returning false.
bool OcUtilityOccupant::Init(GeAttrib *value) {
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

// Bind an uninitialized occupant through its descriptor and apply the original packed coordinates and flags.
// Failure preserves the original zero-initialized partial outputs and sets both removal permissions.
bool OcUtilityOccupant::Init(CtCity *, OcPersistInfo *info) {
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

// Publish the bound key, live type, packed position and removal flags into the original descriptor.
// Both size arguments intentionally come from separate virtual X-size calls in the installed body.
bool OcUtilityOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  info->SetTypeId(TypeId());
  info->SetLocationInCells(position & kOcPositionCellValueMask,
      (position >> kOcPositionZShift) & kOcPositionCellValueMask, u8(position >> kOcPositionAltitudeShift));
  u32 second = SizeInCellsX(), first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  info->SetRemoveFlags(u8((flags >> kOcSimRemoveShift) & 1), u8((flags >> kOcUserRemoveShift) & 1));
  return true;
}

// Release the attribute before destroying the virtual sprite chain, retaining only the original flag subset.
void OcUtilityOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Create the requested attribute sprite and apply its packed rotation and the live virtual geometry flags.
bool OcUtilityOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (definition && definition->CreateInstance(out)) {
    (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
    (*out)->SetGeomFlag(GetGeomFlag());
    return true;
  }
  attributes->Key();
  return false;
}

// Capture each successor before releasing its sprite and clear the live chain output afterward.
void OcUtilityOccupant::DestroySprites(SpSprite **out) {
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

// Restore the stream fields in the installed order, ignoring the same transport replies.
bool OcUtilityOccupant::ReadState(CtCity *, RsStream *record) {
  RsKey key;
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
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  CtCoord coordinate{
      i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), i32(altitude << kCtMicroCoordinateShift)};
  SetLocation(coordinate);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return true;
}

// Write the genuine resource key and separate virtual fields in the installed order.
// A failed attribute-resource query retains its original false result and output effects.
bool OcUtilityOccupant::WriteState(CtCity *, RsStream *record) {
  GeAttrib *value = OcUtilityOccupant::GetAttribData();
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
  CtCoord coordinate{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
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

u8 OcUtilityOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

RsUtilityText::RsUtilityText(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  if (!ScGetSimutilResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

RsUtilityText::~RsUtilityText() {
  ScRelease(&text);
}

// Copy the retained string, or construct the empty result when resource lookup produced no string.
ScString RsUtilityText::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Copy a utility query label while the original localized-resource holder is still live.
ScString ScUtilityQueryText(u32 instance) {
  RsUtilityText resource(instance, kRsCityServiceTextGroup);
  ScString text = resource.GetText(), copy(text);
  return copy;
}

// Emit the localized architect row when the live override supplies a nonempty architect name.
u8 OcUtilityOccupant::QueryTile(ScMessage *, UiQuery *query) {
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
    RsUtilityText resource(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString text = resource.GetText(), copy(text);
    label = copy;
  }
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

u8 OcUtilityOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 OcUtilityOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

OcUtilityBuilding::OcUtilityBuilding()
    : occupancyFlags(0), agentType(nullptr), cachedAttributes(nullptr), cachedSprite(nullptr) {
}

OcUtilityBuilding::~OcUtilityBuilding() {
  ScRelease(&agentType);
}

// Clear the output and publish the city-agent or building view, forwarding other identities to the occupant base.
bool OcUtilityBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  return OcUtilityOccupant::QueryInterface(iid, out);
}

u32 OcUtilityBuilding::AddRef() {
  return OcUtilityOccupant::AddRef();
}

u32 OcUtilityBuilding::Release() {
  return OcUtilityOccupant::Release();
}

bool OcUtilityBuilding::Init(RsKey *key) {
  return OcUtilityOccupant::Init(key);
}

// Bind the generic occupant and populate the borrowed caches and retained agent type in original order.
// A failed generic binding invokes the building's live shutdown path.
bool OcUtilityBuilding::Init(GeAttrib *value) {
  if (!OcUtilityOccupant::Init(value)) {
    Shutdown();
    return false;
  }
  cachedAttributes = nullptr;
  cachedSprite = nullptr;
  if (!agentType) {
    agentType = GetBaseAttribData()->CityAgentType();
    if (agentType)
      agentType->AddRef();
  }
  return true;
}

// Restore generic occupant data and the required occupancy field before optional name and creation date.
bool OcUtilityBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcUtilityOccupant::Init(city, info) || !info->HasLayerData1())
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

// Save generic data, optional changed name and creation date, then the full occupancy word.
bool OcUtilityBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcUtilityOccupant::Save(city, info))
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
  info->SetLayerData1(occupancyFlags);
  return true;
}

// Release the retained agent type, clear both borrowed caches and shut down the adjusted occupant.
void OcUtilityBuilding::Shutdown() {
  ScRelease(&agentType);
  cachedAttributes = nullptr;
  cachedSprite = nullptr;
  OcUtilityOccupant::Shutdown();
}

bool OcUtilityBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return OcUtilityOccupant::DoQueryInfo(message, sender);
}

// Borrow the adjusted occupant view without taking another reference.
ScOccupant *OcUtilityBuilding::AsISC3Occupant() {
  return static_cast<OcUtilityOccupant *>(this);
}

// Borrow the adjusted city-agent view without taking another reference.
CtAgent *OcUtilityBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool OcUtilityBuilding::Simulate(i32 elapsed) {
  return agentType->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 OcUtilityBuilding::MinimumExecutionInterval() {
  return agentType ? agentType->MinimumExecutionInterval() : 6;
}

bool OcUtilityBuilding::IsAgentLocking() {
  return true;
}

// Borrow the stored city-agent type without changing its reference count.
CtAgentType *OcUtilityBuilding::AgentType() {
  return agentType;
}

bool OcUtilityBuilding::SetAgentType(CtAgentType *) {
  return false;
}

u32 OcUtilityBuilding::TypeId() const {
  return OcUtilityOccupant::TypeId();
}

u32 OcUtilityBuilding::GetOccupancyState() {
  return u8(occupancyFlags);
}

// Replace occupancy with the input byte and refresh only when the stored state changes.
void OcUtilityBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancyFlags != state) {
    occupancyFlags = state;
    OnOccupancyStateChanged();
  }
}

// Add requested occupancy bits and refresh on a changed state when the mask includes a low-byte bit.
void OcUtilityBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancyFlags | mask;
  if (value != occupancyFlags) {
    occupancyFlags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

bool OcUtilityBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (occupancyFlags & mask) != 0;
}

// Clear requested occupancy bits and refresh only for changed low-byte masks.
void OcUtilityBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancyFlags & ~mask;
  if (value != occupancyFlags) {
    occupancyFlags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

u32 OcUtilityBuilding::GetGeomFlag() {
  return 0x104;
}

u32 OcUtilityBuilding::GetMemUse() const {
  return 44;
}

// Borrow cached attributes, consulting city defaults for inactive states before falling back to generic attributes.
GeAttrib *OcUtilityBuilding::GetAttribData() {
  if (cachedAttributes)
    return cachedAttributes;
  if (occupancyFlags != 1 && !(u8(occupancyFlags) == 1 && (occupancyFlags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = ScGetSimutilCity()->BuildingLayer();
    if (layer) {
      cachedAttributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cachedAttributes)
        return cachedAttributes;
    }
  }
  cachedAttributes = OcUtilityOccupant::GetAttribData();
  return cachedAttributes;
}

// Borrow the generic occupant's attributes without using the building's selected-default cache.
GeAttrib *OcUtilityBuilding::GetBaseAttribData() {
  return OcUtilityOccupant::GetAttribData();
}

// Select the live default layer before using this building's qualified indexed sprite lookup.
SpDefinition *OcUtilityBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return OcUtilityBuilding::GetSpriteAttrib(layer);
}

// Borrow the cached sprite definition, selecting city defaults for inactive states before the generic fallback.
SpDefinition *OcUtilityBuilding::GetSpriteAttrib(u32 layer) {
  if (cachedSprite)
    return cachedSprite;
  if (occupancyFlags == 1 || (u8(occupancyFlags) == 1 && (occupancyFlags & 0xff00) == 0x200)) {
    cachedSprite = OcUtilityOccupant::GetSpriteAttrib(layer);
    return cachedSprite;
  }
  GeBuildingLayer *buildings = ScGetSimutilCity()->BuildingLayer();
  if (buildings) {
    cachedSprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cachedSprite)
      return cachedSprite;
  }
  cachedSprite = OcUtilityOccupant::GetSpriteAttrib();
  return cachedSprite;
}

// Query the building-attribute view and preserve its raw power byte before releasing the acquired view.
u8 OcUtilityBuilding::PowerRequired() {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  void *selected;
  if (!value->QueryInterface(IID_GeBuildingType, &selected))
    return 0;
  GeBuildingType *building = static_cast<GeBuildingType *>(selected);
  u8 power = building->PowerRequired();
  building->Release();
  return power;
}

bool OcUtilityBuilding::SetName(const ScStringView &) {
  return false;
}

bool OcUtilityBuilding::IsNameChangeable() {
  return false;
}

bool OcUtilityBuilding::HasBirthdate() const {
  return false;
}

bool OcUtilityBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool OcUtilityBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

bool OcUtilityBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcUtilityBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool OcUtilityBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

// Publish the first matching occupancy status, then the localized option and its live checked state and font.
u8 OcUtilityBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
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
    status = ScUtilityQueryText(instance);
    query->SetStatusText(status);
  }
  ScString caption = ScUtilityQueryText(kRsCityServiceMakeHistoricalText);
  WnResources *resources = ScGetSimutilWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return OcUtilityOccupant::QueryBasic(message, query);
}

// For inactive occupancy states, add the original localized footprint-cost row before the generic architect row.
u8 OcUtilityBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *language = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!language)
      return 0;
    EcBudgetLayer *budget = ScGetSimutilCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label = ScUtilityQueryText(kRsCityServiceCleanUpCostText),
               formatted = ScUtilityQueryText(kRsCityServiceCleanUpCostFormatText), number;
      language->MakeMoneyString(i32(amount), &number);
      ScString format(formatted);
      formatted.Sprintf(format.ToChar(), number.ToChar());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return OcUtilityOccupant::QueryTile(message, query);
}

u8 OcUtilityBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcUtilityOccupant::QueryDetail(message, query);
}

u8 OcUtilityBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return OcUtilityOccupant::QueryResponse(message, query);
}

// Capture the live graphics lock for the original sprite-replacement scope.
class ScUtilityGraphicsLock {
public:
  // Enter the acquired city's actual graphics critical section.
  explicit ScUtilityGraphicsLock(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Leave the same critical section when the update scope ends.
  ~ScUtilityGraphicsLock() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Apply the original sprite-refresh effects under the city's graphics lock.
void OcUtilityBuilding::OnOccupancyStateChanged() {
  ScUtilityGraphicsLock scope(ScGetSimutilCity()->GetGraphicsCriticalSection());
  cachedAttributes = nullptr;
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

// Restore generic occupant state before reading the signed occupancy word.
// The original maps a zero word to one even if the transport reports failure.
bool OcUtilityBuilding::ReadState(CtCity *city, RsStream *record) {
  if (!OcUtilityOccupant::ReadState(city, record))
    return false;
  i32 state = 0;
  bool valid = record->ReadI32(&state);
  if (!state)
    state = 1;
  SetOccupancyStateFlag(u32(state));
  return valid;
}

bool OcUtilityBuilding::WriteState(CtCity *city, RsStream *record) {
  if (!OcUtilityOccupant::WriteState(city, record))
    return false;
  return record->WriteI32(i32(occupancyFlags));
}

bool OcUtilityOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool OcUtilityOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 OcUtilityOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void OcUtilityOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 0x1f) << kOcMembershipShift;
}

void OcUtilityOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= ~((mask & 0x1f) << kOcMembershipShift);
}

bool OcUtilityOccupant::IsGeomFlag(u32 mask) {
  return (GetGeomFlag() & mask) == mask;
}

u8 OcUtilityOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 OcUtilityOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 OcUtilityOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 OcUtilityOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void OcUtilityOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 OcUtilityOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u8 OcUtilityOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

u32 OcUtilityOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 OcUtilityOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

void OcUtilityOccupant::AnimStart() {
}

void OcUtilityOccupant::AnimStop() {
}

bool OcUtilityOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void OcUtilityOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool OcUtilityOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void OcUtilityOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

bool OcUtilityOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Borrow the currently bound generic attributes without taking another reference.
GeAttrib *OcUtilityOccupant::GetAttribData() {
  return attributes;
}

// Resolve the requested sprite or city-agent subsystem through the live virtual getters and retain the result.
bool OcUtilityOccupant::GetSubSystemData(u32 id, void **out) {
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

u32 OcUtilityOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

// Borrow the bound attribute's city-agent type without adding a reference.
CtAgentType *OcUtilityOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

u32 OcUtilityOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Borrow the bound attribute's default sprite definition, or return null when unbound.
SpDefinition *OcUtilityOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the bound attribute's sprite definition for the requested layer.
SpDefinition *OcUtilityOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Borrow the occupant's primary sprite chain without retaining it.
SpSprite *OcUtilityOccupant::GetSpriteInst() {
  return sprite;
}

// Borrow the primary sprite only when the requested layer equals the live default layer.
SpSprite *OcUtilityOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 OcUtilityOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 OcUtilityOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

void OcUtilityOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool OcUtilityOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude into microcoordinates.
void OcUtilityOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void OcUtilityOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 OcUtilityOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Obtain both cell coordinates virtually, then scale the live outputs to animation units.
void OcUtilityOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u16 OcUtilityOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Publish the minimum location, then compute Y, Z and X extents through the live virtual size getters.
// Without bound attributes the original leaves the output untouched.
void OcUtilityOccupant::GetBounds(GeBounds *out) const {
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

// Publish the occupied cell rectangle, preserving virtual size calls and untouched outputs when unbound.
void OcUtilityOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

void OcUtilityOccupant::SetAltAnimCells(u16 value) {
  position = (position & kOcPositionWithoutAltitude) | (u32(value & 0xff00) << kOcAltitudeFromMicroShift);
}

u8 OcUtilityOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 OcUtilityOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 OcUtilityOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 OcUtilityOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << kCtMicroCoordinateShift;
}

u32 OcUtilityOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << kCtMicroCoordinateShift;
}

u32 OcUtilityOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << kCtMicroCoordinateShift;
}

// Pack the incoming microcoordinates into the existing position while retaining its orientation bits.
void OcUtilityOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutCellCoords) |
      ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << 11);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void OcUtilityOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void OcUtilityOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool OcUtilityOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void OcUtilityOccupant::SetMarkBit(bool value) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(value) << kOcMarkShift);
}

i32 OcUtilityOccupant::Tick(u32) {
  return 0;
}

void OcUtilityOccupant::SetSubTypeInfo(u8 value) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(value) << kOcSubtypeShift);
}

void OcUtilityOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Replace a changed packed rotation, retire the previous chain and create a sprite for the current default layer.
u8 OcUtilityOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  u32 layer = GetDefaultLayerFlag();
  CreateSprite(layer, &sprite);
  return 1;
}

u32 OcUtilityOccupant::GetMemUse() const {
  return 20;
}

bool OcUtilityOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcUtilityOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool OcUtilityOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

u8 GeUtilityAnimatedBuilding::AnimType() const {
  return 1;
}

// Start the borrowed sprite's animation at the packed occupant position with the current building width.
// The acquired animation view is released after the start call.
void GeUtilityAnimatedBuilding::AnimStart() {
  void *selected = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &selected)) {
    u32 packed = position, width = SizeInCellsX();
    SpAnimation *animation = static_cast<SpAnimation *>(selected);
    animation->Start(i32((packed & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(((packed >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(u32(u8(packed >> kOcPositionAltitudeShift)) << kCtMicroCoordinateShift), width);
    animation->Release();
  }
}

void GeUtilityAnimatedBuilding::AnimStop() {
  void *selected = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &selected)) {
    SpAnimation *animation = static_cast<SpAnimation *>(selected);
    animation->Stop();
    animation->Release();
  }
}

// Forward elapsed time and the sprite's result when one exists; otherwise return one.
i32 GeUtilityAnimatedBuilding::Tick(u32 elapsed) {
  SpSprite *current = GetSpriteInst();
  if (current)
    return current->Tick(elapsed);
  return 1;
}

namespace CtUtility {

// Preserve the original PC53 magic-add rounding and low-DWORD wrap instead of using a host integer conversion.
static u32 ScRoundGraphValue(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

ScGraphInfo::ScGraphInfo(u32 initial)
    : selectionLabelId(0), valueLabelId(0), color(0), maximum(initial), automaticScale(false), minimum(10),
      scaleFactor(1), monthly(12, initial), yearly(10, initial), decades(10, initial) {
}

// Retire the three histories before the four Strings and real module lifetime base in reverse member order.
ScGraphInfo::~ScGraphInfo() = default;

// Publish the original secondary graph identity for either accepted IID and retain through the real lifetime primary.
// Unsupported requests clear the output.
bool ScGraphInfo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScGraphInfoView);
  HANDLE_QUERY_INTERFACE(IID_ScGraphInfoView, ScGraphInfoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScGraphInfo::AddRef() {
  return ScRefCount::AddRef();
}

u32 ScGraphInfo::Release() {
  return ScRefCount::Release();
}

// Retain binary32 multiplication input, the exact2^32 float cap and source rounding before the final minimum clamp.
void ScGraphInfo::ComputeAutoMax() {
  u32 largest = 0;
  for (u32 i = 0; i < 12; ++i)
    if (monthly[i] > largest)
      largest = monthly[i];
  for (u32 i = 0; i < 10; ++i)
    if (yearly[i] > largest)
      largest = yearly[i];
  for (u32 i = 0; i < 10; ++i)
    if (decades[i] > largest)
      largest = decades[i];
  volatile f64 multiplication = f64(largest) * scaleFactor;
  f64 product = multiplication;
  if (product > 4294967296.0)
    product = 4294967296.0;
  u32 rounded = ScRoundGraphValue(product);
  maximum = rounded < minimum ? minimum : rounded;
}

// Borrow the owned selection String without copying or retaining it; the original pointer-versus-reference spelling is
// unresolved.
const ScStringView *ScGraphInfo::GetSelectionLabel() const {
  return &selectionLabel;
}

// Borrow the selected current-value String, using the third String for every selector other than month or year.
const ScStringView *ScGraphInfo::GetCurrentValueLabel(ScGraphDataSet set) const {
  u32 index = set == ScGraphDataSet::Month ? 0 : set == ScGraphDataSet::Year ? 1 : 2;
  return currentLabels + index;
}

// Borrow the original label identifier word through its const receiver; pointer-versus-reference return spelling
// remains unresolved.
const u32 *ScGraphInfo::GetSelectionLabelID() const {
  return &selectionLabelId;
}

// Borrow the original label identifier word through its const receiver; pointer-versus-reference return spelling
// remains unresolved.
const u32 *ScGraphInfo::GetValueLabelID() const {
  return &valueLabelId;
}

u32 ScGraphInfo::GetLineColor() const {
  return color;
}

u32 ScGraphInfo::GetMaxValue() const {
  return maximum;
}

u8 ScGraphInfo::GetMonthData(u32 *out) {
  *out = monthly[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetMonthDataSet(u32 *out) const {
  for (u32 i = 0; i < 12; ++i)
    out[i] = monthly[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetMonthAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 12; ++i)
    sum += monthly[i];
  return sum / 12;
}

u8 ScGraphInfo::GetYearData(u32 *out) {
  *out = yearly[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetYearDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    out[i] = yearly[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetYearAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 10; ++i)
    sum += yearly[i];
  return sum / 10;
}

u8 ScGraphInfo::GetDecadeData(u32 *out) {
  *out = decades[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetDecadeDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    out[i] = decades[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetDecadeAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 10; ++i)
    sum += decades[i];
  return sum / 10;
}

// Report whether automatic recalculation preserves the maximum, or clamp all manual histories and report any change.
u8 ScGraphInfo::VerifyData() {
  if (automaticScale) {
    u32 previous = maximum;
    ComputeAutoMax();
    return previous == maximum;
  }
  u8 unchanged = 1;
  for (u32 i = 0; i < 12; ++i)
    if (monthly[i] > maximum) {
      monthly[i] = maximum;
      unchanged = 0;
    }
  for (u32 i = 0; i < 10; ++i)
    if (yearly[i] > maximum) {
      yearly[i] = maximum;
      unchanged = 0;
    }
  for (u32 i = 0; i < 10; ++i)
    if (decades[i] > maximum) {
      decades[i] = maximum;
      unchanged = 0;
    }
  return unchanged;
}

// Copy through the original temporary String before replacing the owned selection label, retaining alias-safe lifetime
// ordering.
u8 ScGraphInfo::SetSelectionLabel(const ScStringView &value) {
  ScString copy(value);
  selectionLabel = copy;
  return 1;
}

// Reject unsupported datasets before constructing the temporary String, then replace the selected owned label.
u8 ScGraphInfo::SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) {
  u32 index = u32(set);
  if (index > 2)
    return 0;
  ScString copy(value);
  currentLabels[index] = copy;
  return 1;
}

u8 ScGraphInfo::SetSelectionLabelID(const u32 &value) {
  selectionLabelId = value;
  return 1;
}

u8 ScGraphInfo::SetValueLabelID(const u32 &value) {
  valueLabelId = value;
  return 1;
}

u8 ScGraphInfo::SetColor(u32 value) {
  color = value;
  return 1;
}

u8 ScGraphInfo::SetMaxValue(u32 value) {
  maximum = value;
  return 1;
}

u8 ScGraphInfo::SetAutoMax(u8 enabled) {
  automaticScale = enabled;
  return enabled;
}

void ScGraphInfo::SetAutoMaxFactors(u32 floor, f32 factor) {
  minimum = floor;
  scaleFactor = factor >= 1 ? factor : 1;
  ComputeAutoMax();
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetMonthData(u32 value) {
  if (automaticScale) {
    if (value > ScRoundGraphValue(f64(maximum) / scaleFactor))
      ComputeAutoMax();
  } else if (value > maximum) {
    value = maximum;
  }
  monthly.push_front(value);
  monthly.pop_back();
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetMonthDataSet(u32 *values) {
  for (u32 i = 0; i < 12; ++i)
    monthly[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetYearData(u32 value) {
  if (automaticScale)
    ComputeAutoMax();
  else if (value > maximum)
    value = maximum;
  yearly.push_front(value);
  yearly.pop_back();
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetYearDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    yearly[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetDecadeData(u32 value) {
  if (automaticScale) {
    if (value > ScRoundGraphValue(f64(maximum) / scaleFactor))
      ComputeAutoMax();
  } else if (value > maximum) {
    value = maximum;
  }
  decades.push_front(value);
  decades.pop_back();
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetDecadeDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    decades[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Read histories and maximum with short circuiting, then recalculate or virtually verify even after a failed transport.
u8 ScGraphInfo::SerialRead(CtCity *, RsStream *record) {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = record->ReadU32(&monthly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->ReadU32(&yearly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->ReadU32(&decades[i]);
  if (result)
    result = record->ReadU32(&maximum);
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return result;
}

// Write all32 history words and the current maximum in source order, retaining each live value load before its
// callback.
u8 ScGraphInfo::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = record->WriteU32(monthly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->WriteU32(yearly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->WriteU32(decades[i]);
  if (result)
    result = record->WriteU32(maximum);
  return result;
}

ScRefCount::ScRefCount() : references(0) {
}

ScRefCount::~ScRefCount() {
}

// Dispatch virtual removal first and destroy through the real lifetime interface when its returned count is zero.
u32 ScRefCount::Release() {
  u32 count = RemoveRef();
  if (!count)
    delete this;
  return count;
}

u32 ScRefCount::AddRef() {
  ScGetSimutilDirector()->AddRef();
  return ++references;
}

// Leave zero unchanged; otherwise remove the actual module hold before decrementing the live receiver count.
u32 ScRefCount::RemoveRef() {
  if (references) {
    ScGetSimutilDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScRefCount::RefCount() {
  return references;
}
} // namespace CtUtility

// Actual registered factories, using the original product identities.
ScUnknown *ScCreateSimutilWaterLayer();
ScUnknown *ScCreateSimutilPowerLayer();
ScUnknown *ScCreateSimutilCoalPlant();
ScUnknown *ScCreateSimutilOilPlant();
ScUnknown *ScCreateSimutilGasPlant();
ScUnknown *ScCreateSimutilNuclearPlant();
ScUnknown *ScCreateSimutilSolarPlant();
ScUnknown *ScCreateSimutilMicrowavePlant();
ScUnknown *ScCreateSimutilFusionPlant();
ScUnknown *ScCreateSimutilWindPlant();
ScUnknown *ScCreateSimutilWastePlant();
ScUnknown *ScCreateSimutilA2fd2580();
ScUnknown *ScCreateSimutil02fd2c92();
ScUnknown *ScCreateSimutilC2fd2c98();
ScUnknown *ScCreateSimutil62fd2c9f();

// Construct the registered water layer and publish its primary plumbing-layer identity without retaining it.
ScUnknown *ScCreateSimutilWaterLayer() {
  return static_cast<CtPlumbingLayer *>(new CtPlumbingSimulation);
}

// Construct the registered power layer and publish its adjusted power-layer identity without retaining it.
ScUnknown *ScCreateSimutilPowerLayer() {
  return static_cast<CtPowerLayer *>(new CtPowerSimulation);
}

// Construct a coal plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilCoalPlant() {
  return static_cast<BdPowerPlant *>(new BdCoalPlant);
}

// Construct an oil plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilOilPlant() {
  return static_cast<BdPowerPlant *>(new BdOilPlant);
}

// Construct a gas plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilGasPlant() {
  return static_cast<BdPowerPlant *>(new BdGasPlant);
}

// Construct a nuclear plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilNuclearPlant() {
  return static_cast<BdPowerPlant *>(new BdNuclearPlant);
}

// Construct a solar plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilSolarPlant() {
  return static_cast<BdPowerPlant *>(new BdSolarPlant);
}

// Construct a microwave plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilMicrowavePlant() {
  return static_cast<BdPowerPlant *>(new BdMicrowavePlant);
}

// Construct a fusion plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilFusionPlant() {
  return static_cast<BdPowerPlant *>(new BdFusionPlant);
}

// Construct a wind plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilWindPlant() {
  return static_cast<BdPowerPlant *>(new BdWindPlant);
}

// Construct a waste-power plant and publish its primary power-plant identity without retaining it.
ScUnknown *ScCreateSimutilWastePlant() {
  return static_cast<BdPowerPlant *>(new BdWastePowerPlant);
}

// Allocate the registered water pump and publish its adjusted plumbing interface.
// Its complete building remains responsible for the shared receiver lifetime.
ScUnknown *ScCreateSimutilA2fd2580() {
  BdWaterPump *facility = new BdWaterPump;
  return static_cast<BdPlumbingView *>(facility);
}

// Allocate the registered water tower and publish its adjusted plumbing interface.
// Its complete building remains responsible for the shared receiver lifetime.
ScUnknown *ScCreateSimutil02fd2c92() {
  BdWaterTower *facility = new BdWaterTower;
  return static_cast<BdPlumbingView *>(facility);
}

// Allocate the registered desalination plant and publish its adjusted plumbing interface.
// The complete building owns the shared receiver lifetime.
ScUnknown *ScCreateSimutilC2fd2c98() {
  BdDesalinationPlant *facility = new BdDesalinationPlant;
  return static_cast<BdPlumbingView *>(facility);
}

// Allocate the registered water-treatment building and publish its original primary building identity.
// The executable's global new handler provides the required allocation-failure behavior.
ScUnknown *ScCreateSimutil62fd2c9f() {
  BdWaterTreatment *facility = new BdWaterTreatment;
  return static_cast<BdBuilding *>(facility);
}

// Register the two utility layers and thirteen original facility factories in installed order.
// Each registered factory constructs its corresponding utility product.
ScSimutilDirector::ScSimutilDirector() {
  AddClass(kCtPlumbingSimulationClassId, ScCreateSimutilWaterLayer, 0);
  AddClass(kCtPowerSimulationClassId, ScCreateSimutilPowerLayer, 0);
  AddClass(kBdCoalPlantClassId, ScCreateSimutilCoalPlant, 0);
  AddClass(kBdOilPlantClassId, ScCreateSimutilOilPlant, 0);
  AddClass(kBdGasPlantClassId, ScCreateSimutilGasPlant, 0);
  AddClass(kBdNuclearPlantClassId, ScCreateSimutilNuclearPlant, 0);
  AddClass(kBdSolarPlantClassId, ScCreateSimutilSolarPlant, 0);
  AddClass(kBdMicrowavePlantClassId, ScCreateSimutilMicrowavePlant, 0);
  AddClass(kBdFusionPlantClassId, ScCreateSimutilFusionPlant, 0);
  AddClass(kBdWindPlantClassId, ScCreateSimutilWindPlant, 0);
  AddClass(kBdWastePowerPlantClassId, ScCreateSimutilWastePlant, 0);
  AddClass(kBdWaterPumpClassId, ScCreateSimutilA2fd2580, 0);
  AddClass(kBdWaterTowerClassId, ScCreateSimutil02fd2c92, 0);
  AddClass(kBdDesalinationPlantClassId, ScCreateSimutilC2fd2c98, 0);
  AddClass(kBdWaterTreatmentClassId, ScCreateSimutil62fd2c9f, 0);
}

ScSimutilDirector::~ScSimutilDirector() {
}

u32 ScSimutilDirector::GetDirectorID() const {
  return kScSimutilDirectorId;
}

bool ScSimutilDirector::AppRegister(ScRegistry *) {
  return true;
}

class ScSimutilRegistryRef {
public:
  // Acquire the original registry service into its escaped temporary output slot.
  ScSimutilRegistryRef() : object(nullptr) {
    ScGetSimutilFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  }

  // Release the temporary registry view after registration, including when registration unwinds.
  ~ScSimutilRegistryRef() {
    if (object)
      static_cast<ScRegistry *>(object)->Release();
  }

  void *object;
};

bool ScSimutilDirector::OnStart(ScCom *) {
  ScSimutilRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

// Return the original lazily initialized module director without adding a reference.
ScDirector *ScGetSimutilDirector() {
  static ScSimutilDirector director;
  return &director;
}

// Borrow the framework stored by the utility director.
ScFramework *ScGetSimutilFramework() {
  return ScGetSimutilDirector()->FrameWork();
}

static RsManager *ScSimutilResourceManager;

// Scope the original escaped resource-manager acquisition until its borrowed cache has been published.
class ScSimutilResourceRef {
public:
  // Acquire the original resource-manager service and retain any output supplied by the framework.
  ScSimutilResourceRef() : object(nullptr) {
    ScGetSimutilFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
  }

  // Release the temporary acquired view after its cache publication.
  ~ScSimutilResourceRef() {
    if (object)
      static_cast<ScUnknown *>(object)->Release();
  }

  void *object;
};

// Borrow the resource manager, publishing its cache before releasing the temporary service view.
RsManager *ScGetSimutilResourceManager() {
  if (!ScSimutilResourceManager) {
    ScSimutilResourceRef service;
    ScSimutilResourceManager = static_cast<RsManager *>(service.object);
  }
  return ScSimutilResourceManager;
}

static WnResources *ScSimutilWindowResources;

// Original temporary service holder for the SIMUTIL window-resource cache.
class ScSimutilWindowRef {
public:
  // Acquire the original window-resource service into the live escaped output slot.
  ScSimutilWindowRef() : object(nullptr) {
    ScGetSimutilFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
  }

  // Release the acquired temporary after the borrowed cache is published.
  ~ScSimutilWindowRef() {
    if (object)
      static_cast<ScUnknown *>(object)->Release();
  }

  void *object;
};

// Borrow cached window resources, retrying the service acquisition when the cache is null.
WnResources *ScGetSimutilWindowResources() {
  if (!ScSimutilWindowResources) {
    ScSimutilWindowRef service;
    ScSimutilWindowResources = static_cast<WnResources *>(service.object);
  }
  return ScSimutilWindowResources;
}

static ScLanguageManager *ScSimutilLanguageManager;

// Original temporary service holder for the SIMUTIL language-manager cache.
class ScSimutilLanguageRef {
public:
  // Acquire the original language-manager service into its escaped output slot.
  ScSimutilLanguageRef() : object(nullptr) {
    ScGetSimutilFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
  }

  // Release the temporary acquired view after publishing the borrowed cache.
  ~ScSimutilLanguageRef() {
    if (object)
      static_cast<ScUnknown *>(object)->Release();
  }

  void *object;
};

// Borrow the cached language manager, retrying acquisition whenever the cache remains null.
ScLanguageManager *ScGetSimutilLanguageManager() {
  if (!ScSimutilLanguageManager) {
    ScSimutilLanguageRef service;
    ScSimutilLanguageManager = static_cast<ScLanguageManager *>(service.object);
  }
  return ScSimutilLanguageManager;
}

#if (!defined(SC3_STATIC) || defined(SC3_SIMUTIL_DLL)) && !defined(SC3_MONOLITHIC)
// Publish the utility director through the DLL's original loader entry.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return ScGetSimutilDirector();
}
#endif
