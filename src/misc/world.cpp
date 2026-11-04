#include "misc/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "advisors/ad_module.h"
#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "city/sc_random.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sc_graph_info.h"
#include "resources/resource.h"
#include "resources/stream.h"

// The weather constructor retains the original initial elapsed-time word before sampling the seconds timer.
enum EnWeatherInitialTime : u32 {
  kEnWeatherInitialElapsedSeconds = 0xffffffff,
};

// City-size graphs have their own starting maximum; world-population growth uses a separate random-adjustment threshold and bias.
enum CtWorldPopulationLimit : i32 {
  kCtCitySizeGraphInitialMaximum = 0x186a0,
  kCtWorldPopulationAdjustmentThreshold = 0x3b9aca00,
  kCtWorldPopulationAdjustmentBias = 0x77359400,
};

// World history keeps separate city-size, national-population, commerce, industry and unemployment graph colors.
enum CtWorldGraphColor : u32 {
  kCtCitySizeGraphColor = 0x306020,
  kCtNationalPopulationGraphColor = 0xa060a0,
  kCtCommerceGraphColor = 0x1f40ff,
  kCtIndustryGraphColor = 0xe0a040,
  kCtUnemploymentGraphColor = 0x60a0,
};

// World population applies its original integer growth ratio before the separate upper-population adjustment.
enum CtWorldPopulationGrowth : u64 {
  kCtWorldPopulationGrowthNumerator = 0xf4283,
  kCtWorldPopulationGrowthScale = 0xf4240,
};

enum CtWorldArchiveKey : u32 {
  kCtWorldStateRecordGroup = 0xe11bddf6,
};

// Construct the original empty services, graph views, Strings and era vector without retaining the module.
// Native zero initialization of sea level and the workforce startup flag excludes the source's unwritten values.
CtWorld::CtWorld()
    : city(nullptr), valves(nullptr), residential(nullptr), neighbors(nullptr), residential_valve(nullptr),
      industrial_valve(nullptr), commercial_valve(nullptr), workforce(nullptr), total_supply(nullptr),
      population(nullptr), commercial(nullptr), industrial(nullptr), unemployment(nullptr), world_population(0),
      recalculate_population(true), sea_level(0), references(0), initialized(false), initialize_workforce(false) {
}

CtWorld::~CtWorld() {
  CtWorld::Shutdown();
}

// Return the original world, layer or agent identity and acquire a reference only for a supported interface. An
// unsupported query clears the output.
bool CtWorld::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtWorldLayer);
  HANDLE_QUERY_INTERFACE(IID_CtWorldLayer, CtWorldLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the instance count before retaining the module, then return the live count after that callback.
u32 CtWorld::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

// Decrement a shared instance before releasing the module; zero and one both take the original destruction branch.
// The final branch releases the module before destroying the receiver without first overwriting its count.
u32 CtWorld::Release() {
  if (references > 1) {
    --references;
    ScGetMiscDirector()->RemoveRef();
    return references;
  }
  ScGetMiscDirector()->RemoveRef();
  delete this;
  return 0;
}

// Expose the borrowed layer identity without adding a reference.
CtLayer *CtWorld::AsISC3CityLayer() {
  return static_cast<CtLayer *>(this);
}

// Expose the borrowed agent identity without adding a reference.
CtAgent *CtWorld::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

u32 CtWorld::GetWorldPopulation() {
  return world_population;
}

u32 CtWorld::GetGlobalSeaLevel() {
  return sea_level;
}

// Return the borrowed unemployment history view without adding a reference.
ScGraphInfoView *CtWorld::GetUnemploymentHistory() {
  return unemployment;
}

// Retire held services, runtime valve bindings and graph views only after successful instance initialization.
// Clear each hold after its release callback and reset the initialized flag last; the era allocation is retained.
bool CtWorld::Shutdown() {
  if (initialized) {
    ScRelease(&city);
    ScRelease(&valves);
    ScRelease(&residential);
    ScRelease(&neighbors);
    ScRelease(&residential_valve);
    ScRelease(&industrial_valve);
    ScRelease(&commercial_valve);
    ScRelease(&workforce);
    auto end = eras.end();
    for (auto entry = eras.begin(); entry != end; ++entry) {
      if (entry->valve) {
        entry->valve->Release();
        entry->valve = nullptr;
      }
    }
    eras.clear();
    if (total_supply)
      total_supply->Release();
    total_supply = nullptr;
    if (commercial)
      commercial->Release();
    commercial = nullptr;
    if (industrial)
      industrial->Release();
    industrial = nullptr;
    if (unemployment)
      unemployment->Release();
    unemployment = nullptr;
    if (population)
      population->Release();
    population = nullptr;
    initialized = false;
  }
  return true;
}

// Publish the five live graph views in their original order; callbacks may change later fields before they are read.
// A model notification is acknowledged even when the source cannot receive models.
bool CtWorld::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source);
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      ScModelReceiver *receiver = static_cast<ScModelReceiver *>(selected);
      receiver->DumpGraphData(total_supply);
      receiver->DumpGraphData(commercial);
      receiver->DumpGraphData(industrial);
      receiver->DumpGraphData(unemployment);
      receiver->DumpGraphData(population);
      receiver->Release();
    }
    return true;
  }
  return false;
}

// Add the current graph views to a compatible collection for the world information query, then release the collection.
bool CtWorld::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  if (u32(message->data1) != 1541 || !source || !source->QueryInterface(IID_UiGraphsViewIdentity, &selected))
    return false;
  ScObjectCollection *collection = static_cast<ScObjectCollection *>(selected);
  collection->Add(total_supply);
  collection->Add(commercial);
  collection->Add(industrial);
  collection->Add(unemployment);
  collection->Add(population);
  collection->Release();
  return true;
}

bool CtWorld::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

u32 CtWorld::GetLayerType() {
  return kCtWorldLayerType;
}

u32 CtWorld::TypeId() const {
  return kCtWorldLayerType;
}

i32 CtWorld::MinimumExecutionInterval() {
  return 4;
}

bool CtWorld::IsAgentLocking() {
  return false;
}

// This agent has no agent-type object.
CtAgentType *CtWorld::AgentType() {
  return nullptr;
}

bool CtWorld::SetAgentType(CtAgentType *) {
  return false;
}

bool CtWorld::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtWorld::DebugClassTag(ScStringView *) {
  return false;
}

bool CtWorld::DebugTypeTag(ScStringView *) {
  return false;
}

bool CtWorld::DoMessage(ScMessage *) {
  return false;
}

// This overload does not initialize the world receiver or retain the supplied city.
bool CtWorld::Init(CtCity *, ScImporter *importer) {
  if (!importer->MapIsPresent(kCtLegacyMiscPlane) || !importer->GetMiscValue(912, reinterpret_cast<i32 *>(&sea_level)))
    sea_level = 0;
  return true;
}

bool CtWorld::SimulationEnd() {
  ScGetMiscCity()->CitySimulator()->RemoveAgent(static_cast<CtAgent *>(this));
  return true;
}

std::vector<CtValveEra> CtWorldDefaultValves;
std::map<u32, CtPhaseEffect> CtWorldPhases;
u8 CtWorldJobToResidential = 2;
u32 CtWorldExternalJobDemand = 150;
ScStopwatch CtWorldTimer(ScStopwatch::Milliseconds, 0);

static void CtTakeWorldToken(ScString *input, char delimiter, ScString *out) {
  for (char *byte = input->begin; byte != input->end; ++byte)
    if (*byte == delimiter) {
      u32 length = u32(byte - input->begin);
      out->FromChar(input->begin, length);
      input->Erase(0, length + 1);
      return;
    }
  out->FromChar("", 0);
}

static u8 CtReadWorldSetting(EcIni *ini, const char *name, ScString *out) {
  ScString key(name), section("WorldLayer");
  return ini->GetValue(&section, &key, out);
}

// Reset and start the original module-global millisecond timer before loading this world's shared configuration.
// The reader uses only module state, but the original call still carries this world receiver.
bool CtWorld::StaticInit(CtDefinition *) {
  CtWorldTimer.Reset();
  CtWorldTimer.Start();
  return LoadWorldINI();
}

// Destroy the default schedules and phase entries while retaining the vector's capacity, then stop the shared timer.
// Scalar tuning values survive this shutdown, matching the next missing-key configuration fallback.
bool CtWorld::StaticShutdown() {
  CtWorldDefaultValves.clear();
  CtWorldPhases.clear();
  CtWorldTimer.Stop();
  return true;
}

// Parse space-separated month:percentage eras, narrowing durations before summing and rounding each multiplier. A zero
// period marks a permanent era. Compare input before and after extraction so a leading delimiter retains its original
// empty era.
void CtWorld::AddToWorldList(const ScString &key, const ScString &value, void *) {
  const char *identifier = key.ToChar();
  value.ToChar();
  ScString remaining, segment, duration, previous;
  CtValveEra schedule;
  u32 id = ScParseMiscNumber(identifier), period = 0;
  remaining = value;
  bool done = false, permanent = false;
  do {
    CtEraEffect effect = {};
    previous = remaining;
    CtTakeWorldToken(&remaining, ' ', &segment);
    if (previous.IsEqual(&remaining)) {
      segment = remaining;
      done = true;
    }
    CtTakeWorldToken(&segment, ':', &duration);
    effect.months = u16(ScParseMiscNumber(duration.ToChar()));
    i32 number = i32(atol(segment.ToChar()));
    period += effect.months;
    effect.factor = f32(f64(number) * f64(0.01f));
    if (permanent)
      break;
    permanent = effect.months == 0;
    schedule.effects.push_back(effect);
  } while (!done);
  schedule.identifier = id;
  schedule.valve = nullptr;
  schedule.current_era = 0;
  schedule.remaining_months = 0;
  schedule.period = permanent ? 0 : period;
  CtWorldDefaultValves.push_back(schedule);
}

// Append valve schedules and read scalar settings, replacing supplied phase keys without clearing old entries.
// Ensure phase zero exists with effects one/one; missing settings preserve previous scalar and table state.
bool CtWorld::LoadWorldINI() {
  ScString path, value;
  EcIni ini;
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SC3WorldLayer.ini");
  ini.SelectFile(&path);
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SYS.PAK");
  ini.SelectPackage(path);
  {
    ScString section("ListOfWorldValveMapping");
    ini.EnumerateSection(&section, CtWorld::AddToWorldList, nullptr);
  }
  if (CtReadWorldSetting(&ini, "JobToResFactor", &value)) {
    CtWorldJobToResidential = u8(ScParseMiscNumber(value.ToChar()));
    if (!CtWorldJobToResidential)
      CtWorldJobToResidential = 1;
  }
  if (CtReadWorldSetting(&ini, "ExternalJobDemand", &value))
    CtWorldExternalJobDemand = ScParseMiscNumber(value.ToChar());
  if (CtReadWorldSetting(&ini, "EconomicPhases", &value)) {
    ScString row, token;
    if (value.begin != value.end) {
      bool done = false;
      for (;;) {
        CtTakeWorldToken(&value, ' ', &row);
        if (row.begin == row.end) {
          row = value;
          done = true;
        }
        CtTakeWorldToken(&row, ':', &token);
        if (token.begin != token.end) {
          u32 key = ScParseMiscNumber(token.ToChar());
          CtTakeWorldToken(&row, ':', &token);
          if (token.begin != token.end) {
            u32 first = ScParseMiscNumber(token.ToChar());
            if (row.begin != row.end) {
              u32 second = ScParseMiscNumber(row.ToChar());
              if (first || second) {
                CtPhaseEffect effect = {u32(1000 * key), first, second};
                CtWorldPhases[key] = effect;
              }
            }
          }
        }
        if (done)
          break;
      }
    }
  }
  if (CtWorldPhases.empty() || CtWorldPhases.find(0) == CtWorldPhases.end()) {
    CtPhaseEffect initial = {0, 1, 1};
    CtWorldPhases[0] = initial;
  }
  return true;
}

// Keep data reads, formatting and label writes ordered, rereading graph fields after callbacks.
void CtWorld::UpdateHistoryObjects() {
  ScString formatted, number;
  ScLanguageUtility *language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  for (u32 group = 0; group < 5; ++group) {
    ScGraphInfoView **graph;
    ScString *pattern;
    switch (group) {
    case 0:
      graph = &total_supply;
      pattern = &total_format;
      break;
    case 1:
      graph = &population;
      pattern = &population_format;
      break;
    case 2:
      graph = &commercial;
      pattern = &commercial_format;
      break;
    case 3:
      graph = &industrial;
      pattern = &industrial_format;
      break;
    default:
      graph = &unemployment;
      pattern = &unemployment_format;
      break;
    }
    for (u32 set = 0; set < 3; ++set) {
      // Native scratch excludes an unwritten value if a foreign history provider refuses its output.
      u32 value = 0;
      if (set == 0)
        (*graph)->GetMonthData(&value);
      else if (set == 1)
        (*graph)->GetYearData(&value);
      else
        (*graph)->GetDecadeData(&value);
      language->MakeNumberString(group == 4 ? i64(i32(value)) : i64(value), &number);
      const char *text = number.begin, *format = pattern->ToChar();
      formatted.Sprintf(format, text);
      (*graph)->SetCurrentValueLabel(ScGraphDataSet(set), formatted);
    }
  }
}

// Read each neighbor after the prior callback, retain wrapping32-bit accumulation, then apply the original1500 scale.
void CtWorld::RecalcWorldPopulation() {
  world_population = u32(residential->GetPopulation());
  for (u8 index = 0; index < 4; ++index)
    world_population += neighbors->GetNeighborPopulation(index);
  u32 scaled = 1500 * world_population;
  world_population = scaled % 0x67f + scaled;
}

// Retain city services and configure the actual workforce valve before constructing the five owned history graphs.
// Preserve early failure fields, localized temporary lifetimes and live graph reads; initialized is published last.
bool CtWorld::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  recalculate_population = true;
  eras = CtWorldDefaultValves;
  CtLayer *specific = city->GetSpecificLayer(kEcValveLayerType);
  if (!specific) {
    valves = nullptr;
    city->Release();
    return false;
  }
  if (!specific->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves))) {
    city->Release();
    return false;
  }
  neighbors = city->NeighborsLayer();
  if (neighbors)
    neighbors->AddRef();
  residential = city->ResidentialLayer();
  if (residential)
    residential->AddRef();
  workforce = ScAddRefNotNull(new EcWorkforceValve);
  if (!workforce->Init(city, 7000, static_cast<CtLayer *>(this))) {
    workforce->Release();
    workforce = nullptr;
  } else {
    workforce->SetValveMax(2000);
    workforce->SetValveMin(-2000);
    workforce->ConfigureJobToResidential(CtWorldJobToResidential);
    workforce->ConfigureExternalJobDemand(CtWorldExternalJobDemand);
    valves->AddValveToLayer(workforce);
  }
  initialize_workforce = true;
  ScString formatted;
  for (u32 index = 0; index < 5; ++index) {
    ScGraphInfoView **history;
    ScString *pattern;
    u32 label_id, format_id, selection_key, value_key, color;
    switch (index) {
    case 0:
      history = &total_supply;
      pattern = &total_format;
      label_id = 353;
      format_id = 470;
      selection_key = kCtCitySizeGraphSelectionId;
      value_key = kCtCitySizeGraphValueId;
      color = kCtCitySizeGraphColor;
      break;
    case 1:
      history = &population;
      pattern = &population_format;
      label_id = 357;
      format_id = 652;
      selection_key = kCtNationalPopulationGraphSelectionId;
      value_key = kCtNationalPopulationGraphValueId;
      color = kCtNationalPopulationGraphColor;
      break;
    case 2:
      history = &commercial;
      pattern = &commercial_format;
      label_id = 355;
      format_id = 474;
      selection_key = kCtCommerceGraphSelectionId;
      value_key = kCtCommerceGraphValueId;
      color = kCtCommerceGraphColor;
      break;
    case 3:
      history = &industrial;
      pattern = &industrial_format;
      label_id = 356;
      format_id = 467;
      selection_key = kCtIndustryGraphSelectionId;
      value_key = kCtIndustryGraphValueId;
      color = kCtIndustryGraphColor;
      break;
    default:
      history = &unemployment;
      pattern = &unemployment_format;
      label_id = 360;
      format_id = 469;
      selection_key = kCtUnemploymentGraphSelectionId;
      value_key = kCtUnemploymentGraphValueId;
      color = kCtUnemploymentGraphColor;
      break;
    }
    ScGraphInfo *created = new ScGraphInfo(0);
    *history = created;
    (*history)->AddRef();
    if (index < 4) {
      created->SetAutoMaxFactors(index ? 10000 : kCtCitySizeGraphInitialMaximum, 1.1f);
      (*history)->SetAutoMax(true);
    } else {
      (*history)->SetMaxValue(100);
    }
    {
      CtMiscTextResource resource(label_id, kRsGameTextGroup);
      ScString text = resource.GetText(), copy(text), label(copy);
      (*history)->SetSelectionLabel(label);
    }
    {
      CtMiscTextResource resource(format_id, kRsGameTextGroup);
      ScString text = resource.GetText(), copy(text), format(copy);
      *pattern = format;
    }
    const char *format = pattern->ToChar();
    formatted.Sprintf(format, "0");
    (*history)->SetCurrentValueLabel(ScGraphDataSet::Month, formatted);
    (*history)->SetCurrentValueLabel(ScGraphDataSet::Year, formatted);
    (*history)->SetCurrentValueLabel(ScGraphDataSet::Decade, formatted);
    (*history)->SetSelectionLabelID(selection_key);
    (*history)->SetValueLabelID(value_key);
    (*history)->SetColor(color);
  }
  initialized = true;
  return true;
}

// Write version2 world state in its original frame, serializing workforce and five live graphs before population.
// Any refused payload marks the scope for abort before the ordinary frame destructor closes it.
bool CtWorld::Save(CtCity *save_city, RsDatabaseSegment *segment) {
  ScArchiveFrame frame(save_city, segment, {kRsCityLayerRecordType, kCtWorldStateRecordGroup, 0}, 2, 0);
  u8 valid = frame.valid;
  RsStream *stream = nullptr;
  if (valid) {
    stream = frame.GetStream();
    valid = stream != nullptr;
  }
  valid = valid && workforce->SerialWrite(save_city, stream) && total_supply->SerialWrite(save_city, stream) &&
      commercial->SerialWrite(save_city, stream) && industrial->SerialWrite(save_city, stream) &&
      unemployment->SerialWrite(save_city, stream) && population->SerialWrite(save_city, stream) &&
      stream->WriteU32(world_population);
  if (!valid)
    frame.Fail();
  return valid != 0;
}

// Consume obsolete valve schedules in pre-version2 records, then restore workforce, graph histories and population.
// Preserve partial writes. Recalculation stops after a successful population read; startup demand always clears.
bool CtWorld::Init(CtCity *load_city, RsDatabaseSegment *segment) {
  ScArchiveFrame frame(load_city, segment, {kRsCityLayerRecordType, kCtWorldStateRecordGroup, 0});
  u8 valid = frame.valid;
  RsStream *stream = nullptr;
  if (valid) {
    stream = frame.GetStream();
    valid = stream != nullptr;
  }
  std::vector<CtEraEffect> obsolete;
  if (valid) {
    if (frame.version < 2) {
      // Defined scratch excludes the original unwritten count/effect reads after transport refusal.
      u32 count = 0, identifier = 0, effects = 0;
      u8 era = 0;
      u16 remaining = 0;
      CtEraEffect effect = {};
      valid = stream->ReadU32(&count);
      while (count--) {
        valid = valid && stream->ReadU32(&identifier) && stream->ReadU8(&era) && stream->ReadU16(&remaining) &&
            stream->ReadU32(&effects);
        if (effects) {
          while (valid) {
            if (!effects--)
              break;
            valid = stream->ReadU16(&effect.months);
            if (valid)
              valid = stream->ReadF32(&effect.factor);
            obsolete.push_back(effect);
          }
        }
      }
    }
    if (valid)
      valid = workforce->SerialRead(load_city, stream);
    if (valid)
      valid = total_supply->SerialRead(load_city, stream);
    if (valid)
      valid = commercial->SerialRead(load_city, stream);
    if (valid)
      valid = industrial->SerialRead(load_city, stream);
    if (valid)
      valid = unemployment->SerialRead(load_city, stream);
    if (valid)
      valid = population->SerialRead(load_city, stream);
    if (valid) {
      valid = stream->ReadU32(&world_population);
      if (valid)
        recalculate_population = false;
    }
  }
  initialize_workforce = false;
  if (!valid)
    frame.Fail();
  return valid != 0;
}

// Preserve the original PC53 magic-add rounding and low-word wrap for the workforce population calculation.
static u32 CtRoundWorldWorkforce(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Bind runtime valves and select each current era before phase configuration, deferred demand and graph labels.
// Preserve post-erase skips with indices; bounded traversal excludes the source's invalidated past-end iterator reads.
bool CtWorld::SimulationBegin() {
  [[maybe_unused]] CtDate today;
  if (!valves)
    return false;
  for (usize index = 0; index < eras.size(); ++index) {
    EcValve *selected;
    if (valves->GetValvePointer(eras[index].identifier, IID_EcValve, reinterpret_cast<void **>(&selected)))
      eras[index].valve = selected;
    else
      eras.erase(eras.begin() + index);
  }
  if (!valves->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&residential_valve)) ||
      !valves->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&commercial_valve)) ||
      !valves->GetValvePointer(8000, IID_EcValve, reinterpret_cast<void **>(&industrial_valve)))
    return false;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  if (year < 1900)
    year = 1900;
  for (usize index = 0; index < eras.size(); ++index) {
    CtValveEra *entry = &eras[index];
    i32 elapsed = i32(month + 12 * year - 22801);
    if (i32(entry->period) > 0)
      while (elapsed >= i32(entry->period))
        elapsed -= i32(entry->period);
    entry->current_era = 0;
    if (entry->effects.empty()) {
      eras.erase(eras.begin() + index);
      continue;
    }
    auto effect = entry->effects.begin();
    while (effect != entry->effects.end()) {
      u16 duration = effect->months;
      if (elapsed < i32(duration) || !duration) {
        entry->valve->SetEconomyModifier(effect->factor);
        entry->remaining_months = duration ? u16(u32(duration) - u32(elapsed)) : u16(-536);
        break;
      }
      elapsed -= duration;
      ++entry->current_era;
      if (entry->current_era < entry->effects.size())
        ++effect;
      else {
        entry->current_era = 0;
        effect = entry->effects.begin();
      }
    }
  }
  SetCurrentPhase();
  if (initialize_workforce)
    workforce->UpdateExternalJobDemand();
  UpdateHistoryObjects();
  if (recalculate_population)
    RecalcWorldPopulation();
  ScGetMiscCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  return true;
}

// Walk unsigned keys in order, interpolate by stored thresholds and retain the original absolute-difference increase.
void CtWorld::SetCurrentPhase() {
  u64 population_value = u64(i64(residential_valve->QuerySupplyValue()));
  auto lower = CtWorldPhases.begin(), upper = lower;
  ++upper;
  while (upper != CtWorldPhases.end()) {
    if (upper->second.population > population_value)
      break;
    lower = upper;
    ++upper;
  }
  u32 first = lower->second.commercial_ratio, second = lower->second.industrial_ratio;
  if (upper != CtWorldPhases.end()) {
    u64 progress = population_value - lower->second.population;
    u64 span = upper->second.population - lower->second.population;
    i32 delta = i32(upper->second.commercial_ratio - first);
    if (delta < 0)
      delta = i32(0u - u32(delta));
    first += u32(u64(i64(delta)) * progress / span);
    delta = i32(upper->second.industrial_ratio - second);
    if (delta < 0)
      delta = i32(0u - u32(delta));
    second += u32(u64(i64(delta)) * progress / span);
  }
  workforce->SetCIRatio(first, second);
}

// Run the monthly world update only for simulation phase four, acknowledging it independently of private residues.
bool CtWorld::Simulate(i32 phase) {
  if (phase != 4)
    return false;
  EndOfMonth();
  return true;
}

// Retain era countdowns, growth arithmetic, live graph reads and January/year-ten rollups before refreshing labels.
void CtWorld::EndOfMonth() {
  ScRandom random(CtWorldTimer.GetElapsedTime());
  for (CtValveEra &entry : eras) {
    u8 selected = entry.current_era;
    if (selected >= entry.effects.size())
      selected = 0;
    usize effect_index = selected;
    if (entry.effects[effect_index].months) {
      if (entry.remaining_months) {
        --entry.remaining_months;
      } else {
        ++entry.current_era;
        if (entry.current_era >= entry.effects.size())
          entry.current_era = 0;
        effect_index = entry.current_era;
        entry.remaining_months = entry.effects[effect_index].months;
      }
    }
    entry.valve->SetEconomyModifier(entry.effects[effect_index].factor);
  }
  SetCurrentPhase();
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  u64 grown = kCtWorldPopulationGrowthNumerator * world_population / kCtWorldPopulationGrowthScale;
  if (grown > kCtWorldPopulationAdjustmentThreshold) {
    u32 value = random.Next();
    grown = u64(value) - grown + kCtWorldPopulationAdjustmentBias;
  }
  u64 local_population = u32(residential->GetPopulation());
  for (u8 index = 0; index < 4; ++index)
    local_population += neighbors->GetNeighborPopulation(index);
  if (grown >= local_population)
    world_population = u32(grown);
  else
    RecalcWorldPopulation();
  population->SetMonthData(world_population);
  EcValve *residential_source = residential_valve, *industrial_source = industrial_valve;
  EcValve *commercial_source = commercial_valve;
  u32 total = u32(residential_source->QuerySupplyValue());
  total += u32(industrial_source->QuerySupplyValue());
  total += u32(commercial_source->QuerySupplyValue());
  total_supply->SetMonthData(total);
  commercial->SetMonthData(u32(commercial_valve->QuerySupplyValue()));
  industrial->SetMonthData(u32(industrial_valve->QuerySupplyValue()));
  u32 workers = u32(residential_valve->QuerySupplyValue()), unemployment_value = 0;
  if (workers) {
    workers = CtRoundWorldWorkforce(f64(workers) * residential->GetWorkforcePopPct());
    u32 demand = u32(workforce->QueryDemandValue());
    if (demand < workers)
      unemployment_value = demand ? (100 * workers - 100 * demand) / (2 * workers) : 100;
  }
  unemployment->SetMonthData(unemployment_value);
  if (month == 1) {
    ScGraphInfoView **graphs[] = {&total_supply, &commercial, &industrial, &unemployment, &population};
    for (u32 index = 0; index < 5; ++index) {
      u32 average = (*graphs[index])->GetMonthAverage();
      (*graphs[index])->SetYearData(average);
    }
    if (year % 10 == 0)
      for (u32 index = 0; index < 5; ++index) {
        u32 average = (*graphs[index])->GetYearAverage();
        (*graphs[index])->SetDecadeData(average);
      }
  }
  UpdateHistoryObjects();
}

// Construct the actual lifetime base, random state and seconds timer before seeding from half the system uptime tick.
// Start at the original northern130/37 position with no city, manipulator, weather or measured temperature.
EnWeather::EnWeather()
    : manipulator(nullptr), city(nullptr), year(0), month(0), hemisphere(EnHemisphere::Northern), previous_season(-1),
      longitude(130), latitude(37), random(kScRandomSeedFromClock), timer(ScStopwatch::Seconds, 0),
      elapsed(kEnWeatherInitialElapsedSeconds), weather_type(0), intensity(0), low_temperature(0),
      average_temperature(0), high_temperature(0), humidity(0), windyness(0) {
  random.SetSeed(ScSystemMilliseconds() >> 1);
  longitude = 130;
  latitude = 37;
}

EnWeather::~EnWeather() {
  Shutdown();
}

// Publish the same primary receiver for each original unknown/layer/message/query/weather identity.
// Unsupported identities clear the output; accepted identities retain through the live primary virtual operation.
bool EnWeather::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EnWeatherLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, EnWeatherLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, EnWeatherLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, EnWeatherLayer);
  HANDLE_QUERY_INTERFACE(IID_EnWeatherLayer, EnWeatherLayer);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EnWeather::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 EnWeather::Release() {
  return ScMiscRefCount::Release();
}

bool EnWeather::DoMessage(ScMessage *) {
  return false;
}

bool EnWeather::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool EnWeather::StaticInit(CtDefinition *) {
  return true;
}

bool EnWeather::StaticShutdown() {
  return true;
}

// Publish a supplied city before retaining it, preserving the original absence of a replacement-city release.
bool EnWeather::Init(CtCity *value) {
  if (value) {
    city = value;
    value->AddRef();
  }
  return true;
}

bool EnWeather::Init(CtCity *, ScImporter *) {
  return true;
}

bool EnWeather::Init(CtCity *, RsDatabaseSegment *) {
  hemisphere = latitude < 0 ? EnHemisphere::Southern : EnHemisphere::Northern;
  return true;
}

bool EnWeather::Save(CtCity *, RsDatabaseSegment *) {
  return true;
}

bool EnWeather::SimulationEnd() {
  return true;
}

// Release the live city hold before clearing it, then break the manipulator's owner cycle before releasing that hold.
// Reload the manipulator field after its shutdown callback, as the original does before the final Release.
bool EnWeather::Shutdown() {
  ScRelease(&city);
  if (manipulator) {
    manipulator->Shutdown();
    manipulator->Release();
    manipulator = nullptr;
  }
  return true;
}

u32 EnWeather::GetLayerType() {
  return kEnWeatherLayerType;
}

bool EnWeather::DebugClassTag(ScStringView *) {
  return false;
}

bool EnWeather::DebugTypeTag(ScStringView *) {
  return false;
}

void EnWeather::GetPosition(i32 *out_longitude, i32 *out_latitude) {
  *out_longitude = longitude;
  *out_latitude = latitude;
}

// Store the low16 bits of latitude before longitude and derive the cached hemisphere from that truncated latitude.
void EnWeather::SetPosition(i32 new_longitude, i32 new_latitude) {
  latitude = i16(new_latitude);
  longitude = i16(new_longitude);
  hemisphere = latitude < 0 ? EnHemisphere::Southern : EnHemisphere::Northern;
}

EnWeather::EnHemisphere EnWeather::GetHemisphere() {
  return hemisphere;
}

EnWeather::EnSeason EnWeather::GetCurrentSeason() {
  return i32(u32(GetCurrentSeasonMonth()) - 1) / 3;
}

i32 EnWeather::GetCurrentSeasonMonth() {
  return latitude < 0 ? (month + 6) % 12 : month;
}

void EnWeather::GetWeather(EnWeatherType *type, i32 *value, CtDate *) {
  *type = weather_type;
  *value = intensity;
}

// Store both weather words before posting the original typed notification containing this receiver and the new values.
void EnWeather::SetWeather(EnWeatherType type, i32 value) {
  weather_type = type;
  intensity = value;
  ScMessage message(kEnMsg_WeatherChanged, reinterpret_cast<uptr>(this), u32(type), u32(value));
  ScGetMiscMessageServer()->MessagePost(message, false);
}

f32 EnWeather::FarenheitToCelsius(f32 value) {
  return f32((f64(value) - 32.0) * 0.5555555555555556);
}

f32 EnWeather::CelsiusToFarenheit(f32 value) {
  return f32(f64(value) * 1.8 + 32.0);
}

f32 EnWeather::CelsiusToKelvin(f32 value) {
  return f32(f64(value) + 273.15);
}

f32 EnWeather::KelvinToCelsius(f32 value) {
  return f32(f64(value) - 273.15);
}

f32 EnWeather::GetAverageTemperature(bool farenheit, CtDate *) {
  return farenheit ? f32(f64(average_temperature) * 1.8 + 32.0) : average_temperature;
}

f32 EnWeather::GetAverageHighTemperature(bool farenheit, CtDate *) {
  return farenheit ? f32(f64(high_temperature) * 1.8 + 32.0) : high_temperature;
}

f32 EnWeather::GetAverageLowTemperature(bool farenheit, CtDate *) {
  return farenheit ? f32(f64(low_temperature) * 1.8 + 32.0) : low_temperature;
}

f32 EnWeather::GetHumidity(CtDate *) {
  return humidity;
}

f32 EnWeather::GetWindyness(CtDate *) {
  return windyness;
}

// Break the actual weather-manipulator ownership cycle by releasing its owner before clearing the stored pointer.
bool EnWeatherManipulator::Shutdown() {
  weather->Release();
  weather = nullptr;
  return true;
}

bool EnWeather::SimulationBegin() {
  EndOfMonth();
  return true;
}

// Search the requested type's parent chain for the original weather scheduling identity after publishing the flags.
// Lazily retain one real manipulator for this layer and return its borrowed primary view without another output hold.
bool EnWeather::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  const CtAgentType *current = &type;
  while (current && current->Id() != kCtAgentType_AreaAgent)
    current = current->Parent();
  if (!current)
    return false;
  if (!manipulator)
    manipulator = ScAddRefNotNull(new EnWeatherManipulator(this));
  *out = manipulator;
  return true;
}

// Write the original seasonal base/deviation values in source order, preserving aliased output behavior.
// Apply the original low-temperature test and Kelvin ordering before the independent optional Fahrenheit conversions.
void EnWeather::GetAverageTemperaturePerSeasonPerLocation(f32 *low, f32 *low_min, f32 *low_max, f32 *average,
    f32 *average_min, f32 *average_max, f32 *high, f32 *high_min, f32 *high_max, EnSeason season, i32, i32,
    bool farenheit) {
  switch (season) {
  case 0:
    *low = 7.0f;
    *average = 13.0f;
    *high = 16.0f;
    *low_min = -10.0f;
    *low_max = 10.0f;
    *average_min = -6.0f;
    *average_max = 10.0f;
    *high_min = -6.0f;
    *high_max = 15.0f;
    break;
  case 1:
    *low = 13.0f;
    *average = 17.0f;
    *high = 21.0f;
    *low_min = -10.0f;
    *low_max = 10.0f;
    *average_min = -6.0f;
    *average_max = 10.0f;
    *high_min = -6.0f;
    *high_max = 15.0f;
    break;
  case 2:
    *low = 15.0f;
    *average = 20.0f;
    *high = 28.0f;
    *low_min = -11.0f;
    *low_max = 14.0f;
    *average_min = -6.0f;
    *average_max = 11.0f;
    *high_min = -6.0f;
    *high_max = 17.0f;
    break;
  default:
    *low = 13.0f;
    *average = 17.0f;
    *high = 21.0f;
    *low_min = -15.0f;
    *low_max = 10.0f;
    *average_min = -6.0f;
    *average_max = 8.0f;
    *high_min = -6.0f;
    *high_max = 15.0f;
    break;
  }
  if (*low < -40.0f)
    *low = f32(random.RandomDoubleRangeUniform(-2.0, 2.0) - 40.0f);
  if (*low < 57.0f)
    *low = f32(random.RandomDoubleRangeUniform(-2.0, 2.0) + 55.0f);
  *low = f32(f64(*low) + 273.15f);
  *average = f32(f64(*average) + 273.15f);
  *high = f32(f64(*high) + 273.15f);
  f64 minimum_average = f64(*low) * 1.007f;
  if (minimum_average >= *average)
    *average = f32(minimum_average);
  f64 minimum_high = f64(1.007f) * *average;
  if (minimum_high >= *high)
    *high = f32(minimum_high);
  *low = f32(f64(*low) - 273.15f);
  *average = f32(f64(*average) - 273.15f);
  *high = f32(f64(*high) - 273.15f);
  if (farenheit) {
    *low = f32(f64(*low) * 1.8f + 32.0f);
    *average = f32(f64(*average) * 1.8f + 32.0f);
    *high = f32(f64(*high) * 1.8f + 32.0f);
    *low_min = f32(f64(*low_min) * 1.8f + 32.0f);
    *low_max = f32(f64(*low_max) * 1.8f + 32.0f);
    *average_min = f32(f64(*average_min) * 1.8f + 32.0f);
    *average_max = f32(f64(*average_max) * 1.8f + 32.0f);
    *high_min = f32(f64(*high_min) * 1.8f + 32.0f);
    *high_max = f32(f64(*high_max) * 1.8f + 32.0f);
  }
}

// Read the current city date and elapsed seconds before restarting the timer and updating the cached year/month.
// Only a changed season selects weather and samples new temperatures, preserving the source thresholds and float stores.
bool EnWeather::EndOfMonth() {
  CtDate date;
  EnSeason previous = previous_season;
  city->GetSimulationClock()->GetCurrentClockDate(&date);
  elapsed = timer.GetElapsedTime();
  timer.Restart();
  year = i16(date.Year());
  month = i16(date.Month());
  EnSeason current = GetCurrentSeason();
  previous_season = current;
  if (current == previous)
    return true;
  if (elapsed >= 16) {
    if (current == 0)
      SetWeather(2, intensity);
    else if (current == 1)
      SetWeather(1, intensity);
    else
      SetWeather(0, intensity);
  } else if (weather_type != 0) {
    SetWeather(0, intensity);
  }
  f32 low, low_min, low_max, average, average_min, average_max, high, high_min, high_max;
  GetAverageTemperaturePerSeasonPerLocation(&low, &low_min, &low_max, &average, &average_min, &average_max, &high,
      &high_min, &high_max, previous_season, longitude, latitude, false);
  low_temperature = f32(random.RandomDoubleRangeGaussian(f64(low) + low_min, f64(low) + low_max));
  average_temperature = f32(random.RandomDoubleRangeGaussian(f64(average) + average_min, f64(average) + average_max));
  f32 low_kelvin = f32(f64(low_temperature) + 273.15f);
  low_temperature = low_kelvin;
  f32 average_kelvin = f32(f64(average_temperature) + 273.15f);
  average_temperature = average_kelvin;
  f32 high_sample = f32(random.RandomDoubleRangeGaussian(f64(low_min) + high, f64(high) + high_max));
  f64 high_kelvin = f64(high_sample) + 273.15f;
  high_temperature = f32(high_kelvin);
  f32 minimum_high = f32(f64(low_kelvin) * 1.015f);
  if (high_kelvin <= minimum_high)
    high_temperature = minimum_high;
  f64 minimum_average = f64(low_kelvin) * 1.007f;
  if (average_kelvin <= minimum_average)
    average_temperature = f32(minimum_average);
  f64 ordered_high = f64(1.007f) * average_temperature;
  if (ordered_high >= high_temperature)
    high_temperature = f32(ordered_high);
  low_temperature = f32(f64(low_kelvin) - 273.15f);
  average_temperature = f32(f64(average_temperature) - 273.15f);
  high_temperature = f32(f64(high_temperature) - 273.15f);
  return true;
}

EnWeatherManipulator::EnWeatherManipulator(EnWeather *owner) : weather(owner) {
  owner->AddRef();
}

EnWeatherManipulator::~EnWeatherManipulator() {
}

bool EnWeatherManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EnWeatherManipulator::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 EnWeatherManipulator::Release() {
  return ScMiscRefCount::Release();
}

bool EnWeatherManipulator::Apply(CtAgent *) {
  weather->EndOfMonth();
  return true;
}

bool EnWeatherManipulator::IsLockingCity() {
  return false;
}

bool EnWeatherManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EnWeatherManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EnWeatherManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Construct the common valve and secondary workforce identity with empty services and counters.
// The original workforce ratios begin at one each; the raw job-to-residential byte remains zero until configured.
EcWorkforceValve::EcWorkforceValve()
    : commercial_supplied(0), industrial_supplied(0), commercial_ratio(1), industrial_ratio(1), valves(nullptr),
      residential_valve(nullptr), commercial_valve(nullptr), industrial_valve(nullptr), job_to_residential(0),
      external_job_demand(0), force_update(false), ready(false) {
}

// Retire only the base borrowed-city binding. Held workforce services require explicit Shutdown before destruction.
EcWorkforceValve::~EcWorkforceValve() = default;

bool EcWorkforceValve::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_EcWorkforce, EcWorkforceView);
  return EcMiscValve::QueryInterface(iid, out);
}

u32 EcWorkforceValve::AddRef() {
  return EcMiscValve::AddRef();
}

u32 EcWorkforceValve::Release() {
  return EcMiscValve::Release();
}

// Return the borrowed primary valve identity from the secondary workforce view without adding a reference.
EcValve *EcWorkforceValve::AsISC3Valve() {
  return static_cast<EcValve *>(this);
}

// Bind the common city state, then query the real valve layer and initialize the original distribution state.
// Failure clears the layer and borrowed city. The job factor and deferred-update flag retain their entry values.
bool EcWorkforceValve::Init(CtCity *owner, u32 event_id, CtLayer *listener) {
  if (!EcMiscValve::Init(owner, event_id, listener))
    return false;
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves))) {
    valves = nullptr;
    EcMiscValve::Shutdown();
    return false;
  }
  commercial_supplied = 0;
  industrial_supplied = 0;
  commercial_ratio = 1;
  industrial_ratio = 1;
  external_job_demand = 0;
  ready = false;
  return true;
}

// Release the three queried valves and then their layer, clearing each field after its release callback.
// This operation deliberately leaves the base borrowed city and workforce flags unchanged.
bool EcWorkforceValve::Shutdown() {
  ScRelease(&residential_valve);
  ScRelease(&commercial_valve);
  ScRelease(&industrial_valve);
  ScRelease(&valves);
  return true;
}

// Acquire residential, commercial and industrial valves in order, preserving partial-failure releases.
// Apply a deferred external demand update before publishing that the receiver is ready.
bool EcWorkforceValve::SimulationBegin() {
  if (!valves->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&residential_valve)))
    return false;
  if (!valves->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&commercial_valve))) {
    residential_valve->Release();
    return false;
  }
  if (!valves->GetValvePointer(8000, IID_EcValve, reinterpret_cast<void **>(&industrial_valve))) {
    residential_valve->Release();
    commercial_valve->Release();
    return false;
  }
  if (force_update)
    AddToSupplyValue(i32(external_job_demand));
  ready = true;
  return true;
}

// Read the common valve state before the two distribution counters and two ratios, preserving partial writes.
bool EcWorkforceValve::SerialRead(CtCity *owner, RsStream *record) {
  if (!EcMiscValve::SerialRead(owner, record))
    return false;
  if (!record->ReadU32(&commercial_supplied))
    return false;
  if (!record->ReadU32(&industrial_supplied))
    return false;
  if (!record->ReadU32(&commercial_ratio))
    return false;
  return record->ReadU32(&industrial_ratio) != 0;
}

// Write the common state followed by distribution counters and ratios, retaining the final raw transport byte.
u8 EcWorkforceValve::SerialWrite(CtCity *owner, RsStream *record) const {
  u8 result = EcMiscValve::SerialWrite(owner, record);
  result = result && record->WriteU32(commercial_supplied) && record->WriteU32(industrial_supplied) &&
      record->WriteU32(commercial_ratio) && record->WriteU32(industrial_ratio);
  return result;
}

// Accumulate nonzero supply and distribute its wrapping products by the configured commercial/industrial ratios.
// Assign a positive signed16 remainder according to the current share, then forward signed16 amounts in original order.
bool EcWorkforceValve::AddToSupplyValue(i32 amount) {
  if (!amount)
    return true;
  EcMiscValve::AddToSupplyValue(amount);
  u32 first_ratio = commercial_ratio, total_ratio = first_ratio + industrial_ratio;
  u32 first = u32(amount) * first_ratio / total_ratio, second = u32(amount) * industrial_ratio / total_ratio;
  u32 remainder = u32(amount) - second - first;
  if (i16(remainder) > 0) {
    u32 total_supplied = commercial_supplied + industrial_supplied;
    volatile f64 desired = f64(first_ratio) / i32(total_ratio);
    volatile f64 observed = total_supplied ? f64(commercial_supplied) / i32(total_supplied) : 0.0;
    if (observed >= desired)
      second += remainder;
    else
      first += remainder;
  }
  i32 commercial_amount = i16(first), industrial_amount = i16(second);
  commercial_valve->AddToDemandValue(commercial_amount);
  commercial_supplied += u32(commercial_amount);
  industrial_valve->AddToDemandValue(industrial_amount);
  industrial_supplied += u32(industrial_amount);
  return true;
}

bool EcWorkforceValve::AddToDemandValue(i32 amount) {
  EcMiscValve::AddToDemandValue(amount);
  residential_valve->AddToDemandValue(i32(u32(amount) * job_to_residential));
  return true;
}

// Publish the base monthly state, reset industry then commercial counters and apply live external job demand.
bool EcWorkforceValve::EndOfMonth() {
  EcMiscValve::EndOfMonth();
  u32 pending = external_job_demand;
  industrial_supplied = 0;
  commercial_supplied = 0;
  AddToSupplyValue(i32(pending));
  return true;
}

u32 EcWorkforceValve::SetCIRatio(u32 commercial, u32 industrial) {
  commercial_ratio = commercial;
  industrial_ratio = industrial;
  if (!commercial_ratio && !industrial_ratio) {
    commercial_ratio = 1;
    industrial_ratio = 1;
    return 1;
  }
  return industrial;
}

u32 EcWorkforceValve::GetCIRatio(u32 *commercial, u32 *industrial) {
  *commercial = commercial_ratio;
  u32 value = industrial_ratio;
  *industrial = value;
  return value;
}

// A zero factor is replaced by one after storing the supplied byte.
void EcWorkforceValve::ConfigureJobToResidential(u8 factor) {
  job_to_residential = factor;
  if (!factor)
    job_to_residential = 1;
}

void EcWorkforceValve::ConfigureExternalJobDemand(u32 amount) {
  external_job_demand = amount;
}

// A ready receiver forwards through its live virtual supply method; otherwise it records the deferred update.
void EcWorkforceValve::UpdateExternalJobDemand() {
  if (ready)
    AddToSupplyValue(i32(external_job_demand));
  else
    force_update = true;
}

// Common valve source is compiled separately into each game DLL; this copy retains SIMMISC module ownership.

// Initialize the complete scalar valve state without acquiring a city or module ownership reference.
EcMiscValve::EcMiscValve()
    : references(0), identifier(0), supply(0), demand(0), supply_accumulator(0), demand_accumulator(0),
      initial_supply(0), initial_demand(0), maximum(0), minimum(0), balance(0), flag_30(true), flag_31(true),
      economy_modifier(0), tax_modifier(0), demand_cap(0), city(nullptr) {
}

EcMiscValve::~EcMiscValve() {
  EcMiscValve::Shutdown();
}

// Publish this same valve view for the two supported identities before acquiring its reference.
bool EcMiscValve::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_EcValve, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the module, then return its live post-callback value.
u32 EcMiscValve::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

// Release the module before decrementing a nonzero local count and destroy the valve when the live count reaches zero.
u32 EcMiscValve::Release() {
  if (references) {
    ScGetMiscDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Bind the borrowed city and identifier while resetting simulation values and modifiers, preserving flags30/31 ordering.
// The listener is unused by the installed body; reference count and flag31 retain their entry values.
bool EcMiscValve::Init(CtCity *owner, u32 event_id, CtLayer *) {
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

bool EcMiscValve::Shutdown() {
  city = nullptr;
  return true;
}

u32 EcMiscValve::GetId() {
  return identifier;
}

void EcMiscValve::SetId(u32 event_id) {
  identifier = event_id;
}

i32 EcMiscValve::QuerySupplyValue() {
  return supply;
}

i32 EcMiscValve::QueryDemandValue() {
  return demand;
}

i32 EcMiscValve::QueryValveValue() {
  return balance;
}

f32 EcMiscValve::QueryEconomyModifier() {
  return economy_modifier;
}

i32 EcMiscValve::QueryValveMax() {
  return maximum;
}

i32 EcMiscValve::QueryValveMin() {
  return minimum;
}

bool EcMiscValve::AddToSupplyValue(i32 amount) {
  supply_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcMiscValve::AddToDemandValue(i32 amount) {
  demand_accumulator += static_cast<u32>(amount);
  return true;
}

bool EcMiscValve::SetSupplyValue(i32 amount) {
  supply = amount;
  return true;
}

bool EcMiscValve::SetDemandValue(i32 amount) {
  demand = amount;
  return true;
}

void EcMiscValve::SetValveMax(i32 upper) {
  maximum = upper < minimum ? minimum : upper;
}

void EcMiscValve::SetValveMin(i32 lower) {
  minimum = lower > maximum ? maximum : lower;
}

// Remove the previous initial supply contribution before adding and remembering its replacement.
void EcMiscValve::SetInitialSupplyValue(i32 amount) {
  supply -= initial_supply;
  supply += amount;
  initial_supply = amount;
}

// Replace the initial demand contribution while retaining the source subtraction/addition ordering.
void EcMiscValve::SetInitialDemandValue(i32 amount) {
  demand -= initial_demand;
  demand += amount;
  initial_demand = amount;
}

f32 EcMiscValve::GetEconomyModifier() {
  return economy_modifier;
}

void EcMiscValve::SetEconomyModifier(f32 modifier) {
  economy_modifier = modifier;
}

f32 EcMiscValve::GetTaxModifier() {
  return tax_modifier;
}

void EcMiscValve::SetTaxModifier(f32 modifier) {
  tax_modifier = modifier;
}

u8 EcMiscValve::GetDemandCap() {
  return demand_cap;
}

void EcMiscValve::SetDemandCap(u8 cap) {
  demand_cap = cap > 100 ? 100 : cap;
}

// Build the current signed per-thousand notification before acquiring the message server, preserving live callback order.
void EcMiscValve::NotifyValue() const {
  i32 ratio = balance >= 0 ? 1000 * balance / maximum : -1000 * balance / minimum;
  ScMessage message = {kEcMsg_DemandChanged, identifier, static_cast<u32>(ratio), 0};
  ScGetMiscMessageServer()->MessagePost(message, false);
}

// Recalculate an uninitialized valve before posting its current value, retaining the recalculation's separate notification.
bool EcMiscValve::SimulationBegin() {
  if (flag_30)
    UpdateValue(0);
  NotifyValue();
  return true;
}

// Restore the ten serialized values in source order, retaining partial scalar assignments when a read fails.
// Notification occurs even after failure; zero initializes only the otherwise undefined first unread temporary.
bool EcMiscValve::SerialRead(CtCity *, RsStream *record) {
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
u8 EcMiscValve::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = record->WriteU32(static_cast<u32>(supply));
  result = result && record->WriteU32(static_cast<u32>(demand)) && record->WriteU32(supply_accumulator) &&
      record->WriteU32(demand_accumulator) && record->WriteF32(economy_modifier) && record->WriteF32(tax_modifier) &&
      record->WriteU8(demand_cap) && record->WriteU32(static_cast<u32>(balance)) && record->WriteI32(initial_supply) &&
      record->WriteI32(initial_demand);
  return result;
}

void EcMiscValve::UpdateValue(u8 smooth) {
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
bool EcMiscValve::EndOfMonth() {
  u8 smooth = flag_31;
  supply = static_cast<i32>(supply_accumulator);
  demand = static_cast<i32>(demand_accumulator);
  supply_accumulator = static_cast<u32>(initial_supply);
  demand_accumulator = static_cast<u32>(initial_demand);
  UpdateValue(smooth);
  return true;
}

void EcMiscValve::DebugLockValue(i32) {
}

void EcMiscValve::DebugUnlockValue() {
}

bool EcMiscValve::DebugIsValueLocked() {
  return false;
}

// Preserve the original PC53 magic-add rounding and low-DWORD wrap instead of using a host integer conversion.
static u32 ScRoundGraphValue(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

ScGraphInfo::ScGraphInfo(u32 initial)
    : selectionLabelId(0), valueLabelId(0), color(0), maximum(initial), automaticScale(false), minimum(10),
      scaleFactor(1), monthly(12, initial), yearly(10, initial), decades(10, initial) {
}

// Retire the three histories before the four Strings and real module lifetime base in reverse member order.
ScGraphInfo::~ScGraphInfo() = default;

// Publish the original secondary graph identity for either accepted IID and retain through the real lifetime primary.
// Unsupported requests clear the output.
bool ScGraphInfo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScGraphInfoView);
  HANDLE_QUERY_INTERFACE(IID_ScGraphInfoView, ScGraphInfoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScGraphInfo::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 ScGraphInfo::Release() {
  return ScMiscRefCount::Release();
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
  monthly.push_front(value);
  monthly.pop_back();
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
  yearly.push_front(value);
  yearly.pop_back();
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
  decades.push_front(value);
  decades.pop_back();
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
