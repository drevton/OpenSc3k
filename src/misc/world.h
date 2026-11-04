#ifndef SC3_MISC_WORLD_H
#define SC3_MISC_WORLD_H

#include <map>
#include <vector>

#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/sc_random.h"
#include "city/sc_stopwatch.h"
#include "city/simulation.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "economy/api.h"
#include "environment/api.h"
#include "misc/module.h"

class EcValve;

// Original eight-byte era effect stores the narrowed month count and its independently rounded float multiplier.
struct CtEraEffect {
  u16 months;
  f32 factor;
};

// Original28-byte valve schedule owns its effect vector; configuration leaves the runtime valve pointer unbound.
struct CtValveEra {
  u32 identifier;
  EcValve *valve;
  u8 current_era;
  u16 remaining_months;
  u32 period;
  std::vector<CtEraEffect> effects;
};

// Original phase entries store a population threshold and commercial/industrial workforce distribution ratios.
struct CtPhaseEffect {
  u64 population;
  u32 commercial_ratio, industrial_ratio;
};

extern std::vector<CtValveEra> CtWorldDefaultValves;
extern std::map<u32, CtPhaseEffect> CtWorldPhases;
extern u8 CtWorldJobToResidential;
extern u32 CtWorldExternalJobDemand;
extern ScStopwatch CtWorldTimer;

static_assert(sizeof(CtEraEffect) == 8);
static_assert(sizeof(CtPhaseEffect) == 16);

// SIMMISC links a separate concrete valve with the same public interface and original field layout.
// Its module references and notification service stay local rather than binding SIMECO's implementation.
class EcMiscValve : public EcValve {
public:
  EcMiscValve();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Init(CtCity *city, u32 identifier, CtLayer *listener) override;
  virtual bool Shutdown() override;
  virtual bool SimulationBegin() override;
  virtual bool SerialRead(CtCity *city, RsStream *record) override;
  virtual u8 SerialWrite(CtCity *city, RsStream *record) const override;
  virtual u32 GetId() override;
  virtual void SetId(u32 identifier) override;
  virtual i32 QuerySupplyValue() override;
  virtual i32 QueryDemandValue() override;
  virtual i32 QueryValveValue() override;
  virtual f32 QueryEconomyModifier() override;
  virtual i32 QueryValveMax() override;
  virtual i32 QueryValveMin() override;
  virtual bool AddToSupplyValue(i32 amount) override;
  virtual bool AddToDemandValue(i32 amount) override;
  virtual bool SetSupplyValue(i32 amount) override;
  virtual bool SetDemandValue(i32 amount) override;
  virtual void SetValveMax(i32 maximum) override;
  virtual void SetValveMin(i32 minimum) override;
  virtual void SetInitialSupplyValue(i32 amount) override;
  virtual void SetInitialDemandValue(i32 amount) override;
  virtual f32 GetEconomyModifier() override;
  virtual void SetEconomyModifier(f32 modifier) override;
  virtual f32 GetTaxModifier() override;
  virtual void SetTaxModifier(f32 modifier) override;
  virtual u8 GetDemandCap() override;
  virtual void SetDemandCap(u8 cap) override;
  virtual bool EndOfMonth() override;
  virtual void DebugLockValue(i32 value) override;
  virtual void DebugUnlockValue() override;
  virtual bool DebugIsValueLocked() override;
  virtual ~EcMiscValve();

protected:
  // Native effects of18381; its private nominal return and smoothing-argument declaration remain unresolved.
  virtual void UpdateValue(u8 smooth);
  void NotifyValue() const;
  u32 references, identifier;                 // Original +04,+08.
  i32 supply, demand;                         // +0C,+10.
  u32 supply_accumulator, demand_accumulator; // +14,+18.
  i32 initial_supply, initial_demand;         // +1C,+20.
  i32 maximum, minimum, balance;              // +24,+28,+2C.
  bool flag_30, flag_31;                      // Original constructor initializes both true.
  f32 economy_modifier, tax_modifier;         // +34,+38.
  u8 demand_cap;                              // +3C.
  CtCity *city;                               // +40, borrowed until Shutdown.
};

class EnWeather;

// Actual retained-owner manipulator drives weather at its scheduled agent phase without locking the city.
class EnWeatherManipulator : public CtLayerManipulator, public ScMiscRefCount {
public:
  explicit EnWeatherManipulator(EnWeather *weather);
  ~EnWeatherManipulator() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool Shutdown();

protected:
  EnWeather *weather;
};

// Actual weather receiver owns its retained city, monthly manipulator, random distributions and seconds timer.
class EnWeather : public EnWeatherLayer, public ScMiscRefCount {
public:
  EnWeather();
  ~EnWeather() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  void GetPosition(i32 *longitude, i32 *latitude) override;
  void SetPosition(i32 longitude, i32 latitude) override;
  EnHemisphere GetHemisphere() override;
  EnSeason GetCurrentSeason() override;
  i32 GetCurrentSeasonMonth() override;
  void GetWeather(EnWeatherType *type, i32 *intensity, CtDate *date) override;
  void SetWeather(EnWeatherType type, i32 intensity) override;
  f32 FarenheitToCelsius(f32 farenheit) override;
  f32 CelsiusToFarenheit(f32 celsius) override;
  f32 CelsiusToKelvin(f32 celsius) override;
  f32 KelvinToCelsius(f32 kelvin) override;
  f32 GetAverageTemperature(bool farenheit, CtDate *date) override;
  f32 GetAverageHighTemperature(bool farenheit, CtDate *date) override;
  f32 GetAverageLowTemperature(bool farenheit, CtDate *date) override;
  void GetAverageTemperaturePerSeasonPerLocation(f32 *low, f32 *low_min, f32 *low_max, f32 *average, f32 *average_min,
      f32 *average_max, f32 *high, f32 *high_min, f32 *high_max, EnSeason season, i32 longitude, i32 latitude,
      bool farenheit) override;
  bool EndOfMonth();
  f32 GetHumidity(CtDate *date) override;
  f32 GetWindyness(CtDate *date) override;

protected:
  EnWeatherManipulator *manipulator;
  CtCity *city;
  i16 year, month;
  EnHemisphere hemisphere;
  EnSeason previous_season;
  i16 longitude, latitude;
  ScRandom random;
  ScStopwatch timer;
  u32 elapsed;
  EnWeatherType weather_type;
  i32 intensity;
  f32 low_temperature, average_temperature, high_temperature, humidity, windyness;
};

class CtCity;
class EcWorkforceValve;
class EcValveLayer;
class CtResidentialLayer;
class CtNeighborsLayer;

// Recovered world receiver and ownership. Private nominal-return gaps remain explicit in the native effects phases.
// The three original interface identities are established by the PC tables and matching Linux adjustment thunks.
class CtWorld : public CtWorldLayer, public CtLayer, public CtAgent {
public:
  CtWorld();
  virtual ~CtWorld();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  CtLayer *AsISC3CityLayer() override;
  CtAgent *AsISC3CityAgent() override;
  u32 GetWorldPopulation() override;
  u32 GetGlobalSeaLevel() override;
  ScGraphInfoView *GetUnemploymentHistory() override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Shutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Simulate(i32 phase) override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool LoadWorldINI();
  bool OnWindowCreate(ScMessage *message, ScUnknown *source);
  static void AddToWorldList(const ScString &key, const ScString &value, void *context);

private:
  // Complete native label-update effects; original private UpdateHistoryObjects return remains unresolved.
  void UpdateHistoryObjects();
  // Complete native population-update effects; original private RecalcWorldPopulation return remains unresolved.
  void RecalcWorldPopulation();
  // Complete native simulation effects; original private nominal return declarations remain unresolved.
  void SetCurrentPhase();
  void EndOfMonth();
  CtCity *city;
  EcValveLayer *valves;
  CtResidentialLayer *residential;
  CtNeighborsLayer *neighbors;
  EcValve *residential_valve, *industrial_valve, *commercial_valve;
  EcWorkforceValve *workforce;
  ScGraphInfoView *total_supply;
  ScString total_format;
  ScGraphInfoView *population;
  ScString population_format;
  ScGraphInfoView *commercial;
  ScString commercial_format;
  ScGraphInfoView *industrial;
  ScString industrial_format;
  ScGraphInfoView *unemployment;
  ScString unemployment_format;
  u32 world_population;
  bool recalculate_population;
  u32 sea_level, references;
  bool initialized;
  std::vector<CtValveEra> eras;
  bool initialize_workforce;
};

class EcValveLayer;

// Recovered cISC3WorkforceValve secondary identity, including its original ratio-method order.
class EcWorkforceView : public ScUnknown {
public:
  virtual EcValve *AsISC3Valve() = 0;
  virtual u32 SetCIRatio(u32 commercial, u32 industrial) = 0;
  virtual u32 GetCIRatio(u32 *commercial, u32 *industrial) = 0;
};

// Actual cSC3WorkforceValve owns the layer and three queried valves beside its distribution counters and ratios.
// Configuration effects stay distinct from the unresolved original private/extra virtual return contracts.
class EcWorkforceValve : public EcMiscValve, public EcWorkforceView {
public:
  EcWorkforceValve();
  ~EcWorkforceValve() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  EcValve *AsISC3Valve() override;
  u32 SetCIRatio(u32 commercial, u32 industrial) override;
  u32 GetCIRatio(u32 *commercial, u32 *industrial) override;
  bool Init(CtCity *city, u32 identifier, CtLayer *listener) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) const override;
  bool AddToSupplyValue(i32 amount) override;
  bool AddToDemandValue(i32 amount) override;
  bool EndOfMonth() override;
  // These native effects leave the extra setter return declarations unresolved.
  void ConfigureJobToResidential(u8 factor);
  void ConfigureExternalJobDemand(u32 amount);
  void UpdateExternalJobDemand();

private:
  u32 commercial_supplied, industrial_supplied, commercial_ratio, industrial_ratio;
  EcValveLayer *valves;
  EcValve *residential_valve, *commercial_valve, *industrial_valve;
  u8 job_to_residential;
  u32 external_job_demand;
  bool force_update, ready;
};

#endif
