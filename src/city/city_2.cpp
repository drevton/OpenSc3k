#include "city/city.h"
#include <stddef.h>
#include <string.h>
#include <cassert>
#include <cstdlib>
#include "advisors/ad_advice.h"
#include "advisors/ad_advisors.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_module.h"
#include "advisors/ad_petitions.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_init_text.h"
#include "city/ct_layer.h"
#include "city/initialization.h"
#include "city/module.h"
#include "city/sc_random.h"
#include "city/services.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/locale.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "disasters/api.h"
#include "economy/api.h"
#include "environment/api.h"
#include "geometry/api.h"
#include "geometry/selection.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/indexed_file.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sp_tick_manager.h"
#include "terrain/api.h"
#include "terrain/tr_dirt_bag.h"
#include "ui/commands.h"
#include "ui/game_menu.h"

// Run the original per-mode transitions before retaining every requested bit.
void CtCityImpl::SetCityModeFlag(u32 flags) {
  stateLock.Lock();
  if (flags & kCtModeDisaster)
    SetDisasterActive(true);
  if (flags & kCtModeScenario)
    SetScenarioActive(true);
  if (flags & kCtModeTutorial)
    SetTutorialActive(true);
  if (flags & kCtModeDemo)
    SetDemoActive(true);
  modeFlags |= flags;
  stateLock.Unlock();
}

// Run each actual mode transition before clearing all requested bits.
void CtCityImpl::ClearCityModeFlag(u32 flags) {
  stateLock.Lock();
  if (flags & kCtModeDisaster)
    SetDisasterActive(false);
  if (flags & kCtModeScenario)
    SetScenarioActive(false);
  if (flags & kCtModeTutorial)
    SetTutorialActive(false);
  if (flags & kCtModeDemo)
    SetDemoActive(false);
  modeFlags &= ~flags;
  stateLock.Unlock();
}

u32 CtCityImpl::GetSerialNumber() {
  return serialNumber;
}

u32 CtCityImpl::GetCityType() {
  return cityType;
}

u32 CtCityImpl::GetCityID() {
  return cityID;
}

i32 CtCityImpl::GetDifficulty() {
  return difficulty;
}

bool CtCityImpl::IsAutoBudgetEnabled() const {
  return autoBudget;
}

bool CtCityImpl::AreDisastersEnabled() const {
  return disastersEnabled;
}

bool CtCityImpl::CanPlayerPauseUnpause() const {
  return canPause;
}

bool CtCityImpl::CanPlayerChangeSpeed() const {
  return canChangeSpeed;
}

bool CtCityImpl::CanPlayerLoadSave() const {
  return canLoadSave;
}

bool CtCityImpl::IsWriteCitySaveFileDebug() const {
  return writeSaveDebug;
}

bool CtCityImpl::IsReadCitySaveFileDebug() const {
  return readSaveDebug;
}

void CtCityImpl::SetSerialNumber(u32 value) {
  serialNumber = value;
}

void CtCityImpl::SetDifficulty(i32 value) {
  difficulty = value;
}

void CtCityImpl::SetPlayerLoadSaveEnabled(bool enabled) {
  canLoadSave = enabled;
}

// Write language before country through the genuine outputs, preserving alias behavior.
bool CtCityImpl::GetOriginalLanguageAndCountry(i32 *language, i32 *country) {
  *language = originalLanguage;
  *country = originalCountry;
  return true;
}

// Write language before country through the genuine outputs, preserving alias behavior.
bool CtCityImpl::GetLastLanguageAndCountry(i32 *language, i32 *country) {
  *language = lastLanguage;
  *country = lastCountry;
  return true;
}

// Return the borrowed embedded city name string without retaining it.
ScStringView *CtCityImpl::GetCityName() {
  return &cityName;
}

// Return the borrowed embedded description string without retaining it.
ScStringView *CtCityImpl::GetCityDescription() {
  return &description;
}

// Return the borrowed embedded mayor name string without retaining it.
ScStringView *CtCityImpl::GetMayorName() {
  return &mayorName;
}

void CtCityImpl::SetCityName(const ScStringView &text) {
  {
    ScString copy(text);
    cityName = copy;
  }
  ScMessage message = {kCtMsg_CityNameChanged, 0, 0, 0};
  CtGetMessageServer()->MessagePost(message, false);
}

void CtCityImpl::SetCityDescription(const ScStringView &text) {
  ScString copy(text);
  description = copy;
}

void CtCityImpl::SetMayorName(const ScStringView &text) {
  ScString copy(text);
  mayorName = copy;
}

// Post the original change message only after updating a changed option.
void CtCityImpl::SetAutoBudgetEnabled(bool enabled) {
  if (enabled != autoBudget) {
    autoBudget = enabled;
    ScMessage message = {kCtMsg_AutoBudgetChanged, (u32)enabled, 0, 0};
    CtGetMessageServer()->MessagePost(message, false);
  }
}

// Post the original change message only after updating a changed option.
void CtCityImpl::SetDisastersEnabled(bool enabled) {
  if (enabled != disastersEnabled) {
    disastersEnabled = enabled;
    ScMessage message = {kDsMsg_SetDisastersEnabled, (u32)enabled, 0, 0};
    CtGetMessageServer()->MessagePost(message, false);
  }
}

u32 CtCityImpl::CellCountX() const {
  return cellsX;
}

u32 CtCityImpl::AnimCellCountX() const {
  return cellsX << 8;
}

u32 CtCityImpl::CellSizeInAnimUnitsX() const {
  return 256;
}

u32 CtCityImpl::CellCountZ() const {
  return cellsZ;
}

u32 CtCityImpl::AnimCellCountZ() const {
  return cellsZ << 8;
}

u32 CtCityImpl::CellSizeInAnimUnitsZ() const {
  return 256;
}

u32 CtCityImpl::CellCountY() const {
  return cellsY;
}

u32 CtCityImpl::AnimCellCountY() const {
  return cellsY << 8;
}

u32 CtCityImpl::CellSizeInAnimUnitsY() const {
  return 256;
}

i64 CtCityImpl::GetTileSize() {
  return (i64)CellSizeInWorldUnitsX();
}

f32 CtCityImpl::CellSizeInWorldUnitsX() const {
  return 16.0f;
}

f32 CtCityImpl::CellSizeInWorldUnitsZ() const {
  return 16.0f;
}

f32 CtCityImpl::CellSizeInWorldUnitsY() const {
  return 3.266f;
}

f32 CtCityImpl::SizeX() {
  f32 count = (f32)CellCountX();
  return CellSizeInWorldUnitsX() * count;
}

f32 CtCityImpl::SizeZ() {
  f32 count = (f32)CellCountZ();
  return CellSizeInWorldUnitsZ() * count;
}

f32 CtCityImpl::SizeY() {
  f32 count = (f32)CellCountY();
  return CellSizeInWorldUnitsY() * count;
}

f32 CtCityImpl::WorldToMeter(f32 value) const {
  return value * 4.0f;
}

f32 CtCityImpl::WorldToFeet(f32 value) const {
  return value * CtFeetPerWorld;
}

f32 CtCityImpl::MeterToWorld(f32 value) const {
  return value / 4.0f;
}

f32 CtCityImpl::FeetToWorld(f32 value) const {
  return value / CtFeetPerWorld;
}

// Return the borrowed original surface occupants interface without changing its reference count.
ScOccupantManager *CtCityImpl::SurfaceOccupantManager() {
  return surfaceOccupants;
}

// Return the borrowed original underground occupants one interface without changing its reference count.
ScOccupantManager *CtCityImpl::UndergroundOccupantManagerLevelOne() {
  return undergroundOccupantsOne;
}

// Return the borrowed original underground occupants two interface without changing its reference count.
ScOccupantManager *CtCityImpl::UndergroundOccupantManagerLevelTwo() {
  return undergroundOccupantsTwo;
}

// Return the borrowed original dirt interface without changing its reference count.
ScDirtBag *CtCityImpl::DirtBag() {
  return dirt;
}

// Return the borrowed original pollution interface without changing its reference count.
EcLayer *CtCityImpl::PollutionLayer() {
  return pollution;
}

// Return the borrowed original neighbors interface without changing its reference count.
CtNeighborsLayer *CtCityImpl::NeighborsLayer() {
  return neighbors;
}

// Return the borrowed original petitioner interface without changing its reference count.
AdPetitionerManager *CtCityImpl::GetPetitionerManager() {
  return petitioner;
}

// Return the borrowed original simulator interface without changing its reference count.
CtSimulatorInterface *CtCityImpl::CitySimulator() {
  return simulator;
}

// Return the borrowed original clock interface without changing its reference count.
CtClockInterface *CtCityImpl::GetSimulationClock() {
  return clock;
}

void CtCityImpl::GetDate(u32 *month, u32 *day, u32 *year) {
  stateLock.Lock();
  *year = date.Year();
  *month = date.Month();
  *day = date.DayOfMonth();
  stateLock.Unlock();
}

void CtCityImpl::GetDate(u32 *day, u32 *year) {
  stateLock.Lock();
  *year = date.Year();
  *day = date.DayOfYear();
  stateLock.Unlock();
}

// Construct the date inside the lock and publish valid serial state before calling the actual clock.
bool CtCityImpl::SetDate(u32 month, u32 day, u32 year) {
  stateLock.Lock();
  bool result = false;
  CtDate value(month, day, year);
  if (value.serial) {
    date.serial = value.serial;
    result = clock->SetCurrentClockDate(&value);
  }
  stateLock.Unlock();
  return result;
}

// Construct the ordinal date before locking, then publish valid state and return the clock result.
bool CtCityImpl::SetDate(u32 day, u32 year) {
  CtDate value(day, year);
  stateLock.Lock();
  bool result = false;
  if (value.serial) {
    date.serial = value.serial;
    result = clock->SetCurrentClockDate(&value);
  }
  stateLock.Unlock();
  return result;
}

u32 CtCityImpl::GetYearBorn() {
  stateLock.Lock();
  u32 result = yearBorn;
  stateLock.Unlock();
  return result;
}

// Store the original founding year under the state lock and retain its Boolean success.
bool CtCityImpl::SetYearBorn(u32 year) {
  stateLock.Lock();
  yearBorn = year;
  stateLock.Unlock();
  return true;
}

// Read only the user pause counter, preserving the distinction from hidden simulation holds.
bool CtCityImpl::IsPaused() {
  stateLock.Lock();
  bool result = pauseCount != 0;
  stateLock.Unlock();
  return result;
}

u32 CtCityImpl::PauseCount() {
  stateLock.Lock();
  u32 result = pauseCount;
  stateLock.Unlock();
  return result;
}

u32 CtCityImpl::SimHiddenPauseCount() {
  stateLock.Lock();
  u32 result = hiddenPauseCount;
  stateLock.Unlock();
  return result;
}

// Sum the actual user and hidden simulation counters under the state lock.
u32 CtCityImpl::TotalPauseCount() {
  stateLock.Lock();
  u32 result = pauseCount + hiddenPauseCount;
  stateLock.Unlock();
  return result;
}

// Adjust the separate clock hold after the genuine clock call, then re-read the counter after the clock query.
u32 CtCityImpl::HiddenClockPause() {
  if (!clock)
    return 0;
  clock->PauseClock();
  stateLock.Lock();
  ++clockPauseCount;
  stateLock.Unlock();
  clock->GetPauseCount();
  return clockPauseCount;
}

// Adjust the separate clock hold after the genuine clock call, then re-read the counter after the clock query.
u32 CtCityImpl::HiddenClockUnpause() {
  if (!clock)
    return 0;
  clock->UnpauseClock();
  stateLock.Lock();
  if (clockPauseCount)
    --clockPauseCount;
  stateLock.Unlock();
  clock->GetPauseCount();
  return clockPauseCount;
}

void CtCityImpl::Lock() {
  cityLock.Lock();
}

void CtCityImpl::Unlock() {
  cityLock.Unlock();
}

void CtCityImpl::LockGraphics() {
  graphicsLock.Lock();
}

void CtCityImpl::UnlockGraphics() {
  graphicsLock.Unlock();
}

// Return the borrowed address of the actual embedded critical section.
ScLock *CtCityImpl::GetCriticalSection() {
  return &cityLock;
}

// Return the borrowed embedded critical-section address with the SDK integer-pointer spelling.
ScLock *CtCityImpl::GetGraphicsCriticalSection() {
  return &graphicsLock;
}

bool CtCityImpl::CanCitySave() const {
  return true;
}

// Preserve the original lock/unlock effects before exposing the borrowed embedded path string.
ScStringView *CtCityImpl::GetFilePath() {
  stateLock.Lock();
  stateLock.Unlock();
  return &filePath;
}

// Copy and assign the original path while the state lock remains held through temporary destruction.
void CtCityImpl::SetFilePath(const ScStringView &path) {
  CtCityGuard guard(&stateLock);
  {
    ScString copy(path);
    filePath = copy;
  }
}

u32 CtCityImpl::GetVersionComponent(u8 component) const {
  return component < 4 ? version[component] : 0;
}

u32 CtCityImpl::GetVersionComponentFromSaveFile(u8 component) const {
  return component < 4 ? savedVersion[component] : 0;
}

// Preserve disaster speed and user-pause transitions while holding the original recursive state lock.
void CtCityImpl::SetDisasterActive(bool active) {
  stateLock.Lock();
  if (active != ((modeFlags & kCtModeDisaster) != 0)) {
    modeFlags = (modeFlags & ~kCtModeDisaster) | (u32)active;
    if (modeFlags & kCtModeDisaster) {
      previousSpeed = speed;
      SetGameSpeed(0);
      if (IsPaused())
        Unpause();
    } else
      SetGameSpeed(previousSpeed);
  }
  stateLock.Unlock();
}

void CtCityImpl::SetScenarioActive(bool active) {
  CtCityGuard guard(&stateLock);
  modeFlags = (modeFlags & ~kCtModeScenario) | (kCtModeScenario * (u32)active);
  ScMessageServer *server = CtGetMessageServer();
  if (server) {
    ScMessage message = {kWnMsg_GameMenuSetItemEnabled, kUiMenuCityOptionsItemId, (u32)!active, 0};
    server->MessagePost(message, false);
    message = {kWnMsg_GameMenuSetItemEnabled, kUiMenuScenarioStatusItemId, (u32)active, 0};
    server->MessagePost(message, false);
  }
  EnableHolidayFeatures((modeFlags & kCtModeScenario) == 0);
}

// Store only the original tutorial bit while retaining the state-lock boundary.
void CtCityImpl::SetTutorialActive(bool active) {
  stateLock.Lock();
  modeFlags = (modeFlags & ~kCtModeTutorial) | (kCtModeTutorial * (u32)active);
  stateLock.Unlock();
}

// Store only the original demonstration bit while retaining the state-lock boundary.
void CtCityImpl::SetDemoActive(bool active) {
  stateLock.Lock();
  modeFlags = (modeFlags & ~kCtModeDemo) | (kCtModeDemo * (u32)active);
  stateLock.Unlock();
}

// Publish the actual holiday-feature flag while holding the city state lock.
void CtCityImpl::EnableHolidayFeatures(bool enabled) {
  stateLock.Lock();
  holidayFeatures = enabled;
  stateLock.Unlock();
}

// Read the visible speed through the original disaster test while retaining the recursive state lock.
i32 CtCityImpl::GetGameSpeed() {
  stateLock.Lock();
  i32 result = IsDisasterActive() ? previousSpeed : speed;
  stateLock.Unlock();
  return result;
}

// Apply the actual disaster and normal speed rules before clock-rate selection and asynchronous speed notification.
bool CtCityImpl::SetGameSpeed(i32 value) {
  if (value == speed && value == previousSpeed)
    return true;
  if (IsDisasterActive()) {
    stateLock.Lock();
    speed = 0;
    if (value) {
      previousSpeed = value;
      if (value > 4)
        previousSpeed = 4;
    }
    stateLock.Unlock();
  } else if (value > 0 && value <= 4) {
    stateLock.Lock();
    speed = value;
    previousSpeed = value;
    stateLock.Unlock();
  } else
    return false;
  switch (speed) {
  case 0:
    clock->SetClockRate(kCtSpeedZeroClockRate);
    break;
  case 1:
    clock->SetClockRate(kCtSpeedOneClockRate);
    break;
  case 2:
    clock->SetClockRate(kCtSpeedTwoClockRate);
    break;
  case 3:
    clock->SetClockRate(kCtSpeedThreeClockRate);
    break;
  case 4:
    clock->SetClockRate(kCtSpeedFourClockRate);
    break;
  }
  ScMessage message = {kCtMsg_GameSpeedChanged, (u32)previousSpeed, 0, 0};
  CtGetMessageServer()->MessagePost(message, false);
  return true;
}

// Acquire a genuine user pause, announcing the first hold before optionally requesting and retaining its dialog ID.
u32 CtCityImpl::Pause() {
  if (!clock)
    return 0;
  clock->PauseClock();
  stateLock.Lock();
  ++pauseCount;
  stateLock.Unlock();
  clock->GetPauseCount();
  if (pauseCount == 1) {
    ScMessage message = {kCtMsg_Paused, 0, 0, 0};
    CtGetMessageServer()->MessageSend(message);
    if (initialized && dialogJobs.Count() == 0) {
      u32 identifier = CtRequestDialog(761);
      if (identifier)
        dialogJobs.Append(identifier);
    }
  }
  return pauseCount;
}

// Release a user pause and post each live pending dialog on the last hold, re-reading storage after callbacks.
u32 CtCityImpl::Unpause() {
  if (!clock)
    return 0;
  clock->UnpauseClock();
  stateLock.Lock();
  if (pauseCount)
    --pauseCount;
  stateLock.Unlock();
  clock->GetPauseCount();
  if (pauseCount == 0) {
    ScMessage message = {kCtMsg_Unpaused, 0, 0, 0};
    CtGetMessageServer()->MessageSend(message);
    for (u32 i = 0; i < dialogJobs.Count(); ++i) {
      message = {kApMsgDismissNotification, dialogJobs.begin[i], 0, 0};
      CtGetMessageServer()->MessagePost(message, false);
    }
    dialogJobs.end = dialogJobs.begin;
  }
  return pauseCount;
}

// Capture the borrowed terrain-generator interface while holding the original city lock.
TrGeneratorInterface *CtCityImpl::DirtGenerator() {
  cityLock.Lock();
  TrGeneratorInterface *result = generator;
  cityLock.Unlock();
  return result;
}

// Return the borrowed SIMGEOM flora interface acquired for city field+176.
GeFlora *CtCityImpl::FloraLayer() {
  return flora;
}

// Return the borrowed budget-layer interface from the actual city registry field.
EcBudgetLayer *CtCityImpl::BudgetLayer() {
  return budget;
}

void CtCityImpl::SetTipOfTheDayNeeded(bool needed) {
  tipOfTheDayNeeded = needed;
}

bool CtCityImpl::IsTipOfTheDayNeeded() {
  return tipOfTheDayNeeded;
}

// Return the city's borrowed zone-layer identity without acquiring an additional reference.
CtZoneLayer *CtCityImpl::ZoneLayer() {
  return zones;
}

// Return the borrowed animated-occupant manager through its distinct original interface.
OcAnimManager *CtCityImpl::AnimOccupantManager() {
  return animationOccupants;
}

// Return the borrowed ordinance primary interface stored by the original city layer acquisition.
CtOrdinanceLayer *CtCityImpl::OrdinanceLayer() {
  return ordinances;
}

// Return the borrowed traffic interface acquired under IID029CA805 without adding a reference.
NwTrafficLayer *CtCityImpl::TrafficLayer() {
  return traffic;
}

// Return the borrowed residential interface acquired under IID4106C508 without adding a reference.
CtResidentialLayer *CtCityImpl::ResidentialLayer() {
  return residential;
}

// Return the borrowed building-layer interface retained by acquisition under IID20631788 without adding another hold.
GeBuildingLayer *CtCityImpl::BuildingLayer() {
  return buildings;
}

// Return the borrowed primary scenario interface retained under IID83DE4D37 without adding a reference.
CtScenarioLayer *CtCityImpl::ScenarioLayer() {
  return scenario;
}

void CtCityImpl::OnIdle() {
  if (simulator)
    simulator->OnIdle();
}

// Short circuit before reading later components when an earlier component already determines the result.
bool CtCityImpl::IsFileVersionMoreRecentThan(i32 major, i32 minor, i32 patch) {
  i32 savedMajor = (i32)savedVersion[0];
  if (savedMajor > major)
    return true;
  if (savedMajor != major)
    return false;
  i32 savedMinor = (i32)savedVersion[1];
  if (savedMinor > minor)
    return true;
  return savedMinor == minor && (i32)savedVersion[2] > patch;
}

// Return the borrowed world interface without querying or retaining its provider.
CtWorldLayer *CtCityImpl::WorldLayer() {
  return world;
}

// Return the borrowed street simulation interface without querying or retaining its provider.
CtStartSimLayer *CtCityImpl::StrtSimLayer() {
  return streetSimulation;
}

// Return the borrowed crime interface without querying or retaining its provider.
CtCrimeLayer *CtCityImpl::CrimeLayer() {
  return crime;
}

// Return the borrowed police interface without querying or retaining its provider.
CtPoliceLayer *CtCityImpl::PoliceLayer() {
  return police;
}

// Return the borrowed fire interface without querying or retaining its provider.
CtFireLayer *CtCityImpl::FireLayer() {
  return fire;
}

// Return the borrowed commercial interface without querying or retaining its provider.
EcCommercialLayer *CtCityImpl::CommercialLayer() {
  return commercial;
}

// Return the borrowed industrial interface without querying or retaining its provider.
EcIndustrialLayer *CtCityImpl::IndustrialLayer() {
  return industrial;
}

// Return the borrowed land value interface without querying or retaining its provider.
CtLandValueLayer *CtCityImpl::LandValueLayer() {
  return landValue;
}

// Return the borrowed weather interface without querying or retaining its provider.
EnWeatherLayer *CtCityImpl::WeatherLayer() {
  return weather;
}

// Return the borrowed cISTTransitLayer pointer retained during layer acquisition, without querying or retaining it.
// Retail cISC3City declares this return type at slot320; the transit interface definition remains unresolved.
NwTransit *CtCityImpl::TransitLayer() {
  return transit;
}

// Borrow the actual power-layer view acquired under IIDA0AFDF5D without querying or retaining it.
// Retail cISC3City declarations establish the concrete interface return at original slot336.
CtPowerLayer *CtCityImpl::PowerLayer() {
  return power;
}

// Return the actual plumbing-layer view acquired under IIDC2BF0039 without changing its lifetime.
// Retail cISC3City declares cISUPlumbingLayer at original slot340.
CtPlumbingLayer *CtCityImpl::PlumbingLayer() {
  return plumbing;
}

// Return the borrowed demolition interface acquired under IID231C8E63 without adding a reference.
GeDemolitionView *CtCityImpl::DemolitionLayer() {
  return demolition;
}

// Return the borrowed primary disaster interface acquired under IID E1F6ABE2 without adjusting to its layer view.
DsLayer *CtCityImpl::DisasterLayer() {
  return disasters;
}

CtDefinition *CtCityDefinition = nullptr;
bool CtCityStaticReady = false;
u32 CtCityStaticLayerCount = 0;

// Publish and retain the definition before any prior static shutdown, then initialize the live acquired layer roster.
// Release both primary and registry interfaces afterward while retaining the module's static-ready state and count.
bool CtCityImpl::StaticInit(CtDefinition *definition) {
  cityLock.Lock();
  CtCityDefinition = definition;
  definition->AddRef();
  if (CtCityStaticReady)
    StaticShutdown();
  AcquireLayers();
  for (u32 i = 0; i < layers.Count(); ++i)
    if (layers.begin[i])
      layers.begin[i]->StaticInit(CtCityDefinition);
  CtCityStaticLayerCount = layers.Count();
  ReleaseLayers();
  CtCityStaticReady = true;
  cityLock.Unlock();
  return true;
}

// Reacquire and shut down the live layer roster, then release the stored definition while leaving the ready flag set.
bool CtCityImpl::StaticShutdown() {
  cityLock.Lock();
  if (CtCityStaticReady) {
    AcquireLayers();
    for (u32 i = 0; i < layers.Count(); ++i)
      if (layers.begin[i])
        layers.begin[i]->StaticShutdown();
    ReleaseLayers();
    CtCityDefinition->Release();
    CtCityDefinition = nullptr;
  }
  cityLock.Unlock();
  return true;
}

static ApGameInterface *CtApplication;
static bool CtApplicationInitialized;

// Acquire the game-specific application once through the real framework application and its original IID.
// This is the acquisition phase of SIMCITY1000EE2A, not a claimed complete reconstruction of that private declaration.
static ApGameInterface *CtAcquireGameApp() {
  void *out = nullptr;
  CtGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &out);
  return static_cast<ApGameInterface *>(out);
}

// Retain the first acquired game application for this module, including an initial null result.
ApGameInterface *CtGetGameApp() {
  if (!CtApplicationInitialized) {
    CtApplicationInitialized = true;
    CtApplication = CtAcquireGameApp();
  }
  return CtApplication;
}

// Return the borrowed BoxX interface from the game application when that original optional application is available.
ApBoxX *CtGetBoxX() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetBoxX() : nullptr;
}

// Acquire the borrowed city-window view through the original optional game application.
SpCityWindowView *CtGetCityWindow() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetISC3WinCityView() : nullptr;
}

i32 CtHasCityWindow() {
  return CtGetCityWindow() != nullptr;
}

// Return the application's borrowed image-resource factory through its original slot92.
// A missing game application yields null without acquiring or caching another service identity.
RsImageOptions *CtGetBufferResourceFactory() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetBufferResourceFactory() : nullptr;
}

// Query the current generic view each time, preserving the original missing-window result.
SpViewInterface *CtGetCurrentView() {
  SpCityWindowView *window = CtGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Return the current view's borrowed animation tick manager without caching this changing UI identity.
SpTickManager *CtGetTickManager() {
  SpViewInterface *view = CtGetCurrentView();
  return view ? view->GetAnimTickManager() : nullptr;
}

// Obtain the borrowed application agent-type tree when the original optional game application is available.
CtAgentTree *CtGetAgentTree() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Obtain the current borrowed city through the optional game application on every request.
// A missing application yields the original null result.
CtCity *CtGetCity() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetCity() : nullptr;
}

// Return the current borrowed preferences identity through the optional application.
// Each lookup reads the live application field and leaves its ownership unchanged.
ApPreferences *CtGetPreferences() {
  ApGameInterface *application = CtGetGameApp();
  return application ? application->GetAppPreferences() : nullptr;
}

void CtCityImpl::DoQuitGame(bool query) {
  CtApplication->Quit(query, query);
}

// Read a scenario's metadata and optional portrait while the indexed segment is registered with the resource manager.
static u8 CtInitReadScenarioDescriptor(CtCityInfo *info, RsManager *manager, RsDatabaseSegment *segment,
    const RsKey &key, u32 width, u32 height) {
  ScArchiveReader reader(segment, key);
  u8 result = reader.IsValid() != 0;
  if (!result)
    return false;
  i32 descriptionId = 0;
  reader.GetStream()->ReadI32(&descriptionId);
  result = manager->RegisterDBSegment(segment);
  if (!result) {
    segment->Release();
    return false;
  }
  RsKey descriptionKey(kRsTextResourceType, static_cast<u32>(descriptionId), 0);
  RsKey titleKey(kRsTextResourceType, kRsCitySummaryGroup, 2);
  manager->RemoveResource(descriptionKey);
  ScStringView *text = nullptr;
  if (manager->GetResource(descriptionKey, IID_ScStringView, reinterpret_cast<void **>(&text))) {
    info->field84 = ScString(*text);
    text->Release();
  }
  if (manager->GetResource(titleKey, IID_ScStringView, reinterpret_cast<void **>(&text))) {
    info->field104 = ScString(*text);
    text->Release();
  }
  RsKey imageKey(kRsImageResourceType, kRsCitySummaryGroup, 1);
  result = manager->GetResource(imageKey, IID_GxBuffer, reinterpret_cast<void **>(&info->sourceImage));
  if (result) {
    GxBuffer *image = info->sourceImage;
    result = CtGetInitDirector()->GZCOM()->GetClassObject(
        kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&info->preview));
    if (result) {
      info->preview->Init(width, height, GxColorType(GxColorType::Color::RGB16), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      ScRect source{0, 0, static_cast<i32>(image->Width()), static_cast<i32>(image->Height())};
      ScRect destination{0, 0, static_cast<i32>(width), static_cast<i32>(height)};
      result = info->preview->Lock(GxLockFlags::Blt);
      if (result) {
        if (image->Width() >= width || image->Height() >= height)
          image->BltSrc(info->preview, source, source, &destination);
        else if (image->Width() < width && image->Height() < height)
          image->BltStretchSrc(info->preview, source, destination, &destination);
        info->preview->Unlock(GxLockFlags::Blt);
      }
    }
    image->Release();
  }
  manager->UnregisterDBSegment(segment);
  segment->Release();
  return result;
}

// Read the version and, for a city, its population, funds, date and two names before the preview image.
// Early failed reads mark the original record as failed; inaccessible bytes are not consumed.
static u8 CtInitReadCityDescriptor(CtCityInfo *info, RsDatabaseSegment *segment) {
  // SIMINIT 1002A9B5 writes the complete type word; the decompiler's char argument shows only 0x28.
  ScArchiveReader reader(segment, RsKey(kCtCitySummaryRecordType, kCtCitySummaryRecordGroup, 0));
  u8 result = reader.IsValid() != 0;
  RsStream *stream = reader.GetStream();
  i32 major = 0, minor = 0, patch = 0, build = 0;
  if (result)
    result = stream->ReadI32(&major) && stream->ReadI32(&minor) && stream->ReadI32(&patch) && stream->ReadI32(&build);
  if (result && major >= 1 && patch >= 0) {
    if (info->kind == 0) {
      u32 day = 0, year = 0;
      result = stream->ReadU32(&info->population) && stream->ReadI64(&info->funds) && stream->ReadU32(&day) &&
          stream->ReadU32(&year);
      if (result) {
        info->date.Set(day, year);
        u32 language = CtSavedTextLanguage(segment);
        result = CtReadSavedText(stream, &info->mayorName, language) &&
            CtReadSavedText(stream, &info->cityName, language);
      }
    } else if (info->kind != 1 && info->kind != 2) {
      result = false;
    }
    if (result)
      result = CtGetInitImageFactory()->LoadStream(&info->preview, stream, 0);
  }
  if (!result)
    reader.failed = 1;
  return result;
}

// Open the original indexed city or scenario file and read its descriptor through its scoped record stream.
// The constructor owns and shuts down the indexed access object after releasing the queried segment identity.
CtCityInfo::CtCityInfo(const ScStringView &path, u32 type, u32 width, u32 height)
    : reading(true), ownsSegment(true), kind(type), funds(0), population(0), preview(nullptr) {
  RsManager *manager = CtGetInitResourceManager();
  RsKey key;
  u8 result = 1;
  switch (type) {
  case 0:
    key = RsKey(kCtCitySummaryRecordType, kCtCitySummaryRecordGroup, 0);
    break;
  case 1:
    key = RsKey(kCtStarterTownSummaryRecordType, kCtStarterTownSummaryRecordGroup, 0);
    break;
  case 2:
    key = RsKey(kCtTerrainSummaryRecordType, kCtTerrainSummaryRecordGroup, 0);
    break;
  case 3:
    key = RsKey(kRsCitySummaryResourceType, kRsCitySummaryGroup, 0);
    break;
  default: result = false; break;
  }
  if (type == 3) {
    RsKey imageKey(kRsImageResourceType, kRsCitySummaryGroup, 1), titleKey(kRsTextResourceType, kRsCitySummaryGroup, 2);
    manager->RemoveResource(imageKey);
    manager->RemoveResource(titleKey);
  }
  RsIndexedAccess *access = nullptr;
  if (result)
    result = CtGetInitDirector()->GZCOM()->GetClassObject(
        kRsIndexedFileClassId, IID_RsIndexedAccess, reinterpret_cast<void **>(&access));
  if (result && access) {
    if (type == 3)
      result = access->Init(key.type, &path, true);
    else
      access->Init(0, &path, true);
    RsDatabaseSegment *source = nullptr;
    if (access->QueryInterface(IID_RsDatabaseSegment, reinterpret_cast<void **>(&source))) {
      if (type == 3)
        result = CtInitReadScenarioDescriptor(this, manager, source, key, width, height);
      else {
        result = CtInitReadCityDescriptor(this, source);
        if (source)
          source->Release();
      }
    }
  }
  if (access) {
    if (ownsSegment)
      access->Shutdown();
    access->Release();
  }
  readValid = result;
}

uptr CtCityInfo::GetPreview(GxBuffer **out) {
  *out = preview;
  if (preview)
    return preview->AddRef();
  return reinterpret_cast<uptr>(out);
}

// Construct the legacy importer with zero references and no loaded data planes.
// The reconstructed arrays start initialized so a later defined partial load never consumes indeterminate bytes.
CtLegacyImporter::CtLegacyImporter() = default;

// Retire the legacy importer after its owning reference count reaches zero.
CtLegacyImporter::~CtLegacyImporter() = default;

// Publish the importer for either its original interface or Unknown identity, retaining it on success.
bool CtLegacyImporter::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScImporter);
  HANDLE_QUERY_INTERFACE(IID_ScImporter, ScImporter);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the importer reference count before retaining the SIMINIT director.
u32 CtLegacyImporter::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Decrement a positive count, release the director hold, and destroy the importer when no references remain.
u32 CtLegacyImporter::Release() {
  if (references)
    --references;
  CtGetInitDirector()->RemoveRef();
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

// Read one original big-endian word from the file and leave the output untouched when the read fails.
static bool CtReadLegacyWord(RsFileInterface *file, u32 *out) {
  u8 bytes[4];
  if (!file->Read(bytes, 4))
    return false;
  *out = (u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3];
  return true;
}

// Read the FORM length and verify the following SCDH type before accepting the legacy city stream.
static bool SC3_STDCALL CtReadLegacyHeader(RsFileInterface *file, u32 *length) {
  u32 value;
  if (!CtReadLegacyWord(file, &value) || value != kCtLegacyFormTag)
    return false;
  if (!CtReadLegacyWord(file, &value))
    return false;
  *length = value;
  if (!CtReadLegacyWord(file, &value))
    return false;
  if (value != kCtLegacyScdhFormType) {
    *length = 0;
    return false;
  }
  return true;
}

// Expand literal and repeated-byte runs until the packed input or destination is exhausted.
static bool SC3_STDCALL CtReadLegacyPacked(RsFileInterface *file, u32 packedSize, u8 *out, u32 capacity) {
  u8 *packed = new u8[packedSize];
  if (!file->Read(packed, packedSize)) {
    delete[] packed;
    return false;
  }
  u32 input = 0, output = 0;
  while (input < packedSize && output < capacity) {
    u8 count = packed[input++];
    if (!(count & 0x80)) {
      for (u32 i = 0; i < count && output < capacity; ++i) {
        if (input == packedSize) {
          delete[] packed;
          return false;
        }
        out[output++] = packed[input++];
      }
    } else {
      if (input == packedSize) {
        delete[] packed;
        return false;
      }
      u8 value = packed[input++];
      for (u32 i = 0; i <= static_cast<u32>(count & 0x7f) && output < capacity; ++i)
        out[output++] = value;
    }
  }
  bool complete = input == packedSize;
  delete[] packed;
  return complete;
}

// Read legacy city chunks in file order, retaining successful plane bits and decoded data on later failure.
// Unknown chunks are skipped, and FORM completion is measured using the installed wrapping byte counter.
bool CtLegacyImporter::ReadChunks(RsFileInterface *file) {
  loadedPlanes = 0;
  cityNameData[0] = 0;
  u32 formLength;
  if (!CtReadLegacyHeader(file, &formLength))
    return false;
  u32 processed = 4, tag, size;
  while (CtReadLegacyWord(file, &tag)) {
    if (!CtReadLegacyWord(file, &size))
      break;
    u8 *destination = nullptr;
    u32 capacity = 0, bit = 0;
    switch (tag) {
    case kCtLegacyMiscChunkTag:
      destination = reinterpret_cast<u8 *>(properties);
      capacity = 4800;
      bit = kCtLegacyMiscPlane;
      break;
    case kCtLegacyXterChunkTag:
      destination = largePlanes[2];
      capacity = 16384;
      bit = kCtLegacyXterPlane;
      break;
    case kCtLegacyXbldChunkTag:
      destination = largePlanes[1];
      capacity = 16384;
      bit = kCtLegacyXbldPlane;
      break;
    case kCtLegacyXzonChunkTag:
      destination = largePlanes[5];
      capacity = 16384;
      bit = kCtLegacyXzonPlane;
      break;
    case kCtLegacyXundChunkTag:
      destination = largePlanes[4];
      capacity = 16384;
      bit = kCtLegacyXundPlane;
      break;
    case kCtLegacyXtxtChunkTag:
      destination = largePlanes[3];
      capacity = 16384;
      bit = kCtLegacyXtxtPlane;
      break;
    case kCtLegacyXbitChunkTag:
      destination = largePlanes[0];
      capacity = 16384;
      bit = kCtLegacyXbitPlane;
      break;
    case kCtLegacyXtrfChunkTag:
      destination = mediumPlanes[2];
      capacity = 4096;
      bit = kCtLegacyXtrfPlane;
      break;
    case kCtLegacyXpltChunkTag:
      destination = mediumPlanes[1];
      capacity = 4096;
      bit = kCtLegacyXpltPlane;
      break;
    case kCtLegacyXvalChunkTag:
      destination = mediumPlanes[3];
      capacity = 4096;
      bit = kCtLegacyXvalPlane;
      break;
    case kCtLegacyXcrmChunkTag:
      destination = mediumPlanes[0];
      capacity = 4096;
      bit = kCtLegacyXcrmPlane;
      break;
    case kCtLegacyXplcChunkTag:
      destination = smallPlanes[1];
      capacity = 1024;
      bit = kCtLegacyXplcPlane;
      break;
    case kCtLegacyXfirChunkTag:
      destination = smallPlanes[0];
      capacity = 1024;
      bit = kCtLegacyXfirPlane;
      break;
    case kCtLegacyXpopChunkTag:
      destination = smallPlanes[2];
      capacity = 1024;
      bit = kCtLegacyXpopPlane;
      break;
    case kCtLegacyXrogChunkTag:
      destination = smallPlanes[3];
      capacity = 1024;
      bit = kCtLegacyXrogPlane;
      break;
    case kCtLegacyXmicChunkTag:
      destination = micData;
      capacity = 1200;
      break;
    case kCtLegacyXthgChunkTag:
      destination = thingData;
      capacity = 480;
      bit = kCtLegacyXthgPlane;
      break;
    case kCtLegacyXlabChunkTag:
      destination = labelData;
      capacity = 6400;
      bit = kCtLegacyXlabPlane;
      break;
    case kCtLegacyXgrpChunkTag:
      destination = groupData;
      capacity = 3328;
      break;
    case kCtLegacyAltmChunkTag:
      if (size > sizeof(height) || !file->Read(height, size))
        return false;
      for (u32 i = 0; i < 128 * 128; ++i)
        height[i] = static_cast<u16>((height[i] >> 8) | (height[i] << 8));
      loadedPlanes |= kCtLegacyAltmPlane;
      break;
    case kCtLegacyCnamChunkTag:
      if (size != sizeof(cityNameData) || !file->Read(cityNameData, size))
        return false;
      loadedPlanes |= kCtLegacyCnamPlane;
      break;
    default:
      if (file->Seek(static_cast<i32>(size), kRsSeekCurrent) == ~u32(0))
        return false;
      break;
    }
    if (destination) {
      if (!CtReadLegacyPacked(file, size, destination, capacity))
        return false;
      if (tag == kCtLegacyMiscChunkTag) {
        for (u32 i = 0; i < 1200; ++i) {
          u32 value = properties[i];
          properties[i] = (value >> 24) | ((value >> 8) & kCtLegacySwappedLowMiddleByteMask) |
              ((value << 8) & kCtLegacySwappedHighMiddleByteMask) | (value << 24);
        }
      }
      loadedPlanes |= bit;
    }
    processed += size + 8;
    if (processed >= formLength)
      return true;
  }
  return false;
}

// Open a legacy city, read its chunks and derive a missing city name from the filename after closing the file.
// The fallback is populated even when chunk loading fails after the file was successfully opened.
u8 CtLegacyImporter::Init(const ScStringView &path) {
  RsFile file;
  ScString name(path);
  file.SetName(name);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  bool result = ReadChunks(&file);
  file.Close();
  if (!cityNameData[0] || !MapIsPresent(kCtLegacyCnamPlane)) {
    const char *source = file.GetNameData(), *base = source, *extension = nullptr;
    if (source[0] && source[1] == ':')
      base = source + 2;
    for (const char *cursor = base; *cursor; ++cursor)
      if (*cursor == '\\' || *cursor == '/')
        base = cursor + 1;
      else if (*cursor == '.')
        extension = cursor;
    u32 length = 0;
    while (length < 31 && base[length] && (!extension || extension < base || base + length < extension)) {
      cityNameData[length + 1] = static_cast<u8>(base[length]);
      ++length;
    }
    loadedPlanes |= kCtLegacyCnamPlane;
    cityNameData[0] = static_cast<u8>(length);
  }
  return result;
}

u8 CtLegacyImporter::MapIsPresent(u32 plane) {
  return (loadedPlanes & plane) == plane;
}

// Return the legacy city's X, Z and Y dimensions through the three original output parameters.
u8 CtLegacyImporter::GetBaseMapSize(u32 *countX, u32 *countZ, u32 *countY) {
  *countX = 128;
  *countZ = 128;
  *countY = 32;
  return true;
}

// Read an available byte plane at its own resolution. Unsupported special planes return false;
// other unrecognized masks preserve the original successful return without writing the output.
u8 CtLegacyImporter::GetValue(i32 plane, u32 x, u32 z, u8 *cell) {
  if (!MapIsPresent(plane))
    return false;
  u32 index, side;
  const u8 *data;
  switch (plane) {
  case kCtLegacyXbitPlane:
    data = largePlanes[0];
    side = 128;
    break;
  case kCtLegacyXbldPlane:
    data = largePlanes[1];
    side = 128;
    break;
  case kCtLegacyXterPlane:
    data = largePlanes[2];
    side = 128;
    break;
  case kCtLegacyXtxtPlane:
    data = largePlanes[3];
    side = 128;
    break;
  case kCtLegacyXundPlane:
    data = largePlanes[4];
    side = 128;
    break;
  case kCtLegacyXzonPlane:
    data = largePlanes[5];
    side = 128;
    break;
  case kCtLegacyXcrmPlane:
    data = mediumPlanes[0];
    side = 64;
    break;
  case kCtLegacyXpltPlane:
    data = mediumPlanes[1];
    side = 64;
    break;
  case kCtLegacyXtrfPlane:
    data = mediumPlanes[2];
    side = 64;
    break;
  case kCtLegacyXvalPlane:
    data = mediumPlanes[3];
    side = 64;
    break;
  case kCtLegacyXfirPlane:
    data = smallPlanes[0];
    side = 32;
    break;
  case kCtLegacyXplcPlane:
    data = smallPlanes[1];
    side = 32;
    break;
  case kCtLegacyXpopPlane:
    data = smallPlanes[2];
    side = 32;
    break;
  case kCtLegacyXrogPlane:
    data = smallPlanes[3];
    side = 32;
    break;
  case kCtLegacyAltmPlane:
  case kCtLegacyCnamPlane:
  case kCtLegacyXlabPlane:
  case kCtLegacyXmicPlane:
  case kCtLegacyXthgPlane:
  case kCtLegacyXgrpPlane:
  case kCtLegacyMiscPlane:
    return false;
  default:
    return true;
  }
  if (x >= side || z >= side)
    return false;
  index = side * z + x;
  *cell = data[index];
  return true;
}

// Return the low four bits of the available zone plane for a valid city cell.
u8 CtLegacyImporter::GetZoneValue(u32 x, u32 z, u8 *zone) {
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyXzonPlane))
    return 0;
  *zone = largePlanes[5][128 * z + x] & 15;
  return 1;
}

// Translate the zone's high nibble using the saved orientation after querying its property before bounds checks.
u8 CtLegacyImporter::GetFaceValue(u32 x, u32 z, u8 *direction) {
  i32 orientation = 0;
  GetMiscValue(2, &orientation);
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyXzonPlane))
    return false;
  static constexpr u8 translated[4][4] = {
      {0x80, 0x10, 0x20, 0x40}, {0x40, 0x80, 0x10, 0x20}, {0x20, 0x40, 0x80, 0x10}, {0x10, 0x20, 0x40, 0x80}};
  u8 nibble = largePlanes[5][128 * z + x] & 0xf0;
  u32 column = nibble == 0x10 ? 0 : nibble == 0x20 ? 1 : nibble == 0x40 ? 2 : nibble == 0x80 ? 3 : 4;
  *direction = u32(orientation) < 4 && column < 4 ? translated[orientation][column] : 0;
  return true;
}

// Return the installed element count for a legacy plane identifier, or zero for unrecognized identifiers.
u16 CtLegacyImporter::GetMapSize(i32 plane) {
  switch (plane) {
  case kCtLegacyXterPlane:
  case kCtLegacyXbldPlane:
  case kCtLegacyXzonPlane:
  case kCtLegacyXundPlane:
  case kCtLegacyXtxtPlane:
  case kCtLegacyXbitPlane:
  case kCtLegacyAltmPlane:
    return 128;
  case kCtLegacyXtrfPlane:
  case kCtLegacyXpltPlane:
  case kCtLegacyXvalPlane:
  case kCtLegacyXcrmPlane:
    return 64;
  case kCtLegacyXplcPlane:
  case kCtLegacyXfirPlane:
  case kCtLegacyXpopPlane:
  case kCtLegacyXrogPlane:
  case kCtLegacyCnamPlane:
    return 32;
  case kCtLegacyXlabPlane:
    return 256;
  case kCtLegacyXmicPlane:
    return 150;
  case kCtLegacyXthgPlane:
    return 40;
  case kCtLegacyMiscPlane:
    return 1200;
  default:
    return 0;
  }
}

// Copy one stored legacy property when its index is in range and its source chunk was loaded.
u8 CtLegacyImporter::GetMiscValue(u32 index, i32 *value) {
  if (index >= 1200 || !MapIsPresent(kCtLegacyMiscPlane))
    return false;
  *value = i32(properties[index]);
  return true;
}

// Return the low five bits of the packed height word for an available city cell.
u8 CtLegacyImporter::GetTerrainAlt(u32 x, u32 z, u8 *altitude) {
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyAltmPlane))
    return false;
  *altitude = static_cast<u8>(height[128 * z + x] & 31);
  return true;
}

// Return the complete packed height word for an available city cell.
u8 CtLegacyImporter::GetAltValue(u32 x, u32 z, u16 *out) {
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyAltmPlane))
    return false;
  *out = height[128 * z + x];
  return true;
}

// Return bits five through nine of the packed height word for an available city cell.
u8 CtLegacyImporter::GetWaterAlt(u32 x, u32 z, u8 *out) {
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyAltmPlane))
    return false;
  *out = static_cast<u8>((height[128 * z + x] >> 5) & 31);
  return true;
}

// Return bits ten through fourteen of the packed height word for an available city cell.
u8 CtLegacyImporter::GetTunnelDepth(u32 x, u32 z, u8 *out) {
  if (x >= 128 || z >= 128 || !MapIsPresent(kCtLegacyAltmPlane))
    return false;
  *out = static_cast<u8>((height[128 * z + x] >> 10) & 31);
  return true;
}

// Copy the stored bounded city name and terminate it; an unavailable name writes an empty string.
u8 CtLegacyImporter::GetCityName(char *out) {
  if (!MapIsPresent(kCtLegacyCnamPlane)) {
    *out = 0;
    return false;
  }
  u32 length = cityNameData[0];
  if (!length || length > 31)
    length = 31;
  u32 i = 0;
  while (i < length && cityNameData[i + 1]) {
    out[i] = static_cast<char>(cityNameData[i + 1]);
    ++i;
  }
  out[i] = 0;
  return true;
}

// The installed method does not write the supplied output buffer.
u8 CtLegacyImporter::GetLabel(u32 index, char *) {
  if (!MapIsPresent(kCtLegacyXlabPlane))
    return 0;
  return index < 256;
}

// Copy a complete eight-byte micro record when the corresponding plane and index are available.
u8 CtLegacyImporter::GetMicroRecord(u32 index, CtMicroRecord *out) {
  if (!MapIsPresent(kCtLegacyXmicPlane) || index >= 150)
    return 0;
  memcpy(out, micData + index * sizeof(CtMicroRecord), sizeof(CtMicroRecord));
  return 1;
}

// Copy a complete twelve-byte thing record when the corresponding plane and index are available.
u8 CtLegacyImporter::GetThingRecord(u32 index, CtThingRecord *out) {
  if (!MapIsPresent(kCtLegacyXthgPlane) || index >= 40)
    return 0;
  memcpy(out, thingData + index * sizeof(CtThingRecord), sizeof(CtThingRecord));
  return 1;
}

// Read a graph sample using the installed 32-bit object offset, including samples that reach earlier loaded fields.
// Samples must stay within the receiver; its first word is the actual native virtual-table pointer.
u8 CtLegacyImporter::GetGraphValue(i32 category, u8 scale, u8 index, u32 *out) {
  if (!MapIsPresent(kCtLegacyXgrpPlane) || category >= 16)
    return 0;
  u32 offset;
  if (scale == 1 && index < 12)
    offset = index;
  else if (scale == 10 && index < 20)
    offset = 12 + index;
  else if (scale == 100 && index < 20)
    offset = 32 + index;
  else
    return 0;
  u32 location = kCtLegacyGroupDataOffset + static_cast<u32>(category) * 208u + offset * 4u;
  const u8 *source = nullptr;
  if (location >= kCtLegacyGroupDataOffset && location <= kCtLegacyGroupDataOffset + sizeof(groupData) - sizeof(u32))
    source = groupData + (location - kCtLegacyGroupDataOffset);
  else if (location >= kCtLegacyLabelDataOffset &&
      location <= kCtLegacyLabelDataOffset + sizeof(labelData) - sizeof(u32))
    source = labelData + (location - kCtLegacyLabelDataOffset);
  else if (location >= kCtLegacyThingDataOffset &&
      location <= kCtLegacyThingDataOffset + sizeof(thingData) - sizeof(u32))
    source = thingData + (location - kCtLegacyThingDataOffset);
  else if (location >= kCtLegacyMicroDataOffset && location <= kCtLegacyMicroDataOffset + sizeof(micData) - sizeof(u32))
    source = micData + (location - kCtLegacyMicroDataOffset);
  else if (location >= kCtLegacyPropertiesOffset &&
      location <= kCtLegacyPropertiesOffset + sizeof(properties) - sizeof(u32))
    source = reinterpret_cast<const u8 *>(properties) + (location - kCtLegacyPropertiesOffset);
  else if (location >= kCtLegacyCityNameOffset &&
      location <= kCtLegacyCityNameOffset + sizeof(cityNameData) - sizeof(u32))
    source = cityNameData + (location - kCtLegacyCityNameOffset);
  else if (location >= kCtLegacySmallPlanesOffset &&
      location <= kCtLegacySmallPlanesOffset + sizeof(smallPlanes) - sizeof(u32))
    source = reinterpret_cast<const u8 *>(smallPlanes) + (location - kCtLegacySmallPlanesOffset);
  else if (location >= kCtLegacyMediumPlanesOffset &&
      location <= kCtLegacyMediumPlanesOffset + sizeof(mediumPlanes) - sizeof(u32))
    source = reinterpret_cast<const u8 *>(mediumPlanes) + (location - kCtLegacyMediumPlanesOffset);
  else if (location >= 32780u && location <= 32780u + sizeof(largePlanes) - sizeof(u32))
    source = reinterpret_cast<const u8 *>(largePlanes) + (location - 32780u);
  else if (location >= 12u && location <= 12u + sizeof(height) - sizeof(u32))
    source = reinterpret_cast<const u8 *>(height) + (location - 12u);
  else if (location == 8u)
    source = reinterpret_cast<const u8 *>(&loadedPlanes);
  else if (location == 4u)
    source = reinterpret_cast<const u8 *>(&references);
  else if (location == 0u)
    source = reinterpret_cast<const u8 *>(this);
  assert(source);
  memcpy(out, source, sizeof(u32));
  return 1;
}

// Load the default city and mayor names, then initialize the original funds, dimensions, year and city type.
CtNewCityInfoImpl::CtNewCityInfoImpl() : references(0) {
  cityName = CtInitText(kRsGameNewCityText, kRsGameTextGroup);
  mayorName = CtInitText(kRsGameDefactoText, kRsGameTextGroup);
  initialFunds = 0;
  loanByte = 0;
  countX = 128;
  countY = 128;
  countZ = 128;
  difficulty = 0;
  generator = nullptr;
  startYear = 1900;
  cityType = 7;
  autoBudgetByte = disastersByte = 0;
}

// Release the retained terrain generator before the two owned names are destroyed.
CtNewCityInfoImpl::~CtNewCityInfoImpl() {
  if (generator)
    generator->Release();
  generator = nullptr;
}

// Publish and retain the new-city interface for either supported identity when an output pointer is supplied.
bool CtNewCityInfoImpl::QueryInterface(u32 iid, void **out) {
  if ((iid != IID_ScUnknown && iid != IID_CtNewCityInfo) || !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<CtNewCityInfo *>(this);
  AddRef();
  return true;
}

u32 CtNewCityInfoImpl::AddRef() {
  return ++references;
}

u32 CtNewCityInfoImpl::Release() {
  return ScReleaseImpl(this, &references);
}

void CtNewCityInfoImpl::SetCityName(ScStringView *name) {
  cityName.Copy(*name);
}

void CtNewCityInfoImpl::SetMayorName(ScStringView *name) {
  mayorName.Copy(*name);
}

void CtNewCityInfoImpl::SetFunds(i64 funds) {
  initialFunds = funds;
}

void CtNewCityInfoImpl::SetFundsAreDebt(u8 debt) {
  loanByte = debt;
}

void CtNewCityInfoImpl::SetCitySize(u32 size) {
  countX = countY = countZ = size;
}

void CtNewCityInfoImpl::SetCityXSize(u32 size) {
  countX = size;
}

void CtNewCityInfoImpl::SetCityYSize(u32 size) {
  countY = size;
}

void CtNewCityInfoImpl::SetCityZSize(u32 size) {
  countZ = size;
}

void CtNewCityInfoImpl::SetStartYear(u32 year) {
  startYear = year;
}

void CtNewCityInfoImpl::SetDifficulty(i32 value) {
  difficulty = value;
}

// Release the previously retained terrain generator before retaining the supplied generator.
void CtNewCityInfoImpl::SetDirtGenerator(TrGeneratorInterface *value) {
  if (generator)
    generator->Release();
  generator = ScAddRefNotNull(value);
}

void CtNewCityInfoImpl::SetCityType(i32 value) {
  cityType = static_cast<u32>(value);
}

void CtNewCityInfoImpl::SetAutoBudgetEnabled(u8 enabled) {
  autoBudgetByte = enabled;
}

void CtNewCityInfoImpl::SetDisastersEnabled(u8 enabled) {
  disastersByte = enabled;
}

// Borrow the embedded city-name String without retaining it.
ScStringView *CtNewCityInfoImpl::GetCityName() {
  return &cityName;
}

// Borrow the embedded mayor-name String without retaining it.
ScStringView *CtNewCityInfoImpl::GetMayorName() {
  return &mayorName;
}

i64 CtNewCityInfoImpl::GetInitialFunds() {
  return initialFunds;
}

u8 CtNewCityInfoImpl::GetLoanByte() {
  return loanByte;
}

u32 CtNewCityInfoImpl::GetCellCountX() {
  return countX;
}

u32 CtNewCityInfoImpl::GetCellCountY() {
  return countY;
}

u32 CtNewCityInfoImpl::GetCellCountZ() {
  return countZ;
}

u32 CtNewCityInfoImpl::GetStartYear() {
  return startYear;
}

i32 CtNewCityInfoImpl::GetDifficulty() {
  return difficulty;
}

// Borrow the retained terrain generator without adding a reference.
TrGeneratorInterface *CtNewCityInfoImpl::GetTerrainGenerator() {
  return generator;
}

u32 CtNewCityInfoImpl::GetCityType() {
  return cityType;
}

u8 CtNewCityInfoImpl::GetAutoBudgetByte() {
  return autoBudgetByte;
}

u8 CtNewCityInfoImpl::GetDisastersByte() {
  return disastersByte;
}
