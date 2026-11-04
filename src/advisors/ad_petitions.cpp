#include "advisors/ad_petitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_advisors.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_ini.h"
#include "advisors/ad_module.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_scenario.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_tornado.h"
#include "economy/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"

// Accepting the original cash-grant petition deposits this amount into the city budget.
enum AdPetitionCashValue : i32 {
  kAdPetitionCashGrant = 0x186a0,
};

// Petitioner descriptions and rejection replies use this installed text group.
enum AdPetitionTextGroup : u32 {
  kAdPetitionDescriptionTextGroup = 0x826dd2d0,
};

enum AdPetitionerArchiveKey : u32 {
  kAdPetitionerStateRecordGroup = 0x02619041,
};

AdPetition::AdPetition()
    : id(0), replacement(0), notification(0), unknown0c(0), flag16(0), delay(0), age(0), dismissed(0), flag20(0),
      replaceable(0), redisplay(0) {
  tokens = new AdTokenLink;
  tokens->next = tokens;
  tokens->previous = tokens;
}

// This destructor is folded with ordinary advice despite the classes' distinct first-word contracts.
AdPetition::~AdPetition() {
  AdTokenLink *node = tokens->next;
  while (node != tokens) {
    AdTokenLink *next = node->next;
    delete static_cast<AdTokenNode *>(node);
    node = next;
  }
  delete tokens;
}

bool AdPetition::Read(CtCity *, u32 version, u32 token_version, RsStream *stream) {
  if (version != 1)
    return false;
  if (!stream->ReadU32(&id) || !stream->ReadU32(&replacement) || !stream->ReadU32(&notification) ||
      !stream->ReadU32(&unknown0c) || !stream->ReadU8(&flag16) || !stream->ReadU8(&delay) || !stream->ReadU8(&age))
    return false;
  u8 packed = 0, result = stream->ReadU8(&packed);
  dismissed = (packed & 1) != 0;
  flag20 = (packed & 2) != 0;
  replaceable = (packed & 4) != 0;
  redisplay = (packed & 8) != 0;
  if (!result)
    return false;
  u32 count = 0;
  result = stream->ReadU32(&count);
  for (u32 index = 0; result && index < count; ++index) {
    AdToken token;
    result = token.Read(token_version, stream);
    if (result) {
      AdTokenNode *node = new AdTokenNode{{tokens, tokens->previous}, token};
      node->previous->next = node;
      node->next->previous = node;
    }
  }
  return result != 0;
}

bool AdPetition::Write(CtCity *, RsStream *stream) {
  if (!stream->WriteU32(id) || !stream->WriteU32(replacement) || !stream->WriteU32(notification) ||
      !stream->WriteU32(unknown0c) || !stream->WriteU8(flag16) || !stream->WriteU8(delay) || !stream->WriteU8(age))
    return false;
  u8 packed =
      static_cast<u8>((dismissed != 0) | ((flag20 != 0) << 1) | ((replaceable != 0) << 2) | ((redisplay != 0) << 3));
  if (!stream->WriteU8(packed))
    return false;
  u32 count = 0;
  for (AdTokenLink *node = tokens->next; node != tokens; node = node->next)
    ++count;
  u8 result = stream->WriteU32(count);
  if (count) {
    if (!result)
      return false;
    AdTokenLink *head = tokens;
    for (AdTokenLink *node = head->next; node != head; node = node->next) {
      if (!result)
        return false;
      result = static_cast<AdTokenNode *>(node)->value.Write(stream);
    }
  }
  return result != 0;
}

void AdPetitionerQueue::WriteHeaders(AdPetitionerHeader **out) {
  ScLock *region = lock;
  region->Lock();
  {
    AdTextHandle resource;
    for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
      AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
      if (record->dismissed)
        continue;
      static_cast<AdPetitioners *>(manager)->ResolveText(record->id, 3, &resource);
      ScString *text = new ScString;
      u32 count = 0;
      for (AdTokenLink *token = record->tokens->next; token != record->tokens; token = token->next)
        ++count;
      const AdToken **tokens = count ? new const AdToken *[count] : nullptr;
      u32 index = 0;
      for (AdTokenLink *token = static_cast<AdPetitionNode *>(node)->petition->tokens->next; token != record->tokens;
          token = token->next)
        tokens[index++] = &static_cast<AdTokenNode *>(token)->value;
      resource.CopyTo(text, tokens, count);
      (*out)->text = text;
      (*out)->id = record->id;
      ++out;
      delete[] tokens;
    }
  }
  region->Unlock();
}

// Type eight carries the petitioner action; release text before the resource handle and return the genuine notification
// output.
u32 AdPetitionerQueue::CreateNotification(u32 id, u32 type, AdPetition *petition) {
  AdTextHandle resource;
  static_cast<AdPetitioners *>(manager)->ResolveText(id, type, &resource);
  if (!resource.IsValid())
    return 0;
  ScString *text = new ScString;
  if (petition) {
    u32 count = 0;
    for (AdTokenLink *node = petition->tokens->next; node != petition->tokens; node = node->next)
      ++count;
    const AdToken **tokens = count ? new const AdToken *[count] : nullptr;
    u32 index = 0;
    for (AdTokenLink *node = petition->tokens->next; node != petition->tokens; node = node->next)
      tokens[index++] = &static_cast<AdTokenNode *>(node)->value;
    resource.CopyTo(text, tokens, count);
    delete[] tokens;
  } else
    resource.CopyTo(text);
  RsKey key{};
  resource.GetKey(&key);
  text->AddRef();
  AdNotification notification = {
      0, key.group, key.instance, text, type == 8 ? kWnMsg_PetitionerDialog : 0u, type == 8 ? id : 0u, 0, 0};
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0};
  if (AdGetMessageServer())
    AdGetMessageServer()->MessageSend(message);
  text->Release();
  return notification.notification;
}

// Fill the original outdated-petitioner portrait, zero ID and optional text pointers under the saved recursive lock.
// Resolve ID250 types5/3/4 into separate zero-reference Strings even when missing; leave advisor portrait/word14 untouched.
bool AdPetitionerQueue::CreateOutdatedRecord(AdPetitionerRecord *out) {
  ScLock *region = lock;
  region->Lock();
  {
    AdTextHandle resource;
    out->id = 0;
    for (u32 index = 3; index < 7; ++index)
      out->text[index] = nullptr;
    out->advisor_name = nullptr;
    out->extra_text = nullptr;
    out->portrait = {kRsImageResourceType, kRsAdvisorPortraitGroup, 0xff04};
    ScString *text = new ScString;
    static_cast<AdPetitioners *>(manager)->ResolveText(250, 5, &resource);
    resource.CopyTo(text);
    out->text[0] = text;
    text = new ScString;
    static_cast<AdPetitioners *>(manager)->ResolveText(250, 3, &resource);
    resource.CopyTo(text);
    out->text[1] = text;
    text = new ScString;
    static_cast<AdPetitioners *>(manager)->ResolveText(250, 4, &resource);
    resource.CopyTo(text);
    out->text[2] = text;
  }
  region->Unlock();
  return true;
}

// Search configured petitioner extra-info files in order through the actual loose/package INI reader.
// The first matching full-width ID supplies comma-separated portrait IDs; a zero advisor ID clears only its resource kind.
bool AdPetitionerQueue::ReadPortraits(u32 id, RsKey *portrait, RsKey *advisor_portrait) {
  AdPetitioners *owner = static_cast<AdPetitioners *>(manager);
  for (u32 index = 0; index < owner->extra_info.size; ++index) {
    ScString path;
    AdIni reader;
    AdGetApplication()->GetAppDataDirectory(&path);
    path.ScString::Append("Sys\\", 4);
    const ScString &file = *owner->extra_info.data[index];
    path.ScString::Append(file.begin, static_cast<u32>(file.end - file.begin));
    reader.SetPath(path);
    AdGetApplication()->GetAppDataDirectory(&path);
    path.ScString::Append("\\Sys\\SYS.PAK", 12);
    reader.SetCompressedPath(path);
    char key_bytes[11];
    snprintf(key_bytes, sizeof(key_bytes), "0x%08X", id);
    ScString key(key_bytes, 10), value, section("PetitionerExtraInfo", 19);
    if (!reader.ReadEntry(section, key, &value))
      continue;
    u32 count = static_cast<u32>(value.end - value.begin), split = 0;
    while (split < count && value.begin[split] != ',')
      ++split;
    ScString first;
    if (split < count) {
      first.ScString::FromChar(value.begin, split);
      value.ScString::Erase(0, split + 1);
    }
    portrait->type = kRsImageResourceType;
    portrait->group = kRsAdvisorPortraitGroup;
    portrait->instance = AdParseIniNumber(first.begin);
    advisor_portrait->type = kRsImageResourceType;
    advisor_portrait->group = kRsAdvisorPortraitGroup;
    advisor_portrait->instance = AdParseIniNumber(value.begin);
    if (!advisor_portrait->instance) {
      advisor_portrait->group = 0;
      advisor_portrait->type = 0;
    }
    return true;
  }
  *portrait = {};
  *advisor_portrait = {};
  return false;
}

// A missing petition receives the nested outdated fallback and returns false; metadata-file reading remains a direct
// dependency.
bool AdPetitionerQueue::GetStructure(u32 id, AdPetitionerRecord *out) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  {
    AdTextHandle resource;
    for (u32 index = 3; index < 7; ++index)
      out->text[index] = nullptr;
    out->advisor_name = nullptr;
    out->extra_text = nullptr;
    AdPetition *record = nullptr;
    for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
      AdPetition *candidate = static_cast<AdPetitionNode *>(node)->petition;
      if (candidate->id == id && !candidate->dismissed) {
        record = candidate;
        break;
      }
    }
    if (!record)
      CreateOutdatedRecord(out);
    else {
      u32 count = 0;
      for (AdTokenLink *node = record->tokens->next; node != record->tokens; node = node->next)
        ++count;
      const AdToken **tokens = count ? new const AdToken *[count] : nullptr;
      u32 index = 0;
      for (AdTokenLink *node = record->tokens->next; node != record->tokens; node = node->next)
        tokens[index++] = &static_cast<AdTokenNode *>(node)->value;
      out->id = id;
      ScString *text = new ScString;
      {
        AdTextHandle first(id, kAdPetitionDescriptionTextGroup);
        resource.Assign(first);
      }
      AdPetitioners *owner = static_cast<AdPetitioners *>(manager);
      owner->ResolveText(id, 5, &resource);
      resource.CopyTo(text, tokens, count);
      out->text[0] = text;
      text = new ScString;
      owner->ResolveText(id, 3, &resource);
      resource.CopyTo(text, tokens, count);
      out->text[1] = text;
      text = new ScString;
      owner->ResolveText(id, 4, &resource);
      resource.CopyTo(text, tokens, count);
      out->text[2] = text;
      const u32 optional_types[] = {1, 2, 6, 7};
      for (u32 slot = 0; slot < 4; ++slot) {
        owner->ResolveText(id, optional_types[slot], &resource);
        if (resource.IsValid()) {
          text = new ScString;
          resource.CopyTo(text, tokens, count);
          out->text[slot + 3] = text;
        } else
          out->text[slot + 3] = nullptr;
      }
      if (ReadPortraits(id, &out->portrait, &out->advisor_portrait)) {
        const u32 portraits[] = {0xff01, 0xff02, 0xff12, 0xff07, 0xff0b, 0xff08, 0xff0e, 0xff15, 0xff11, 0xff0c, 0xff0f,
            0xff09, 0xff13, 0xff06, 0xff0a, 0xff0d};
        u32 portrait = out->portrait.instance;
        if (portrait >= 2 && portrait <= 17)
          out->portrait.instance = portraits[portrait - 2];
        else if (portrait == 18) {
          u32 neighbor = 0;
          for (AdTokenLink *node = record->tokens->next; node != record->tokens; node = node->next) {
            AdToken *token = &static_cast<AdTokenNode *>(node)->value;
            if (token->kind == 4)
              neighbor = token->first;
          }
          if (neighbor <= 3) {
            const u32 neighbor_portraits[] = {0xff03, 0xff05, 0xff10, 0xff14};
            out->portrait.instance = neighbor_portraits[neighbor];
          }
        } else if (portrait == 19)
          out->portrait.instance = 0xff04;
        AdBaseAdvisor *advisor = nullptr;
        switch (out->advisor_portrait.instance) {
        case 1:
          advisor = owner->utilities;
          break;
        case 2:
          advisor = owner->budget;
          break;
        case 3:
          advisor = owner->planning;
          break;
        case 4:
          advisor = owner->environment;
          break;
        case 5:
          advisor = owner->safety;
          break;
        case 6:
          advisor = owner->transportation;
          break;
        case 7:
          advisor = owner->demographics;
          break;
        }
        if (advisor) {
          ScString *name = new ScString;
          advisor->GetNameAdvice(name);
          out->advisor_name = name;
          out->advisor_portrait = *advisor->GetLargeAdvisorPicture();
        }
      }
      ScString *extra = nullptr;
      owner->ResolveText(id, 0, &resource);
      if (resource.IsValid()) {
        extra = new ScString;
        resource.CopyTo(extra, tokens, count);
      }
      out->extra_text = extra;
      delete[] tokens;
      found = true;
    }
  }
  region->Unlock();
  return found;
}

bool AdPetitionerQueue::RecreateNotification(u32 id, u32 type) {
  AdTextHandle resource;
  static_cast<AdPetitioners *>(manager)->ResolveText(id, type, &resource);
  if (!resource.IsValid())
    return false;
  AdPetitionNode *selected = nullptr;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id && !record->dismissed) {
      selected = static_cast<AdPetitionNode *>(node);
      break;
    }
  }
  if (!selected)
    return false;
  AdDismissNotification(selected->petition->notification);
  u32 count = 0;
  for (AdTokenLink *node = selected->petition->tokens->next; node != selected->petition->tokens; node = node->next)
    ++count;
  const AdToken **tokens = count ? new const AdToken *[count] : nullptr;
  u32 index = 0;
  for (AdTokenLink *node = selected->petition->tokens->next; node != selected->petition->tokens; node = node->next)
    tokens[index++] = &static_cast<AdTokenNode *>(node)->value;
  ScString *text = new ScString;
  resource.CopyTo(text, tokens, count);
  delete[] tokens;
  RsKey key{};
  resource.GetKey(&key);
  text->AddRef();
  AdNotification notification = {
      0, key.group, key.instance, text, type == 8 ? kWnMsg_PetitionerDialog : 0u, type == 8 ? id : 0u, 0, 0};
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0};
  if (AdGetMessageServer())
    AdGetMessageServer()->MessageSend(message);
  text->Release();
  selected->petition->notification = notification.notification;
  return true;
}

// Map buttons one and two to response text types nine and ten; ignore the refresh result and report a recognized button.
// Zero and all other button values return false without resolving text or searching records.
bool AdPetitionerQueue::Respond(u32 id, u32 button) {
  if (button == 1)
    RecreateNotification(id, 9);
  else if (button == 2)
    RecreateNotification(id, 10);
  else
    return false;
  return true;
}

// Failed records remain unlinked as in the original; previously appended records and notification effects are retained.
bool AdPetitionerQueue::Read(CtCity *city, u32 version, RsStream *stream) {
  if (!records || version != 1)
    return false;
  u32 count = 0, record_version = 0, token_version = 0;
  if (!stream->ReadU32(&count) || !stream->ReadU32(&record_version) || !stream->ReadU32(&token_version))
    return false;
  for (u32 index = 0; index < count; ++index) {
    AdPetition *record = new AdPetition;
    if (!record->Read(city, record_version, token_version, stream))
      return false;
    Append(record);
  }
  return true;
}

// Preserve the saved-count gate, cached sentinel and original null-record skip without fabricating successful records.
bool AdPetitionerQueue::Write(CtCity *city, RsStream *stream) {
  if (!records)
    return false;
  u32 count = 0;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next)
    ++count;
  if (!stream->WriteU32(count) || !stream->WriteU32(1))
    return false;
  u8 result = stream->WriteU32(1);
  if (count) {
    if (!result)
      return false;
    AdPetitionLink *head = records->head;
    for (AdPetitionLink *node = head->next; node != head; node = node->next) {
      if (!result)
        return false;
      AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
      if (record)
        result = record->Write(city, stream);
    }
  }
  return result != 0;
}

// Restore the insertion effects of 22FD2 insertion effects without exposing the unproved private source return as a game result.
// Resolve notifications before loading/acquiring the lock, then publish the ID and redisplay byte before linking ownership.
void AdPetitionerQueue::Append(AdPetition *petition) {
  u32 notification = 0;
  if (!petition->dismissed)
    notification = CreateNotification(petition->id, 8, petition);
  ScLock *region = lock;
  region->Lock();
  petition->notification = notification;
  petition->redisplay = 1;
  AdPetitionNode *node = new AdPetitionNode;
  node->petition = petition;
  node->next = records->head;
  node->previous = records->head->previous;
  node->previous->next = node;
  records->head->previous = node;
  region->Unlock();
}

// Initialize borrowed services and empty retained lists, then install the original petitioner ordinance thresholds.
// Other simulation values, pending byte and load cursor await original Init; native arrays start empty.
AdPetitioners::AdPetitioners() {
  queue = nullptr;
  simulation_city = nullptr;
  city_budget = nullptr;
  residential = nullptr;
  aura_layer = nullptr;
  ordinance_service = nullptr;
  neighbors = nullptr;
  population = 0;
  treasury = 0;
  AdForcedPetitionNode *sentinel = new AdForcedPetitionNode;
  sentinel->next = sentinel;
  sentinel->previous = sentinel;
  forced_ids = sentinel;
  references = 0;
  power_conservation_year = 1972;
  shuttle_service_year = 1945;
  electronics_job_fair_year = 1988;
  nuclear_free_year = 2000;
  parking_fines_year = 1925;
  water_meters_year = 1935;
  tire_recycling_year = 1945;
  tourist_promotion_year = 1949;
  conservation_corps_year = 1953;
  paperwork_reduction_year = 1962;
  public_access_tv_year = 1982;
  backyard_compost_year = 1979;
  industrial_waste_year = 1952;
  aerospace_tax_year = 1967;
  biotech_tax_year = 1984;
  clean_industry_year = 1971;
  stairwell_lighting_year = 1931;
  homeless_shelter_population = 10000;
  pro_reading_population = 10000;
  population_for_gambling = 5000;
  max_gambling_aura = 10;
  pro_reading_aura = 10;
  education_for_clinic = 60;
}

// Retire extra-info Strings in original forward order, then group buffers from last to first before forced-ID nodes.
// Empty native array storage afterward so automatic member cleanup cannot release the same buffers again.
AdPetitioners::~AdPetitioners() {
  for (u32 index = 0; index < extra_info.size; ++index)
    delete extra_info.data[index];
  free(extra_info.data);
  extra_info.data = nullptr;
  extra_info.size = extra_info.capacity = 0;
  for (u32 index = 11; index != 0; --index) {
    ScArray<u32> *group = &string_groups[index - 1];
    free(group->data);
    group->data = nullptr;
    group->size = group->capacity = 0;
  }
  AdForcedPetitionLink *node = forced_ids->next;
  while (node != forced_ids) {
    AdForcedPetitionLink *next = node->next;
    delete static_cast<AdForcedPetitionNode *>(node);
    node = next;
  }
  delete static_cast<AdForcedPetitionNode *>(forced_ids);
}

// Select the original primary, city-agent or city-layer identity and retain through the shared virtual method.
// An unknown IID clears the caller's output pointer and returns false.
bool AdPetitioners::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AdPetitionerManager);
  HANDLE_QUERY_INTERFACE(IID_AdPetitionerManager, AdPetitionerManager);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the shared count before retaining the actual DLL director and return the live count after its callback.
u32 AdPetitioners::AddRef() {
  ++references;
  AdGetDirector()->AddRef();
  return references;
}

// Drop the DLL reference before final virtual deletion, including the original zero-count final-release path.
u32 AdPetitioners::Release() {
  if (references > 1) {
    --references;
    AdGetDirector()->RemoveRef();
    return references;
  }
  AdGetDirector()->RemoveRef();
  delete this;
  return 0;
}

// Borrow the actual secondary city-agent subobject without another retain.
CtAgent *AdPetitioners::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Phase three dispatches the manager's advisor simulation; phase four sets its pending byte for a later pass.
// Other phases have no effect and all phases preserve the original true result.
bool AdPetitioners::Simulate(i32 elapsed) {
  if (elapsed == 3)
    DoAdvisorSimulation();
  else if (elapsed == 4)
    MarkForSimulation();
  return true;
}

i32 AdPetitioners::MinimumExecutionInterval() {
  return 3;
}

bool AdPetitioners::IsAgentLocking() {
  return false;
}

u32 AdPetitioners::TypeId() const {
  return kAdPetitionersLayerType;
}

// This specialized manager has no retained city-agent type object.
CtAgentType *AdPetitioners::AgentType() {
  return nullptr;
}

bool AdPetitioners::SetAgentType(CtAgentType *) {
  return false;
}

bool AdPetitioners::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdPetitioners::DebugClassTag(ScStringView *) {
  return false;
}

bool AdPetitioners::DebugTypeTag(ScStringView *) {
  return false;
}

bool AdPetitioners::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool AdPetitioners::StaticInit(CtDefinition *) {
  return true;
}

bool AdPetitioners::StaticShutdown() {
  return true;
}

u32 AdPetitioners::GetLayerType() {
  return kAdPetitionersLayerType;
}

bool AdPetitioners::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

bool AdPetitioners::MarkForSimulation() {
  simulation_pending = 1;
  return true;
}

// Count actual retained petitioner advice without acquiring another lock. A missing queue reports zero.
u32 AdPetitioners::GetPetitionerQueueSize() {
  if (!queue)
    return 0;
  u32 count = 0;
  for (AdPetitionLink *node = queue->records->head->next; node != queue->records->head; node = node->next)
    ++count;
  return count;
}

bool AdPetitioners::AddToken(u32 id, AdToken token) {
  queue->AddToken(id, &token);
  return true;
}

bool AdPetitioners::Init(CtCity *, ScImporter *) {
  return true;
}

// Forward the full petitioner ID and record output to the bound queue; a missing queue leaves output untouched.
bool AdPetitioners::GetPetitionerStructureFromID(u32 id, AdPetitionerRecord *out) {
  if (queue)
    return queue->GetStructure(id, out);
  return false;
}

bool AdPetitioners::GetPetitionerHeaders(AdPetitionerHeader **out) {
  if (!queue)
    return false;
  queue->WriteHeaders(out);
  return true;
}

// Build the selected static petitioner portrait and owned name/message Strings, resolving each scoped resource twice.
// Neighbor selectors substitute the city name before copying its mayor name; unrelated output fields stay untouched.
bool AdPetitioners::GetPetitionerStructure(u32 id, AdPetitionerRecord *out) {
  AdTextHandle message, name;
  // The installed default path deliberately copies the output address into the portrait instance register.
  u32 portrait = static_cast<u32>(reinterpret_cast<uptr>(out));
  if ((id >= 2 && id <= 17) || id == 19) {
    static const u32 portraits[] = {0xff01, 0xff02, 0xff12, 0xff07, 0xff0b, 0xff08, 0xff0e, 0xff15, 0xff11, 0xff0c,
        0xff0f, 0xff09, 0xff13, 0xff06, 0xff0a, 0xff0d, 0xff04};
    static const u32 messages[] = {
        1, 2, 0x1d, 0x15, 0x16, 0x22, 0x0c, 0x1b, 0x14, 0x28, 0x63, 0x8f, 0x74, 0x85, 0x78, 0x10, 0xd7};
    u32 index = id == 19 ? 16 : id - 2;
    portrait = portraits[index];
    {
      AdTextHandle selected(messages[index], kAdPetitionDescriptionTextGroup);
      message.Assign(selected);
    }
    {
      AdTextHandle selected(index + kRsSupplementalGameBeverlyOverlyText, kRsSupplementalGameTextGroup);
      name.Assign(selected);
    }
  }
  if (id >= 20 && id <= 23) {
    ScString neighbor;
    ScString *expanded = new ScString, *display_name = new ScString;
    u8 index = static_cast<u8>(id - 20);
    neighbors->GetNeighborName(index, &neighbor);
    {
      AdTextHandle selected(0xa5, kAdPetitionDescriptionTextGroup);
      message.Assign(selected);
    }
    {
      ScString needle("%NEIGHBOR%");
      message.CopyWithReplacement(&needle, &neighbor, expanded);
    }
    neighbors->GetNeighborMayorName(index, display_name);
    static const u32 portraits[] = {0xff03, 0xff05, 0xff10, 0xff14};
    portrait = portraits[index];
    out->text[0] = display_name;
    out->text[2] = expanded;
  } else {
    ScString *display_name = new ScString;
    name.CopyTo(display_name);
    out->text[0] = display_name;
    ScString *expanded = new ScString;
    message.CopyTo(expanded);
    out->text[2] = expanded;
  }
  out->portrait = {kRsImageResourceType, kRsAdvisorPortraitGroup, portrait};
  return true;
}

// Probe the selected type's groups in stored order and return the first existing resource's group, or zero on exhaustion.
// Reuse the mutable key and reload the array after each service call, preserving callback changes to both.
u32 AdPetitioners::GetPetitionerStringGroup(u32 type, u32 id) {
  RsKey key = {kRsTextResourceType, 0, id};
  ScArray<u32> *groups = &string_groups[type];
  for (u32 index = 0; index < groups->size; ++index) {
    key.group = groups->data[index];
    if (AdGetTextManager()->TestForKey(&key))
      return key.group;
  }
  return 0;
}

// Each resolved field receives a separate owned String; garbage quantity uses the current pollution scaling service.
bool AdPetitioners::GetNeighborDealPetitioner(u8 type, AdPetitionerRecord *out) {
  AdTextHandle resource;
  CtDealRecord deal;
  if (!neighbors->GetDeal(type, &deal))
    return false;
  u32 id = 0;
  AdBaseAdvisor *advisor = nullptr;
  if (type <= 5) {
    static const u32 ids[] = {234, 233, 236, 235, 238, 237};
    id = ids[type];
    if (type < 4)
      advisor = utilities;
    else
      advisor = environment;
  }
  if (advisor) {
    ScString *name = new ScString;
    advisor->GetNameAdvice(name);
    out->advisor_name = name;
    out->advisor_portrait = *advisor->GetLargeAdvisorPicture();
    out->advisor_id = advisor->GetAdvisorID();
  } else {
    out->advisor_id = 0;
    out->advisor_name = nullptr;
    out->extra_text = nullptr;
  }
  // A const input view lets each literal temporary retire immediately after its token's deep copy.
  AdToken neighbor(4, &static_cast<const ScStringView &>(ScString("%NEIGHBOR%")), static_cast<u32>(deal.value_10));
  AdToken parameter_w(3, &static_cast<const ScStringView &>(ScString("%W%")), static_cast<i32>(deal.value_18));
  u32 quantity = deal.value_08;
  if (deal.type == 4 || deal.type == 5) {
    EcLayer *pollution = AdGetCity()->PollutionLayer();
    if (pollution) {
      volatile f64 product = static_cast<f64>(pollution->GetGarbageScalingFactor()) * static_cast<i32>(deal.value_08);
      f64 biased = product + kScDoubleIntegerBias;
      u64 bits;
      memcpy(&bits, &biased, sizeof(bits));
      quantity = static_cast<u32>(bits) + kScDoubleIntegerLowWordBias;
    } else {
      quantity = static_cast<u32>(static_cast<i32>(deal.value_08) / 10);
    }
  }
  AdToken parameter_x(2, &static_cast<const ScStringView &>(ScString("%X%")), static_cast<i32>(quantity));
  u32 cost = deal.type == 4 || deal.type == 0 || deal.type == 2 ? deal.value_20 : deal.value_0c;
  AdToken parameter_y(3, &static_cast<const ScStringView &>(ScString("%Y%")), static_cast<i32>(cost));
  AdToken parameter_z(3, &static_cast<const ScStringView &>(ScString("%Z%")), static_cast<i32>(deal.value_1c));
  AdToken year_token(5, &static_cast<const ScStringView &>(ScString("%YEAROFDEAL%")), deal.year);
  const AdToken *tokens[] = {&neighbor, &parameter_w, &parameter_x, &parameter_y, &parameter_z, &year_token};
  if (id) {
    out->id = id;
    static const u32 types[] = {5, 3, 4, 1, 6, 2, 7};
    static const u32 slots[] = {0, 1, 2, 3, 5, 4, 6};
    for (u32 index = 0; index < 7; ++index) {
      ResolveText(id, types[index], &resource);
      ScString *text = new ScString;
      resource.CopyTo(text, tokens, 6);
      out->text[slots[index]] = text;
    }
    ResolveText(id, 0, &resource);
    ScString *extra = new ScString;
    resource.CopyTo(extra, tokens, 6);
    out->extra_text = extra;
  }
  out->portrait.type = kRsImageResourceType;
  out->portrait.group = kRsAdvisorPortraitGroup;
  if (deal.value_10 < 4) {
    static const u32 portraits[] = {0xff03, 0xff05, 0xff10, 0xff14};
    out->portrait.instance = portraits[deal.value_10];
  }
  return true;
}

bool AdPetitioners::AddPetition(u32 id, u32 replacement, u8 delay) {
  return AddPetition(id, replacement, 0, 0, delay);
}

bool AdPetitioners::AddPetition(u32 id, u32 replacement, u8 flag20, u8 delay) {
  return AddPetition(id, replacement, flag20, 0, delay);
}

// Reactivate an existing ID and return false; otherwise allocate its distinct record and publish the original fields.
// Notification creation and queue ownership happen through the actual append phase after all record fields are set.
bool AdPetitioners::AddPetition(u32 id, u32 replacement, u8 flag20, u8 replaceable, u8 delay) {
  if (queue->Reactivate(id))
    return false;
  AdPetition *record = new AdPetition;
  record->flag16 = 0;
  record->replacement = replacement;
  record->age = 0;
  record->dismissed = 0;
  record->delay = delay;
  record->id = id;
  record->flag20 = flag20;
  record->replaceable = replaceable;
  queue->Append(record);
  return true;
}

bool AdPetitioners::ResolveText(u32 id, u32 type, AdTextHandle *out) {
  u32 group = GetPetitionerStringGroup(type, id);
  AdTextHandle resource(id, group);
  out->Assign(resource);
  return group != 0;
}

bool AdPetitioners::AddReplaceablePetition(u32 id, u32 replacement, u8 delay) {
  return AddPetition(id, replacement, 0, 1, delay);
}

static bool AdReadPetitionerSetting(AdIni *reader, const char *name, ScString *out) {
  ScString key(name, static_cast<u32>(strlen(name))), section("PETITIONER_ORDINANCE_TRIGGER_MAPPINGS", 37);
  return reader->ReadEntry(section, key, out);
}

static void AdStorePetitionerGroup(const ScString &, const ScString &value, void *identity) {
  AdPetitioners *owner = static_cast<AdPetitioners *>(static_cast<AdPetitionerManager *>(identity));
  *owner->string_groups[owner->string_group_index].Append() = AdParseIniNumber(value.begin);
}

static void AdStorePetitionerFile(const ScString &, const ScString &value, void *identity) {
  AdPetitioners *owner = static_cast<AdPetitioners *>(static_cast<AdPetitionerManager *>(identity));
  *owner->extra_info.Append() = new ScString(value);
}

// Read ordinance thresholds, then append eleven resource-group sections and configured portrait filenames in source order.
// Missing scalar keys retain their prior fields; repeated loads append vectors and finish with group cursor10.
bool AdPetitioners::LoadConfiguration() {
  ScString path, value;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\PetitionerManager.INI", 26);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  if (AdReadPetitionerSetting(&reader, "POPULATION_FOR_GAMBLING", &value))
    population_for_gambling = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "MAX_GAMBLING_AURA", &value))
    max_gambling_aura = static_cast<u8>(atoi(value.begin));
  if (AdReadPetitionerSetting(&reader, "PRO_READING_AURA", &value))
    pro_reading_aura = static_cast<u8>(atoi(value.begin));
  if (AdReadPetitionerSetting(&reader, "PRO_READING_POP", &value))
    pro_reading_population = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "LE_LEVEL_FOR_CLINIC", &value))
    education_for_clinic = static_cast<u8>(AdParseIniNumber(value.begin));
  if (AdReadPetitionerSetting(&reader, "PARKING_FINES_YEAR", &value))
    parking_fines_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "POWER_CONSERVATION_YEAR", &value))
    power_conservation_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "SHUTTLE_SERVICE_YEAR", &value))
    shuttle_service_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "NUCLEAR_FREE_YEAR", &value))
    nuclear_free_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "ELECTRONICS_JOB_FAIR_YEAR", &value))
    electronics_job_fair_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "WATER_METERS_YEAR", &value))
    water_meters_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "TIRE_RECYCLING_YEAR", &value))
    tire_recycling_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "TOURIST_PROMOTION_YEAR", &value))
    tourist_promotion_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "CONSERVATION_CORPS_YEAR", &value))
    conservation_corps_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "PAPERWORK_REDUCTION_YEAR", &value))
    paperwork_reduction_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "PUBLIC_ACCESS_TV_YEAR", &value))
    public_access_tv_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "BACKYARD_COMPOST_YEAR", &value))
    backyard_compost_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "IND_WASTE_DISPOSAL_YEAR", &value))
    industrial_waste_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "AEROSPACE_TAX_YEAR", &value))
    aerospace_tax_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "CLEAN_INDUSTRY_ASSOC_YEAR", &value))
    clean_industry_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "BIOTECH_TAX_INCENTIVE_YEAR", &value))
    biotech_tax_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "STAIRWELL_LIGHTING_YEAR", &value))
    stairwell_lighting_year = AdParseIniNumber(value.begin);
  if (AdReadPetitionerSetting(&reader, "HOMELESS_SHELTER_POPULATION", &value))
    homeless_shelter_population = AdParseIniNumber(value.begin);
  const char *sections[] = {"PETITIONER_STRINGS_ADVICE", "PETITIONER_STRINGS_BUTTON1", "PETITIONER_STRINGS_BUTTON2",
      "PETITIONER_STRINGS_HEADING", "PETITIONER_STRINGS_MESSAGE", "PETITIONER_STRINGS_NAME",
      "PETITIONER_STRINGS_REACTION1", "PETITIONER_STRINGS_REACTION2", "PETITIONER_STRINGS_TICKER",
      "PETITIONER_STRINGS_TICKER_X", "PETITIONER_STRINGS_TICKER_Y"};
  for (u32 index = 0; index < 11; ++index) {
    string_group_index = index;
    ScString section(sections[index], static_cast<u32>(strlen(sections[index])));
    reader.EnumEntries(section, AdStorePetitionerGroup, static_cast<AdPetitionerManager *>(this));
  }
  ScString section("PetitionerExtraInfo Filenames", 29);
  reader.EnumEntries(section, AdStorePetitionerFile, static_cast<AdPetitionerManager *>(this));
  return true;
}

// Retain the city, construct its petitioner queue and bind original layer services and borrowed domain advisor identities.
// Load real configuration before registering each cheat/subscription pair and the calendar notification in source order.
bool AdPetitioners::Init(CtCity *city) {
  if (!city)
    return false;
  simulation_city = city;
  city->AddRef();
  queue = new AdPetitionerQueue(static_cast<AdPetitionerManager *>(this));
  simulation_pending = 1;
  city_budget = nullptr;
  CtLayer *layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&city_budget));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinance_service));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kCtAuraLayerType);
  layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura_layer));
  layer = simulation_city->GetSpecificLayer(kCtNeighborsLayerType);
  layer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  budget = static_cast<AdBudgetAdvisor *>(simulation_city->GetBudgetAdvisor());
  planning = static_cast<AdCityPlanningAdvisor *>(simulation_city->GetCityPlanningAdvisor());
  demographics = static_cast<AdDemographicsAdvisor *>(simulation_city->GetDemographicsAdvisor());
  environment = static_cast<AdEnvironmentAdvisor *>(simulation_city->GetEnvironmentAdvisor());
  safety = static_cast<AdPublicSafetyAdvisor *>(simulation_city->GetPublicSafetyAdvisor());
  transportation = static_cast<AdTransportationAdvisor *>(simulation_city->GetTransportationAdvisor());
  utilities = static_cast<AdUtilitiesAdvisor *>(simulation_city->GetUtilityAdvisor());
  LoadConfiguration();
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(
      kAdForcePetitionersToSayCheatId, AdForcePetitionersToSay);
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdForcePetitionersToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdCallCousinVinnieCheatId, AdCallCousinVinnie);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdCallCousinVinnieCheatId);
  AdGetMessageServer()->AddNotification(target, kAdMsg_QueuePetition);
  return true;
}

// Unregister and unsubscribe each cheat before removing the calendar listener, then release retained city services in order.
// Clear before virtual queue destruction, then free the numeric forced-ID list nodes.
bool AdPetitioners::Shutdown() {
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForcePetitionersToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdForcePetitionersToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdCallCousinVinnieCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdCallCousinVinnieCheatId);
  AdGetMessageServer()->RemoveNotification(target, kAdMsg_QueuePetition);
  ScRelease(&simulation_city);
  ScRelease(&city_budget);
  ScRelease(&ordinance_service);
  ScRelease(&residential);
  ScRelease(&aura_layer);
  ScRelease(&neighbors);
  if (queue) {
    queue->Clear();
    delete queue;
    queue = nullptr;
  }
  while (forced_ids->next != forced_ids) {
    AdForcedPetitionLink *node = forced_ids->previous;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdForcedPetitionNode *>(node);
  }
  return true;
}

// Refresh only values supplied by retained services, reloading the residential receiver after each external query.
u8 AdPetitioners::RefreshCityData() {
  if (residential) {
    population = static_cast<u32>(residential->GetPopulation());
    workforce_eq = residential->GetWorkforceEQ();
    workforce_le = residential->GetWorkforceLE();
  }
  if (aura_layer)
    mayor_rating = static_cast<u8>(aura_layer->GetMayorRating());
  if (city_budget)
    treasury = city_budget->GetTotalFunds();
  if (simulation_city) {
    u32 day;
    simulation_city->GetDate(&day, &year);
  }
  return 1;
}

// Register the actual virtual city-agent identity before taking the initial city-service snapshot.
// Simulator refusal does not change the original successful result or suppress sampling.
bool AdPetitioners::SimulationBegin() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  RefreshCityData();
  return true;
}

bool AdPetitioners::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return true;
}

static void AdTryOrdinancePetition(AdPetitioners *owner, AdRandom *random, u32 id) {
  if (!owner->queue->Reactivate(id) && random->Next(100) == 0)
    owner->AddPetition(id, 0, 120);
}

u8 AdPetitioners::EvaluateOrdinances() {
  AdRandom random(kScRandomSeedFromClock);
  u32 population_now = population;
  if (population_now < 100)
    return 1;
  if (population_now > population_for_gambling &&
      ordinance_service->QueryOrdinanceAvailable(kCtOrdinance_LegalizedGambling)) {
    if (ordinance_service->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling))
      AdTryOrdinancePetition(this, &random, kAdRepealGamblingOrdinancePetitionerId);
    else if (static_cast<i8>(mayor_rating) < static_cast<i8>(max_gambling_aura))
      AdTryOrdinancePetition(this, &random, kAdLegalizedGamblingProposedPetitionerId);
  }
  if (static_cast<i8>(mayor_rating) < static_cast<i8>(pro_reading_aura) && population > pro_reading_population &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_ProReading))
    AdTryOrdinancePetition(this, &random, kAdProReadingOrdinanceProposedPetitionerId);
  if (ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_MandatorySmokeDetectors))
    AdTryOrdinancePetition(this, &random, kAdSmokeDetectorOrdinancePetitionerId);
  if (ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_NeighborhoodWatch))
    AdTryOrdinancePetition(this, &random, kAdNeighborhoodWatchOrdinancePetitionerId);
  if (ordinance_service->QueryOrdinanceAvailable(kCtOrdinance_CommunityCPRTraining)) {
    if (ordinance_service->QueryOrdinanceOn(kCtOrdinance_CommunityCPRTraining))
      AdTryOrdinancePetition(this, &random, kAdKillCprOrdinanceNowPetitionerId);
    else
      AdTryOrdinancePetition(this, &random, kAdCprTrainingOrdinancePetitionerId);
  }
  if (ordinance_service->QueryOrdinanceAvailable(kCtOrdinance_PublicSmokingBan)) {
    if (ordinance_service->QueryOrdinanceOn(kCtOrdinance_PublicSmokingBan))
      AdTryOrdinancePetition(this, &random, kAdRepealSmokingBanPetitionerId);
    else
      AdTryOrdinancePetition(this, &random, kAdPublicSmokingBanPetitionerId);
  }
  if (workforce_le < education_for_clinic && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_FreeClinics))
    AdTryOrdinancePetition(this, &random, kAdFreeClinicsOrdinancePetitionerId);
  if (ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_HomelessShelters) &&
      population > homeless_shelter_population)
    AdTryOrdinancePetition(this, &random, kAdHomelessShelterOrdinanceProposedPetitionerId);
  if (year > shuttle_service_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_ShuttleService))
    AdTryOrdinancePetition(this, &random, kAdShuttleServiceProposedPetitionerId);
  if (year > power_conservation_year && ordinance_service->QueryOrdinanceAvailable(kCtOrdinance_PowerConservation)) {
    if (ordinance_service->QueryOrdinanceOn(kCtOrdinance_PowerConservation))
      AdTryOrdinancePetition(this, &random, kAdPowerConservationProtestedPetitionerId);
    else
      AdTryOrdinancePetition(this, &random, kAdPowerConservationProposedPetitionerId);
  }
  if (year > nuclear_free_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_NuclearFreeZone))
    AdTryOrdinancePetition(this, &random, kAdDeclareCityNuclearFreeZonePetitionerId);
  if (year > parking_fines_year && ordinance_service->QueryOrdinanceAvailable(kCtOrdinance_ParkingFines)) {
    u32 id = ordinance_service->QueryOrdinanceOn(kCtOrdinance_ParkingFines) ? 0x81 : 0x80;
    AdTryOrdinancePetition(this, &random, id);
  }
  if (year > electronics_job_fair_year &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_ElectronicsJobFair))
    AdTryOrdinancePetition(this, &random, kAdElectronicsJobFairProposedPetitionerId);
  if (year > water_meters_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_MandatoryWaterMeters))
    AdTryOrdinancePetition(this, &random, kAdWaterMetersProposedPetitionerId);
  if (year > tire_recycling_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_TireRecycling))
    AdTryOrdinancePetition(this, &random, kAdTireRecyclingProposedPetitionerId);
  if (year > tourist_promotion_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_TouristPromotion))
    AdTryOrdinancePetition(this, &random, kAdTouristPromotionSuggestedPetitionerId);
  if (year > conservation_corps_year &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_ConservationCorps))
    AdTryOrdinancePetition(this, &random, kAdConservationCorpsProposedPetitionerId);
  if (year > paperwork_reduction_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_PaperReduction))
    AdTryOrdinancePetition(this, &random, kAdPaperworkReductionProposedPetitionerId);
  if (year > public_access_tv_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_PublicAccessCable))
    AdTryOrdinancePetition(this, &random, kAdPublicAccessTvProposedPetitionerId);
  if (year > backyard_compost_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_BackyardComposting))
    AdTryOrdinancePetition(this, &random, kAdBackyardCompostingProposedPetitionerId);
  if (year > industrial_waste_year &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_IndustrialWasteDisposal))
    AdTryOrdinancePetition(this, &random, kAdIndustrialWasteTaxProposedPetitionerId);
  if (year > aerospace_tax_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_AerospaceTaxIncentive))
    AdTryOrdinancePetition(this, &random, kAdAerospaceTaxIncentivePetitionerId);
  if (year > clean_industry_year &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_CleanIndustryAssociation))
    AdTryOrdinancePetition(this, &random, kAdJoinCleanIndustryAssociationPetitionerId);
  if (ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_EarthquakeRetrofitting)) {
    ApPreferences *preferences = AdGetAppPreferences();
    if (reinterpret_cast<const u8 *>(preferences->SimPrefs())[2] &&
        !queue->Reactivate(kAdProtectBuildingsAgainstQuakesPetitionerId) && random.Next(100) == 0)
      AddPetition(kAdProtectBuildingsAgainstQuakesPetitionerId, 0, 120);
  }
  if (year > biotech_tax_year && ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_BiotechTaxIncentives))
    AdTryOrdinancePetition(this, &random, kAdBiotechTaxIncentivePetitionerId);
  if (year > stairwell_lighting_year &&
      ordinance_service->QueryOrdinanceAvailableAndOff(kCtOrdinance_StairwellLighting))
    AdTryOrdinancePetition(this, &random, kAdStairwellLightingOrdinancePetitionerId);
  return 1;
}

// Append the complete forced petitioner ID without narrowing the parsed atoi word or rejecting duplicates.
u8 AdPetitioners::ForcePetition(u32 id) {
  AdForcedPetitionNode *node = new AdForcedPetitionNode;
  node->id = id;
  node->next = forced_ids;
  node->previous = forced_ids->previous;
  node->previous->next = node;
  node->next->previous = node;
  return 1;
}

// Drop all owned forced-ID nodes while retaining the circular sentinel and existing queued petitions.
u8 AdPetitioners::ClearForcedPetitions() {
  while (forced_ids->next != forced_ids) {
    AdForcedPetitionLink *node = forced_ids->previous;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdForcedPetitionNode *>(node);
  }
  return 1;
}

// Reissue each forced numeric ID using the actual four-argument ordinary wrapper, preserving duplicates and list order.
// Reload the current successor and sentinel after synchronous notification callbacks.
u8 AdPetitioners::ReplayForcedPetitions() {
  for (AdForcedPetitionLink *node = forced_ids->next; node != forced_ids; node = node->next)
    AddPetition(static_cast<AdForcedPetitionNode *>(node)->id, 0, 0, 0);
  return 1;
}

// Clear pending after the post callback even when it schedules another pass, and ignore the server's refusal.
bool AdPetitioners::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    RefreshCityData();
    EvaluateOrdinances();
    queue->Advance();
    queue->ResetRedisplay();
    ReplayForcedPetitions();
    ScMessage message = {kAdMsg_PetitionerListUpdated, IID_AdPetitionerManager, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

// Handle actual cheat and calendar messages, retaining the mutable command suffix and full parsed forced ID.
// All cheat subcommands are consumed; calendar posts forward the raw low delay byte, and unrelated messages return false.
bool AdPetitioners::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    switch (static_cast<u32>(message->data1)) {
    case kAdForcePetitionersToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForcePetitionersToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForcePetitionersToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      ForcePetition(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    case kAdStopForcingAdviceCheatId:
      ClearForcedPetitions();
      break;
    case kAdCallCousinVinnieCheatId:
      AddPetition(kAdLocalFundraisingEventPetitionerId, 0, 1, 0, 6);
      break;
    }
    return true;
  }
  if (message->type == kAdMsg_QueuePetition) {
    AddPetition(static_cast<u32>(message->data1), 0, 0, static_cast<u8>(message->data2));
    return true;
  }
  return false;
}

// Load the actual petitioner record through its scoped frame and separate queue version, retaining completed queue effects.
// Missing queues or unsuccessful frame/version/queue reads return false; failed scopes abort during normal destruction.
bool AdPetitioners::Init(CtCity *city, RsDatabaseSegment *segment) {
  if (!queue)
    return false;
  ScArchiveFrame frame(city, segment, RsKey{kRsCityLayerRecordType, kAdPetitionerStateRecordGroup, 0});
  RsStream *stream = frame.GetStream();
  u32 version = 0;
  bool success = frame.valid && stream && stream->ReadU32(&version) && queue->Read(city, version, stream);
  if (!success)
    frame.Fail();
  return success;
}

// Save the actual petitioner key with framed header version1 and a separate queue version before writing real records.
bool AdPetitioners::Save(CtCity *city, RsDatabaseSegment *segment) {
  if (!queue)
    return false;
  ScArchiveFrame frame(city, segment, RsKey{kRsCityLayerRecordType, kAdPetitionerStateRecordGroup, 0}, 1, 0);
  RsStream *stream = frame.GetStream();
  bool success = frame.valid && stream && stream->WriteU32(1) && queue->Write(city, stream);
  if (!success)
    frame.Fail();
  return success;
}

// Respond, dismiss and show the original petitioner reaction before routing the selected game action.
bool AdPetitioners::PetitionerButtonPressed(u32 id, u32 button) {
  queue->Respond(id, button);
  queue->Dismiss(id);
  ShowResponse(id, button);
  switch (id) {
  case 1:
  case 4:
  case 7:
  case 10:
  case 132:
  case 136:
  case 138:
  case 139:
  case 140:
  case 141:
  case 142:
  case 143:
  case 144:
  case 148:
  case 150:
  case 151:
  case 205:
  case 300:
  case 303:
    planning->RespondToPetitioner(id, static_cast<i32>(button));
    return true;
  case kAdFundsOfferedForDisposalPlantPetitionerId:
  case kAdToxicWasteFactoryQuestionedPetitionerId:
  case kAdProtestToxicWasteConversionPlantPetitionerId:
  case kAdBuildCasinoPetitionerId:
  case kAdGigamallProposedPetitionerId:
  case kAdMaximumSecurityPrisonOfferedPetitionerId:
    budget->RespondToPetitioner(id, static_cast<i32>(button));
    return true;
  case kAdHealthcareWorkersDemandRaisePetitionerId:
  case kAdHealthcareWorkersOnStrikePetitionerId:
  case kAdTeachersAreUnderpaidPetitionerId:
  case kAdTeachersStrikeContinuesPetitionerId:
    demographics->RespondToPetitioner(id, button);
    return true;
  case kAdPoliceThreatenWalkoutPetitionerId:
  case kAdPoliceStrikePetitionerId:
  case kAdFirefightersStrikePetitionerId:
  case kAdFirefightersThreatenStrikePetitionerId:
    safety->RespondToPetitioner(id, static_cast<i32>(button));
    return true;
  case kAdThreatOfTransitStrikePetitionerId:
  case kAdTransitWorkersStrikePetitionerId:
    transportation->RespondToPetitioner(id, static_cast<i32>(button));
    return true;
  case 165:
  case 166:
  case 167:
  case 168:
  case kAdWaterDealDrainsTreasuryPetitionerId:
  case 172:
  case 173:
  case 175:
  case kAdLetSEmpowerOurselvesPetitionerId:
  case 179:
  case 180:
  case 181:
  case 185:
  case 186:
  case 187:
  case 189:
  case 191:
  case 192:
  case 193:
  case 194:
  case kAdTrashExportingDealStinksPetitionerId:
  case 196:
  case 197:
  case 199:
  case 202:
  case 233:
  case 234:
  case 235:
  case 236:
  case 237:
  case 238:
  case 247:
  case 248:
    utilities->RespondToPetitioner(id, static_cast<i32>(button));
    return true;
  case kAdLocalFundraisingEventPetitionerId:
    if (button == 1)
      city_budget->DepositFunds(kAdPetitionCashGrant);
    else
      planning->flag_30e = 1;
    queue->ForceDismiss(kAdLocalFundraisingEventPetitionerId);
    return true;
  case kAdParadePetitionerId:
    if (button == 1) {
      DsManager *manager = AdGetCity()->DisasterLayer()->GetDisasterManager(kDsParadeTypeId);
      void *queried = nullptr;
      if (manager && manager->QueryInterface(IID_DsParadeControl, &queried)) {
        DsParadeControl *control = static_cast<DsParadeControl *>(queried);
        control->RequestEnd();
        control->Release();
      }
    }
    return true;
  }
  u32 ordinance;
  bool on = true;
  switch (id) {
  case kAdCprTrainingOrdinancePetitionerId:
    ordinance = kCtOrdinance_CommunityCPRTraining;
    break;
  case kAdKillCprOrdinanceNowPetitionerId:
    ordinance = kCtOrdinance_CommunityCPRTraining;
    on = false;
    break;
  case kAdSmokeDetectorOrdinancePetitionerId:
    ordinance = kCtOrdinance_MandatorySmokeDetectors;
    break;
  case kAdNeighborhoodWatchOrdinancePetitionerId:
    ordinance = kCtOrdinance_NeighborhoodWatch;
    break;
  case kAdYouthCurfewProposedPetitionerId:
    ordinance = kCtOrdinance_YouthCurfew;
    break;
  case kAdRepealCurfewOrdinancePetitionerId:
    ordinance = kCtOrdinance_YouthCurfew;
    on = false;
    break;
  case kAdRepealGamblingOrdinancePetitionerId:
    ordinance = kCtOrdinance_LegalizedGambling;
    on = false;
    break;
  case kAdLegalizedGamblingProposedPetitionerId:
    ordinance = kCtOrdinance_LegalizedGambling;
    break;
  case kAdProReadingOrdinanceProposedPetitionerId:
    ordinance = kCtOrdinance_ProReading;
    break;
  case kAdPublicSmokingBanPetitionerId:
    ordinance = kCtOrdinance_PublicSmokingBan;
    break;
  case kAdRepealSmokingBanPetitionerId:
    ordinance = kCtOrdinance_PublicSmokingBan;
    on = false;
    break;
  case kAdFreeClinicsOrdinancePetitionerId:
    ordinance = kCtOrdinance_FreeClinics;
    break;
  case kAdJuniorSportsProposedPetitionerId:
    ordinance = kCtOrdinance_JuniorSports;
    break;
  case kAdRepealJuniorSportsPetitionerId:
    ordinance = kCtOrdinance_JuniorSports;
    on = false;
    break;
  case kAdHomelessShelterOrdinanceProposedPetitionerId:
    ordinance = kCtOrdinance_HomelessShelters;
    break;
  case kAdMandatoryCarpoolOrdinanceProposedPetitionerId:
    ordinance = kCtOrdinance_CarpoolIncentive;
    break;
  case kAdShuttleServiceProposedPetitionerId:
    ordinance = kCtOrdinance_ShuttleService;
    break;
  case kAdCrossingGuardOrdinancePetitionerId:
    ordinance = kCtOrdinance_CrossingGuards;
    break;
  case kAdPowerConservationProtestedPetitionerId:
    ordinance = kCtOrdinance_PowerConservation;
    on = false;
    break;
  case kAdPowerConservationProposedPetitionerId:
    ordinance = kCtOrdinance_PowerConservation;
    break;
  case kAdWaterConservationProposedPetitionerId:
    ordinance = kCtOrdinance_WaterConservation;
    break;
  case kAdWaterConservationUnderAttackPetitionerId:
    ordinance = kCtOrdinance_WaterConservation;
    on = false;
    break;
  case kAdAirPollutionLevelsHighPetitionerId:
    ordinance = kCtOrdinance_CleanAir;
    break;
  case kAdRepealCleanAirOrdinancePetitionerId:
    ordinance = kCtOrdinance_CleanAir;
    on = false;
    break;
  case kAdDeclareCityNuclearFreeZonePetitionerId:
    ordinance = kCtOrdinance_NuclearFreeZone;
    break;
  case 128:
    ordinance = kCtOrdinance_ParkingFines;
    break;
  case 129:
    ordinance = kCtOrdinance_ParkingFines;
    on = false;
    break;
  case kAdSortTrashSaveCashPetitionerId:
    ordinance = kCtOrdinance_TrashPresort;
    break;
  case kAdCarSmoggingOrdinanceProposedPetitionerId:
    ordinance = kCtOrdinance_CarSmogging;
    break;
  case kAdLawnChemicalOrdinancePetitionerId:
    ordinance = kCtOrdinance_LawnChemicalBan;
    break;
  case kAdElectronicsJobFairProposedPetitionerId:
    ordinance = kCtOrdinance_ElectronicsJobFair;
    break;
  case kAdAlternateDayDrivingPetitionerId:
    ordinance = kCtOrdinance_AlternateDayDriving;
    break;
  case kAdWaterMetersProposedPetitionerId:
    ordinance = kCtOrdinance_MandatoryWaterMeters;
    break;
  case kAdFarmerSMarketProposedPetitionerId:
    ordinance = kCtOrdinance_FarmersMarket;
    break;
  case kAdTireRecyclingProposedPetitionerId:
    ordinance = kCtOrdinance_TireRecycling;
    break;
  case kAdTouristPromotionSuggestedPetitionerId:
    ordinance = kCtOrdinance_TouristPromotion;
    break;
  case kAdConservationCorpsProposedPetitionerId:
    ordinance = kCtOrdinance_ConservationCorps;
    break;
  case kAdPaperworkReductionProposedPetitionerId:
    ordinance = kCtOrdinance_PaperReduction;
    break;
  case kAdPublicAccessTvProposedPetitionerId:
    ordinance = kCtOrdinance_PublicAccessCable;
    break;
  case kAdBackyardCompostingProposedPetitionerId:
    ordinance = kCtOrdinance_BackyardComposting;
    break;
  case kAdIndustrialWasteTaxProposedPetitionerId:
    ordinance = kCtOrdinance_IndustrialWasteDisposal;
    break;
  case kAdAerospaceTaxIncentivePetitionerId:
    ordinance = kCtOrdinance_AerospaceTaxIncentive;
    break;
  case kAdLandfillGasRecoveryProposedPetitionerId:
    ordinance = kCtOrdinance_LandfillGasRecovery;
    break;
  case kAdJoinCleanIndustryAssociationPetitionerId:
    ordinance = kCtOrdinance_CleanIndustryAssociation;
    break;
  case kAdProtectBuildingsAgainstQuakesPetitionerId:
    ordinance = kCtOrdinance_EarthquakeRetrofitting;
    break;
  case kAdBiotechTaxIncentivePetitionerId:
    ordinance = kCtOrdinance_BiotechTaxIncentives;
    break;
  case kAdStairwellLightingOrdinancePetitionerId:
    ordinance = kCtOrdinance_StairwellLighting;
    break;
  default:
    return false;
  }
  if (button == 1)
    ordinance_service->SetOrdinance(ordinance, on);
  queue->ForceDismiss(id);
  return true;
}

// Construct the original zero-reference petitioner manager and return its primary manager identity.
// The director performs the following query and reference cleanup without a factory retain.
ScUnknown *AdCreatePetitioners() {
  return static_cast<AdPetitionerManager *>(new AdPetitioners);
}

// Resolve real configured portrait keys and return only the primary portrait instance, including zero on lookup failure.
u32 AdPetitionerQueue::GetPortraitInstance(u32 id) {
  RsKey portrait{}, advisor_portrait{};
  ReadPortraits(id, &portrait, &advisor_portrait);
  return portrait.instance;
}

// Show the actual BoxX response event for the chosen button and configured portrait, with separate neighbor-deal messages.
// Button zero returns false before lookup; other buttons return true even when no event is selected or BoxX refuses it.
bool AdPetitioners::ShowResponse(u32 id, u32 button) {
  if (!button)
    return false;
  i32 message = 0;
  if (id >= 0xe9 && id <= 0xee) {
    message = button == 1 ? 155 : 164;
  } else {
    u32 portrait = queue->GetPortraitInstance(id);
    static const i32 accepted[19] = {
        0, 0, 164, 164, 162, 160, 163, 164, 159, 166, 166, 165, 162, 166, 164, 161, 166, 164, 164};
    static const i32 rejected[19] = {
        0, 0, 156, 156, 158, 158, 157, 158, 157, 157, 157, 156, 158, 157, 156, 157, 157, 156, 155};
    if (portrait < 19) {
      if (button == 1)
        message = accepted[portrait];
      else if (button == 2)
        message = rejected[portrait];
    }
  }
  if (message)
    AdGetBoxX()->Event(kAuEventPlayEffect, message, 0, 0, 0);
  return true;
}

// Create the recursive lock before the separate list wrapper and circular sentinel, retaining the borrowed manager.
AdPetitionerQueue::AdPetitionerQueue(AdPetitionerManager *owner) : manager(nullptr), lock(nullptr) {
  lock = new ScLock;
  manager = owner;
  records = new AdPetitionerList;
  AdPetitionNode *sentinel = new AdPetitionNode;
  sentinel->next = sentinel;
  sentinel->previous = sentinel;
  records->head = sentinel;
}

// Detach the manager, cancel and delete live petitions under the saved lock, then free list storage before the lock.
// Clear through the actual separate phase before freeing the list wrapper, sentinel and current lock.
AdPetitionerQueue::~AdPetitionerQueue() {
  manager = nullptr;
  Clear();
  delete static_cast<AdPetitionNode *>(records->head);
  delete records;
  records = nullptr;
  delete lock;
  lock = nullptr;
}

void AdPetitionerQueue::Clear() {
  ScLock *region = lock;
  region->Lock();
  AdPetitionLink *head = records->head;
  while (head->next != head) {
    AdPetitionLink *node = head->next;
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->notification)
      AdDismissNotification(record->notification);
    delete record;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdPetitionNode *>(node);
    head = records->head;
  }
  region->Unlock();
}

// Clear each retained advice's redisplay byte under the original saved recursive lock and return true.
bool AdPetitionerQueue::ResetRedisplay() {
  ScLock *region = lock;
  region->Lock();
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next)
    static_cast<AdPetitionNode *>(node)->petition->redisplay = 0;
  region->Unlock();
  return true;
}

// Advance petitioner ages; delay zero keeps flagged or dismissed advice aging with raw-byte wrapping.
// Expired replacements and dismissed redisplays skip a successor after deletion, including defined sentinel wrapping.
bool AdPetitionerQueue::Advance() {
  ScLock *region = lock;
  region->Lock();
  AdPetitionLink *node = records->head->next;
  while (node != records->head) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    bool remove = false, skip_next = false;
    if (record->flag20) {
      if (record->delay && record->age >= record->delay) {
        record->dismissed = 1;
        if (static_cast<AdPetitionNode *>(node)->petition->notification)
          AdDismissNotification(static_cast<AdPetitionNode *>(node)->petition->notification);
        static_cast<AdPetitionNode *>(node)->petition->notification = 0;
      } else {
        ++record->age;
      }
    } else if (record->replaceable) {
      if (record->age < record->delay) {
        ++record->age;
      } else {
        remove = true;
        skip_next = true;
      }
    } else if (record->redisplay) {
      if (record->dismissed) {
        if (!record->delay || record->age < record->delay) {
          ++record->age;
        } else {
          remove = true;
          skip_next = true;
        }
      }
    } else {
      if (record->notification)
        AdDismissNotification(record->notification);
      remove = true;
    }
    if (remove) {
      AdPetitionLink *next = node->next;
      delete static_cast<AdPetitionNode *>(node)->petition;
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete static_cast<AdPetitionNode *>(node);
      node = next;
      if (skip_next)
        node = node->next;
    } else {
      node = node->next;
    }
  }
  region->Unlock();
  return true;
}

// Mark the first matching full petitioner ID for redisplay without changing its dismissal state.
bool AdPetitionerQueue::Reactivate(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id) {
      record->redisplay = 1;
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// Search all duplicate petitioner IDs until an undismissed record is found, under the saved queue lock.
bool AdPetitionerQueue::ContainsActive(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id && !record->dismissed) {
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// Return the first full petitioner ID whose separate replacement key matches, or zero if none does.
u32 AdPetitionerQueue::GetIdForReplacement(u32 replacement) {
  ScLock *region = lock;
  region->Lock();
  u32 result = 0;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->replacement == replacement) {
      result = record->id;
      break;
    }
  }
  region->Unlock();
  return result;
}

// Dismiss the first eligible petitioner with a delay, timed flag or replacement flag, skipping ineligible duplicates.
// Cancellation is unconditional even for notification zero; clear the saved record's ID after the callback.
bool AdPetitionerQueue::Dismiss(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id && (record->delay || record->flag20 || record->replaceable)) {
      record->dismissed = 1;
      AdDismissNotification(record->notification);
      record->notification = 0;
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// Set timed/dismissed flags on the first matching petitioner, then cancel its saved notification unconditionally.
bool AdPetitionerQueue::ForceDismiss(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id) {
      u32 notification = record->notification;
      record->flag20 = 1;
      record->dismissed = 1;
      AdDismissNotification(notification);
      record->notification = 0;
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// Remove the first matching petitioner, canceling a nonzero notification before deleting the saved advice and node.
bool AdPetitionerQueue::Remove(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id) {
      if (record->notification)
        AdDismissNotification(record->notification);
      delete record;
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete static_cast<AdPetitionNode *>(node);
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// A missing petitioner leaves both lists untouched and returns false; repeated IDs do not receive extra copies.
bool AdPetitionerQueue::AddToken(u32 id, const AdToken *token) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdPetitionLink *node = records->head->next; node != records->head; node = node->next) {
    AdPetition *record = static_cast<AdPetitionNode *>(node)->petition;
    if (record->id == id) {
      AdTokenNode *entry = new AdTokenNode{{record->tokens, record->tokens->previous}, *token};
      entry->previous->next = entry;
      record->tokens->previous = entry;
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}
