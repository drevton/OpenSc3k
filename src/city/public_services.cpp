#include "city/public_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cassert>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_graph_info.h"
#include "city/ct_police.h"
#include "city/ct_service_text.h"
#include "city/oc_service_building.h"
#include "city/oc_service_occupant.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3.h"
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
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/sc3_graphics.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/tr_dirt_bag.h"
#include "ui/api.h"
#include "window/api.h"

// The crime history graph uses the original dark-red series color.
enum CtCrimeGraphColor : u32 {
  kCtCrimeGraphColor = 0xc00000,
};

// The jail descriptor stores inmate count followed by the next-key monthly upkeep word.
enum BdJailPersistenceKey : u32 {
  kBdJailInmateCountKey = 0x352fbbd,
};

// Serialized keys retain their record-format role independently of live class and interface identities.
enum CtServicesArchiveGroup : u32 {
  kCtCrimeRecordGroup = 0x20a7ae7f,
  kCtFlammabilityRecordGroup = 0x61448030,
};

// These descriptor properties persist the three packed coverage bytes and the two station upkeep amounts.
enum BdStationProperty : u32 {
  kBdFireCoverage = 0x2352c88d,
  kBdFireOptimalUpkeep = 0x2352c88e,
  kBdFireMonthlyUpkeep = 0x2352c88f,
  kBdPoliceCoverage = 0xa352c8e8,
  kBdPoliceOptimalUpkeep = 0xa352c8e9,
  kBdPoliceMonthlyUpkeep = 0xa352c8ea,
};

// Original mutable tuning words, subsequently populated by the real jail constructor.
u32 BdJail::inmate_capacity = 300, BdJail::optimal_upkeep = 75;

static u8 BdFireRadius = 20, BdFireMaximum = 100, BdFireMinimum = 20;
static u8 BdPoliceRadius = 20, BdPoliceMaximum = 100, BdPoliceMinimum = 20;
static u32 BdFireOptimal = 500, BdPoliceOptimal = 500, BdPoliceOppression = 200;
static f32 BdFireMaxEfficiency = 1.2f, BdPoliceMaxEfficiency = 1.2f;
static f32 BdFireMystical = 30.0f, BdPoliceMystical = 30.0f;
static bool BdFireTuningLoaded, BdPoliceTuningLoaded, BdJailTuningLoaded;

// Preserve the original magic-add rounding used for the unsigned monthly-upkeep word.
static u32 BdRoundUpkeep(f64 value) {
  volatile f64 rounded = value + kScDoubleIntegerBias;
  f64 stored = rounded;
  u64 bits;
  memcpy(&bits, &stored, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Initialize the fire-station tuning once, then copy the live optimal coverage and upkeep to this new building.
BdFireStation::BdFireStation() {
  if (!BdFireTuningLoaded) {
    ScString path, value;
    ApCompressedIni ini;
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SetPath(path);
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SetCompressedPath(path);
    if (ini.ReadEntry("FireStation", "OptimalCoverageRadius", &value))
      BdFireRadius = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("FireStation", "OptimalMaxCoverage", &value))
      BdFireMaximum = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("FireStation", "OptimalMinCoverage", &value))
      BdFireMinimum = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("FireStation", "OptimalMonthlyUpkeep", &value))
      BdFireOptimal = ScConvertToUint32(value.begin);
    if (ini.ReadEntry("FireStation", "MaxEfficiency", &value)) {
      BdFireMaxEfficiency = f32(f64(ScConvertToUint32(value.begin)) * f64(0.01f));
      if (BdFireMaxEfficiency < 1.0f)
        BdFireMaxEfficiency = 1.0f;
    }
    if (ini.ReadEntry("FireStation", "MysticalE", &value))
      BdFireMystical = f32(ScConvertToUint32(value.begin));
    BdFireTuningLoaded = true;
  }
  coverage_radius = BdFireRadius;
  maximum_coverage = BdFireMaximum;
  minimum_coverage = BdFireMinimum;
  optimal_upkeep = monthly_upkeep = BdFireOptimal;
}

// Retire the independently linked building base without adding a station-specific shutdown call.
BdFireStation::~BdFireStation() = default;

// Initialize police-station tuning once and copy its coverage and upkeep values into the new receiver.
BdPoliceStation::BdPoliceStation() {
  if (!BdPoliceTuningLoaded) {
    ScString path, value;
    ApCompressedIni ini;
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SetPath(path);
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SetCompressedPath(path);
    if (ini.ReadEntry("PoliceStation", "OptimalCoverageRadius", &value))
      BdPoliceRadius = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("PoliceStation", "OptimalMaxCoverage", &value))
      BdPoliceMaximum = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("PoliceStation", "OptimalMinCoverage", &value))
      BdPoliceMinimum = u8(ScConvertToUint32(value.begin));
    if (ini.ReadEntry("PoliceStation", "OptimalMonthlyUpkeep", &value))
      BdPoliceOptimal = ScConvertToUint32(value.begin);
    if (ini.ReadEntry("PoliceStation", "OppressionThreshold", &value))
      BdPoliceOppression = u32(atol(value.begin));
    if (ini.ReadEntry("PoliceStation", "MaxEfficiency", &value)) {
      BdPoliceMaxEfficiency = f32(f64(ScConvertToUint32(value.begin)) * f64(0.01f));
      if (BdPoliceMaxEfficiency < 1.0f)
        BdPoliceMaxEfficiency = 1.0f;
    }
    if (ini.ReadEntry("PoliceStation", "MysticalE", &value))
      BdPoliceMystical = f32(ScConvertToUint32(value.begin));
    BdPoliceTuningLoaded = true;
  }
  coverage_radius = BdPoliceRadius;
  maximum_coverage = BdPoliceMaximum;
  minimum_coverage = BdPoliceMinimum;
  optimal_upkeep = monthly_upkeep = BdPoliceOptimal;
}

// Retire the independently linked building base without invoking a separate station shutdown.
BdPoliceStation::~BdPoliceStation() = default;

// Initialize the shared jail capacity and upkeep once, then start this jail empty at the tuned monthly upkeep.
BdJail::BdJail() {
  if (!BdJailTuningLoaded) {
    ScString path, value;
    ApCompressedIni ini;
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SetPath(path);
    CtService::CtGetApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SetCompressedPath(path);
    if (ini.ReadEntry("Jail", "InmateCapacity", &value))
      inmate_capacity = ScConvertToUint32(value.begin);
    if (ini.ReadEntry("Jail", "MonthlyUpkeep", &value))
      optimal_upkeep = ScConvertToUint32(value.begin);
    BdJailTuningLoaded = true;
  }
  inmate_count = 0;
  monthly_upkeep = optimal_upkeep;
}

// Retire the independently linked building base without implicitly invoking jail shutdown.
BdJail::~BdJail() = default;

// Scale monthly upkeep and the three fire-coverage bytes with the original overfunding curve.
void BdFireStation::SetFundingPercentage(u8 percentage) {
  f64 factor = f64(percentage) * f64(0.01f);
  monthly_upkeep = BdRoundUpkeep(f64(optimal_upkeep) * factor);
  if (monthly_upkeep > optimal_upkeep) {
    f64 curve = 1.2 / (1.0 - BdFireMaxEfficiency) + BdFireMystical;
    factor = BdFireMaxEfficiency -
        1.0 / ((curve * 4.999999 - 1.0 / (1.0 - BdFireMaxEfficiency)) * factor - curve * 4.999999);
  }
  coverage_radius = u8(i64(f64(BdFireRadius) * factor));
  maximum_coverage = u8(i64(f64(BdFireMaximum) * factor));
  minimum_coverage = u8(i64(f64(BdFireMinimum) * factor));
}

// Scale monthly upkeep and police coverage using the original fraction-based overfunding condition.
void BdPoliceStation::SetFundingPercentage(u8 percentage) {
  f64 factor = f64(percentage) * f64(0.01f);
  monthly_upkeep = BdRoundUpkeep(f64(optimal_upkeep) * factor);
  if (factor > 1.0) {
    f64 curve = 1.2 / (1.0 - BdPoliceMaxEfficiency) + BdPoliceMystical;
    factor = BdPoliceMaxEfficiency -
        1.0 / ((curve * 4.999999 - 1.0 / (1.0 - BdPoliceMaxEfficiency)) * factor - curve * 4.999999);
  }
  coverage_radius = u8(i64(f64(BdPoliceRadius) * factor));
  maximum_coverage = u8(i64(f64(BdPoliceMaximum) * factor));
  minimum_coverage = u8(i64(f64(BdPoliceMinimum) * factor));
}

void BdJail::SetFundingPercentage(u8 percentage) {
  monthly_upkeep = BdRoundUpkeep(f64(percentage) * f64(0.01f) * f64(optimal_upkeep));
}

// Clear the output before selecting this service-building identity or the separate upkeep view.
// Delegate other identities to the actual SIMSERV building base and retain only the selected view.
bool BdFireStation::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdFireStationView, BdFireStationView);
  HANDLE_QUERY_INTERFACE(IID_BdUpkeepView, BdUpkeepView);
  return CtService::OcBuilding::QueryInterface(iid, out);
}

u32 BdFireStation::AddRef() {
  return CtService::OcBuilding::AddRef();
}

u32 BdFireStation::Release() {
  return CtService::OcBuilding::Release();
}

// Restore the base building first, then the packed coverage bytes and two upkeep words in source order.
// The three coverage bytes are committed even when the first integer-data lookup refuses.
bool BdFireStation::Init(CtCity *city, OcPersistInfo *info) {
  if (!CtService::OcBuilding::Init(city, info))
    return false;
  u32 packed = 0;
  bool result = info->GetIntData(kBdFireCoverage, &packed) != 0;
  coverage_radius = u8(packed);
  maximum_coverage = u8(packed >> 8);
  minimum_coverage = u8(packed >> 16);
  return result && info->GetIntData(kBdFireOptimalUpkeep, &optimal_upkeep) &&
      info->GetIntData(kBdFireMonthlyUpkeep, &monthly_upkeep);
}

// Save the base building, then pack radius, maximum and minimum coverage into the low three bytes.
// Preserve both upkeep words and stop after the first refused property write.
bool BdFireStation::Save(CtCity *city, OcPersistInfo *info) {
  if (!CtService::OcBuilding::Save(city, info))
    return false;
  u32 packed = u32(coverage_radius) | (u32(maximum_coverage) << 8) | (u32(minimum_coverage) << 16);
  return info->SetIntData(kBdFireCoverage, packed) && info->SetIntData(kBdFireOptimalUpkeep, optimal_upkeep) &&
      info->SetIntData(kBdFireMonthlyUpkeep, monthly_upkeep);
}

// Clear the output before selecting this service-building identity or the separate upkeep view.
// Delegate other identities to the actual SIMSERV building base and retain only the selected view.
bool BdPoliceStation::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdPoliceStationView, BdPoliceStationView);
  HANDLE_QUERY_INTERFACE(IID_BdUpkeepView, BdUpkeepView);
  return CtService::OcBuilding::QueryInterface(iid, out);
}

u32 BdPoliceStation::AddRef() {
  return CtService::OcBuilding::AddRef();
}

u32 BdPoliceStation::Release() {
  return CtService::OcBuilding::Release();
}

// Restore the base building first, then the packed coverage bytes and two upkeep words in source order.
// The three coverage bytes are committed even when the first integer-data lookup refuses.
bool BdPoliceStation::Init(CtCity *city, OcPersistInfo *info) {
  if (!CtService::OcBuilding::Init(city, info))
    return false;
  u32 packed = 0;
  bool result = info->GetIntData(kBdPoliceCoverage, &packed) != 0;
  coverage_radius = u8(packed);
  maximum_coverage = u8(packed >> 8);
  minimum_coverage = u8(packed >> 16);
  return result && info->GetIntData(kBdPoliceOptimalUpkeep, &optimal_upkeep) &&
      info->GetIntData(kBdPoliceMonthlyUpkeep, &monthly_upkeep);
}

// Save the base building, then pack radius, maximum and minimum coverage into the low three bytes.
// Preserve both upkeep words and stop after the first refused property write.
bool BdPoliceStation::Save(CtCity *city, OcPersistInfo *info) {
  if (!CtService::OcBuilding::Save(city, info))
    return false;
  u32 packed = u32(coverage_radius) | (u32(maximum_coverage) << 8) | (u32(minimum_coverage) << 16);
  return info->SetIntData(kBdPoliceCoverage, packed) && info->SetIntData(kBdPoliceOptimalUpkeep, optimal_upkeep) &&
      info->SetIntData(kBdPoliceMonthlyUpkeep, monthly_upkeep);
}

// Clear the output before selecting this service-building identity or the separate upkeep view.
// Delegate other identities to the actual SIMSERV building base and retain only the selected view.
bool BdJail::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdJailView, BdJailView);
  HANDLE_QUERY_INTERFACE(IID_BdUpkeepView, BdUpkeepView);
  return CtService::OcBuilding::QueryInterface(iid, out);
}

u32 BdJail::AddRef() {
  return CtService::OcBuilding::AddRef();
}

u32 BdJail::Release() {
  return CtService::OcBuilding::Release();
}

bool BdJail::Init(CtCity *city, OcPersistInfo *info) {
  return CtService::OcBuilding::Init(city, info) && info->GetIntData(kBdJailInmateCountKey, &inmate_count) &&
      info->GetIntData(kBdJailInmateCountKey + 1, &monthly_upkeep);
}

bool BdJail::Save(CtCity *city, OcPersistInfo *info) {
  return CtService::OcBuilding::Save(city, info) && info->SetIntData(kBdJailInmateCountKey, inmate_count) &&
      info->SetIntData(kBdJailInmateCountKey + 1, monthly_upkeep);
}

// Borrow the actual building subobject without adding a reference.
BdBuilding *BdFireStation::AsISC3Building() {
  return static_cast<CtService::OcBuilding *>(this);
}

// Borrow the occupant subobject of the independently linked building without retaining it.
ScOccupant *BdFireStation::AsISC3Occupant() {
  return static_cast<CtService::OcOccupant *>(this);
}

u8 BdFireStation::GetCoverageRadius() {
  return coverage_radius;
}

u8 BdFireStation::GetMaxCoverage() {
  return maximum_coverage;
}

u8 BdFireStation::GetMinCoverage() {
  return minimum_coverage;
}

i32 BdFireStation::GetMonthlyUpkeep() {
  return i32(optimal_upkeep);
}

// Borrow the actual building subobject without adding a reference.
BdBuilding *BdPoliceStation::AsISC3Building() {
  return static_cast<CtService::OcBuilding *>(this);
}

// Borrow the occupant subobject of the independently linked building without retaining it.
ScOccupant *BdPoliceStation::AsISC3Occupant() {
  return static_cast<CtService::OcOccupant *>(this);
}

u8 BdPoliceStation::GetCoverageRadius() {
  return coverage_radius;
}

u8 BdPoliceStation::GetMaxCoverage() {
  return maximum_coverage;
}

u8 BdPoliceStation::GetMinCoverage() {
  return minimum_coverage;
}

i32 BdPoliceStation::GetMonthlyUpkeep() {
  return i32(optimal_upkeep);
}

// Borrow the jail's actual building subobject without acquiring a reference.
BdBuilding *BdJail::AsISC3Building() {
  return static_cast<CtService::OcBuilding *>(this);
}

// Borrow the jail's occupant subobject without acquiring a reference.
ScOccupant *BdJail::AsISC3Occupant() {
  return static_cast<CtService::OcOccupant *>(this);
}

u32 BdJail::GetInmateCapacity() {
  return inmate_capacity;
}

u32 BdJail::GetInmateCount() {
  return inmate_count;
}

u32 BdJail::SetInmateCount(u32 count) {
  inmate_count = count;
  return count;
}

i32 BdJail::GetMonthlyUpkeep() {
  return i32(optimal_upkeep);
}

bool BdFireStation::ReadState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::ReadState(city, record) && record->ReadU8(&coverage_radius) &&
      record->ReadU8(&maximum_coverage) && record->ReadU8(&minimum_coverage) && record->ReadU32(&optimal_upkeep) &&
      record->ReadU32(&monthly_upkeep);
}

// Write all three coverage bytes and both upkeep words after the base stream phase.
bool BdFireStation::WriteState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::WriteState(city, record) && record->WriteU8(coverage_radius) &&
      record->WriteU8(maximum_coverage) && record->WriteU8(minimum_coverage) && record->WriteU32(optimal_upkeep) &&
      record->WriteU32(monthly_upkeep);
}

bool BdPoliceStation::ReadState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::ReadState(city, record) && record->ReadU8(&coverage_radius) &&
      record->ReadU8(&maximum_coverage) && record->ReadU8(&minimum_coverage) && record->ReadU32(&optimal_upkeep) &&
      record->ReadU32(&monthly_upkeep);
}

// Write all three coverage bytes and both upkeep words after the base stream phase.
bool BdPoliceStation::WriteState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::WriteState(city, record) && record->WriteU8(coverage_radius) &&
      record->WriteU8(maximum_coverage) && record->WriteU8(minimum_coverage) && record->WriteU32(optimal_upkeep) &&
      record->WriteU32(monthly_upkeep);
}

bool BdJail::ReadState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::ReadState(city, record) && record->ReadU32(&inmate_count);
}

bool BdJail::WriteState(CtCity *city, RsStream *record) {
  return CtService::OcBuilding::WriteState(city, record) && record->WriteU32(inmate_count);
}

u8 BdFireStation::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  auto *layer = CtService::CtGetCity()->FireLayer();
  if (layer && layer->IsOnStrike())
    query->SetStatusText(CtService::ScTextResource(kRsCityServiceWorkersOnStrikeText, kRsCityServiceTextGroup));
  return CtService::OcBuilding::QueryBasic(message, query);
}

// Publish the five original station-information rows before the generic building tile query.
u8 BdFireStation::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  bool warning = false;
  if (query) {
    for (u32 index = 0; index < 5; ++index) {
      if (GetInfoStrings(&label, &value, index, &warning)) {
        u32 category = index < 3 ? kUiQueryPrimaryDetailsCategory
            : index == 3         ? kUiQueryMonthlyCostCategory
                                 : kUiQuerySecondaryDetailsCategory;
        query->AddText(&label, &value, category, warning);
      }
    }
  }
  return CtService::OcBuilding::QueryTile(message, query);
}

u8 BdPoliceStation::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  auto *layer = CtService::CtGetCity()->PoliceLayer();
  if (layer && layer->IsOnStrike())
    query->SetStatusText(CtService::ScTextResource(kRsCityServiceWorkersOnStrikeText, kRsCityServiceTextGroup));
  return CtService::OcBuilding::QueryBasic(message, query);
}

// Publish the five original station-information rows before the generic building tile query.
u8 BdPoliceStation::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  bool warning = false;
  if (query) {
    for (u32 index = 0; index < 5; ++index) {
      if (GetInfoStrings(&label, &value, index, &warning)) {
        u32 category = index < 3 ? kUiQueryPrimaryDetailsCategory
            : index == 3         ? kUiQueryMonthlyCostCategory
                                 : kUiQuerySecondaryDetailsCategory;
        query->AddText(&label, &value, category, warning);
      }
    }
  }
  return CtService::OcBuilding::QueryTile(message, query);
}

u8 BdFireStation::QueryDetail(ScMessage *message, UiQuery *query) {
  return CtService::OcBuilding::QueryDetail(message, query);
}

u8 BdPoliceStation::QueryDetail(ScMessage *message, UiQuery *query) {
  return CtService::OcBuilding::QueryDetail(message, query);
}

u8 BdJail::QueryDetail(ScMessage *message, UiQuery *query) {
  return CtService::OcBuilding::QueryDetail(message, query);
}

u32 BdFireStation::GetInfoStringCount() {
  return 5;
}

u32 BdPoliceStation::GetInfoStringCount() {
  return 5;
}

u32 BdPoliceStation::GetDebugInfoStringCount() {
  return 0;
}

u8 BdPoliceStation::GetDebugInfoStrings(ScStringView *, ScStringView *, u32) {
  return 0;
}

u32 BdJail::GetDebugInfoStringCount() {
  return 0;
}

u8 BdJail::GetDebugInfoStrings(ScStringView *, ScStringView *, u32) {
  return 0;
}

static void BdStationText(u32 instance, ScString *out) {
  *out = ScString(CtService::ScTextResource(instance, kRsCityServiceTextGroup));
}

// Format one original numeric station row with its actual language utility and resource template.
static void BdStationNumber(ScStringView *label, ScStringView *value, u32 label_id, u32 format_id,
    ScLanguageUtility *language, i32 number, ScString *format, ScString *output, ScString *localized) {
  BdStationText(label_id, format);
  label->Copy(*format);
  BdStationText(format_id, format);
  language->MakeNumberString(number, localized);
  output->Sprintf(format->Data(), localized->Data());
  value->Copy(*output);
}

// Format a station or jail row whose original value is read after both resource acquisitions and the label copy.
// Keep the live field address until those callbacks have completed, then use the original signed numeric conversion.
static void BdStationLiveNumber(ScStringView *label, ScStringView *value, u32 label_id, u32 format_id,
    ScLanguageUtility *language, const u32 *number, bool money, ScString *format, ScString *output,
    ScString *localized) {
  BdStationText(label_id, format);
  label->Copy(*format);
  BdStationText(format_id, format);
  i64 current = i32(*number);
  if (money)
    language->MakeMoneyString(current, localized);
  else
    language->MakeNumberString(current, localized);
  output->Sprintf(format->Data(), localized->Data());
  value->Copy(*output);
}

// Format the police effectiveness row after the label and format resources have completed their callbacks.
// Read the live oppression divisor at its original point and exclude the original undefined signed divisions.
static void BdStationPoliceEffect(ScStringView *label, ScStringView *value, ScLanguageUtility *language, i32 score,
    i32 percentage, const u32 *oppression, ScString *format, ScString *output, ScString *localized) {
  BdStationText(138, format);
  label->Copy(*format);
  BdStationText(139, format);
  i32 numerator = i32(u32(score) * u32(percentage)), divisor = i32(*oppression);
  assert(divisor && !(numerator == i32(kScIntegerSignBit) && divisor == -1));
  language->MakeNumberString(numerator / divisor, localized);
  output->Sprintf(format->Data(), localized->Data());
  value->Copy(*output);
}

// Recover the five original fire-station information rows, using live hazard, aura and land-value views.
u8 BdFireStation::GetInfoStrings(ScStringView *label, ScStringView *value, u32 index, bool *warning) {
  ScString format, output, localized;
  ScLanguageUtility *language = CtService::CtGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return 0;
  if (index <= 1) {
    CtLayer *selected = CtService::CtGetCity()->GetSpecificLayer(kCtFlammabilityLayerType);
    void *identity = nullptr;
    if (!selected)
      return 0;
    selected->QueryInterface(IID_EnHazardMap, &identity);
    CtFlammabilityLayer *hazard = static_cast<CtFlammabilityLayer *>(identity);
    if (!hazard)
      return 0;
    if (warning)
      *warning = false;
    GeCellRect rect{};
    static_cast<CtService::OcOccupant *>(this)->GetBoundingRectInCellCoords(&rect);
    u32 center_x = rect.left + ((rect.right - rect.left) >> 1);
    u32 center_z = rect.top + ((rect.bottom - rect.top) >> 1);
    u32 r = BdFireRadius;
    u32 first_x = rect.left >= r ? rect.left - r : 0, first_z = rect.top >= r ? rect.top - r : 0;
    u32 last_x = rect.right + r, last_z = rect.bottom + r;
    last_x = UintMin(last_x, CtService::CtGetCity()->CellCountX() - 1);
    last_z = UintMin(last_z, CtService::CtGetCity()->CellCountZ() - 1);
    u32 sum = 0, count = 0;
    u8 cell = 0;
    for (u32 x = first_x; x <= last_x; ++x) {
      for (u32 z = first_z; z <= last_z; ++z) {
        i64 dx = i64(center_x) - x, dz = i64(center_z) - z;
        if ((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz) <= r) {
          hazard->GetValue(x, z, &cell);
          sum += cell;
          ++count;
        }
      }
    }
    u32 divisor = 2u * count;
    assert(divisor);
    u32 average = sum / divisor;
    if (index == 0) {
      BdStationNumber(label, value, 156, 157, language, i32(average), &format, &output, &localized);
    } else {
      CtLayer *aura_layer = CtService::CtGetCity()->GetSpecificLayer(kCtAuraLayerType);
      identity = nullptr;
      assert(aura_layer);
      aura_layer->QueryInterface(IID_CtAuraLayer, &identity);
      CtAuraLayer *aura = static_cast<CtAuraLayer *>(identity);
      assert(aura);
      u32 x = position & kOcPositionCellValueMask, z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
      i8 rating = 0;
      aura->GetValue(x, z, &rating);
      aura->Release();
      i32 adjusted = i32(f64(average) * (1.5 - (f64(rating) + 127.0) * 0.00392156862745098 * 0.3));
      BdStationNumber(label, value, 158, 159, language, adjusted, &format, &output, &localized);
    }
    hazard->Release();
    return 1;
  }
  if (index == 2) {
    BdStationText(160, &format);
    label->Copy(format);
    f64 ratio = f64(monthly_upkeep) / f64(BdFireOptimal);
    bool severe = !(ratio >= 0.4);
    u32 instance = severe ? 161 : !(ratio >= 0.7) ? 162 : !(ratio >= 1.0) ? 163 : 164;
    BdStationText(instance, &format);
    if (severe && warning)
      *warning = true;
    value->Copy(format);
    return 1;
  }
  if (index == 3) {
    BdStationLiveNumber(label, value, 134, 135, language, &monthly_upkeep, true, &format, &output, &localized);
    return 1;
  }
  if (index == 4) {
    CtLandValueLayer *land = CtService::CtGetCity()->LandValueLayer();
    if (!land)
      return 0;
    u32 x = position & kOcPositionCellValueMask, z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
    u8 land_value = 0;
    land->GetValue(x, z, &land_value);
    i32 adjusted = i32(f64(land_value) * 0.6666666666666666);
    BdStationNumber(label, value, 165, 166, language, adjusted, &format, &output, &localized);
    return 1;
  }
  return 0;
}

// Recover the five police-station information rows from the live crime and police grids.
u8 BdPoliceStation::GetInfoStrings(ScStringView *label, ScStringView *value, u32 index, bool *warning) {
  ScString format, output, localized;
  ScLanguageUtility *language = CtService::CtGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return 0;
  CtCrimeLayer *crime = CtService::CtGetCity()->CrimeLayer();
  if (!crime)
    return 0;
  CtPoliceLayer *police = CtService::CtGetCity()->PoliceLayer();
  if (!police)
    return 0;
  if (warning)
    *warning = false;
  GeCellRect bounds{};
  static_cast<CtService::OcOccupant *>(this)->GetBoundingRectInCellCoords(&bounds);
  u32 center_x = bounds.left + ((bounds.right - bounds.left) >> 1);
  u32 center_z = bounds.top + ((bounds.bottom - bounds.top) >> 1);
  u32 radius = BdPoliceRadius;
  u32 first_x = bounds.left >= radius ? bounds.left - radius : 0;
  u32 first_z = bounds.top >= radius ? bounds.top - radius : 0;
  u32 last_x = bounds.right + radius, last_z = bounds.bottom + radius;
  last_x = UintMin(last_x, crime->CellCountX() - 1);
  last_z = UintMin(last_z, crime->CellCountZ() - 1);
  u32 crime_sum = 0, crime_count = 0;
  u8 cell = 0;
  for (u32 x = first_x; x <= last_x; ++x) {
    for (u32 z = first_z; z <= last_z; ++z) {
      i64 dx = i64(center_x) - x, dz = i64(center_z) - z;
      if ((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz) <= radius) {
        crime->GetValue(x, z, &cell);
        crime_sum += cell;
        ++crime_count;
      }
    }
  }
  radius = GetCoverageRadius();
  static_cast<CtService::OcOccupant *>(this)->GetBoundingRectInCellCoords(&bounds);
  first_x = bounds.left >= radius ? bounds.left - radius : 0;
  first_z = bounds.top >= radius ? bounds.top - radius : 0;
  last_x = bounds.right + radius;
  last_z = bounds.bottom + radius;
  last_x = UintMin(last_x, crime->CellCountX() - 1);
  last_z = UintMin(last_z, crime->CellCountZ() - 1);
  u32 coverage_sum = 0, coverage_count = 0;
  for (u32 x = first_x; x <= last_x; ++x) {
    for (u32 z = first_z; z <= last_z; ++z) {
      i64 dx = i64(center_x) - x, dz = i64(center_z) - z;
      i64 distance = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
      if (distance > 0)
        --distance;
      if (distance <= radius) {
        police->GetValue(x, z, &cell);
        coverage_sum += cell;
        ++coverage_count;
      }
    }
  }
  i32 score = crime_count ? i32(u32(i32(10u * crime_sum) / i32(crime_count)) + u32(i32(crime_sum) % 13)) : 0;
  i32 coverage = coverage_count ? i32(coverage_sum) / i32(coverage_count) : 0;
  i32 baseline = i32(BdPoliceMinimum) + (i32(BdPoliceMaximum) - i32(BdPoliceMinimum)) / 3;
  if (!baseline)
    baseline = 1;
  i32 percentage = i32(u32(coverage) * 100u) / baseline;
  if (index == 0) {
    BdStationNumber(label, value, 136, 137, language, score, &format, &output, &localized);
    return 1;
  }
  if (index == 1) {
    BdStationPoliceEffect(label, value, language, score, percentage, &BdPoliceOppression, &format, &output, &localized);
    return 1;
  }
  if (index == 2) {
    BdStationText(140, &format);
    label->Copy(format);
    u32 instance;
    bool severe = false;
    if (percentage < 50) {
      instance = 141;
      severe = true;
    } else if (percentage < 80) {
      instance = 142;
    } else if (percentage > i32(BdPoliceOppression) && score != 0) {
      instance = 144;
      severe = true;
    } else {
      instance = 143;
    }
    BdStationText(instance, &format);
    if (severe && warning)
      *warning = true;
    value->Copy(format);
    return 1;
  }
  if (index == 3) {
    BdStationLiveNumber(label, value, 134, 135, language, &monthly_upkeep, true, &format, &output, &localized);
    return 1;
  }
  if (index == 4) {
    GeBuildingLayer *buildings = CtService::CtGetCity()->BuildingLayer();
    if (!buildings)
      return 0;
    u32 city_x = 0, city_z = 0;
    buildings->GetCityCenter(&city_x, &city_z);
    i64 dx = i64(city_x) - (position & kOcPositionCellValueMask),
        dz = i64(city_z) - ((position >> kOcPositionZShift) & kOcPositionCellValueMask);
    u32 x_distance = u32(dx < 0 ? -dx : dx), z_distance = u32(dz < 0 ? -dz : dz);
    u32 distance = z_distance >= x_distance ? z_distance + x_distance / 2 : x_distance + z_distance / 2;
    i32 result;
    if (score > 0) {
      f64 factor = score >= 100 ? 100.0 / score : 11.0 - f64(score) * 0.1;
      result = i32(u32(i64(factor * f64(distance))));
    } else {
      result = i32(10u * distance);
    }
    BdStationNumber(label, value, 145, 146, language, result, &format, &output, &localized);
    return 1;
  }
  return 0;
}

// Populate the jail's inmate, capacity, crowding and upkeep rows before the generic building tile query.
u8 BdJail::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value, format, localized;
  ScLanguageUtility *language = CtService::CtGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return 0;
  if (query) {
    BdStationText(147, &label);
    BdStationText(148, &format);
    language->MakeNumberString(i32(inmate_count), &localized);
    value.Sprintf(format.Data(), localized.Data());
    query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, false);
    BdStationText(149, &label);
    BdStationText(150, &format);
    language->MakeNumberString(i32(inmate_capacity), &localized);
    value.Sprintf(format.Data(), localized.Data());
    query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, false);
    BdStationText(151, &label);
    u32 instance;
    bool warning = false;
    if (inmate_count > inmate_capacity) {
      instance = 155;
      warning = true;
    } else if (f64(inmate_count) >= f64(inmate_capacity) * 0.9) {
      instance = 152;
    } else if (f64(inmate_count) >= f64(inmate_capacity) * 0.75) {
      instance = 153;
    } else {
      instance = 154;
    }
    BdStationText(instance, &value);
    query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, warning);
    BdStationText(134, &label);
    BdStationText(135, &format);
    language->MakeMoneyString(i32(monthly_upkeep), &localized);
    value.Sprintf(format.Data(), localized.Data());
    query->AddText(&label, &value, kUiQueryMonthlyCostCategory, false);
  }
  return CtService::OcBuilding::QueryTile(message, query);
}

CtCrimeMods CtCrime::agent_modifiers;
u16 CtCrime::update_period = 43;
u8 CtCrime::police_factor = 4;
i16 CtCrime::curfew_effect = -1, CtCrime::gambling_effect = 20, CtCrime::sports_effect = -2, CtCrime::watch_effect = -4,
    CtCrime::corps_effect = -1;

CtCrimeMods::CtCrimeMods() : first(nullptr) {
}

CtCrimeMods::~CtCrimeMods() {
  Clear();
}

// Delete each stable modifier entry after advancing the owning head. The actual key/byte payloads have no callbacks.
void CtCrimeMods::Clear() {
  while (first) {
    CtCrimeMod *retired = first;
    first = retired->next;
    delete retired;
  }
}

// Construct the primary map and six genuine interfaces, then the iterator, signed map, modifier owner and real string.
// Leave the three scan words unwritten until all members finish, matching their original final constructor stores.
CtCrime::CtCrime()
    : CtSizedByteMap(1), bound(false), city(nullptr), zones(nullptr), police(nullptr), ordinances(nullptr),
      agent_types(nullptr), working(1), sum(0), sample_count(0), average(0), graph(nullptr) {
  cells_per_tick = 0;
  scan_x = 0;
  scan_z = 0;
}

CtCrime::~CtCrime() {
  CtCrime::Shutdown();
}

// Release city/tree/zones/ordinances/police in order, clearing each field after its callback; stop the working map.
// Release the retained value producer before clearing the layer flag and finally shutting down the primary byte map.
bool CtCrime::Shutdown() {
  if (!bound)
    return true;
  ScRelease(&city);
  if (agent_types)
    agent_types->Release();
  agent_types = nullptr;
  if (zones)
    zones->Release();
  zones = nullptr;
  if (ordinances)
    ordinances->Release();
  ordinances = nullptr;
  if (police)
    police->Release();
  police = nullptr;
  working.Shutdown();
  if (graph)
    graph->Release();
  graph = nullptr;
  bound = false;
  return CtSizedByteMap::Shutdown();
}

// Normalize the original unsigned crime cell into the positive signed-map range using multiplier 8421504.
// Select the cell through the qualified sized-map body. Later overrides cannot redirect this original base call.
void CtCrime::GetValueSint32(u32 x, u32 z, i32 *value) const {
  u8 cell;
  CtSizedByteMap::GetValue(x, z, &cell);
  *value = i32(u32(cell) * kCtByteMapSint32Scale);
}

u8 CtCrime::GetAverageValue() {
  return average;
}

// Publish the actual selected crime, city, agent, change, query-string or sprite-factory view before retaining it.
// Other identities delegate to the qualified sized-map query, preserving the primary identity and cleared refusals.
bool CtCrime::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtCrimeLayer, CtCrimeLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryInfoString, ScQueryInfoString);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  return CtSizedByteMap::QueryInterface(iid, out);
}

u32 CtCrime::AddRef() {
  return CtSizedByteMap::AddRef();
}

u32 CtCrime::Release() {
  return CtSizedByteMap::Release();
}

u32 CtCrime::GetLayerType() {
  return kCtCrimeLayerType;
}

u32 CtCrime::CellCountX() const {
  return CtSizedByteMap::CellCountX();
}

u32 CtCrime::CellCountZ() const {
  return CtSizedByteMap::CellCountZ();
}

bool CtCrime::InBounds(u32 x, u32 z) const {
  return CtSizedByteMap::InBounds(x, z);
}

bool CtCrime::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::InBounds(x0, z0, x1, z1);
}

bool CtCrime::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::ValidRect(x0, z0, x1, z1);
}

bool CtCrime::SerialRead(RsStream *record) {
  return CtSizedByteMap::SerialRead(record);
}

bool CtCrime::SerialWrite(RsStream *record) const {
  return CtSizedByteMap::SerialWrite(record);
}

bool CtCrime::Init(CtCity *source_city, const u8 *initial) {
  return CtSizedByteMap::Init(source_city, initial);
}

void CtCrime::GetValue(u32 x, u32 z, u8 *value) const {
  CtSizedByteMap::GetValue(x, z, value);
}

void CtCrime::SetValue(u32 x, u32 z, const u8 *value) {
  CtSizedByteMap::SetValue(x, z, value);
}

void CtCrime::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  CtSizedByteMap::SetValue(x0, z0, x1, z1, value);
}

void CtCrime::SetAllCells(const u8 *value) {
  CtSizedByteMap::SetAllCells(value);
}

i32 CtCrime::MinimumExecutionInterval() {
  return 2;
}

bool CtCrime::IsAgentLocking() {
  return true;
}

u32 CtCrime::TypeId() const {
  return 0;
}

// Expose no agent-type object, matching the original folded zero-return method without consulting the retained tree.
CtAgentType *CtCrime::AgentType() {
  return nullptr;
}

bool CtCrime::SetAgentType(CtAgentType *) {
  return false;
}

bool CtCrime::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtCrime::DebugClassTag(ScStringView *) {
  return false;
}

bool CtCrime::DebugTypeTag(ScStringView *) {
  return false;
}

bool CtCrime::DoMessage(ScMessage *) {
  return false;
}

bool CtCrime::StaticShutdown() {
  agent_modifiers.Clear();
  return true;
}

// Import plane512 in descending X/Z order, expanding each source cell to an inclusive two-by-two crime rectangle.
// Refused plane opening invokes virtual shutdown; the original ignores each cell-read reply and all rectangle replies.
bool CtCrime::Init(CtCity *, ScImporter *importer) {
  if (!importer->MapIsPresent(kCtLegacyXcrmPlane)) {
    Shutdown();
    return false;
  }
  i32 last = i32(importer->GetMapSize(kCtLegacyXcrmPlane)) - 1;
  for (i32 x = last; x >= 0; --x) {
    for (i32 z = last; z >= 0; --z) {
      // Initialized scratch excludes original unwritten-output reads; it is not a recovered failed-import value.
      u8 cell = 0;
      importer->GetValue(kCtLegacyXcrmPlane, u32(x), u32(z), &cell);
      SetValue(u32(2 * x), u32(2 * z), u32(2 * x + 1), u32(2 * z + 1), &cell);
    }
  }
  return true;
}

bool CtCrime::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

u32 CtCrime::GetMapSpriteCount() {
  return 1;
}

// Publish the crime map sprite kind for index zero. Other indices leave the caller's output untouched.
bool CtCrime::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10013;
  return true;
}

bool CtCrime::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool CtCrime::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool CtCrime::CellChangedAll(u32) {
  return true;
}

bool CtCrime::CellChanged(u32, u32, u32) {
  return true;
}

bool CtCrime::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

u32 CtCrime::GetInfoStringCount(u32) {
  return 0;
}

u8 CtCrime::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

void CtCrime::EndOfCycle() {
  average = sample_count ? u8(sum / sample_count) : 0;
  sum = 0;
  sample_count = 0;
}

// Snapshot ordinance switches before scanning; lock each coarse cell, subtract four police samples, then apply effects.
// Exhaustion restarts the iterator before averaging the completed pass; each next coordinate is published before unlock.
u8 CtCrime::DoDailySim() {
  bool curfew = ordinances->QueryOrdinanceOn(kCtOrdinance_YouthCurfew),
       gambling = ordinances->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling),
       sports = ordinances->QueryOrdinanceOn(kCtOrdinance_JuniorSports),
       watch = ordinances->QueryOrdinanceOn(kCtOrdinance_NeighborhoodWatch),
       corps = ordinances->QueryOrdinanceOn(kCtOrdinance_ConservationCorps);
  // Initial scratch only excludes an original first failed Next with unwritten outputs; later outputs remain live.
  i32 next_x = 0, next_z = 0;
  for (u32 processed = 0; processed < cells_per_tick; ++processed) {
    city->Lock();
    i16 generated;
    working.GetValue(scan_x, scan_z, &generated);
    i32 value = generated;
    for (u32 cell = 0; cell < 4; ++cell) {
      u8 coverage;
      police->GetValue(scan_x + (cell & 1), scan_z + (cell >> 1), &coverage);
      assert(police_factor);
      value -= coverage / i32(police_factor);
    }
    if (curfew)
      value += curfew_effect;
    if (gambling)
      value += gambling_effect;
    if (sports)
      value += sports_effect;
    if (watch)
      value += watch_effect;
    if (corps)
      value += corps_effect;
    i32 quarter = value / 4;
    u8 crime_value = quarter > 255 ? 255 : quarter < 0 ? 0 : u8(quarter);
    SetValue(scan_x, scan_z, &crime_value);
    if (generated > 0) {
      sum += 4u * crime_value;
      sample_count += 4;
    }
    if (iterator.state == 2) {
      iterator.Begin(1);
      EndOfCycle();
    }
    iterator.Next(&next_x, &next_z);
    scan_x = 2u * u32(next_x);
    scan_z = 2u * u32(next_z);
    city->Unlock();
  }
  return 1;
}

// A refused query originally frees raw storage without releasing constructor acquisitions. Detach those actual owners
// before ordinary C++ deletion so their leaked holds remain, while the still-null palette has no teardown effects.
bool CtCrime::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10013)
    return false;
  CtCrimeMapSprite *sprite = new CtCrimeMapSprite(this, zones);
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->crime = nullptr;
  sprite->zones = nullptr;
  delete sprite;
  return false;
}

// Bind city services in the original acquisition order, initialize the working map and configure the module-local graph.
// Preserve early-failure holds, graph setup before primary-map initialization, and the final packed scan cursor seeding.
bool CtCrime::Init(CtCity *source_city) {
  if (bound || !source_city)
    return false;
  city = ScAddRefNotNull(source_city);
  agent_types = ScAddRefNotNull(CtService::CtGetAgentTree());
  zones = ScAddRefNotNull(city->ZoneLayer());
  ordinances = city->OrdinanceLayer();
  if (!ordinances) {
    Shutdown();
    return false;
  }
  ordinances->AddRef();
  police = city->PoliceLayer();
  if (!police) {
    Shutdown();
    return false;
  }
  police->AddRef();
  i16 zero_short = 0;
  working.Init(city, &zero_short);
  graph = ScAddRefNotNull(new CtService::ScGraphInfo(0));
  graph->SetMaxValue(260);
  {
    CtService::ScTextResource resource(kRsGameCrimeText, kRsGameTextGroup);
    ScString translated = static_cast<ScString>(resource), copy(translated), label(copy);
    graph->SetSelectionLabel(label);
  }
  {
    CtService::ScTextResource resource(kRsGameCrimeFormatText, kRsGameTextGroup);
    ScString translated = static_cast<ScString>(resource), copy(translated);
    number_format = copy;
  }
  ScString label;
  label.Sprintf(number_format.ToChar(), "0");
  graph->SetCurrentValueLabel(ScGraphDataSet::Month, label);
  graph->SetCurrentValueLabel(ScGraphDataSet::Year, label);
  graph->SetCurrentValueLabel(ScGraphDataSet::Decade, label);
  u32 selection_key = kCtCrimeGraphSelectionId, value_key = kCtCrimeGraphValueId;
  graph->SetSelectionLabelID(selection_key);
  graph->SetValueLabelID(value_key);
  graph->SetColor(kCtCrimeGraphColor);
  sum = 0;
  sample_count = 0;
  average = 0;
  modifiers.Clear();
  u8 zero_byte = 0;
  bound = Init(city, &zero_byte);
  u32 rows = CellCountZ(), columns = CellCountX();
  assert(4u * update_period);
  cells_per_tick = rows * columns / (4u * update_period);
  if (iterator.mode) {
    iterator.mode = 0;
    iterator.dirty = 1;
  }
  rows = CellCountZ();
  columns = CellCountX();
  iterator.left = 0;
  iterator.top = 0;
  iterator.right = i32((columns >> 1) - 1);
  iterator.bottom = i32((rows >> 1) - 1);
  if (iterator.right < iterator.left)
    iterator.right = iterator.left;
  if (iterator.bottom < iterator.top)
    iterator.bottom = iterator.top;
  iterator.dirty = 1;
  iterator.Begin(1);
  i32 next_x = 0, next_z = 0;
  iterator.Next(&next_x, &next_z);
  scan_x = 2u * u32(next_x);
  scan_z = 2u * u32(next_z);
  return bound;
}

// Find an existing modifier by its unsigned type key. Sorted stable entries preserve the original ordered lookup.
CtCrimeMod *CtCrimeMods::Find(u32 type) const {
  CtCrimeMod *entry = first;
  while (entry && entry->type < type)
    entry = entry->next;
  return entry && entry->type == type ? entry : nullptr;
}

// Return an existing modifier or insert a zero value in unsigned-key order without moving other game entries.
// This replaces only the original map's value ownership and keeps traversal live across original callbacks.
CtCrimeMod *CtCrimeMods::Get(u32 type) {
  CtCrimeMod **link = &first;
  while (*link && (*link)->type < type)
    link = &(*link)->next;
  if (*link && (*link)->type == type)
    return *link;
  CtCrimeMod *entry = new CtCrimeMod{*link, type, 0, 0};
  *link = entry;
  return entry;
}

// Count the current modifier entries before serialization captures the original map size.
u32 CtCrimeMods::Count() const {
  u32 count = 0;
  for (CtCrimeMod *entry = first; entry; entry = entry->next)
    ++count;
  return count;
}

bool CtCrime::OccupantInserted(u32, ScOccupant *occupant) {
  return OccupantChanged(0, occupant) != 0;
}

bool CtCrime::OccupantRemoved(u32, ScOccupant *occupant) {
  return OccupantChanged(1, occupant) != 0;
}

// Ignore inactive buildings, select the nearest ancestor modifier in unsigned-key order and cache it in the SAME map.
// Expand the occupant rectangle by the stored radius, clamp to map bounds and wrap each signed-short working update.
u8 CtCrime::OccupantChanged(u32 change, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  if (change > 1)
    return 1;
  void *building_identity = nullptr;
  if (occupant->QueryInterface(IID_BdBuilding, &building_identity)) {
    BdBuilding *building = static_cast<BdBuilding *>(building_identity);
    bool active = building->GetOccupancyState() == 1;
    building->Release();
    if (!active)
      return 1;
  }
  u32 type = occupant->TypeId();
  CtCrimeMod *entry = agent_modifiers.Find(type);
  i32 amount = 0;
  u8 radius = 0;
  if (entry) {
    radius = entry->radius;
    amount = entry->amount;
  } else {
    u32 nearest = UINT32_MAX;
    for (entry = agent_modifiers.first; entry; entry = entry->next) {
      u32 distance;
      if (agent_types->IsAncestor(entry->type, type, &distance) && distance < nearest) {
        nearest = distance;
        radius = entry->radius;
        amount = entry->amount;
      }
    }
    entry = agent_modifiers.Get(type);
    entry->amount = i8(amount);
    entry->radius = radius;
  }
  if (change == 1)
    amount = -amount;
  if (!amount)
    return 1;
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  bounds.left = bounds.left >= radius ? bounds.left - radius : 0;
  bounds.top = bounds.top >= radius ? bounds.top - radius : 0;
  u32 right = bounds.right + radius;
  bounds.right = UintMin(right, CellCountX() - 1);
  u32 bottom = bounds.bottom + radius;
  bounds.bottom = UintMin(bottom, CellCountZ() - 1);
  for (u32 x = bounds.left; i32(x) <= i32(bounds.right); ++x) {
    for (u32 z = bounds.top; i32(z) <= i32(bounds.bottom); ++z) {
      i16 value;
      working.GetValue(x, z, &value);
      value = i16(u16(value) + u16(amount));
      working.SetValue(x, z, &value);
    }
  }
  return 1;
}

// Load primary cells, cursor/aggregate/graph state and versioned iterator/working-map/saved modifier entries in order.
// Clamp each cursor even after an earlier read refused; failed loading marks the actual record for abort before cleanup.
bool CtCrime::Init(CtCity *source_city, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source_city, segment, RsKey{kRsCityLayerRecordType, kCtCrimeRecordGroup, 0});
  bool result = frame.IsValid() != 0;
  RsStream *record = frame.GetDBSerialRecordPointer();
  if (result)
    result = SerialRead(record);
  if (!frame.RecordVersion() && result) {
    u8 legacy_byte;
    result = record->ReadU8(&legacy_byte);
  }
  if (result)
    result = record->ReadU32(&scan_x);
  u32 columns = CellCountX();
  if (scan_x >= columns)
    scan_x = 0;
  if (result)
    result = record->ReadU32(&scan_z);
  u32 rows = CellCountZ();
  if (scan_z >= rows)
    scan_z = 0;
  result = result && record->ReadU32(&sum) && record->ReadU32(&sample_count) && record->ReadU8(&average) &&
      graph->SerialRead(source_city, record) != 0;
  if (frame.RecordVersion()) {
    result = result && iterator.SerialRead(source_city, record) && working.SerialRead(record);
    i32 count = 0;
    if (result)
      result = record->ReadI32(&count);
    while (result && count > 0) {
      u32 type = 0;
      i32 amount = 0;
      u8 radius = 0;
      result = record->ReadU32(&type) && record->ReadI32(&amount) && record->ReadU8(&radius);
      if (result) {
        CtCrimeMod *entry = modifiers.Get(type);
        entry->amount = i8(amount);
        entry->radius = radius;
      }
      --count;
    }
  }
  if (!result)
    frame.SetFailed();
  return result;
}

// Write the version1 record through primary cells, cursor/aggregates/graph, iterator, working cells and shared modifiers.
// Capture the map count before its write callback, then visit live sorted entries with separate key/amount/radius calls.
bool CtCrime::Save(CtCity *source_city, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source_city, segment, RsKey{kRsCityLayerRecordType, kCtCrimeRecordGroup, 0}, 1, false);
  bool result = frame.IsValid() != 0;
  RsStream *record = frame.GetDBSerialRecordPointer();
  result = result && SerialWrite(record) && record->WriteU32(scan_x) && record->WriteU32(scan_z) &&
      record->WriteU32(sum) && record->WriteU32(sample_count) && record->WriteU8(average) &&
      graph->SerialWrite(source_city, record) != 0 && iterator.SerialWrite(source_city, record) &&
      working.SerialWrite(record);
  if (result) {
    i32 remaining = i32(agent_modifiers.Count());
    result = record->WriteI32(remaining);
    for (CtCrimeMod *entry = agent_modifiers.first; remaining > 0 && entry; entry = entry->next) {
      if (!result)
        break;
      result = record->WriteU32(entry->type) && record->WriteI32(entry->amount) && record->WriteU8(entry->radius);
      --remaining;
    }
  }
  if (!result)
    frame.SetFailed();
  return result;
}

// Replace the two mapped bytes without moving an existing entry; the enumeration context is unused.
void CtCrime::RegisterAgentModifier(const ScString &key, const ScString &value, ApIniContext *) {
  ScString key_copy(key), remaining(value), token;
  u32 type = ScConvertToUint32(key_copy.begin);
  {
    ScString extracted = remaining.SplitToken(",");
    token = extracted;
  }
  if (token.begin == token.end)
    return;
  u8 radius = u8(ScConvertToUint32(token.begin));
  token = remaining;
  if (token.begin == token.end)
    return;
  i32 amount = i32(atol(token.begin));
  amount = amount < -127 ? -127 : amount > 127 ? 127 : amount;
  CtCrimeMod *entry = agent_modifiers.Get(type);
  entry->amount = i8(amount);
  entry->radius = radius;
}

// Clear shared modifiers, enumerate the actual crime INI section and preserve previous tuning for missing keys.
// Narrow update period before its zero-to-one rule; police factor remains an unchecked byte and effects clamp to500.
bool CtCrime::StaticInit(CtDefinition *) {
  ScString path, value;
  ApCompressedIni ini;
  agent_modifiers.Clear();
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Crime.INI");
  ini.SetPath(path);
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  ini.EnumEntries(
      "AgentIDModifiers", RegisterAgentModifier, reinterpret_cast<ApIniContext *>(static_cast<CtSizedByteMap *>(this)));
  if (ini.ReadEntry("TuningParameters", "UpdatePeriod", &value)) {
    update_period = u16(ScConvertToUint32(value.begin));
    if (!update_period)
      update_period = 1;
  }
  if (ini.ReadEntry("TuningParameters", "PoliceFactor", &value))
    police_factor = u8(ScConvertToUint32(value.begin));
  const char *kKeys[5] = {"YouthCurfewOrdEffect", "GamblingOrdEffect", "JrSportsOrdEffect",
      "NeighborhoodWatchOrdEffect", "ConservationCorpsOrdEffect"};
  i16 *kEffects[5] = {&curfew_effect, &gambling_effect, &sports_effect, &watch_effect, &corps_effect};
  for (u32 index = 0; index < 5; ++index) {
    if (ini.ReadEntry("TuningParameters", kKeys[index], &value)) {
      i32 amount = i32(atol(value.begin));
      *kEffects[index] = i16(amount < -500 ? -500 : amount > 500 ? 500 : amount);
    }
  }
  return true;
}

// Remove one actual saved snapshot entry after advancing its owning link. Other modifier addresses remain stable.
void CtCrimeMods::Remove(u32 type) {
  CtCrimeMod **link = &first;
  while (*link && (*link)->type < type)
    link = &(*link)->next;
  if (*link && (*link)->type == type) {
    CtCrimeMod *retired = *link;
    *link = retired->next;
    delete retired;
  }
}

// Compare saved modifiers with current entries and nearest parents, preserving the original rule for absent new keys.
// Clear the snapshot before optional working-map rebuild, subscribe to occupants, refresh labels and add the agent.
bool CtCrime::SimulationBegin() {
  ScOccupantManager *occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return false;
  bool rebuild = true;
  if (modifiers.first) {
    rebuild = false;
    for (CtCrimeMod *entry = agent_modifiers.first; entry; entry = entry->next) {
      CtCrimeMod *saved = modifiers.Find(entry->type);
      if (saved) {
        if (saved->amount != entry->amount || saved->radius != entry->radius) {
          rebuild = true;
          break;
        }
        modifiers.Remove(entry->type);
      }
    }
    if (!rebuild) {
      for (CtCrimeMod *entry = modifiers.first; entry; entry = entry->next) {
        i8 amount = entry->amount;
        u8 radius = entry->radius;
        if (!amount)
          continue;
        u32 type = entry->type;
        CtAgentType *node = agent_types->GetNode(type);
        bool inherited = false;
        if (node) {
          while (type != 50) {
            node = node->Parent();
            if (!node)
              break;
            type = node->Id();
            CtCrimeMod *current = agent_modifiers.Find(type);
            if (current) {
              inherited = amount == current->amount && radius == current->radius;
              break;
            }
          }
        }
        if (!inherited) {
          rebuild = true;
          break;
        }
      }
    }
  }
  modifiers.Clear();
  if (rebuild) {
    i16 zero = 0;
    working.SetAllCells(&zero);
  }
  u32 subscription;
  if (!occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &subscription, rebuild))
    return false;
  UpdateHistoryObjects();
  CtService::CtGetCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  return true;
}

// Capture the current simulator before obtaining the bound city's optional occupant manager and withdrawing both views.
// Ignore each unregister/remove reply and preserve the original successful result.
bool CtCrime::SimulationEnd() {
  CtSimulatorInterface *simulator = CtService::CtGetCity()->CitySimulator();
  ScOccupantManager *occupants = city->SurfaceOccupantManager();
  if (occupants)
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  if (simulator)
    simulator->RemoveAgent(static_cast<CtAgent *>(this));
  return true;
}

// Format three live graph values through one retained language utility and reuse the same numeric and label Strings.
void CtCrime::UpdateHistoryObjects() {
  ScString label, number;
  ScLanguageUtility *utility = CtService::CtGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  u32 value = 0;
  for (u32 dataset = 0; dataset < 3; ++dataset) {
    if (!dataset)
      graph->GetMonthData(&value);
    else if (dataset == 1)
      graph->GetYearData(&value);
    else
      graph->GetDecadeData(&value);
    utility->MakeNumberString(i64(i32(value)), &number);
    const char *numeric_text = number.begin, *format = number_format.ToChar();
    label.Sprintf(format, numeric_text);
    graph->SetCurrentValueLabel(ScGraphDataSet(dataset), label);
  }
}

// Add the current crime byte monthly, roll yearly only in month1, and roll decades in years divisible by10.
void CtCrime::EndOfMonth() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  graph->SetMonthData(average);
  if (month == 1) {
    u32 value = graph->GetMonthAverage();
    graph->SetYearData(value);
    if (!(year % 10)) {
      value = graph->GetYearAverage();
      graph->SetDecadeData(value);
    }
  }
  UpdateHistoryObjects();
}

// Run the daily scan for interval2 and month-end history updates for interval4; other intervals report success directly.
// Only the daily worker's actual AL0/1 result replaces the original default true result.
bool CtCrime::Simulate(i32 elapsed) {
  if (elapsed == 2)
    return DoDailySim() != 0;
  if (elapsed == 4)
    EndOfMonth();
  return true;
}

// The original stores the crime pointer only after the zone retain; construction preserves that publication order.
CtCrimeMapSprite::CtCrimeMapSprite(CtCrime *source, CtZoneLayer *zone) : zones(nullptr) {
  if (zone)
    zones = ScAddRefNotNull(zone);
  crime = ScAddRefNotNull(source);
  u32 rows = crime->CellCountZ(), columns = crime->CellCountX();
  SetId(10013);
  SetBaseSize(columns, rows);
}

CtCrimeMapSprite::~CtCrimeMapSprite() {
  ScRelease(&crime);
  ScRelease(&zones);
}

// Skip unzoned or zero-crime cells without touching the output, then scale the crime byte into the current palette.
// Preserve the PC low-DWORD product and signed division by256 before forwarding the actual palette result.
bool CtCrimeMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  if (zones) {
    u8 zone;
    zones->GetValue(x, z, &zone);
    if (!zone)
      return false;
  }
  u8 cell;
  crime->GetValue(x, z, &cell);
  if (!cell)
    return false;
  GxPaletteView *source = GetPalette();
  if (!source)
    return false;
  i32 entries = source->GetNumberEntries();
  return source->GetColor(i32(u32(entries) * u32(cell)) / 256, out);
}

// Route query creation to the actual helper and acknowledge history delivery even when its source is absent or refuses.
// A successful model query receives the live graph view before the acquired receiver is released.
bool CtCrime::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(graph);
      static_cast<ScUnknown *>(selected)->Release();
    }
    return true;
  }
  return false;
}

// Dispatch each original query subtype through its real acquired interface, preserving ignored helper results.
// Raster creation/addition retains its raw byte result and releases the published temporary even after refusal.
u8 CtCrime::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (!source || !source->QueryInterface(IID_UiGraphsViewIdentity, &selected))
      return 0;
    static_cast<ScObjectCollection *>(selected)->Add(graph);
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagDialog:
    if (!source || !source->QueryInterface(IID_UiQueryWindow, &selected))
      return 0;
    DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagDebug:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return 0;
    DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagMaps: {
    if (!source || !source->QueryInterface(IID_UiRasterQuery, &selected))
      return 0;
    void *raster = nullptr;
    u8 result = static_cast<SpMapSpriteFactory *>(this)->CreateMapSprite(10013, IID_GxRasterSource, &raster);
    if (result)
      result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
    if (raster) {
      static_cast<ScUnknown *>(raster)->Release();
      raster = nullptr;
    }
    static_cast<ScUnknown *>(selected)->Release();
    return result;
  }
  case kUiQueryTagStandard:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return 0;
    DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  default:
    return 0;
  }
}

u8 CtCrime::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 CtCrime::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u32 CtCrime::GetInfoStringCount() {
  return 1;
}

// Initialize location and Strings before querying coordinates, then publish the available standard crime row.
// Ignore individual text-add results and preserve success once the location lookup succeeds.
u8 CtCrime::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  bool warning = false;
  if (!query || !query->GetLocation(&location))
    return 0;
  for (u32 category = 0; category < GetInfoStringCount(); ++category)
    if (GetInfoStrings(u32(location.x >> 8), u32(location.z >> 8), &label, &value, category, &warning))
      query->AddText(&label, &value, 2048, u8(warning));
  return 1;
}

// Clear the optional warning first; only category0 publishes a localized label and byte-threshold crime grade.
// Release each text resource before its external copy, and set the severe warning after the final text acquisition.
u8 CtCrime::GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, bool *warning) {
  if (warning)
    *warning = false;
  if (category)
    return 0;
  ScString text = static_cast<ScString>(CtService::ScTextResource(kRsCityServiceCrimeText, kRsCityServiceTextGroup));
  label->Copy(text);
  u8 cell;
  GetValue(x, z, &cell);
  u32 grade = !cell ? 28 : cell < 47 ? 29 : cell < 84 ? 30 : cell < 122 ? 31 : cell < 151 ? 32 : 33;
  text = static_cast<ScString>(CtService::ScTextResource(grade, kRsCityServiceTextGroup));
  if (grade == 33 && warning)
    *warning = true;
  value->Copy(text);
  return 1;
}

u8 CtFlammability::leaf_effect = 10, CtFlammability::detector_effect = 35, CtFlammability::water_effect = 25;
