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

// Legacy miscellaneous words swap their two middle bytes separately from their outer bytes.
enum CtLegacyByteSwapMask : u32 {
  kCtLegacySwappedLowMiddleByteMask = 0xff00,
  kCtLegacySwappedHighMiddleByteMask = 0xff0000,
};

// Each of the five game-speed indices selects its original clock rate.
enum CtSpeedClockRate : u32 {
  kCtSpeedZeroClockRate = 120,
  kCtSpeedOneClockRate = 0x2760,
  kCtSpeedTwoClockRate = 0xa8c0,
  kCtSpeedThreeClockRate = 0x1fa40,
  kCtSpeedFourClockRate = 0x80520,
};

// City state and template summaries use distinct record keys; saved layers retain their initial capacity and growth size.
enum CtCityStateArchiveKey : u32 {
  kCtStateHeaderRecordGroup = 0x617e198d,
  kCtStateHeaderRecordType = 0xfa1,
  kCtStarterTownSummaryRecordType = 0xa329b74f,
  kCtStarterTownSummaryRecordGroup = 0x8329b722,
  kCtTerrainSummaryRecordType = 0xc329b755,
  kCtTerrainSummaryRecordGroup = 0xa329b758,
  kCtSaveLayerInitialCapacity = 0x200000,
  kCtSaveLayerGrowth = 0x80000,
};

// Dialog identifiers and retained layer pointers preserve their independent storage count limits.
enum CtCityStorageLimit : u32 {
  kCtDialogJobMaximumCount = 0x3fffffff,
  kCtLayerStorageMaximumCount = 0x3fffffff,
};

// Legacy import access translates the original data offsets to the corresponding native buffers without changing
// range checks.
enum CtLegacyDataOffset : u32 {
  kCtLegacyGroupDataOffset = 0x2c27c,
  kCtLegacyLabelDataOffset = 0x2a97c,
  kCtLegacyThingDataOffset = 0x2a79c,
  kCtLegacyMicroDataOffset = 0x2a2ec,
  kCtLegacyPropertiesOffset = 0x2902c,
  kCtLegacyCityNameOffset = 0x2900c,
  kCtLegacySmallPlanesOffset = 0x2400c,
  kCtLegacyMediumPlanesOffset = 0x2000c,
};

enum CtCityArchiveKey : u32 {
  kCtCitySummaryRecordType = 0xe2f42628,
  kCtCitySummaryRecordGroup = 0x62f42635,
  kCtCityKindRecordType = 0xc359837e,
  kCtCityKindRecordGroup = 0x43598384,
  kCtCityIdentityRecordType = 0x035ad1cb,
  kCtCityIdentityRecordGroup = 0x835ad1d1,
  kCtCityDescriptionRecordType = 0x79fdb6c8,
  kCtCityDescriptionRecordGroup = 0x59fdb6fd,
  kCtCityDescriptionRecordInstance = 0x79fdb71e,
  kCtCitySchemeRecordType = 0x0525a6b9,
  kCtCitySchemeRecordGroup = 0x4525a6e2,
};

// Select the current language only for the UTF-8 save boundary. Original byte builds require no added service query.
static u32 CtCurrentSaveLanguage() {
  return CtGetLanguageManager()->GetCurrentLanguage();
}

// Resolve the game's explicit current-language alias before selecting a legacy text codec.
// Early city records retain this zero value instead of carrying a concrete last-save language.
static u32 CtSavedTextPage([[maybe_unused]] u32 language) {
  if (language == kScLanguageCurrent)
    language = CtCurrentSaveLanguage();
  return ScLanguageCrtCodePage(language);
}

bool CtWriteSavedText(RsStream *stream, const ScStringView *text, u32 language) {
  return RsWriteLegacyText(stream, text, CtSavedTextPage(language));
}

// Decode legacy city text into owned native storage without modifying its value on conversion failure.
// Generic archive strings also contain binary data and therefore never perform this conversion themselves.
static bool CtDecodeSavedText([[maybe_unused]] ScString *text, [[maybe_unused]] u32 language) {
  return ScDecodeLegacyText(CtSavedTextPage(language), *text, text);
}

bool CtReadSavedText(RsStream *stream, ScStringView *text, u32 language) {
  return RsReadLegacyText(stream, text, CtSavedTextPage(language));
}

// Read the last-save language from the identity record for a standalone descriptor that has no city receiver.
// Early saves omit this metadata; their text retains the language selected by the original reader at startup.
static u32 CtSavedTextLanguage([[maybe_unused]] RsDatabaseSegment *segment) {
  u32 language = CtCurrentSaveLanguage();
  ScArchiveReader reader(segment, {kCtCityIdentityRecordType, kCtCityIdentityRecordGroup, 0});
  if (reader.IsValid()) {
    RsStream *stream = reader.GetStream();
    u32 ignored, saved;
    if (stream->ReadU32(&ignored) && stream->ReadU32(&ignored) && stream->ReadU32(&ignored) &&
        stream->ReadU32(&ignored) && stream->ReadU32(&saved))
      language = saved;
  }
  return language;
}

// Return the borrowed AdUtilities primary interface acquired for this city without retaining it again.
AdUtilities *CtCityImpl::GetUtilityAdvisor() {
  return utilityAdvisor;
}

// Return the borrowed AdDemographics primary interface acquired for this city without retaining it again.
AdDemographics *CtCityImpl::GetDemographicsAdvisor() {
  return demographicsAdvisor;
}

// Return the borrowed AdPublicSafety primary interface acquired for this city without retaining it again.
AdPublicSafety *CtCityImpl::GetPublicSafetyAdvisor() {
  return publicSafetyAdvisor;
}

// Return the borrowed AdEnvironment primary interface acquired for this city without retaining it again.
AdEnvironment *CtCityImpl::GetEnvironmentAdvisor() {
  return environmentAdvisor;
}

// Return the borrowed AdCityPlanning primary interface acquired for this city without retaining it again.
AdCityPlanning *CtCityImpl::GetCityPlanningAdvisor() {
  return planningAdvisor;
}

// Return the borrowed AdTransportation primary interface acquired for this city without retaining it again.
AdTransportation *CtCityImpl::GetTransportationAdvisor() {
  return transportationAdvisor;
}

// Return the borrowed AdBudget primary interface acquired for this city without retaining it again.
AdBudget *CtCityImpl::GetBudgetAdvisor() {
  return budgetAdvisor;
}

// Count advisor identities in the captured layer range, releasing each temporary query hold before counting it.
// A successful query clears the shared output only after Release, preserving what callbacks can observe.
u32 CtCityImpl::GetAdvisorListLength() {
  CtLayer **first = layers.begin, **last = layers.end;
  void *queried = nullptr;
  u32 count = 0;
  while (first != last) {
    if (*first && (*first)->QueryInterface(IID_AdAdvisor, &queried)) {
      static_cast<AdAdvisor *>(queried)->Release();
      queried = nullptr;
      ++count;
    }
    ++first;
  }
  return count;
}

// Transfer acquired advisor query holds into the caller's array up to its original capacity.
// Preserve the captured layer range and persistent query-output word across successful and failed entries.
u32 CtCityImpl::GetAdvisorList(u32 capacity, AdAdvisor **out) {
  u32 count = 0;
  if (capacity) {
    CtLayer **first = layers.begin, **last = layers.end;
    void *queried = nullptr;
    while (first != last) {
      if (*first && (*first)->QueryInterface(IID_AdAdvisor, &queried)) {
        ++count;
        *out++ = static_cast<AdAdvisor *>(queried);
      }
      if (count >= capacity)
        break;
      ++first;
    }
  }
  return count;
}

// Query the captured roster for a matching advisor ID, clearing the genuine caller output before traversal.
// Publish the matching borrowed identity before releasing its query hold; Release can still change the caller's output.
bool CtCityImpl::GetAdvisor(u32 id, AdAdvisor **out) {
  void *queried = out;
  *out = nullptr;
  CtLayer **first = layers.begin, **last = layers.end;
  while (first != last) {
    if ((*first)->QueryInterface(IID_AdAdvisor, &queried)) {
      AdAdvisor *advisor = static_cast<AdAdvisor *>(queried);
      if (advisor->GetAdvisorID() == id) {
        *out = advisor;
        advisor->Release();
        return true;
      }
      advisor->Release();
    }
    ++first;
  }
  return false;
}

// Advance the city date on daily notifications, publish calendar events, and request the original founding anniversaries.
void CtCityImpl::SimCalendarNotification(i32 phase) {
  u32 type;
  switch (phase) {
  case 2:
    stateLock.Lock();
    ++date.serial;
    stateLock.Unlock();
    type = kCtMsg_DayChanged;
    break;
  case 3:
    type = kCtMsg_WeekChanged;
    break;
  case 4:
    type = kCtMsg_MonthChanged;
    break;
  case 5:
    type = kCtMsg_YearChanged;
    break;
  default:
    return;
  }
  u32 ordinal = date.DayOfYear(), year = date.Year();
  ScMessage message = {type, year, ordinal, 0};
  CtGetMessageServer()->MessagePost(message, false);
  if (phase != 5)
    return;
  u32 born = GetYearBorn(), age = date.Year() - born, request;
  switch (age) {
  case 10:
    request = 500;
    break;
  case 25:
    request = 501;
    break;
  case 50:
    request = 502;
    break;
  case 100:
    request = 503;
    break;
  case 250:
    request = 504;
    break;
  case 500:
    request = 505;
    break;
  case 1000:
    request = 506;
    break;
  case 2000:
    request = 507;
    break;
  default:
    return;
  }
  CtRequestDialog(request);
}

// Execute the new-city command and accept only its exact one-byte confirmation value of one.
// A refused or declined request posts the original bankruptcy notice when the current funds are below the threshold.
bool CtCityImpl::DoNewCity(bool) {
  UiCommandInterface *command = nullptr;
  bool accepted = false;
  if (CtGetCommandCatalog()->GetCommand(
          kCtNewCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->Execute();
    ScVariant *result;
    command->PopFrontOutput(&result);
    accepted = *result->AsBool();
    result->Release();
    command->Release();
    command = nullptr;
  }
  if (accepted == 1)
    return true;
  if (budget && budget->GetTotalFunds() < -kEcBudgetDebtMagnitudeLimit) {
    ScMessage warning = {kWnMsg_ModalAdvisorEndGame, kAdBudgetAdvisorId, kAdFiredAdviceId, 0};
    CtGetMessageServer()->MessagePost(warning, false);
  }
  return false;
}

// Supply the load command's resource selector and query flag as independently owned scalar variants.
// Only a declined available command checks bankruptcy; an unavailable command returns immediately.
bool CtCityImpl::DoLoadCity(bool query) {
  UiCommandInterface *command = nullptr;
  if (!CtGetCommandCatalog()->GetCommand(
          kCtLoadCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)))
    return false;
  ScValue *selector = new ScValue;
  selector->AddRef();
  selector->SetValUint32(kCtFileOpenCity);
  command->PushBackInput(selector);
  selector->Release();
  ScValue *question = new ScValue;
  question->AddRef();
  question->SetValBool(query);
  command->PushBackInput(question);
  question->Release();
  bool executed = command->Execute();
  bool accepted = false;
  if (executed && command->OutputQueueSize()) {
    ScVariant *result;
    command->PopFrontOutput(&result);
    result->GetValBool(&accepted);
    result->Release();
  }
  command->Release();
  command = nullptr;
  if (accepted == 1)
    return true;
  if (budget && budget->GetTotalFunds() < -kEcBudgetDebtMagnitudeLimit) {
    ScMessage warning = {kWnMsg_ModalAdvisorEndGame, kAdBudgetAdvisorId, kAdFiredAdviceId, 0};
    CtGetMessageServer()->MessagePost(warning, false);
  }
  return false;
}

// Populate the original descriptor and render its terrain thumbnail followed by the live current city's zone overlay.
u8 CtCityImpl::WriteDescriptor(RsDatabaseSegment *segment) {
  if (!segment)
    return 0;
  GxBuffer *buffer = nullptr;
  {
    CtCityInfo info(segment, field404);
    if (!field404) {
      info.SetPopulation(residential ? residential->GetPopulation() : 0);
      info.SetFunds(budget ? budget->GetTotalFunds() : 0);
      u32 day = 1, year = 1900;
      GetDate(&day, &year);
      CtDate current(day, year);
      info.SetDate(current);
      info.SetMayorName(ScString(*GetMayorName()));
      info.SetCityName(ScString(*GetCityName()));
      if (dirt) {
        SpMapSpriteFactory *factory = nullptr;
        if (dirt->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
          GxRasterSource *raster = nullptr;
          if (factory->CreateMapSprite(10001, IID_GxRasterSource, reinterpret_cast<void **>(&raster))) {
            CtGetGraphics()->CreateBuffer(&buffer);
            i32 height = raster->GetBaseHeight(), width = raster->GetBaseWidth();
            if (buffer->Init((u32)width, (u32)height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
                    GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
              height = raster->GetBaseHeight();
              width = raster->GetBaseWidth();
              ScRect area = {0, 0, width, height};
              if (buffer->Lock(GxLockFlags::GetSetPixel)) {
                raster->Draw(buffer, area, nullptr);
                buffer->Unlock(GxLockFlags::GetSetPixel);
              }
            }
            raster->Release();
            raster = nullptr;
          }
          factory->Release();
        }
      }
      CtCity *city = CtGetCity();
      if (city) {
        CtZoneLayer *zone = city->ZoneLayer();
        if (zone) {
          SpMapSpriteFactory *factory = nullptr;
          if (zone->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
            if (buffer) {
              GxRasterSource *raster = nullptr;
              if (factory->CreateMapSprite(10011, IID_GxRasterSource, reinterpret_cast<void **>(&raster))) {
                i32 height = raster->GetBaseHeight(), width = raster->GetBaseWidth();
                ScRect area = {0, 0, width, height};
                if (buffer->Lock(GxLockFlags::GetSetPixel)) {
                  raster->Draw(buffer, area, nullptr);
                  buffer->Unlock(GxLockFlags::GetSetPixel);
                }
                raster->Release();
                raster = nullptr;
              }
            }
            factory->Release();
            factory = nullptr;
          }
        }
      }
      info.SetPreview(buffer);
    }
  }
  if (buffer)
    buffer->Release();
  return 1;
}

// Construct the real city and publish its primary city/Unknown identity without adding an initial reference.
// This is the registered constructor phase at SIMCITY100025BF; its private nominal return declaration is unresolved.
ScUnknown *CtCreateCity() {
  return static_cast<CtCity *>(new CtCityImpl);
}

// Pause the live clock before incrementing the hidden counter, then pause the twice-acquired animation manager.
u32 CtCityImpl::SimHiddenPause() {
  if (!clock)
    return 0;
  clock->PauseClock();
  clock->GetPauseCount();
  stateLock.Lock();
  ++hiddenPauseCount;
  stateLock.Unlock();
  if (CtGetTickManager())
    CtGetTickManager()->PauseAnimationSystem(true);
  return hiddenPauseCount;
}

// Resume the clock and reduce a nonzero hidden count, retaining animation resume calls even when the count was zero.
u32 CtCityImpl::SimHiddenUnpause() {
  if (!clock)
    return 0;
  clock->UnpauseClock();
  clock->GetPauseCount();
  stateLock.Lock();
  if (hiddenPauseCount)
    --hiddenPauseCount;
  stateLock.Unlock();
  if (CtGetTickManager())
    CtGetTickManager()->PauseAnimationSystem(false);
  return hiddenPauseCount;
}

CtCityInfo::CtCityInfo(RsDatabaseSegment *source, u32 type)
    : reading(false), ownsSegment(false), kind(type), funds(0), population(0), preview(nullptr) {
  segment = source;
}

// Commit a writable descriptor's version, city metadata and preview through the original record stream.
// Close that record before retiring version data, optionally shut down the segment, then release owned members.
CtCityInfo::~CtCityInfo() {
  if (!reading) {
    if (segment) {
      CtVersionInfo version;
      ScArchiveReader writer(
          segment, {kCtCitySummaryRecordType, kCtCitySummaryRecordGroup, 0}, kScArchiveWriteOverloadTag);
      u8 valid = (u8)writer.IsValid();
      RsStream *stream = writer.GetStream();
      i32 major = version.GetModuleVersion(kScVersionMajor), minor = version.GetModuleVersion(kScVersionMinor),
          build = version.GetModuleVersion(kScVersionBuild), revision = version.GetModuleVersion(kScVersionRevision);
      if (valid) {
        valid = stream->WriteI32(major);
        if (valid) {
          valid = stream->WriteI32(minor);
          if (valid) {
            valid = stream->WriteI32(build);
            if (valid)
              valid = stream->WriteI32(revision);
          }
        }
      }
      if (major) {
        bool complete = valid != 0;
        if (!kind) {
          u32 language = CtCurrentSaveLanguage();
          complete = complete && stream->WriteU32(population) && stream->WriteI64(funds) &&
              stream->WriteU32(date.DayOfYear()) && stream->WriteU32(date.Year()) &&
              CtWriteSavedText(stream, &mayorName, language) && CtWriteSavedText(stream, &cityName, language);
        } else if (kind != 1 && kind != 2)
          complete = false;
        if (complete) {
          RsImageOptions *images = CtGetBufferResourceFactory();
          valid = images->SaveStream(preview, stream);
        } else
          valid = 0;
      }
      if (!valid)
        writer.failed = 1;
    }
    if (ownsSegment)
      segment->Shutdown();
  }
  ScRelease(&preview);
}

void CtCityInfo::SetPopulation(u32 value) {
  if (!reading)
    population = value;
}

void CtCityInfo::SetFunds(i64 value) {
  if (!reading)
    funds = value;
}

void CtCityInfo::SetDate(CtDate value) {
  if (!reading)
    date.serial = value.serial;
}

void CtCityInfo::SetMayorName(ScString value) {
  if (!reading)
    mayorName = value;
}

void CtCityInfo::SetCityName(ScString value) {
  if (!reading)
    cityName = value;
}

// Release the old preview before clearing its field, then publish and retain the requested replacement.
void CtCityInfo::SetPreview(GxBuffer *buffer) {
  if (!reading) {
    ScRelease(&preview);
    if (buffer)
      preview = ScAddRefNotNull(buffer);
  }
}

class CtInitLock {
public:
  // Hold the actual city critical section through provider creation and all ordinary return paths.
  explicit CtInitLock(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the city critical section after later initialization-scope objects have been destroyed.
  ~CtInitLock() {
    lock->Unlock();
  }

  ScLock *lock;
};

// Run the common initialization phase shared by the original city creation and load overloads.
bool CtCityImpl::InitCommon(u32 countX, u32 countY, u32 countZ, TrGeneratorInterface *terrainGenerator) {
  CtInitLock guard(&cityLock);
  if (initialized)
    Shutdown();
  const u32 notifications[] = {kCtMsg_SaveToSegment, kCtMsg_SetGameSpeed, kCtMsg_RequestPause, kCtMsg_RequestUnpause,
      kCtMsg_DisasterStarted, kCtMsg_DisasterEnded, kCtMsg_RequestNewCity, kCtMsg_RequestLoadCity,
      kCtMsg_RequestSaveCity, kCtMsg_RequestQuitGame, kWnMsg_EnterTerrainEditor, kWnMsg_EnterCity,
      kCtMsg_SetSimulationControlsEnabled};
  for (u32 id : notifications)
    CtGetMessageServer()->AddNotification(this, id);
  ScRelease(&clock);
  clock = ScAddRefNotNull(new CtClock);
  clock->Init();
  Pause();
  clock->SetCurrentClockDate(&date);
  SetGameSpeed(1);
  CtVersionInfo executableVersion;
  for (u32 i = 0; i < kScVersionPartCount; ++i)
    version[i] = executableVersion.GetModuleVersion(i);
  readSaveDebug = false;
  cellsX = countX;
  cellsZ = countZ;
  cellsY = countY;
  if (terrainGenerator)
    generator = ScAddRefNotNull(terrainGenerator);
  bool ready = CtGetCom()->GetClassObject(
                   kCtSimulatorClassId, IID_CtSimulatorInterface, reinterpret_cast<void **>(&simulator)) &&
      simulator->Init(this);
  if (ready)
    ready = CtGetCom()->GetClassObject(
        kScOccupantManagerClassId, IID_ScOccupantManager, reinterpret_cast<void **>(&surfaceOccupants));
  if (ready) {
    surfaceOccupants->Init(this, kOcSurfaceManagerId, kOcMsg_SurfaceInserted, kOcMsg_SurfaceRemoved,
        kOcMsg_SurfaceChanged, 1);
    ready = CtGetCom()->GetClassObject(
        kScOccupantManagerClassId, IID_ScOccupantManager, reinterpret_cast<void **>(&undergroundOccupantsOne));
  }
  if (ready) {
    undergroundOccupantsOne->Init(this, kOcPipeManagerId, kOcMsg_PipeInserted, kOcMsg_PipeRemoved,
        kOcMsg_PipeChanged, 2);
    ready = CtGetCom()->GetClassObject(
        kScOccupantManagerClassId, IID_ScOccupantManager, reinterpret_cast<void **>(&undergroundOccupantsTwo));
  }
  if (ready) {
    undergroundOccupantsTwo->Init(this, kOcSubwayManagerId, kOcMsg_SubwayInserted, kOcMsg_SubwayRemoved,
        kOcMsg_SubwayChanged, 4);
    ready = CtGetCom()->GetClassObject(
        kOcAnimManagerClassId, IID_OcAnimManager, reinterpret_cast<void **>(&animationOccupants));
  }
  if (!ready) {
    Shutdown();
    return false;
  }
  animationOccupants->Init(this, kOcAnimManagerId);
  InitLayers();
  CtAgentTreeIterator *iterator = nullptr;
  CtCityDefinition->GetAgentTypeTree()->CreateNodeIterator(&iterator);
  i32 flags = (i32)countZ;
  while (CtAgentType *type = iterator->Next()) {
    CtLayer **first = layers.begin, **last = layers.end;
    CtLayerManipulator *manipulator = nullptr;
    while (first != last) {
      if ((*first)->GetManipulator(*type, &manipulator, &flags) && manipulator)
        type->AttachManipulator(manipulator, flags);
      manipulator = nullptr;
      ++first;
    }
  }
  iterator->Release();
  ScRelease(&generator);
  initialized = true;
  return true;
}

// Initialize a city from the installed new-city settings interface, preserving repeated live getters and generator holds.
// Publish options and initial cash or loan only after common initialization succeeds, then resume the hidden clock pause.
bool CtCityImpl::Init(CtNewCityInfo *info) {
  CtInitLock guard(&cityLock);
  if (progress)
    delete progress;
  u32 count = 2 * CtCityStaticLayerCount;
  ScString caption;
  {
    CtTextResource resource(kRsGameLoadingText, kRsGameTextGroup);
    caption = resource.GetText();
  }
  progress = new CtProgress(count, &caption);
  if (initialized)
    Shutdown();
  cellsX = info->GetCellCountX();
  cellsZ = info->GetCellCountZ();
  cellsY = info->GetCellCountY();
  cityType = info->GetCityType();
  TrGeneratorInterface *terrainGenerator = info->GetTerrainGenerator();
  if (terrainGenerator)
    terrainGenerator->AddRef();
  CtDate initialDate(1, 1, info->GetStartYear());
  date.serial = initialDate.serial;
  if (!InitCommon(cellsX, cellsY, cellsZ, terrainGenerator)) {
    Shutdown();
    return false;
  }
  SetDate(1, 1, info->GetStartYear());
  SetYearBorn(info->GetStartYear());
  SimHiddenPause();
  SetCityName(*info->GetCityName());
  SetMayorName(*info->GetMayorName());
  SetDifficulty(info->GetDifficulty());
  SetGameSpeed(1);
  SetDisastersEnabled(info->GetDisastersByte() != 0);
  SetAutoBudgetEnabled(info->GetAutoBudgetByte() != 0);
  if (budget) {
    if (info->GetLoanByte() == 1) {
      budget->SetTotalFunds(0);
      budget->IssueBond((u32)info->GetInitialFunds());
    } else
      budget->SetTotalFunds(info->GetInitialFunds());
  }
  if (terrainGenerator)
    terrainGenerator->Release();
  SimHiddenUnpause();
  initialized = true;
  return true;
}

// Restore saved state through scoped records, preserving partial version fields and closing the main record before services.
// Publish options and names after common initialization, then import each entry in the live layer roster.
bool CtCityImpl::Init(RsDatabaseSegment *segment) {
  CtInitLock guard(&cityLock);
  if (!segment)
    return false;
  if (progress)
    delete progress;
  u32 count = 3 * CtCityStaticLayerCount + 1;
  ScString caption;
  {
    CtTextResource resource(kRsGameLoadingText, kRsGameTextGroup);
    caption = resource.GetText();
  }
  progress = new CtProgress(count, &caption);
  RsFileSegment *file = nullptr;
  if (segment->QueryInterface(IID_RsFileSegment, reinterpret_cast<void **>(&file))) {
    file->LoadRecords();
    file->Release();
    file = nullptr;
  }
  if (progress)
    progress->Step();
  for (u32 i = 0; i < 4; ++i)
    savedVersion[i] = 0;
  ScString ignored, name, mayor;
  // Initialize external-read scratch to exclude the stock undefined reads when a refused read leaves its output unwritten.
  i32 major = 0, minor = 0, patch = 0;
  u32 unused = 0, countX = 0, countY = 0, countZ = 0, day = 0, year = 0, requestedSpeed = 0, born = 1900;
  u8 value = 0;
  bool result, automatic = false, allowDisasters = false;
  {
    ScArchiveFrame frame(this, segment, {kCtStateHeaderRecordType, kCtStateHeaderRecordGroup, 0});
    RsStream *stream = frame.GetStream();
    readSaveDebug = frame.flag != 0;
    result = stream != nullptr;
    if (stream) {
      do {
        if (frame.flag) {
          ScString tag;
          result = stream->ReadString(&tag) != 0;
          if (!result)
            break;
        }
        result = stream->ReadI32(&major) != 0;
        savedVersion[0] = (u32)major;
        if (!result)
          break;
        result = stream->ReadI32(&minor) != 0;
        savedVersion[1] = (u32)minor;
        if (!result)
          break;
        result = stream->ReadI32(&patch) != 0;
        savedVersion[2] = (u32)patch;
        if (!result)
          break;
        if (!(result = stream->ReadI32(reinterpret_cast<i32 *>(&savedVersion[3])) != 0))
          break;
        if (!(result = stream->ReadU32(&unused) != 0))
          break;
        if (!(result = stream->ReadString(&ignored) != 0))
          break;
        if (!(result = stream->ReadU32(&countX) != 0))
          break;
        if (!(result = stream->ReadU32(&countY) != 0))
          break;
        if (!(result = stream->ReadU32(&countZ) != 0))
          break;
        if (!(result = stream->ReadString(&name) != 0))
          break;
        if (!(result = stream->ReadString(&mayor) != 0))
          break;
        if (!(result = stream->ReadU32(&day) != 0))
          break;
        if (!(result = stream->ReadU32(&year) != 0))
          break;
        if (!(result = stream->ReadU32(&requestedSpeed) != 0))
          break;
        if (!(result = stream->ReadU32(&born) != 0))
          break;
        if (!(result = stream->ReadU8(&value) != 0))
          break;
        allowDisasters = value != 0;
        if (!(result = stream->ReadU8(&value) != 0))
          break;
        automatic = value != 0;
        if (!(result = stream->ReadU8(&value) != 0))
          break;
        if (frame.flag) {
          ScString tag;
          result = stream->ReadString(&tag) != 0;
        }
      } while (false);
    }
    if (!result)
      frame.Fail();
  }
  if (result) {
    cityType = CtReadCityType(segment);
    if (!cityType)
      cityType = 3;
    cityID = ReadIdentity(segment);
    result = CtDecodeSavedText(&name, lastLanguage) && CtDecodeSavedText(&mayor, lastLanguage);
    if (result && !InitCommon(countX, countY, countZ, nullptr)) {
      Shutdown();
      result = false;
    }
    if (result) {
      SetDate(day, year);
      SetYearBorn(born);
      SetAutoBudgetEnabled(automatic);
      SetDisastersEnabled(allowDisasters);
      SimHiddenPause();
      SetGameSpeed((i32)requestedSpeed);
      SetCityName(name);
      SetMayorName(mayor);
      ReadDescription(segment);
      if (layers.Count()) {
        result = true;
        for (u32 i = 0; i < layers.Count(); ++i) {
          if (progress)
            progress->Step();
          layers.begin[i]->Init(this, segment);
        }
      }
      SimHiddenUnpause();
    }
  }
  if (!result && progress) {
    delete progress;
    progress = nullptr;
  }
  return result;
}

// Initialize from the real legacy importer, converting its calendar and revisiting the live registry for layer imports.
// Preserve the unsigned height floor, normal fallback date and raw-plane speed selection before publishing the name/type.
bool CtCityImpl::Init(ScImporter *importer) {
  CtInitLock guard(&cityLock);
  if (progress)
    delete progress;
  ScString caption;
  {
    CtTextResource resource(kRsGameLoadingText, kRsGameTextGroup);
    caption = resource.GetText();
  }
  progress = new CtProgress(3 * CtCityStaticLayerCount, &caption);
  u32 countX = 0, countZ = 0, countY = 0;
  if (importer->GetBaseMapSize(&countX, &countZ, &countY)) {
    if (countY < 256)
      countY = 256;
  } else {
    countX = 128;
    countZ = 128;
    countY = 256;
  }
  if (!InitCommon(countX, countY, countZ, nullptr)) {
    Shutdown();
    return false;
  }
  char nameBytes[32];
  importer->GetCityName(nameBytes);
  u32 yearWord, dayWord;
  if (importer->GetMiscValue(3, reinterpret_cast<i32 *>(&yearWord)) && importer->GetMiscValue(4, reinterpret_cast<i32 *>(&dayWord))) {
    i32 days = (i32)dayWord;
    SetDate((u32)(days / 25 % 12 + 1), (u32)(days % 25 + 1), yearWord + (u32)(days / 300));
    SetYearBorn(yearWord);
  } else {
    SetDate(1, 1, 3000);
    SetYearBorn(3000);
  }
  SimHiddenPause();
  for (u32 i = 0; i < layers.Count(); ++i) {
    if (progress)
      progress->Step();
    layers.begin[i]->Init(this, importer);
  }
  u32 requestedSpeed = 1;
  if (importer->MapIsPresent(kCtLegacyMiscPlane)) {
    importer->GetMiscValue(1019, reinterpret_cast<i32 *>(&requestedSpeed));
    switch (requestedSpeed) {
    case 1:
      requestedSpeed = 2;
      break;
    case 2:
      requestedSpeed = 3;
      break;
    case 3:
      requestedSpeed = 4;
      break;
    default:
      requestedSpeed = 1;
      break;
    }
  }
  SetGameSpeed((i32)requestedSpeed);
  ScString importedName(nameBytes, (u32)strlen(nameBytes));
  SetCityName(importedName);
  cityType = 1;
  SimHiddenUnpause();
  return true;
}

// Acquire the city roster and initialize its captured forward range, stepping the live progress receiver before each
// entry.
void CtCityImpl::InitLayers() {
  AcquireLayers();
  CtLayer **first = layers.begin, **last = layers.end;
  while (first != last) {
    if (progress)
      progress->Step();
    if (*first)
      (*first)->Init(this);
    ++first;
  }
}

// Acquire the ordered layer roster through the actual module factories and retain their common interfaces in storage.
void CtCityImpl::AcquireLayers() {
  CtLayer *common;
  dirt = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kTrDirtBagClassId, &common, IID_ScDirtBag, reinterpret_cast<void **>(&dirt)) && common)
    layers.Append(common);
  buildings = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kGeBuildingControllerClassId, &common, IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings)) &&
      common)
    layers.Append(common);
  transit = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kNwManagerClassId, &common, IID_NwManager, reinterpret_cast<void **>(&transit)) && common)
    layers.Append(common);
  flora = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kGeFloraControllerClassId, &common, IID_GeFlora, reinterpret_cast<void **>(&flora)) && common)
    layers.Append(common);
  zones = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcZoneLayerClassId, &common, IID_CtZoneLayer, reinterpret_cast<void **>(&zones)) && common)
    layers.Append(common);
  disasters = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kDsControllerClassId, &common, IID_DsLayer, reinterpret_cast<void **>(&disasters)) && common)
    layers.Append(common);
  neighbors = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtNeighborsClassId, &common, IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors)) &&
      common)
    layers.Append(common);
  common = nullptr;
  if (CtAcquireLayer(kEcDemandClassId, &common, 0, nullptr) && common)
    layers.Append(common);
  ordinances = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtOrdinancesClassId, &common, IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances)) &&
      common)
    layers.Append(common);
  common = nullptr;
  if (CtAcquireLayer(kCtAuraClassId, &common, 0, nullptr) && common)
    layers.Append(common);
  pollution = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcPollutionClassId, &common, IID_EcLayer, reinterpret_cast<void **>(&pollution)) && common)
    layers.Append(common);
  crime = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtCrimeClassId, &common, IID_CtCrimeLayer, reinterpret_cast<void **>(&crime)) && common)
    layers.Append(common);
  police = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtPoliceClassId, &common, IID_CtPoliceLayer, reinterpret_cast<void **>(&police)) && common)
    layers.Append(common);
  fire = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtFireClassId, &common, IID_CtFireLayer, reinterpret_cast<void **>(&fire)) && common)
    layers.Append(common);
  residential = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcResidentialClassId, &common, IID_CtResidentialLayer,
      reinterpret_cast<void **>(&residential)) && common)
    layers.Append(common);
  commercial = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcCommercialClassId, &common, IID_EcCommercialLayer,
      reinterpret_cast<void **>(&commercial)) && common)
    layers.Append(common);
  industrial = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcIndustrialClassId, &common, IID_EcIndustrialLayer,
      reinterpret_cast<void **>(&industrial)) && common)
    layers.Append(common);
  landValue = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcLandValueClassId, &common, IID_CtLandValueLayer,
      reinterpret_cast<void **>(&landValue)) && common)
    layers.Append(common);
  streetSimulation = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kNwStreetLayerClassId, &common, IID_CtStartSimLayer, reinterpret_cast<void **>(&streetSimulation)) &&
      common)
    layers.Append(common);
  traffic = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kNwTransitLayerClassId, &common, IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic)) &&
      common)
    layers.Append(common);
  budget = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEcBudgetClassId, &common, IID_EcBudgetLayer, reinterpret_cast<void **>(&budget)) && common)
    layers.Append(common);
  world = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtWorldClassId, &common, IID_CtWorldLayer, reinterpret_cast<void **>(&world)) && common)
    layers.Append(common);
  power = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kCtPowerSimulationClassId, &common, IID_CtPowerLayer, reinterpret_cast<void **>(&power)) && common)
    layers.Append(common);
  plumbing = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kCtPlumbingSimulationClassId, &common, IID_CtPlumbingLayer, reinterpret_cast<void **>(&plumbing)) &&
      common)
    layers.Append(common);
  common = nullptr;
  if (CtAcquireLayer(kCtFlammabilityClassId, &common, 0, nullptr) && common)
    layers.Append(common);
  common = nullptr;
  if (CtAcquireLayer(kGePortLayerClassId, &common, 0, nullptr) && common)
    layers.Append(common);
  weather = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kEnWeatherClassId, &common, IID_EnWeatherLayer, reinterpret_cast<void **>(&weather)) && common)
    layers.Append(common);
  demolition = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kGeDemolitionClassId, &common, IID_GeDemolitionView, reinterpret_cast<void **>(&demolition)) &&
      common)
    layers.Append(common);
  common = nullptr;
  if (CtAcquireLayer(kCtScenarioLayerClassId, &common, IID_CtScenarioLayer, reinterpret_cast<void **>(&scenario)) &&
      common)
    layers.Append(common);
  utilityAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kAdUtilitiesAdvisorClassId, &common, IID_AdUtilities, reinterpret_cast<void **>(&utilityAdvisor)) &&
      common)
    layers.Append(common);
  demographicsAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kAdDemographicsAdvisorClassId, &common, IID_AdDemographics,
          reinterpret_cast<void **>(&demographicsAdvisor)) &&
      common)
    layers.Append(common);
  publicSafetyAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kAdPublicSafetyAdvisorClassId, &common, IID_AdPublicSafety,
          reinterpret_cast<void **>(&publicSafetyAdvisor)) &&
      common)
    layers.Append(common);
  environmentAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kAdEnvironmentAdvisorClassId, &common, IID_AdEnvironment, reinterpret_cast<void **>(&environmentAdvisor)) &&
      common)
    layers.Append(common);
  planningAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(
          kAdCityPlanningAdvisorClassId, &common, IID_AdCityPlanning, reinterpret_cast<void **>(&planningAdvisor)) &&
      common)
    layers.Append(common);
  transportationAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kAdTransportationAdvisorClassId, &common, IID_AdTransportation,
          reinterpret_cast<void **>(&transportationAdvisor)) &&
      common)
    layers.Append(common);
  budgetAdvisor = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kAdBudgetAdvisorClassId, &common, IID_AdBudget, reinterpret_cast<void **>(&budgetAdvisor)) &&
      common)
    layers.Append(common);
  petitioner = nullptr;
  common = nullptr;
  if (CtAcquireLayer(kAdPetitionersClassId, &common, IID_AdPetitionerManager, reinterpret_cast<void **>(&petitioner)) &&
      common)
    layers.Append(common);
}

// Release the typed primary interfaces before the live forward registry, preserving provider callback ordering.
void CtCityImpl::ReleaseLayers() {
  ScRelease(&budgetAdvisor);
  ScRelease(&transportationAdvisor);
  ScRelease(&planningAdvisor);
  ScRelease(&environmentAdvisor);
  ScRelease(&publicSafetyAdvisor);
  ScRelease(&demographicsAdvisor);
  ScRelease(&utilityAdvisor);
  ScRelease(&petitioner);
  ScRelease(&demolition);
  ScRelease(&weather);
  ScRelease(&pollution);
  ScRelease(&crime);
  ScRelease(&police);
  ScRelease(&fire);
  ScRelease(&residential);
  ScRelease(&commercial);
  ScRelease(&industrial);
  ScRelease(&landValue);
  ScRelease(&ordinances);
  ScRelease(&budget);
  ScRelease(&zones);
  ScRelease(&disasters);
  ScRelease(&neighbors);
  ScRelease(&power);
  ScRelease(&plumbing);
  ScRelease(&flora);
  if (transit) {
    // SIMNTWRK QI4147C2FB publishes the same primary CtLayer lifetime prefix.
    reinterpret_cast<CtLayer *>(transit)->Release();
    transit = nullptr;
  }
  ScRelease(&traffic);
  ScRelease(&streetSimulation);
  ScRelease(&world);
  ScRelease(&buildings);
  ScRelease(&dirt);
  ScRelease(&scenario);
  for (u32 i = 0; i < layers.Count(); ++i)
    layers.begin[i]->Release();
  layers.end = layers.begin;
}

bool CtCityImpl::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

// Search live layer order by its actual virtual type, reloading the matched registry entry after the callback.
CtLayer *CtCityImpl::GetSpecificLayer(u32 type) {
  for (u32 i = 0; i < layers.Count(); ++i)
    if (layers.begin[i]->GetLayerType() == type)
      return layers.begin[i];
  return nullptr;
}

// Search the actual registry directly and forward the caller's genuine interface output to the matched layer.
bool CtCityImpl::GetLayer(u32 type, u32 iid, void **out) {
  for (u32 i = 0; i < layers.Count(); ++i)
    if (layers.begin[i]->GetLayerType() == type)
      return layers.begin[i]->QueryInterface(iid, out);
  return false;
}

u32 CtCityImpl::NumberOfLayers() {
  return layers.Count();
}

// Preserve the original index and null-entry checks before querying the requested interface.
bool CtCityImpl::GetLayerByIndex(u32 index, u32 iid, void **out) {
  if (index < layers.Count() && layers.begin[index])
    return layers.begin[index]->QueryInterface(iid, out);
  return false;
}

// Publish and retain each live registry entry, checking capacity only after the original first write.
u32 CtCityImpl::GetLayerList(u32 capacity, CtLayer **out) {
  u32 count = 0;
  for (u32 i = 0; i < layers.Count(); ++i) {
    CtLayer *layer = layers.begin[i];
    out[i] = layer;
    layer->AddRef();
    if (++count >= capacity)
      break;
  }
  return count;
}

// Clear the three static occupant managers followed by the animated manager through their live virtual accessors.
// Each provider is required by the city contract; completing the four calls returns the original true result.
bool CtCityImpl::RemoveAllOccupants() {
  SurfaceOccupantManager()->RemoveAllOccupants();
  UndergroundOccupantManagerLevelOne()->RemoveAllOccupants();
  UndergroundOccupantManagerLevelTwo()->RemoveAllOccupants();
  AnimOccupantManager()->RemoveAllOccupants();
  return true;
}

// Visit every live layer, including null entries, through the caller-owned enumerator and original mutable message.
// Stop on a refused callback; otherwise revisit the live count and storage so defined roster changes remain visible.
bool CtCityImpl::DoEnumLayers(ScMessage *message, CtCityEnumerator *enumerator) {
  for (u32 i = 0; i < layers.Count(); ++i)
    if (!enumerator->Enumerate(message, layers.begin[i]))
      return false;
  return true;
}

static const CtDate CtInitialCityDate(1, 1, 1900);

CtLayerStorage::CtLayerStorage() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

CtLayerStorage::~CtLayerStorage() {
  delete[] begin;
}

CtDialogJobs::CtDialogJobs() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

// Copy the live pending-dialog words into independent storage, retaining the original snapshot lifetime at startup.
CtDialogJobs::CtDialogJobs(const CtDialogJobs &source) : begin(nullptr), end(nullptr), storageEnd(nullptr) {
  u32 count = source.Count();
  if (count) {
    begin = new u32[count];
    for (u32 i = 0; i < count; ++i)
      begin[i] = source.begin[i];
    end = begin + count;
    storageEnd = end;
  }
}

CtDialogJobs::~CtDialogJobs() {
  delete[] begin;
}

// Construct the actual strings, date, three locks and ordered city fields, then obtain the serial and locale values.
CtCityImpl::CtCityImpl()
    : cityType(0), field12(3), cityID(0), initialized(false), references(0), difficulty(0), editMode(0), modeFlags(0),
      cellsX(0), cellsZ(0), cellsY(0), surfaceOccupants(nullptr), undergroundOccupantsOne(nullptr),
      undergroundOccupantsTwo(nullptr), animationOccupants(nullptr), buildings(nullptr), dirt(nullptr),
      transit(nullptr), traffic(nullptr), flora(nullptr), zones(nullptr), power(nullptr), plumbing(nullptr),
      ordinances(nullptr), budget(nullptr), world(nullptr), streetSimulation(nullptr), disasters(nullptr),
      pollution(nullptr), crime(nullptr), police(nullptr), fire(nullptr), residential(nullptr), commercial(nullptr),
      industrial(nullptr), landValue(nullptr), neighbors(nullptr), weather(nullptr), demolition(nullptr),
      utilityAdvisor(nullptr), budgetAdvisor(nullptr), planningAdvisor(nullptr), environmentAdvisor(nullptr),
      demographicsAdvisor(nullptr), publicSafetyAdvisor(nullptr), transportationAdvisor(nullptr), petitioner(nullptr),
      simulator(nullptr), field296(nullptr), simulationStarted(false), clock(nullptr), yearBorn(1900), pauseCount(0),
      hiddenPauseCount(0), clockPauseCount(0), speed(2), previousSpeed(2), canPause(true), canChangeSpeed(true),
      canLoadSave(true), generator(nullptr), writeSaveDebug(false), readSaveDebug(false), field404(0),
      progress(nullptr), tipOfTheDayNeeded(true), autoBudget(true), disastersEnabled(false), holidayFeatures(true) {
  for (u32 i = 0; i < 4; ++i) {
    version[i] = 0;
    savedVersion[i] = 0;
  }
  date.serial = CtInitialCityDate.serial;
  writeSaveDebug = false;
  serialNumber = CtSeedFromTime();
  originalLanguage = CtGetLanguageManager()->GetCurrentLanguage();
  originalCountry = CtGetLanguageManager()->GetCurrentCountry();
  lastLanguage = originalLanguage;
  lastCountry = originalCountry;
}

CtCityImpl::~CtCityImpl() {
  Shutdown();
}

u32 CtDialogJobs::Count() const {
  return begin ? (u32)(end - begin) : 0;
}

// Append one original dialog identifier, growing only the dedicated word storage when its capacity is exhausted.
void CtDialogJobs::Append(u32 identifier) {
  if (end == storageEnd) {
    u32 count = Count();
    if (count >= kCtDialogJobMaximumCount)
      ScFatal(SC_ERROR_NOMEM, "City dialog storage is too long");
    u32 capacity = count ? count * 2 : 1;
    if (capacity > kCtDialogJobMaximumCount)
      capacity = kCtDialogJobMaximumCount;
    u32 *values = new u32[capacity];
    for (u32 i = 0; i < count; ++i)
      values[i] = begin[i];
    delete[] begin;
    begin = values;
    end = values + count;
    storageEnd = values + capacity;
  }
  *end++ = identifier;
}

u32 CtLayerStorage::Count() const {
  return begin ? (u32)(end - begin) : 0;
}

// Grow the dedicated native layer-pointer storage and append the caller's already-retained interface.
void CtLayerStorage::Append(CtLayer *layer) {
  if (end == storageEnd) {
    u32 count = Count();
    if (count >= kCtLayerStorageMaximumCount)
      ScFatal(SC_ERROR_NOMEM, "City layer storage is too long");
    u32 capacity = count ? count * 2 : 1, limit = kCtLayerStorageMaximumCount;
    if (capacity > limit)
      capacity = limit;
    CtLayer **values = new CtLayer *[capacity];
    for (u32 i = 0; i < count; ++i)
      values[i] = begin[i];
    delete[] begin;
    begin = values;
    end = values + count;
    storageEnd = values + capacity;
  }
  *end++ = layer;
}

// Route each original city command through its genuine method, reading only the payload words that command consumes.
bool CtCityImpl::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kCtMsg_RequestSaveCity:
    DoSaveCity((u32)message->data1 != 0);
    break;
  case kCtMsg_SaveToSegment:
    Save(reinterpret_cast<RsDatabaseSegment *>(message->data1));
    break;
  case kCtMsg_DisasterEnded:
    SetDisasterActive(false);
    break;
  case kCtMsg_SetSimulationControlsEnabled:
    canPause = (u32)message->data1 != 0;
    canChangeSpeed = (u32)message->data1 != 0;
    break;
  case kWnMsg_EnterTerrainEditor:
    SetEditMode(2);
    ScDelete(&progress);
    break;
  case kWnMsg_EnterCity:
    SetEditMode(1);
    ScDelete(&progress);
    StartCitySimulation();
    break;
  case kCtMsg_RequestLoadCity:
    DoLoadCity((u32)message->data1 != 0);
    break;
  case kCtMsg_RequestQuitGame:
    DoQuitGame((u32)message->data1 != 0);
    break;
  case kCtMsg_SetGameSpeed:
    SetGameSpeed((i32)(u32)message->data1);
    break;
  case kCtMsg_RequestPause:
    Pause();
    break;
  case kCtMsg_RequestUnpause:
    Unpause();
    break;
  case kCtMsg_RequestNewCity:
    DoNewCity((u32)message->data1 != 0);
    break;
  case kCtMsg_DisasterStarted:
    SetDisasterActive(true);
    break;
  }
  return true;
}

bool CtCityImpl::DoSaveCity(bool) {
  return false;
}

// Read the raw city-type word through the actual scoped reader, marking the record for abort when opening or reading fails.
// This is the complete 1000774A body; private signedness is unknown, so the native result follows u32 caller use.
u32 SC3_STDCALL CtReadCityType(RsDatabaseSegment *segment) {
  ScArchiveReader reader(segment, {kCtCityKindRecordType, kCtCityKindRecordGroup, 0});
  i32 type = 0;
  if (!reader.IsValid() || !reader.GetStream()->ReadI32(&type)) {
    reader.failed = 1;
    return 0;
  }
  return (u32)type;
}

// Read identity and locale metadata, publishing each field only after its own successful read and keeping earlier
// updates.
u32 CtCityImpl::ReadIdentity(RsDatabaseSegment *segment) {
  ScArchiveReader reader(segment, {kCtCityIdentityRecordType, kCtCityIdentityRecordGroup, 0});
  u32 identifier = 0, serial = 0;
  i32 first = 0, second = 0;
  RsStream *stream = nullptr;
  if (!reader.IsValid())
    goto failed;
  stream = reader.GetStream();
  if (!stream->ReadU32(&identifier) || !stream->ReadU32(&serial))
    goto failed;
  serialNumber = serial;
  if (!stream->ReadI32(&first))
    goto failed;
  originalLanguage = first;
  if (!stream->ReadI32(&second))
    goto failed;
  originalCountry = second;
  if (!stream->ReadI32(&first))
    goto failed;
  lastLanguage = first;
  if (!stream->ReadI32(&second))
    goto failed;
  lastCountry = second;
  return identifier;
failed:
  reader.failed = 1;
  return 0;
}

// Load the separately framed description when its scope and stream are available, preserving the raw final read byte.
// Missing description is accepted; read refusal does not mark this scope for abort in the original 10004B85 body.
u8 CtCityImpl::ReadDescription(RsDatabaseSegment *segment) {
  ScArchiveFrame frame(
      this, segment, {kCtCityDescriptionRecordType, kCtCityDescriptionRecordGroup, kCtCityDescriptionRecordInstance});
  u8 valid = frame.valid;
  RsStream *stream = frame.GetStream();
  if (valid && stream) {
    u32 tag;
    u8 result = stream->ReadU32(&tag);
    if (result)
      result = CtReadSavedText(stream, &description, tag);
    return result;
  }
  return 1;
}

// Write the separate type word from field12, retaining the original success result after a nonnull segment is supplied.
u8 CtCityImpl::WriteCityType(RsDatabaseSegment *segment) {
  if (!segment)
    return 0;
  ScArchiveReader writer(segment, {kCtCityKindRecordType, kCtCityKindRecordGroup, 0}, kScArchiveWriteOverloadTag);
  if (!writer.IsValid() || !writer.GetStream()->WriteI32((i32)field12))
    writer.failed = 1;
  return 1;
}

// Write identity and original locale before reading the current locale through the actual language service.
// The full private body returns success for any nonnull segment; write refusal marks the scoped record for abort.
u8 CtCityImpl::WriteIdentity(RsDatabaseSegment *segment) {
  if (!segment)
    return 0;
  ScArchiveReader writer(
      segment, {kCtCityIdentityRecordType, kCtCityIdentityRecordGroup, 0}, kScArchiveWriteOverloadTag);
  RsStream *stream = writer.GetStream();
  if (!writer.IsValid() || !stream->WriteU32(cityID) || !stream->WriteU32(serialNumber) ||
      !stream->WriteI32(originalLanguage) || !stream->WriteI32(originalCountry) ||
      !stream->WriteI32(CtGetLanguageManager()->GetCurrentLanguage()) ||
      !stream->WriteI32(CtGetLanguageManager()->GetCurrentCountry()))
    writer.failed = 1;
  return 1;
}

// Write the current language and the virtual description inside the original version1 frame with debug disabled.
// Preserve the raw final byte and normal close on payload refusal; 10004C17 does not mark the frame failed.
u8 CtCityImpl::WriteDescription(RsDatabaseSegment *segment) {
  ScArchiveFrame frame(this, segment,
      {kCtCityDescriptionRecordType, kCtCityDescriptionRecordGroup, kCtCityDescriptionRecordInstance}, 1, 0);
  u8 valid = frame.valid;
  RsStream *stream = frame.GetStream();
  if (valid && stream) {
    u32 language = CtGetLanguageManager()->GetCurrentLanguage();
    u8 result = stream->WriteU32(language);
    if (result)
      result = CtWriteSavedText(stream, GetCityDescription(), language);
    return result;
  }
  return 0;
}

static SC_NORETURN void CtUndefinedOccupantFactory() {
  assert(false && "The original city occupant factory would dispatch through a null receiver");
  abort();
}

// Query each live layer using the original null output pointer; empty and all-refused rosters return false.
bool CtCityImpl::CreateOccupant(RsKey *, u32, void **) {
  u32 index = 0;
  if (!layers.Count())
    return false;
  do {
    if (layers.begin[index]->QueryInterface(IID_OcFactory, nullptr))
      CtUndefinedOccupantFactory();
    ++index;
  } while (index < layers.Count());
  return false;
}

// Preserve the attribute overload's identical live query traversal and untouched creation arguments on refusal.
// Its successful query has the same original undefined receiver access as the key overload.
bool CtCityImpl::CreateOccupant(GeAttrib *, u32, void **) {
  u32 index = 0;
  if (!layers.Count())
    return false;
  do {
    if (layers.begin[index]->QueryInterface(IID_OcFactory, nullptr))
      CtUndefinedOccupantFactory();
    ++index;
  } while (index < layers.Count());
  return false;
}

// Publish the original primary city/message identity or its distinct occupant-factory subobject, retaining success.
bool CtCityImpl::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtCity);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtCity);
  HANDLE_QUERY_INTERFACE(IID_CtCity, CtCity);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtCity);
  HANDLE_QUERY_INTERFACE(IID_OcFactory, OcFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the city count under its state lock, then retain the original module director before returning the live count.
u32 CtCityImpl::AddRef() {
  stateLock.Lock();
  ++references;
  stateLock.Unlock();
  CtGetDirector()->AddRef();
  return references;
}

// Remove the module hold before decrementing a nonzero city count, and unlock before final virtual destruction.
u32 CtCityImpl::Release() {
  stateLock.Lock();
  if (references) {
    CtGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    stateLock.Unlock();
    delete this;
    return 0;
  }
  stateLock.Unlock();
  return references;
}

class CtSaveLock {
public:
  // Hold the city lock through progress, record writes, layer saves and final cursor retirement.
  explicit CtSaveLock(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Unlock after every later save temporary has retired.
  ~CtSaveLock() {
    lock->Unlock();
  }

  ScLock *lock;
};

// Serialize the city header and all live layers, keeping metadata side effects independent of the main record result.
// Retire the optional file hold and progress display before the cursor, caption and city lock leave scope.
bool CtCityImpl::Save(RsDatabaseSegment *segment) {
  CtSaveLock guard(&cityLock);
  if (!segment)
    return false;
  if (progress)
    delete progress;
  u32 count = CtCityStaticLayerCount + 1;
  ScString caption;
  {
    CtTextResource resource(kRsGameSavingText, kRsGameTextGroup);
    caption = resource.GetText();
  }
  progress = new CtProgress(count, &caption);
  CtCursorScope cursor(14);
  SimHiddenPause();
  RsFileSegment *file = nullptr;
  segment->QueryInterface(IID_RsFileSegment, reinterpret_cast<void **>(&file));
  if (!serialNumber)
    serialNumber = CtSeedFromTime();
  WriteCityType(segment);
  cityID = 0;
  WriteIdentity(segment);
  WriteDescriptor(segment);
  WriteDescription(segment);
  WriteScheme(segment);
  if (progress)
    progress->Step();
  bool result;
  {
    ScArchiveFrame frame(this, segment, {kCtStateHeaderRecordType, kCtStateHeaderRecordGroup, 0}, 4, 0);
    writeSaveDebug = false;
    u8 valid = frame.valid;
    RsStream *stream = frame.GetStream();
    CtVersionInfo executable;
    for (u32 i = 0; i < kScVersionPartCount; ++i)
      valid = valid && stream->WriteI32(executable.GetModuleVersion(i));
    valid = valid && stream->WriteU32(executable.GetSystemType());
    ScString user;
    executable.UserName(&user, nullptr);
    u32 language = CtCurrentSaveLanguage();
    valid = valid && CtWriteSavedText(stream, &user, language) && stream->WriteU32(CellCountX()) &&
        stream->WriteU32(CellCountY()) && stream->WriteU32(CellCountZ()) &&
        CtWriteSavedText(stream, GetCityName(), language) && CtWriteSavedText(stream, GetMayorName(), language);
    u32 day, year;
    GetDate(&day, &year);
    valid = valid && stream->WriteU32(day) && stream->WriteU32(year);
    i32 requestedSpeed = GetGameSpeed();
    valid = valid && stream->WriteU32((u32)requestedSpeed);
    u32 born = GetYearBorn();
    valid = valid && stream->WriteU32(born);
    bool enabled = AreDisastersEnabled();
    valid = valid && stream->WriteU8(enabled);
    enabled = IsAutoBudgetEnabled();
    valid = valid && stream->WriteU8(enabled);
    ApPreferences *preferences = CtGetGameApp()->GetAppPreferences();
    enabled = *reinterpret_cast<const u8 *>(preferences->SimPrefs()) == 1;
    valid = valid && stream->WriteU8(enabled);
    result = valid != 0;
    if (!valid)
      frame.Fail();
  }
  if (CtHasCityWindow())
    CtGetCityWindow()->Save(this, segment);
  if (scenario && scenario->IsScenarioRunning())
    scenario->SaveUncompressedScenarioRelatedRecords(segment);
  if (file) {
    RsKey key = {0, 0, 0};
    file->StoreRecords(&key, kCtSaveLayerInitialCapacity, kCtSaveLayerGrowth);
  }
  for (u32 i = 0; i < layers.Count(); ++i) {
    if (progress)
      progress->Step();
    if (!layers.begin[i]->Save(this, segment))
      result = false;
  }
  cityType = 3;
  SimHiddenUnpause();
  ScRelease(&file);
  ScDelete(&progress);
  return result;
}

// Save replacement data before opening the scheme-selection record, then write building, flora and landscape in order.
// This is 10004CCD's full member phase; false getters write zero and only the last raw stream result is returned.
u8 CtCityImpl::WriteScheme(RsDatabaseSegment *segment) {
  CtSchemeManager *scheme = CtGetGameApp()->CitySchemeManager();
  if (!scheme)
    return 0;
  scheme->SaveBATReplacementSet(segment);
  RsRecord *record = nullptr;
  if (!segment->OpenRecordForWrite({kCtCitySchemeRecordType, kCtCitySchemeRecordGroup, 1}, &record))
    return 0;
  RsStream *stream = nullptr;
  u8 result = 0;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    u32 buildingSet = 0, floraSet = 0, landscape = 0;
    if (!scheme->GetCurrentBuildingSet(&buildingSet))
      buildingSet = 0;
    stream->WriteU32(buildingSet);
    if (!scheme->GetCurrentFloraSet(&floraSet))
      floraSet = 0;
    stream->WriteU32(floraSet);
    if (!scheme->GetCurrentLandScape(&landscape))
      landscape = 0;
    result = stream->WriteU32(landscape);
    stream->Release();
    stream = nullptr;
  }
  segment->CloseRecord(&record);
  return result;
}

class CtShutdownLock {
public:
  // Retain the actual city critical section across provider cleanup and notification removal.
  explicit CtShutdownLock(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the same critical section on ordinary return or provider callback unwinding.
  ~CtShutdownLock() {
    lock->Unlock();
  }

  ScLock *lock;
};

// Stop an initialized city, detach its layers and managers in original ownership order, then remove notifications.
bool CtCityImpl::Shutdown() {
  if (!initialized)
    return true;
  SimHiddenPause();
  for (u32 i = 0; i < dialogJobs.Count(); ++i) {
    ScMessage message = {kApMsgDismissNotification, dialogJobs.begin[i], 0, 0};
    CtGetMessageServer()->MessagePost(message, false);
  }
  dialogJobs.end = dialogJobs.begin;
  initialized = false;
  if (simulator)
    simulator->CommandStop(nullptr);
  CtShutdownLock guard(&cityLock);
  CtLayer **first = layers.begin, **last = layers.end;
  while (last != first) {
    --last;
    if (*last)
      (*last)->Shutdown();
  }
  if (surfaceOccupants)
    surfaceOccupants->Shutdown();
  if (undergroundOccupantsOne)
    undergroundOccupantsOne->Shutdown();
  if (undergroundOccupantsTwo)
    undergroundOccupantsTwo->Shutdown();
  if (animationOccupants)
    animationOccupants->Shutdown();
  CtAgentTreeIterator *iterator = nullptr;
  CtGetAgentTree()->CreateNodeIterator(&iterator);
  while (CtAgentType *type = iterator->Next())
    type->ReleaseAllManipulators();
  iterator->Release();
  if (simulator)
    simulator->Shutdown();
  if (clock)
    clock->Shutdown();
  ReleaseLayers();
  ScRelease(&surfaceOccupants);
  ScRelease(&undergroundOccupantsOne);
  ScRelease(&undergroundOccupantsTwo);
  ScRelease(&animationOccupants);
  ScRelease(&simulator);
  if (clock) {
    clock->Shutdown();
    clock->Release();
    clock = nullptr;
  }
  for (u32 i = 0; i < 4; ++i)
    savedVersion[i] = 0;
  const u32 notifications[] = {kCtMsg_SaveToSegment, kCtMsg_SetGameSpeed, kCtMsg_RequestPause, kCtMsg_RequestUnpause,
      kCtMsg_DisasterStarted, kCtMsg_DisasterEnded, kCtMsg_RequestNewCity, kCtMsg_RequestLoadCity,
      kCtMsg_RequestSaveCity, kCtMsg_RequestQuitGame, kWnMsg_EnterTerrainEditor, kWnMsg_EnterCity,
      kCtMsg_SetSimulationControlsEnabled};
  for (u32 id : notifications)
    CtGetMessageServer()->RemoveNotification(this, id);
  return true;
}

// Pause an active city, detach its retained agent, end layers in reverse order, then stop the simulator and clear activity.
bool CtCityImpl::EndCitySimulation() {
  if (!simulationStarted)
    return true;
  SimHiddenPause();
  if (field296) {
    CitySimulator()->RemoveAgent(field296);
    field296->Release();
    field296 = nullptr;
  }
  CtLayer **begin = layers.begin, **end = layers.end;
  while (end != begin) {
    --end;
    if (*end)
      (*end)->SimulationEnd();
  }
  bool result = CitySimulator()->CommandStop(nullptr);
  simulationStarted = false;
  return result;
}

// Rebuild startup dialogs, attach the city's agent and begin the captured layer roster before starting its simulator.
// Preserve ignored layer predicates, live pending entries, dialog range boundaries and the independent started flag.
bool CtCityImpl::StartCitySimulation() {
  CtDialogJobs prior(dialogJobs);
  // Stock clears the live list before its notification loop; the retained snapshot is never used for posting.
  dialogJobs.end = dialogJobs.begin;
  u32 current = 0, last = 0;
  if (cityType == 3) {
    ScRandom random(kScRandomSeedFromClock);
    CtRequestDialog((u32)random.Range(749, 754));
  }
  if (cityType == 7) {
    for (u32 i = 100; i <= 104; ++i) {
      u32 identifier = CtRequestDialog(i);
      if (identifier)
        dialogJobs.Append(identifier);
    }
    current = 105;
    last = 114;
  } else {
    u32 identifier = CtRequestDialog(761);
    if (identifier)
      dialogJobs.Append(identifier);
  }
  CtAgentTree *tree = CtGetAgentTree();
  if (!tree)
    return false;
  CtAgentType *type = tree->GetNode(kCtAgentType_AreaAgent);
  if (!type || !CtGetCom()->GetClassObject(kCtAgentClassId, IID_CtAgent, reinterpret_cast<void **>(&field296)) ||
      !field296)
    return false;
  field296->SetAgentType(type);
  bool result = CitySimulator()->AddAgent(field296);
  ScUnknown *system = nullptr;
  for (CtLayer **entry = layers.begin, **end = layers.end; entry != end; ++entry) {
    if (progress)
      progress->Step();
    if (*entry) {
      (*entry)->SimulationBegin();
      if ((*entry)->QueryInterface(IID_AdAdvisor, reinterpret_cast<void **>(&system))) {
        if (current < last) {
          u32 identifier = CtRequestDialog(current);
          if (identifier)
            dialogJobs.Append(identifier);
        }
        ++current;
        system->Release();
      }
    }
  }
  for (u32 i = current; i <= last; ++i) {
    u32 identifier = CtRequestDialog(i);
    if (identifier)
      dialogJobs.Append(identifier);
  }
  if (result)
    result = CitySimulator()->CommandStart(nullptr);
  simulationStarted = true;
  return result;
}

class CtCityGuard {
public:
  // Enter the original scoped city lock, retaining its actual recursive receiver.
  explicit CtCityGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the scoped lock during both ordinary return and original callback unwinding.
  ~CtCityGuard() {
    lock->Unlock();
  }

  ScLock *lock;
};

static const f32 CtFeetPerWorld = 4.0f * 3.281f;

i32 CtCityImpl::GetEditMode() {
  stateLock.Lock();
  i32 result = editMode;
  stateLock.Unlock();
  return result;
}

// Publish the actual edit mode while retaining the original recursive lock boundary.
void CtCityImpl::SetEditMode(i32 mode) {
  stateLock.Lock();
  editMode = mode;
  stateLock.Unlock();
}

// Read the disaster bit under the original state lock.
bool CtCityImpl::IsDisasterActive() {
  stateLock.Lock();
  bool result = (modeFlags & kCtModeDisaster) != 0;
  stateLock.Unlock();
  return result;
}

bool CtCityImpl::IsScenarioActive() {
  return (modeFlags & kCtModeScenario) != 0;
}

bool CtCityImpl::IsTutorialActive() {
  return (modeFlags & kCtModeTutorial) != 0;
}

bool CtCityImpl::IsDemoActive() {
  return (modeFlags & kCtModeDemo) != 0;
}

bool CtCityImpl::AreHolidayFeaturesEnabled() {
  return holidayFeatures;
}

// Require every requested mode bit while preserving the state lock and pre-unlock result capture.
bool CtCityImpl::IsModeFlagActive(u32 flags) {
  stateLock.Lock();
  bool result = (modeFlags & flags) == flags;
  stateLock.Unlock();
  return result;
}
