#ifndef SC3_ADVISORS_AD_ADVICE_H
#define SC3_ADVISORS_AD_ADVICE_H

#include "advisors/ad_module.h"
#include "advisors/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "resources/resource.h"

class ScLock;
class ScMessageServer;
class AdAdvisor;
class AdHeaderRecord;
class AdMessageRecord;
class RsStream;
class CtCity;

// The original token value has one string and three scalar substitution arguments.
class AdToken {
public:
  AdToken();
  AdToken(u32 kind, const ScStringView *name, i32 value);
  AdToken(u32 kind, const ScStringView *name, u32 id);
  AdToken(const AdToken &source);
  virtual ~AdToken();
  u8 Read(u32 version, RsStream *stream);
  u8 Write(RsStream *stream);
  u32 kind;
  ScString text;
  u32 first, second;
  i32 third;
};

struct AdTokenLink {
  AdTokenLink *next, *previous;
};

struct AdTokenNode : AdTokenLink {
  AdToken value;
};

// Advice records are owned by queue nodes. Flag bytes retain their raw values.
class AdAdvice {
public:
  AdAdvice();
  ~AdAdvice();
  bool Read(CtCity *city, u32 version, u32 token_version, RsStream *stream);
  bool Write(CtCity *city, RsStream *stream);

  u8 category;
  u32 id, notification, replacement;
  u8 flag16, delay, age, dismissed, flag20, replaceable, redisplay;
  AdTokenLink *tokens;
};

struct AdAdviceLink {
  AdAdviceLink *next, *previous;
};

struct AdAdviceNode : AdAdviceLink {
  AdAdvice *advice;
};

class AdAdviceQueue {
public:
  AdAdviceQueue(AdAdvisor *advisor, u8 category);
  virtual ~AdAdviceQueue();
  void Configure(u8 category);
  bool Read(CtCity *city, u32 version, RsStream *stream);
  bool Write(CtCity *city, RsStream *stream);
  void Add(AdAdvice *advice);
  bool AddToken(u32 id, const AdToken &token);
  u32 GetMood();
  u32 CreateNotification(u32 id);
  bool Contains(u32 id);
  bool Reactivate(u32 id);
  bool Remove(u32 id);
  bool RemoveReplacement(u32 replacement);
  void Clear();
  bool Dismiss(u32 id);
  void GetHeaders(u32 *count, AdHeaderRecord **out);
  bool GetMessageText(u32 id, ScStringView *out);
  bool GetTitle(u32 id, ScStringView *out);
  bool Advance();
  bool ResetRedisplay();
  bool RecreateNotification(u32 id);
  bool GetStructureByReplacement(u32 replacement, AdMessageRecord **out);
  bool GetStructure(u32 id, AdMessageRecord **out);
  bool CreateOutdatedMessage(AdMessageRecord **out);
  AdAdviceLink *head;
  AdAdvisor *advisor;
  u8 category;
  u32 title_group, message_group, header_group, notification_group;
  ScLock *lock;
};

ScMessageServer *AdGetMessageServer();
bool AD_STDCALL AdDismissNotification(u32 notification);
// Original INI-backed lookup; writes zero after both base and Gold mappings are unavailable.
bool AdLoadMood(u32 id, u8 *mood);

// Interface declarations adapted from sc3k-gzcom-dll cISC3BaseAdvisor.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See SDK-LICENSE.txt.
class CtAgent;
class AdHeaderRecord;
class AdMessageRecord;

// Unlimited advisor interface, matched to the installed base vtable at 1003127C.
// Unlimited places ordinance methods before help; the PC SDK reverses those groups. Mutable SDK refs use pointers.
class AdAdvisor : public ScUnknown {
public:
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual void GetName(ScStringView *out) = 0;
  virtual void SetName(ScStringView *name) = 0;
  virtual bool GetNameAdvice(ScStringView *out) = 0;
  virtual bool SetNameAdvice(ScStringView *name) = 0;
  virtual bool GetNameOrdinance(ScStringView *out) = 0;
  virtual bool SetNameOrdinance(ScStringView *name) = 0;
  virtual bool GetNameBriefings(ScStringView *out) = 0;
  virtual bool SetNameBriefings(ScStringView *name) = 0;
  virtual bool GetPersonalName(ScStringView *out) = 0;
  virtual bool SetPersonalName(ScStringView *name) = 0;
  virtual RsKey *GetSmallAdvisorPicture() = 0;
  virtual RsKey *GetLargeAdvisorPicture() = 0;
  virtual bool SetSmallAdvisorPicture(const RsKey &key) = 0;
  virtual bool SetLargeAdvisorPicture(const RsKey &key) = 0;
  virtual bool DoAdvisorSimulation() = 0;
  virtual u32 GetAdvisorID() = 0;
  virtual u32 GetAdvisorAverageMood() = 0;
  virtual u32 GetOutdatedResponseID() = 0;
  virtual u32 GetAdviceQueueSize() = 0;
  virtual bool GetAdviceHeaders(u32 *count, AdHeaderRecord **out) = 0;
  virtual bool GetAdviceMessageFromID(u32 id, ScStringView *out) = 0;
  virtual bool GetAdviceStructureFromID(u32 id, AdMessageRecord **out) = 0;
  virtual u32 GetOrdinanceQueueSize() = 0;
  virtual bool GetOrdinanceHeaders(u32 *count, AdHeaderRecord **out) = 0;
  virtual bool GetOrdinanceTitleFromID(u32 id, ScStringView *out) = 0;
  virtual bool GetOrdinanceMessageFromID(u32 id, ScStringView *out) = 0;
  virtual u32 GetHelpQueueSize() = 0;
  virtual bool GetHelpHeaders(u32 *count, AdHeaderRecord **out) = 0;
  virtual bool GetHelpTitleFromID(u32 id, ScStringView *out) = 0;
  virtual bool GetHelpMessageFromID(u32 id, ScStringView *out) = 0;
  virtual bool DoMessage(ScMessage *message) = 0;
  virtual bool DoQueryInfo(ScMessage *message, ScUnknown *source) = 0;
  virtual void GetSimulationData() = 0;
  virtual bool CheckOnAdvice() = 0;
};

class AdAdviceQueue;
class AdPetitionerManager;
class AdToken;

struct AdForcedLink {
  AdForcedLink *next, *previous;
};

struct AdForcedNode : AdForcedLink {
  u32 advice;
};

// The original shared advisor state has distinct Advisor, CityAgent and CityLayer bases.
// Concrete domains supply their remaining simulation behavior through the inherited pure interfaces.
class AdBaseAdvisor : public AdAdvisor, public CtAgentBase, public CtLayer {
public:
  AdBaseAdvisor();
  ~AdBaseAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  CtAgent *AsISC3CityAgent() override;
  void GetName(ScStringView *out) override;
  void SetName(ScStringView *name) override;
  bool GetNameAdvice(ScStringView *out) override;
  bool SetNameAdvice(ScStringView *name) override;
  bool GetNameOrdinance(ScStringView *out) override;
  bool SetNameOrdinance(ScStringView *name) override;
  bool GetNameBriefings(ScStringView *out) override;
  bool SetNameBriefings(ScStringView *name) override;
  bool GetPersonalName(ScStringView *out) override;
  bool SetPersonalName(ScStringView *name) override;
  RsKey *GetSmallAdvisorPicture() override;
  RsKey *GetLargeAdvisorPicture() override;
  bool SetSmallAdvisorPicture(const RsKey &key) override;
  bool SetLargeAdvisorPicture(const RsKey &key) override;
  u32 GetAdvisorAverageMood() override;
  u32 GetOutdatedResponseID() override;
  u32 GetAdviceQueueSize() override;
  u32 GetHelpQueueSize() override;
  u32 GetOrdinanceQueueSize() override;
  bool GetAdviceHeaders(u32 *count, AdHeaderRecord **out) override;
  bool GetHelpHeaders(u32 *count, AdHeaderRecord **out) override;
  bool GetOrdinanceHeaders(u32 *count, AdHeaderRecord **out) override;
  bool GetAdviceMessageFromID(u32 id, ScStringView *out) override;
  bool GetAdviceStructureFromID(u32 id, AdMessageRecord **out) override;
  bool GetHelpTitleFromID(u32 id, ScStringView *out) override;
  bool GetHelpMessageFromID(u32 id, ScStringView *out) override;
  bool GetOrdinanceTitleFromID(u32 id, ScStringView *out) override;
  bool GetOrdinanceMessageFromID(u32 id, ScStringView *out) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  using CtLayer::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool HasAdvice(u32 id);
  bool ReactivateAdvice(u32 id);
  bool DismissAdvice(u32 id);
  bool AddHelp(u32 id);
  bool AddOrdinance(u32 id);
  bool AddAdvice(u32 id);
  bool AddAdvice(u32 id, u32 replacement);
  bool AddAdvice(u32 id, u32 replacement, u8 delay);
  bool AddAdvice(u32 id, u32 replacement, u8 delay, u8 flag20, u8 category);
  bool AddAdvice(u32 id, u32 replacement, u8 delay, u8 flag20, u8 replaceable, u8 category);
  bool AddDismissibleAdvice(u32 id, u32 replacement, u8 delay);
  bool AddReplaceableAdvice(u32 id, u32 replacement, u8 delay);
  bool AddCategoryOneAdvice(u32 id, u32 replacement, u8 delay);
  bool AddForcedAdvice(u32 id);
  bool AddAdviceToken(u32 id, AdToken token);
  // Native forwarding phase of1DBDF; its unconsumed private nominal return remains unresolved.
  void RecreateAdviceNotification(u32 id);
  bool AddReplaceablePetitioner(u32 id, u32 replacement, u8 delay);
  bool ContainsActivePetitioner(u32 id);
  bool AddPetitioner(u32 id, u32 replacement, u8 delay);
  bool AddPetitioner(u32 id, u32 replacement);
  bool AddDismissiblePetitioner(u32 id, u32 replacement);
  bool AddPetitionerToken(u32 id, AdToken token);
  u32 GetPetitionerIdForReplacement(u32 replacement);
  bool ReactivatePetitioner(u32 id);
  bool RemovePetitioner(u32 id);
  bool ProcessForcedAdvice();
  bool ClearForcedAdvice();
  ScString name_advice, name_ordinance, name_briefings, personal_name;
  RsKey small_picture, large_picture;
  u32 unknown7c; // Not read by the recovered base methods and not initialized by the original constructor.
  AdAdviceQueue *advice, *ordinances, *help;
  u32 outdated_response; // Initialized by each concrete domain advisor, not its base constructor.
  AdForcedLink *forced;
  CtCity *city;
  AdPetitionerManager *petitioners;
  u8 simulation_pending;
};

#endif
