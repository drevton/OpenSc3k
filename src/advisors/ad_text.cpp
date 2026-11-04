#include "advisors/ad_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_ini.h"
#include "advisors/ad_module.h"
#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"

AdHeader::AdHeader() : id(0), references(0) {
}

AdHeader::~AdHeader() {
}

// Publish and retain the header interface for its original two identifiers. Failed queries clear the output.
bool AdHeader::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AdHeaderRecord);
  HANDLE_QUERY_INTERFACE(IID_AdHeaderRecord, AdHeaderRecord);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the record count before retaining the module and return the live record count afterward.
u32 AdHeader::AddRef() {
  ++references;
  AdGetDirector()->AddRef();
  return references;
}

// Nonfinal releases decrement only the record count. The original drops one module reference only on final deletion.
u32 AdHeader::Release() {
  if (references > 1)
    return --references;
  AdGetDirector()->RemoveRef();
  delete this;
  return 0;
}

// Publish a separate owned text copy before retaining its String interface. The returned copy does not alias the header.
bool AdHeader::GetText(ScStringView **out) {
  ScString *copy = new ScString(text);
  *out = copy;
  copy->AddRef();
  return true;
}

bool AdHeader::CopyText(ScStringView *out) {
  out->Copy(text);
  return true;
}

// Borrow the actual mutable embedded String without acquiring another reference.
ScStringView *AdHeader::Text() {
  return &text;
}

u32 AdHeader::GetID() {
  return id;
}

bool AdHeader::SetText(ScStringView *source) {
  text.Copy(*source);
  return true;
}

void AdHeader::SetID(u32 value) {
  id = value;
}

AdMessage::AdMessage() : id(0), mood(0), picture{0, 0, 0}, references(0) {
}

AdMessage::AdMessage(u32 value, u8 level, const RsKey &key, const ScString &heading, const ScString &body)
    : id(value), title(heading), message(body), mood(level), picture(key), references(0) {
}

AdMessage::~AdMessage() {
}

// Publish and retain the message interface for either original identifier; failed queries clear the output.
bool AdMessage::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AdMessageRecord);
  HANDLE_QUERY_INTERFACE(IID_AdMessageRecord, AdMessageRecord);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the director, then return the current object count.
u32 AdMessage::AddRef() {
  ++references;
  AdGetDirector()->AddRef();
  return references;
}

// Nonfinal releases change only the object count; final deletion drops one director reference and destroys both Strings.
u32 AdMessage::Release() {
  if (references > 1)
    return --references;
  AdGetDirector()->RemoveRef();
  delete this;
  return 0;
}

u32 AdMessage::GetID() {
  return id;
}

u32 AdMessage::GetMood() {
  return mood;
}

// Borrow the embedded complete topic-picture key without allocation or reference changes.
RsKey *AdMessage::GetPicture() {
  return &picture;
}

// Borrow the actual mutable title String, whose lifetime follows the message record.
ScStringView *AdMessage::Title() {
  return &title;
}

// Publish a separate owned title copy before retaining its String interface and returning true.
bool AdMessage::GetTitle(ScStringView **out) {
  ScString *copy = new ScString(title);
  *out = copy;
  copy->AddRef();
  return true;
}

bool AdMessage::CopyTitle(ScStringView *out) {
  out->Copy(title);
  return true;
}

// Borrow the embedded mutable message String without retaining or copying it.
ScStringView *AdMessage::Message() {
  return &message;
}

// Publish a separate owned message copy, retain it after publication and return the original true result.
bool AdMessage::GetMessageText(ScStringView **out) {
  ScString *copy = new ScString(message);
  *out = copy;
  copy->AddRef();
  return true;
}

bool AdMessage::CopyMessage(ScStringView *out) {
  out->Copy(message);
  return true;
}

// Assign the concrete title String and return its embedded address, matching the original assignment result.
ScString *AdMessage::SetTitle(const ScString &source) {
  title = source;
  return &title;
}

// Assign the concrete body String and return the embedded String's original assignment result.
ScString *AdMessage::SetMessage(const ScString &source) {
  message = source;
  return &message;
}

// Replace the matched byte range using the original concrete String storage and insertion growth rule.
// Retain the allocation when it fits, including the terminating-byte move; actual library range/length failures are fatal.
static void AdReplaceTokenRange(ScString *text, u32 position, u32 count, const ScString &replacement) {
  u32 length = static_cast<u32>(text->end - text->begin);
  if (position > length)
    ScFatal(SC_ERROR_INVALID, "Token replacement position exceeds String length");
  if (count > length - position)
    count = length - position;
  u32 inserted = static_cast<u32>(replacement.end - replacement.begin), remaining = length - count;
  if (inserted == UINT32_MAX || remaining >= UINT32_MAX - 1 - inserted)
    ScFatal(SC_ERROR_INVALID, "Token replacement exceeds String length limit");
  u32 total = remaining + inserted, tail = length - position - count;
  if (total < static_cast<u32>(text->storage_end - text->begin)) {
    if (inserted)
      memcpy(text->begin + position, replacement.begin, inserted < count ? inserted : count);
    memmove(text->begin + position + inserted, text->begin + position + count, tail + 1);
    if (inserted > count)
      memcpy(text->begin + position + count, replacement.begin + count, inserted - count);
    text->end = text->begin + total;
    return;
  }
  u32 extra = inserted - count;
  u64 capacity = static_cast<u64>(length) + (length > extra ? length : extra) + 1;
  if (capacity > UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Token replacement exceeds String capacity limit");
  char *copy = new char[static_cast<usize>(capacity)];
  if (position)
    memcpy(copy, text->begin, position);
  if (inserted)
    memcpy(copy + position, replacement.begin, inserted);
  memcpy(copy + position + inserted, text->begin + position + count, tail + 1);
  delete[] text->begin;
  text->begin = copy;
  text->end = copy + total;
  text->storage_end = copy + capacity;
}

// Ignore formatter results, replace at the live output position and advance it by replacement length minus one.
bool AD_STDCALL AdTryParameterToken(ScString *text, const AdToken *token, u32 *position) {
  ScString name(static_cast<const ScStringView &>(token->text)), candidate;
  u32 start = *position, length = static_cast<u32>(text->end - text->begin), name_length = name.Strlen();
  if (start < length) {
    u32 copied = name_length < length - start ? name_length : length - start;
    candidate.FromChar(text->begin + start, copied);
  }
  if (!candidate.IsEqual(&name))
    return false;
  ScString replacement;
  switch (token->kind) {
  case 1: {
    replacement = AdTextHandle(token->first, token->second).ToString();
    break;
  }
  case 2: {
    ScLanguageUtility *utility = AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return false;
    utility->MakeNumberString(static_cast<i64>(token->third), &replacement);
    break;
  }
  case 3: {
    ScLanguageUtility *utility = AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return false;
    utility->MakeMoneyString(static_cast<i64>(token->third), &replacement);
    break;
  }
  case 4: {
    CtCity *city = AdGetCity();
    if (!city)
      return false;
    CtNeighborsLayer *neighbors = city->NeighborsLayer();
    if (!neighbors)
      return false;
    neighbors->GetNeighborName(static_cast<u8>(token->first), &replacement);
    break;
  }
  case 5: {
    char bytes[12];
    int size = snprintf(bytes, sizeof(bytes), "%d", token->third);
    replacement.FromChar(bytes, static_cast<u32>(size));
    break;
  }
  default:
    return false;
  }
  AdReplaceTokenRange(text, *position, name_length, replacement);
  *position += replacement.Strlen() - 1;
  return true;
}

AdTextHandle::AdTextHandle() : text(nullptr), key{0, 0, 0} {
}

AdTextHandle::AdTextHandle(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

AdTextHandle::~AdTextHandle() {
  ScRelease(&text);
}

// Request the text selector through the real module service. Failure explicitly clears any pointer it published.
bool AdTextHandle::Resolve() {
  bool result = AdGetTextManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text));
  if (!result)
    text = nullptr;
  return result;
}

bool AdTextHandle::IsValid() const {
  return text != nullptr;
}

// Publish the text resource key through the genuine output pointer and return that same pointer.
RsKey *AdTextHandle::GetKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
  return out;
}

// Missing resources leave the caller's String untouched.
bool AdTextHandle::CopyTo(ScStringView *out) {
  if (!text)
    return false;
  ScString expanded(*text);
  AdExpandTokens(&expanded);
  out->FromChar(expanded.Data(), expanded.Strlen());
  return true;
}

// Missing text converts to an empty copy.
ScString AdTextHandle::ToString() const {
  if (text)
    return ScString(*text);
  return ScString();
}

// Release the former interface, copy the source key and resolve it again rather than retaining the source interface.
AdTextHandle *AdTextHandle::Assign(const AdTextHandle &source) {
  if (&source != this) {
    ScRelease(&text);
    source.GetKey(&key);
    Resolve();
  }
  return this;
}

// Failed resolution leaves the caller's existing contents intact.
bool AdTextHandle::CopyTo(ScStringView *out, const AdToken *const *tokens, u32 count) {
  if (!text)
    return false;
  ScString expanded(*text);
  AdExpandTokens(&expanded, tokens, count);
  out->FromChar(expanded.Data(), expanded.Strlen());
  return true;
}

// Copy the resolved resource and replace its first C-string needle match with the complete replacement byte range.
bool AdTextHandle::CopyWithReplacement(const ScStringView *needle, const ScStringView *replacement, ScString *out) {
  *out = ScString(*text);
  const char *search = needle->ToChar();
  usize length = strlen(search);
  u32 size = out->ScString::Strlen(), position = 0;
  while (position < size && (length > size - position || memcmp(out->ScString::Data() + position, search, length)))
    ++position;
  if (position >= size)
    return false;
  u32 replacement_length = replacement->Strlen();
  const char *data = replacement->Data();
  u32 count = needle->Strlen();
  out->Replace(position, count, data, replacement_length);
  return true;
}

// Missing resources return an empty String.
ScString AdTextHandle::ToExpandedString() const {
  if (text) {
    ScString expanded(*text);
    AdExpandTokens(&expanded);
    return expanded;
  }
  return ScString();
}

// Search ordinary then Gold mood files using the full formatted advice ID and the real loose/package INI reader.
// Narrow a found integer to its raw byte; both missing mappings clear the output and return false.
bool AdLoadMood(u32 id, u8 *mood) {
  for (u32 pass = 0; pass < 2; ++pass) {
    ScString path;
    AdIni reader;
    AdGetApplication()->GetAppDataDirectory(&path);
    const char *suffix = pass ? "\\Sys\\GoldAdvisorMoods.INI" : "\\Sys\\AdvisorMoods.INI";
    path.ScString::Append(suffix, static_cast<u32>(strlen(suffix)));
    reader.SetPath(path);
    AdGetApplication()->GetAppDataDirectory(&path);
    path.ScString::Append("\\Sys\\SYS.PAK", 12);
    reader.SetCompressedPath(path);
    char bytes[11];
    snprintf(bytes, sizeof(bytes), "0x%08X", id);
    ScString key(bytes), value;
    bool found;
    {
      ScString section("AdvisorMoods");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      *mood = static_cast<u8>(atoi(value.begin));
      return true;
    }
  }
  *mood = 0;
  return false;
}

// Resolve a topic picture from the ordinary then Gold INI files, parsing the comma-separated group and instance.
// A found row is successful even when its parsed words are zero; only two missing mappings clear the whole key.
bool AdLoadTopicPicture(u32 id, RsKey *out) {
  for (u32 pass = 0; pass < 2; ++pass) {
    ScString path;
    AdIni reader;
    AdGetApplication()->GetAppDataDirectory(&path);
    const char *suffix = pass ? "\\Sys\\GoldAdvisor-TopicGfxIds.ini" : "\\Sys\\Advisor-TopicGfxIDs.INI";
    path.ScString::Append(suffix, static_cast<u32>(strlen(suffix)));
    reader.SetPath(path);
    AdGetApplication()->GetAppDataDirectory(&path);
    path.ScString::Append("\\Sys\\SYS.PAK", 12);
    reader.SetCompressedPath(path);
    char bytes[11];
    snprintf(bytes, sizeof(bytes), "0x%08X", id);
    ScString key(bytes), value;
    bool found;
    {
      ScString section("AdvisorTopicGfxIDs");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      out->type = kRsImageResourceType;
      ScString group;
      u32 length = static_cast<u32>(value.end - value.begin), position = 0;
      while (position < length && value.begin[position] != ',')
        ++position;
      if (position < length) {
        group.ScString::FromChar(value.begin, position);
        value.ScString::Erase(0, position + 1);
      }
      out->group = AdParseIniNumber(group.begin);
      out->instance = AdParseIniNumber(value.begin);
      return true;
    }
  }
  *out = {0, 0, 0};
  return false;
}

static bool AdBuiltinMatches(const ScString &first, const ScString &second) {
  u32 length = static_cast<u32>(first.end - first.begin);
  return length == static_cast<u32>(second.end - second.begin) && !memcmp(first.begin, second.begin, length);
}

// Replace a matched builtin with concrete String storage, then advance by the inserted length minus one.
// Empty replacements intentionally wrap the scan word so the outer scanner resumes at the original position.
static void AdReplaceBuiltin(ScString *text, const ScString &token, u32 *position, const ScString &replacement) {
  u32 length = static_cast<u32>(replacement.end - replacement.begin);
  u32 removed = token.Strlen();
  text->ScString::Replace(*position, removed, replacement.begin, length);
  *position += length - 1;
}

static ScString AdParadeName(u32 instance) {
  AdTextHandle resource(instance, kRsSupplementalGameTextGroup);
  return resource.ToString();
}

// Preserve repeated borrowed-name getters, missing-city results and the mutable full-width scan position.
bool AD_STDCALL AdTryToken(ScString *text, const ScString &token, u32 *position) {
  u32 count = token.Strlen(), length = static_cast<u32>(text->end - text->begin);
  if (*position > length)
    ScFatal(SC_ERROR_INVALID, "Advisor token position exceeds its String");
  if (count > length - *position)
    count = length - *position;
  ScString candidate(text->begin + *position, count);
  if (!AdBuiltinMatches(candidate, token))
    return false;
  if (AdBuiltinMatches(token, AdTokenNames[0]) || AdBuiltinMatches(token, AdTokenNames[1])) {
    CtCity *city = AdGetCity();
    if (city) {
      ScStringView *first = city->GetMayorName(), *second = city->GetMayorName();
      u32 inserted = first->Strlen();
      const char *data = second->Data();
      u32 removed = token.Strlen();
      text->ScString::Replace(*position, removed, data, inserted);
      *position += city->GetMayorName()->Strlen() - 1;
    }
    return true;
  }
  if (AdBuiltinMatches(token, AdTokenNames[2]) || AdBuiltinMatches(token, AdTokenNames[3])) {
    CtCity *city = AdGetCity();
    if (city) {
      ScStringView *first = city->GetCityName(), *second = city->GetCityName();
      u32 inserted = first->Strlen();
      const char *data = second->Data();
      u32 removed = token.Strlen();
      text->ScString::Replace(*position, removed, data, inserted);
      *position += city->GetCityName()->Strlen() - 1;
    }
    return true;
  }
  if (AdBuiltinMatches(token, AdTokenNames[4])) {
    ScString fallback("0");
    CtCity *city = AdGetCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      char bytes[12];
      snprintf(bytes, sizeof(bytes), "%d", population);
      ScString replacement(bytes);
      AdReplaceBuiltin(text, token, position, replacement);
    } else
      AdReplaceBuiltin(text, token, position, fallback);
    return true;
  }
  if (AdBuiltinMatches(token, AdTokenNames[5])) {
    CtCity *city = AdGetCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      char bytes[12];
      snprintf(bytes, sizeof(bytes), "%d", static_cast<i32>(year));
      ScString replacement(bytes);
      AdReplaceBuiltin(text, token, position, replacement);
    } else {
      ScString fallback("1900");
      AdReplaceBuiltin(text, token, position, fallback);
    }
    return true;
  }
  if (AdBuiltinMatches(token, AdTokenNames[7])) {
    CtCity *city = AdGetCity();
    if (!city)
      return false;
    CtNeighborsLayer *neighbors = city->NeighborsLayer();
    if (!neighbors)
      return false;
    AdRandom random(kScRandomSeedFromClock);
    ScString replacement;
    neighbors->GetNeighborName(static_cast<u8>(random.Next(4)), &replacement);
    AdReplaceBuiltin(text, token, position, replacement);
    return true;
  }
  if (AdBuiltinMatches(token, AdTokenNames[6])) {
    ScString replacement;
    u32 instance = 0;
    switch (static_cast<u32>(AdGetAppPreferences()->GetCountry())) {
    case 6:
      instance = 600;
      break;
    case 17:
      instance = 601;
      break;
    case 19:
      instance = 602;
      break;
    case 31:
      instance = 603;
      break;
    case 32:
      instance = 604;
      break;
    case 45:
      instance = 605;
      break;
    case 46:
      instance = 606;
      break;
    case 49:
      instance = 607;
      break;
    case 54:
      instance = 608;
      break;
    }
    if (instance)
      replacement = AdParadeName(instance);
    AdReplaceBuiltin(text, token, position, replacement);
  }
  return true;
}

AdToken::AdToken() : kind(0), text("%TOKEN%", 7), first(0), second(0), third(0) {
}

// Construct empty text before publishing the kind and copying the supplied interface through a temporary String.
// This numeric form clears both resource arguments and keeps the full signed replacement value.
AdToken::AdToken(u32 type, const ScStringView *name, i32 value) {
  kind = type;
  {
    ScString copy(*name);
    text = copy;
  }
  first = 0;
  second = 0;
  third = value;
}

// The second argument and signed numeric value start at zero in this selector form.
AdToken::AdToken(u32 type, const ScStringView *name, u32 id) {
  kind = type;
  {
    ScString copy(*name);
    text = copy;
  }
  second = 0;
  third = 0;
  first = id;
}

AdToken::AdToken(const AdToken &source)
    : kind(source.kind), text(source.text), first(source.first), second(source.second), third(source.third) {
}

AdToken::~AdToken() {
}

u8 AdToken::Read(u32, RsStream *stream) {
  u8 result = stream->ReadU32(&kind);
  result = result && stream->ReadString(&text) && stream->ReadU32(&first) && stream->ReadU32(&second) &&
      stream->ReadI32(&third);
  return result;
}

u8 AdToken::Write(RsStream *stream) {
  u8 result = stream->WriteU32(kind);
  result = result && stream->WriteString(&text) && stream->WriteU32(first) && stream->WriteU32(second) &&
      stream->WriteI32(third);
  return result;
}

const ScString AdTokenNames[8] = {ScString("%MAYOR%", 7), ScString("%YOURNAME%", 10), ScString("%CITYNAME%", 10),
    ScString("%YOURCITY%", 10), ScString("%POPULATION%", 12), ScString("%YEAR%", 6), ScString("%PARADENAME%", 12),
    ScString("%ANYNEIGHBOR%", 13)};

// Find the next percent byte within the live String range.
static u32 AdFindPercent(const ScString &text, u32 start) {
  u32 length = text.Strlen();
  const char *bytes = text.Data();
  for (u32 index = start; index < length; ++index)
    if (bytes[index] == '%')
      return index;
  return UINT32_MAX;
}

// Read the live String range after each substitution because replacements can change both text and length.
u32 AD_STDCALL AdExpandTokens(ScString *text) {
  u32 position = AdFindPercent(*text, 0);
  while (position < text->Strlen()) {
    AdSubstituteToken(text, &position);
    position = AdFindPercent(*text, position + 1);
  }
  return position;
}

// Try the original eight names in their short-circuit order. AnyNeighbor precedes ParadeName despite their init order.
bool AD_STDCALL AdSubstituteToken(ScString *text, u32 *position) {
  return AdTryToken(text, AdTokenNames[0], position) || AdTryToken(text, AdTokenNames[1], position) ||
      AdTryToken(text, AdTokenNames[2], position) || AdTryToken(text, AdTokenNames[3], position) ||
      AdTryToken(text, AdTokenNames[4], position) || AdTryToken(text, AdTokenNames[5], position) ||
      AdTryToken(text, AdTokenNames[7], position) || AdTryToken(text, AdTokenNames[6], position);
}

// Try built-in substitutions first at each percent byte. Each leaf may change the shared scan position and String
// length before scanning resumes one byte afterward.
u32 AD_STDCALL AdExpandTokens(ScString *text, const AdToken *const *tokens, u32 count) {
  u32 position = AdFindPercent(*text, 0);
  while (position < text->Strlen()) {
    if (!AdSubstituteToken(text, &position) && tokens && count)
      for (u32 index = 0; index < count; ++index)
        AdTryParameterToken(text, tokens[index], &position);
    position = AdFindPercent(*text, position + 1);
  }
  return position;
}
