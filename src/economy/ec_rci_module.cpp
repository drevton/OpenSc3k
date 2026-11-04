#include "economy/ec_rci_module.h"

#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_civic.h"
#include "economy/ec_development.h"
#include "economy/ec_land_value.h"
#include "economy/ec_landfill_developer.h"
#include "economy/ec_neighbor_gate.h"
#include "economy/ec_rci_graph_info.h"
#include "economy/ec_rci_random.h"
#include "economy/ec_rci_text.h"
#include "economy/ec_residential.h"
#include "economy/ec_sector.h"
#include "economy/ec_zones.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/stream.h"
#include "window/api.h"

EcRciDirector::EcRciDirector() {
  AddClass(kEcZoneLayerClassId, EcCreateRciClass10036660, 0);
  AddClass(kEcCommercialClassId, EcCreateRciCommercial, 0);
  AddClass(kEcIndustrialClassId, EcCreateRciIndustrial, 0);
  AddClass(kEcLandValueClassId, EcCreateRciClass10036702, 0);
  AddClass(kEcResidentialClassId, EcCreateRciResidential, 0);
  AddClass(kEcDemandClassId, EcCreateDemand, 0);
  AddClass(kEcValveClassId, EcCreateRciValve, 0);
  AddClass(kEcResidentialDeveloperClassId, EcCreateRciResidentialDeveloper, 0);
  AddClass(kEcCommercialDeveloperClassId, EcCreateRciClass1003680E, 0);
  AddClass(kEcIndustrialDeveloperClassId, EcCreateRciClass10036843, 0);
  AddClass(kEcIndustrialPortDeveloperClassId, EcCreateRciClass10036878, 0);
  AddClass(kEcCommercialPortDeveloperClassId, EcCreateRciClass100368AA, 0);
  AddClass(kEcLandfillDeveloperClassId, EcCreateRciClass100368DC, 0);
  AddClass(kEcMixedLotRuleClassId, EcCreateRciClass10036911, 0);
  AddClass(kEcDirectionalLotRuleClassId, EcCreateRciClass1003694C, 0);
  AddClass(kEcLargeLotRuleClassId, EcCreateRciClass10036987, 0);
  AddClass(kEcIndustrialStripRuleClassId, EcCreateRciClass100369C2, 0);
  AddClass(kEcBlockRuleClassId, EcCreateRciBlockRule, 0);
  AddClass(kEcLotRuleClassId, EcCreateRciLotRule, 0);
  AddClass(kEcTransportBlockRuleClassId, EcCreateRciTransportBlockRule, 0);
  AddClass(kEcIndustrialBlockRule2ClassId, EcCreateRciIndustrialBlockRule2, 0);
  AddClass(kEcIndustrialBlockRule3ClassId, EcCreateRciIndustrialBlockRule3, 0);
  AddClass(kEcIndustrialBlockRuleClassId, EcCreateRciIndustrialBlockRule, 0);
  AddClass(kEcCommercialLotRule2ClassId, EcCreateRciCommercialLotRule2, 0);
  AddClass(kEcCommercialLotRule3ClassId, EcCreateRciCommercialLotRule3, 0);
  AddClass(kEcCommercialBlockRuleClassId, EcCreateRciCommercialBlockRule, 0);
  AddClass(kEcPortRoadRuleClassId, EcCreateRciClass10036C10, 0);
  AddClass(kEcPortBlockRuleClassId, EcCreateRciPortBlockRule, 0);
  AddClass(kEcPortBuildingRuleClassId, EcCreateRciClass10036C86, 0);
  AddClass(kEcPortPieceRuleClassId, EcCreateRciClass10036CC1, 0);
  AddClass(kEcZoneCommandFactoryClassId, EcCreateRciClass10036CFC, 0);
  AddClass(kEcSchoolClassId, EcCreateRciSchool, 0);
  AddClass(kEcLibraryClassId, EcCreateRciLibrary, 0);
  AddClass(kEcMuseumClassId, EcCreateRciMuseum, 0);
  AddClass(kEcCollegeClassId, EcCreateRciCollege, 0);
  AddClass(kEcHospitalClassId, EcCreateRciHospital, 0);
  AddClass(kEcNeighborGateClassId, EcCreateRciClass10036E61, 0);
}

EcRciDirector::~EcRciDirector() {
}

// Construct the original zoning layer and publish its zone-layer identity without adding a reference.
ScUnknown *EcCreateRciClass10036660() {
  return static_cast<CtZoneLayer *>(new EcZoneLayer);
}

// Create the actual land-value layer and publish its original land-value interface view.
ScUnknown *EcCreateRciClass10036702() {
  return static_cast<CtLandValueLayer *>(new EcLandValue);
}

// Create the landfill developer and publish its original primary zoning-developer identity.
ScUnknown *EcCreateRciClass100368DC() {
  return static_cast<CtZoneDeveloper *>(new EcLandfillDeveloper);
}

// Create the industrial strip placement rule and return its original zone-rule identity.
ScUnknown *EcCreateRciClass100369C2() {
  return static_cast<EcZoneRule *>(new EcIndustrialStripRule);
}

// Create the directional two-cell lot rule and publish its zone-rule identity.
ScUnknown *EcCreateRciClass1003694C() {
  return static_cast<EcZoneRule *>(new EcDirectionalLotRule);
}

// Create the large-lot placement rule and publish its zone-rule identity.
ScUnknown *EcCreateRciClass10036987() {
  return static_cast<EcZoneRule *>(new EcLargeLotRule);
}

// Construct the mixed-size lot rule and publish its separate original Unknown identity.
ScUnknown *EcCreateRciClass10036911() {
  return static_cast<EcRuleUnknown *>(new EcMixedLotRule);
}

// Construct the waterfront road rule and publish its separate original Unknown identity.
ScUnknown *EcCreateRciClass10036C10() {
  return static_cast<EcRuleUnknown *>(new EcPortRoadRule);
}

// Construct the civic neighbor gate and return its original primary building view.
ScUnknown *EcCreateRciClass10036E61() {
  return static_cast<BdBuilding *>(new EcNeighborGate);
}

u32 EcRciDirector::GetDirectorID() const {
  return kEcRciDirectorId;
}

// Common OnStart uses the SIMRCI module's registry service.
// Invoke the live registration hook before releasing the temporary registry acquisition.
bool EcRciDirector::OnStart(ScCom *) {
  void *object = nullptr;
  EcGetRciFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  ScRegistry *registry = static_cast<ScRegistry *>(object);
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

bool EcRciDirector::AppRegister(ScRegistry *) {
  return true;
}

// Return the lazily constructed SIMRCI director, whose static lifetime owns the real registration table.
ScDirector *EcGetRciDirector() {
  static EcRciDirector director;
  return &director;
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciResidential() {
  return static_cast<CtResidentialLayer *>(new EcResidential);
}

// Create the residential developer and return its primary zone-developer identity.
ScUnknown *EcCreateRciResidentialDeveloper() {
  return static_cast<CtZoneDeveloper *>(new EcResidentialDeveloper);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciValve() {
  return static_cast<::EcValve *>(new EcRci::EcValve);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciSchool() {
  return static_cast<BdBuilding *>(new EcSchool);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciLibrary() {
  return static_cast<BdBuilding *>(new EcLibrary);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciMuseum() {
  return static_cast<BdBuilding *>(new EcMuseum);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciCollege() {
  return static_cast<BdBuilding *>(new EcCollege);
}

// Create the actual class and publish its original selected view without acquiring an extra reference.
ScUnknown *EcCreateRciHospital() {
  return static_cast<BdBuilding *>(new EcHospital);
}

// Create the actual commercial layer and publish its primary identity without an additional retain.
ScUnknown *EcCreateRciCommercial() {
  return static_cast<EcCommercialLayer *>(new EcCommercial);
}

// Create the actual industrial layer and publish its primary identity without an additional retain.
ScUnknown *EcCreateRciIndustrial() {
  return static_cast<EcIndustrialLayer *>(new EcIndustrial);
}

// Construct the actual residential block rule and return its separate Unknown identity.
ScUnknown *EcCreateRciBlockRule() {
  return static_cast<EcRuleUnknown *>(new EcBlockRule);
}

// Create the transport-aware block rule and expose its separate original Unknown identity.
ScUnknown *EcCreateRciTransportBlockRule() {
  return static_cast<EcRuleUnknown *>(new EcTransportBlockRule);
}

// Create the port block rule and expose its separate original Unknown identity.
ScUnknown *EcCreateRciPortBlockRule() {
  return static_cast<EcRuleUnknown *>(new EcPortBlockRule);
}

// Create the residential lot rule and expose its separate original Unknown identity.
ScUnknown *EcCreateRciLotRule() {
  return static_cast<EcRuleUnknown *>(new EcLotRule);
}

// Create the second industrial block rule and expose its original Unknown identity.
ScUnknown *EcCreateRciIndustrialBlockRule2() {
  return static_cast<EcRuleUnknown *>(new EcIndustrialBlockRule2);
}

// Create the third industrial block rule and expose its original Unknown identity.
ScUnknown *EcCreateRciIndustrialBlockRule3() {
  return static_cast<EcRuleUnknown *>(new EcIndustrialBlockRule3);
}

// Create the first industrial block rule and expose its original Unknown identity.
ScUnknown *EcCreateRciIndustrialBlockRule() {
  return static_cast<EcRuleUnknown *>(new EcIndustrialBlockRule);
}

// Create the second commercial lot rule and expose its original Unknown identity.
ScUnknown *EcCreateRciCommercialLotRule2() {
  return static_cast<EcRuleUnknown *>(new EcCommercialLotRule2);
}

// Create the third commercial lot rule and expose its original Unknown identity.
ScUnknown *EcCreateRciCommercialLotRule3() {
  return static_cast<EcRuleUnknown *>(new EcCommercialLotRule3);
}

// Create the commercial block rule and expose its original Unknown identity.
ScUnknown *EcCreateRciCommercialBlockRule() {
  return static_cast<EcRuleUnknown *>(new EcCommercialBlockRule);
}

namespace EcRci {

// Preserve the original PC53 magic-add rounding and low-DWORD wrap instead of using a host integer conversion.
static u32 ScRoundGraphValue(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Construct the real lifetime and four Strings before setting graph defaults and filling all three histories with
// initial.
// The supplied unsigned value is both the first maximum and each queue element, not a zero-fill request.
ScGraphInfo::ScGraphInfo(u32 initial)
    : selectionLabelId(0), valueLabelId(0), color(0), maximum(initial), automaticScale(false), minimum(10),
      scaleFactor(1) {
  for (u32 i = 0; i < 12; ++i)
    monthly[i] = initial;
  for (u32 i = 0; i < 10; ++i)
    yearly[i] = initial;
  for (u32 i = 0; i < 10; ++i)
    decades[i] = initial;
}

// Retire the three histories before the four Strings and real module lifetime base in reverse member order.
ScGraphInfo::~ScGraphInfo() = default;

// Publish the original secondary graph identity for either
// accepted IID and retain through the real lifetime primary. Unsupported requests clear the output.
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

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetMonthData(u32 value) {
  if (automaticScale) {
    if (value > ScRoundGraphValue(f64(maximum) / scaleFactor))
      ComputeAutoMax();
  } else if (value > maximum) {
    value = maximum;
  }
  for (u32 i = 11; i; --i)
    monthly[i] = monthly[i - 1];
  monthly[0] = value;
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
  for (u32 i = 9; i; --i)
    yearly[i] = yearly[i - 1];
  yearly[0] = value;
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
  for (u32 i = 9; i; --i)
    decades[i] = decades[i - 1];
  decades[0] = value;
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

void ScGraphInfo::SetAutoMaxFactors(u32 floor, f32 factor) {
  minimum = floor;
  scaleFactor = factor >= 1 ? factor : 1;
  ComputeAutoMax();
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
  EcGetRciDirector()->AddRef();
  return ++references;
}

// Leave zero unchanged; otherwise remove the actual module hold before decrementing the live receiver count.
u32 ScRefCount::RemoveRef() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScRefCount::RefCount() {
  return references;
}
} // namespace EcRci

static bool EcRciApplicationInitialized;
static void *EcRciApplication;
static ScMessageServer *EcRciMessages;
static RsManager *EcRciResources;
static ScLanguageManager *EcRciLanguage;
static WnResources *EcRciWindowResources;
static WnWindowManager *EcRciWindows;
static GxGraphics *EcRciGraphics;
static RsFileSystem *EcRciFileSystem;

// Borrow the module director's current framework without acquiring an additional reference.
ScFramework *EcGetRciFramework() {
  return EcGetRciDirector()->FrameWork();
}

// Return the actual COM root captured by this module director, retaining neither receiver.
ScCom *EcGetRciCom() {
  return EcGetRciDirector()->GZCOM();
}

// Guard acquisition before entering the framework and let QueryInterface publish directly into the live cache.
// Recursive calls observe the same slot; the ignored query reply does not discard a published application.
ApGameInterface *EcGetRciApplication() {
  if (!EcRciApplicationInitialized) {
    EcRciApplicationInitialized = true;
    EcGetRciFramework()->Application()->QueryInterface(IID_ApGameInterface, &EcRciApplication);
  }
  return static_cast<ApGameInterface *>(EcRciApplication);
}

// Ask the acquired application for its live agent-type tree. Missing application acquisition leaves a null tree.
CtAgentTree *EcGetRciAgentTree() {
  ApGameInterface *application = EcGetRciApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Borrow the live application city without acquiring a reference. A missing application has no current city.
CtCity *EcGetRciCity() {
  ApGameInterface *application = EcGetRciApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow this application's actual occupant-attribute cache. The module getter does not retain the returned cache.
OcAttribCache *EcGetRciAttribCache() {
  ApGameInterface *application = EcGetRciApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Cache the borrowed message server before releasing its temporary service lookup hold. Retry when no service is published.
ScMessageServer *EcGetRciMessageServer() {
  if (!EcRciMessages) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    EcRciMessages = server;
    if (server)
      server->Release();
  }
  return EcRciMessages;
}

// Publish the borrowed resource manager before releasing its temporary lookup hold. A null cache permits a later retry.
RsManager *EcGetRciResourceManager() {
  if (!EcRciResources) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    EcRciResources = manager;
    if (manager)
      manager->Release();
  }
  return EcRciResources;
}

// Cache the borrowed language manager after publishing its actual lookup result and release the temporary hold.
ScLanguageManager *EcGetRciLanguageManager() {
  if (!EcRciLanguage) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(object);
    EcRciLanguage = manager;
    if (manager)
      manager->Release();
  }
  return EcRciLanguage;
}

// Publish the borrowed window-resource service before releasing its temporary acquisition hold.
WnResources *EcGetRciWindowResources() {
  if (!EcRciWindowResources) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    WnResources *resources = static_cast<WnResources *>(object);
    EcRciWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return EcRciWindowResources;
}

// Cache the borrowed graphics service before releasing its temporary acquisition reference.
GxGraphics *EcGetRciGraphics() {
  if (!EcRciGraphics) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &object);
    GxGraphics *graphics = static_cast<GxGraphics *>(object);
    EcRciGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return EcRciGraphics;
}

// Cache the borrowed window manager before dropping the temporary framework service reference.
WnWindowManager *EcGetRciWindowManager() {
  if (!EcRciWindows) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    WnWindowManager *windows = static_cast<WnWindowManager *>(object);
    EcRciWindows = windows;
    if (windows)
      windows->Release();
  }
  return EcRciWindows;
}

// Cache the borrowed filesystem service before releasing its temporary lookup hold, retrying after a null lookup.
RsFileSystem *EcGetRciFileSystem() {
  if (!EcRciFileSystem) {
    void *object = nullptr;
    EcGetRciFramework()->GetSystemService(RsFileSystem::ServiceId, RsFileSystem::Iid, &object);
    RsFileSystem *system = static_cast<RsFileSystem *>(object);
    EcRciFileSystem = system;
    if (system)
      system->Release();
  }
  return EcRciFileSystem;
}

namespace EcRci {
ScRandom::ScRandom(u32 seed) {
  SetSeed(seed);
  normal_ready = 0;
}

// Replace the all-ones seed with the multimedia tick and publish it to both streams.
void ScRandom::SetSeed(u32 seed) {
  if (seed == kScRandomSeedFromClock)
    seed = ScMultimediaMilliseconds();
  integer_seed = seed;
  real_seed = seed;
}

// Advance the integer stream using the installed high-word mixing; zero bound consumes no random state.
// Small ranges use remainder and larger ranges retain the original inclusive rejection condition.
u32 ScRandom::Bounded(u32 bound) {
  if (!bound)
    return 0;
  u32 next = integer_seed, value;
  do {
    u64 product = u64(next) * kScRandomMultiplier;
    u32 low = u32(product), high = u32(product >> 32) << 16;
    next = low + kScRandomIncrement;
    high += 65535u + (next < low);
    value = (high & kScRandomRetainedHighWordMask) | (next >> 16);
    if (bound < kScRandomModuloThreshold) {
      value %= bound;
      break;
    }
  } while (value > bound);
  integer_seed = next;
  return value;
}

// Advance the independent real stream, substituting the original nonzero seed before applying the source scale.
f64 ScRandom::RandomDoubleUniform() {
  if (!real_seed)
    real_seed = kScRandomFallbackRealSeed;
  real_seed *= kScRandomRealMultiplier;
  return f64(real_seed) * kScRandomRealScale;
}
} // namespace EcRci

namespace EcRci {
// Construct the original localized text key and publish its acquired String resource directly.
ScTextResource::ScTextResource(u32 instance, u32 group)
    : text_identity(nullptr), key{kRsTextResourceType, group, instance} {
  if (!EcGetRciResourceManager()->GetResource(key, IID_ScStringView, &text_identity))
    text_identity = nullptr;
}

ScTextResource::~ScTextResource() {
  if (text_identity) {
    static_cast<ScStringView *>(text_identity)->Release();
    text_identity = nullptr;
  }
}

ScString ScTextResource::GetText() {
  ScStringView *text = static_cast<ScStringView *>(text_identity);
  return text ? ScString(*text) : ScString();
}

// This private field getter shares a folded machine-code body.
// Borrow the retained string resource without acquiring another reference.
ScStringView *ScTextResource::GetResource() {
  return static_cast<ScStringView *>(text_identity);
}
} // namespace EcRci

namespace EcRci {

// Initialize the original scalar state without acquiring a city or a module reference.
EcValve::EcValve()
    : references(0), identifier(0), supply(0), demand(0), supply_accumulator(0), demand_accumulator(0),
      initial_supply(0), initial_demand(0), maximum(0), minimum(0), balance(0), flag_30(true), flag_31(true),
      economy_modifier(0), tax_modifier(0), demand_cap(0), city(nullptr) {
}

EcValve::~EcValve() {
  EcValve::Shutdown();
}

// Publish this same valve view for the two supported identities before acquiring its reference.
bool EcValve::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_EcValve, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the module, then return its live post-callback value.
u32 EcValve::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Release the module before decrementing a nonzero local count and destroy the valve when the live count reaches zero.
u32 EcValve::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Bind the borrowed city and identifier while resetting simulation values and modifiers, preserving flags30/31 ordering.
// The listener is unused by the installed body; reference count and flag31 retain their entry values.
bool EcValve::Init(CtCity *owner, u32 event_id, CtLayer *) {
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

bool EcValve::Shutdown() {
  city = nullptr;
  return true;
}

// Recalculate an uninitialized valve before posting its current value, retaining the recalculation's separate notification.
bool EcValve::SimulationBegin() {
  if (flag_30)
    UpdateValue(0);
  NotifyValue();
  return true;
}

// Restore the ten serialized values in source order, retaining partial scalar assignments when a read fails.
// Notification occurs even after failure; zero initializes only the otherwise undefined first unread temporary.
bool EcValve::SerialRead(CtCity *, RsStream *record) {
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

u8 EcValve::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = record->WriteU32(static_cast<u32>(supply));
  result = result && record->WriteU32(static_cast<u32>(demand)) && record->WriteU32(supply_accumulator) &&
      record->WriteU32(demand_accumulator) && record->WriteF32(economy_modifier) && record->WriteF32(tax_modifier) &&
      record->WriteU8(demand_cap) && record->WriteU32(static_cast<u32>(balance)) && record->WriteI32(initial_supply) &&
      record->WriteI32(initial_demand);
  return result;
}

u32 EcValve::GetId() {
  return identifier;
}

void EcValve::SetId(u32 event_id) {
  identifier = event_id;
}

i32 EcValve::QuerySupplyValue() {
  return supply;
}

i32 EcValve::QueryDemandValue() {
  return demand;
}

i32 EcValve::QueryValveValue() {
  return balance;
}

f32 EcValve::QueryEconomyModifier() {
  return economy_modifier;
}

i32 EcValve::QueryValveMax() {
  return maximum;
}

i32 EcValve::QueryValveMin() {
  return minimum;
}

bool EcValve::AddToSupplyValue(i32 amount) {
  supply_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcValve::AddToDemandValue(i32 amount) {
  demand_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcValve::SetSupplyValue(i32 amount) {
  supply = amount;
  return true;
}

bool EcValve::SetDemandValue(i32 amount) {
  demand = amount;
  return true;
}

void EcValve::SetValveMax(i32 upper) {
  maximum = upper < minimum ? minimum : upper;
}

void EcValve::SetValveMin(i32 lower) {
  minimum = lower > maximum ? maximum : lower;
}

// Remove the previous initial supply contribution before adding and remembering its replacement.
void EcValve::SetInitialSupplyValue(i32 amount) {
  supply = i32(u32(supply) - u32(initial_supply));
  supply = i32(u32(supply) + u32(amount));
  initial_supply = amount;
}

// Replace the initial demand contribution while retaining the source subtraction/addition ordering.
void EcValve::SetInitialDemandValue(i32 amount) {
  demand = i32(u32(demand) - u32(initial_demand));
  demand = i32(u32(demand) + u32(amount));
  initial_demand = amount;
}

f32 EcValve::GetEconomyModifier() {
  return economy_modifier;
}

void EcValve::SetEconomyModifier(f32 modifier) {
  economy_modifier = modifier;
}

f32 EcValve::GetTaxModifier() {
  return tax_modifier;
}

void EcValve::SetTaxModifier(f32 modifier) {
  tax_modifier = modifier;
}

u8 EcValve::GetDemandCap() {
  return demand_cap;
}

void EcValve::SetDemandCap(u8 cap) {
  demand_cap = cap > 100 ? 100 : cap;
}

// Publish accumulated supply/demand, reset accumulators to initial contributions, then recalculate with the stored flag.
bool EcValve::EndOfMonth() {
  u8 smooth = flag_31;
  supply = static_cast<i32>(supply_accumulator);
  demand = static_cast<i32>(demand_accumulator);
  supply_accumulator = static_cast<u32>(initial_supply);
  demand_accumulator = static_cast<u32>(initial_demand);
  UpdateValue(smooth);
  return true;
}

void EcValve::DebugLockValue(i32) {
}

void EcValve::DebugUnlockValue() {
}

bool EcValve::DebugIsValueLocked() {
  return false;
}

void EcValve::UpdateValue(u8 smooth) {
  i32 upper = maximum, difference = i32(u32(demand) - u32(supply));
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
  i32 target = static_cast<i32>(adjusted);
  i32 cap_value = i32(u32(upper) * u32(demand_cap)) / 100;
  i32 capped = i32(u32(upper) - u32(cap_value));
  if (adjusted > capped)
    target = capped;
  if (target < minimum)
    target = minimum;
  if (!smooth) {
    balance = target;
  } else {
    i64 step = 7 * (static_cast<i64>(upper) - minimum) / 100;
    i32 delta = i32(u32(target) - u32(balance));
    if (delta >= step || delta <= -step) {
      if (delta > 0)
        if (target > 0 && balance < 0 && static_cast<i64>(balance) + step < 0)
          balance = 0;
        else
          balance = i32(u32(balance) + u32(step));
      else if (target < 0 && balance > 0 && static_cast<i64>(balance) - step > 0)
        balance = 0;
      else
        balance = i32(u32(balance) - u32(step));
    } else {
      balance = target;
    }
  }
  NotifyValue();
}

// Build the current signed per-thousand notification before acquiring the message server, preserving live callback order.
void EcValve::NotifyValue() const {
  i32 ratio = balance >= 0 ? i32(1000u * u32(balance)) / maximum : i32(0u - 1000u * u32(balance)) / minimum;
  ScMessage message = {kEcMsg_DemandChanged, identifier, static_cast<u32>(ratio), 0};
  EcGetRciMessageServer()->MessagePost(message, false);
}

EcResidentialValve::EcResidentialValve() : valve_layer(nullptr), residential_layer(nullptr), workforce_valve(nullptr) {
}

EcResidentialValve::~EcResidentialValve() {
  EcResidentialValve::Shutdown();
}

// Initialize common state, then acquire the demand and residential interfaces in order from the supplied city.
bool EcResidentialValve::Init(CtCity *owner, u32 event_id, CtLayer *listener) {
  if (!EcValve::Init(owner, event_id, listener))
    return false;
  CtLayer *layer = owner->GetSpecificLayer(kEcValveLayerType);
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer))) {
    valve_layer = nullptr;
    EcValve::Shutdown();
    return false;
  }
  layer = owner->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer || !layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential_layer))) {
    residential_layer = nullptr;
    EcValve::Shutdown();
    return false;
  }
  return true;
}

// Release workforce, demand and residential views in the installed order, clearing each only after its callback.
bool EcResidentialValve::Shutdown() {
  ScRelease(&workforce_valve);
  ScRelease(&valve_layer);
  ScRelease(&residential_layer);
  return true;
}

// Publish the common valve's starting state before acquiring the workforce valve used for population contributions.
bool EcResidentialValve::SimulationBegin() {
  if (!EcValve::SimulationBegin())
    return false;
  return valve_layer->GetValvePointer(kEcWorkforceValveId, IID_EcValve, reinterpret_cast<void **>(&workforce_valve));
}

bool EcResidentialValve::SerialRead(CtCity *owner, RsStream *record) {
  return EcValve::SerialRead(owner, record);
}

u8 EcResidentialValve::SerialWrite(CtCity *owner, RsStream *record) const {
  return EcValve::SerialWrite(owner, record);
}

// Add residential supply first, then contribute the rounded working-age share to the live workforce valve.
bool EcResidentialValve::AddToSupplyValue(i32 amount) {
  if (amount) {
    EcValve::AddToSupplyValue(amount);
    volatile f64 rounded = f64(residential_layer->GetWorkforcePopPct()) * f64(amount) + kScDoubleIntegerBias;
    f64 stored = rounded;
    u64 bits;
    memcpy(&bits, &stored, sizeof(bits));
    workforce_valve->AddToSupplyValue(i32(u32(bits) + kScDoubleIntegerLowWordBias));
  }
  return true;
}

bool EcResidentialValve::AddToDemandValue(i32 amount) {
  EcValve::AddToDemandValue(amount);
  return true;
}

bool EcResidentialValve::EndOfMonth() {
  EcValve::EndOfMonth();
  return true;
}
} // namespace EcRci
