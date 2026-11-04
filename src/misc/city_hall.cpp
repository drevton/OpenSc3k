#include "misc/city_hall.h"

#include <stdio.h>

#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "misc/api.h"
#include "misc/module.h"
#include "resources/resource.h"
#include "ui/api.h"

// City-hall placeables return the original civic building geometry word.
enum BdCityHallGeometry : u32 {
  kBdCityHallBuildingGeometry = 0x40000104,
};

OcAirport::OcAirport() {
}

// Retire the port through its real base, including the optional name and retained building fields.
OcAirport::~OcAirport() = default;

// Report the original supply/capacity state and rating-derived metric before ordinary special-building rows.
u8 OcAirport::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kEcValveLayerType);
    EcValveLayer *valves = nullptr;
    EcValve *supply_valve = nullptr;
    if (!common->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves)) ||
        !valves->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&supply_valve)))
      return 0;
    u32 supply = u32(supply_valve->QuerySupplyValue());
    valves->Release();
    supply_valve->Release();
    u32 count = u32(buildings->CheckOccupantTracking(12310));
    if (!count)
      return 0;
    f32 ratio = f32(f64(supply) / (f64(count) * 4000.0));
    common = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    if (!common->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
      return 0;
    i32 adjusted = i32(aura->GetMayorRating()) + 128;
    i32 metric = i32(i64(f64(adjusted) * 0.00392156862745098 * (1.0 / f64(ratio)) * 100.0));
    if (u32(metric) > 98)
      metric = 98;
    aura->Release();
    {
      ScString working, number;
      for (u32 row = 0; row < 2; ++row) {
        {
          CtMiscTextResource resource(254 + row * kRsCityServiceRatingRowStride, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        label.Copy(working);
        u32 resource_id = 260;
        u8 warning = 0;
        if (!row) {
          if (f64(ratio) < 0.8)
            resource_id = 255;
          else if (f64(ratio) < 1.1)
            resource_id = 256;
          else if (f64(ratio) < 1.5)
            resource_id = 257;
          else {
            resource_id = 258;
            warning = 1;
          }
        }
        {
          CtMiscTextResource resource(resource_id, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        if (row)
          working.Sprintf(working.Data(), metric);
        value.Copy(working);
        query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, warning);
      }
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcCityHall::OcCityHall() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcCityHall::~OcCityHall() = default;

bool OcCityHall::IsNameChangeable() {
  return true;
}

bool OcCityHall::HasBirthdate() const {
  return true;
}

bool OcCityHall::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcCityHall::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report enabled ordinances before the stored birthdate and ordinary special-building query rows.
u8 OcCityHall::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    CtOrdinanceLayer *ordinances = ScGetMiscCity()->OrdinanceLayer();
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceMilesOfRedTapeText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(251, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(i32(ordinances->TotalOrdinancesOn()), &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcCourthouse::OcCourthouse() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcCourthouse::~OcCourthouse() = default;

bool OcCourthouse::IsNameChangeable() {
  return true;
}

bool OcCourthouse::HasBirthdate() const {
  return true;
}

bool OcCourthouse::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcCourthouse::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Add the original population/education and population/crime metrics before the dated special-building rows.
u8 OcCourthouse::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    f64 education = f64(residential->GetWorkforceEQ());
    u32 population = u32(residential->GetPopulation());
    i32 first = i32(i64(f64(population) * education * 0.00005));
    CtCrimeLayer *crime = ScGetMiscCity()->CrimeLayer();
    f64 crime_rating = f64(crime->GetAverageValue());
    u32 next_population = u32(residential->GetPopulation());
    i32 second = i32(i64(f64(next_population) * crime_rating * 0.0001));
    ScString working, number;
    for (u32 row = 0; row < 2; ++row) {
      {
        CtMiscTextResource resource(290 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      label.Copy(working);
      {
        CtMiscTextResource resource(291 + row * kRsCityServiceLabelValueRowStride, kRsCityServiceTextGroup);
        ScString text = resource.GetText(), copy(text);
        working = copy;
      }
      utility->MakeNumberString(row ? second : first, &number);
      working.Sprintf(working.Data(), number.Data());
      value.Copy(working);
      query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcDefenseContractor::OcDefenseContractor() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcDefenseContractor::~OcDefenseContractor() = default;

bool OcDefenseContractor::IsNameChangeable() {
  return true;
}

bool OcDefenseContractor::HasBirthdate() const {
  return true;
}

bool OcDefenseContractor::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcDefenseContractor::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Select the original year-dependent defense tip before the special-building date and ordinary rows.
u8 OcDefenseContractor::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = year % 10 + 320;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceCurrentProjectText, kRsCityServiceTextGroup);
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
  return OcMiscBuilding::QueryTile(message, query);
}

OcMedicalResearchCenter::OcMedicalResearchCenter() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcMedicalResearchCenter::~OcMedicalResearchCenter() = default;

bool OcMedicalResearchCenter::IsNameChangeable() {
  return true;
}

bool OcMedicalResearchCenter::HasBirthdate() const {
  return true;
}

bool OcMedicalResearchCenter::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcMedicalResearchCenter::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Report the original education-weighted tracked-building metric before the birthdate and ordinary query rows.
u8 OcMedicalResearchCenter::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    i32 count = 0;
    if (buildings->CheckOccupantTracking(13310) >= 0)
      count = buildings->CheckOccupantTracking(13310);
    u8 education = residential->GetWorkforceEQ();
    i32 product = i32(u32(count) * u32(education));
    i32 metric = i32(i64(f64(product) * 0.1));
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceResearchersText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceResearchersFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeNumberString(metric, &number);
    working.Sprintf(working.Data(), number.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcMilitaryBase::OcMilitaryBase() : occupancy_updating(0) {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcMilitaryBase::~OcMilitaryBase() = default;

bool OcMilitaryBase::HasBirthdate() const {
  return true;
}

bool OcMilitaryBase::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcMilitaryBase::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

u32 OcMilitaryBase::GetGeomFlag() {
  return kBdCityHallBuildingGeometry;
}

// Propagate occupancy to matching neighboring buildings while the receiver's byte prevents recursive reentry.
// Preserve the exclusive upper map bounds, footprint test and interface-release ordering of the installed method.
void OcMilitaryBase::SetOccupancyState(u32 value) {
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

// Report the original population/cohort longevity metric before animated birthdate and ordinary building rows.
u8 OcMilitaryBase::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    CtResidentialLayer *residential = ScGetMiscCity()->ResidentialLayer();
    f32 longevity = f32(residential->GetAgeCohortLE(18, 25));
    f32 weighted = f32(f64(residential->GetAgeCohortPopPct(18, 25)) * f64(longevity));
    u32 population = u32(residential->GetPopulation());
    i32 metric = i32(i64(f64(population) * f64(weighted) * 0.001));
    ScString working, number;
    {
      CtMiscTextResource resource(kRsCityServiceNewRecruitsText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(289, kRsCityServiceTextGroup);
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

OcPrison::OcPrison() {
}

OcPrison::~OcPrison() = default;

// Report the original scaled world population before the business-deal date and operational-income rows.
u8 OcPrison::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    ScString number;
    CtWorldLayer *world = ScGetMiscCity()->WorldLayer();
    u32 population = world->GetWorldPopulation();
    {
      CtMiscTextResource resource(kRsCityServiceLicensePlatesProducedText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      label = copy;
    }
    {
      CtMiscTextResource resource(285, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      value = copy;
    }
    i32 metric = i32(i64(f64(population) * 0.001));
    utility->MakeNumberString(metric, &number);
    value.Sprintf(value.Data(), number.Data());
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcDealBuilding::QueryTile(message, query);
}

OcScienceCenter::OcScienceCenter() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcScienceCenter::~OcScienceCenter() = default;

bool OcScienceCenter::IsNameChangeable() {
  return true;
}

bool OcScienceCenter::HasBirthdate() const {
  return true;
}

bool OcScienceCenter::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcScienceCenter::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Select the original year-dependent science tip before the animated special-building date and ordinary rows.
u8 OcScienceCenter::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = year % 11 + 331;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceLatestBreakthroughText, kRsCityServiceTextGroup);
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

OcSeaport::OcSeaport() {
}

// Retire the port through its real base, including the optional name and retained building fields.
OcSeaport::~OcSeaport() = default;

// Report the original supply/capacity state and rating-derived metric before ordinary special-building rows.
u8 OcSeaport::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
    CtLayer *common = ScGetMiscCity()->GetSpecificLayer(kEcValveLayerType);
    EcValveLayer *valves = nullptr;
    EcValve *supply_valve = nullptr;
    if (!common->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves)) ||
        !valves->GetValvePointer(8000, IID_EcValve, reinterpret_cast<void **>(&supply_valve)))
      return 0;
    u32 supply = u32(supply_valve->QuerySupplyValue());
    valves->Release();
    supply_valve->Release();
    u32 count = u32(buildings->CheckOccupantTracking(12330));
    if (!count)
      return 0;
    f32 ratio = f32(f64(supply) / (f64(count) * 4000.0));
    common = ScGetMiscCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    if (!common->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
      return 0;
    i32 adjusted = i32(aura->GetMayorRating()) + 128;
    i32 metric = i32(i64((1.0 - f64(adjusted) * 0.00392156862745098) * f64(ratio) * 1000000.0));
    aura->Release();
    {
      ScString working, number;
      for (u32 row = 0; row < 2; ++row) {
        {
          CtMiscTextResource resource(261 + row * kRsCityServiceRatingRowStride, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        label.Copy(working);
        u32 resource_id = 267;
        u8 warning = 0;
        if (!row) {
          if (f64(ratio) < 0.8)
            resource_id = 262;
          else if (f64(ratio) < 1.1)
            resource_id = 263;
          else if (f64(ratio) < 1.5)
            resource_id = 264;
          else {
            resource_id = 265;
            warning = 1;
          }
        }
        {
          CtMiscTextResource resource(resource_id, kRsCityServiceTextGroup);
          ScString text = resource.GetText(), copy(text);
          working = copy;
        }
        if (row) {
          utility->MakeNumberString(metric, &number);
          working.Sprintf(working.Data(), number.Data());
        }
        value.Copy(working);
        query->AddText(
            &label, &value, row ? kUiQuerySecondaryDetailsCategory : kUiQueryPrimaryDetailsCategory, warning);
      }
    }
  }
  return OcMiscBuilding::QueryTile(message, query);
}

OcSpaceport::OcSpaceport() {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcSpaceport::~OcSpaceport() = default;

bool OcSpaceport::IsNameChangeable() {
  return true;
}

bool OcSpaceport::HasBirthdate() const {
  return true;
}

bool OcSpaceport::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcSpaceport::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

// Select the original month/year-dependent tip before the animated special-building birthdate and ordinary rows.
u8 OcSpaceport::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    // Defined city calendar outputs are required; initialization excludes unwritten external output reads.
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = (month + 12 * year) % 10 + 309;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceNextShuttleToText, kRsCityServiceTextGroup);
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

OcToxicWastePlant::OcToxicWastePlant() {
}

OcToxicWastePlant::~OcToxicWastePlant() = default;

// Select the original year-dependent waste tip before the animated business-deal date and operational-income rows.
u8 OcToxicWastePlant::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = year % 10 + 362;
    {
      CtMiscTextResource resource(kRsCityServiceCurrentConversionProjectText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      label = copy;
    }
    {
      CtMiscTextResource resource(tip, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      value = copy;
    }
    query->AddText(&label, &value, kUiQuerySecondaryDetailsCategory, 0);
  }
  return OcDealAnimBuilding::QueryTile(message, query);
}

OcUniversity::OcUniversity() : occupancy_updating(0) {
}

// Retire the dated building receiver through its actual special-building base and owned name.
OcUniversity::~OcUniversity() = default;

bool OcUniversity::HasBirthdate() const {
  return true;
}

bool OcUniversity::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface to its original value representation, then copy only the day word into the field.
bool OcUniversity::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

u32 OcUniversity::GetGeomFlag() {
  return kBdCityHallBuildingGeometry;
}

// Propagate occupancy to matching neighboring buildings while the receiver's byte prevents recursive reentry.
// Preserve the exclusive upper map bounds, footprint test and interface-release ordering of the installed method.
void OcUniversity::SetOccupancyState(u32 value) {
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

// Select the original year-dependent university tip before the animated special-building date and ordinary rows.
u8 OcUniversity::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  if (query) {
    u32 month = 0, day = 0, year = 0;
    ScGetMiscCity()->GetDate(&month, &day, &year);
    u32 tip = (month + 12 * year) % 10 + 373;
    ScString working, unused_number;
    {
      CtMiscTextResource resource(kRsCityServiceMostPopularCourseText, kRsCityServiceTextGroup);
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
