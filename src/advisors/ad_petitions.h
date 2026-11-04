#ifndef SC3_ADVISORS_AD_PETITIONS_H
#define SC3_ADVISORS_AD_PETITIONS_H

#include "advisors/ad_advice.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "resources/resource.h"

class CtCity;

// Queued petition records have a full DWORD ID and replacement key, distinct from ordinary advice's byte category.
class AdPetition {
public:
  AdPetition();
  ~AdPetition();
  bool Read(CtCity *city, u32 version, u32 token_version, RsStream *stream);
  bool Write(CtCity *city, RsStream *stream);
  u32 id, replacement, notification, unknown0c;
  u8 flag16, delay, age, dismissed, flag20, replaceable, redisplay;
  AdTokenLink *tokens;
};

struct AdPetitionLink {
  AdPetitionLink *next, *previous;
};

struct AdPetitionNode : AdPetitionLink {
  AdPetition *petition;
};

// Caller-owned header prefix written by233EA: a transferred zero-reference String followed by the full petitioner ID.
// The SDK only forward-declares this record; no constructor, destructor or additional original fields are asserted.
class AdPetitionerHeader {
public:
  ScStringView *text;
  u32 id;
};

class EcBudgetLayer;
class CtResidentialLayer;
class CtAuraLayer;
class CtOrdinanceLayer;
class CtNeighborsLayer;

class AdPetitionerHeader;
class AdPetitionerRecord;
class AdPetitionerQueue;
class AdToken;
class AdTextHandle;
class AdBudgetAdvisor;
class AdTransportationAdvisor;
class AdPublicSafetyAdvisor;
class AdEnvironmentAdvisor;
class AdCityPlanningAdvisor;
class AdUtilitiesAdvisor;
class AdDemographicsAdvisor;

struct AdForcedPetitionLink {
  AdForcedPetitionLink *next, *previous;
};

struct AdForcedPetitionNode : AdForcedPetitionLink {
  u32 id;
};

// Interface declarations adapted from cISC3PetitionerManager.h, Copyright (C) 2025 Nicholas Hayes.
// LGPL-2.1-or-later; see SDK-LICENSE.txt.
// Primary petitioner interface, matched to the SDK and installed10031504 slots.
class AdPetitionerManager : public ScUnknown {
public:
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual bool DoAdvisorSimulation() = 0;
  virtual u32 GetPetitionerQueueSize() = 0;
  virtual bool GetPetitionerHeaders(AdPetitionerHeader **out) = 0;
  virtual bool GetPetitionerStructureFromID(u32 id, AdPetitionerRecord *out) = 0;
  virtual bool GetPetitionerStructure(u32 id, AdPetitionerRecord *out) = 0;
  virtual bool PetitionerButtonPressed(u32 id, u32 button) = 0;
  virtual bool GetNeighborDealPetitioner(u8 neighbor, AdPetitionerRecord *out) = 0;
  // Installed1001FDEC returns the selected group ID, disagreeing with the SDK's bool spelling.
  virtual u32 GetPetitionerStringGroup(u32 type, u32 id) = 0;
};

// The manager implements separate petitioner, city-agent and city-layer identities rather than CtAgentBase.
// Original vectors: eleven u32 groups at188 (100226C4/100226D8/10022946) and String values at320 (10022654).
class AdPetitioners : public AdPetitionerManager, public CtAgent, public CtLayer {
public:
  AdPetitioners();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  CtAgent *AsISC3CityAgent() override;
  bool DoAdvisorSimulation() override;
  bool DoMessage(ScMessage *message) override;
  u32 GetPetitionerQueueSize() override;
  u32 GetPetitionerStringGroup(u32 type, u32 id) override;
  bool GetPetitionerHeaders(AdPetitionerHeader **out) override;
  bool GetPetitionerStructureFromID(u32 id, AdPetitionerRecord *out) override;
  bool GetPetitionerStructure(u32 id, AdPetitionerRecord *out) override;
  bool PetitionerButtonPressed(u32 id, u32 button) override;
  bool GetNeighborDealPetitioner(u8 type, AdPetitionerRecord *out) override;
  virtual ~AdPetitioners();
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  u32 GetLayerType() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  using CtLayer::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  u8 RefreshCityData();
  u8 EvaluateOrdinances();
  bool ShowResponse(u32 id, u32 button);
  u8 ForcePetition(u32 id);
  u8 ClearForcedPetitions();
  u8 ReplayForcedPetitions();
  bool LoadConfiguration();
  bool MarkForSimulation();
  bool AddToken(u32 id, AdToken token);
  bool AddPetition(u32 id, u32 replacement, u8 delay);
  bool AddPetition(u32 id, u32 replacement, u8 flag20, u8 delay);
  bool AddPetition(u32 id, u32 replacement, u8 flag20, u8 replaceable, u8 delay);
  bool AddReplaceablePetition(u32 id, u32 replacement, u8 delay);
  bool ResolveText(u32 id, u32 type, AdTextHandle *out);
  AdBudgetAdvisor *budget;
  AdCityPlanningAdvisor *planning;
  AdDemographicsAdvisor *demographics;
  AdEnvironmentAdvisor *environment;
  AdPublicSafetyAdvisor *safety;
  AdTransportationAdvisor *transportation;
  AdUtilitiesAdvisor *utilities;
  AdPetitionerQueue *queue;
  CtCity *simulation_city;
  EcBudgetLayer *city_budget;
  CtResidentialLayer *residential;
  CtAuraLayer *aura_layer;
  CtOrdinanceLayer *ordinance_service;
  CtNeighborsLayer *neighbors;
  u32 population_for_gambling, education_for_clinic;
  u32 power_conservation_year, shuttle_service_year, electronics_job_fair_year, nuclear_free_year;
  u32 parking_fines_year, water_meters_year, tire_recycling_year, tourist_promotion_year;
  u32 conservation_corps_year, paperwork_reduction_year, public_access_tv_year, backyard_compost_year;
  u32 industrial_waste_year, aerospace_tax_year, biotech_tax_year, clean_industry_year, stairwell_lighting_year;
  u32 homeless_shelter_population, pro_reading_population;
  u8 max_gambling_aura, pro_reading_aura;
  u32 year, population;
  // The sampler stores AL; the original signed aura comparisons remain explicit at the rule use sites.
  u8 mayor_rating, workforce_eq, workforce_le;
  i64 treasury;
  u8 simulation_pending;
  AdForcedPetitionLink *forced_ids;
  u32 references;
  ScArray<u32> string_groups[11];
  ScArray<ScString *> extra_info;
  u32 string_group_index;
};

// Proven output prefix of the SDK's forward-declared petitioner record, through word sixteen.
// Text slots correspond to resource types5/3/4/1/2/6/7; original construction and any later fields remain unknown.
class AdPetitionerRecord {
public:
  RsKey portrait;
  u32 id;
  ScStringView *text[7];
  RsKey advisor_portrait;
  u32 advisor_id;
  ScStringView *advisor_name, *extra_text;
};

class AdPetitionerManager;
class AdPetitionerHeader;
class AdPetitionerRecord;

// The original petitioner queue owns a separately allocated list header and distinct AdPetition values.
struct AdPetitionerList {
  AdPetitionLink *head;
};

class AdPetitionerQueue {
public:
  AdPetitionerQueue(AdPetitionerManager *manager);
  virtual ~AdPetitionerQueue();
  // Native output phase of233EA; its private original return declaration is unknown.
  void WriteHeaders(AdPetitionerHeader **out);
  bool GetStructure(u32 id, AdPetitionerRecord *out);
  // Read configured extra-info INI metadata using the separate full-width ID and two resource-key outputs.
  bool ReadPortraits(u32 id, RsKey *portrait, RsKey *advisor_portrait);
  u32 GetPortraitInstance(u32 id);
  bool CreateOutdatedRecord(AdPetitionerRecord *out);
  // Native clearing phase of23354; its unconsumed private source return remains unknown.
  void Clear();
  bool Advance();
  bool AddToken(u32 id, const AdToken *token);
  u32 CreateNotification(u32 id, u32 type, AdPetition *petition);
  bool RecreateNotification(u32 id, u32 type);
  bool Respond(u32 id, u32 button);
  // Native append phase of22FD2; the unconsumed original private return declaration remains unknown.
  void Append(AdPetition *petition);
  bool Reactivate(u32 id);
  bool ContainsActive(u32 id);
  u32 GetIdForReplacement(u32 replacement);
  bool Dismiss(u32 id);
  bool ForceDismiss(u32 id);
  bool Remove(u32 id);
  bool ResetRedisplay();
  bool Read(CtCity *city, u32 version, RsStream *stream);
  bool Write(CtCity *city, RsStream *stream);
  AdPetitionerList *records;
  AdPetitionerManager *manager;
  ScLock *lock;
};

#endif
