#include "misc/landmark.h"

#include <stdio.h>
#include <string.h>

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sp_map_sprite.h"
#include "network/nw_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "ui/api.h"

// Landmarks return the original civic building geometry word.
enum BdLandmarkGeometry : u32 {
  kBdLandmarkBuildingGeometry = 0x40000104,
};

// The stadium descriptor stores its user-visible team name under this key.
enum OcStadiumPersistenceKey : u32 {
  kOcStadiumTeamNameKey = 0x43530181,
};

OcBallPark::OcBallPark() {
}

// Retire the ball park through its actual base and optional owned name.
OcBallPark::~OcBallPark() = default;

bool OcBallPark::IsNameChangeable() {
  return true;
}

// Report the original capped population-cohort metric before ordinary special-building query rows.
u8 OcBallPark::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    if (buildings->CheckOccupantTracking(14313) < 0)
      return 0;
    i32 count = buildings->CheckOccupantTracking(14313);
    if (!count)
      return 0;
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    if (!common->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
      return 0;
    // Zero native output excludes an unwritten result from an out-of-contract map lookup.
    i8 local = 0;
    aura->GetValue(position & 0x7ff, (position >> 11) & 0x7ff, &local);
    aura->Release();
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    f64 cohort = f64(residential->GetAgeCohortPopPct(6, 10));
    u32 population = u32(residential->GetPopulation());
    i32 adjusted = i32(local) + 128;
    i32 metric = i32(i64(f64(population) * cohort * (f64(adjusted) * 0.00392156862745098) * 0.25 / f64(u32(count))));
    if (u32(metric) > 210)
      metric = 210;
    {
      ScString working, number;
      {
        CtMiscTextResource resource(kRsCityServiceLittleLeaguersText, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(243, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(metric, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcCasino::OcCasino() {
}

OcCasino::~OcCasino() = default;

// Report the original cohort education/population metric before animated business-deal date and income rows.
u8 OcCasino::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    ScString number;
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    f32 education = f32(256 - u32(residential->GetAgeCohortEQ(16, 22)));
    f32 weighted = f32(f64(residential->GetAgeCohortPopPct(16, 22)) * f64(education));
    u32 population = u32(residential->GetPopulation());
    i32 metric = i32(i64(f64(population) * f64(weighted) * 0.00005));
    {
      CtMiscTextResource resource(kRsCityServiceDriveThroughWeddingsPerformedText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      label = copy;
    }
    {
      CtMiscTextResource resource(287, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      value = copy;
    }
    utility->MakeNumberString(metric, &number);
    value.Sprintf(value.Data(), number.Data());
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcDealAnimBuilding::QueryTile(message, query);
}

OcCastle::OcCastle() {
}

// Retire the concrete receiver through its actual special-building base and optional owned name.
OcCastle::~OcCastle() = default;

bool OcCastle::IsNameChangeable() {
  return true;
}

// Publish six original count/funds rows, querying each live value only after its localized label is loaded.
u8 OcCastle::QueryTile(ScMessage *message, UiQuery *query) {
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    ScString label, value;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    EcBudgetLayer *budget = ScGetMiscCity()->BudgetLayer();
    const u32 kinds[6] = {14140, 13002, 13881, 0, 13130, 14450};
    for (u32 row = 0; row < 6; ++row) {
      {
        CtMiscTextResource resource(441 + row, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        label = copy;
      }
      i32 metric = 0;
      if (row == 3)
        metric = i32(budget->GetTotalFunds() / 50000);
      else if (buildings->CheckOccupantTracking(kinds[row]) >= 0)
        metric = buildings->CheckOccupantTracking(kinds[row]);
      utility->MakeNumberString(metric, &value);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcCountryClub::OcCountryClub() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcCountryClub::~OcCountryClub() = default;

bool OcCountryClub::IsNameChangeable() {
  return true;
}

bool OcCountryClub::HasBirthdate() const {
  return true;
}

bool OcCountryClub::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcCountryClub::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report the original education/tracking and subway-count metrics before the dated special-building query rows.
u8 OcCountryClub::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    i32 count = 0;
    if (buildings->CheckOccupantTracking(13873) >= 0)
      count = buildings->CheckOccupantTracking(13873);
    u32 tracked = 379 * (u32(count) + 1);
    i32 first = i32(tracked + residential->GetWorkforceEQ());
    // The stored IID4147C2FB view is the real NwManager producer's CtLayer primary, without a query or extra hold.
    CtLayer *network_view = reinterpret_cast<CtLayer *>(ScGetMiscCity()->TransitLayer());
    NwManager *networks = static_cast<NwManager *>(network_view);
    u32 second = networks->GetSubwayCount() / 5;
    ScString working, number;
    for (u32 row = 0; row < 2; ++row) {
      {
        CtMiscTextResource resource(302 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(303 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(row ? i32(second) : first, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcGeyserPark::OcGeyserPark() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcGeyserPark::~OcGeyserPark() = default;

bool OcGeyserPark::IsNameChangeable() {
  return true;
}

bool OcGeyserPark::HasBirthdate() const {
  return true;
}

bool OcGeyserPark::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcGeyserPark::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report the original air/connection metric and tracked-building sum before animated date and ordinary rows.
u8 OcGeyserPark::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    CtNeighborsLayer *neighbors = ScGetMiscCity()->NeighborsLayer();
    u32 connections = neighbors->GetSubwayConnections();
    connections += neighbors->GetHighwayConnections();
    connections += neighbors->GetRoadConnections();
    connections += neighbors->GetRailConnections();
    EcLayer *pollution = ScGetMiscCity()->PollutionLayer();
    u32 air = 0;
    pollution->GetAirValue(position & 0x7ff, (position >> 11) & 0x7ff, &air);
    u32 factor = (0x7fff - air) / 0x7fff;
    i32 first = i32(i64(f64(factor) * f64(connections + 1) * 250.0));
    i32 zoo = 0, park = 0;
    if (buildings->CheckOccupantTracking(14450) >= 0)
      zoo = buildings->CheckOccupantTracking(14450);
    if (buildings->CheckOccupantTracking(14315) >= 0)
      park = buildings->CheckOccupantTracking(14315);
    i32 second = i32(u32(zoo) + u32(park));
    ScString working, number;
    for (u32 row = 0; row < 2; ++row) {
      {
        CtMiscTextResource resource(296 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(297 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(row ? second : first, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscAnimBuilding::QueryTile(message, query);
}

OcGigamall::OcGigamall() {
}

OcGigamall::~OcGigamall() = default;

// Report the original demand/cohort metric before the business-deal date and operational-income rows.
u8 OcGigamall::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    ScString number;
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kEcValveLayerType);
    EcValveLayer *valves = nullptr;
    EcValve *valve = nullptr;
    if (!common->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves)) ||
        !valves->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&valve)))
      return 0;
    u32 demand = u32(valve->QueryDemandValue());
    valves->Release();
    valve->Release();
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    f64 cohort = f64(residential->GetAgeCohortPopPct(65, 89));
    i32 metric = i32(i64(cohort * 50.0 + f64(demand / 500)));
    {
      CtMiscTextResource resource(kRsCityServiceAvgWaitForParkingSpaceText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      label = copy;
    }
    {
      CtMiscTextResource resource(kRsCityServiceHoursFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      value = copy;
    }
    utility->MakeNumberString(metric, &number);
    value.Sprintf(value.Data(), number.Data());
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcDealBuilding::QueryTile(message, query);
}

OcLandmark::OcLandmark() {
}

// Retire the concrete receiver through its actual special-building base and optional owned name.
OcLandmark::~OcLandmark() = default;

// Read the actual attribute's description key and append its resource text before ordinary special-building rows.
u8 OcLandmark::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScString working;
    RsKey key;
    attributes->GetQueryDescKey(&key);
    {
      CtMiscTextResource resource(kRsCityServicePlaceOfOriginText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(key.instance, key.group);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcLighthouse::OcLighthouse() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcLighthouse::~OcLighthouse() = default;

bool OcLighthouse::IsNameChangeable() {
  return true;
}

bool OcLighthouse::HasBirthdate() const {
  return true;
}

bool OcLighthouse::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcLighthouse::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report the original signed tracking quotient and wrapping product before animated date rows.
u8 OcLighthouse::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    const u32 kinds[3] = {12330, 14430, 12840};
    i32 counts[3] = {};
    for (u32 index = 0; index < 3; ++index)
      if (buildings->CheckOccupantTracking(kinds[index]) >= 0)
        counts[index] = buildings->CheckOccupantTracking(kinds[index]);
    i32 metric = i32(u32(counts[0] / 4) + u32(counts[1]) * u32(counts[2]));
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceShipsSavedText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(301, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(metric, &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscAnimBuilding::QueryTile(message, query);
}

SpMapSprite::SpMapSprite() : references(0), kind(0), palette(nullptr), width(0), height(0), enabled(0) {
  SpMapSprite::SetDestinationCoords(0, 0, 0, 1, 1, 0);
}

SpMapSprite::~SpMapSprite() {
  ScRelease(&palette);
}

// Publish the actual unknown/raster identity before the virtual retain, clearing the output.
bool SpMapSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpMapSprite);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, SpMapSprite);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpMapSprite::AddRef() {
  return ++references;
}

u32 SpMapSprite::Release() {
  return ScReleaseImpl(this, &references);
}

// Set the raster kind before its dimensions through the actual virtual entries, ignoring both setter replies.
bool SpMapSprite::Init(u32 value, u32 x, u32 z) {
  SetId(value);
  SetBaseSize(x, z);
  return true;
}

u32 SpMapSprite::GetId() const {
  return kind;
}

bool SpMapSprite::SetId(u32 value) {
  kind = value;
  return true;
}

// Retain the incoming palette before inspecting and releasing the current palette, then publish the new owner.
bool SpMapSprite::SetPalette(GxPaletteView *source) {
  if (source)
    source->AddRef();
  GxPaletteView *previous = palette;
  if (previous) {
    previous->Release();
    palette = nullptr;
  }
  palette = source;
  return true;
}

// Borrow the current palette without another retain; the original reads the field at+12.
GxPaletteView *SpMapSprite::GetPalette() {
  return palette;
}

i32 SpMapSprite::GetBaseWidth() const {
  return width;
}

i32 SpMapSprite::GetBaseHeight() const {
  return width;
}

// Publish both original unsigned dimension arguments as their stored DWORD bits and report success.
bool SpMapSprite::SetBaseSize(u32 x, u32 z) {
  width = i32(x);
  height = i32(z);
  return true;
}

u8 SpMapSprite::SetPostFilter(u8 value) {
  enabled = value;
  return value;
}

// Store three defining corners and derive the fourth in the source's nonsequential write order.
void SpMapSprite::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  corner_x[1] = x1;
  corner_x[0] = x0;
  corner_z[1] = z1;
  corner_x[2] = i32(u32(x3) + u32(x1) - u32(x0));
  corner_z[0] = z0;
  corner_x[3] = x3;
  corner_z[2] = i32(u32(z3) + u32(z1) - u32(z0));
  corner_z[3] = z3;
}

u8 SpMapSprite::IsPostFilter() const {
  return enabled;
}

// Require a destination, run the virtual pixel pass, and invoke completion only when that pass reports success.
bool SpMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  if (!destination)
    return false;
  if (!DrawCells(destination, rectangle, clip))
    return false;
  return DrawAnnotations(destination, rectangle, clip);
}

u8 SpMapSprite::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 SpMapSprite::DebugClassTag(ScStringView *) {
  return 0;
}

u8 SpMapSprite::DebugTypeTag(ScStringView *) {
  return 0;
}

bool SpMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

// Scale columns before rows with a vertical source flip and destination clipping.
// Wrap DWORD coordinate arithmetic before signed scaling, matching the installed SUB/IMUL/IDIV sequence.
bool SpMapSprite::DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  i32 span_x = i32(u32(rectangle.right) - u32(rectangle.left)),
      span_z = i32(u32(rectangle.bottom) - u32(rectangle.top));
  i32 left = 0, top = 0, right = span_x, bottom = span_z;
  if (clip) {
    i32 x0 = i32(u32(clip->left) - u32(rectangle.left)), z0 = i32(u32(clip->top) - u32(rectangle.top));
    i32 x1 = i32(u32(clip->right) - u32(rectangle.left)), z1 = i32(u32(clip->bottom) - u32(rectangle.top));
    if (x0 > left)
      left = x0;
    if (z0 > top)
      top = z0;
    if (x1 < right)
      right = x1;
    if (z1 < bottom)
      bottom = z1;
  }
  u32 pixel = 0;
  i32 last_z = i32(u32(height) - 1);
  for (i32 x = left; x < right; ++x) {
    i32 source_x = i32(u32(x) * u32(width)) / span_x;
    for (i32 z = top; z < bottom; ++z) {
      i32 source_z = i32(u32(z) * u32(height)) / span_z;
      if (GetColor(u32(source_x), u32(last_z) - u32(source_z), &pixel))
        destination->SetPixel(u32(x) + u32(rectangle.left), u32(z) + u32(rectangle.top), pixel);
    }
  }
  return true;
}

bool SpMapSprite::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 SpMapSprite::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 SpMapSprite::SetDebugClassTag(const ScStringView &) {
  return 0;
}

// Retain the supplied map views before capturing Z then X dimensions and publishing the aura raster kind.
// Initialize otherwise indeterminate optional fields explicitly; valid construction still requires the aura view.
SpAuraMapSprite::SpAuraMapSprite(CtAuraLayer *source, CtZoneLayer *zones) : ready(0), aura(nullptr), zone(nullptr) {
  if (source)
    aura = ScAddRefNotNull(source);
  if (zones)
    zone = ScAddRefNotNull(zones);
  u32 z = aura->CellCountZ(), x = aura->CellCountX();
  SetId(10012);
  SetBaseSize(x, z);
  if (aura && zone)
    ready = 1;
}

SpAuraMapSprite::~SpAuraMapSprite() {
  ScRelease(&aura);
  ScRelease(&zone);
}

// Mask unzoned cells before borrowing the palette, then map the inverse aura byte into its live entry count.
bool SpAuraMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  if (!ready)
    return false;
  u8 zoning;
  zone->GetValue(x, z, &zoning);
  if (!zoning)
    return false;
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i8 rating;
  aura->GetValue(x, z, &rating);
  u8 inverse = u8(127 - rating);
  i32 count = colors->GetNumberEntries();
  i32 index = i32(u32(inverse) * u32(count)) / 256;
  return colors->GetColor(index, out);
}

OcMarina::OcMarina() {
}

// Retire the marina through its actual base and optional owned name.
OcMarina::~OcMarina() = default;

bool OcMarina::IsNameChangeable() {
  return true;
}

// Report the original capped population, aura and tracked-building metric before ordinary special-building query rows.
u8 OcMarina::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    if (buildings->CheckOccupantTracking(14430) < 0)
      return 0;
    i32 count = buildings->CheckOccupantTracking(14430);
    if (!count)
      return 0;
    i32 first = 0, second = 0;
    if (buildings->CheckOccupantTracking(13873) >= 0)
      first = buildings->CheckOccupantTracking(13873);
    if (buildings->CheckOccupantTracking(13871) >= 0)
      second = buildings->CheckOccupantTracking(13871);
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    if (!common->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
      return 0;
    // Zero native output excludes an unwritten result from an out-of-contract map lookup.
    i8 local = 0;
    aura->GetValue(position & 0x7ff, (position >> 11) & 0x7ff, &local);
    aura->Release();
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    u32 population = u32(residential->GetPopulation());
    i32 adjusted = i32(local) + 128;
    i32 metric = i32(i64(f64(population) * (f64(adjusted) * 0.00392156862745098) * (f64(u32(second)) * 0.5 + 1.0) *
        (f64(u32(first)) * 0.25 + 1.0) * 0.01 / f64(u32(count))));
    if (u32(metric) > 400)
      metric = 400;
    {
      ScString working, number;
      {
        CtMiscTextResource resource(kRsCityServiceBoatsBerthedText, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(245, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(metric, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcMayorsHouse::OcMayorsHouse() {
}

// Retire the mayor receiver through its actual special-building base and owned name.
OcMayorsHouse::~OcMayorsHouse() = default;

bool OcMayorsHouse::IsNameChangeable() {
  return true;
}

bool OcMayorsHouse::HasBirthdate() const {
  return true;
}

bool OcMayorsHouse::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcMayorsHouse::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Add the mayor's localized rating-derived number before the special-building birthdate and ordinary query rows.
u8 OcMayorsHouse::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    ScString working, number;
    CtLayer *layer = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
    u8 count = u8((255 - i32(aura->GetMayorRating())) / 20);
    aura->Release();
    {
      CtMiscTextResource resource(kRsCityServiceNumberOfEggsThrownText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(249, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(count, &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcPark::OcPark() {
}

// Retire the park through its real special-building base, including the optional owned name and building fields.
OcPark::~OcPark() = default;

bool OcPark::IsNameChangeable() {
  return true;
}

// Report weighted park area and the original valve/footprint-derived amount before ordinary special-building rows.
u8 OcPark::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (!query)
    return OcMiscBuilding::QueryTile(message, query);
  ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
  i32 count = 0;
  const u32 kinds[6] = {14311, 14312, 14313, 14314, 14315, 14316}, areas[6] = {1, 9, 16, 4, 4, 1};
  for (u32 index = 0; index < 6; ++index) {
    i32 tracked = buildings->CheckOccupantTracking(kinds[index]);
    if (tracked > 0)
      count = i32(u32(count) + u32(tracked) * areas[index]);
  }
  if (!count)
    return 0;
  f32 amount = 0.0f;
  CtOrdinanceLayer *ordinances = ScGetMiscCity()->OrdinanceLayer();
  CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
  CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *valves = nullptr;
  EcValve *population_valve = nullptr, *workforce_valve = nullptr;
  if (!common->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves)) ||
      !valves->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&population_valve)) ||
      !valves->GetValvePointer(7000, IID_EcValve, reinterpret_cast<void **>(&workforce_valve)))
    return 0;
  u32 population = u32(population_valve->QuerySupplyValue());
  if (population) {
    f64 biased = f64(population) * f64(residential->GetWorkforcePopPct()) + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &biased, sizeof(bits));
    u32 needed = u32(bits) + kScDoubleIntegerLowWordBias, current = u32(workforce_valve->QueryValveValue());
    if (current >= needed) {
      amount = 1.0f;
    } else {
      f32 ratio = f32(f64(needed - current) / f64(count));
      if (ratio > 5.0f)
        ratio = 5.0f;
      GeAttrib *attrib = cached_attributes;
      u32 depth = attrib->SizeInCellsZ(), width = attrib->SizeInCellsX(), area = depth * width;
      amount = f32(f64(area) * f64(ratio));
    }
    if (ordinances->QueryOrdinanceOn(kCtOrdinance_HomelessShelters))
      amount *= 0.5f;
  }
  valves->Release();
  population_valve->Release();
  // The original leaves the second valve hold acquired, including after a successful report.
  {
    ScString working, number;
    for (u32 row = 0; row < 2; ++row) {
      {
        CtMiscTextResource resource(234 + 2 * row, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(235 + 2 * row, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(row ? i32(i64(amount)) : count, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcPerformingArtsCenter::OcPerformingArtsCenter() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcPerformingArtsCenter::~OcPerformingArtsCenter() = default;

bool OcPerformingArtsCenter::IsNameChangeable() {
  return true;
}

bool OcPerformingArtsCenter::HasBirthdate() const {
  return true;
}

bool OcPerformingArtsCenter::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcPerformingArtsCenter::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Select the original month/year arts tip, substituting the live city name in the two named variants.
u8 OcPerformingArtsCenter::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = ((month + 12 * year) & 7) + 343;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceNowAppearingText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(tip, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    if (tip == 349 || tip == 350) {
      ScString city_name(*ScGetMiscCity()->GetCityName());
      working.Sprintf(working.Data(), city_name.Data());
    }
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcStadium::OcStadium() : team_name("") {
}

OcStadium::~OcStadium() = default;

// Refuse a missing descriptor before inherited initialization, then read the saved team String through its real key.
// The descriptor getter result is ignored after the inherited building initialization succeeds.
bool OcStadium::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcBuilding::Init(city, info))
    return false;
  info->GetStringData(kOcStadiumTeamNameKey, &team_name);
  return true;
}

// Preserve inherited building persistence before writing the team String; the original ignores the field setter result.
bool OcStadium::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcBuilding::Save(city, info))
    return false;
  info->SetStringData(kOcStadiumTeamNameKey, &team_name);
  return true;
}

bool OcStadium::HasBirthdate() const {
  return true;
}

bool OcStadium::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the input date to its original representation before copying its day word into the stadium's field.
bool OcStadium::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Populate the paired team-name editor from the original localized label before the inherited query presentation.
u8 OcStadium::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString label;
  {
    CtMiscTextResource resource(kRsCityServiceTeamNameText, kRsCityServiceTextGroup);
    ScString text = resource.GetText(), copy(text);
    label = copy;
  }
  query->SetStatusPair(&label, &team_name);
  return OcMiscAnimBuilding::QueryBasic(message, query);
}

u8 OcStadium::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  {
    ScString copy(*query->GetAlternateStatusText());
    team_name = copy;
  }
  return OcMiscAnimBuilding::QueryResponse(message, query);
}

// Combine the unsigned land-value byte and signed mayor rating before reporting the original score and receipt rows.
u8 OcStadium::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    CtLandValueLayer *land_value = ScGetMiscCity()->LandValueLayer();
    CtAuraLayer *aura;
    ScGetMiscCity()
        ->GetSpecificLayer(kCtAuraLayerType)
        ->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
    i8 mayor = aura->GetMayorRating();
    aura->Release();
    u8 land;
    land_value->GetValue(position & 0x7ff, (position >> 11) & 0x7ff, &land);
    i32 score = (i32(land) + i32(mayor) + 128) / 20, receipts = 2000 * score;
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceWinLossRecordText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceWinLossRecordFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    working.Sprintf(working.Data(), score, 25 - score);
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    {
      CtMiscTextResource resource(kRsCityServiceSeasonTicketHoldersText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(275, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(i64(receipts), &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscAnimBuilding::QueryTile(message, query);
}

OcStatue::OcStatue() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcStatue::~OcStatue() = default;

bool OcStatue::IsNameChangeable() {
  return true;
}

bool OcStatue::HasBirthdate() const {
  return true;
}

bool OcStatue::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcStatue::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report the original tracked-building count before the stored birthdate and ordinary query rows.
u8 OcStatue::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    i32 count = 0;
    if (buildings->CheckOccupantTracking(13000) >= 0)
      count = buildings->CheckOccupantTracking(13000);
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceNumberOfPigeonsText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(253, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(count, &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcStockExchange::OcStockExchange() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcStockExchange::~OcStockExchange() = default;

bool OcStockExchange::IsNameChangeable() {
  return true;
}

bool OcStockExchange::HasBirthdate() const {
  return true;
}

bool OcStockExchange::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcStockExchange::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Preserve the original language-utility gate and temporary String lifetimes before ordinary dated-building rows.
u8 OcStockExchange::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    if (!ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent))
      return 0;
    ScString working, number;
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcThemePark::OcThemePark() : occupancy_updating(0) {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcThemePark::~OcThemePark() = default;

bool OcThemePark::HasBirthdate() const {
  return true;
}

bool OcThemePark::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcThemePark::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

u32 OcThemePark::GetGeomFlag() {
  return kBdLandmarkBuildingGeometry;
}

// Propagate occupancy to matching neighboring buildings while the receiver's byte prevents recursive reentry.
// Preserve the exclusive upper map bounds, footprint test and interface-release ordering of the installed method.
void OcThemePark::SetOccupancyState(u32 value) {
  if (occupancy_updating)
    return;
  occupancy_updating = 1;
  u32 type = static_cast<CtAgent *>(this)->TypeId();
  i32 width = SizeInCellsX(), depth = SizeInCellsZ();
  CtCity *city = ScGetMiscCity();
  ScOccupantManager *manager = city->SurfaceOccupantManager();
  i32 max_x = i32(city->CellCountX() - 1), max_z = i32(city->CellCountZ() - 1);
  u32 location_x, location_z;
  GetLocationInCellCoords(&location_x, &location_z);
  i32 origin_x = i32(location_x), origin_z = i32(location_z);
  i32 footprint_x = origin_x + width, footprint_z = origin_z + depth;
  i32 first_x = origin_x - 2 * width, first_z = origin_z - 2 * depth;
  i32 last_x = origin_x + 2 * width, last_z = origin_z + 2 * depth;
  if (first_x < 0)
    first_x = 0;
  if (first_z < 0)
    first_z = 0;
  if (last_x > max_x)
    last_x = max_x;
  if (last_z > max_z)
    last_z = max_z;
  for (i32 x = first_x; x < last_x; ++x) {
    for (i32 z = first_z; z < last_z; ++z) {
      if (x < origin_x || z < origin_z || x >= footprint_x || z >= footprint_z) {
        ScOccupant *occupant = nullptr;
        if (manager->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
          if (occupant->TypeId() == type) {
            BdBuilding *building = nullptr;
            if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
              if (building->GetOccupancyState() != value)
                building->SetOccupancyState(value);
              building->Release();
            }
          }
          occupant->Release();
        }
      } else if (GetOccupancyState() != value) {
        OcMiscAnimBuilding::SetOccupancyState(value);
      }
    }
  }
  occupancy_updating = 0;
}

// Select the original year-dependent theme-park tip before the animated special-building date and ordinary rows.
u8 OcThemePark::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = year % 9 + 352;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceFavoriteAttractionText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(tip, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscAnimBuilding::QueryTile(message, query);
}

OcZoo::OcZoo() {
}

// Retire the zoo through the actual special-building destructor and its owned name.
OcZoo::~OcZoo() = default;

bool OcZoo::IsNameChangeable() {
  return true;
}

// Compute the two zoo metrics from live tracking, population, aura and ordinance state before ordinary query rows.
u8 OcZoo::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    if (buildings->CheckOccupantTracking(14450) < 0)
      return 0;
    i32 count = buildings->CheckOccupantTracking(14450);
    if (!count)
      return 0;
    i32 museums = 0;
    if (buildings->CheckOccupantTracking(12851) >= 0)
      museums = buildings->CheckOccupantTracking(12851);
    u32 population = u32(ScGetMiscCity()->ResidentialLayer()->GetPopulation());
    CtOrdinanceLayer *ordinances = ScGetMiscCity()->OrdinanceLayer();
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    if (!common->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
      return 0;
    // Zero native output excludes an unwritten result from an out-of-contract map lookup.
    i8 local = 0;
    aura->GetValue(position & 0x7ff, (position >> 11) & 0x7ff, &local);
    i8 mayor = aura->GetMayorRating();
    i32 sum = i32(mayor) + i32(local) + 255;
    i32 first = i32(i64(f64(sum) * 0.001953125 * f64(population) * 0.1 / f64(u32(count))));
    bool on = ordinances->QueryOrdinanceOn(kCtOrdinance_IndustrialWasteDisposal);
    i32 adjusted = i32(local) + 128;
    i32 second = i32(i64((f64(on) * 0.5 + 1.0) * (f64(adjusted) * 0.25) * (f64(u32(museums)) * 0.25 + 1.0)));
    aura->Release();
    {
      ScString working, number;
      for (u32 row = 0; row < 2; ++row) {
        {
          CtMiscTextResource resource(238 + 2 * row, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        label.Copy(working);
        {
          CtMiscTextResource resource(239 + 2 * row, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        utility->MakeNumberString(row ? second : first, &number);
        working.Sprintf(working.Data(), number.Data());
        value.Copy(working);
        query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
      }
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}
