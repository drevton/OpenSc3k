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

CtFlammability::CtFlammability()
    : city(nullptr), ordinances(nullptr), plumbing(nullptr), occupants(nullptr), bound(false), references(0),
      cells_per_tick(0), sum(0), sample_count(0), average(0), scan_x(0), scan_z(0) {
}

CtFlammability::~CtFlammability() {
  if (bound)
    CtFlammability::Shutdown();
}

// Publish the actual five secondary identities and retain through the primary virtual override.
bool CtFlammability::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_EnHazardMap, CtFlammabilityLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryInfoString, ScQueryInfoString);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtFlammability::AddRef() {
  return ++references;
}

u32 CtFlammability::Release() {
  return ScReleaseImpl(this, &references);
}

// Withdraw both notifications before releasing occupants, ordinances, the city340 interface and the city in that order.
// Clear each retained member before Release, then retire the primary map and clear the bound byte last.
bool CtFlammability::Shutdown() {
  CtService::CtGetMessageServer()->RemoveNotification(static_cast<CtLayer *>(this), kOcMsg_SurfaceInserted);
  CtService::CtGetMessageServer()->RemoveNotification(static_cast<CtLayer *>(this), kOcMsg_SurfaceRemoved);
  ScRelease(&occupants);
  ScRelease(&ordinances);
  ScRelease(&plumbing);
  ScRelease(&city);
  CtByteMap::Shutdown();
  bound = false;
  return true;
}

bool CtFlammability::StaticShutdown() {
  return true;
}

bool CtFlammability::Init(CtCity *, ScImporter *) {
  return true;
}

u32 CtFlammability::GetLayerType() {
  return kCtFlammabilityLayerType;
}

bool CtFlammability::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

// Capture agent insertion success before subscribing the city-layer view to both actual message types.
// Notification replies do not change the captured agent result.
bool CtFlammability::SimulationBegin() {
  bool result = CtService::CtGetCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  CtService::CtGetMessageServer()->AddNotification(static_cast<CtLayer *>(this), kOcMsg_SurfaceInserted);
  CtService::CtGetMessageServer()->AddNotification(static_cast<CtLayer *>(this), kOcMsg_SurfaceRemoved);
  return result;
}

bool CtFlammability::SimulationEnd() {
  return CtService::CtGetCity()->CitySimulator()->RemoveAgent(static_cast<CtAgent *>(this));
}

i32 CtFlammability::MinimumExecutionInterval() {
  return 2;
}

bool CtFlammability::IsAgentLocking() {
  return false;
}

u32 CtFlammability::TypeId() const {
  return kCtFlammabilityLayerType;
}

// Return no agent-type object without acquiring or publishing another interface.
CtAgentType *CtFlammability::AgentType() {
  return nullptr;
}

bool CtFlammability::SetAgentType(CtAgentType *) {
  return false;
}

bool CtFlammability::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtFlammability::DebugClassTag(ScStringView *) {
  return false;
}

bool CtFlammability::DebugTypeTag(ScStringView *) {
  return false;
}

void CtFlammability::SetValue(u32 x, u32 z, const u8 *value) {
  CtByteMap::SetValue(x, z, value);
}

u8 CtFlammability::GetAverageValue() {
  return average;
}

u32 CtFlammability::GetInfoStringCount(u32) {
  return 0;
}

u8 CtFlammability::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

u32 CtFlammability::GetMapSpriteCount() {
  return 1;
}

bool CtFlammability::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10028;
  return true;
}

// Read three optional byte tuning values from the original INI and package, retaining earlier values when absent.
// Construct key before section for every lookup and narrow each parsed unsigned value directly to a byte.
bool CtFlammability::StaticInit(CtDefinition *) {
  ScString path, unused, value, other;
  ApCompressedIni ini;
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Tune.INI");
  ini.SetPath(path);
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  if (ini.ReadEntry("FlammabilityLayer", "LeafBurningBanOrdEffect", &value))
    leaf_effect = u8(ScConvertToUint32(value.begin));
  if (ini.ReadEntry("FlammabilityLayer", "SmokeDetectorsOrdEffect", &value))
    detector_effect = u8(ScConvertToUint32(value.begin));
  if (ini.ReadEntry("FlammabilityLayer", "NoWaterEffect", &value))
    water_effect = u8(ScConvertToUint32(value.begin));
  return true;
}

// Retain the city, occupant manager, ordinance layer and primary plumbing identity in the original order.
// Failed acquisition dispatches virtual shutdown; successful initialization sizes the map before resetting the scan.
bool CtFlammability::Init(CtCity *source) {
  if (bound)
    Shutdown();
  if (!source) {
    Shutdown();
    return false;
  }
  city = ScAddRefNotNull(source);
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return false;
  }
  occupants->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances) {
    Shutdown();
    return false;
  }
  ordinances->AddRef();
  plumbing = city->PlumbingLayer();
  if (!plumbing) {
    Shutdown();
    return false;
  }
  plumbing->AddRef();
  u8 zero = 0;
  bound = CtByteMap::Init(source, &zero);
  u32 rows = CellCountZ(), columns = CellCountX();
  cells_per_tick = rows * columns / 30;
  scan_x = scan_z = sum = sample_count = 0;
  average = 0;
  return bound;
}

// Load the full-resolution grid and scan state from the actual versioned record; only versions through1 are accepted.
// Mark the record failed on any refusal and let the original frame release, close or abort it.
bool CtFlammability::Init(CtCity *source, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source, segment, RsKey{kRsCityLayerRecordType, kCtFlammabilityRecordGroup, 0});
  bool result = frame.IsValid() != 0;
  RsStream *record = frame.GetDBSerialRecordPointer();
  if (result)
    result = record && frame.RecordVersion() <= 1 && SerialRead(record);
  if (result)
    result = record->ReadU32(&scan_x) && record->ReadU32(&scan_z) && record->ReadU32(&sum) &&
        record->ReadU32(&sample_count) && record->ReadU8(&average);
  if (!result)
    frame.SetFailed();
  return result;
}

// Write version1 grid data followed by the two cursors, running sum, sample count and average byte.
// The first failed operation marks the actual record for abort before the frame is destroyed.
bool CtFlammability::Save(CtCity *source, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source, segment, RsKey{kRsCityLayerRecordType, kCtFlammabilityRecordGroup, 0}, 1, false);
  bool result = frame.IsValid() != 0;
  RsStream *record = frame.GetDBSerialRecordPointer();
  if (result)
    result = record && SerialWrite(record) && record->WriteU32(scan_x) && record->WriteU32(scan_z) &&
        record->WriteU32(sum) && record->WriteU32(sample_count) && record->WriteU8(average);
  if (!result)
    frame.SetFailed();
  return result;
}

// Read occupant attributes before releasing the acquired occupant, then apply active ordinances and water coverage.
u8 CtFlammability::CalculateValue(u32 x, u32 z) {
  ScOccupant *occupant = nullptr;
  u8 value = 0;
  if (occupants->GetOccupantAtCell(x, z, &occupant)) {
    value = occupant->GetAttribData()->Flammability();
    occupant->Release();
    if (value) {
      i32 effect = 0;
      if (ordinances->QueryOrdinanceOn(kCtOrdinance_LeafBurningBan))
        effect = leaf_effect;
      if (ordinances->QueryOrdinanceOn(kCtOrdinance_MandatorySmokeDetectors))
        effect += detector_effect;
      if (!plumbing->IsWatered(x, z))
        effect -= water_effect;
      if (effect >= 100)
        return 0;
      i32 adjusted = i32(value) - effect * i32(value) / 100;
      return u16(adjusted) >= 255 ? 255 : u8(adjusted);
    }
  }
  return value;
}

// Advance X then Z, and update the raw average after the current cycle reaches the live virtual map height.
u8 CtFlammability::DoDailySim() {
  for (u32 i = 0; i < cells_per_tick; ++i) {
    u8 value = CalculateValue(scan_x, scan_z);
    SetValue(scan_x, scan_z, &value);
    if (value) {
      ++sample_count;
      sum += value;
    }
    ++scan_x;
    if (scan_x >= CellCountX()) {
      ++scan_z;
      scan_x = 0;
      if (scan_z >= CellCountZ()) {
        scan_z = 0;
        average = sample_count ? u8(sum / sample_count) : 0;
        sum = sample_count = 0;
      }
    }
  }
  return 1;
}

bool CtFlammability::Simulate(i32 elapsed) {
  return elapsed == 2 ? DoDailySim() != 0 : true;
}

// Refresh each cell in a changed occupant's inclusive world-coordinate bounds for either subscribed message type.
// Missing data or an unrelated message leaves the grid untouched and reports false.
bool CtFlammability::DoMessage(ScMessage *message) {
  if (message->type != kOcMsg_SurfaceRemoved && message->type != kOcMsg_SurfaceInserted)
    return false;
  ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1);
  if (!occupant)
    return false;
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  for (u32 x = u32(bounds.minimum.x >> 8); x <= u32(bounds.maximum.x >> 8); ++x)
    for (u32 z = u32(bounds.minimum.z >> 8); z <= u32(bounds.maximum.z >> 8); ++z) {
      u8 value = CalculateValue(x, z);
      SetValue(x, z, &value);
    }
  return true;
}

// Read the live cached byte and lazily calculate zero cells at or beyond the current scan cursor.
u8 CtFlammability::GetValue(u32 x, u32 z, u8 *value) {
  *value = cells->columns[x][z];
  if (!*value && (x > scan_x || (x == scan_x && z >= scan_z))) {
    *value = CalculateValue(x, z);
    SetValue(x, z, value);
  }
  return 1;
}

// Construct the sole supported raster with the bound city zone view and return the requested real interface.
// On a refused query the original frees raw storage without releasing its constructor holds; detach those holds first.
bool CtFlammability::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10028)
    return false;
  CtFlammabilityMapSprite *sprite = new CtFlammabilityMapSprite(this, city->ZoneLayer());
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->flammability = nullptr;
  sprite->zones = nullptr;
  delete sprite;
  return false;
}

CtFlammabilityMapSprite::CtFlammabilityMapSprite(CtFlammability *source, CtZoneLayer *zone) : zones(nullptr) {
  if (zone)
    zones = ScAddRefNotNull(zone);
  flammability = ScAddRefNotNull(source);
  u32 rows = CtService::CtGetCity()->CellCountZ(), columns = CtService::CtGetCity()->CellCountX();
  SetId(10028);
  SetBaseSize(columns, rows);
}

CtFlammabilityMapSprite::~CtFlammabilityMapSprite() {
  ScRelease(&flammability);
  ScRelease(&zones);
}

// Scale the byte by the palette entry count with low-DWORD multiplication and signed division by256.
bool CtFlammabilityMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  u8 cell = 0;
  flammability->GetValue(x, z, &cell);
  if (!cell)
    return false;
  GxPaletteView *source = GetPalette();
  if (!source)
    return false;
  i32 entries = source->GetNumberEntries();
  return source->GetColor(i32(u32(entries) * u32(cell)) / 256, out);
}

// Dispatch only the original query-create message, returning the actual helper or raster-add byte result.
// Acquired views and a published temporary raster are released in their original order.
bool CtFlammability::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type != kWnMsg_QueryOpened)
    return false;
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagDialog:
    if (!source || !source->QueryInterface(IID_UiQueryWindow, &selected))
      return false;
    {
      u8 result = DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagDebug:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagMaps:
    if (!source || !source->QueryInterface(IID_UiRasterQuery, &selected))
      return false;
    {
      void *raster = nullptr;
      u8 result = static_cast<SpMapSpriteFactory *>(this)->CreateMapSprite(10028, IID_GxRasterSource, &raster);
      if (result)
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster) {
        static_cast<ScUnknown *>(raster)->Release();
        raster = nullptr;
      }
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagStandard:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  default:
    return false;
  }
}

u8 CtFlammability::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 CtFlammability::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u32 CtFlammability::GetInfoStringCount() {
  return 1;
}

// Initialize the location and Strings before looking up coordinates, then add each available standard row.
u8 CtFlammability::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  bool warning = false;
  if (!query || !query->GetLocation(&location))
    return 0;
  for (u32 category = 0; category < GetInfoStringCount(); ++category)
    if (GetInfoStrings(u32(location.x >> 8), u32(location.z >> 8), &label, &value, category, &warning))
      query->AddText(&label, &value, 4096, u8(warning));
  return 1;
}

// Clear the optional warning first and publish the localized label and threshold grade for category0 only.
// Release each temporary resource before the caller-visible copy; only the highest grade sets the warning.
u8 CtFlammability::GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, bool *warning) {
  if (warning)
    *warning = false;
  if (category)
    return 0;
  ScString text =
      static_cast<ScString>(CtService::ScTextResource(kRsCityServiceFlammabilityText, kRsCityServiceTextGroup));
  label->Copy(text);
  u8 cell;
  GetValue(x, z, &cell);
  u32 grade = !cell ? 35 : cell < 72 ? 36 : cell < 129 ? 37 : cell < 187 ? 38 : cell < 230 ? 39 : 40;
  text = static_cast<ScString>(CtService::ScTextResource(grade, kRsCityServiceTextGroup));
  if (grade == 40 && warning)
    *warning = true;
  value->Copy(text);
  return 1;
}

// Register the seven real service classes in constructor order through the common ordered class table.
// The registered factories publish the real service and building interface views.
CtServiceDirector::CtServiceDirector() {
  AddClass(kCtCrimeClassId, CreateCrimeLayer, 0);
  AddClass(kCtFlammabilityClassId, CreateFlammabilityLayer, 0);
  AddClass(kCtPoliceClassId, CreatePoliceLayer, 0);
  AddClass(kCtFireClassId, CreateFireLayer, 0);
  AddClass(kBdFireStationClassId, CreateBuildingFireStation, 0);
  AddClass(kBdPoliceStationClassId, CreateBuildingPoliceStation, 0);
  AddClass(kBdJailClassId, CreateBuildingJail, 0);
}

CtServiceDirector::~CtServiceDirector() {
}

u32 CtServiceDirector::GetDirectorID() const {
  return kCtServiceDirectorId;
}

bool CtServiceDirector::AppRegister(ScRegistry *) {
  return true;
}

// Provide SIMSERV's inherited ScDllDirector::OnStart behavior through the module-specific native override.
bool CtServiceDirector::OnStart(ScCom *) {
  void *identity = nullptr;
  CtService::CtGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &identity);
  bool result = AppRegister(static_cast<ScRegistry *>(identity));
  if (identity)
    static_cast<ScRegistry *>(identity)->Release();
  return result;
}

// Construct the actual crime receiver and return its crime-layer view before any factory caller queries or retains it.
ScUnknown *CtServiceDirector::CreateCrimeLayer() {
  return static_cast<CtCrimeLayer *>(new CtCrime);
}

// Lazily construct and return the borrowed service director when its accessor is first called.
// The static lifetime performs the original exit destruction without tying that lifetime to the reference counter.
ScDirector *CtGetServiceDirector() {
  static CtServiceDirector director;
  return &director;
}

// Construct the real flammability receiver and return its distinct flammability view before factory retention.
ScUnknown *CtServiceDirector::CreateFlammabilityLayer() {
  return static_cast<CtFlammabilityLayer *>(new CtFlammability);
}

// Construct the actual fire-station building and publish its embedded building identity.
ScUnknown *CtServiceDirector::CreateBuildingFireStation() {
  return static_cast<BdBuilding *>(static_cast<CtService::OcBuilding *>(new BdFireStation));
}

// Construct the actual police-station building and publish its embedded building identity.
ScUnknown *CtServiceDirector::CreateBuildingPoliceStation() {
  return static_cast<BdBuilding *>(static_cast<CtService::OcBuilding *>(new BdPoliceStation));
}

// Construct the actual jail building and publish its embedded building identity.
ScUnknown *CtServiceDirector::CreateBuildingJail() {
  return static_cast<BdBuilding *>(static_cast<CtService::OcBuilding *>(new BdJail));
}

// Construct the original police layer and publish its distinct police interface to the director.
ScUnknown *CtServiceDirector::CreatePoliceLayer() {
  return static_cast<CtPoliceLayer *>(new CtPolice);
}

// Construct the original fire layer and publish its distinct fire interface to the director.
ScUnknown *CtServiceDirector::CreateFireLayer() {
  return static_cast<CtFireLayer *>(new CtFire);
}

#if (!defined(SC3_STATIC) || defined(SC3_SIMSERV_DLL)) && !defined(SC3_MONOLITHIC)
// Publish the module's borrowed lazy director through the original undecorated ordinal-one DLL export.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return CtGetServiceDirector();
}

#endif

namespace CtService {
static void *application_identity;
static u8 application_initialized;
static RsManager *CtServiceResourceManager;
static ScLanguageManager *language;
static ScMessageServer *messages;
static GxGraphics *graphics;
static WnResources *window_resources;

static void CtApplicationExit() {
}

// Borrow the framework through SIMSERV's actual module director rather than another DLL's service cache.
ScFramework *CtGetFramework() {
  return CtGetServiceDirector()->FrameWork();
}

// Set the one-time bit before querying into the live application slot, then register the original empty exit hook.
// The query result is ignored and a refused first acquisition does not retry.
ApGameInterface *CtGetApplication() {
  if (!(application_initialized & 1)) {
    application_initialized |= 1;
    CtGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &application_identity);
    atexit(CtApplicationExit);
  }
  return static_cast<ApGameInterface *>(application_identity);
}

// Borrow the current city from the optional cached application on each call; the city identity itself is not cached.
CtCity *CtGetCity() {
  ApGameInterface *application = CtGetApplication();
  return application ? application->GetCity() : nullptr;
}

u8 CtHasCity() {
  return CtGetCity() != nullptr;
}

// Borrow the application agent tree on each call, preserving the null application result and the caller's later retain.
CtAgentTree *CtGetAgentTree() {
  ApGameInterface *application = CtGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Borrow the application's event dispatcher, preserving the original null result when no application exists.
ApBoxX *CtGetBoxX() {
  ApGameInterface *application = CtGetApplication();
  return application ? application->GetBoxX() : nullptr;
}

// Borrow the application occupant-attribute cache, preserving a null result when the application is unavailable.
OcAttribCache *CtGetOccupantAttribCache() {
  ApGameInterface *application = CtGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Publish this module's borrowed resource-manager cache before releasing its temporary acquisition hold.
// Ignore the service predicate and retry later if the resulting cache remains null.
RsManager *CtGetResourceManager() {
  if (!CtServiceResourceManager) {
    void *object = nullptr;
    CtGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    CtServiceResourceManager = manager;
    if (manager)
      manager->Release();
  }
  return CtServiceResourceManager;
}

// Publish the borrowed language manager before releasing the temporary lookup hold; a null result retries later.
ScLanguageManager *CtGetLanguageManager() {
  if (!language) {
    void *object = nullptr;
    CtGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(object);
    language = manager;
    if (manager)
      manager->Release();
  }
  return language;
}

// Cache the borrowed graphics service after releasing its temporary lookup hold; a null result retries later.
GxGraphics *CtGetGraphics() {
  if (!graphics) {
    void *identity = nullptr;
    CtGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &identity);
    graphics = static_cast<GxGraphics *>(identity);
    if (graphics)
      graphics->Release();
  }
  return graphics;
}

// Cache the borrowed window-resource service after releasing the temporary lookup hold.
WnResources *CtGetWindowResources() {
  if (!window_resources) {
    void *identity = nullptr;
    CtGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &identity);
    window_resources = static_cast<WnResources *>(identity);
    if (window_resources)
      window_resources->Release();
  }
  return window_resources;
}

// Publish the borrowed module message service before releasing its initialized temporary lookup hold.
// Ignore the lookup predicate and retry on a subsequent call if the resulting cache remains null.
ScMessageServer *CtGetMessageServer() {
  if (!messages) {
    void *identity = nullptr;
    CtGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &identity);
    ScMessageServer *server = static_cast<ScMessageServer *>(identity);
    messages = server;
    if (server)
      server->Release();
  }
  return messages;
}
} // namespace CtService

namespace CtService {
ScTextResource::ScTextResource() : text_identity(nullptr), key{} {
}

ScTextResource::ScTextResource(const ScTextResource &source) : text_identity(nullptr), key(source.key) {
  LoadResource();
}

ScTextResource::ScTextResource(u32 instance, u32 group)
    : text_identity(nullptr), key{kRsTextResourceType, group, instance} {
  LoadResource();
}

ScTextResource::~ScTextResource() {
  UnloadResource();
}

void ScTextResource::LoadResource() {
  if (!CtGetResourceManager()->GetResource(*GetResKey(), IID_ScStringView, &text_identity))
    text_identity = nullptr;
}

// The original Windows class declaration returns void; the installed destructor inlines this operation.
void ScTextResource::UnloadResource() {
  if (text_identity) {
    static_cast<ScStringView *>(text_identity)->Release();
    text_identity = nullptr;
  }
}

// Release the old identity before reading the source key and independently resolving the replacement.
// Self-assignment performs no callbacks; the original borrowed reference result uses pointer spelling here.
ScTextResource *ScTextResource::operator=(const ScTextResource &source) {
  if (this != &source) {
    UnloadResource();
    source.GetResKey(&key);
    LoadResource();
  }
  return this;
}

// Compare through the retained String interface when present; an absent resource compares false.
// The original class operator returns bool and does not retain either operand.
bool ScTextResource::operator==(const ScStringView &other) {
  return text_identity && static_cast<ScStringView *>(text_identity)->IsEqual(&other);
}

// Borrow the retained const String interface without requesting another resource or reference.
// The original class declaration identifies the return type; SIMSERV reads the same identity directly.
const ScStringView *ScTextResource::Value() const {
  return static_cast<const ScStringView *>(text_identity);
}

// Borrow the resource key stored in this text object without copying or retaining another object.
// The Windows class declaration establishes a const-reference result; SIMSERV uses the same key address directly.
const RsKey *ScTextResource::GetResKey() const {
  return &key;
}

// Publish the fixed String resource type, then read and copy the stored group and instance into the caller's key.
// This preserves normalization even for an empty handle and keeps overlapping output reads in their original order.
void ScTextResource::GetResKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
}

bool ScTextResource::IsStringPresent() {
  return text_identity != nullptr;
}

// Return the original not-found sentinel when the pattern cannot fit or does not occur.
static u32 CtFindTextPattern(const ScString &value, const char *pattern, u32 length) {
  u32 size = u32(value.end - value.begin);
  if (length > size)
    return UINT32_MAX;
  for (u32 i = 0; i <= size - length; ++i)
    if (!memcmp(value.begin + i, pattern, length))
      return i;
  return UINT32_MAX;
}

// Replace the first pattern in the retained concrete String; the original requires a published String resource.
// Its Windows class declaration returns bool and uses mutable String arguments; the Linux class confirms the operation.
bool ScTextResource::ReplaceParameter(ScString *pattern, ScString *replacement) {
  ScString *stored = static_cast<ScString *>(static_cast<ScStringView *>(text_identity));
  const char *needle = pattern->ToChar();
  u32 found = CtFindTextPattern(*stored, needle, u32(strlen(needle)));
  if (found >= u32(stored->end - stored->begin))
    return false;
  u32 length = replacement->Strlen();
  const char *data = replacement->Data();
  u32 remove = pattern->Strlen();
  stored->Replace(found, remove, data, length);
  return true;
}

// Copy the retained String to the output before searching and replace only the first matching pattern there.
// A missing retained resource remains outside the original method's defined domain.
bool ScTextResource::ReplaceParameter(ScString *pattern, ScString *replacement, ScString *out) {
  ScStringView *stored = static_cast<ScStringView *>(text_identity);
  u32 size = stored->Strlen();
  const char *source = stored->Data();
  *out = ScString(source, size);
  const char *needle = pattern->ToChar();
  u32 found = CtFindTextPattern(*out, needle, u32(strlen(needle)));
  if (found >= u32(out->end - out->begin))
    return false;
  u32 length = replacement->Strlen();
  const char *data = replacement->Data();
  u32 remove = pattern->Strlen();
  out->Replace(found, remove, data, length);
  return true;
}

// Copy the retained string, or copy an empty temporary into the returned value when the resource is absent.
ScTextResource::operator ScString() {
  if (IsStringPresent())
    return ScString(*static_cast<ScStringView *>(text_identity));
  ScString empty;
  return ScString(empty);
}
} // namespace CtService

namespace CtService {

OcBuilding::OcBuilding() : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

OcBuilding::~OcBuilding() {
  ScRelease(&agent_type);
}

// Clear output before selecting building or city-agent identity. Delegate other requests to the adjusted occupant base.
bool OcBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return OcOccupant::QueryInterface(iid, out);
}

u32 OcBuilding::AddRef() {
  return OcOccupant::AddRef();
}

u32 OcBuilding::Release() {
  return OcOccupant::Release();
}

// Borrow the adjusted occupant interface without acquiring another reference.
ScOccupant *OcBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the city-agent subobject without a query or reference acquisition.
CtAgent *OcBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool OcBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 OcBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool OcBuilding::IsAgentLocking() {
  return true;
}

// Borrow the retained agent type without changing its reference count.
CtAgentType *OcBuilding::AgentType() {
  return agent_type;
}

bool OcBuilding::SetAgentType(CtAgentType *) {
  return false;
}

u32 OcBuilding::TypeId() const {
  return OcOccupant::TypeId();
}

u32 OcBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

bool OcBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (u32(mask) & occupancy_flags) != 0;
}

u32 OcBuilding::GetGeomFlag() {
  return 260;
}

u32 OcBuilding::GetMemUse() const {
  return 44;
}

// Borrow the original generic attribute pointer directly, bypassing the city-specific default-data selection.
GeAttrib *OcBuilding::GetBaseAttribData() {
  return OcOccupant::GetAttribData();
}

// Query the specialized building attribute view from the virtual generic getter. Preserve its raw power byte
// before releasing the acquired interface; absent attributes or a failed query produce zero.
u8 OcBuilding::PowerRequired() {
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

bool OcBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

bool OcBuilding::Init(RsKey *key) {
  return OcOccupant::Init(key);
}

// Bind the actual occupant attributes before clearing borrowed building caches. Capture and retain the default agent
// type only when none is stored; failed occupant initialization performs the original second virtual shutdown call.
bool OcBuilding::Init(GeAttrib *value) {
  if (!OcOccupant::Init(value)) {
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

bool OcBuilding::SetName(const ScStringView &) {
  return false;
}

bool OcBuilding::IsNameChangeable() {
  return false;
}

bool OcBuilding::HasBirthdate() const {
  return false;
}

bool OcBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool OcBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// Replace the complete occupancy word with the input's low byte. Changed values always invoke virtual sprite refresh.
void OcBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancy_flags != state) {
    occupancy_flags = state;
    OnOccupancyStateChanged();
  }
}

// Add the requested occupancy bits. Refresh only when the word changes and the input includes a low-byte bit.
void OcBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

// Remove the requested occupancy bits. High-byte-only changes intentionally leave the original sprite untouched.
void OcBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      OnOccupancyStateChanged();
  }
}

// Restore generic occupant data before requiring the descriptor's raw state word. Ignore optional name/date failures;
// a successful raw state load uses the original OR setter, and birthdate outputs are year first, then ordinal day.
bool OcBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcOccupant::Init(city, info) || !info->HasLayerData1())
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

// Save generic occupant fields before optional custom names and construction dates. Publish a name only when its
// actual generic default exists and differs, then store the live raw occupancy word after all callbacks.
bool OcBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString default_name;
      if (attributes && attributes->GetName(&default_name) && !name.IsEqual(&default_name))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    EcDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.DayOfYear(), year = date.Year();
      info->SetSimCreateDate(year, day);
    }
  }
  info->SetLayerData1(occupancy_flags);
  return true;
}

// Retire the agent type before clearing both borrowed caches. Delegate directly to occupant cleanup after those stores.
void OcBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  OcOccupant::Shutdown();
}

bool OcBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return OcOccupant::DoQueryInfo(message, sender);
}

// Borrow cached attributes when present. Otherwise select the city's building default for inactive occupancy states,
// falling back to generic attributes on absent defaults and publishing that borrowed result after callbacks.
GeAttrib *OcBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = CtGetCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = OcOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before invoking this building's qualified indexed getter.
SpDefinition *OcBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return OcBuilding::GetSpriteAttrib(layer);
}

// Reuse the borrowed sprite cache for every layer. Active states use the generic indexed definition; other states
// ask the city's building defaults and fall back to the generic default definition when no result is available.
SpDefinition *OcBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = OcOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = CtGetCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = OcOccupant::GetSpriteAttrib();
  return cached_sprite;
}

// The original169AD stack guard has a destructor interface and one actual critical-section pointer.
class OcGraphicsLockScope {
public:
  explicit OcGraphicsLockScope(ScLock *lock);
  virtual ~OcGraphicsLockScope();

private:
  ScLock *lock;
};

OcGraphicsLockScope::OcGraphicsLockScope(ScLock *lock) : lock(lock) {
  lock->Lock();
}

OcGraphicsLockScope::~OcGraphicsLockScope() {
  lock->Unlock();
}

// Invalidate the attribute cache, use the selected definition's instance methods and keep failure outputs live.
void OcBuilding::OnOccupancyStateChanged() {
  OcGraphicsLockScope scope(CtGetCity()->GetGraphicsCriticalSection());
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

bool OcBuilding::ReadState(CtCity *city, RsStream *record) {
  if (!OcOccupant::ReadState(city, record))
    return false;
  i32 state = 0;
  bool valid = record->ReadI32(&state);
  if (!state)
    state = 1;
  SetOccupancyStateFlag(u32(state));
  return valid;
}

bool OcBuilding::WriteState(CtCity *city, RsStream *record) {
  if (!OcOccupant::WriteState(city, record))
    return false;
  return record->WriteI32(i32(occupancy_flags));
}

bool OcBuilding::DoMessage(ScMessage *) {
  return false;
}

bool OcBuilding::GetName(ScStringView *out) {
  return OcOccupant::GetName(out);
}

static ScString OcServiceQueryText(u32 instance, u32 group) {
  return ScTextResource(instance, group);
}

u8 OcBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  u32 instance = 0;
  if (GetOccupancyStateFlag(kGeBuildingCoveredInGarbage))
    instance = 198;
  else if (GetOccupancyStateFlag(kGeBuildingAbandoned))
    instance = 197;
  else if (GetOccupancyStateFlag(kGeBuildingUnderConstruction))
    instance = 196;
  else if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16))
    instance = 199;
  if (instance)
    query->SetStatusText(ScTextResource(instance, kRsCityServiceTextGroup));
  ScString caption = OcServiceQueryText(kRsCityServiceMakeHistoricalText, kRsCityServiceTextGroup);
  WnResources *resources = CtGetWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return OcOccupant::QueryBasic(message, query);
}

u8 OcBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *utility = CtGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    EcBudgetLayer *budget = CtGetCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label = OcServiceQueryText(kRsCityServiceCleanUpCostText, kRsCityServiceTextGroup);
      ScString formatted = OcServiceQueryText(kRsCityServiceCleanUpCostFormatText, kRsCityServiceTextGroup);
      ScString number;
      utility->MakeMoneyString(i32(amount), &number);
      formatted.Sprintf(formatted.Data(), number.Data());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return OcOccupant::QueryTile(message, query);
}

u8 OcBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcOccupant::QueryDetail(message, query);
}

u8 OcBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return OcOccupant::QueryResponse(message, query);
}

u8 OcStaticAnimBuilding::AnimType() const {
  return 1;
}

// Acquire the sprite's animation view, then start it at the captured packed location and the live building width.
// Release the acquired view after the start callback; the original passes only eight altitude bits in animation units.
void OcStaticAnimBuilding::AnimStart() {
  void *identity = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &identity)) {
    u32 location = position, width = SizeInCellsX();
    static_cast<SpAnimation *>(identity)->Start(i32((location & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(((location >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(u32(u8(location >> kOcPositionAltitudeShift)) << kCtMicroCoordinateShift), width);
    static_cast<SpAnimation *>(identity)->Release();
  }
}

void OcStaticAnimBuilding::AnimStop() {
  void *identity = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &identity)) {
    static_cast<SpAnimation *>(identity)->Stop();
    static_cast<SpAnimation *>(identity)->Release();
  }
}

i32 OcStaticAnimBuilding::Tick(u32 elapsed) {
  SpSprite *current = GetSpriteInst();
  return current ? current->Tick(elapsed) : 1;
}

} // namespace CtService

namespace CtService {

OcOccupant::OcOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

bool OcOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool OcOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 OcOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void OcOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void OcOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

u8 OcOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool OcOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void OcOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool OcOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void OcOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

void OcOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool OcOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude to micro coordinates. Exclude orientation bits from altitude.
void OcOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void OcOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 OcOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Replace cell coordinates and altitude from micro coordinates in source store order. Preserve orientation.
void OcOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void OcOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void OcOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool OcOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void OcOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void OcOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void OcOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Borrow the generic attribute pointer without acquiring a reference or consulting the city.
GeAttrib *OcOccupant::GetAttribData() {
  return attributes;
}

u32 OcOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

bool OcOccupant::DoMessage(ScMessage *) {
  return false;
}

bool OcOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 OcOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 OcOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 OcOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 OcOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void OcOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 OcOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u32 OcOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 OcOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

void OcOccupant::AnimStart() {
}

void OcOccupant::AnimStop() {
}

i32 OcOccupant::Tick(u32) {
  return 0;
}

u32 OcOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

// Expose the four identities sharing this occupant receiver. Successful queries acquire a virtual reference.
bool OcOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScQueryTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool OcOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Select the actual primary sprite-definition or agent-type subsystem through virtual getters.
// Acquire a reference before publishing the selected identity.
bool OcOccupant::GetSubSystemData(u32 id, void **out) {
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

// Borrow the default sprite definition from generic attributes without acquiring it.
SpDefinition *OcOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *OcOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Borrow the stored sprite instance without changing ownership.
SpSprite *OcOccupant::GetSpriteInst() {
  return sprite;
}

// Compare against the virtual default layer before fetching the virtual instance. Other layers have no instance.
SpSprite *OcOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u8 OcOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 OcOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 OcOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 OcOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << 8;
}

u32 OcOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << 8;
}

u32 OcOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << 8;
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void OcOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u32 OcOccupant::GetMemUse() const {
  return 20;
}

bool OcOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

u32 OcOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Leave output untouched without attributes. Otherwise capture the minimum before callbacks and compute Y, Z then X
// extents, reloading the packed position after each virtual size getter before publishing maximum coordinates.
void OcOccupant::GetBounds(GeBounds *out) const {
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
void OcOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

u8 OcOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

u16 OcOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

void OcOccupant::SetAltAnimCells(u16 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude & 0xff00) << kOcAltitudeFromMicroShift);
}

// SIMSERV's separately linked recursive lock protects its occupant reference word.
static ScLock OcServiceOccupantLock;

// Retain the service director before locking and incrementing the occupant's packed reference word.
u32 OcOccupant::AddRef() {
  CtGetServiceDirector()->AddRef();
  OcServiceOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  OcServiceOccupantLock.Unlock();
  return count;
}

// Remove the service director hold while locked, then reload and decrement the live packed reference count.
// At zero, unlock before the actual virtual shutdown and deleting destructor.
u32 OcOccupant::Release() {
  OcServiceOccupantLock.ScLock::Lock();
  if (u16(flags)) {
    CtGetServiceDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      OcServiceOccupantLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  OcServiceOccupantLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Retire each linked sprite before clearing the published head. The original helper has no normal result.
void OcOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (current) {
    do {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    } while (current);
    *out = nullptr;
  }
}

OcOccupant::~OcOccupant() {
  if (attributes) {
    attributes->Release();
    attributes = nullptr;
    OcOccupant::DestroySprites(&sprite);
    flags &= kOcFlagsWithoutSubtypeOrMembership;
  }
}

// Keep an existing attribute binding unchanged. Otherwise acquire through the application cache, invoke the virtual
// attribute initializer and release its live temporary output; failed cache lookup clears the instance pointer.
bool OcOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!CtGetOccupantAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// Retire old attributes before the sprite chain, retain the new pointer and create its actual default-layer sprite.
// Failed creation invokes virtual shutdown; successful binding adopts the default subtype only when its byte is zero.
bool OcOccupant::Init(GeAttrib *value) {
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
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Bind a descriptor key only when attributes are absent, then apply partial position and rotation outputs even on
// failure. Successful records supply removal flags; failed records enable both permissions without status wrappers.
bool OcOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool result = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    result = Init(&key);
    u32 type = 0;
    if (result)
      result = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (result)
    result = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (result)
    result = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (result) {
    u32 simulation = info->GetCanSimRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutSimRemove) | (simulation << kOcSimRemoveShift);
    u32 user = info->GetCanUserRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutUserRemove) | (user << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return result;
}

// Write generic occupant properties to the actual descriptor in source order. The original invokes the X-size
// getter twice: first for the second size argument, then for the first; preserve these distinct virtual calls.
bool OcOccupant::Save(CtCity *, OcPersistInfo *info) {
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

// Release and clear the live attributes before virtual sprite cleanup. Apply the original flag mask after callbacks;
// the shared void contract exposes the cleanup effects without inventing a success result.
void OcOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Borrow the generic attribute agent type without acquiring a reference. Unbound occupants have no type.
CtAgentType *OcOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

// Clear the query-window output before testing attributes and resolve the configured class through this module's COM.
bool OcOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  void *identity = nullptr;
  bool result = CtGetServiceDirector()->GZCOM()->GetClassObject(class_id, IID_UiQueryWindow, &identity);
  *out = static_cast<UiQueryWindow *>(identity);
  return result;
}

// Replace the packed orientation only when its full input differs.
u8 OcOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  u32 layer = GetDefaultLayerFlag();
  CreateSprite(layer, &sprite);
  return 1;
}

// Route the four query notifications through their actual interfaces and private virtual handlers. Always return
// false after releasing the live QI output, including when a handler successfully populates the query.
bool OcOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  ScMsgCode type = message->type;
  if (type != kWnMsg_QueryOpened && type != kWnMsg_QueryClosed)
    return false;
  u32 kind = u32(message->data1);
  if (kind == kUiQueryTagDialog) {
    void *identity = nullptr;
    if (sender && sender->QueryInterface(IID_UiQueryWindow, &identity)) {
      UiQueryWindow *query = static_cast<UiQueryWindow *>(identity);
      if (type == kWnMsg_QueryOpened)
        QueryBasic(message, query);
      else
        QueryResponse(message, query);
      query->Release();
    }
  } else if (type == kWnMsg_QueryOpened && (kind == kUiQueryTagDebug || kind == kUiQueryTagStandard)) {
    void *identity = nullptr;
    if (sender && sender->QueryInterface(IID_UiQuerySection, &identity)) {
      UiQuery *query = static_cast<UiQuery *>(identity);
      if (kind == kUiQueryTagDebug)
        QueryDetail(message, query);
      else
        QueryTile(message, query);
      query->Release();
    }
  }
  return false;
}

bool OcOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (!definition || !definition->CreateInstance(out)) {
    attributes->Key();
    return false;
  }
  (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
  u32 geometry = GetGeomFlag();
  (*out)->SetGeomFlag(geometry);
  return true;
}

bool OcOccupant::ReadState(CtCity *, RsStream *record) {
  RsKey key;
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 unused_type = 0;
  record->ReadU32(&unused_type);
  u8 unused_bytes[5]{};
  for (u32 index = 0; index < 5; ++index)
    record->ReadU8(&unused_bytes[index]);
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
  coordinate.x = i32(x << 8);
  coordinate.z = i32(z << 8);
  coordinate.altitude = i32(altitude << 8);
  SetLocation(coordinate);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return true;
}

bool OcOccupant::WriteState(CtCity *, RsStream *record) {
  GeAttrib *attrib = OcOccupant::GetAttribData();
  void *identity = nullptr;
  if (!attrib || !attrib->QueryInterface(IID_RsResource, &identity))
    return false;
  RsResource *resource = static_cast<RsResource *>(identity);
  record->WriteU32(*static_cast<const RsResource *>(resource)->Type());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Group());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Instance());
  resource->Release();
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
  record->WriteU32(u32(coordinate.x >> 8));
  record->WriteU32(u32(coordinate.z >> 8));
  record->WriteU32(u32(coordinate.altitude >> 8));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return true;
}

u8 OcOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 OcOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 OcOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 OcOccupant::QueryTile(ScMessage *, UiQuery *query) {
  GeAttrib *attrib = GetAttribData();
  if (!attrib)
    return 0;
  OcOverride *override_attrib = attrib->GetOverRideAttrib();
  if (!override_attrib)
    return 0;
  ScString architect;
  override_attrib->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  ScString label = ScTextResource(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

} // namespace CtService
