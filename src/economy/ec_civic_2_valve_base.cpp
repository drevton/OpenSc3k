#include "economy/ec_civic.h"
#include <math.h>
#include <string.h>
#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_text.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// For inactive buildings, add the localized footprint cost using the live budget and language utility.
// Preserve the early refusal when no language utility exists, then delegate to the occupant's architect row.
u8 EcCivicBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    EcBudgetLayer *budget = EcGetRciCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label, formatted, number;
      {
        EcRci::ScTextResource resource(kRsCityServiceCleanUpCostText, kRsCityServiceTextGroup);
        label = resource.GetText();
      }
      {
        EcRci::ScTextResource resource(kRsCityServiceCleanUpCostFormatText, kRsCityServiceTextGroup);
        formatted = resource.GetText();
      }
      utility->MakeMoneyString(i32(amount), &number);
      EcFormatText(&formatted, formatted.Data(), number.Data());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return EcCivicOccupant::QueryTile(message, query);
}

// Select the first matching occupancy status, then add the checked option with the live window-resource font.
// Delegate to the original empty occupant query after the status and option callbacks.
u8 EcCivicBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
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
    {
      EcRci::ScTextResource resource(instance, kRsCityServiceTextGroup);
      status = resource.GetText();
    }
    query->SetStatusText(status);
  }
  ScString caption;
  {
    EcRci::ScTextResource resource(kRsCityServiceMakeHistoricalText, kRsCityServiceTextGroup);
    caption = resource.GetText();
  }
  WnResources *resources = EcGetRciWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return EcCivicOccupant::QueryBasic(message, query);
}

// Publish the localized strike status when the corresponding city service is striking, then add the common building rows.
u8 EcSchool::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (layer && layer->SchoolIsOnStrike()) {
    {
      EcRci::ScTextResource resource(kRsCityServiceWorkersOnStrikeText, kRsCityServiceTextGroup);
      status = resource.GetText();
    }
    query->SetStatusText(status);
  }
  return EcCivicBuilding::QueryBasic(message, query);
}

// Publish the localized strike status when the corresponding city service is striking, then add the common building rows.
u8 EcCollege::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (layer && layer->CollegeIsOnStrike()) {
    {
      EcRci::ScTextResource resource(kRsCityServiceWorkersOnStrikeText, kRsCityServiceTextGroup);
      status = resource.GetText();
    }
    query->SetStatusText(status);
  }
  return EcCivicBuilding::QueryBasic(message, query);
}

// Publish the localized strike status when the corresponding city service is striking, then add the common building rows.
u8 EcHospital::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (layer && layer->HealthIsOnStrike()) {
    {
      EcRci::ScTextResource resource(kRsCityServiceWorkersOnStrikeText, kRsCityServiceTextGroup);
      status = resource.GetText();
    }
    query->SetStatusText(status);
  }
  return EcCivicBuilding::QueryBasic(message, query);
}

// Display library upkeep, estimated readership and the funding-dependent service rating.
// The residential and language services are borrowed in the same order as the original query.
u8 EcLibrary::QueryTile(ScMessage *message, UiQuery *query) {
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (!layer)
    return 0;
  ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  u32 day, year;
  EcGetRciCity()->GetDate(&day, &year);
  if (query) {
    ScString label, format, text, number;
    {
      EcRci::ScTextResource resource(kRsCityServiceFundingCostText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceMonthlyValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeMoneyString(i32(funded_upkeep), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryMonthlyCostCategory, 0);
    {
      EcRci::ScTextResource resource(kRsCityServiceBooksSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceLibraryBookCountFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    u32 estimate = 19u * layer->GetWorkforceEQ();
    estimate += 10u * u32(layer->GetLibrarySystemCapacity());
    estimate += 13u * layer->GetSchoolSystemRating() + year % 17u - 52u;
    utility->MakeNumberString(i64(estimate), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    {
      EcRci::ScTextResource resource(kRsCityServiceBooksInCirculationText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceLibraryReaderCountFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    u64 readers = (u64(layer->GetWorkforceEQ()) * estimate) / 303u + day % 17u;
    u32 capacity = u32(layer->GetPopulation());
    if (readers > capacity)
      readers = u32(layer->GetPopulation());
    utility->MakeNumberString(i64(readers), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    capacity = u32(layer->GetPopulation());
    u32 occupancy = capacity ? (100u * u32(layer->GetLibrarySystemCapacity())) / capacity : 100u;
    if (occupancy > 100)
      occupancy = 100;
    u64 rating = (u64(funded_upkeep) * occupancy) / optimal_upkeep;
    {
      EcRci::ScTextResource resource(kRsCityServiceLibraryGradeLabelText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    u32 resource_id = rating > 87 ? 217 : rating > 71 ? 218 : rating > 63 ? 219 : rating > 46 ? 220 : 221;
    {
      EcRci::ScTextResource resource(resource_id, kRsCityServiceTextGroup);
      text = resource.GetText();
    }
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, resource_id >= 220);
  }
  return EcCivicBuilding::QueryTile(message, query);
}

// Show the citywide school capacity, attendance, staff, rating, upkeep and local education benefits.
// City layers and localized text are consulted in the original callback order.
u8 EcSchool::QueryTile(ScMessage *message, UiQuery *query) {
  ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (!layer)
    return 0;
  if (query) {
    ScString label, format, text, number;
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfDesksSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceSchoolCapacityFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(layer->GetSchoolSystemCapacity(), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    i32 students = layer->GetSchoolStudentCount();
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfStudentsSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceSchoolStudentsFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(students, &number);
    EcFormatText(&text, format.Data(), number.Data());
    u8 warning = u32(students) > u32(layer->GetSchoolSystemCapacity());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
    i32 teachers = layer->GetTeacherCount();
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfTeachersSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceSchoolTeachersFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(teachers, &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    u32 rating = layer->GetSchoolSystemRating();
    {
      EcRci::ScTextResource resource(kRsCityServiceSchoolGradeLabelText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    u32 rating_text = rating == 4 ? 186 : rating >= 3 ? 187 : rating == 2 ? 188 : rating == 1 ? 189 : 190;
    {
      EcRci::ScTextResource resource(rating_text, kRsCityServiceTextGroup);
      text = resource.GetText();
    }
    warning = rating < 2;
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
    {
      EcRci::ScTextResource resource(kRsCityServiceFundingCostText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceMonthlyValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeMoneyString(i32(funded_upkeep), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryMonthlyCostCategory, 0);
    CtLayer *aura_layer = EcGetRciCity()->GetSpecificLayer(kCtAuraLayerType);
    CtAuraLayer *aura = nullptr;
    aura_layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
    i8 aura_value = 0;
    aura->GetValue(
        position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask, &aura_value);
    u32 benefit = aura_value > 0 ? u32(aura_value) / 10u : 0;
    aura->Release();
    CtOrdinanceLayer *ordinances = EcGetRciCity()->OrdinanceLayer();
    if (ordinances->QueryOrdinanceOn(kCtOrdinance_ProReading))
      benefit += 3;
    if (ordinances->QueryOrdinanceOn(kCtOrdinance_JuniorSports))
      benefit += 2;
    {
      EcRci::ScTextResource resource(kRsCityServiceApplesForTheTeacherText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceEducationBenefitFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(benefit, &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQuerySecondaryDetailsCategory, warning);
  }
  return EcCivicBuilding::QueryTile(message, query);
}

// Present the current college capacity, enrollment, professors, service rating and funded upkeep.
u8 EcCollege::QueryTile(ScMessage *message, UiQuery *query) {
  ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (!layer)
    return 0;
  if (query) {
    ScString label, format, text, number;
    {
      EcRci::ScTextResource resource(kRsCityServiceCapacityText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceCollegeCapacityFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(layer->GetCollegeSystemCapacity(), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    i32 students = layer->GetCollegeStudentCount();
    {
      EcRci::ScTextResource resource(kRsCityServiceStudentsInAttendanceText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceCollegeStudentsFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(students, &number);
    EcFormatText(&text, format.Data(), number.Data());
    u8 warning = u32(students) > u32(layer->GetCollegeSystemCapacity());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
    i32 professors = layer->GetProfessorCount();
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfProfessorsText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceCollegeProfessorsFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(professors, &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    u32 rating = layer->GetCollegeSystemRating();
    {
      EcRci::ScTextResource resource(kRsCityServiceCollegeGradeLabelText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    u32 rating_text = rating == 4 ? 207 : rating >= 3 ? 208 : rating == 2 ? 209 : rating == 1 ? 210 : 211;
    {
      EcRci::ScTextResource resource(rating_text, kRsCityServiceTextGroup);
      text = resource.GetText();
    }
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, rating < 2);
    {
      EcRci::ScTextResource resource(kRsCityServiceFundingCostText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceMonthlyValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeMoneyString(i32(funded_upkeep), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryMonthlyCostCategory, 0);
  }
  return EcCivicBuilding::QueryTile(message, query);
}

// Show funded hospital upkeep, doctors, patients, capacity and the city health rating when both services exist.
u8 EcHospital::QueryTile(ScMessage *message, UiQuery *query) {
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (query && utility && layer) {
    ScString label, format, text, number;
    {
      EcRci::ScTextResource resource(kRsCityServiceFundingCostText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceMonthlyValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeMoneyString(i32(funded_upkeep), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    i32 doctor_count = layer->GetDoctorCount();
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfDoctorsSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceDoctorsFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(doctor_count, &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfPatientsSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServicePatientsFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(layer->GetPatientCount(), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    {
      EcRci::ScTextResource resource(kRsCityServiceNumberOfBedsSystemwideText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceHospitalCapacityFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeNumberString(layer->GetHealthSystemCapacity(), &number);
    EcFormatText(&text, format.Data(), number.Data());
    u32 capacity = u32(layer->GetHealthSystemCapacity());
    u8 warning = u32(layer->GetPatientCount()) > capacity;
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
    u32 rating = layer->GetHealthSystemRating();
    {
      EcRci::ScTextResource resource(kRsCityServiceHospitalGradeLabelText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    u32 rating_text = rating == 4 ? 174 : rating >= 3 ? 175 : rating == 2 ? 176 : rating == 1 ? 177 : 178;
    {
      EcRci::ScTextResource resource(rating_text, kRsCityServiceTextGroup);
      text = resource.GetText();
    }
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, rating < 2);
  }
  return EcCivicBuilding::QueryTile(message, query);
}

// Show citywide museum demand, funding quality, upkeep and the world-population-derived attendance row.
u8 EcMuseum::QueryTile(ScMessage *message, UiQuery *query) {
  CtResidentialLayer *layer = EcGetRciCity()->ResidentialLayer();
  if (!layer)
    return 0;
  ScLanguageUtility *utility = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  u32 month, day, year;
  EcGetRciCity()->GetDate(&month, &day, &year);
  u32 population = u32(layer->GetPopulation());
  if (!population)
    population = 1;
  if (query) {
    ScString label, format, text, number;
    {
      EcRci::ScTextResource resource(kRsCityServiceAttendanceText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceLastYearValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    u32 capacity = u32(layer->GetMuseumSystemCapacity());
    u32 effective = optimal_capacity;
    if (capacity > population)
      effective = (population * optimal_capacity) / (u32(layer->GetMuseumSystemCapacity()) + 1u);
    utility->MakeNumberString(i64(effective), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, 0);
    u32 occupancy = (100u * u32(layer->GetMuseumSystemCapacity())) / population;
    if (occupancy > 100)
      occupancy = 100;
    u64 rating = (u64(funded_upkeep) * occupancy) / optimal_upkeep;
    {
      EcRci::ScTextResource resource(kRsCityServiceMuseumGradeLabelText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    u32 rating_text = rating > 83 ? 229 : rating > 65 ? 230 : rating > 51 ? 231 : rating > 39 ? 232 : 233;
    {
      EcRci::ScTextResource resource(rating_text, kRsCityServiceTextGroup);
      text = resource.GetText();
    }
    query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, rating_text >= 232);
    {
      EcRci::ScTextResource resource(kRsCityServiceFundingCostText, kRsCityServiceTextGroup);
      label = resource.GetText();
    }
    {
      EcRci::ScTextResource resource(kRsCityServiceMonthlyValueFormatText, kRsCityServiceTextGroup);
      format = resource.GetText();
    }
    utility->MakeMoneyString(i32(funded_upkeep), &number);
    EcFormatText(&text, format.Data(), number.Data());
    query->AddText(&label, &text, kUiQueryMonthlyCostCategory, 0);
    CtWorldLayer *world = EcGetRciCity()->WorldLayer();
    if (world) {
      {
        EcRci::ScTextResource resource(kRsCityServiceDinosaurBonesText, kRsCityServiceTextGroup);
        label = resource.GetText();
      }
      {
        EcRci::ScTextResource resource(kRsCityServiceDinosaurBonesFormatText, kRsCityServiceTextGroup);
        format = resource.GetText();
      }
      u32 attendance = world->GetWorldPopulation() % 53u - year % 3u;
      utility->MakeNumberString(i64(attendance), &number);
      EcFormatText(&text, format.Data(), number.Data());
      query->AddText(&label, &text, kUiQuerySecondaryDetailsCategory, 0);
    }
  }
  return EcCivicBuilding::QueryTile(message, query);
}

// Read the original occupant key and state in stream order, preserving virtual setters and ignored primitive replies.
// Initialize output staging only where the original could otherwise read an unwritten value.
u8 EcCivicOccupant::ReadRecord(CtCity *, RsStream *record) {
  RsKey key{};
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 type = 0;
  u8 major = 0, animation = 0, size_x = 0, size_z = 0, size_y = 0;
  u8 subtype = 0, mark = 0, membership = 0, sim_remove = 0, user_remove = 0;
  record->ReadU32(&type);
  record->ReadU8(&major);
  record->ReadU8(&animation);
  record->ReadU8(&size_x);
  record->ReadU8(&size_z);
  record->ReadU8(&size_y);
  record->ReadU8(&subtype);
  SetSubTypeInfo(subtype);
  record->ReadU8(&mark);
  SetMarkBit(mark != 0);
  record->ReadU8(&membership);
  SetIsInCityFlag(membership);
  record->ReadU8(&sim_remove);
  SetSimRemove(sim_remove != 0);
  record->ReadU8(&user_remove);
  SetUserRemove(user_remove != 0);
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  CtCoord point{i32(x << 8), i32(z << 8), i32(altitude << 8)};
  SetLocation(point);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return 1;
}

// Write the retained attribute key and live virtual occupant values in original order, ignoring primitive write replies.
u8 EcCivicOccupant::WriteRecord(CtCity *, RsStream *record) {
  GeAttrib *value = EcCivicOccupant::GetAttribData();
  void *publication = nullptr;
  if (!value || !value->QueryInterface(IID_RsResource, &publication))
    return 0;
  record->WriteU32(*static_cast<RsResource *>(publication)->Type());
  record->WriteU32(*static_cast<RsResource *>(publication)->Group());
  record->WriteU32(*static_cast<RsResource *>(publication)->Instance());
  static_cast<RsResource *>(publication)->Release();
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
  record->WriteU32(u32(point.x >> 8));
  record->WriteU32(u32(point.z >> 8));
  record->WriteU32(u32(point.altitude >> 8));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return 1;
}

// Read the common occupant record, then publish occupancy even if its stream read refuses the output.
u8 EcCivicBuilding::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicOccupant::ReadRecord(city, record);
  if (!result)
    return result;
  i32 state = i32(u32(uptr(record)));
  result = record->ReadI32(&state);
  if (!state)
    state = 1;
  SetOccupancyState(u32(state));
  return result;
}

// Write the common occupant state before the original raw building occupancy field.
u8 EcCivicBuilding::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicOccupant::WriteRecord(city, record);
  if (result)
    result = record->WriteI32(i32(occupancy_flags));
  return result;
}

// Read the actual common building record, followed by this service's saved fields in order.
u8 EcSchool::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::ReadRecord(city, record);
  result = result && record->ReadU32(&staff) && record->ReadU32(&funded_upkeep);
  return result;
}

// Write the common building record, followed by this service's saved fields in order.
u8 EcSchool::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::WriteRecord(city, record);
  result = result && record->WriteU32(staff) && record->WriteU32(funded_upkeep);
  return result;
}

// Read the actual common building record, followed by this service's saved fields in order.
u8 EcCollege::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::ReadRecord(city, record);
  result = result && record->ReadU32(&staff) && record->ReadF32(&education_effect) && record->ReadU32(&funded_upkeep);
  return result;
}

// Write the common building record, followed by this service's saved fields in order.
u8 EcCollege::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::WriteRecord(city, record);
  result = result && record->WriteU32(staff) && record->WriteF32(education_effect) && record->WriteU32(funded_upkeep);
  return result;
}

// Read the actual common building record, followed by this service's saved fields in order.
u8 EcHospital::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::ReadRecord(city, record);
  result = result && record->ReadU32(&beds) && record->ReadF32(&health_effect) && record->ReadU32(&funded_upkeep);
  return result;
}

// Write the common building record, followed by this service's saved fields in order.
u8 EcHospital::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::WriteRecord(city, record);
  result = result && record->WriteU32(beds) && record->WriteF32(health_effect) && record->WriteU32(funded_upkeep);
  return result;
}

u8 EcLibrary::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::ReadRecord(city, record);
  return result;
}

u8 EcLibrary::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::WriteRecord(city, record);
  return result;
}

u8 EcMuseum::ReadRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::ReadRecord(city, record);
  return result;
}

u8 EcMuseum::WriteRecord(CtCity *city, RsStream *record) {
  u8 result = EcCivicBuilding::WriteRecord(city, record);
  return result;
}

// Initialize the complete scalar valve state without acquiring a city or module ownership reference.
EcValveBase::EcValveBase()
    : references(0), identifier(0), supply(0), demand(0), supply_accumulator(0), demand_accumulator(0),
      initial_supply(0), initial_demand(0), maximum(0), minimum(0), balance(0), flag_30(true), flag_31(true),
      economy_modifier(0), tax_modifier(0), demand_cap(0), city(nullptr) {
}

EcValveBase::~EcValveBase() {
  EcValveBase::Shutdown();
}

// Publish this same valve view for the two supported identities before acquiring its reference.
bool EcValveBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_EcValve, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the module, then return its live post-callback value.
u32 EcValveBase::AddRef() {
  ++references;
  EcGetDirector()->AddRef();
  return references;
}

// Release the module before decrementing a nonzero local count and destroy the valve when the live count reaches zero.
u32 EcValveBase::Release() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Bind the borrowed city and identifier while resetting simulation values and modifiers, preserving flags30/31 ordering.
// The listener is unused by the installed body; reference count and flag31 retain their entry values.
bool EcValveBase::Init(CtCity *owner, u32 event_id, CtLayer *) {
  flag_30 = true;
  city = owner;
  economy_modifier = 0;
  identifier = event_id;
  supply = 0;
  demand = 0;
  tax_modifier = 0;
  supply_accumulator = 0;
  demand_accumulator = 0;
  maximum = 0;
  minimum = 0;
  demand_cap = 0;
  balance = 0;
  initial_supply = 0;
  initial_demand = 0;
  return true;
}

bool EcValveBase::Shutdown() {
  city = nullptr;
  return true;
}

u32 EcValveBase::GetId() {
  return identifier;
}

void EcValveBase::SetId(u32 event_id) {
  identifier = event_id;
}

i32 EcValveBase::QuerySupplyValue() {
  return supply;
}

i32 EcValveBase::QueryDemandValue() {
  return demand;
}

i32 EcValveBase::QueryValveValue() {
  return balance;
}

f32 EcValveBase::QueryEconomyModifier() {
  return economy_modifier;
}

i32 EcValveBase::QueryValveMax() {
  return maximum;
}

i32 EcValveBase::QueryValveMin() {
  return minimum;
}

bool EcValveBase::AddToSupplyValue(i32 amount) {
  supply_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcValveBase::AddToDemandValue(i32 amount) {
  demand_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcValveBase::SetSupplyValue(i32 amount) {
  supply = amount;
  return true;
}

bool EcValveBase::SetDemandValue(i32 amount) {
  demand = amount;
  return true;
}

void EcValveBase::SetValveMax(i32 upper) {
  maximum = upper < minimum ? minimum : upper;
}

void EcValveBase::SetValveMin(i32 lower) {
  minimum = lower > maximum ? maximum : lower;
}

// Remove the previous initial supply contribution before adding and remembering its replacement.
void EcValveBase::SetInitialSupplyValue(i32 amount) {
  supply -= initial_supply;
  supply += amount;
  initial_supply = amount;
}

// Replace the initial demand contribution while retaining the source subtraction/addition ordering.
void EcValveBase::SetInitialDemandValue(i32 amount) {
  demand -= initial_demand;
  demand += amount;
  initial_demand = amount;
}

f32 EcValveBase::GetEconomyModifier() {
  return economy_modifier;
}

void EcValveBase::SetEconomyModifier(f32 modifier) {
  economy_modifier = modifier;
}

f32 EcValveBase::GetTaxModifier() {
  return tax_modifier;
}

void EcValveBase::SetTaxModifier(f32 modifier) {
  tax_modifier = modifier;
}

u8 EcValveBase::GetDemandCap() {
  return demand_cap;
}

void EcValveBase::SetDemandCap(u8 cap) {
  demand_cap = cap > 100 ? 100 : cap;
}

EcDisposal::EcDisposal() {
  flag_31 = false;
}

// Retire the base valve binding after the disposal-specific lifetime ends.
EcDisposal::~EcDisposal() = default;

// Subtract supply from demand, applying the upper bound before the lower bound when the stored bounds are inverted.
i32 EcDisposal::QueryValveValue() {
  i32 remaining = demand - supply;
  if (remaining > maximum)
    return maximum;
  if (remaining < minimum)
    return minimum;
  return remaining;
}

// Accumulate a nonzero supply amount through the base method before increasing current supply.
bool EcDisposal::AddToSupplyValue(i32 amount) {
  if (amount) {
    EcValveBase::AddToSupplyValue(amount);
    supply += amount;
  }
  return true;
}

// Accumulate a nonzero demand amount through the base method before increasing current demand.
bool EcDisposal::AddToDemandValue(i32 amount) {
  if (amount) {
    EcValveBase::AddToDemandValue(amount);
    demand += amount;
  }
  return true;
}

// Build the current signed per-thousand notification before acquiring the message server, preserving live callback order.
void EcValveBase::NotifyValue() const {
  i32 ratio = balance >= 0 ? 1000 * balance / maximum : -1000 * balance / minimum;
  ScMessage message = {kEcMsg_DemandChanged, identifier, static_cast<u32>(ratio), 0};
  EcGetMessageServer()->MessagePost(message, false);
}

// Recalculate an uninitialized valve before posting its current value, retaining the recalculation's separate notification.
bool EcValveBase::SimulationBegin() {
  if (flag_30)
    CalculateValveValue(false);
  NotifyValue();
  return true;
}

// Restore the ten serialized values in source order, retaining partial scalar assignments when a read fails.
// Notification occurs even after failure; zero initializes only the otherwise undefined first unread temporary.
bool EcValveBase::SerialRead(CtCity *, RsStream *record) {
  u32 temporary = 0;
  bool result = record->ReadU32(&temporary) != 0;
  supply = static_cast<i32>(temporary);
  if (result)
    result = record->ReadU32(&temporary) != 0;
  demand = static_cast<i32>(temporary);
  if (result)
    result = record->ReadU32(&temporary) != 0;
  supply_accumulator = temporary;
  if (result)
    result = record->ReadU32(&temporary) != 0;
  demand_accumulator = temporary;
  if (result && (result = record->ReadF32(&economy_modifier) != 0) && (result = record->ReadF32(&tax_modifier) != 0) &&
      (result = record->ReadU8(&demand_cap) != 0))
    result = record->ReadU32(&temporary) != 0;
  flag_30 = false;
  balance = static_cast<i32>(temporary);
  if (result && (result = record->ReadI32(&initial_supply) != 0))
    result = record->ReadI32(&initial_demand) != 0;
  NotifyValue();
  return result;
}

// Write the ten live values in order, stopping at the first failure.
u8 EcValveBase::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = record->WriteU32(static_cast<u32>(supply));
  result = result && record->WriteU32(static_cast<u32>(demand)) && record->WriteU32(supply_accumulator) &&
      record->WriteU32(demand_accumulator) && record->WriteF32(economy_modifier) && record->WriteF32(tax_modifier) &&
      record->WriteU8(demand_cap) && record->WriteU32(static_cast<u32>(balance)) && record->WriteI32(initial_supply) &&
      record->WriteI32(initial_demand);
  return result;
}

void EcValveBase::CalculateValveValue(bool smooth) {
  i32 upper = maximum, difference = demand - supply;
  if (difference > upper)
    difference = upper;
  else if (difference < minimum)
    difference = minimum;
  volatile f64 modifiers = static_cast<f64>(tax_modifier) + economy_modifier;
  volatile f64 product = modifiers * upper;
  i64 converted = static_cast<i64>(product);
  i64 adjusted = static_cast<i32>(static_cast<u32>(converted) + static_cast<u32>(difference));
  if (adjusted > upper)
    adjusted = upper;
  else if (adjusted < minimum)
    adjusted = minimum;
  i32 target = static_cast<i32>(adjusted), capped = upper - upper * demand_cap / 100;
  if (adjusted > capped)
    target = capped;
  if (target < minimum)
    target = minimum;
  if (!smooth) {
    balance = target;
  } else {
    i64 step = 7 * (static_cast<i64>(upper) - minimum) / 100;
    i32 delta = target - balance;
    if (delta >= step || delta <= -step) {
      if (delta > 0)
        if (target > 0 && balance < 0 && static_cast<i64>(balance) + step < 0)
          balance = 0;
        else
          balance += static_cast<i32>(step);
      else if (target < 0 && balance > 0 && static_cast<i64>(balance) - step > 0)
        balance = 0;
      else
        balance -= static_cast<i32>(step);
    } else {
      balance = target;
    }
  }
  NotifyValue();
}

// Publish accumulated supply/demand, reset accumulators to initial contributions, then recalculate with the stored flag.
bool EcValveBase::EndOfMonth() {
  bool smooth = flag_31;
  supply = static_cast<i32>(supply_accumulator);
  demand = static_cast<i32>(demand_accumulator);
  supply_accumulator = static_cast<u32>(initial_supply);
  demand_accumulator = static_cast<u32>(initial_demand);
  CalculateValveValue(smooth);
  return true;
}

// Run the base monthly publication before copying live current values back into the disposal accumulators.
bool EcDisposal::EndOfMonth() {
  EcValveBase::EndOfMonth();
  supply_accumulator = static_cast<u32>(supply);
  demand_accumulator = static_cast<u32>(demand);
  return true;
}

void EcValveBase::DebugLockValue(i32) {
}

void EcValveBase::DebugUnlockValue() {
}

bool EcValveBase::DebugIsValueLocked() {
  return false;
}

u32 EcIncineratorOptimal = 10000, EcIncineratorLifespan = 180, EcIncineratorDecline = 120;
static u8 EcIncineratorTuningLoaded;

// Construct the building and date before loading this class's tuning once. Retain the original String lifetimes,
// two application-directory lookups, lifespan normalization and final instance-store order.
EcIncinerator::EcIncinerator() {
  retired = 0;
  if (!EcIncineratorTuningLoaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    static const char *const keys[] = {"OptimalGarbageCap", "MaxLifespan", "DeclineAge"};
    u32 *fields[] = {&EcIncineratorOptimal, &EcIncineratorLifespan, &EcIncineratorDecline};
    for (u32 index = 0; index < 3; ++index) {
      u8 found;
      {
        ScString key(keys[index], static_cast<u32>(strlen(keys[index]))), section("Incinerator", 11);
        found = ini.GetValue(&section, &key, &value);
      }
      if (found)
        *fields[index] = EcParseTuningNumber(value.Data());
      if (index == 1 && !EcIncineratorLifespan)
        EcIncineratorLifespan = 1;
    }
    if (EcIncineratorDecline > EcIncineratorLifespan)
      EcIncineratorDecline = EcIncineratorLifespan >> 1;
    EcIncineratorTuningLoaded = 1;
  }
  capacity = 0;
  previousMonthConsumed = 0;
  consumed = 0;
}

EcIncinerator::~EcIncinerator() {
}

// Select either private incinerator identity after clearing output. Other identities delegate to the building base.
bool EcIncinerator::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_EcCombustion, EcCombustion);
  HANDLE_QUERY_INTERFACE(IID_BdMortal, EcCombustion);
  return EcBuilding::QueryInterface(iid, out);
}

u32 EcIncinerator::AddRef() {
  return EcBuilding::AddRef();
}

u32 EcIncinerator::Release() {
  return EcBuilding::Release();
}

// Borrow the enclosing building view without acquiring another reference.
BdBuilding *EcIncinerator::AsISC3Building() {
  return static_cast<EcBuilding *>(this);
}

// Borrow the actual occupant interface selected through the original qualified building-view getter.
ScOccupant *EcIncinerator::GetOccupant() {
  return EcBuilding::AsISC3Occupant();
}

u32 EcIncinerator::GetProduction() {
  return capacity;
}

u32 EcIncinerator::GetCapacity() {
  u32 monthly = 30u * capacity;
  return monthly > consumed ? monthly - consumed : 0;
}

u32 EcIncinerator::Consume(u32 amount) {
  consumed += amount;
  return amount;
}

u32 EcIncinerator::GetAge() {
  u32 month, day, year;
  EcGetCity()->GetDate(&month, &day, &year);
  u32 age = 12u * year - 12u * construction_date.Year() + month;
  return age - construction_date.Month();
}

u32 EcIncinerator::GetDeclineAge() {
  return EcIncineratorDecline;
}

bool EcIncinerator::IsAging() {
  u32 age = GetAge();
  return GetOccupancyState() == 1 && age >= GetDeclineAge();
}

bool EcIncinerator::HasBirthdate() const {
  return true;
}

bool EcIncinerator::GetBirthdate(CtDateInterface *out) const {
  out->Set(construction_date.serial);
  return true;
}

bool EcIncinerator::SetBirthdate(const CtDateInterface &value) {
  construction_date.serial = value.DayNumber();
  return true;
}

// Restore the actual building descriptor before reading class properties directly into their live fields. Failed reads
// retain their written prefixes; publish the normalized retired byte only after its final property read succeeds.
bool EcIncinerator::Init(CtCity *city, OcPersistInfo *info) {
  bool result = EcBuilding::Init(city, info);
  result = result && info->GetIntData(kEcIncineratorCapacityKey, &capacity) != 0 &&
      info->GetIntData(kEcIncineratorConsumedKey, &consumed) != 0 &&
      info->GetIntData(kEcIncineratorPreviousMonthConsumedKey, &previousMonthConsumed) != 0;
  u32 flag = 0;
  if (result) {
    result = info->GetIntData(kEcIncineratorRetiredKey, &flag) != 0;
    if (result)
      retired = flag != 0;
  }
  return result;
}

// Save the actual building fields first, then append class properties in original order. Each callback gates the next
// write and the following field is read live, preserving partial saves and changes made by preceding callbacks.
bool EcIncinerator::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kEcIncineratorCapacityKey, capacity))
    return false;
  if (!info->SetIntData(kEcIncineratorConsumedKey, consumed))
    return false;
  if (!info->SetIntData(kEcIncineratorPreviousMonthConsumedKey, previousMonthConsumed))
    return false;
  return info->SetIntData(kEcIncineratorRetiredKey, retired) != 0;
}

u8 EcIncinerator::GetRetired() {
  return retired;
}

// Preserve the base agent result while applying phase-three effectiveness and phase-four monthly accounting effects.
bool EcIncinerator::Simulate(i32 phase) {
  bool result = EcBuilding::Simulate(phase);
  if (phase == 3)
    UpdateEffectiveness();
  else if (phase == 4) {
    u32 previous = consumed;
    consumed = 0;
    previousMonthConsumed = previous;
  }
  return result;
}

// Capture the city before retirement/age callbacks, query power twice as the source does, and preserve raw traffic
// eligibility and DWORD decline.
void EcIncinerator::UpdateEffectiveness() {
  bool powered = false;
  CtCity *city = EcGetCity();
  if (GetRetired() || !city)
    return;
  u32 age = GetAge();
  if (age >= EcIncineratorLifespan) {
    capacity = 0;
    ReplaceRetired();
    return;
  }
  if (city->PowerLayer()) {
    CtPowerLayer *power = city->PowerLayer();
    power->GetValue(
        position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask, &powered);
  }
  bool operating = powered && GetOccupancyState() == 1;
  if (operating) {
    NwTrafficLayer *traffic = city->TrafficLayer();
    if (traffic &&
        traffic->IsTripAvailable(
            position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask) == 1)
      operating = traffic->RouteOccupantTrip(AsISC3Occupant(), 3, 255, 0, 1, 0, 0) != 0;
  }
  if (!operating)
    capacity = 0;
  else if (age > EcIncineratorDecline)
    capacity = EcIncineratorOptimal -
        EcIncineratorOptimal * (age - EcIncineratorDecline) / (EcIncineratorLifespan - EcIncineratorDecline);
  else
    capacity = EcIncineratorOptimal;
}

void EcIncinerator::SetMaximumAge(u32 value) {
  EcIncineratorLifespan = value;
}

u32 EcIncinerator::GetMaximumAge() {
  return EcIncineratorLifespan;
}

void EcIncinerator::SetDeclineAge(u32 value) {
  EcIncineratorDecline = value;
}

void EcIncinerator::Retire() {
  retired = 1;
}

// Obtain the two current-city services separately, preserve callback-visible replacement order, and retain the date
// through final occupant releases.
void EcIncinerator::ReplaceRetired() {
  GeBuildingLayer *layer = EcGetCity()->BuildingLayer();
  ScOccupantManager *world = EcGetCity()->SurfaceOccupantManager();
  if (!layer || !world)
    return;
  ScOccupant *old_occupant;
  if (!QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&old_occupant)))
    return;
  BdBuilding *building;
  if (!layer->CreateBuilding(cached_attributes, IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    old_occupant->Release();
    return;
  }
  building->SetOccupancyState(8);
  ScOccupant *new_occupant;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&new_occupant))) {
    new_occupant->SetLocationInCellCoords(
        position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
    new_occupant->Release();
  }
  EcDate date;
  GetBirthdate(&date);
  building->SetBirthdate(date);
  BdMortal *mortal;
  if (building->QueryInterface(IID_BdMortal, reinterpret_cast<void **>(&mortal))) {
    mortal->SetMaximumAge(GetMaximumAge());
    mortal->SetDeclineAge(GetDeclineAge());
    mortal->Retire();
    mortal->Release();
  }
  world->RemoveOccupant(old_occupant);
  layer->InsertBuilding(building);
  building->Release();
  old_occupant->Release();
}

i32 EcIncinerator::MinimumExecutionInterval() {
  i32 interval = EcBuilding::MinimumExecutionInterval();
  return interval >= 3 ? 3 : interval;
}

u8 EcIncinerator::AnimType() const {
  return 1;
}

// Query the current sprite's real animation interface, capture position before the size callback, and start it in micro
// coordinates. Release the live QI output after the animation callback; the borrowed sprite itself is not retained.
void EcIncinerator::AnimStart() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    u32 captured = position, size = SizeInCellsX();
    animation->Start(i32((captured & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(((captured >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(u16((captured >> kOcPositionAltitudeShift) << kCtMicroCoordinateShift)), size);
    animation->Release();
  }
}

// Stop the currently borrowed sprite through its separately queried animation identity, then release the live output.
void EcIncinerator::AnimStop() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    animation->Stop();
    animation->Release();
  }
}

i32 EcIncinerator::Tick(u32 elapsed) {
  SpSprite *current = GetSpriteInst();
  return current ? current->Tick(elapsed) : 1;
}

u32 EcRecyclerOptimal = 45, EcRecyclerPopulation = 20000, EcRecyclerLifespan = 180, EcRecyclerDecline = 120;
static u8 EcRecyclerTuningLoaded;

// Construct the independent recycling date and load this class's four tuning values once. Keep scalar normalization
// after each reached lookup and initialize instance effectiveness/population only after all tuning scopes retire.
EcRecycler::EcRecycler() {
  retired = 0;
  if (!EcRecyclerTuningLoaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    static const char *const keys[] = {"OptimalPctReduction", "OptimalPopServed", "MaxLifespan", "DeclineAge"};
    u32 *fields[] = {&EcRecyclerOptimal, &EcRecyclerPopulation, &EcRecyclerLifespan, &EcRecyclerDecline};
    for (u32 index = 0; index < 4; ++index) {
      u8 found;
      {
        ScString key(keys[index], static_cast<u32>(strlen(keys[index]))), section("RecyclingCenter", 15);
        found = ini.GetValue(&section, &key, &value);
      }
      if (found)
        *fields[index] = EcParseTuningNumber(value.Data());
      if (index == 2 && !EcRecyclerLifespan)
        EcRecyclerLifespan = 1;
    }
    if (EcRecyclerDecline > EcRecyclerLifespan)
      EcRecyclerDecline = EcRecyclerLifespan >> 1;
    EcRecyclerTuningLoaded = 1;
  }
  effectiveness = 0;
  population = 0;
}

EcRecycler::~EcRecycler() {
}

// Select the private recycling identities after clearing output. Successful queries use qualified building retention.
bool EcRecycler::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid != IID_BdMortal && iid != IID_EcRecycling)
    return EcBuilding::QueryInterface(iid, out);
  *out = static_cast<EcRecycling *>(this);
  EcBuilding::AddRef();
  return true;
}

u32 EcRecycler::AddRef() {
  return EcBuilding::AddRef();
}

u32 EcRecycler::Release() {
  return EcBuilding::Release();
}

// Borrow the enclosing building view without acquiring another reference.
BdBuilding *EcRecycler::AsISC3Building() {
  return static_cast<EcBuilding *>(this);
}

// Borrow the actual occupant interface through the original qualified building-view getter.
ScOccupant *EcRecycler::GetOccupant() {
  return EcBuilding::AsISC3Occupant();
}

u32 EcRecycler::GetEffectiveness() {
  return effectiveness;
}

u32 EcRecycler::GetCapacity() {
  return population;
}

u32 EcRecycler::GetAge() {
  u32 month, day, year;
  EcGetCity()->GetDate(&month, &day, &year);
  u32 age = 12u * year - 12u * construction_date.Year() + month;
  return age - construction_date.Month();
}

u32 EcRecycler::GetDeclineAge() {
  return EcRecyclerDecline;
}

bool EcRecycler::IsAging() {
  u32 age = GetAge();
  return GetOccupancyState() == 1 && age >= GetDeclineAge();
}

bool EcRecycler::HasBirthdate() const {
  return true;
}

bool EcRecycler::GetBirthdate(CtDateInterface *out) const {
  out->Set(construction_date.serial);
  return true;
}

bool EcRecycler::SetBirthdate(const CtDateInterface &value) {
  u32 year = value.Year(), day = value.DayOfYear();
  construction_date.serial = day + (year ? CtCalendarEncode(12, 31, year - 1) : kCtTimeEpochDay);
  return true;
}

// Restore the actual building descriptor before reading class properties directly into their live fields. Failed reads
// retain their written prefixes; publish the normalized retired byte only after its final property read succeeds.
bool EcRecycler::Init(CtCity *city, OcPersistInfo *info) {
  bool result = EcBuilding::Init(city, info);
  result = result && info->GetIntData(kEcRecyclerEffectivenessKey, &effectiveness) != 0 &&
      info->GetIntData(kEcRecyclerPopulationKey, &population) != 0;
  u32 flag = 0;
  if (result) {
    result = info->GetIntData(kEcRecyclerRetiredKey, &flag) != 0;
    if (result)
      retired = flag != 0;
  }
  return result;
}

// Save the actual building fields first, then append class properties in original order. Each callback gates the next
// write and the following field is read live, preserving partial saves and changes made by preceding callbacks.
bool EcRecycler::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kEcRecyclerEffectivenessKey, effectiveness))
    return false;
  if (!info->SetIntData(kEcRecyclerPopulationKey, population))
    return false;
  return info->SetIntData(kEcRecyclerRetiredKey, retired) != 0;
}

u8 EcRecycler::GetRetired() {
  return retired;
}

// Run the base city agent before phase-three effectiveness changes. The original phase-four helper has no effects.
bool EcRecycler::Simulate(i32 phase) {
  bool result = EcBuilding::Simulate(phase);
  if (phase == 3)
    UpdateEffectiveness();
  return result;
}

// Power, occupancy and traffic gate both outputs; independent wrapped decline calculations retain live tuning reads and
// replacement sees cleared outputs.
void EcRecycler::UpdateEffectiveness() {
  bool powered = false;
  CtCity *city = EcGetCity();
  if (GetRetired() || !city)
    return;
  u32 age = GetAge();
  if (age >= EcRecyclerLifespan) {
    effectiveness = 0;
    population = 0;
    ReplaceRetired();
    return;
  }
  if (city->PowerLayer()) {
    CtPowerLayer *power = city->PowerLayer();
    power->GetValue(
        position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask, &powered);
  }
  bool operating = powered && GetOccupancyState() == 1;
  if (operating) {
    NwTrafficLayer *traffic = city->TrafficLayer();
    if (traffic &&
        traffic->IsTripAvailable(
            position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask) == 1)
      operating = traffic->RouteOccupantTrip(AsISC3Occupant(), 3, 255, 0, 1, 0, 0) != 0;
  }
  if (!operating) {
    effectiveness = 0;
    population = 0;
  } else if (age > EcRecyclerDecline) {
    effectiveness =
        EcRecyclerOptimal - EcRecyclerOptimal * (age - EcRecyclerDecline) / (EcRecyclerLifespan - EcRecyclerDecline);
    population = EcRecyclerPopulation -
        EcRecyclerPopulation * (age - EcRecyclerDecline) / (EcRecyclerLifespan - EcRecyclerDecline);
  } else {
    effectiveness = EcRecyclerOptimal;
    population = EcRecyclerPopulation;
  }
}

void EcRecycler::SetMaximumAge(u32 value) {
  EcRecyclerLifespan = value;
}

u32 EcRecycler::GetMaximumAge() {
  return EcRecyclerLifespan;
}

void EcRecycler::SetDeclineAge(u32 value) {
  EcRecyclerDecline = value;
}

void EcRecycler::Retire() {
  retired = 1;
}

// Obtain the two current-city services separately, preserve callback-visible replacement order, and retain the date
// through final occupant releases.
void EcRecycler::ReplaceRetired() {
  GeBuildingLayer *layer = EcGetCity()->BuildingLayer();
  ScOccupantManager *world = EcGetCity()->SurfaceOccupantManager();
  if (!layer || !world)
    return;
  ScOccupant *old_occupant;
  if (!QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&old_occupant)))
    return;
  BdBuilding *building;
  if (!layer->CreateBuilding(cached_attributes, IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    old_occupant->Release();
    return;
  }
  building->SetOccupancyState(8);
  ScOccupant *new_occupant;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&new_occupant))) {
    new_occupant->SetLocationInCellCoords(
        position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
    new_occupant->Release();
  }
  EcDate date;
  GetBirthdate(&date);
  building->SetBirthdate(date);
  BdMortal *mortal;
  if (building->QueryInterface(IID_BdMortal, reinterpret_cast<void **>(&mortal))) {
    mortal->SetMaximumAge(GetMaximumAge());
    mortal->SetDeclineAge(GetDeclineAge());
    mortal->Retire();
    mortal->Release();
  }
  world->RemoveOccupant(old_occupant);
  layer->InsertBuilding(building);
  building->Release();
  old_occupant->Release();
}

i32 EcRecycler::MinimumExecutionInterval() {
  i32 interval = EcBuilding::MinimumExecutionInterval();
  return interval >= 3 ? 3 : interval;
}

static void EcFacilityQueryText(ScString *out, u32 instance) {
  EcTextRef resource(instance, kRsCityServiceTextGroup);
  *out = resource.CopyString();
}

// Preserve the original PC53 magic-add conversion and low-DWORD wrap rather than using host integer conversion rules.
static u32 EcFacilityQueryRound(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Append the incinerator status after generic building content.
u8 EcIncinerator::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  EcBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (GetOccupancyState() == 1) {
    NwTrafficLayer *traffic = EcGetCity()->TrafficLayer();
    if (traffic &&
        traffic->IsTripAvailable(
            position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask) == 1)
      instance = 440;
    else if (IsAging())
      instance = 406;
  } else if (GetRetired())
    instance = 407;
  if (instance) {
    EcFacilityQueryText(&status, instance);
    query->SetStatusText(status);
  }
  return 1;
}

// Apply the recycler's independently installed status override with the same original traffic/aging/retired precedence.
u8 EcRecycler::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  EcBuilding::QueryBasic(message, query);
  u32 instance = 0;
  if (GetOccupancyState() == 1) {
    NwTrafficLayer *traffic = EcGetCity()->TrafficLayer();
    if (traffic &&
        traffic->IsTripAvailable(
            position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask) == 1)
      instance = 440;
    else if (IsAging())
      instance = 406;
  } else if (GetRetired())
    instance = 407;
  if (instance) {
    EcFacilityQueryText(&status, instance);
    query->SetStatusText(status);
  }
  return 1;
}

// Populate monthly maximum, current capacity, prior consumption and age rows before generic building content. FIMUL
// consumes signed wrapped DWORDs; prior consumption rounds through float storage, and callbacks see the same live Strings.
u8 EcIncinerator::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, formatted, pattern, number, maximum;
  ScLanguageUtility *utility = EcGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  EcLayer *pollution = EcGetCity()->PollutionLayer();
  if (query && pollution) {
    f64 scale = pollution->GetGarbageScalingFactor();
    u32 value = EcFacilityQueryRound(floor(scale * i32(30u * EcIncineratorOptimal)));
    EcFacilityQueryText(&label, 427);
    EcFacilityQueryText(&pattern, 119);
    utility->MakeNumberString(i32(value), &number);
    EcFormatText(&formatted, pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, 0);

    EcFacilityQueryText(&label, 118);
    EcFacilityQueryText(&pattern, 119);
    scale = pollution->GetGarbageScalingFactor();
    value = EcFacilityQueryRound(floor(scale * i32(30u * capacity)));
    utility->MakeNumberString(i32(value), &number);
    u8 warning = capacity == 0;
    EcFormatText(&formatted, pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, warning);

    EcFacilityQueryText(&label, 120);
    scale = pollution->GetGarbageScalingFactor();
    f32 prior = f32(scale * i32(previousMonthConsumed));
    EcFacilityQueryText(&pattern, 119);
    utility->MakeNumberString(i32(EcFacilityQueryRound(prior)), &number);
    EcFormatText(&formatted, pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, 0);

    EcFacilityQueryText(&label, 437);
    utility->MakeNumberString(GetAge() / 12u, &number);
    utility->MakeNumberString(GetMaximumAge() / 12u, &maximum);
    EcFacilityQueryText(&formatted, 438);
    EcFormatText(&formatted, formatted.Data(), number.Data(), maximum.Data());
    u32 age = GetAge();
    warning = age >= GetMaximumAge();
    query->AddText(&label, &formatted, 8, warning);
  }
  return EcBuilding::QueryTile(message, query);
}

// Add effectiveness, served-population and age rows with the original unsigned arithmetic and warning timing, then
// always delegate generic building content.
u8 EcRecycler::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, formatted, pattern, number, maximum;
  ScLanguageUtility *utility = EcGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  EcLayer *pollution = EcGetCity()->PollutionLayer();
  CtResidentialLayer *residential = EcGetCity()->ResidentialLayer();
  if (query && utility && pollution && residential) {
    u8 warning = 0;
    EcFacilityQueryText(&label, 430);
    if (u32(pollution->GetRecyclingCenterEffect()) >= EcRecyclerOptimal)
      EcFacilityQueryText(&formatted, 431);
    else {
      EcFacilityQueryText(&formatted, 432);
      warning = 1;
    }
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, warning);
    f32 cohort = residential->GetAgeCohortPopPct(0, 4);
    u32 people = u32(residential->GetPopulation());
    u32 served = effectiveness * EcFacilityQueryRound(f64(people) * cohort) / 100u;
    EcFacilityQueryText(&label, 433);
    EcFacilityQueryText(&pattern, 434);
    utility->MakeNumberString(i32(served), &number);
    EcFormatText(&formatted, pattern.Data(), number.Data());
    query->AddText(&label, &formatted, kUiQueryPrimaryDetailsCategory, 0);

    EcFacilityQueryText(&label, 437);
    utility->MakeNumberString(GetAge() / 12u, &number);
    utility->MakeNumberString(GetMaximumAge() / 12u, &maximum);
    EcFacilityQueryText(&formatted, 438);
    EcFormatText(&formatted, formatted.Data(), number.Data(), maximum.Data());
    u32 age = GetAge();
    warning = age >= GetMaximumAge();
    query->AddText(&label, &formatted, 8, warning);
  }
  return EcBuilding::QueryTile(message, query);
}

u8 EcIncinerator::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcBuilding::QueryDetail(message, query);
}

u8 EcRecycler::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcBuilding::QueryDetail(message, query);
}
