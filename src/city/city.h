#ifndef SC3_CITY_CITY_H
#define SC3_CITY_CITY_H

#include "city/api.h"
#include "city/ct_date.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "occupants/api.h"

class CtAgentType;
class RsStream;

// Native save-boundary helpers retain legacy file bytes while decoded game text uses UTF-8.
bool CtReadSavedText(RsStream *stream, ScStringView *text, u32 language);
bool CtWriteSavedText(RsStream *stream, const ScStringView *text, u32 language);

// Reached cISC3AgentTypeTreeIterator prefix from its installed SIMINIT producer and city shutdown caller.
// Next is descriptive; the missing nominal source declaration remains unresolved. It returns a borrowed type.
class CtAgentTreeIterator : public ScUnknown {
public:
  virtual CtAgentType *Next() = 0; // Original +16, no stack arguments; no additional retain.
};

struct ScMessage;
class CtLayer;

// Reached city enumeration interface, preserving the producer's primary Unknown prefix and actual callback arguments.
// SIMUI table100AADC8/QI10062167 accepts Unknown and callback10063376 consumes message plus common layer.
// Enumerate is a descriptive native method name; the pinned SDK only forward-declares cISC3CityEnumerator.
class CtCityEnumerator : public ScUnknown {
public:
  virtual bool Enumerate(ScMessage *message, CtLayer *layer) = 0;
};

struct ScMessage;
class CtLayer;
class CtDefinition;
class CtNewCityInfo;
class ScImporter;
class CtAgent;
class OcAnimManager;
class AdAdvisor;
class AdUtilities;
class AdDemographics;
class AdPublicSafety;
class AdEnvironment;
class AdCityPlanning;
class AdTransportation;
class AdBudget;
class CtProgress;
class TrGeneratorInterface;
class GeFlora;
class RsDatabaseSegment;

// Dedicated storage of retained city-layer interfaces; shutdown owns the separate interface releases.
class CtLayerStorage {
public:
  CtLayerStorage();
  ~CtLayerStorage();
  u32 Count() const;
  void Append(CtLayer *layer);
  CtLayer **begin, **end, **storageEnd;
};

// Original three-pointer storage of pending dialog identifiers, distinct from interface ownership.
class CtDialogJobs {
public:
  CtDialogJobs();
  CtDialogJobs(const CtDialogJobs &source);
  ~CtDialogJobs();
  void Append(u32 identifier);
  u32 Count() const;
  u32 *begin, *end, *storageEnd;
};

// The installed receiver exposes its city/message prefix plus a distinct secondary occupant-factory interface.
// Recovered methods use actual fields and typed services; foreign implementations remain independent dependencies.
class CtCityImpl : public CtCity, public OcFactory {
public:
  CtCityImpl();
  virtual ~CtCityImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateOccupant(RsKey *key, u32 iid, void **out) override;
  bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) override;
  virtual bool Shutdown() override;
  virtual bool Init(RsDatabaseSegment *segment) override;
  virtual bool Init(CtNewCityInfo *info) override;
  virtual bool Init(ScImporter *importer) override;
  virtual bool StaticInit(CtDefinition *definition) override;
  virtual bool StaticShutdown() override;
  void AcquireLayers();
  void InitLayers();
  void ReleaseLayers();
  bool InitCommon(u32 countX, u32 countY, u32 countZ, TrGeneratorInterface *terrainGenerator);
  u32 ReadIdentity(RsDatabaseSegment *segment);
  u8 ReadDescription(RsDatabaseSegment *segment);
  u8 WriteCityType(RsDatabaseSegment *segment);
  u8 WriteIdentity(RsDatabaseSegment *segment);
  u8 WriteDescription(RsDatabaseSegment *segment);
  u8 WriteDescriptor(RsDatabaseSegment *segment);
  u8 WriteScheme(RsDatabaseSegment *segment);
  bool DoMessage(ScMessage *message) override;
  bool Save(RsDatabaseSegment *segment) override;
  virtual bool DoNewCity(bool query) override;
  virtual bool DoLoadCity(bool query) override;
  virtual bool DoSaveCity(bool query) override;
  virtual void DoQuitGame(bool query) override;
  virtual bool StartCitySimulation() override;
  virtual bool EndCitySimulation() override;
  virtual bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool RemoveAllOccupants() override;
  CtLayer *GetSpecificLayer(u32 type) override;
  virtual bool GetLayer(u32 type, u32 iid, void **out) override;
  virtual u32 NumberOfLayers() override;
  bool DoEnumLayers(ScMessage *message, CtCityEnumerator *enumerator) override;
  virtual bool GetLayerByIndex(u32 index, u32 iid, void **out) override;
  virtual u32 GetLayerList(u32 capacity, CtLayer **out) override;
  virtual u32 GetAdvisorListLength() override;
  virtual u32 GetAdvisorList(u32 capacity, AdAdvisor **out) override;
  virtual bool GetAdvisor(u32 id, AdAdvisor **out) override;
  virtual bool SetGameSpeed(i32 speed) override;
  virtual u32 Pause() override;
  virtual u32 Unpause() override;
  virtual void SetDisasterActive(bool active) override;
  virtual void SetTutorialActive(bool active) override;
  virtual void SetDemoActive(bool active) override;
  virtual void EnableHolidayFeatures(bool enabled) override;
  void SetScenarioActive(bool active);
  virtual TrGeneratorInterface *DirtGenerator() override;
  virtual GeFlora *FloraLayer() override;
  EcBudgetLayer *BudgetLayer() override;
  CtOrdinanceLayer *OrdinanceLayer() override;
  CtZoneLayer *ZoneLayer() override;
  NwTransit *TransitLayer() override;
  CtPowerLayer *PowerLayer() override;
  CtPlumbingLayer *PlumbingLayer() override;
  GeDemolitionView *DemolitionLayer() override;
  DsLayer *DisasterLayer() override;
  CtWorldLayer *WorldLayer() override;
  CtStartSimLayer *StrtSimLayer() override;
  CtCrimeLayer *CrimeLayer() override;
  CtPoliceLayer *PoliceLayer() override;
  CtFireLayer *FireLayer() override;
  EcCommercialLayer *CommercialLayer() override;
  EcIndustrialLayer *IndustrialLayer() override;
  CtLandValueLayer *LandValueLayer() override;
  EnWeatherLayer *WeatherLayer() override;
  virtual i32 GetGameSpeed() override;
  virtual i32 GetEditMode() override;
  virtual void SetEditMode(i32 mode) override;
  virtual bool IsDisasterActive() override;
  virtual bool IsScenarioActive() override;
  virtual bool IsTutorialActive() override;
  virtual bool IsDemoActive() override;
  bool AreHolidayFeaturesEnabled() override;
  virtual bool IsModeFlagActive(u32 flags) override;
  virtual void SetCityModeFlag(u32 flags) override;
  virtual void ClearCityModeFlag(u32 flags) override;
  virtual u32 GetSerialNumber() override;
  virtual u32 GetCityType() override;
  virtual u32 GetCityID() override;
  virtual i32 GetDifficulty() override;
  virtual bool IsAutoBudgetEnabled() const override;
  virtual bool AreDisastersEnabled() const override;
  virtual bool CanPlayerPauseUnpause() const override;
  virtual bool CanPlayerChangeSpeed() const override;
  virtual bool CanPlayerLoadSave() const override;
  virtual bool IsWriteCitySaveFileDebug() const override;
  virtual bool IsReadCitySaveFileDebug() const override;
  virtual void SetSerialNumber(u32 value) override;
  virtual void SetDifficulty(i32 value) override;
  virtual void SetPlayerLoadSaveEnabled(bool enabled) override;
  virtual bool GetOriginalLanguageAndCountry(i32 *language, i32 *country) override;
  virtual bool GetLastLanguageAndCountry(i32 *language, i32 *country) override;
  virtual ScStringView *GetCityName() override;
  virtual ScStringView *GetCityDescription() override;
  virtual ScStringView *GetMayorName() override;
  virtual void SetCityName(const ScStringView &text) override;
  virtual void SetCityDescription(const ScStringView &text) override;
  virtual void SetMayorName(const ScStringView &text) override;
  virtual void SetAutoBudgetEnabled(bool enabled) override;
  virtual void SetDisastersEnabled(bool enabled) override;
  u32 CellCountX() const override;
  virtual u32 AnimCellCountX() const override;
  virtual u32 CellSizeInAnimUnitsX() const override;
  u32 CellCountZ() const override;
  virtual u32 AnimCellCountZ() const override;
  virtual u32 CellSizeInAnimUnitsZ() const override;
  virtual u32 CellCountY() const override;
  virtual u32 AnimCellCountY() const override;
  virtual u32 CellSizeInAnimUnitsY() const override;
  virtual i64 GetTileSize() override;
  virtual f32 CellSizeInWorldUnitsX() const override;
  virtual f32 CellSizeInWorldUnitsZ() const override;
  virtual f32 CellSizeInWorldUnitsY() const override;
  virtual f32 SizeX() override;
  virtual f32 SizeZ() override;
  virtual f32 SizeY() override;
  f32 WorldToMeter(f32 value) const override;
  virtual f32 WorldToFeet(f32 value) const override;
  virtual f32 MeterToWorld(f32 value) const override;
  virtual f32 FeetToWorld(f32 value) const override;
  ScOccupantManager *SurfaceOccupantManager() override;
  ScOccupantManager *UndergroundOccupantManagerLevelOne() override;
  ScOccupantManager *UndergroundOccupantManagerLevelTwo() override;
  GeBuildingLayer *BuildingLayer() override;
  ScDirtBag *DirtBag() override;
  virtual OcAnimManager *AnimOccupantManager() override;
  NwTrafficLayer *TrafficLayer() override;
  CtResidentialLayer *ResidentialLayer() override;
  EcLayer *PollutionLayer() override;
  CtNeighborsLayer *NeighborsLayer() override;
  virtual AdUtilities *GetUtilityAdvisor() override;
  virtual AdDemographics *GetDemographicsAdvisor() override;
  virtual AdPublicSafety *GetPublicSafetyAdvisor() override;
  virtual AdEnvironment *GetEnvironmentAdvisor() override;
  virtual AdCityPlanning *GetCityPlanningAdvisor() override;
  virtual AdTransportation *GetTransportationAdvisor() override;
  virtual AdBudget *GetBudgetAdvisor() override;
  AdPetitionerManager *GetPetitionerManager() override;
  CtSimulatorInterface *CitySimulator() override;
  CtClockInterface *GetSimulationClock() override;
  void GetDate(u32 *month, u32 *day, u32 *year) override;
  virtual void GetDate(u32 *day, u32 *year) override;
  virtual bool SetDate(u32 month, u32 day, u32 year) override;
  virtual bool SetDate(u32 day, u32 year) override;
  virtual u32 GetYearBorn() override;
  virtual bool SetYearBorn(u32 year) override;
  void SimCalendarNotification(i32 phase) override;
  u32 SimHiddenPause() override;
  u32 SimHiddenUnpause() override;
  virtual void SetTipOfTheDayNeeded(bool needed) override;
  virtual bool IsTipOfTheDayNeeded() override;
  virtual bool IsPaused() override;
  virtual u32 PauseCount() override;
  virtual u32 SimHiddenPauseCount() override;
  u32 TotalPauseCount() override;
  u32 HiddenClockPause() override;
  u32 HiddenClockUnpause() override;
  void Lock() override;
  void Unlock() override;
  void LockGraphics() override;
  void UnlockGraphics() override;
  virtual ScLock *GetCriticalSection() override;
  virtual ScLock *GetGraphicsCriticalSection() override;
  virtual void OnIdle() override;
  virtual bool IsFileVersionMoreRecentThan(i32 major, i32 minor, i32 patch) override;
  virtual bool CanCitySave() const override;
  virtual ScStringView *GetFilePath() override;
  virtual void SetFilePath(const ScStringView &path) override;
  u32 GetVersionComponent(u8 component) const override;
  u32 GetVersionComponentFromSaveFile(u8 component) const override;
  CtScenarioLayer *ScenarioLayer() override;
  u32 cityType, field12, serialNumber;
  i32 originalLanguage, originalCountry, lastLanguage, lastCountry;
  u32 cityID;
  bool initialized;
  u32 references;
  i32 difficulty, editMode;
  u32 modeFlags, cellsX, cellsZ, cellsY;
  ScString cityName, mayorName, description;
  ScOccupantManager *surfaceOccupants, *undergroundOccupantsOne, *undergroundOccupantsTwo;
  OcAnimManager *animationOccupants;
  CtLayerStorage layers;
  GeBuildingLayer *buildings;
  ScDirtBag *dirt;
  NwTransit *transit;
  NwTrafficLayer *traffic;
  GeFlora *flora;
  CtZoneLayer *zones;
  CtPowerLayer *power;
  CtPlumbingLayer *plumbing;
  CtOrdinanceLayer *ordinances;
  EcBudgetLayer *budget;
  CtWorldLayer *world;
  CtStartSimLayer *streetSimulation;
  DsLayer *disasters;
  EcLayer *pollution;
  CtCrimeLayer *crime;
  CtPoliceLayer *police;
  CtFireLayer *fire;
  CtResidentialLayer *residential;
  EcCommercialLayer *commercial;
  EcIndustrialLayer *industrial;
  CtLandValueLayer *landValue;
  CtNeighborsLayer *neighbors;
  EnWeatherLayer *weather;
  GeDemolitionView *demolition;
  CtScenarioLayer *scenario;
  AdUtilities *utilityAdvisor;
  AdBudget *budgetAdvisor;
  AdCityPlanning *planningAdvisor;
  AdEnvironment *environmentAdvisor;
  AdDemographics *demographicsAdvisor;
  AdPublicSafety *publicSafetyAdvisor;
  AdTransportation *transportationAdvisor;
  AdPetitionerManager *petitioner;
  CtSimulatorInterface *simulator;
  CtAgent *field296;
  bool simulationStarted;
  CtClockInterface *clock;
  CtDate date;
  u32 yearBorn, pauseCount, hiddenPauseCount, clockPauseCount;
  i32 speed, previousSpeed;
  bool canPause, canChangeSpeed, canLoadSave;
  TrGeneratorInterface *generator;
  ScString filePath;
  u32 version[4], savedVersion[4];
  bool writeSaveDebug, readSaveDebug;
  u32 field404;
  CtDialogJobs dialogJobs;
  CtProgress *progress;
  ScLock cityLock, graphicsLock;
  bool tipOfTheDayNeeded, autoBudget, disastersEnabled, holidayFeatures;
  ScLock stateLock;
};

class RsDatabaseSegment;
class GxBuffer;

// Original city descriptor receiver owns its text, date and retained preview independently of the city.
// Writable destruction serializes metadata and the preview before releasing its owned fields.
class CtCityInfo {
public:
  CtCityInfo(RsDatabaseSegment *source, u32 kind);
  CtCityInfo(const ScStringView &path, u32 kind, u32 width, u32 height);
  ~CtCityInfo();
  void SetPopulation(u32 value);
  void SetFunds(i64 value);
  void SetDate(CtDate value);
  void SetMayorName(ScString value);
  void SetCityName(ScString value);
  void SetPreview(GxBuffer *buffer);
  // The nominal return is unknown; this exposes the observed register while callers use the retained output.
  uptr GetPreview(GxBuffer **out);
  bool reading, ownsSegment;
  u8 readValid;
  u32 kind;
  RsDatabaseSegment *segment;
  ScString mayorName, cityName;
  i64 funds;
  CtDate date;
  u32 population;
  GxBuffer *preview;
  GxBuffer *sourceImage;
  ScString field84, field104;
};

class RsFileInterface;
class ScStringView;

// Retail cISC2Importer::tMicroRec stores two byte fields and three 16-bit fields.
struct CtMicroRecord {
  u8 type, stat0;
  u16 stat1, stat2, stat3;
};

// Retail cISC2Importer::tThingRec stores its twelve record fields as bytes.
struct CtThingRecord {
  u8 type, direction, state, x, y, z, px, py, dx, dy, label, goal;
};

static_assert(sizeof(CtMicroRecord) == 8);
static_assert(sizeof(CtThingRecord) == 12);

// Legacy city chunk identities use the exact four-character labels read from the file; FORM and SCDH frame the stream.
enum CtLegacyChunkTag : u32 {
  kCtLegacyFormTag = 0x464f524d,
  kCtLegacyScdhFormType = 0x53434448,
  kCtLegacyMiscChunkTag = 0x4d495343,
  kCtLegacyXterChunkTag = 0x58544552,
  kCtLegacyXbldChunkTag = 0x58424c44,
  kCtLegacyXzonChunkTag = 0x585a4f4e,
  kCtLegacyXundChunkTag = 0x58554e44,
  kCtLegacyXtxtChunkTag = 0x58545854,
  kCtLegacyXbitChunkTag = 0x58424954,
  kCtLegacyXtrfChunkTag = 0x58545246,
  kCtLegacyXpltChunkTag = 0x58504c54,
  kCtLegacyXvalChunkTag = 0x5856414c,
  kCtLegacyXcrmChunkTag = 0x5843524d,
  kCtLegacyXplcChunkTag = 0x58504c43,
  kCtLegacyXfirChunkTag = 0x58464952,
  kCtLegacyXpopChunkTag = 0x58504f50,
  kCtLegacyXrogChunkTag = 0x58524f47,
  kCtLegacyXmicChunkTag = 0x584d4943,
  kCtLegacyXthgChunkTag = 0x58544847,
  kCtLegacyXlabChunkTag = 0x584c4142,
  kCtLegacyXgrpChunkTag = 0x58475250,
  kCtLegacyAltmChunkTag = 0x414c544d,
  kCtLegacyCnamChunkTag = 0x434e414d,
};

// Plane-presence bits identify the same decoded chunks used by the importer getters and local callers.
enum CtLegacyPlane : u32 {
  kCtLegacyXterPlane = 1,
  kCtLegacyXbldPlane = 2,
  kCtLegacyXzonPlane = 4,
  kCtLegacyXundPlane = 8,
  kCtLegacyXtxtPlane = 16,
  kCtLegacyXbitPlane = 32,
  kCtLegacyXtrfPlane = 64,
  kCtLegacyXpltPlane = 128,
  kCtLegacyXvalPlane = 0x100,
  kCtLegacyXcrmPlane = 0x200,
  kCtLegacyXplcPlane = 0x400,
  kCtLegacyXfirPlane = 0x800,
  kCtLegacyXpopPlane = 0x1000,
  kCtLegacyXrogPlane = 0x2000,
  kCtLegacyAltmPlane = 0x4000,
  kCtLegacyCnamPlane = 0x8000,
  kCtLegacyXlabPlane = 0x10000,
  kCtLegacyXmicPlane = 0x20000,
  kCtLegacyXthgPlane = 0x40000,
  kCtLegacyXgrpPlane = 0x80000,
  kCtLegacyMiscPlane = 0x100000,
};

// Recovered city-data fields and reader methods of SIMINIT's legacy importer.
class CtLegacyImporter : public ScImporter {
public:
  CtLegacyImporter();
  virtual ~CtLegacyImporter();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(const ScStringView &path) override;
  u8 GetBaseMapSize(u32 *countX, u32 *countZ, u32 *countY) override;
  u8 MapIsPresent(u32 plane) override;
  u8 GetValue(i32 plane, u32 x, u32 z, u8 *cell) override;
  u8 GetZoneValue(u32 x, u32 z, u8 *zone) override;
  u8 GetFaceValue(u32 x, u32 z, u8 *direction) override;
  u16 GetMapSize(i32 plane) override;
  u8 GetMiscValue(u32 index, i32 *value) override;
  u8 GetAltValue(u32 x, u32 z, u16 *out) override;
  u8 GetTerrainAlt(u32 x, u32 z, u8 *altitude) override;
  u8 GetWaterAlt(u32 x, u32 z, u8 *out) override;
  u8 GetTunnelDepth(u32 x, u32 z, u8 *out) override;
  u8 GetCityName(char *out) override;
  u8 GetLabel(u32 index, char *out) override;
  u8 GetMicroRecord(u32 index, CtMicroRecord *out) override;
  u8 GetThingRecord(u32 index, CtThingRecord *out) override;
  // Some categories select earlier receiver fields; samples outside the original receiver are invalid.
  u8 GetGraphValue(i32 category, u8 scale, u8 index, u32 *out) override;

protected:
  bool ReadChunks(RsFileInterface *file);
  u32 references = 0;
  u32 loadedPlanes = 0;
  u16 height[128 * 128] = {};
  u8 largePlanes[6][128 * 128] = {};
  u8 mediumPlanes[4][64 * 64] = {};
  u8 smallPlanes[4][32 * 32] = {};
  u8 cityNameData[32] = {};
  u32 properties[1200] = {};
  u8 micData[1200] = {}, thingData[480] = {}, labelData[6400] = {}, groupData[3328] = {};
};

class ScStringView;
class TrGeneratorInterface;

// Reached installed new-city settings interface, IID43271E76; SIMINIT table1002F940 supplies these borrowed getters.
// Original private getter names and nominal byte/bool spelling are unavailable; this native subset is not a vtable overlay.
class CtNewCityInfo : public ScUnknown {
public:
  virtual ScStringView *GetCityName() = 0;                 // +68,1000C0FC: embedded String+8.
  virtual ScStringView *GetMayorName() = 0;                // +72,1000C100: embedded String+28.
  virtual i64 GetInitialFunds() = 0;                       // +76,1000C104: paired64-bit field+48.
  virtual u8 GetLoanByte() = 0;                            // +80,1000C10B: raw byte+56, city tests exactly1.
  virtual u32 GetCellCountX() = 0;                         // +84,1000C10F: field+60.
  virtual u32 GetCellCountY() = 0;                         // +88,1000C113: field+64.
  virtual u32 GetCellCountZ() = 0;                         // +92,1000C117: field+68.
  virtual u32 GetStartYear() = 0;                          // +96,1000C11B: field+72.
  virtual i32 GetDifficulty() = 0;                         // +100,1000C11F: field+84.
  virtual TrGeneratorInterface *GetTerrainGenerator() = 0; // +104,1000C123: borrowed field+88.
  virtual u32 GetCityType() = 0;                           // +108,1000C127: field+76.
  virtual u8 GetAutoBudgetByte() = 0;                      // +112,1000C12B: byte+80, flows to city's bool setter.
  virtual u8 GetDisastersByte() = 0;                       // +116,1000C12F: byte+81, flows to city's bool setter.
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class WnWindow;
class RsDatabaseSegment;

// Reached cISCNScenarioLayer IID83DE4D37 primary methods, verified against SCENARIO's actual producer.
// Native order is a subset, not a foreign-vtable overlay; the common CtLayer identity is a distinct secondary interface.
class CtScenarioLayer : public ScUnknown {
public:
  virtual bool CreateProgressDlgWin(WnWindow **out, bool flag) = 0;                    // SDK :32; original +12.
  virtual bool IsScenarioRunning() = 0;                                                // SDK :33; original +16.
  virtual bool SaveUncompressedScenarioRelatedRecords(RsDatabaseSegment *segment) = 0; // SDK :34; original +20.

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual void GetScenarioFilePath(ScStringView *) = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3WorldLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScGraphInfoView;

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class CtWorldLayer : public ScUnknown {

public:
  virtual CtLayer *AsISC3CityLayer() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;

  virtual u32 GetWorldPopulation() = 0;
  virtual u32 GetGlobalSeaLevel() = 0;
  virtual ScGraphInfoView *GetUnemploymentHistory() = 0;
};

// SIMINIT's new-city settings receiver. The shared native interface omits the unrecovered setter slots.
class CtNewCityInfoImpl : public CtNewCityInfo {
public:
  CtNewCityInfoImpl();
  virtual ~CtNewCityInfoImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual void SetCityName(ScStringView *name);
  virtual void SetMayorName(ScStringView *name);
  virtual void SetFunds(i64 funds);
  virtual void SetFundsAreDebt(u8 debt);
  virtual void SetCitySize(u32 size);
  virtual void SetCityXSize(u32 size);
  virtual void SetCityYSize(u32 size);
  virtual void SetCityZSize(u32 size);
  virtual void SetStartYear(u32 year);
  virtual void SetDifficulty(i32 value);
  virtual void SetDirtGenerator(TrGeneratorInterface *value);
  virtual void SetCityType(i32 value);
  virtual void SetAutoBudgetEnabled(u8 enabled);
  virtual void SetDisastersEnabled(u8 enabled);
  ScStringView *GetCityName() override;
  ScStringView *GetMayorName() override;
  i64 GetInitialFunds() override;
  u8 GetLoanByte() override;
  u32 GetCellCountX() override;
  u32 GetCellCountY() override;
  u32 GetCellCountZ() override;
  u32 GetStartYear() override;
  i32 GetDifficulty() override;
  TrGeneratorInterface *GetTerrainGenerator() override;
  u32 GetCityType() override;
  u8 GetAutoBudgetByte() override;
  u8 GetDisastersByte() override;

private:
  u32 references;
  ScString cityName, mayorName;
  i64 initialFunds;
  u8 loanByte;
  u32 countX, countY, countZ, startYear, cityType;
  u8 autoBudgetByte, disastersByte;
  i32 difficulty;
  TrGeneratorInterface *generator;
};

#endif
