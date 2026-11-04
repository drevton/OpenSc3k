#include "advisors/ad_advice.h"

#include "application/api.h"
#include "advisors/ad_module.h"
#include "advisors/ad_petitions.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "city/api.h"
#include "common/thread.h"
#include "common/value.h"
#include "resources/resource.h"
#include "resources/stream.h"

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
static constexpr u32 kAdAdvisorRecordType = 0x13dee82;

// Create an empty advice queue and its recursive lock. Only queues attached to an advisor initialize dispatch metadata.
AdAdviceQueue::AdAdviceQueue(AdAdvisor *owner, u8 value) {
  head = new AdAdviceLink;
  head->next = head;
  head->previous = head;
  advisor = nullptr;
  lock = new ScLock;
  if (owner) {
    advisor = owner;
    category = value;
    Configure(value);
  }
}

// Detach the advisor before clearing owned advice. The queue then releases its lock and empty list sentinel.
AdAdviceQueue::~AdAdviceQueue() {
  advisor = nullptr;
  Clear();
  delete lock;
  delete head;
}

// Select the original message and resource groups for ordinary advice or either secondary category.
// Values above two deliberately leave dispatch metadata untouched.
void AdAdviceQueue::Configure(u8 value) {
  if (!value) {
    title_group = 0;
    header_group = kAdAdviceHeaderGroup;
    message_group = kAdAdviceMessageGroup;
    notification_group = kAdAdviceNotificationGroup;
  } else if (value <= 2) {
    notification_group = 0;
    header_group = kAdSecondaryTitleGroup;
    title_group = kAdSecondaryTitleGroup;
    message_group = kAdSecondaryMessageGroup;
  }
}

// Search for the first advice with this identifier while holding the queue's recursive lock.
bool AdAdviceQueue::Contains(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    if (static_cast<AdAdviceNode *>(node)->advice->id == id) {
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

// Mark the first retained advice for redisplay. Replaceable advice is removed under the same recursive lock instead.
bool AdAdviceQueue::Reactivate(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    AdAdvice *advice = static_cast<AdAdviceNode *>(node)->advice;
    if (advice->id != id)
      continue;
    if (!advice->replaceable) {
      advice->redisplay = 1;
      found = true;
    } else {
      Remove(id);
    }
    break;
  }
  region->Unlock();
  return found;
}

// Cancel the first matching advice's live notification before deleting its record and unlinking its node.
bool AdAdviceQueue::Remove(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    AdAdvice *advice = static_cast<AdAdviceNode *>(node)->advice;
    if (advice->id != id)
      continue;
    if (advice->notification)
      AdDismissNotification(advice->notification);
    delete advice;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdAdviceNode *>(node);
    found = true;
    break;
  }
  region->Unlock();
  return found;
}

// Remove only the first record sharing the replacement identifier. Its notification is cancelled before ownership ends.
bool AdAdviceQueue::RemoveReplacement(u32 replacement) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    AdAdvice *advice = static_cast<AdAdviceNode *>(node)->advice;
    if (advice->replacement != replacement)
      continue;
    if (advice->notification)
      AdDismissNotification(advice->notification);
    delete advice;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdAdviceNode *>(node);
    found = true;
    break;
  }
  region->Unlock();
  return found;
}

// Delete all queued records without sending cancellation messages. Release the lock after restoring an empty list.
void AdAdviceQueue::Clear() {
  ScLock *region = lock;
  region->Lock();
  while (head->next != head) {
    AdAdviceLink *node = head->next;
    delete static_cast<AdAdviceNode *>(node)->advice;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdAdviceNode *>(node);
  }
  region->Unlock();
}

// Dismiss the first matching eligible record and cancel its live notification. Ineligible duplicates are skipped.
bool AdAdviceQueue::Dismiss(u32 id) {
  ScLock *region = lock;
  region->Lock();
  bool found = false;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    AdAdvice *advice = static_cast<AdAdviceNode *>(node)->advice;
    if (advice->id != id || (!advice->delay && !advice->flag20 && !advice->replaceable))
      continue;
    advice->dismissed = 1;
    if (advice->notification)
      AdDismissNotification(advice->notification);
    advice->notification = 0;
    found = true;
    break;
  }
  region->Unlock();
  return found;
}

bool AD_STDCALL AdDismissNotification(u32 notification) {
  ScMessage message = {kApMsgDismissNotification, notification, 0, 0};
  AdGetMessageServer()->MessageSend(message);
  return true;
}

bool AdAdvice::Write(CtCity *, RsStream *stream) {
  if (!stream->WriteU32(id) || !stream->WriteU32(notification) || !stream->WriteU32(replacement) ||
      !stream->WriteU8(category) || !stream->WriteU8(flag16) || !stream->WriteU8(delay) || !stream->WriteU8(age))
    return false;
  u8 packed =
      static_cast<u8>((dismissed != 0) | ((flag20 != 0) << 1) | ((replaceable != 0) << 2) | ((redisplay != 0) << 3));
  if (!stream->WriteU8(packed))
    return false;
  u32 count = 0;
  for (AdTokenLink *node = tokens->next; node != tokens; node = node->next)
    ++count;
  bool result = stream->WriteU32(count) != 0;
  if (count) {
    if (!result)
      return false;
    AdTokenLink *sentinel = tokens;
    for (AdTokenLink *node = sentinel->next; node != sentinel; node = node->next) {
      if (!result)
        return false;
      result = static_cast<AdTokenNode *>(node)->value.Write(stream) != 0;
    }
  }
  return result;
}

// Decode the initialized packed flag byte before checking that read's result, retaining the original partial mutation.
bool AdAdvice::Read(CtCity *, u32 version, u32 token_version, RsStream *stream) {
  if (version != 1)
    return false;
  if (!stream->ReadU32(&id) || !stream->ReadU32(&notification) || !stream->ReadU32(&replacement) ||
      !stream->ReadU8(&category) || !stream->ReadU8(&flag16) || !stream->ReadU8(&delay) || !stream->ReadU8(&age))
    return false;
  u8 packed = 0;
  bool result = stream->ReadU8(&packed) != 0;
  dismissed = static_cast<u8>((packed & 1) != 0);
  flag20 = static_cast<u8>((packed & 2) != 0);
  replaceable = static_cast<u8>((packed & 4) != 0);
  redisplay = static_cast<u8>((packed & 8) != 0);
  if (!result)
    return false;
  u32 count = 0;
  result = stream->ReadU32(&count) != 0;
  for (u32 index = 0; index < count && result; ++index) {
    AdToken token;
    result = token.Read(token_version, stream) != 0;
    if (result) {
      AdTokenNode *copy = new AdTokenNode{{tokens, tokens->previous}, token};
      copy->previous->next = copy;
      tokens->previous = copy;
    }
  }
  return result;
}

// Create a notification before entering the queue lock, then append unless the identifier already exists.
// A nonzero replacement identifier removes its first predecessor before appending the new record.
void AdAdviceQueue::Add(AdAdvice *advice) {
  u32 notification = CreateNotification(advice->id);
  ScLock *region = lock;
  region->Lock();
  advice->notification = notification;
  bool append;
  if (advice->replacement) {
    RemoveReplacement(advice->replacement);
    append = true;
  } else {
    append = !Contains(advice->id);
  }
  if (append) {
    AdAdviceNode *node = new AdAdviceNode;
    node->advice = advice;
    node->next = head;
    node->previous = head->previous;
    node->previous->next = node;
    head->previous = node;
  }
  region->Unlock();
}

// Average mood bytes for nondismissed advice without acquiring the queue lock. Empty active sets return mood three.
// The original shares one output byte across the loop; the INI loader writes zero when both mappings are absent.
u32 AdAdviceQueue::GetMood() {
  u8 mood = 0;
  u32 sum = 0, count = 0;
  for (AdAdviceLink *node = head->next; node != head; node = node->next) {
    AdAdvice *advice = static_cast<AdAdviceNode *>(node)->advice;
    if (!advice->dismissed) {
      AdLoadMood(advice->id, &mood);
      sum += mood;
      ++count;
    }
  }
  return count ? sum / count : 3;
}

// Null payloads remain counted but are skipped, and no queue lock is introduced into this original save path.
bool AdAdviceQueue::Write(CtCity *city, RsStream *stream) {
  u32 count = 0;
  for (AdAdviceLink *node = head->next; node != head; node = node->next)
    ++count;
  if (!stream->WriteU32(count) || !stream->WriteU32(1))
    return false;
  bool result = stream->WriteU32(1) != 0;
  if (count) {
    AdAdviceLink *sentinel = head;
    for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
      if (!result)
        return false;
      AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
      if (record)
        result = record->Write(city, stream);
    }
  }
  return result;
}

// Append each successfully decoded, undismissed record through the real notification/queue insertion path.
bool AdAdviceQueue::Read(CtCity *city, u32 version, RsStream *stream) {
  if (version != 1)
    return false;
  u32 count = 0, record_version = 0, token_version = 0;
  if (!stream->ReadU32(&count) || !stream->ReadU32(&record_version))
    return false;
  bool result = stream->ReadU32(&token_version) != 0;
  for (u32 index = 0; index < count && result; ++index) {
    AdAdvice *record = new AdAdvice;
    result = record->Read(city, record_version, token_version, stream);
    if (!result || record->dismissed)
      delete record;
    else
      Add(record);
  }
  return result;
}

AdAdvice::AdAdvice()
    : category(0), id(0), notification(0), replacement(0), flag16(0), delay(0), age(0), dismissed(0), flag20(0),
      replaceable(0), redisplay(0) {
  tokens = new AdTokenLink;
  tokens->next = tokens;
  tokens->previous = tokens;
}

AdAdvice::~AdAdvice() {
  AdTokenLink *node = tokens->next;
  while (node != tokens) {
    AdTokenLink *next = node->next;
    delete static_cast<AdTokenNode *>(node);
    node = next;
  }
  delete tokens;
}

bool AdAdviceQueue::AddToken(u32 id, const AdToken &token) {
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *sentinel = head;
  bool found = false;
  for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    if (record->id == id) {
      AdTokenLink *tokens = record->tokens;
      AdTokenNode *copy = new AdTokenNode{{tokens, tokens->previous}, token};
      copy->previous->next = copy;
      tokens->previous = copy;
      found = true;
      break;
    }
  }
  region->Unlock();
  return found;
}

bool AdBaseAdvisor::AddAdviceToken(u32 id, AdToken token) {
  return advice->AddToken(id, token);
}

void AdBaseAdvisor::RecreateAdviceNotification(u32 id) {
  advice->RecreateNotification(id);
}

// Clear every retained record's redisplay byte under the saved queue lock and preserve the original true result.
bool AdAdviceQueue::ResetRedisplay() {
  ScLock *region = lock;
  region->Lock();
  for (AdAdviceLink *node = head->next; node != head; node = node->next)
    static_cast<AdAdviceNode *>(node)->advice->redisplay = 0;
  region->Unlock();
  return true;
}

// Advance retained ages, dismiss timed advice, remove expired replacements and reissue eligible dismissed notifications.
// Replacement deletion also advances past its successor, even through the sentinel; ordinary deletion cancels first.
bool AdAdviceQueue::Advance() {
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *node = head->next;
  while (node != head) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    bool remove = false, skip_next = false;
    if (record->category == 1 || record->flag20) {
      if (record->age >= record->delay) {
        u32 notification = record->notification;
        record->dismissed = 1;
        if (notification)
          AdDismissNotification(notification);
        record->notification = 0;
      } else {
        ++record->age;
      }
    } else if (record->replaceable) {
      if (record->age >= record->delay) {
        remove = true;
        skip_next = true;
      } else {
        ++record->age;
      }
    } else if (record->redisplay) {
      if (record->dismissed && ++record->age >= record->delay) {
        record->age = 0;
        record->dismissed = 0;
        RecreateNotification(record->id);
      }
    } else if (!record->category) {
      if (record->notification)
        AdDismissNotification(record->notification);
      remove = true;
    }
    if (remove) {
      AdAdviceLink *next = node->next;
      delete static_cast<AdAdviceNode *>(node)->advice;
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete static_cast<AdAdviceNode *>(node);
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

// Resolve the actual advisor identity before loading its ordinary advice queue from framed instance11.
// Keep the original zero-ID read allowance and retained partial queue effects; failed reads mark the frame for abort.
bool AdBaseAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  u32 id = GetAdvisorID();
  if (!advice)
    return false;
  ScArchiveFrame frame(input, segment, RsKey{kAdAdvisorRecordType, id, 11});
  RsStream *stream = frame.GetStream();
  u32 version = 0;
  bool success = frame.valid && stream && stream->ReadU32(&version) && advice->Read(input, version, stream);
  if (!success)
    frame.Fail();
  return success;
}

// Write the ordinary advice queue under its real advisor-specific framed key and separate version-one queue prefix.
// A missing queue or zero advisor identity declines before creating a frame; destructor close results do not replace the body result.
bool AdBaseAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  u32 id = GetAdvisorID();
  if (!advice || !id)
    return false;
  ScArchiveFrame frame(input, segment, RsKey{kAdAdvisorRecordType, id, 11}, 1, 0);
  RsStream *stream = frame.GetStream();
  bool success = frame.valid && stream && stream->WriteU32(1) && advice->Write(input, stream);
  if (!success)
    frame.Fail();
  return success;
}

// Reject petitioner ID zero before touching the retained manager, then forward the original separate arguments.
bool AdBaseAdvisor::AddPetitioner(u32 id, u32 replacement, u8 delay) {
  if (!id)
    return false;
  return static_cast<AdPetitioners *>(petitioners)->AddPetition(id, replacement, delay);
}

u32 AdBaseAdvisor::GetPetitionerIdForReplacement(u32 replacement) {
  return static_cast<AdPetitioners *>(petitioners)->queue->GetIdForReplacement(replacement);
}

bool AdBaseAdvisor::ReactivatePetitioner(u32 id) {
  return static_cast<AdPetitioners *>(petitioners)->queue->Reactivate(id);
}

// Reject ID zero before accessing the manager, then preserve the replaceable creation contract and separate arguments.
bool AdBaseAdvisor::AddReplaceablePetitioner(u32 id, u32 replacement, u8 delay) {
  if (!id)
    return false;
  return static_cast<AdPetitioners *>(petitioners)->AddReplaceablePetition(id, replacement, delay);
}

bool AdBaseAdvisor::ContainsActivePetitioner(u32 id) {
  return static_cast<AdPetitioners *>(petitioners)->queue->ContainsActive(id);
}

// Reject a zero ID before creating a petitioner with the original dismissal flag set and delay/replacement flag cleared.
bool AdBaseAdvisor::AddDismissiblePetitioner(u32 id, u32 replacement) {
  if (!id)
    return false;
  return static_cast<AdPetitioners *>(petitioners)->AddPetition(id, replacement, 1, 0);
}

// Reject a zero petitioner ID before calling the real manager with the original cleared optional flag bytes.
bool AdBaseAdvisor::AddPetitioner(u32 id, u32 replacement) {
  if (!id)
    return false;
  return static_cast<AdPetitioners *>(petitioners)->AddPetition(id, replacement, 0);
}

bool AdBaseAdvisor::AddPetitionerToken(u32 id, AdToken token) {
  return static_cast<AdPetitioners *>(petitioners)->AddToken(id, token);
}

bool AdBaseAdvisor::RemovePetitioner(u32 id) {
  return static_cast<AdPetitioners *>(petitioners)->queue->Remove(id);
}

bool AdBaseAdvisor::HasAdvice(u32 id) {
  return advice->Contains(id);
}

bool AdBaseAdvisor::ReactivateAdvice(u32 id) {
  return advice->Reactivate(id);
}

// Construct an ordinary-category help record and pass ownership to the help queue.
bool AdBaseAdvisor::AddHelp(u32 id) {
  AdAdvice *record = new AdAdvice;
  record->id = id;
  record->replacement = 0;
  record->notification = 0;
  record->flag16 = 0;
  record->delay = 0;
  record->age = 0;
  record->dismissed = 0;
  help->Add(record);
  return true;
}

// Construct an ordinance record with its original record category three, distinct from the queue's category one.
bool AdBaseAdvisor::AddOrdinance(u32 id) {
  AdAdvice *record = new AdAdvice;
  record->category = 3;
  record->id = id;
  record->replacement = 0;
  record->notification = 0;
  record->flag16 = 0;
  record->delay = 0;
  record->age = 0;
  record->dismissed = 0;
  ordinances->Add(record);
  return true;
}

bool AdBaseAdvisor::AddAdvice(u32 id) {
  return AddAdvice(id, 0, 0, 0, 0, 0);
}

bool AdBaseAdvisor::AddAdvice(u32 id, u32 replacement) {
  return AddAdvice(id, replacement, 0, 0, 0, 0);
}

bool AdBaseAdvisor::AddAdvice(u32 id, u32 replacement, u8 delay) {
  return AddAdvice(id, replacement, delay, 0, 0, 0);
}

bool AdBaseAdvisor::AddAdvice(u32 id, u32 replacement, u8 delay, u8 flag20, u8 category) {
  return AddAdvice(id, replacement, delay, flag20, 0, category);
}

bool AdBaseAdvisor::AddDismissibleAdvice(u32 id, u32 replacement, u8 delay) {
  return AddAdvice(id, replacement, delay, 1, 0, 0);
}

bool AdBaseAdvisor::AddReplaceableAdvice(u32 id, u32 replacement, u8 delay) {
  return AddAdvice(id, replacement, delay, 1, 1, 0);
}

bool AdBaseAdvisor::AddCategoryOneAdvice(u32 id, u32 replacement, u8 delay) {
  return AddAdvice(id, replacement, delay, 1, 1, 1);
}

// Reactivate an existing retained record first; that path returns false without allocating a second record.
// Otherwise construct the original record fields and pass the record to the ordinary queue, returning true.
bool AdBaseAdvisor::AddAdvice(u32 id, u32 replacement, u8 delay, u8 flag20, u8 replaceable, u8 category) {
  if (advice->Reactivate(id))
    return false;
  AdAdvice *record = new AdAdvice;
  record->category = category;
  record->replacement = replacement;
  record->delay = delay;
  record->flag20 = flag20;
  record->id = id;
  record->notification = 0;
  record->flag16 = 0;
  record->age = 0;
  record->dismissed = 0;
  record->redisplay = 1;
  record->replaceable = replaceable;
  advice->Add(record);
  return true;
}

// Enumerate ordinary advice headers when its queue exists, preserving the genuine count/output arguments.
bool AdBaseAdvisor::GetAdviceHeaders(u32 *count, AdHeaderRecord **out) {
  if (!advice)
    return false;
  advice->GetHeaders(count, out);
  return true;
}

// Enumerate help headers without modifying caller outputs when the help queue is absent.
bool AdBaseAdvisor::GetHelpHeaders(u32 *count, AdHeaderRecord **out) {
  if (!help)
    return false;
  help->GetHeaders(count, out);
  return true;
}

// Enumerate ordinance headers through their actual queue; a removed queue leaves caller outputs untouched.
bool AdBaseAdvisor::GetOrdinanceHeaders(u32 *count, AdHeaderRecord **out) {
  if (!ordinances)
    return false;
  ordinances->GetHeaders(count, out);
  return true;
}

bool AdBaseAdvisor::DismissAdvice(u32 id) {
  return advice->Dismiss(id);
}

bool AdBaseAdvisor::GetAdviceMessageFromID(u32 id, ScStringView *out) {
  DismissAdvice(id);
  return advice ? advice->GetMessageText(id, out) : false;
}

// Try the replacement identifier before ordinary ID lookup, consulting the live queue again if the first search fails.
// Dismiss the caller's original ID after successful structure creation, even when a replacement selected another record.
bool AdBaseAdvisor::GetAdviceStructureFromID(u32 id, AdMessageRecord **out) {
  if (!advice)
    return false;
  bool result = advice->GetStructureByReplacement(id, out);
  if (!result)
    result = advice->GetStructure(id, out);
  if (result)
    DismissAdvice(id);
  return result;
}

bool AdBaseAdvisor::GetHelpTitleFromID(u32 id, ScStringView *out) {
  return help ? help->GetTitle(id, out) : false;
}

bool AdBaseAdvisor::GetHelpMessageFromID(u32 id, ScStringView *out) {
  return help ? help->GetMessageText(id, out) : false;
}

bool AdBaseAdvisor::GetOrdinanceTitleFromID(u32 id, ScStringView *out) {
  return ordinances ? ordinances->GetTitle(id, out) : false;
}

bool AdBaseAdvisor::GetOrdinanceMessageFromID(u32 id, ScStringView *out) {
  return ordinances ? ordinances->GetMessageText(id, out) : false;
}

// Count the actual linked advice records without acquiring a new lock, matching the original list size operation.
static u32 AdQueueSize(const AdAdviceQueue *queue) {
  u32 count = 0;
  for (AdAdviceLink *node = queue->head->next; node != queue->head; node = node->next)
    ++count;
  return count;
}

// Initialize the owned Strings, picture keys and forced-advice list before constructing the three queue categories.
// Domain-owned IDs, city/petitioner pointers and the unidentified DWORD retain the original uninitialized state.
AdBaseAdvisor::AdBaseAdvisor() {
  small_picture = {0, 0, 0};
  large_picture = {0, 0, 0};
  advice = nullptr;
  ordinances = nullptr;
  help = nullptr;
  forced = new AdForcedLink;
  forced->next = forced;
  forced->previous = forced;
  simulation_pending = 0;
  advice = new AdAdviceQueue(this, 0);
  help = new AdAdviceQueue(this, 2);
  ordinances = new AdAdviceQueue(this, 1);
}

// Clear and destroy advice, ordinances and help in that order, then release the forced-advice list.
AdBaseAdvisor::~AdBaseAdvisor() {
  if (advice) {
    advice->Clear();
    delete advice;
    advice = nullptr;
  }
  if (ordinances) {
    ordinances->Clear();
    delete ordinances;
    ordinances = nullptr;
  }
  if (help) {
    help->Clear();
    delete help;
    help = nullptr;
  }
  AdForcedLink *node = forced->next;
  while (node != forced) {
    AdForcedLink *next = node->next;
    delete static_cast<AdForcedNode *>(node);
    node = next;
  }
  forced->next = forced;
  forced->previous = forced;
  delete forced;
}

// Publish the original advisor or layer subobject and retain it through virtual AddRef.
// Unknown and CityAgent queries use the real secondary city-agent base, including its genuine failure output clearing.
bool AdBaseAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdAdvisor, AdAdvisor);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  return CtAgentBase::QueryInterface(iid, out);
}

u32 AdBaseAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdBaseAdvisor::Release() {
  return CtAgentBase::Release();
}

// Return the actual secondary CityAgent interface without retaining it.
CtAgent *AdBaseAdvisor::AsISC3CityAgent() {
  return static_cast<CtAgentBase *>(this);
}

void AdBaseAdvisor::GetName(ScStringView *out) {
  GetNameAdvice(out);
}

void AdBaseAdvisor::SetName(ScStringView *name) {
  SetNameAdvice(name);
}

bool AdBaseAdvisor::GetNameAdvice(ScStringView *out) {
  if (!name_advice.Strlen()) {
    AdTextHandle resource(kRsCommonNONameWrongStringText, kAdFallbackNameGroup);
    name_advice.Copy(resource.ToString());
  }
  out->Copy(name_advice);
  return true;
}

bool AdBaseAdvisor::SetNameAdvice(ScStringView *name) {
  name_advice.Copy(*name);
  return true;
}

// Resolve the shared fallback only when the ordinance label is empty, then copy the owned label to the output String.
bool AdBaseAdvisor::GetNameOrdinance(ScStringView *out) {
  if (!name_ordinance.Strlen()) {
    AdTextHandle resource(kRsCommonNONameWrongStringText, kAdFallbackNameGroup);
    name_ordinance.Copy(resource.ToString());
  }
  out->Copy(name_ordinance);
  return true;
}

bool AdBaseAdvisor::SetNameOrdinance(ScStringView *name) {
  name_ordinance.Copy(*name);
  return true;
}

// Supply the common fallback for an empty briefing label and copy the resulting String to the caller.
bool AdBaseAdvisor::GetNameBriefings(ScStringView *out) {
  if (!name_briefings.Strlen()) {
    AdTextHandle resource(kRsCommonNONameWrongStringText, kAdFallbackNameGroup);
    name_briefings.Copy(resource.ToString());
  }
  out->Copy(name_briefings);
  return true;
}

bool AdBaseAdvisor::SetNameBriefings(ScStringView *name) {
  name_briefings.Copy(*name);
  return true;
}

// Lazily supply the original default personal name, then copy the owned value to the caller's String.
bool AdBaseAdvisor::GetPersonalName(ScStringView *out) {
  if (!personal_name.Strlen()) {
    AdTextHandle resource(kRsCommonNONameWrongStringText, kAdFallbackNameGroup);
    personal_name.Copy(resource.ToString());
  }
  out->Copy(personal_name);
  return true;
}

bool AdBaseAdvisor::SetPersonalName(ScStringView *name) {
  personal_name.Copy(*name);
  return true;
}

// Borrow the small picture key only when its type field marks it present.
RsKey *AdBaseAdvisor::GetSmallAdvisorPicture() {
  return small_picture.type ? &small_picture : nullptr;
}

// Borrow the large picture key only when its type field marks it present.
RsKey *AdBaseAdvisor::GetLargeAdvisorPicture() {
  return large_picture.type ? &large_picture : nullptr;
}

bool AdBaseAdvisor::SetSmallAdvisorPicture(const RsKey &key) {
  small_picture = key;
  return true;
}

bool AdBaseAdvisor::SetLargeAdvisorPicture(const RsKey &key) {
  large_picture = key;
  return true;
}

u32 AdBaseAdvisor::GetAdvisorAverageMood() {
  return advice->GetMood();
}

u32 AdBaseAdvisor::GetOutdatedResponseID() {
  return outdated_response;
}

u32 AdBaseAdvisor::GetAdviceQueueSize() {
  return advice ? AdQueueSize(advice) : 0;
}

u32 AdBaseAdvisor::GetHelpQueueSize() {
  return help ? AdQueueSize(help) : 0;
}

u32 AdBaseAdvisor::GetOrdinanceQueueSize() {
  return ordinances ? AdQueueSize(ordinances) : 0;
}

bool AdBaseAdvisor::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool AdBaseAdvisor::StaticInit(CtDefinition *) {
  return true;
}

bool AdBaseAdvisor::StaticShutdown() {
  return true;
}

// Publish the city before querying and storing its petitioner manager, then preserve the original true result.
bool AdBaseAdvisor::Init(CtCity *input) {
  city = input;
  petitioners = input->GetPetitionerManager();
  return true;
}

bool AdBaseAdvisor::SimulationBegin() {
  GetSimulationData();
  CheckOnAdvice();
  return true;
}

bool AdBaseAdvisor::SimulationEnd() {
  return true;
}

bool AdBaseAdvisor::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

bool AdBaseAdvisor::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdBaseAdvisor::DebugClassTag(ScStringView *) {
  return false;
}

bool AdBaseAdvisor::DebugTypeTag(ScStringView *) {
  return false;
}

bool AdBaseAdvisor::Init(CtCity *, ScImporter *) {
  return true;
}

// Append the full DWORD advice ID while the current city is locked. Query the current city again before unlocking.
bool AdBaseAdvisor::AddForcedAdvice(u32 id) {
  AdGetCity()->Lock();
  AdForcedNode *node = new AdForcedNode;
  node->advice = id;
  node->next = forced;
  node->previous = forced->previous;
  node->previous->next = node;
  forced->previous = node;
  AdGetCity()->Unlock();
  return true;
}

// Visit forced advice in stored order and issue each original five-argument ordinary-advice request.
// Records remain in the forced list so later simulation passes can reactivate them again.
bool AdBaseAdvisor::ProcessForcedAdvice() {
  for (AdForcedLink *node = forced->next; node != forced; node = node->next)
    AddAdvice(static_cast<AdForcedNode *>(node)->advice, 0, 0, 0, 0);
  return true;
}

// Remove forced IDs from the back under the current city's lock, preserving the original separate unlock lookup.
bool AdBaseAdvisor::ClearForcedAdvice() {
  AdGetCity()->Lock();
  while (forced->next != forced) {
    AdForcedLink *node = forced->previous;
    node->previous->next = forced;
    forced->previous = node->previous;
    delete static_cast<AdForcedNode *>(node);
  }
  AdGetCity()->Unlock();
  return true;
}

// Clear and destroy all three queues before clearing forced advice under the application's current-city lock.
// Null each field after its destruction so subsequent teardown observes the original shutdown state.
bool AdBaseAdvisor::Shutdown() {
  if (advice) {
    advice->Clear();
    delete advice;
    advice = nullptr;
  }
  if (ordinances) {
    ordinances->Clear();
    delete ordinances;
    ordinances = nullptr;
  }
  if (help) {
    help->Clear();
    delete help;
    help = nullptr;
  }
  ClearForcedAdvice();
  return true;
}

// Create an ordinary advice notification from its resolved, expanded resource text. Secondary queue categories return zero.
// The synchronous receiver writes the notification ID into the original payload before the temporary String is released.
u32 AdAdviceQueue::CreateNotification(u32 id) {
  if (category)
    return 0;
  AdTextHandle resource(id, notification_group);
  if (!resource.IsValid())
    return 0;
  ScString *text = new ScString;
  resource.CopyTo(text);
  RsKey key;
  resource.GetKey(&key);
  u32 advisor_id = advisor->GetAdvisorID();
  text->AddRef();
  AdNotification notification = {0, key.group, key.instance, text, kWnMsg_AdvisorTopic, advisor_id, id, 0};
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0};
  AdGetMessageServer()->MessageSend(message);
  text->Release();
  return notification.notification;
}

// Zero the source-unwritten instance word to exclude undefined payload bytes; no original instance value is asserted.
void AdShowTextNotification(u32 text_id) {
  ScString *text = new ScString;
  AdTextHandle resource(text_id, kRsNewsTextGroup);
  resource.CopyTo(text);
  AdNotification notification = {0, kRsNewsTextGroup, 0, text, 0, 0, 0, 0};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0};
  AdGetMessageServer()->MessageSend(message);
  text->Release();
}

bool AdAdviceQueue::RecreateNotification(u32 id) {
  if (category)
    return false;
  AdTextHandle resource(id, notification_group);
  if (!resource.IsValid())
    return false;
  AdAdviceLink *node = head->next;
  while (node != head && static_cast<AdAdviceNode *>(node)->advice->id != id)
    node = node->next;
  if (node == head)
    return false;
  AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
  if (record->notification)
    AdDismissNotification(record->notification);
  u32 count = 0;
  for (AdTokenLink *item = record->tokens->next; item != record->tokens; item = item->next)
    ++count;
  const AdToken **tokens = nullptr;
  if (count) {
    tokens = new const AdToken *[count];
    u32 index = 0;
    for (AdTokenLink *item = record->tokens->next; item != record->tokens; item = item->next)
      tokens[index++] = &static_cast<AdTokenNode *>(item)->value;
  }
  ScString *text = new ScString;
  resource.CopyTo(text, tokens, count);
  RsKey key{};
  resource.GetKey(&key);
  text->AddRef();
  AdNotification notification = {0, key.group, key.instance, text, kWnMsg_AdvisorTopic, advisor->GetAdvisorID(), id, 0};
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0};
  if (AdGetMessageServer())
    AdGetMessageServer()->MessageSend(message);
  text->Release();
  static_cast<AdAdviceNode *>(node)->advice->notification = notification.notification;
  return true;
}

static u32 AdCountTokens(const AdAdvice *record) {
  u32 count = 0;
  for (AdTokenLink *node = record->tokens->next; node != record->tokens; node = node->next)
    ++count;
  return count;
}

// Build the original temporary argument array with direct pointers to the record's real token objects.
static const AdToken **AdCollectTokens(const AdAdvice *record, u32 count) {
  if (!count)
    return nullptr;
  const AdToken **tokens = new const AdToken *[count];
  u32 index = 0;
  for (AdTokenLink *node = record->tokens->next; node != record->tokens; node = node->next)
    tokens[index++] = &static_cast<AdTokenNode *>(node)->value;
  return tokens;
}

// Enumerate nondismissed records under the queue lock, retaining each completed header before publishing it.
// Reuse one expanded text buffer and re-resolve assigned resource keys, preserving stale text on a failed lookup.
void AdAdviceQueue::GetHeaders(u32 *count, AdHeaderRecord **out) {
  ScLock *region = lock;
  region->Lock();
  u32 written = 0;
  {
    ScString title;
    AdTextHandle resource;
    AdAdviceLink *sentinel = head;
    for (AdAdviceLink *node = sentinel->next; node != sentinel && written < *count; node = node->next) {
      AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
      if (!record || record->dismissed)
        continue;
      {
        AdTextHandle next(record->id, header_group);
        resource.Assign(next);
      }
      u32 token_count = AdCountTokens(record);
      const AdToken **tokens = AdCollectTokens(record, token_count);
      resource.CopyTo(&title, tokens, token_count);
      delete[] tokens;
      AdHeader *header = new AdHeader;
      header->SetText(&title);
      header->SetID(record->id);
      header->AddRef();
      ++written;
      *out++ = header;
    }
    *count = written;
  }
  region->Unlock();
}

// Resolve the first matching record's message, dismiss eligible advice, then copy text while retaining the queue lock.
// A missing resource still produces the original successful record lookup with an empty output String.
bool AdAdviceQueue::GetMessageText(u32 id, ScStringView *out) {
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *sentinel = head;
  for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    if (!record || record->id != id)
      continue;
    u32 count = AdCountTokens(record);
    const AdToken **tokens = AdCollectTokens(record, count);
    {
      ScString message;
      AdTextHandle(id, message_group).CopyTo(&message, tokens, count);
      Dismiss(id);
      out->Copy(message);
      delete[] tokens;
    }
    region->Unlock();
    return true;
  }
  region->Unlock();
  return false;
}

// Resolve every matching title in list order, allowing later duplicates to replace the caller's earlier output.
// Preserve the original false return even when text was copied successfully.
bool AdAdviceQueue::GetTitle(u32 id, ScStringView *out) {
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *sentinel = head;
  for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    if (record->id != id)
      continue;
    u32 count = AdCountTokens(record);
    const AdToken **tokens = AdCollectTokens(record, count);
    {
      ScString title;
      AdTextHandle(id, title_group).CopyTo(&title, tokens, count);
      out->Copy(title);
      delete[] tokens;
    }
  }
  region->Unlock();
  return false;
}

// Resolve the first record with a nonzero replacement ID, including dismissed records, into an owned message interface.
// Acquire both text resources before loading the topic picture and mood, then publish the record before retaining it.
bool AdAdviceQueue::GetStructureByReplacement(u32 replacement, AdMessageRecord **out) {
  if (!replacement)
    return false;
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *sentinel = head;
  for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    if (record->replacement != replacement)
      continue;
    u32 id = record->id, count = AdCountTokens(record);
    const AdToken **tokens = AdCollectTokens(record, count);
    {
      ScString title;
      AdTextHandle(id, header_group).CopyTo(&title, tokens, count);
      ScString message;
      AdTextHandle(id, message_group).CopyTo(&message, tokens, count);
      RsKey picture = {0, 0, 0};
      AdLoadTopicPicture(id, &picture);
      u8 mood = 0;
      AdLoadMood(id, &mood);
      AdMessage *result = new AdMessage(id, mood, picture, title, message);
      *out = result;
      result->AddRef();
      delete[] tokens;
    }
    region->Unlock();
    return true;
  }
  region->Unlock();
  return false;
}

// Resolve the first nondismissed record with this ID, or build the advisor's outdated response under the nested lock.
// Both paths preserve the original true result and publish an actual retained message interface.
bool AdAdviceQueue::GetStructure(u32 id, AdMessageRecord **out) {
  ScLock *region = lock;
  region->Lock();
  AdAdviceLink *sentinel = head;
  for (AdAdviceLink *node = sentinel->next; node != sentinel; node = node->next) {
    AdAdvice *record = static_cast<AdAdviceNode *>(node)->advice;
    if (!record || record->id != id || record->dismissed)
      continue;
    u32 count = AdCountTokens(record);
    const AdToken **tokens = AdCollectTokens(record, count);
    {
      ScString title;
      AdTextHandle(id, header_group).CopyTo(&title, tokens, count);
      ScString message;
      AdTextHandle(id, message_group).CopyTo(&message, tokens, count);
      RsKey picture = {0, 0, 0};
      AdLoadTopicPicture(id, &picture);
      u8 mood = 0;
      AdLoadMood(id, &mood);
      AdMessage *result = new AdMessage(id, mood, picture, title, message);
      *out = result;
      result->AddRef();
      delete[] tokens;
    }
    region->Unlock();
    return true;
  }
  CreateOutdatedMessage(out);
  region->Unlock();
  return true;
}

// Expand the owner's outdated-response title/body using the fixed ordinary-advice groups while holding this queue lock.
// Retain the default message before assigning its text and publishing it; its ID, mood and picture remain zero.
bool AdAdviceQueue::CreateOutdatedMessage(AdMessageRecord **out) {
  ScLock *region = lock;
  region->Lock();
  u32 id = advisor->GetOutdatedResponseID();
  {
    ScString title = AdTextHandle(id, kAdAdviceHeaderGroup).ToExpandedString();
    ScString message = AdTextHandle(id, kAdAdviceMessageGroup).ToExpandedString();
    AdMessage *result = new AdMessage;
    result->AddRef();
    result->SetTitle(title);
    result->SetMessage(message);
    *out = result;
  }
  region->Unlock();
  return true;
}
