#include "buildings/bd_water.h"

#include <stdio.h>
#include <string.h>

#include "advisors/ad_module.h"
#include "application/api.h"
#include "buildings/bd_power.h"
#include "buildings/bd_utility_graph.h"
#include "city/api.h"
#include "city/timing.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "geometry/utilities.h"
#include "graphics/image.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "terrain/api.h"
#include "ui/api.h"

// Water buildings return the installed plumbing geometry word and initialize unmatched modifier IDs separately.
enum BdWaterGeometry : u32 {
  kBdWaterBuildingGeometry = 0x800104,
  kCtWaterUnusedAgentModifierId = 0xffffffff,
  kCtWaterHistoryColor = 0x7e93d9,
};

// Plumbing descriptors retain packed capacity/production pairs and treatment consumption state.
enum BdWaterPersistenceKey : u32 {
  kBdPlumbingCapacityPairKey = 0x8351fdf3,
  kBdPlumbingProductionTypePairKey = 0x8351fdf4,
  kBdPlumbingWaterDistanceKey = 0x8351fdf5,
  kBdPlumbingRetiredKey = 0x8351fdf6,
  kBdTreatmentDailyCapacityKey = 0x351d624,
  kBdTreatmentConsumptionKey = 0x351d625,
  kBdTreatmentPreviousConsumptionKey = 0x351d626,
  kBdTreatmentRetiredKey = 0x351d627,
};

BdPlumbingBuilding::BdPlumbingBuilding()
    : plumbingSprite(nullptr), fullCapacity(0), capacity(0), production(0), pumpType(0), waterDistance(0),
      constructionDate(), maximumAge(0), declineAge(0), retired(0) {
}

BdPlumbingBuilding::~BdPlumbingBuilding() {
}

// Bind building attributes and recreate the separate plumbing-layer sprite on successful base initialization.
// A failed base initialization dispatches the original virtual shutdown path.
bool BdPlumbingBuilding::Init(GeAttrib *value) {
  if (!OcUtilityBuilding::Init(value)) {
    Shutdown();
    return false;
  }
  if (plumbingSprite)
    DestroySprites(&plumbingSprite);
  return CreateSprite(2, &plumbingSprite);
}

// Read the packed capacity/type WORDs and retirement byte after the shared building descriptor succeeds.
// Each attempted packed read stores its initialized temporary even when that read reports failure.
bool BdPlumbingBuilding::Init(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Init(city, info);
  u32 value;
  if (result) {
    value = 0;
    result = info->GetIntData(kBdPlumbingCapacityPairKey, &value);
    fullCapacity = u16(value);
    capacity = u16(value >> 16);
    if (result) {
      value = 0;
      result = info->GetIntData(kBdPlumbingProductionTypePairKey, &value);
      production = u16(value);
      pumpType = u16(value >> 16);
      if (result) {
        value = 0;
        result = info->GetIntData(kBdPlumbingWaterDistanceKey, &value);
        waterDistance = u16(value);
      }
    }
  }
  value = 0;
  if (result) {
    result = info->GetIntData(kBdPlumbingRetiredKey, &value);
    if (result)
      retired = value != 0;
  }
  return result != 0;
}

// Write the two packed WORD pairs, water distance and retirement byte after the shared building descriptor.
// Preserve the installed short-circuit order and do not write later properties after a refusal.
bool BdPlumbingBuilding::Save(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Save(city, info);
  if (result) {
    result = info->SetIntData(kBdPlumbingCapacityPairKey, u32(fullCapacity) | (u32(capacity) << 16));
    if (result) {
      result = info->SetIntData(kBdPlumbingProductionTypePairKey, u32(production) | (u32(pumpType) << 16));
      if (result) {
        result = info->SetIntData(kBdPlumbingWaterDistanceKey, waterDistance);
        if (result)
          result = info->SetIntData(kBdPlumbingRetiredKey, retired);
      }
    }
  }
  return result != 0;
}

void BdPlumbingBuilding::Shutdown() {
  OcUtilityBuilding::Shutdown();
  DestroySprites(&plumbingSprite);
}

// Publish the adjusted plumbing view for its own and mortality identities, retaining through the complete receiver.
// Other requests delegate to the generic building query.
bool BdPlumbingBuilding::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdPlumbingView, BdPlumbingView);
  HANDLE_QUERY_INTERFACE(IID_BdMortal, BdPlumbingView);
  return OcUtilityBuilding::QueryInterface(iid, out);
}

u32 BdPlumbingBuilding::AddRef() {
  return OcUtilityBuilding::AddRef();
}

u32 BdPlumbingBuilding::Release() {
  return OcUtilityBuilding::Release();
}

void BdPlumbingBuilding::SetMaximumAge(u32 value) {
  maximumAge = value;
}

u32 BdPlumbingBuilding::GetMaximumAge() {
  return maximumAge;
}

void BdPlumbingBuilding::SetDeclineAge(u32 value) {
  declineAge = value;
}

u32 BdPlumbingBuilding::GetDeclineAge() {
  return declineAge;
}

// Compute elapsed whole months between the stored construction date and the current city date.
u32 BdPlumbingBuilding::GetAge() {
  u32 month, day, year;
  ScGetSimutilCity()->GetDate(&month, &day, &year);
  return 12 * year - 12 * constructionDate.Year() + month - constructionDate.Month();
}

bool BdPlumbingBuilding::IsAging() {
  u32 age = GetAge();
  return GetOccupancyState() == 1 && age >= GetDeclineAge();
}

void BdPlumbingBuilding::Retire() {
  retired = 1;
}

u8 BdPlumbingBuilding::GetRetired() {
  return retired;
}

// Replace the expired plumbing building, transferring location, birthdate and mortality to the replacement.
// Remove the original occupant from both managers before insertion and post the installed packed-bounds message.
void BdPlumbingBuilding::BecomeDefunctDueToAge() {
  GeBuildingLayer *buildings = ScGetSimutilCity()->BuildingLayer();
  ScOccupantManager *surface = ScGetSimutilCity()->SurfaceOccupantManager();
  ScOccupantManager *underground = ScGetSimutilCity()->UndergroundOccupantManagerLevelOne();
  if (!buildings || !surface || !underground)
    return;
  void *selected;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  void *created;
  if (!buildings->CreateBuilding(cachedAttributes, IID_BdBuilding, &created)) {
    occupant->Release();
    return;
  }
  BdBuilding *replacement = static_cast<BdBuilding *>(created);
  replacement->SetOccupancyState(8);
  if (replacement->QueryInterface(IID_ScOccupant, &selected)) {
    ScOccupant *newOccupant = static_cast<ScOccupant *>(selected);
    newOccupant->SetLocationInCellCoords(position & 0x7ff, (position >> 11) & 0x7ff);
    newOccupant->Release();
  }
  GeDate date;
  GetBirthdate(&date);
  replacement->SetBirthdate(date);
  if (replacement->QueryInterface(IID_BdMortal, &selected)) {
    BdPlumbingView *plumbing = static_cast<BdPlumbingView *>(selected);
    plumbing->SetMaximumAge(GetMaximumAge());
    plumbing->SetDeclineAge(GetDeclineAge());
    plumbing->Retire();
    plumbing->Release();
  }
  surface->RemoveOccupant(occupant);
  underground->RemoveOccupant(occupant);
  buildings->InsertBuilding(replacement);
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  u32 first =
      (u32(bounds.minimum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 last =
      (u32(bounds.maximum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  ScMessage message(kNwMsg_RefreshNetwork, 8, first, last);
  ScGetSimutilMessageServer()->MessagePost(message, false);
  replacement->Release();
  occupant->Release();
}

// Borrow the original adjusted occupant view without adding a reference.
ScOccupant *BdPlumbingBuilding::AsISC3Occupant() {
  return OcUtilityBuilding::AsISC3Occupant();
}

// Borrow the complete primary building view without adding a reference.
BdBuilding *BdPlumbingBuilding::AsISC3Building() {
  return static_cast<OcUtilityBuilding *>(this);
}

// Borrow the original adjusted city-agent view without adding a reference.
CtAgent *BdPlumbingBuilding::AsISC3CityAgent() {
  return OcUtilityBuilding::AsISC3CityAgent();
}

void BdPlumbingBuilding::SetFullCapacity(u16 value) {
  fullCapacity = value;
}

u16 BdPlumbingBuilding::GetFullCapacity() const {
  return fullCapacity;
}

void BdPlumbingBuilding::SetCapacity(u16 value) {
  capacity = value;
}

u16 BdPlumbingBuilding::GetCapacity() const {
  return capacity;
}

void BdPlumbingBuilding::SetProduction(u16 value) {
  production = value;
}

u16 BdPlumbingBuilding::GetProduction() const {
  return production;
}

void BdPlumbingBuilding::SetPumpType(u16 value) {
  pumpType = value;
}

u16 BdPlumbingBuilding::GetPumpType() const {
  return pumpType;
}

void BdPlumbingBuilding::SetWaterDistance(u16 value) {
  waterDistance = value;
}

u16 BdPlumbingBuilding::GetWaterDistance() const {
  return waterDistance;
}

bool BdPlumbingBuilding::HasBirthdate() const {
  return true;
}

bool BdPlumbingBuilding::GetBirthdate(CtDateInterface *out) const {
  out->Set(constructionDate.serial);
  return true;
}

bool BdPlumbingBuilding::SetBirthdate(const CtDateInterface &value) {
  constructionDate.serial = value.DayNumber();
  return true;
}

u32 BdPlumbingBuilding::GetGeomFlag() {
  return kBdWaterBuildingGeometry;
}

// Return the plumbing sprite for layer2 while active, or the shared sprite for layer1 and inactive layer2.
// Unsupported layers return null without changing either sprite.
SpSprite *BdPlumbingBuilding::GetSpriteInst(u32 layer) {
  if (layer == 1)
    return sprite;
  if (layer != 2)
    return nullptr;
  if (GetOccupancyState() != 16 && GetOccupancyState() != 8)
    return plumbingSprite;
  return sprite;
}

// Add the original aging or retirement status after the shared building query options.
// Aging takes precedence over retirement, preserving the installed virtual call order.
u8 BdPlumbingBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (IsAging())
    instance = 406;
  else if (GetRetired())
    instance = 407;
  if (instance) {
    ScString text = ScUtilityQueryText(instance);
    query->SetStatusText(text);
  }
  return 1;
}

// Add the plumbing building's age, full capacity, current capacity and production rows before generic content.
// Use the live virtual getters for each callback, including the repeated current-capacity warning check.
u8 BdPlumbingBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, text, pattern, number, maximum;
  if (!query)
    return OcUtilityBuilding::QueryTile(message, query);
  ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  label = ScUtilityQueryText(kRsCityServiceAgeMaxText);
  utility->MakeNumberString(GetAge() / 12, &number);
  utility->MakeNumberString(GetMaximumAge() / 12, &maximum);
  pattern = ScUtilityQueryText(kRsCityServiceYrsYrsFormatText);
  text.Sprintf(pattern.Data(), number.Data(), maximum.Data());
  u32 age = GetAge();
  u8 warning = age >= GetMaximumAge();
  query->AddText(&label, &text, 8, warning);

  label = ScUtilityQueryText(kRsCityServiceMaximumWaterCapacityText);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetFullCapacity(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);

  label = ScUtilityQueryText(kRsCityServiceCurrentWaterCapacityText);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetCapacity(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  warning = GetCapacity() == 0;
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);

  label = ScUtilityQueryText(107);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetProduction(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
  return OcUtilityBuilding::QueryTile(message, query);
}

BdWaterPump::BdWaterPump() {
}

BdWaterPump::~BdWaterPump() {
}

BdWaterTower::BdWaterTower() {
}

BdWaterTower::~BdWaterTower() {
}

// Populate the animated building's status after its generic options, checking active occupancy before aging.
// Retired buildings use the separate retirement status even when they are not active.
u8 BdPlumbingBuildingAnimated::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (GetOccupancyState() == 1 && IsAging())
    instance = 406;
  else if (GetRetired())
    instance = 407;
  if (instance) {
    ScString text = ScUtilityQueryText(instance);
    query->SetStatusText(text);
  }
  return 1;
}

BdDesalinationPlant::BdDesalinationPlant() {
}

BdDesalinationPlant::~BdDesalinationPlant() {
}

// Add desalination count and water-distance status before the animated plumbing query rows.
// Each nonnegative tracked count is fetched a second time, preserving the original callback order.
u8 BdDesalinationPlant::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, text, pattern, number;
  if (query) {
    ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetSimutilCity()->BuildingLayer();
    u32 total = 0;
    if (buildings->CheckOccupantTracking(13871) >= 0)
      total += u32(buildings->CheckOccupantTracking(13871));
    if (buildings->CheckOccupantTracking(13873) >= 0)
      total += u32(buildings->CheckOccupantTracking(13873));
    if (buildings->CheckOccupantTracking(13010) >= 0)
      total += u32(buildings->CheckOccupantTracking(13010));
    label = ScUtilityQueryText(kRsCityServiceSurfersCloggingIntakeText);
    pattern = ScUtilityQueryText(106);
    utility->MakeNumberString(i32(total), &number);
    text.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &text, kUiQuerySecondaryDetailsCategory, 0);

    label = ScUtilityQueryText(kRsCityServiceConnectedToCoastText);
    text = ScUtilityQueryText(GetWaterDistance() ? 104 : 103);
    u8 warning = GetWaterDistance() != 0;
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
  }
  return BdPlumbingBuildingAnimated::QueryTile(message, query);
}

BdPlumbingBuildingAnimated::BdPlumbingBuildingAnimated()
    : plumbingSprite(nullptr), fullCapacity(0), capacity(0), production(0), pumpType(0), waterDistance(0),
      constructionDate(), maximumAge(0), declineAge(0), retired(0) {
}

BdPlumbingBuildingAnimated::~BdPlumbingBuildingAnimated() {
}

// Bind building attributes and recreate the separate plumbing-layer sprite on successful base initialization.
// A failed base initialization dispatches the original virtual shutdown path.
bool BdPlumbingBuildingAnimated::Init(GeAttrib *value) {
  if (!OcUtilityBuilding::Init(value)) {
    Shutdown();
    return false;
  }
  if (plumbingSprite)
    DestroySprites(&plumbingSprite);
  return CreateSprite(2, &plumbingSprite);
}

// Read the packed capacity/type WORDs and retirement byte after the shared building descriptor succeeds.
// Each attempted packed read stores its initialized temporary even when that read reports failure.
bool BdPlumbingBuildingAnimated::Init(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Init(city, info);
  u32 value;
  if (result) {
    value = 0;
    result = info->GetIntData(kBdPlumbingCapacityPairKey, &value);
    fullCapacity = u16(value);
    capacity = u16(value >> 16);
    if (result) {
      value = 0;
      result = info->GetIntData(kBdPlumbingProductionTypePairKey, &value);
      production = u16(value);
      pumpType = u16(value >> 16);
      if (result) {
        value = 0;
        result = info->GetIntData(kBdPlumbingWaterDistanceKey, &value);
        waterDistance = u16(value);
      }
    }
  }
  value = 0;
  if (result) {
    result = info->GetIntData(kBdPlumbingRetiredKey, &value);
    if (result)
      retired = value != 0;
  }
  return result != 0;
}

// Write the two packed WORD pairs, water distance and retirement byte after the shared building descriptor.
// Preserve the installed short-circuit order and do not write later properties after a refusal.
bool BdPlumbingBuildingAnimated::Save(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Save(city, info);
  if (result) {
    result = info->SetIntData(kBdPlumbingCapacityPairKey, u32(fullCapacity) | (u32(capacity) << 16));
    if (result) {
      result = info->SetIntData(kBdPlumbingProductionTypePairKey, u32(production) | (u32(pumpType) << 16));
      if (result) {
        result = info->SetIntData(kBdPlumbingWaterDistanceKey, waterDistance);
        if (result)
          result = info->SetIntData(kBdPlumbingRetiredKey, retired);
      }
    }
  }
  return result != 0;
}

void BdPlumbingBuildingAnimated::Shutdown() {
  OcUtilityBuilding::Shutdown();
  DestroySprites(&plumbingSprite);
}

// Publish the adjusted plumbing view for its own and mortality identities, retaining through the complete receiver.
// Other requests delegate to the generic building query.
bool BdPlumbingBuildingAnimated::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdPlumbingView, BdPlumbingView);
  HANDLE_QUERY_INTERFACE(IID_BdMortal, BdPlumbingView);
  return OcUtilityBuilding::QueryInterface(iid, out);
}

u32 BdPlumbingBuildingAnimated::AddRef() {
  return OcUtilityBuilding::AddRef();
}

u32 BdPlumbingBuildingAnimated::Release() {
  return OcUtilityBuilding::Release();
}

void BdPlumbingBuildingAnimated::SetMaximumAge(u32 value) {
  maximumAge = value;
}

u32 BdPlumbingBuildingAnimated::GetMaximumAge() {
  return maximumAge;
}

void BdPlumbingBuildingAnimated::SetDeclineAge(u32 value) {
  declineAge = value;
}

u32 BdPlumbingBuildingAnimated::GetDeclineAge() {
  return declineAge;
}

// Compute elapsed whole months between the stored construction date and the current city date.
u32 BdPlumbingBuildingAnimated::GetAge() {
  u32 month, day, year;
  ScGetSimutilCity()->GetDate(&month, &day, &year);
  return 12 * year - 12 * constructionDate.Year() + month - constructionDate.Month();
}

bool BdPlumbingBuildingAnimated::IsAging() {
  u32 age = GetAge();
  return GetOccupancyState() == 1 && age >= GetDeclineAge();
}

void BdPlumbingBuildingAnimated::Retire() {
  retired = 1;
}

u8 BdPlumbingBuildingAnimated::GetRetired() {
  return retired;
}

// Replace the expired plumbing building, transferring location, birthdate and mortality to the replacement.
// Remove the original occupant from both managers before insertion and post the installed packed-bounds message.
void BdPlumbingBuildingAnimated::BecomeDefunctDueToAge() {
  GeBuildingLayer *buildings = ScGetSimutilCity()->BuildingLayer();
  ScOccupantManager *surface = ScGetSimutilCity()->SurfaceOccupantManager();
  ScOccupantManager *underground = ScGetSimutilCity()->UndergroundOccupantManagerLevelOne();
  if (!buildings || !surface || !underground)
    return;
  void *selected;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  void *created;
  if (!buildings->CreateBuilding(cachedAttributes, IID_BdBuilding, &created)) {
    occupant->Release();
    return;
  }
  BdBuilding *replacement = static_cast<BdBuilding *>(created);
  replacement->SetOccupancyState(8);
  if (replacement->QueryInterface(IID_ScOccupant, &selected)) {
    ScOccupant *newOccupant = static_cast<ScOccupant *>(selected);
    newOccupant->SetLocationInCellCoords(position & 0x7ff, (position >> 11) & 0x7ff);
    newOccupant->Release();
  }
  GeDate date;
  GetBirthdate(&date);
  replacement->SetBirthdate(date);
  if (replacement->QueryInterface(IID_BdMortal, &selected)) {
    BdPlumbingView *plumbing = static_cast<BdPlumbingView *>(selected);
    plumbing->SetMaximumAge(GetMaximumAge());
    plumbing->SetDeclineAge(GetDeclineAge());
    plumbing->Retire();
    plumbing->Release();
  }
  surface->RemoveOccupant(occupant);
  underground->RemoveOccupant(occupant);
  buildings->InsertBuilding(replacement);
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  u32 first =
      (u32(bounds.minimum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 last =
      (u32(bounds.maximum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  ScMessage message(kNwMsg_RefreshNetwork, 8, first, last);
  ScGetSimutilMessageServer()->MessagePost(message, false);
  replacement->Release();
  occupant->Release();
}

// Borrow the original adjusted occupant view without adding a reference.
ScOccupant *BdPlumbingBuildingAnimated::AsISC3Occupant() {
  return OcUtilityBuilding::AsISC3Occupant();
}

// Borrow the complete primary building view without adding a reference.
BdBuilding *BdPlumbingBuildingAnimated::AsISC3Building() {
  return static_cast<OcUtilityBuilding *>(this);
}

// Borrow the original adjusted city-agent view without adding a reference.
CtAgent *BdPlumbingBuildingAnimated::AsISC3CityAgent() {
  return OcUtilityBuilding::AsISC3CityAgent();
}

void BdPlumbingBuildingAnimated::SetFullCapacity(u16 value) {
  fullCapacity = value;
}

u16 BdPlumbingBuildingAnimated::GetFullCapacity() const {
  return fullCapacity;
}

void BdPlumbingBuildingAnimated::SetCapacity(u16 value) {
  capacity = value;
}

u16 BdPlumbingBuildingAnimated::GetCapacity() const {
  return capacity;
}

void BdPlumbingBuildingAnimated::SetProduction(u16 value) {
  production = value;
}

u16 BdPlumbingBuildingAnimated::GetProduction() const {
  return production;
}

void BdPlumbingBuildingAnimated::SetPumpType(u16 value) {
  pumpType = value;
}

u16 BdPlumbingBuildingAnimated::GetPumpType() const {
  return pumpType;
}

void BdPlumbingBuildingAnimated::SetWaterDistance(u16 value) {
  waterDistance = value;
}

u16 BdPlumbingBuildingAnimated::GetWaterDistance() const {
  return waterDistance;
}

bool BdPlumbingBuildingAnimated::HasBirthdate() const {
  return true;
}

bool BdPlumbingBuildingAnimated::GetBirthdate(CtDateInterface *out) const {
  out->Set(constructionDate.serial);
  return true;
}

bool BdPlumbingBuildingAnimated::SetBirthdate(const CtDateInterface &value) {
  constructionDate.serial = value.DayNumber();
  return true;
}

u32 BdPlumbingBuildingAnimated::GetGeomFlag() {
  return kBdWaterBuildingGeometry;
}

// Return the plumbing sprite for layer2 while active, or the shared sprite for layer1 and inactive layer2.
// Unsupported layers return null without changing either sprite.
SpSprite *BdPlumbingBuildingAnimated::GetSpriteInst(u32 layer) {
  if (layer == 1)
    return sprite;
  if (layer != 2)
    return nullptr;
  if (GetOccupancyState() != 16 && GetOccupancyState() != 8)
    return plumbingSprite;
  return sprite;
}

// Add the animated plumbing building's age, full capacity, current capacity and production rows before generic content.
// Use the live virtual getters for each callback, including the repeated current-capacity warning check.
u8 BdPlumbingBuildingAnimated::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, text, pattern, number, maximum;
  if (!query)
    return OcUtilityBuilding::QueryTile(message, query);
  ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  label = ScUtilityQueryText(kRsCityServiceAgeMaxText);
  utility->MakeNumberString(GetAge() / 12, &number);
  utility->MakeNumberString(GetMaximumAge() / 12, &maximum);
  pattern = ScUtilityQueryText(kRsCityServiceYrsYrsFormatText);
  text.Sprintf(pattern.Data(), number.Data(), maximum.Data());
  u32 age = GetAge();
  u8 warning = age >= GetMaximumAge();
  query->AddText(&label, &text, 8, warning);

  label = ScUtilityQueryText(kRsCityServiceMaximumWaterCapacityText);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetFullCapacity(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);

  label = ScUtilityQueryText(kRsCityServiceCurrentWaterCapacityText);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetCapacity(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  warning = GetCapacity() == 0;
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);

  label = ScUtilityQueryText(107);
  pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
  utility->MakeNumberString(GetProduction(), &number);
  text.Sprintf(pattern.Data(), number.Data());
  query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
  return OcUtilityBuilding::QueryTile(message, query);
}

// Installed defaults are replaced by the plumbing-layer tunable loader during static initialization.
static u32 CtWaterTowerCapacity = 100, CtWaterPumpCapacity = 400, CtDesalinationCapacity = 1600;
static u32 CtWaterTowerLife = 74, CtWaterPumpLife = 49, CtDesalinationLife = 39;
static u32 CtWaterFromPipe = 3, CtWaterPumpDistanceFactor = 80, CtWaterConservationFactor = 75;
static u32 CtWaterMeterFactor = 75, CtWaterDefaultConsumption = 5, CtWaterDefaultLeak = 1;
static u32 CtWaterAgentIds[127];
static u8 CtWaterAgentModifiers[127];
static u16 CtWaterAgentModifierCount;

CtWaterPumpRecord::CtWaterPumpRecord()
    : position{INT32_MAX, INT32_MAX, INT32_MAX}, unknown12(0), fullCapacity(0), capacity(0), production(0),
      unknown22(0), pumpType(0), unknown26(0), waterDistance(0) {
}

// Compare city coordinates by X and Z, ignoring the altitude component.
bool operator==(CtCoord first, CtCoord second) {
  return first.x == second.x && first.z == second.z;
}

// Compare pump records using their city coordinates, ignoring capacity and other record fields.
bool operator==(CtWaterPumpRecord first, CtWaterPumpRecord second) {
  return first.position == second.position;
}

static i32 CtWaterBitTest(const u32 *bits, i32 width, i32 x, i32 z) {
  i32 row = i32(u32(width) * u32(z)) / 32, column = x / 32;
  return (bits[row + column] & (1u << (u32(x) & 31))) != 0;
}

static void CtWaterBitSet(u32 *bits, i32 width, i32 x, i32 z) {
  i32 row = i32(u32(width) * u32(z)) / 32, column = x / 32;
  bits[row + column] |= 1u << (u32(x) & 31);
}

static void CtWaterBitClear(u32 *bits, i32 width, i32 x, i32 z) {
  i32 row = i32(u32(width) * u32(z)) / 32, column = x / 32;
  bits[row + column] &= ~(1u << (u32(x) & 31));
}

// Clear the original square grid in descending DWORD order, using its wrapped signed size calculation.
void CtWaterBitsClear(u32 *bits, i32 width) {
  i32 count = i32(u32(width) * u32(width)) / 32;
  while (count > 0)
    bits[--count] = 0;
}

// Count set bits in the original square grid, scanning each DWORD from least to most significant bit.
// Linux Bit1_Count takes a DWORD pointer and an int; PC callers consume the full integer result.
i32 CtWaterBitsCount(u32 *bits, i32 width) {
  i32 count = i32(u32(width) * u32(width)) / 32, total = 0;
  while (count > 0) {
    u32 value = bits[--count];
    for (i32 bit = 0; value && bit < 32; ++bit) {
      total += value & 1;
      value >>= 1;
    }
  }
  return total;
}

void CtWaterBitsXor(u32 *destination, u32 *source, i32 width) {
  i32 count = i32(u32(width) * u32(width)) / 32;
  while (count > 0) {
    --count;
    destination[count] ^= source[count];
  }
}

// Copy each source DWORD into the destination in the original descending order, including overlapping ranges.
void CtWaterBitsCopy(u32 *destination, u32 *source, i32 width) {
  i32 count = i32(u32(width) * u32(width)) / 32;
  while (count > 0) {
    --count;
    destination[count] = source[count];
  }
}

// Combine each source selection DWORD into the destination in descending order.
void CtWaterBitsSelectionSet(u32 *destination, u32 *source, i32 width) {
  i32 count = i32(u32(width) * u32(width)) / 32;
  while (count > 0) {
    --count;
    destination[count] |= source[count];
  }
}

// Expand set bits horizontally over each selected row and report the changed row range.
u8 CtWaterBitsGrowHorizontal(u32 *bits, i32 width, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, end = *endRow, words = width / 32;
  *firstRow = width;
  *endRow = 0;
  bool changed = false;
  for (i32 row = first; row < end; ++row) {
    u32 *line = bits + row * words;
    u32 previous = line[0], current = line[1];
    u32 next = previous | (previous >> 1) | (previous << 1) | (current << 31);
    line[0] = next;
    bool rowChanged = next != previous;
    for (i32 word = 1; word < words - 1; ++word) {
      u32 original = current;
      current = line[word + 1];
      next = original | (original >> 1) | (original << 1) | (previous >> 31) | (current << 31);
      line[word] = next;
      rowChanged = rowChanged || next != original;
      previous = original;
    }
    next = current | (current >> 1) | (current << 1) | (previous >> 31);
    line[words - 1] = next;
    rowChanged = rowChanged || next != current;
    changed = changed || rowChanged;
    if (rowChanged) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
    }
  }
  return changed;
}

// Expand set bits vertically through the selected rows, scanning DWORD columns in original descending order.
u8 CtWaterBitsGrowVertical(u32 *bits, i32 width, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, last = *endRow - 1, words = width / 32;
  *firstRow = width;
  *endRow = 0;
  bool changed = false;
  for (i32 word = words - 1; word >= 0; --word) {
    u32 previous = bits[first * words + word], current = bits[(first + 1) * words + word];
    u32 next = previous | current;
    bits[first * words + word] = next;
    if (next != previous) {
      changed = true;
      if (first < *firstRow)
        *firstRow = first;
      if (first >= *endRow)
        *endRow = first + 1;
    }
    i32 row = first + 1;
    while (row < last) {
      u32 original = current;
      current = bits[(row + 1) * words + word];
      next = previous | original | current;
      bits[row * words + word] = next;
      if (next != original) {
        changed = true;
        if (row < *firstRow)
          *firstRow = row;
        if (row >= *endRow)
          *endRow = row + 1;
      }
      previous = original;
      ++row;
    }
    next = previous | current;
    bits[row * words + word] = next;
    if (next != current) {
      changed = true;
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
    }
  }
  return changed;
}

// Grow a square water bitmap in alternating horizontal and vertical passes until stable or the iteration limit.
// The two caller-supplied bounds are read but are not changed by this overload.
i32 CtWaterBitsGrow(u32 *bits, i32 width, i32 limit, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, end = *endRow;
  i32 verticalFirst = first > 0 ? first - 1 : first;
  i32 verticalEnd = end < width - 1 ? end + 1 : end;
  bool changed = true;
  i32 iterations = 0;
  for (; iterations < limit && changed; ++iterations) {
    u8 horizontal = CtWaterBitsGrowHorizontal(bits, width, &first, &end);
    u8 vertical = CtWaterBitsGrowVertical(bits, width, &verticalFirst, &verticalEnd);
    if (first >= verticalFirst)
      first = verticalFirst;
    if (end <= verticalEnd)
      end = verticalEnd;
    verticalFirst = first > 0 ? first - 1 : first;
    verticalEnd = end < width - 1 ? end + 1 : end;
    changed = horizontal != 0 || vertical != 0;
  }
  return iterations;
}

// Expand each selected row horizontally through the mask, preserving the original XOR-based change reporting.
u8 CtWaterBitsSelectionGrowHorizontal(u32 *bits, u32 *selection, i32 width, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, end = *endRow, words = width / 32;
  *firstRow = width;
  *endRow = 0;
  bool changed = false;
  for (i32 row = first; row < end; ++row) {
    u32 *line = bits + row * words, *mask = selection + row * words;
    u32 previous = line[0], current = line[1];
    u32 expanded = previous | (previous >> 1) | (previous << 1) | (current << 31);
    u32 delta = previous ^ (mask[0] & expanded);
    if (delta) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      changed = true;
      line[0] = previous | delta;
    }
    for (i32 word = 1; word < words - 1; ++word) {
      u32 original = current;
      current = line[word + 1];
      expanded = original | (original >> 1) | (original << 1) | (previous >> 31) | (current << 31);
      delta = original ^ (mask[word] & expanded);
      if (delta) {
        if (row < *firstRow)
          *firstRow = row;
        if (row >= *endRow)
          *endRow = row + 1;
        changed = true;
        line[word] = original | delta;
      }
      previous = original;
    }
    expanded = current | (current >> 1) | (current << 1) | (previous >> 31);
    delta = current ^ (mask[words - 1] & expanded);
    if (delta) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      changed = true;
      line[words - 1] = current | delta;
    }
  }
  return changed;
}

// Expand the selected grid vertically through the mask, scanning DWORD columns in original ascending order.
u8 CtWaterBitsSelectionGrowVertical(u32 *bits, u32 *selection, i32 width, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, last = *endRow - 1, words = width / 32;
  *firstRow = width;
  *endRow = 0;
  bool changed = false;
  for (i32 word = 0; word * 32 < width; ++word) {
    i32 offset = first * words + word;
    u32 previous = bits[offset], current = bits[offset + words];
    u32 delta = previous ^ (selection[offset] & (previous | current));
    if (delta) {
      if (first < *firstRow)
        *firstRow = first;
      if (first >= *endRow)
        *endRow = first + 1;
      changed = true;
      bits[offset] = previous | delta;
    }
    i32 row = first + 1;
    while (row < last) {
      offset = row * words + word;
      u32 original = current;
      current = bits[offset + words];
      delta = original ^ (selection[offset] & (previous | original | current));
      if (delta) {
        if (row < *firstRow)
          *firstRow = row;
        if (row >= *endRow)
          *endRow = row + 1;
        changed = true;
        bits[offset] = original | delta;
      }
      previous = original;
      ++row;
    }
    offset = row * words + word;
    delta = current ^ (selection[offset] & (previous | current));
    if (delta) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      changed = true;
      bits[offset] = current | delta;
    }
  }
  return changed;
}

// Grow the selected bitmap through its mask, publishing accumulated bounds before each original iteration.
// Stop at the iteration limit or after both directional passes report no change.
i32 CtWaterBitsSelectionGrow(u32 *bits, u32 *selection, i32 width, i32 limit, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, end = *endRow;
  i32 verticalFirst = first > 0 ? first - 1 : first;
  i32 verticalEnd = end < width - 1 ? end + 1 : end;
  bool changed = true;
  i32 iterations = 0;
  for (; iterations < limit && changed; ++iterations) {
    if (first < *firstRow)
      *firstRow = first;
    if (end > *endRow)
      *endRow = end;
    u8 horizontal = CtWaterBitsSelectionGrowHorizontal(bits, selection, width, &first, &end);
    u8 vertical = CtWaterBitsSelectionGrowVertical(bits, selection, width, &verticalFirst, &verticalEnd);
    if (first >= verticalFirst)
      first = verticalFirst;
    if (end <= verticalEnd)
      end = verticalEnd;
    verticalFirst = first > 0 ? first - 1 : first;
    verticalEnd = end < width ? end + 1 : end;
    changed = horizontal != 0 || vertical != 0;
  }
  return iterations;
}

CtPlumbingMapSprite::CtPlumbingMapSprite(CtPlumbingSimulation *source) : layer(source) {
  layer->AddRef();
  u32 rows = ScGetSimutilCity()->CellCountZ(), columns = ScGetSimutilCity()->CellCountX();
  SetId(10016);
  SetBaseSize(columns, rows);
}

CtPlumbingMapSprite::~CtPlumbingMapSprite() {
  ScRelease(&layer);
}

// Color buildings and occupied zones according to the plumbing layer's fixed-point water query.
bool CtPlumbingMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  CtCity *city = ScGetSimutilCity();
  if (!city)
    return false;
  CtZoneLayer *zones = city->ZoneLayer();
  if (!zones)
    return false;
  bool visible = false;
  ScOccupant *occupant = nullptr;
  if (city->SurfaceOccupantManager()->GetOccupantAtCell(x, z, &occupant) && occupant) {
    BdBuilding *building = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      visible = true;
      building->Release();
    }
    occupant->Release();
  } else {
    u8 zone = 0;
    zones->GetValue(x, z, &zone);
    if (zone == 0 || zone == 17)
      return false;
    visible = true;
  }
  if (!visible)
    return false;
  CtCoord position{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  return colors->GetColor(layer->IsWatered(position) ? 0 : 1, out);
}

CtPlumbingManipulatorBase::CtPlumbingManipulatorBase(CtPlumbingSimulation *source) : references(0), layer(source) {
}

// Destroy the shared manipulator without releasing its borrowed plumbing layer.
CtPlumbingManipulatorBase::~CtPlumbingManipulatorBase() = default;

CtPlumbingMonthlyManipulator::CtPlumbingMonthlyManipulator(CtPlumbingSimulation *source)
    : CtPlumbingManipulatorBase(source) {
}

CtPlumbingMonthlyManipulator::~CtPlumbingMonthlyManipulator() = default;

// Publish the Unknown or layer-manipulator identity and retain successful queries.
bool CtPlumbingManipulatorBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the manipulator count before retaining the containing module.
u32 CtPlumbingManipulatorBase::AddRef() {
  ++references;
  ScGetSimutilDirector()->AddRef();
  return references;
}

// Release the module only for a nonzero reference, then destroy the receiver when its count reaches zero.
u32 CtPlumbingManipulatorBase::Release() {
  if (references) {
    --references;
    ScGetSimutilDirector()->RemoveRef();
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool CtPlumbingMonthlyManipulator::Apply(CtAgent *) {
  layer->DoMonthlySimulation();
  return true;
}

bool CtPlumbingManipulatorBase::IsLockingCity() {
  return true;
}

bool CtPlumbingManipulatorBase::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtPlumbingManipulatorBase::DebugClassTag(ScStringView *) {
  return false;
}

bool CtPlumbingManipulatorBase::DebugTypeTag(ScStringView *) {
  return false;
}

// Query an agent's occupant view and release the temporary reference before returning the borrowed pointer.
// Linux identifies the base member and occupant IID; a separate installed PC body remains unlocated.
ScOccupant *CtPlumbingManipulatorBase::GetOccupantPtr(CtAgent *agent) {
  if (!agent)
    return nullptr;
  ScOccupant *occupant = nullptr;
  if (!agent->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant)))
    return nullptr;
  occupant->Release();
  return occupant;
}

void CtPlumbingSimulation::Lock() {
  referenceLock->Lock();
}

void CtPlumbingSimulation::Unlock() {
  referenceLock->Unlock();
}

// Zero opaque fields that the original leaves unwritten until initialization so they are never read uninitialized.
CtPlumbingSimulation::CtPlumbingSimulation()
    : mapWidth(0), initialized(0), unrecovered11(0), agingPumps(0), references(0), referenceLock(new ScLock),
      city(nullptr), surfaceOccupants(nullptr), undergroundOccupants(nullptr), ordinances(nullptr), power(nullptr),
      pollution(nullptr), terrain(nullptr), budget(nullptr), neighbors(nullptr), waterHistory(nullptr),
      differenceBits(nullptr), operationLock(), waterBits(nullptr), buildingBits(nullptr), mainBits(nullptr),
      pumps(nullptr), simulationCycle(0), conservationOrdinance(0), meterOrdinance(0), unrecovered126{},
      terrainWidth(0), terrainHeight(0), pumpPollution(0), sellerConnected(0), buyerConnected(0), unrecovered170{},
      totalCapacity{}, totalUsed{},
      firstWaterCount(0), secondWaterCount(0), unrecovered200{}, unrecovered216{} {
}

// Publish the primary plumbing layer for its supported layer and Unknown identities, retaining each success.
bool CtPlumbingSimulation::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtPlumbingLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtPlumbingLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtPlumbingLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtPlumbingLayer);
  HANDLE_QUERY_INTERFACE(IID_CtPlumbingLayer, CtPlumbingLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

CtPlumbingSimulation::~CtPlumbingSimulation() {
  delete referenceLock;
}

// Increment the layer's protected reference count before acquiring the module hold.
u32 CtPlumbingSimulation::AddRef() {
  Lock();
  ++references;
  Unlock();
  ScGetSimutilDirector()->AddRef();
  return references;
}

// Decide whether to destroy the layer while holding its reference lock, then release the module hold.
// The surviving path rereads the live count after the director callback, as in the installed body.
u32 CtPlumbingSimulation::Release() {
  Lock();
  if (references)
    --references;
  bool destroy = references == 0;
  Unlock();
  ScGetSimutilDirector()->RemoveRef();
  if (destroy) {
    delete this;
    return 0;
  }
  return references;
}

// Dispatch the original four surface and underground occupant notifications, accepting other messages unchanged.
bool CtPlumbingSimulation::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kOcMsg_PipeInserted:
    return UnderOccupantInserted(reinterpret_cast<ScOccupant *>(message->data1));
  case kOcMsg_PipeRemoved:
    return UnderOccupantRemoved(reinterpret_cast<ScOccupant *>(message->data1));
  case kOcMsg_SurfaceInserted:
    return SurfaceOccupantInserted(reinterpret_cast<ScOccupant *>(message->data1));
  case kOcMsg_SurfaceRemoved:
    return SurfaceOccupantRemoved(reinterpret_cast<ScOccupant *>(message->data1));
  default:
    return true;
  }
}

// Dispatch the layer's query request or publish its history model to the original model-receiver identity.
bool CtPlumbingSimulation::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source) != 0;
  if (message->type == kWnMsg_QueryClosed)
    return OnWindowDestroy(message, source) != 0;
  if (message->type == kWnMsg_QueryRefresh)
    return OnWindowRefresh(message, source) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      ScModelReceiver *receiver = static_cast<ScModelReceiver *>(selected);
      receiver->DumpGraphData(waterHistory);
      receiver->Release();
    }
  }
  return true;
}

// Publish the history, raster or location-specific rows for supported query submessages and acknowledge all requests.
u8 CtPlumbingSimulation::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (source && source->QueryInterface(IID_UiGraphsViewIdentity, &selected)) {
      ScObjectCollection *collection = static_cast<ScObjectCollection *>(selected);
      collection->Add(waterHistory);
      collection->Release();
    }
    break;
  case kUiQueryTagDebug:
    if (source && source->QueryInterface(IID_UiQuerySection, &selected))
      static_cast<UiQuery *>(selected)->Release();
    break;
  case kUiQueryTagMaps:
    if (source && source->QueryInterface(IID_UiRasterQuery, &selected)) {
      UiRasterQuery *query = static_cast<UiRasterQuery *>(selected);
      void *raster = nullptr;
      if (CreateMapSprite(10016, IID_GxRasterSource, &raster))
        query->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster)
        static_cast<GxRasterSource *>(raster)->Release();
      query->Release();
    }
    break;
  case kUiQueryTagStandard:
    if (source && source->QueryInterface(IID_UiQuerySection, &selected)) {
      UiQuery *query = static_cast<UiQuery *>(selected);
      DoStandardQueryViewInfo(message, query);
      query->Release();
    }
    break;
  default:
    break;
  }
  return 1;
}

u8 CtPlumbingSimulation::OnWindowDestroy(ScMessage *, ScUnknown *) {
  return 1;
}

u8 CtPlumbingSimulation::OnWindowRefresh(ScMessage *, ScUnknown *) {
  return 1;
}

// Request the query location and add each available original water information row in order.
u8 CtPlumbingSimulation::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  bool warning = false;
  if (!query || !query->GetLocation(&location))
    return 0;
  u32 count = u16(GetInfoStringCount());
  for (u32 index = 0; index < count; ++index) {
    u32 categories;
    if (GetInfoStrings(u32(location.x >> kCtMicroCoordinateShift), u32(location.z >> kCtMicroCoordinateShift), &label,
            &value, &categories, index, &warning))
      query->AddText(&label, &value, categories, u8(warning));
  }
  return 1;
}

u32 CtPlumbingSimulation::GetInfoStringCount() {
  return 2;
}

// Produce the water-service or pump-pollution query row for the selected cell.
u8 CtPlumbingSimulation::GetInfoStrings(
    u32 x, u32 z, ScStringView *label, ScStringView *value, u32 *categories, u32 index, bool *warning) {
  CtWaterPumpRecord pump;
  if (index > 6 || !ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent))
    return 0;
  *warning = false;
  if (index == 0) {
    ScString caption = ScUtilityQueryText(kRsCityServiceWateredText);
    *categories = 1024;
    label->Copy(caption);
    u8 watered = IsWatered(x, z);
    *warning = watered == 0;
    ScString description = ScUtilityQueryText(watered ? 5 : 6);
    value->Copy(description);
    return 1;
  }
  if (index != 1 || !FindPump(u16(x), u16(z), &pump))
    return 0;
  *warning = false;
  ScString caption = ScUtilityQueryText(kRsCityServiceEffectOfWaterPollutionText);
  label->Copy(caption);
  u32 waterPollution;
  pollution->GetWaterValue(x, z, &waterPollution);
  u32 descriptionId = 97;
  if (waterPollution > 1) {
    u32 maximum = u32(pollution->GetMaxExpectedWaterValue());
    if (!maximum)
      maximum = 1;
    u16 rank = u16((u32(waterPollution * 4) - 1) / maximum);
    if (rank == 0) {
      descriptionId = 98;
    } else if (rank == 1) {
      descriptionId = 99;
    } else {
      descriptionId = rank == 2 ? 100 : 101;
      *warning = true;
    }
  }
  ScString description = ScUtilityQueryText(descriptionId);
  value->Copy(description);
  *categories = 0x20000;
  return 1;
}

// Store a parsed agent modifier if its decimal value is valid and the original 127-entry table has room.
// The INI callback's context is unused; its source return declaration is the external void callback contract.
void CtPlumbingSimulation::RegisterAgentModifier(const ScString &key, const ScString &value, void *) {
  u32 id = GeParseNumber(key.ToChar());
  int modifier;
  if (sscanf(value.ToChar(), "%d", &modifier) == 1 && CtWaterAgentModifierCount < 127) {
    u16 index = CtWaterAgentModifierCount++;
    CtWaterAgentIds[index] = id;
    CtWaterAgentModifiers[index] = u8(modifier);
  }
}

// Load water-layer modifiers and tuning from the installed INI and compressed resource path.
bool CtPlumbingSimulation::StaticInit(CtDefinition *) {
  for (u32 &id : CtWaterAgentIds)
    id = kCtWaterUnusedAgentModifierId;
  GeIni ini;
  ScString path, value;
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SC3WaterLayer.INI", 22);
  ini.SetPath(path);
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  ini.EnumEntries(ScString("AgentIDModifiers"), RegisterAgentModifier, this);
  ScString section("TuningParameters");
  if (ini.ReadEntry(section, ScString("WaterFromPipe"), &value)) {
    CtWaterFromPipe = u16(GeParseNumber(value.ToChar()));
    if (!CtWaterFromPipe)
      CtWaterFromPipe = 3;
  }
  if (ini.ReadEntry(section, ScString("WaterPumpDistanceFactor"), &value))
    CtWaterPumpDistanceFactor = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterTowerCapa"), &value))
    CtWaterTowerCapacity = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterPumpCapa"), &value))
    CtWaterPumpCapacity = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("DesaltPlantCapa"), &value))
    CtDesalinationCapacity = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterConservationFactor"), &value))
    CtWaterConservationFactor = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterMeterFactor"), &value))
    CtWaterMeterFactor = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("DefaultConsumption"), &value))
    CtWaterDefaultConsumption = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("DefaultLeak"), &value))
    CtWaterDefaultLeak = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterTowerLife"), &value))
    CtWaterTowerLife = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("WaterPumpLife"), &value))
    CtWaterPumpLife = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("DesaltPlantLife"), &value))
    CtDesalinationLife = GeParseNumber(value.ToChar());
  for (i32 end = i32(CtWaterAgentModifierCount) - 1; end >= 1; --end) {
    for (i32 i = 1; i <= end; ++i) {
      if (CtWaterAgentIds[i - 1] > CtWaterAgentIds[i]) {
        u32 id = CtWaterAgentIds[i - 1];
        u8 modifier = CtWaterAgentModifiers[i - 1];
        CtWaterAgentIds[i - 1] = CtWaterAgentIds[i];
        CtWaterAgentModifiers[i - 1] = CtWaterAgentModifiers[i];
        CtWaterAgentIds[i] = id;
        CtWaterAgentModifiers[i] = modifier;
      }
    }
  }
  return true;
}

// Forward an underground insertion only for an occupant carrying the original water-main geometry flag.
bool CtPlumbingSimulation::UnderOccupantInserted(ScOccupant *occupant) {
  if (occupant->IsGeomFlag(kOcGeomPipe))
    WaterMainInserted(occupant);
  return true;
}

// Forward an underground removal only for an occupant carrying the original water-main geometry flag.
bool CtPlumbingSimulation::UnderOccupantRemoved(ScOccupant *occupant) {
  if (occupant->IsGeomFlag(kOcGeomPipe))
    WaterMainRemoved(occupant);
  return true;
}

// Classify surface building insertions by pump, desalination plant, tower or ordinary building type.
bool CtPlumbingSimulation::SurfaceOccupantInserted(ScOccupant *occupant) {
  if (occupant->MajorType() == 1) {
    switch (occupant->TypeId()) {
    case 12810:
      PumpInserted(occupant);
      break;
    case 12830:
      DesalinationInserted(occupant);
      break;
    case 12840:
      TowerInserted(occupant);
      break;
    default:
      BuildingInserted(occupant);
      break;
    }
  }
  return true;
}

// Classify surface building removals in the same original facility-type groups as insertions.
bool CtPlumbingSimulation::SurfaceOccupantRemoved(ScOccupant *occupant) {
  if (occupant->MajorType() == 1) {
    switch (occupant->TypeId()) {
    case 12810:
      PumpRemoved(occupant);
      break;
    case 12830:
      DesalinationRemoved(occupant);
      break;
    case 12840:
      TowerRemoved(occupant);
      break;
    default:
      BuildingRemoved(occupant);
      break;
    }
  }
  return true;
}

// Mark every cell in the inserted water main's bounds after its original location and rectangle callbacks.
void CtPlumbingSimulation::WaterMainInserted(ScOccupant *occupant) {
  u32 x, z;
  GeCellRect bounds;
  occupant->GetLocationInCellCoords(&x, &z);
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  for (u16 column = u16(bounds.left); column <= bounds.right; ++column)
    for (u16 row = u16(bounds.top); row <= bounds.bottom; ++row)
      CtWaterBitSet(mainBits, mapWidth, column, row);
  Unlock();
}

// Clear every cell in the removed water main's bounds after its original location and rectangle callbacks.
void CtPlumbingSimulation::WaterMainRemoved(ScOccupant *occupant) {
  u32 x, z;
  GeCellRect bounds;
  occupant->GetLocationInCellCoords(&x, &z);
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  for (u16 column = u16(bounds.left); column <= bounds.right; ++column)
    for (u16 row = u16(bounds.top); row <= bounds.bottom; ++row)
      CtWaterBitClear(mainBits, mapWidth, column, row);
  Unlock();
}

void CtPlumbingSimulation::BuildingInserted(ScOccupant *occupant) {
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  u16 x = u16((bounds.left + bounds.right) >> 1), z = u16((bounds.top + bounds.bottom) >> 1);
  Lock();
  CtWaterBitSet(buildingBits, mapWidth, x, z);
  Unlock();
}

void CtPlumbingSimulation::BuildingRemoved(ScOccupant *occupant) {
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  u16 x = u16((bounds.left + bounds.right) >> 1), z = u16((bounds.top + bounds.bottom) >> 1);
  Lock();
  CtWaterBitClear(buildingBits, mapWidth, x, z);
  Unlock();
}

// Remove pump records matching the first pump in the occupant's cell, then clear its occupied main-grid rectangle.
void CtPlumbingSimulation::PumpRemoved(ScOccupant *occupant) {
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeCellRect bounds;
  occupant->GetLocation(&position);
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  for (auto entry = pumps->begin(); entry != pumps->end(); ++entry) {
    if (((u32(position.x) ^ u32(entry->position.x)) & kCtMicroCellMask) == 0 &&
        ((u32(position.z) ^ u32(entry->position.z)) & kCtMicroCellMask) == 0) {
      i32 x = entry->position.x, z = entry->position.z;
      for (auto item = pumps->begin(); item != pumps->end();)
        if (item->position.x == x && item->position.z == z)
          item = pumps->erase(item);
        else
          ++item;
      break;
    }
  }
  for (u16 x = u16(bounds.left); x <= bounds.right; ++x)
    for (u16 z = u16(bounds.top); z <= bounds.bottom; ++z)
      CtWaterBitClear(mainBits, mapWidth, x, z);
  Unlock();
}

// Remove matching desalination records before reading and clearing the occupant's rectangle while locked.
void CtPlumbingSimulation::DesalinationRemoved(ScOccupant *occupant) {
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeCellRect bounds;
  occupant->GetLocation(&position);
  Lock();
  for (auto entry = pumps->begin(); entry != pumps->end(); ++entry) {
    if (((u32(position.x) ^ u32(entry->position.x)) & kCtMicroCellMask) == 0 &&
        ((u32(position.z) ^ u32(entry->position.z)) & kCtMicroCellMask) == 0) {
      i32 x = entry->position.x, z = entry->position.z;
      for (auto item = pumps->begin(); item != pumps->end();)
        if (item->position.x == x && item->position.z == z)
          item = pumps->erase(item);
        else
          ++item;
      break;
    }
  }
  occupant->GetBoundingRectInCellCoords(&bounds);
  for (u16 x = u16(bounds.left); x <= bounds.right; ++x)
    for (u16 z = u16(bounds.top); z <= bounds.bottom; ++z)
      CtWaterBitClear(mainBits, mapWidth, x, z);
  Unlock();
}

// Remove matching tower records after collecting the occupant's location and bounds, then clear the rectangle.
void CtPlumbingSimulation::TowerRemoved(ScOccupant *occupant) {
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeCellRect bounds;
  occupant->GetLocation(&position);
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  for (auto entry = pumps->begin(); entry != pumps->end(); ++entry) {
    if (((u32(position.x) ^ u32(entry->position.x)) & kCtMicroCellMask) == 0 &&
        ((u32(position.z) ^ u32(entry->position.z)) & kCtMicroCellMask) == 0) {
      i32 x = entry->position.x, z = entry->position.z;
      for (auto item = pumps->begin(); item != pumps->end();)
        if (item->position.x == x && item->position.z == z)
          item = pumps->erase(item);
        else
          ++item;
      break;
    }
  }
  for (u16 x = u16(bounds.left); x <= bounds.right; ++x)
    for (u16 z = u16(bounds.top); z <= bounds.bottom; ++z)
      CtWaterBitClear(mainBits, mapWidth, x, z);
  Unlock();
}

// Initialize an inserted pump's plumbing view, retain its original record, and mark its occupied rectangle.
void CtPlumbingSimulation::PumpInserted(ScOccupant *occupant) {
  CtWaterPumpRecord record;
  GeCellRect bounds;
  occupant->GetLocation(&record.position);
  i32 x = record.position.x >> kCtMicroCoordinateShift, z = record.position.z >> kCtMicroCoordinateShift;
  if (IsThereWater(i16(x - 1), i16(z - 1), i16(x + 1), i16(z + 1), 0))
    record.waterDistance = 0;
  else
    record.waterDistance = IsThereWater(i16(x - 2), i16(z - 2), i16(x + 2), i16(z + 2), 0) ? 1 : 3;
  BdPlumbingView *plumbing = nullptr;
  if (occupant->QueryInterface(IID_BdPlumbingView, reinterpret_cast<void **>(&plumbing))) {
    plumbing->SetPumpType(3);
    plumbing->SetFullCapacity(u16(GetPotentialCapacity(3)));
    plumbing->SetWaterDistance(record.waterDistance);
    plumbing->SetMaximumAge(12 * CtWaterPumpLife);
    plumbing->SetDeclineAge(6 * CtWaterPumpLife);
    record.capacity = u8(plumbing->GetCapacity());
    record.production = u8(plumbing->GetProduction());
    plumbing->Release();
  }
  record.pumpType = 3;
  record.fullCapacity = u16(GetPotentialCapacity(3));
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  pumps->push_back(record);
  for (u16 column = u16(bounds.left); column <= bounds.right; ++column)
    for (u16 row = u16(bounds.top); row <= bounds.bottom; ++row)
      CtWaterBitSet(mainBits, mapWidth, column, row);
  Unlock();
}

// Initialize a water tower's plumbing view, append its original record, and mark its occupied rectangle.
void CtPlumbingSimulation::TowerInserted(ScOccupant *occupant) {
  CtWaterPumpRecord record;
  GeCellRect bounds;
  occupant->GetLocation(&record.position);
  BdPlumbingView *plumbing = nullptr;
  if (occupant->QueryInterface(IID_BdPlumbingView, reinterpret_cast<void **>(&plumbing))) {
    plumbing->SetPumpType(2);
    plumbing->SetFullCapacity(u16(GetPotentialCapacity(2)));
    plumbing->SetMaximumAge(12 * CtWaterTowerLife);
    plumbing->SetDeclineAge(6 * CtWaterTowerLife);
    record.capacity = u8(plumbing->GetCapacity());
    record.production = u8(plumbing->GetProduction());
    plumbing->Release();
  }
  occupant->GetBoundingRectInCellCoords(&bounds);
  record.pumpType = 2;
  record.fullCapacity = u16(GetPotentialCapacity(2));
  Lock();
  pumps->push_back(record);
  for (u16 column = u16(bounds.left); column <= bounds.right; ++column)
    for (u16 row = u16(bounds.top); row <= bounds.bottom; ++row)
      CtWaterBitSet(mainBits, mapWidth, column, row);
  Unlock();
}

// Initialize a desalination plant's plumbing view, append its record, and mark its occupied rectangle.
void CtPlumbingSimulation::DesalinationInserted(ScOccupant *occupant) {
  CtWaterPumpRecord record;
  GeCellRect bounds;
  occupant->GetLocation(&record.position);
  i32 x = record.position.x >> kCtMicroCoordinateShift, z = record.position.z >> kCtMicroCoordinateShift;
  record.waterDistance = IsThereWater(i16(x - 1), i16(z - 1), i16(x + 3), i16(z + 3), 1) ? 0 : 3;
  BdPlumbingView *plumbing = nullptr;
  if (occupant->QueryInterface(IID_BdPlumbingView, reinterpret_cast<void **>(&plumbing))) {
    plumbing->SetPumpType(5);
    plumbing->SetFullCapacity(u16(GetPotentialCapacity(5)));
    plumbing->SetWaterDistance(record.waterDistance);
    plumbing->SetMaximumAge(12 * CtDesalinationLife);
    plumbing->SetDeclineAge(6 * CtDesalinationLife);
    record.capacity = u8(plumbing->GetCapacity());
    record.production = u8(plumbing->GetProduction());
    plumbing->Release();
  }
  record.pumpType = 5;
  record.fullCapacity = u16(GetPotentialCapacity(5));
  occupant->GetBoundingRectInCellCoords(&bounds);
  Lock();
  pumps->push_back(record);
  for (u16 column = u16(bounds.left); column <= bounds.right; ++column)
    for (u16 row = u16(bounds.top); row <= bounds.bottom; ++row)
      CtWaterBitSet(mainBits, mapWidth, column, row);
  Unlock();
}

bool CtPlumbingSimulation::StaticShutdown() {
  return true;
}

// Acquire the city's water services and initialize its bit grids, pump list and history graph in original order.
bool CtPlumbingSimulation::Init(CtCity *input) {
  if (initialized)
    Shutdown();
  if (!input) {
    Shutdown();
    return false;
  }
  city = ScAddRefNotNull(input);
  u32 width = city->CellCountX();
  city->CellCountZ();
  mapWidth = u16(width);
  u32 words = u32(mapWidth) * mapWidth / 32;
  waterBits = new u32[words];
  mainBits = new u32[words];
  buildingBits = new u32[words];
  differenceBits = new u32[words];
  CtWaterBitsClear(waterBits, mapWidth);
  CtWaterBitsClear(mainBits, mapWidth);
  CtWaterBitsClear(buildingBits, mapWidth);
  pumps = new std::list<CtWaterPumpRecord>;
  simulationCycle = 0;
  surfaceOccupants = city->SurfaceOccupantManager();
  if (!surfaceOccupants) {
    Shutdown();
    return false;
  }
  surfaceOccupants->AddRef();
  undergroundOccupants = city->UndergroundOccupantManagerLevelOne();
  if (!undergroundOccupants) {
    Shutdown();
    return false;
  }
  undergroundOccupants->AddRef();
  CtLayer *layer = city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer || !layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances))) {
    Shutdown();
    return false;
  }
  power = city->PowerLayer();
  if (!power) {
    Shutdown();
    return false;
  }
  power->AddRef();
  pollution = city->PollutionLayer();
  if (!pollution) {
    Shutdown();
    return false;
  }
  pollution->AddRef();
  terrain = city->DirtBag();
  if (!terrain) {
    Shutdown();
    return false;
  }
  terrain->AddRef();
  budget = city->BudgetLayer();
  if (!budget) {
    Shutdown();
    return false;
  }
  budget->AddRef();
  neighbors = city->NeighborsLayer();
  if (!neighbors) {
    Shutdown();
    return false;
  }
  neighbors->AddRef();
  initialized = 1;
  terrainWidth = u16(terrain->CellCountX());
  terrainHeight = u16(terrain->CellCountZ());
  waterHistory = ScAddRefNotNull(new CtUtility::ScGraphInfo(0));
  waterHistory->SetMaxValue(100);
  for (i32 index = -1; index < 3; ++index) {
    RsUtilityText resource(kRsGameWaterText, kRsGameTextGroup);
    ScString text = resource.GetText(), copy(text);
    if (index < 0)
      waterHistory->SetSelectionLabel(copy);
    else
      waterHistory->SetCurrentValueLabel(ScGraphDataSet(index), copy);
  }
  u32 selectionKey = kCtWaterGraphSelectionId, valueKey = kCtWaterGraphValueId;
  waterHistory->SetSelectionLabelID(selectionKey);
  waterHistory->SetValueLabelID(valueKey);
  waterHistory->SetColor(kCtWaterHistoryColor);
  pumpPollution = 0;
  agingPumps = 0;
  return true;
}

bool CtPlumbingSimulation::Init(CtCity *, ScImporter *) {
  return true;
}

// Restore the water grid, history and accounting record in original short-circuit order.
bool CtPlumbingSimulation::Init(CtCity *input, RsDatabaseSegment *segment) {
  ScArchiveFrame frame(input, segment, {kRsCityLayerRecordType, kCtPlumbingSimulationLayerType, 0});
  bool result = frame.valid != 0;
  u32 first = 0, count = 0;
  if (result) {
    RsStream *stream = frame.GetStream();
    result = stream->ReadU32(&first) && stream->ReadU32(&count);
    if (result && count)
      result = stream->ReadU32Array(waterBits + first, count);
    result = result && waterHistory->SerialRead(input, stream) && stream->ReadU32Array(totalCapacity, 2) &&
        stream->ReadU32Array(totalUsed, 2) && stream->ReadU32Array(unrecovered216, 4) &&
        stream->ReadU32Array(unrecovered200, 4) && stream->ReadU32(&secondWaterCount) &&
        stream->ReadU32(&firstWaterCount);
    if (result && frame.version >= 4)
      result = stream->ReadU32(&pumpPollution) && stream->ReadU32(&agingPumps);
  }
  return result;
}

// Save the nonzero span of the water grid followed by history and accounting data as version four.
bool CtPlumbingSimulation::Save(CtCity *input, RsDatabaseSegment *segment) {
  u32 words = u32(mapWidth) * u32(mapWidth) / 32, first = 0, count = 0;
  while (first < words && !waterBits[first])
    ++first;
  if (first == words) {
    first = 0;
  } else {
    u32 last = words - 1;
    while (last && !waterBits[last])
      --last;
    count = 1 - first + last;
  }
  ScArchiveFrame frame(input, segment, {kRsCityLayerRecordType, kCtPlumbingSimulationLayerType, 0}, 4, 0);
  bool result = frame.valid != 0;
  RsStream *stream = frame.GetStream();
  result = result &&
      stream->WriteU32(first) && stream->WriteU32(count) && stream->WriteU32Array(waterBits + first, count) &&
      waterHistory->SerialWrite(input, stream) && stream->WriteU32Array(totalCapacity, 2) &&
      stream->WriteU32Array(totalUsed, 2) && stream->WriteU32Array(unrecovered216, 4) &&
      stream->WriteU32Array(unrecovered200, 4) && stream->WriteU32(secondWaterCount) &&
      stream->WriteU32(firstWaterCount) && stream->WriteU32(pumpPollution) && stream->WriteU32(agingPumps);
  return result;
}

bool CtPlumbingSimulation::SimulationBegin() {
  return RegisterCityServices();
}

bool CtPlumbingSimulation::RegisterCityServices() {
  ScMessageServer *server = ScGetSimutilMessageServer();
  if (server) {
    server->AddNotification(this, kOcMsg_PipeInserted);
    server->AddNotification(this, kOcMsg_PipeRemoved);
    server->AddNotification(this, kOcMsg_SurfaceInserted);
    server->AddNotification(this, kOcMsg_SurfaceRemoved);
  }
  surfaceOccupants->PostOccupantAll(static_cast<ScMessageTarget *>(this));
  undergroundOccupants->PostOccupantAll(static_cast<ScMessageTarget *>(this));
  return true;
}

// Remove the four registered occupant notifications in their original order when the message server exists.
void CtPlumbingSimulation::UnregisterCityServices() {
  ScMessageServer *server = ScGetSimutilMessageServer();
  if (server) {
    server->RemoveNotification(this, kOcMsg_PipeInserted);
    server->RemoveNotification(this, kOcMsg_PipeRemoved);
    server->RemoveNotification(this, kOcMsg_SurfaceInserted);
    server->RemoveNotification(this, kOcMsg_SurfaceRemoved);
  }
}

bool CtPlumbingSimulation::SimulationEnd() {
  return true;
}

// Remove notifications, retire the pump list and grids, then release the retained city services in installed order.
bool CtPlumbingSimulation::Shutdown() {
  UnregisterCityServices();
  if (pumps) {
    pumps->clear();
    delete pumps;
    pumps = nullptr;
  }
  if (differenceBits) {
    delete[] differenceBits;
    differenceBits = nullptr;
  }
  if (waterBits) {
    delete[] waterBits;
    waterBits = nullptr;
  }
  if (mainBits) {
    delete[] mainBits;
    mainBits = nullptr;
  }
  if (buildingBits) {
    delete[] buildingBits;
    buildingBits = nullptr;
  }
  ScRelease(&city);
  ScRelease(&surfaceOccupants);
  ScRelease(&undergroundOccupants);
  ScRelease(&ordinances);
  ScRelease(&budget);
  ScRelease(&terrain);
  ScRelease(&neighbors);
  ScRelease(&power);
  ScRelease(&pollution);
  ScRelease(&waterHistory);
  initialized = 0;
  return true;
}

u32 CtPlumbingSimulation::GetLayerType() {
  return kCtPlumbingSimulationLayerType;
}

// Create a zero-reference monthly manipulator only for the daily agent type and publish its original flag.
bool CtPlumbingSimulation::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    *flags = 4;
    *out = new CtPlumbingMonthlyManipulator(this);
  }
  return true;
}

bool CtPlumbingSimulation::DebugClassTag(ScStringView *) {
  return false;
}

bool CtPlumbingSimulation::DebugTypeTag(ScStringView *) {
  return false;
}

u8 CtPlumbingSimulation::IsWatered(const CtCoord &position) const {
  return IsWatered(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift));
}

// Test the row-major packed water grid, or return zero if the grid is not initialized.
u8 CtPlumbingSimulation::IsWatered(u32 x, u32 z) const {
  if (!waterBits)
    return 0;
  return u8(CtWaterBitTest(waterBits, mapWidth, i32(x), i32(z)));
}

// Copy the current monthly graph value and stored capacity and usage to the caller's three outputs.
void CtPlumbingSimulation::GetWaterData(u32 *consumers, i64 *capacity, i64 *used) {
  *consumers = 0;
  waterHistory->GetMonthData(consumers);
  memcpy(capacity, totalCapacity, sizeof(*capacity));
  memcpy(used, totalUsed, sizeof(*used));
}

// Return the same history and water totals followed by the stored unwatered and total-consumer counts.
void CtPlumbingSimulation::GetWaterData(u32 *consumers, i64 *capacity, i64 *used, u32 *first, u32 *second) {
  GetWaterData(consumers, capacity, used);
  *second = secondWaterCount;
  *first = firstWaterCount;
}

// Return the configured production capacity for the three original facility type identifiers.
u32 CtPlumbingSimulation::GetProductionCapacity(u32 type) {
  switch (type) {
  case 12810:
    return CtWaterPumpCapacity;
  case 12830:
    return CtDesalinationCapacity;
  case 12840:
    return CtWaterTowerCapacity;
  default:
    return 0;
  }
}

u32 CtPlumbingSimulation::GetPotentialCapacity(u16 pumpType) {
  switch (pumpType) {
  case 2:
    return CtWaterTowerCapacity;
  case 3:
    return CtWaterPumpCapacity;
  case 5:
    return CtDesalinationCapacity;
  default:
    return 0;
  }
}

// Apply each stored pump record's capacity, production and water distance to its current surface occupant.
void CtPlumbingSimulation::SaveCapacityForAllPumps() {
  for (const CtWaterPumpRecord &record : *pumps) {
    ScOccupant *occupant = nullptr;
    if (surfaceOccupants->GetOccupant(record.position, &occupant)) {
      BdPlumbingView *plumbing = nullptr;
      if (occupant->QueryInterface(IID_BdPlumbingView, reinterpret_cast<void **>(&plumbing))) {
        plumbing->SetCapacity(record.capacity);
        plumbing->SetProduction(record.production);
        plumbing->SetWaterDistance(record.waterDistance);
        plumbing->Release();
      }
      occupant->Release();
    }
  }
}

// Recalculate every pump's capacity using its live plumbing view, power, nearby water, pollution and aging.
void CtPlumbingSimulation::CalculateCapacityForAllPumps() {
  bool powered = false, inactive = false, aging = false;
  u32 age = 0, maximumAge = 0, declineAge = 0, pollutedCount = 0;
  u64 pollutionSum = 0;
  agingPumps = 0;
  for (CtWaterPumpRecord &record : *pumps) {
    i32 x = record.position.x >> kCtMicroCoordinateShift, z = record.position.z >> kCtMicroCoordinateShift;
    ScOccupant *occupant = nullptr;
    if (surfaceOccupants->GetOccupant(record.position, &occupant)) {
      BdPlumbingView *plumbing = nullptr;
      if (occupant->QueryInterface(IID_BdPlumbingView, reinterpret_cast<void **>(&plumbing))) {
        record.capacity = plumbing->GetCapacity();
        record.production = plumbing->GetProduction();
        record.pumpType = plumbing->GetPumpType();
        record.fullCapacity = plumbing->GetFullCapacity();
        record.waterDistance = plumbing->GetWaterDistance();
        inactive = plumbing->GetRetired() || plumbing->AsISC3Building()->GetOccupancyState() != 1;
        aging = plumbing->IsAging();
        age = plumbing->GetAge();
        maximumAge = plumbing->GetMaximumAge();
        declineAge = plumbing->GetDeclineAge();
        if (!inactive && age >= maximumAge) {
          plumbing->BecomeDefunctDueToAge();
          inactive = true;
        }
        plumbing->Release();
      }
      occupant->Release();
    }
    u32 capacity = record.fullCapacity;
    power->GetValue(u16(x), u16(z), &powered);
    if (powered && !inactive) {
      if (record.pumpType == 3) {
        if (IsThereWater(i16(x - 1), i16(z - 1), i16(x + 1), i16(z + 1), 0)) {
          record.waterDistance = 0;
        } else if (IsThereWater(i16(x - 2), i16(z - 2), i16(x + 2), i16(z + 2), 0)) {
          capacity = capacity * CtWaterPumpDistanceFactor / 100;
          record.waterDistance = 1;
        } else {
          capacity = 0;
          record.waterDistance = 3;
        }
      }
      if (record.pumpType == 5) {
        if (IsThereWater(i16(x - 1), i16(z - 1), i16(x + 3), i16(z + 3), 1)) {
          record.waterDistance = 0;
        } else {
          capacity = 0;
          record.waterDistance = 3;
        }
      }
      u32 waterPollution = 0;
      if (pollution) {
        pollution->GetWaterValue(u16(x), u16(z), &waterPollution);
        pollutionSum += waterPollution;
        ++pollutedCount;
      }
      u32 maxPollution = u32(pollution->GetMaxExpectedWaterValue());
      if (maxPollution) {
        if (maxPollution > waterPollution)
          capacity = capacity * (maxPollution - waterPollution) / maxPollution;
        else
          capacity = 0;
      }
      if (aging) {
        capacity -= capacity * (age - declineAge) / (maximumAge - declineAge);
        ++agingPumps;
      }
      if (capacity) {
        record.capacity = u16(capacity);
      } else {
        record.capacity = 0;
        record.unknown12 = simulationCycle;
      }
    } else {
      record.capacity = 0;
      record.unknown12 = simulationCycle;
    }
    if (!record.capacity)
      record.production = 0;
  }
  pumpPollution = pollutedCount ? u32(pollutionSum / pollutedCount) : 0;
}

// Increase purchased water toward the deal amount using only the supply still available to the caller.
void CtPlumbingSimulation::UpdateBuyerDeal(CtDealRecord deal, i64 *available) {
  i64 purchased;
  memcpy(&purchased, unrecovered216, sizeof(purchased));
  i64 target = i32(deal.value_08);
  if (purchased < target && *available > 0) {
    i64 difference = i64(u64(target) - u64(purchased));
    i64 consumed = difference < *available ? difference : *available;
    *available = i64(u64(*available) - u64(consumed));
    memcpy(&purchased, unrecovered216, sizeof(purchased));
    purchased = i64(u64(purchased) + u64(consumed));
    memcpy(unrecovered216, &purchased, sizeof(purchased));
  }
}

// Reserve enough water for a seller deal and publish the remaining amount through the original in/out pointers.
void CtPlumbingSimulation::UpdateSellerDeal(CtDealRecord, i64 *available, i64 *required) {
  i64 limit = i64(0x1fffffffffffffffull - u64(*available));
  if (limit > *required) {
    i64 sold;
    memcpy(&sold, unrecovered200, sizeof(sold));
    sold = i64(u64(sold) + u64(limit) - u64(*required));
    memcpy(unrecovered200, &sold, sizeof(sold));
    *required = limit;
  }
  *available = i64(u64(*required) - u64(limit));
}

// Finalize the sold quantity and payment, then publish the low DWORD through the original neighbor deal operation.
void CtPlumbingSimulation::FinalizeSellerDeal(CtDealRecord deal) {
  i64 sold;
  memcpy(&sold, unrecovered200, sizeof(sold));
  i64 target = i32(deal.value_08);
  if (sold < target)
    sold = target;
  memcpy(unrecovered200, &sold, sizeof(sold));
  i64 charge = i64(u64(sold) * u64(i64(i32(deal.value_0c)))) / 1000;
  memcpy(unrecovered200 + 2, &charge, sizeof(charge));
  neighbors->PublishDeal(kCtDealImportWater, i32(u32(sold)));
}

void CtPlumbingSimulation::FinalizeBuyerDeal(CtDealRecord deal) {
  i64 purchased;
  memcpy(&purchased, unrecovered216, sizeof(purchased));
  i64 charge = i64(u64(purchased) * u64(i64(i32(deal.value_0c)))) / 1000;
  memcpy(unrecovered216 + 2, &charge, sizeof(charge));
  if (purchased < i32(deal.value_08))
    neighbors->CancelDeal(kCtDealExportWater);
}

// Copy the low words of the accumulated water quantity and charge into the requested deal record.
void CtPlumbingSimulation::GetWaterDeal(CtDealType type, CtDealRecord *deal) {
  deal->value_08 = 0;
  deal->value_0c = 0;
  if (type == static_cast<CtDealType>(2)) {
    deal->value_08 = unrecovered216[0];
    deal->value_0c = unrecovered216[2];
  } else if (type == static_cast<CtDealType>(3)) {
    deal->value_08 = unrecovered200[0];
    deal->value_0c = unrecovered200[2];
  }
}

// Simulate monthly water regions, pump production, neighbor deals and history before posting the water update.
void CtPlumbingSimulation::DoMonthlySimulation() {
  conservationOrdinance = ordinances->QueryOrdinanceOn(kCtOrdinance_WaterConservation);
  meterOrdinance = ordinances->QueryOrdinanceOn(kCtOrdinance_MandatoryWaterMeters);
  Lock();
  ++simulationCycle;
  f32 factor = 1.0f;
  if (conservationOrdinance)
    factor = f32(1.0 - f64(CtWaterConservationFactor) * 0.01);
  if (meterOrdinance)
    factor = f32(f64(factor) - f64(CtWaterMeterFactor) * 0.01);
  CalculateCapacityForAllPumps();
  ++simulationCycle;
  CtWaterBitsCopy(differenceBits, waterBits, mapWidth);
  CtWaterBitsClear(waterBits, mapWidth);
  CtDealRecord seller{}, buyer{};
  AddFakePumps(static_cast<CtDealType>(2), mainBits, &seller);
  u32 words = u32(mapWidth) * mapWidth / 32;
  u32 *network = nullptr, *reached = nullptr, *unserved = nullptr;
  i64 total = 0, unused = 0;
  u32 region = 0;
  bool foundSeller = false, foundBuyer = false;
  auto first = pumps->begin();
  while (first != pumps->end() && (!first->capacity || first->unknown12 == simulationCycle))
    ++first;
  if (first != pumps->end()) {
    network = new u32[words];
    reached = new u32[words];
    unserved = new u32[words];
    CtWaterBitsCopy(unserved, buildingBits, mapWidth);
    while (first != pumps->end()) {
      CtWaterPumpRecord seed = *first;
      CtWaterBitsClear(network, mapWidth);
      i32 seedX = i32(u32(seed.position.x) >> kCtMicroCoordinateShift),
          seedZ = i32(u32(seed.position.z) >> kCtMicroCoordinateShift);
      CtWaterBitSet(network, mapWidth, seedX, seedZ);
      i32 firstRow = seedZ, endRow = seedZ + 1;
      CtWaterBitsSelectionGrow(network, mainBits, mapWidth, 500, &firstRow, &endRow);
      firstRow = seedZ;
      endRow = seedZ + 1;
      i64 capacity = 0;
      CtWaterBitsClear(reached, mapWidth);
      for (CtWaterPumpRecord &pump : *pumps) {
        u32 x = u32(pump.position.x) >> kCtMicroCoordinateShift, z = u32(pump.position.z) >> kCtMicroCoordinateShift;
        if (CtWaterBitTest(network, mapWidth, i32(x), i32(z)) && pump.capacity) {
          pump.unknown12 = simulationCycle;
          pump.unknown22 = u16(region);
          capacity = i64(u64(capacity) + pump.capacity);
          CtWaterBitSet(reached, mapWidth, i32(x), i32(z));
          if (i32(z) < firstRow)
            firstRow = i32(z);
          if (i32(z) >= endRow)
            endRow = i32(z) + 1;
        }
      }
      memset(unrecovered200, 0, sizeof(unrecovered200));
      memset(unrecovered216, 0, sizeof(unrecovered216));
      memset(totalCapacity, 0, sizeof(totalCapacity));
      memset(totalUsed, 0, sizeof(totalUsed));
      Unlock();
      sellerConnected = FindBuyerOrSeller(static_cast<CtDealType>(2), network, &seller);
      buyerConnected = FindBuyerOrSeller(static_cast<CtDealType>(3), network, &buyer);
      foundSeller = foundSeller || sellerConnected != 0;
      foundBuyer = foundBuyer || buyerConnected != 0;
      Lock();
      i64 supply = sellerConnected ? i64(0x1fffffffffffffffull) : capacity;
      i32 allFirst = 0, allEnd = mapWidth;
      CtWaterBitsGrow(network, mapWidth, i32(CtWaterFromPipe), &allFirst, &allEnd);
      i32 horizontalFirst = firstRow, horizontalEnd = endRow, verticalFirst = firstRow, verticalEnd = endRow;
      if (verticalFirst > 0)
        --verticalFirst;
      if (verticalEnd < mapWidth)
        ++verticalEnd;
      // Preserve the exact FILD operand; the neighbor allowance exceeds binary64's exact integer range.
      i64 remaining = ScDivideInt64Float(supply, factor);
      while (remaining > 0) {
        u8 horizontal =
            GrowHorizontalWithDrain(reached, network, unserved, mapWidth, &remaining, &horizontalFirst, &horizontalEnd);
        u8 vertical =
            GrowVerticalWithDrain(reached, network, unserved, mapWidth, &remaining, &verticalFirst, &verticalEnd);
        horizontalFirst = horizontalFirst < verticalFirst ? horizontalFirst : verticalFirst;
        horizontalEnd = horizontalEnd > verticalEnd ? horizontalEnd : verticalEnd;
        verticalFirst = horizontalFirst > 0 ? horizontalFirst - 1 : horizontalFirst;
        verticalEnd = horizontalEnd < mapWidth ? horizontalEnd + 1 : horizontalEnd;
        if (!horizontal && !vertical)
          break;
      }
      remaining = ScMultiplyInt64Float(remaining, factor);
      if (buyerConnected)
        UpdateBuyerDeal(buyer, &remaining);
      if (sellerConnected)
        UpdateSellerDeal(seller, &remaining, &capacity);
      total = i64(u64(total) + u64(capacity));
      CtWaterBitsSelectionSet(waterBits, reached, mapWidth);
      unused = i64(u64(unused) + u64(remaining));
      for (CtWaterPumpRecord &pump : *pumps) {
        if (u32(pump.unknown22) == region) {
          if (capacity <= 0) {
            pump.production = pump.capacity;
          } else {
            i64 delivered = i64(u64(capacity) - u64(remaining));
            pump.production = u16(i64(u64(delivered) * pump.capacity) / capacity);
          }
        }
      }
      ++region;
      first = pumps->begin();
      while (first != pumps->end() && (!first->capacity || first->unknown12 == simulationCycle))
        ++first;
    }
  }
  for (auto it = pumps->begin(); it != pumps->end();) {
    if (it->pumpType != 1) {
      ++it;
      continue;
    }
    CtWaterPumpRecord selected = *it;
    for (auto candidate = pumps->begin(); candidate != pumps->end();)
      if (*candidate == selected)
        candidate = pumps->erase(candidate);
      else
        ++candidate;
    it = pumps->begin();
  }
  secondWaterCount = u32(CtWaterBitsCount(unserved ? unserved : buildingBits, mapWidth));
  if (secondWaterCount) {
    firstWaterCount = u32(CtWaterBitsCount(buildingBits, mapWidth));
  } else {
    u16 count = u16(u32(mapWidth) * mapWidth / 32), index = 0;
    while (index < count && !buildingBits[index])
      ++index;
    if (index < count)
      firstWaterCount = 100;
  }
  delete[] network;
  delete[] reached;
  delete[] unserved;
  CtWaterBitsXor(differenceBits, waterBits, mapWidth);
  Unlock();
  if (foundSeller)
    FinalizeSellerDeal(seller);
  if (foundBuyer)
    FinalizeBuyerDeal(buyer);
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  Lock();
  i64 used = i64(u64(total) - u64(unused));
  memcpy(totalCapacity, &total, sizeof(total));
  memcpy(totalUsed, &used, sizeof(used));
  u32 percentage = total > 0 ? u32(i64(u64(unused) * 100) / total) : 0;
  waterHistory->SetMonthData(percentage);
  if (month == 1) {
    waterHistory->SetYearData(waterHistory->GetMonthAverage());
    if (year % 10 == 0)
      waterHistory->SetDecadeData(waterHistory->GetYearAverage());
  }
  SaveCapacityForAllPumps();
  Unlock();
  ScMessage message(kCtMsg_WaterCapacityUpdated);
  ScGetSimutilMessageServer()->MessagePost(message, false);
}

// Find the first pump in the requested cell, copying its complete record only on success while holding the lock.
u8 CtPlumbingSimulation::FindPump(u16 x, u16 z, CtWaterPumpRecord *out) {
  Lock();
  u8 found = 0;
  for (const CtWaterPumpRecord &record : *pumps) {
    if ((record.position.x >> kCtMicroCoordinateShift) == x && (record.position.z >> kCtMicroCoordinateShift) == z) {
      *out = record;
      found = 1;
      break;
    }
  }
  Unlock();
  return found;
}

// Insert temporary unit-capacity pumps at connected water mains along the selected neighbor's city edge.
void CtPlumbingSimulation::AddFakePumps(CtDealType type, u32 *bits, CtDealRecord *deal) {
  CtWaterPumpRecord record;
  record.unknown12 = simulationCycle - 1;
  record.fullCapacity = 0;
  record.capacity = 1;
  record.production = 0;
  record.unknown22 = 0;
  record.pumpType = 1;
  if (!neighbors->GetDeal(u8(type), deal) || deal->value_10 > 3)
    return;
  u8 side = deal->value_10;
  u16 edge = u16(mapWidth - 1);
  u16 coordinate = side == 1 || side == 2 ? 1 : 0;
  while (i32(coordinate) < (side == 1 || side == 2 ? i32(mapWidth) : i32(mapWidth) - 1)) {
    u16 x = side == 0 ? 0 : side == 2 ? edge : coordinate;
    u16 z = side == 1 ? 0 : side == 3 ? edge : coordinate;
    if (CtWaterBitTest(bits, mapWidth, x, z) && neighbors->HasConnection(5, x, z)) {
      record.position = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
      pumps->push_back(record);
    }
    ++coordinate;
  }
}

// Find the first live water connection belonging to the requested neighbor and return its observed low byte.
// The neighbor record is written by GetDeal even when no connected edge cell is found.
u8 CtPlumbingSimulation::FindBuyerOrSeller(CtDealType type, u32 *bits, CtDealRecord *deal) {
  if (!neighbors->GetDeal(u8(type), deal) || deal->value_10 > 3)
    return 0;
  u8 side = deal->value_10;
  u16 edge = u16(mapWidth - 1);
  u16 coordinate = side == 1 || side == 2 ? 1 : 0;
  while (i32(coordinate) < (side == 1 || side == 2 ? i32(mapWidth) : i32(mapWidth) - 1)) {
    u16 x = side == 0 ? 0 : side == 2 ? edge : coordinate;
    u16 z = side == 1 ? 0 : side == 3 ? edge : coordinate;
    if (CtWaterBitTest(bits, mapWidth, x, z) && neighbors->HasConnection(5, x, z))
      return 1;
    ++coordinate;
  }
  return 0;
}

u8 CtPlumbingSimulation::IsOnTheEdge(i16 x, i16 z) {
  return x == 0 || z == 0 || x == terrainWidth || z == terrainHeight;
}

// Select the neighbor bordering a terrain-edge cell, preserving the original corner precedence and zero fallback.
i32 CtPlumbingSimulation::WhoIsTheNeighbor(i16 x, i16 z) {
  if (x == 0 && z < terrainHeight)
    return 0;
  if (z == 0 && x > 0)
    return 1;
  if (x == terrainWidth && z > 0)
    return 2;
  if (z == terrainHeight && x < terrainWidth)
    return 3;
  return 0;
}

bool CtPlumbingSimulation::ValidPos(i16 x, i16 z) const {
  return x >= 0 && z >= 0 && terrain->InCellBounds(u32(x), u32(z));
}

// Search the four sides for the requested fresh/salt classification, preserving sample and early-return order.
bool CtPlumbingSimulation::IsThereWater(i16 x0, i16 z0, i16 x1, i16 z1, u8 waterType) {
  bool found = false;
  i16 coordinate = x0;
  while (coordinate <= x1 && !found) {
    if (ValidPos(coordinate, z0) && terrain->IsWater(u32(coordinate), u32(z0)))
      found = waterType == u8(pollution->IsSaltWater(u32(coordinate), u32(z0)));
    coordinate = i16(u16(coordinate) + 1);
  }
  coordinate = x0;
  while (coordinate <= x1 && !found) {
    if (ValidPos(coordinate, z1) && terrain->IsWater(u32(coordinate), u32(z1)))
      found = waterType == u8(pollution->IsSaltWater(u32(coordinate), u32(z1)));
    coordinate = i16(u16(coordinate) + 1);
  }
  coordinate = z0;
  while (coordinate <= z1 && !found) {
    if (ValidPos(x0, coordinate) && terrain->IsWater(u32(x0), u32(coordinate)))
      found = waterType == u8(pollution->IsSaltWater(u32(x0), u32(coordinate)));
    coordinate = i16(u16(coordinate) + 1);
  }
  coordinate = z0;
  while (coordinate <= z1 && !found) {
    if (ValidPos(x1, coordinate) && terrain->IsWater(u32(x1), u32(coordinate)))
      found = waterType == u8(pollution->IsSaltWater(u32(x1), u32(coordinate)));
    coordinate = i16(u16(coordinate) + 1);
  }
  return found;
}

// Calculate the original cell's building water demand from occupant area and its optional agent-ID modifier.
u16 CtPlumbingSimulation::BuildingConsumption(u16 x, u16 z) {
  CtCoord location{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  u32 modifier = CtWaterDefaultConsumption, area = 1;
  if (surfaceOccupants->GetOccupant(location, &occupant)) {
    u32 id = occupant->TypeId();
    u8 depth = occupant->SizeInCellsZ();
    area = u32(occupant->SizeInCellsX()) * depth;
    occupant->Release();
    if (id) {
      u16 step = 64, index = 63;
      while (step) {
        step >>= 1;
        u32 candidate = CtWaterAgentIds[index];
        if (candidate == id) {
          modifier = CtWaterAgentModifiers[index];
          break;
        }
        if (candidate <= id)
          index = u16(index + step);
        else
          index = u16(index - step);
      }
    }
  }
  return u16(modifier * area);
}

// Consume newly reached building cells in increasing bit order, stopping before clearing a cell that exhausts supply.
void CtPlumbingSimulation::ChangeLimit(u32 mask, u32 *buildings, i64 *remaining, i32 word, i32 row, i32 index) {
  u32 pending = mask & buildings[index];
  i32 bit = 0;
  while (pending) {
    if (pending & 1) {
      u16 demand = BuildingConsumption(u16(u32(word) * 32 + u32(bit)), u16(row));
      if (*remaining <= demand) {
        *remaining = 0;
        return;
      }
      buildings[index] &= ~(1u << bit);
      *remaining -= demand;
    }
    pending >>= 1;
    ++bit;
  }
}

// Grow selected water cells horizontally, draining reached buildings as each changed DWORD is visited.
u8 CtPlumbingSimulation::GrowHorizontalWithDrain(
    u32 *bits, u32 *selection, u32 *buildings, u32 width, i64 *remaining, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, end = *endRow, cursor = first * i32(width >> 5);
  *firstRow = i32(width);
  *endRow = 0;
  bool changed = false;
  for (i32 row = first; row < end; ++row) {
    u32 previous = bits[cursor], current = bits[cursor + 1];
    u32 delta = previous ^ (selection[cursor] & (previous | (previous >> 1) | (previous << 1) | (current << 31)));
    if (delta) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      changed = true;
      bits[cursor] = previous | delta;
      if (delta & buildings[cursor])
        ChangeLimit(delta, buildings, remaining, 0, row, cursor);
    }
    i32 index = cursor + 1, word = 1, interior = i32(width) - 96;
    if (*remaining) {
      while (interior >= 0) {
        u32 original = current;
        current = bits[index + 1];
        delta = original ^
            (selection[index] & (original | (original >> 1) | (original << 1) | (previous >> 31) | (current << 31)));
        if (delta) {
          if (row < *firstRow)
            *firstRow = row;
          if (row >= *endRow)
            *endRow = row + 1;
          changed = true;
          bits[index] = original | delta;
          if (delta & buildings[index])
            ChangeLimit(delta, buildings, remaining, word, row, index);
        }
        previous = original;
        interior -= 32;
        ++index;
        ++word;
        if (!*remaining)
          break;
      }
    }
    delta = current ^ (selection[index] & (current | (current >> 1) | (current << 1) | (previous >> 31)));
    if (delta) {
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      changed = true;
      bits[index] = current | delta;
      if (delta & buildings[index])
        ChangeLimit(delta, buildings, remaining, word, row, index);
    }
    cursor = index + 1;
  }
  return changed;
}

// Grow selected water cells vertically and drain reached buildings in original column and row order.
u8 CtPlumbingSimulation::GrowVerticalWithDrain(
    u32 *bits, u32 *selection, u32 *buildings, u32 width, i64 *remaining, i32 *firstRow, i32 *endRow) {
  i32 first = *firstRow, last = *endRow - 1, words = i32(width >> 5);
  *firstRow = i32(width);
  *endRow = 0;
  bool changed = false;
  for (i32 word = 0; word < words; ++word) {
    i32 index = first * words + word;
    u32 previous = bits[index], current = bits[index + words];
    u32 delta = previous ^ (selection[index] & (previous | current));
    if (delta) {
      changed = true;
      bits[index] = previous | delta;
      if (delta & buildings[index])
        ChangeLimit(delta, buildings, remaining, word, first, index);
      if (first < *firstRow)
        *firstRow = first;
      if (first >= *endRow)
        *endRow = first + 1;
    }
    i32 row = first + 1;
    index += words;
    if (*remaining) {
      while (row < last) {
        u32 original = current;
        current = bits[index + words];
        delta = original ^ (selection[index] & (previous | original | current));
        if (delta) {
          changed = true;
          bits[index] = original | delta;
          if (row < *firstRow)
            *firstRow = row;
          if (row >= *endRow)
            *endRow = row + 1;
          if (delta & buildings[index])
            ChangeLimit(delta, buildings, remaining, word, row, index);
        }
        previous = original;
        index += words;
        ++row;
        if (!*remaining)
          break;
      }
    }
    delta = current ^ (selection[index] & (previous | current));
    if (delta) {
      changed = true;
      bits[index] = current | delta;
      if (row < *firstRow)
        *firstRow = row;
      if (row >= *endRow)
        *endRow = row + 1;
      if (delta & buildings[index])
        ChangeLimit(delta, buildings, remaining, word, row, index);
    }
  }
  return changed;
}

u32 CtPlumbingSimulation::GetNumPumpsAging() {
  return agingPumps;
}

u32 CtPlumbingSimulation::GetAverageWaterPollutionInPumps() {
  return pumpPollution;
}

// Copy the original difference map only when the requested DWORD count matches the square grid.
u8 CtPlumbingSimulation::GetDiffMap(u32 *out, u32 count) {
  if (count != u32(mapWidth) * u32(mapWidth) / 32)
    return 0;
  Lock();
  for (u32 i = 0; i < count; ++i)
    out[i] = differenceBits[i];
  Unlock();
  return 1;
}

u32 CtPlumbingSimulation::GetMapSpriteCount() {
  return 1;
}

bool CtPlumbingSimulation::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10016)
    return false;
  CtPlumbingMapSprite *sprite = new CtPlumbingMapSprite(this);
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->layer = nullptr;
  delete sprite;
  return false;
}

bool CtPlumbingSimulation::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10016;
  return true;
}

static u8 BdTreatmentTuningLoaded;
static u32 BdTreatmentOptimal = 10000, BdTreatmentMaximum = 180, BdTreatmentDecline = 120;

// Initialize the two real receiver subobjects and the shared water-treatment tuning before clearing monthly state.
// The installed constructor reads the three tuning values once and clamps decline only when it exceeds maximum.
BdWaterTreatment::BdWaterTreatment() : treatmentSprite(nullptr), constructionDate(), retired(0) {
  if (!BdTreatmentTuningLoaded) {
    GeIni ini;
    ScString path, value;
    ScGetSimutilApplication()->GetAppDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SetPath(path);
    ScGetSimutilApplication()->GetAppDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SetCompressedPath(path);
    ScString section("WaterTreatment");
    if (ini.ReadEntry(section, ScString("OptimalPollutionCap"), &value))
      BdTreatmentOptimal = GeParseNumber(value.ToChar());
    if (ini.ReadEntry(section, ScString("MaxLifespan"), &value))
      BdTreatmentMaximum = GeParseNumber(value.ToChar());
    if (!BdTreatmentMaximum)
      BdTreatmentMaximum = 1;
    if (ini.ReadEntry(section, ScString("DeclineAge"), &value))
      BdTreatmentDecline = GeParseNumber(value.ToChar());
    if (BdTreatmentDecline > BdTreatmentMaximum)
      BdTreatmentDecline = BdTreatmentMaximum >> 1;
    BdTreatmentTuningLoaded = 1;
  }
  dailyCapacity = 0;
  previousConsumption = 0;
  consumption = 0;
}

BdWaterTreatment::~BdWaterTreatment() {
}

// Bind the supplied attributes, replacing any treatment-layer sprite after the base initialization succeeds.
// On base failure the original dispatches virtual shutdown before returning false.
bool BdWaterTreatment::Init(GeAttrib *value) {
  if (!OcUtilityBuilding::Init(value)) {
    Shutdown();
    return false;
  }
  if (treatmentSprite)
    DestroySprites(&treatmentSprite);
  return CreateSprite(2, &treatmentSprite);
}

// Read the three treatment counters and retirement state after the building descriptor has loaded successfully.
// The descriptor's individual low-byte return declarations remain qualified in the shared interface.
bool BdWaterTreatment::Init(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Init(city, info);
  if (result) {
    result = info->GetIntData(kBdTreatmentDailyCapacityKey, &dailyCapacity);
    if (result) {
      result = info->GetIntData(kBdTreatmentConsumptionKey, &consumption);
      if (result)
        result = info->GetIntData(kBdTreatmentPreviousConsumptionKey, &previousConsumption);
    }
  }
  u32 savedRetired = 0;
  if (result) {
    result = info->GetIntData(kBdTreatmentRetiredKey, &savedRetired);
    if (result)
      retired = savedRetired != 0;
  }
  return result != 0;
}

// Write treatment counters and retirement state after the building descriptor, stopping at the first refusal.
// This follows the installed order of the four private integer properties.
bool BdWaterTreatment::Save(CtCity *city, OcPersistInfo *info) {
  u8 result = OcUtilityBuilding::Save(city, info);
  if (result) {
    result = info->SetIntData(kBdTreatmentDailyCapacityKey, dailyCapacity);
    if (result) {
      result = info->SetIntData(kBdTreatmentConsumptionKey, consumption);
      if (result) {
        result = info->SetIntData(kBdTreatmentPreviousConsumptionKey, previousConsumption);
        if (result)
          result = info->SetIntData(kBdTreatmentRetiredKey, retired);
      }
    }
  }
  return result != 0;
}

void BdWaterTreatment::Shutdown() {
  OcUtilityBuilding::Shutdown();
  DestroySprites(&treatmentSprite);
}

// Publish the treatment subobject for its own and the mortality identities, otherwise query the building.
// Every route starts by clearing the output, matching the installed primary query method.
bool BdWaterTreatment::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid != IID_BdTreatment && iid != IID_BdMortal)
    return OcUtilityBuilding::QueryInterface(iid, out);
  *out = static_cast<BdTreatment *>(this);
  OcUtilityBuilding::AddRef();
  return true;
}

u32 BdWaterTreatment::AddRef() {
  return OcUtilityBuilding::AddRef();
}

u32 BdWaterTreatment::Release() {
  return OcUtilityBuilding::Release();
}

void BdWaterTreatment::SetMaximumAge(u32 value) {
  BdTreatmentMaximum = value;
}

u32 BdWaterTreatment::GetMaximumAge() {
  return BdTreatmentMaximum;
}

void BdWaterTreatment::SetDeclineAge(u32 value) {
  BdTreatmentDecline = value;
}

u32 BdWaterTreatment::GetDeclineAge() {
  return BdTreatmentDecline;
}

u32 BdWaterTreatment::GetAge() {
  u32 month, day, year;
  ScGetSimutilCity()->GetDate(&month, &day, &year);
  return 12 * year - 12 * constructionDate.Year() + month - constructionDate.Month();
}

bool BdWaterTreatment::IsAging() {
  u32 age = GetAge();
  return GetOccupancyState() == 1 && age >= GetDeclineAge();
}

void BdWaterTreatment::Retire() {
  retired = 1;
}

u8 BdWaterTreatment::GetRetired() {
  return retired;
}

// Borrow the complete building view from the treatment interface.
BdBuilding *BdWaterTreatment::AsISC3Building() {
  return static_cast<OcUtilityBuilding *>(this);
}

// Borrow the actual occupant view from the complete utility-building receiver.
ScOccupant *BdWaterTreatment::GetOccupant() {
  return OcUtilityBuilding::AsISC3Occupant();
}

u32 BdWaterTreatment::GetProduction() {
  return dailyCapacity;
}

u32 BdWaterTreatment::GetCapacity() {
  u32 monthly = 30 * dailyCapacity;
  return monthly > consumption ? monthly - consumption : 0;
}

u32 BdWaterTreatment::Consume(u32 amount) {
  consumption += amount;
  return amount;
}

u32 BdWaterTreatment::GetGeomFlag() {
  return kBdWaterBuildingGeometry;
}

// Select the treatment sprite only for layer2 when the building is not in either inactive state.
// Layer1 and inactive layer2 both return the original shared occupant sprite.
SpSprite *BdWaterTreatment::GetSpriteInst(u32 layer) {
  if (layer == 1)
    return sprite;
  if (layer != 2)
    return nullptr;
  if (GetOccupancyState() != 16 && GetOccupancyState() != 8)
    return treatmentSprite;
  return sprite;
}

bool BdWaterTreatment::HasBirthdate() const {
  return true;
}

bool BdWaterTreatment::GetBirthdate(CtDateInterface *out) const {
  out->Set(constructionDate.serial);
  return true;
}

bool BdWaterTreatment::SetBirthdate(const CtDateInterface &value) {
  constructionDate.serial = value.DayNumber();
  return true;
}

// Move the current consumption into the previous-month field after clearing the current field.
void BdWaterTreatment::DoMonthlySim() {
  u32 previous = consumption;
  consumption = 0;
  previousConsumption = previous;
}

// Recompute treatment capacity from age, power, water and occupancy, dispatching the original replacement when old.
// Both utility queries occur before their results are tested; the replacement method remains a direct dependency.
void BdWaterTreatment::DoWeeklySim() {
  CtCity *city = ScGetSimutilCity();
  if (GetRetired())
    return;
  u32 age = GetAge();
  if (age >= BdTreatmentMaximum) {
    dailyCapacity = 0;
    BecomeDefunctDueToAge();
    return;
  }
  CtPowerLayer *power = city->PowerLayer();
  bool powered;
  power->GetValue(position & 0x7ff, (position >> 11) & 0x7ff, &powered);
  CtPlumbingLayer *water = city->PlumbingLayer();
  u8 watered = water->IsWatered(position & 0x7ff, (position >> 11) & 0x7ff);
  if (!powered || !watered || GetOccupancyState() != 1) {
    dailyCapacity = 0;
    return;
  }
  if (age > BdTreatmentDecline)
    dailyCapacity = BdTreatmentOptimal -
        BdTreatmentOptimal * (age - BdTreatmentDecline) / (BdTreatmentMaximum - BdTreatmentDecline);
  else
    dailyCapacity = BdTreatmentOptimal;
}

// Replace the expired treatment facility, transferring location, birthdate and mortality.
// Remove the old occupant from both managers before inserting the replacement and posting its original bounds message.
void BdWaterTreatment::BecomeDefunctDueToAge() {
  GeBuildingLayer *buildings = ScGetSimutilCity()->BuildingLayer();
  ScOccupantManager *surface = ScGetSimutilCity()->SurfaceOccupantManager();
  ScOccupantManager *underground = ScGetSimutilCity()->UndergroundOccupantManagerLevelOne();
  if (!buildings || !surface || !underground)
    return;
  void *selected;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  void *created;
  if (!buildings->CreateBuilding(cachedAttributes, IID_BdBuilding, &created)) {
    occupant->Release();
    return;
  }
  BdBuilding *replacement = static_cast<BdBuilding *>(created);
  replacement->SetOccupancyState(8);
  if (replacement->QueryInterface(IID_ScOccupant, &selected)) {
    ScOccupant *newOccupant = static_cast<ScOccupant *>(selected);
    newOccupant->SetLocationInCellCoords(position & 0x7ff, (position >> 11) & 0x7ff);
    newOccupant->Release();
  }
  GeDate date;
  GetBirthdate(&date);
  replacement->SetBirthdate(date);
  if (replacement->QueryInterface(IID_BdMortal, &selected)) {
    BdMortal *mortal = static_cast<BdMortal *>(selected);
    mortal->SetMaximumAge(GetMaximumAge());
    mortal->SetDeclineAge(GetDeclineAge());
    mortal->Retire();
    mortal->Release();
  }
  surface->RemoveOccupant(occupant);
  underground->RemoveOccupant(occupant);
  buildings->InsertBuilding(replacement);
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  u32 first =
      (u32(bounds.minimum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 last =
      (u32(bounds.maximum.x >> kCtMicroCoordinateShift) << 16) | u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  ScMessage message(kNwMsg_RefreshNetwork, 8, first, last);
  ScGetSimutilMessageServer()->MessagePost(message, false);
  replacement->Release();
  occupant->Release();
}

bool BdWaterTreatment::Simulate(i32 phase) {
  bool result = OcUtilityBuilding::Simulate(phase);
  if (phase == 3)
    DoWeeklySim();
  else if (phase == 4)
    DoMonthlySim();
  return result;
}

i32 BdWaterTreatment::MinimumExecutionInterval() {
  i32 interval = OcUtilityBuilding::MinimumExecutionInterval();
  return interval >= 3 ? 3 : interval;
}

// Add the facility's aging or retirement status after the shared building options.
// Aging takes precedence over the raw retirement byte, matching the installed virtual call order.
u8 BdWaterTreatment::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (IsAging())
    instance = 406;
  else if (GetRetired())
    instance = 407;
  if (instance) {
    ScString text = ScUtilityQueryText(instance);
    query->SetStatusText(text);
  }
  return 1;
}

u8 BdWaterTreatment::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcUtilityBuilding::QueryDetail(message, query);
}

// The language service and each live city/utility call retain the installed ordering and warning predicates.
u8 BdWaterTreatment::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, text, pattern, number, maximum;
  ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  if (query) {
    label = ScUtilityQueryText(kRsCityServiceMaximumWaterCapacityText);
    pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
    utility->MakeNumberString(i32(BdTreatmentOptimal), &number);
    text.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);

    label = ScUtilityQueryText(kRsCityServiceCurrentWaterCapacityText);
    pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
    utility->MakeNumberString(i32(dailyCapacity), &number);
    u8 warning = dailyCapacity == 0;
    text.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);

    label = ScUtilityQueryText(107);
    pattern = ScUtilityQueryText(kRsCityServiceMMonthFormatText);
    utility->MakeNumberString(previousConsumption / 30, &number);
    text.Sprintf(pattern.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);

    label = ScUtilityQueryText(kRsCityServiceConnectedToPipeNetworkText);
    u32 x, z;
    GetLocationInCellCoords(&x, &z);
    CtPlumbingLayer *water = ScGetSimutilCity()->PlumbingLayer();
    u8 watered = water->IsWatered(x, z);
    text = ScUtilityQueryText(watered ? 5 : 6);
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, !watered);

    label = ScUtilityQueryText(kRsCityServiceAgeMaxText);
    utility->MakeNumberString(GetAge() / 12, &number);
    utility->MakeNumberString(GetMaximumAge() / 12, &maximum);
    pattern = ScUtilityQueryText(kRsCityServiceYrsYrsFormatText);
    text.Sprintf(pattern.Data(), number.Data(), maximum.Data());
    u32 age = GetAge();
    warning = age >= GetMaximumAge();
    query->AddText(&label, &text, 8, warning);
  }
  return OcUtilityBuilding::QueryTile(message, query);
}
