#include <ctype.h>

#include <cassert>
#include <cstring>

#include "common/sc3_cmdline.h"

static void ScCommandLower(ScString *value) {
  value->MakeLower();
}

static bool ScCommandEqual(const ScString &first, const ScString &second) {
  usize length = first.end - first.begin;
  return length == (usize)(second.end - second.begin) && !memcmp(first.begin, second.begin, length);
}

static u32 ScCommandFind(const ScString &text, const ScString &needle) {
  u32 length = (u32)(text.end - text.begin), count = (u32)(needle.end - needle.begin);
  if (count <= length)
    for (u32 i = 0; i <= length - count; ++i)
      if (!memcmp(text.begin + i, needle.begin, count))
        return i;
  return UINT32_MAX;
}

static bool ScCommandPrefix(char value) {
  return value == '/' || value == '-' || value == '\\';
}

// Produce the manually paired ASCII key used by character switches; other bytes keep their original value.
static char ScCommandAlternate(char value) {
  if (value >= 'A' && value <= 'Z')
    return value + ('a' - 'A');
  if (value >= 'a' && value <= 'z')
    return value - ('a' - 'A');
  return value;
}

ScCommandLine::ScCommandLine() {
}

// Retain space-trimmed raw text and split on space/tab/LF outside odd double-quote parity.
// Only consecutive spaces are skipped after a split, so tab/LF runs can create empty arguments.
ScCommandLine::ScCommandLine(const char *line) : references(0), raw(line) {
  while (raw.end != raw.begin && raw.end[-1] == ' ')
    raw.Erase((u32)(raw.end - raw.begin) - 1, 1);
  while (raw.end != raw.begin && raw.begin[0] == ' ')
    raw.Erase(0, 1);
  u32 length = (u32)(raw.end - raw.begin), first = 0, quotes = 0;
  if (!length)
    return;
  for (u32 i = 0; i < length; ++i) {
    char byte = raw.begin[i];
    if (byte == ' ' || byte == '\t' || byte == '\n') {
      if (!(quotes & 1)) {
        quotes = 0;
        AppendToken(raw.begin + first, i - first);
        while (i + 1 < length && raw.begin[i + 1] == ' ')
          ++i;
        first = i + 1;
      }
    } else if (byte == '"')
      ++quotes;
  }
  AppendToken(raw.begin + first, length - first);
}

ScCommandLine::ScCommandLine(const ScCommandLine &source)
    : references(0), arguments(source.arguments), raw(source.raw) {
}

// Copy raw text through the source interface before capturing its argument count and copying each argument.
// Grow at the original vector boundaries; the independent fallback String starts empty.
ScCommandLine::ScCommandLine(const ScCmdLine &source) : references(0) {
  source.GetCommandLineText(&raw);
  u32 count = source.argc();
  for (u32 i = 0; i < count; ++i) {
    ScString argument(source.argv(i));
    GrowArguments();
    arguments.push_back(argument);
  }
}

ScCommandLine::~ScCommandLine() {
}

// Assign reference count, vector, raw text and fallback in original order; vector assignment reuses live elements.
ScCommandLine &ScCommandLine::operator=(const ScCommandLine &source) {
  references = source.references;
  arguments = source.arguments;
  raw = source.raw;
  fallback = source.fallback;
  return *this;
}

void ScCommandLine::GrowArguments() {
  if (arguments.size() == arguments.capacity())
    arguments.reserve(arguments.empty() ? 1 : arguments.size() * 2);
}

void ScCommandLine::AppendToken(const char *first, u32 length) {
  if (length && first[0] == '"' && first[length - 1] == '"') {
    --length;
    if (length) {
      ++first;
      --length;
    }
  }
  ScString token(first, length);
  GrowArguments();
  arguments.push_back(token);
}

// Publish this primary identity before retaining it; unsupported identities explicitly clear the output.
bool ScCommandLine::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandLine);
  HANDLE_QUERY_INTERFACE(IID_ScCommandLine, ScCommandLine);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScCommandLine::AddRef() {
  return ++references;
}

// Only counts above one decrement; zero and one destroy the complete owner without first storing zero.
u32 ScCommandLine::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

i32 ScCommandLine::argc() const {
  return (i32)arguments.size();
}

const char *const ScCommandLine::argv(i32 index) const {
  return (u32)index < arguments.size() ? arguments[(u32)index].begin : fallback.begin;
}

// Borrow the actual mutable String object; repeated out-of-range queries return the same fallback object.
ScString *ScCommandLine::GetArgument(u32 index) {
  return index < arguments.size() ? &arguments[index] : &fallback;
}

bool ScCommandLine::GetCommandLineText(ScStringView *text) const {
  text->Copy(raw);
  return true;
}

i32 ScCommandLine::Find(const ScStringView &value, bool case_sensitive) const {
  ScString needle(value);
  if (!case_sensitive)
    ScCommandLower(&needle);
  for (u32 i = 0; i < arguments.size(); ++i) {
    if (case_sensitive) {
      if (ScCommandEqual(needle, arguments[i]))
        return (i32)i;
    } else {
      ScString candidate(arguments[i]);
      ScCommandLower(&candidate);
      if (ScCommandEqual(needle, candidate))
        return (i32)i;
    }
  }
  return -1;
}

i32 ScCommandLine::FindSubString(const ScStringView &value, bool case_sensitive) const {
  ScString needle(value);
  if (!case_sensitive)
    ScCommandLower(&needle);
  for (u32 i = 0; i < arguments.size(); ++i) {
    if (case_sensitive) {
      if (ScCommandFind(arguments[i], needle) < (u32)(arguments[i].end - arguments[i].begin))
        return (i32)i;
    } else {
      ScString candidate(arguments[i]);
      ScCommandLower(&candidate);
      if (ScCommandFind(candidate, needle) < (u32)(candidate.end - candidate.begin))
        return (i32)i;
    }
  }
  return -1;
}

// Match the first prefixed ASCII character switch. Bare switches leave output untouched; only paired double quotes strip.
bool ScCommandLine::IsSwitchPresent(char argument, ScStringView *value, bool option) const {
  char alternate = ScCommandAlternate(argument);
  for (const ScString &token : arguments) {
    u32 length = (u32)(token.end - token.begin);
    if (length < 2 || !ScCommandPrefix(token.begin[0]) || (token.begin[1] != argument && token.begin[1] != alternate))
      continue;
    if (length == 2)
      return true;
    u32 first = 2, count = length - 2;
    if (option && length >= 4 && token.begin[2] == '"' && token.end[-1] == '"') {
      first = 3;
      count -= 2;
    }
    ScString result(token.begin + first, count);
    value->Copy(result);
    return true;
  }
  return false;
}

// Match a case-folded name followed by colon and a nonempty value, retaining its original case in the output.
bool ScCommandLine::IsSwitchPresent(const ScStringView &argument, ScStringView *value, bool option) const {
  ScString needle(argument);
  ScCommandLower(&needle);
  u32 count = (u32)(needle.end - needle.begin);
  if (!count)
    return false;
  for (const ScString &token : arguments) {
    u32 length = (u32)(token.end - token.begin);
    if (length < count + 3 || !ScCommandPrefix(token.begin[0]) || token.begin[count + 1] != ':')
      continue;
    ScString candidate(token);
    ScCommandLower(&candidate);
    if (memcmp(candidate.begin + 1, needle.begin, count))
      continue;
    ScString result(token.begin + count + 2, length - count - 2);
    if (option && (result.begin[0] == '"' || result.begin[0] == '\'')) {
      char last = result.end[-1];
      if (last == '"' || last == '\'') {
        assert(result.end - result.begin >= 2);
        result.Erase(0, 1);
        result.Erase((u32)(result.end - result.begin) - 1, 1);
      }
    }
    value->Copy(result);
    return true;
  }
  return false;
}

// Return the first prefixed character match at or after the clamped start index; no value or colon is required.
i32 ScCommandLine::GetIndexOfSwitch(char argument, i32 start) const {
  char alternate = ScCommandAlternate(argument);
  for (u32 i = start < 0 ? 0 : (u32)start; i < arguments.size(); ++i) {
    const ScString &token = arguments[i];
    if (token.end - token.begin >= 2 && ScCommandPrefix(token.begin[0]) &&
        (token.begin[1] == argument || token.begin[1] == alternate))
      return (i32)i;
  }
  return -1;
}

// This lookup intentionally does not require the colon/value used by IsSwitchPresent.
i32 ScCommandLine::GetIndexOfSwitch(const ScStringView &argument, i32 start) const {
  ScString needle(argument);
  ScCommandLower(&needle);
  for (u32 i = start < 0 ? 0 : (u32)start; i < arguments.size(); ++i) {
    const ScString &token = arguments[i];
    if (token.end - token.begin < 2 || !ScCommandPrefix(token.begin[0]))
      continue;
    ScString candidate(token.begin + 1, (u32)(token.end - token.begin) - 1);
    ScCommandLower(&candidate);
    if (ScCommandFind(candidate, needle) == 0)
      return (i32)i;
  }
  return -1;
}

void ScCommandLine::RebuildText() {
  raw.Erase(0, (u32)(raw.end - raw.begin));
  for (u32 i = 0; i < arguments.size(); ++i) {
    raw.Append(arguments[i].begin, (u32)(arguments[i].end - arguments[i].begin));
    if (i + 1 < arguments.size())
      raw.Append(" ");
  }
}

// Snapshot the input before vector mutation and insert before the clamped index; an out-of-range index selects the last.
// Rebuild raw text only after publishing the inserted String.
bool ScCommandLine::InsertArgument(const ScStringView &argument, i32 index) {
  u32 at = arguments.empty() ? 0 : (u32)index < arguments.size() ? (u32)index : (u32)arguments.size() - 1;
  ScString value(argument);
  GrowArguments();
  arguments.insert(arguments.begin() + at, value);
  RebuildText();
  return true;
}

// Remove the indexed String, clamping unsigned out-of-range indices to the last argument, then rebuild raw text.
// Calling this method on an empty vector is outside the original defined domain.
bool ScCommandLine::EraseArgument(i32 index) {
  assert(!arguments.empty());
  u32 at = (u32)index < arguments.size() ? (u32)index : (u32)arguments.size() - 1;
  arguments.erase(arguments.begin() + at);
  RebuildText();
  return true;
}
