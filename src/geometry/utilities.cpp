#define WIN32_LEAN_AND_MEAN
#include "geometry/utilities.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL_timer.h>

#include "application/application_core.h"
#include "city/api.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/locale.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/utf8.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/sprite.h"

#ifdef GetObject
#undef GetObject
#endif

// Override-list searches return this missing-index value when no stored interface pointer matches.
enum GeOverrideIndex : u32 {
  kGeOverrideIndexNotFound = 0xffffffff,
};

GeDate::GeDate() : references(0) {
}

GeDate::GeDate(const GeDate &source) : CtDate(static_cast<const CtDateInterface &>(source)), references(0) {
}

GeDate::GeDate(const CtDate &source) : CtDate(source), references(0) {
}

GeDate::GeDate(u32 day, u32 year) : CtDate(day, year), references(0) {
}

GeDate::GeDate(u32 month, u32 day, u32 year) : CtDate(month, day, year), references(0) {
}

GeDate::~GeDate() {
}

// Publish the adjusted date interface for Unknown and the actual date IID, then retain virtually. Unsupported queries
// clear the output.
bool GeDate::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtDateInterface);
  HANDLE_QUERY_INTERFACE(IID_CtDateInterface, CtDateInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeDate::AddRef() {
  return ++references;
}

// Decrement counts above one; zero and one both delete the complete wrapper. The final count is not cleared before
// destruction, preserving what a derived destructor can observe.
u32 GeDate::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

void GeDate::Set(u32 day, u32 year) {
  CtDate::Set(day, year);
}

void GeDate::Set(u32 month, u32 day, u32 year) {
  CtDate::Set(month, day, year);
}

void GeDate::Set(const CtTime &time) {
  CtDate::Set(time);
}

void GeDate::Set(u32 value) {
  CtDate::Set(value);
}

void GeDate::Set(const char *month, u32 day, u32 year) {
  CtDate::Set(month, day, year);
}

void GeDate::Copy(const CtDateInterface &source) {
  Set(source.DayNumber());
}

void GeDate::Clone(CtDateInterface **out) {
  *out = static_cast<CtDateInterface *>(new GeDate(*this));
  (*out)->AddRef();
}

i32 GeDate::Between(const CtDateInterface &first, const CtDateInterface &second) const {
  CtDate last(second), firstValue(first);
  return CtDate::Between(firstValue, last);
}

i32 GeDate::CompareTo(const CtDateInterface &other) const {
  return CtDate::CompareTo(CtDate(other));
}

u32 GeDate::WeekDay() const {
  return CtDate::DayOfWeek();
}

u32 GeDate::Year() const {
  return CtDate::Year();
}

u32 GeDate::DayOfYear() const {
  return CtDate::DayOfYear();
}

u32 GeDate::DayOfMonth() const {
  return CtDate::DayOfMonth();
}

u32 GeDate::FirstDayOfMonth() const {
  return CtDate::FirstDayOfMonth();
}

u32 GeDate::FirstDayOfMonth(u32 month) const {
  return CtDate::FirstDayOfMonth(month);
}

u32 GeDate::Hash() const {
  return CtDate::Hash();
}

i32 GeDate::IsValid() const {
  return CtDate::IsValid();
}

i32 GeDate::Leap() const {
  return CtDate::Leap();
}

// Choose the larger value after the source callback, then copy through the output interface before retiring the temporary.
void GeDate::MaxDate(const CtDateInterface &other, CtDateInterface *out) const {
  CtDate value(other);
  GeDate selected(CtDate::Maximum(value));
  out->Copy(selected);
}

// Choose the smaller value after the source callback, then copy through the output interface before retiring the temporary.
void GeDate::MinDate(const CtDateInterface &other, CtDateInterface *out) const {
  CtDate value(other);
  GeDate selected(CtDate::Minimum(value));
  out->Copy(selected);
}

u32 GeDate::Month() const {
  return CtDate::Month();
}

void GeDate::Previous(u32 weekday, CtDateInterface *out) const {
  GeDate selected(CtDate::OnOrBefore(weekday));
  out->Copy(selected);
}

u32 GeDate::YearLastTwoDigits() const {
  return CtDate::YearLastTwoDigits();
}

u32 GeDate::DayNumber() const {
  return CtDate::DayNumber();
}

// Read the other date virtually before testing whether this live unsigned value precedes it.
i32 GeDate::operator<(const CtDateInterface &other) const {
  return CtDate::operator<(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value precedes or equals it.
i32 GeDate::operator<=(const CtDateInterface &other) const {
  return CtDate::operator<=(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value follows it.
i32 GeDate::operator>(const CtDateInterface &other) const {
  return CtDate::operator>(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value follows or equals it.
i32 GeDate::operator>=(const CtDateInterface &other) const {
  return CtDate::operator>=(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value equals it.
i32 GeDate::operator==(const CtDateInterface &other) const {
  return CtDate::operator==(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value differs from it.
i32 GeDate::operator!=(const CtDateInterface &other) const {
  return CtDate::operator!=(CtDate(other));
}

// Read the other date virtually, then subtract its date word with unsigned wraparound.
u32 GeDate::operator-(const CtDateInterface &other) const {
  return CtDate::operator-(CtDate(other));
}

// Dispatch through this same virtual day-subtraction slot, preserving the installed forwarding behavior.
u32 GeDate::operator-(i32 days) const {
  return static_cast<const CtDateInterface *>(this)->operator-(days);
}

// Dispatch through this same virtual date-addition slot, preserving the installed forwarding behavior.
u32 GeDate::operator+(const CtDateInterface &other) const {
  return static_cast<const CtDateInterface *>(this)->operator+(other);
}

// Dispatch through this same virtual day-addition slot, preserving the installed forwarding behavior.
u32 GeDate::operator+(i32 days) const {
  return static_cast<const CtDateInterface *>(this)->operator+(days);
}

// Increment the stored day with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *GeDate::operator++() {
  CtDate::operator++();
  return this;
}

// Exclude the original postfix result, whose stack date has already been destroyed before it is returned.
// A live replacement object would invent ownership and a lifetime absent from the installed implementation.
const CtDateInterface *GeDate::operator++(int) {
  assert(false && "GeDate postfix increment returns an expired stack date");
  abort();
}

// Decrement the stored day with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *GeDate::operator--() {
  CtDate::operator--();
  return this;
}

// Exclude the original postfix result, whose stack date has already been destroyed before it is returned.
// A live replacement object would invent ownership and a lifetime absent from the installed implementation.
const CtDateInterface *GeDate::operator--(int) {
  assert(false && "GeDate postfix decrement returns an expired stack date");
  abort();
}

// Add the signed displacement with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *GeDate::operator+=(i32 days) {
  CtDate::operator+=(days);
  return this;
}

// Subtract the signed displacement with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *GeDate::operator-=(i32 days) {
  CtDate::operator-=(days);
  return this;
}

// Format through the direct value base, copy through the output string interface, then retire the temporary.
void GeDate::DateString(ScStringView *out, u32 format) const {
  ScString text;
  CtDate::DateString(&text, format);
  out->Copy(text);
}

i32 GeDate::DayWithinMonth(u32 month, u32 day, u32 year) {
  return CtCalendarValid(month, day, year);
}

u32 GeDate::DaysInYear(u32 year) {
  return CtDate::DaysInYear(year);
}

u32 GeDate::Jday(u32 month, u32 day, u32 year) {
  return CtCalendarEncode(month, day, year);
}

i32 GeDate::LeapYear(u32 year) {
  return CtCalendarLeapYear(year);
}

CtDate::CtDate(const GeDate &source) : serial(source.DayNumber()) {
}

static void GeUpperIniName(ScString *name) {
  name->MakeUpper();
}

// Remove only ASCII spaces from the requested edges. Bound the scan to exclude the source's all-space overread.
static void GeTrimIniName(ScString *name, bool leading, bool trailing) {
  if (leading) {
    u32 count = 0;
    while (count < name->Strlen() && name->begin[count] == ' ')
      ++count;
    if (count)
      name->Erase(0, count);
  }
  if (trailing) {
    u32 length = name->Strlen(), end = length;
    while (end && name->begin[end - 1] == ' ')
      --end;
    if (end != length)
      name->Erase(end, length - end);
  }
}

GeIniIndex::~GeIniIndex() {
  Clear();
}

// Delete each owned section record. Retain the simple backing allocation for a later index rebuild.
void GeIniIndex::Clear() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
  entries.Clear();
}

GeIniFile::GeIniFile() : keep_open(1), indexed(0) {
}

GeIniFile::~GeIniFile() {
  if (file.IsOpen())
    file.Close();
}

// Borrow the reader's selected filename. This does not open the file or add a reference.
const ScString *GeIniFile::GetPath() const {
  return &name;
}

// Invalidate an already loaded section index through virtual Close. Then replace the complete selected path.
void GeIniFile::SetPath(const ScString &input) {
  if (indexed)
    Close();
  name = input;
}

// Release the open handle and each section record. Retain the selected filename and keep-open preference.
bool GeIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  sections.Clear();
  indexed = 0;
  return 1;
}

// Open the concrete file with the source access/share modes. Existing locked files retain the bounded retry.
bool GeIniFile::Open(bool write) {
  i32 start = static_cast<i32>(time(nullptr));
  while (!file.Open(write ? 3 : 1, write ? 3 : 2, write ? 0 : 1)) {
    if (RsFile::FileExists(name)) {
      i32 now = static_cast<i32>(time(nullptr));
      SDL_Delay(250);
      if (now - start < 3)
        continue;
    }
    return 0;
  }
  return 1;
}

// Clear output before reading, then validate native UTF-8 only after assembling all chunks of the logical line.
bool GeIniFile::GetFileLine(ScString *out) {
  ScString line;
  u32 position = file.Position(), remaining = file.Size() - position;
  out->FromChar("");
  bool read = false;
  char block[44] = {};
  while (remaining) {
    u32 count = remaining > 40 ? 40 : remaining;
    if (!file.Read(block, count))
      break;
    read = 1;
    remaining -= count;
    u32 end = 0;
    while (end < count && block[end] != '\r' && block[end] != '\n')
      ++end;
    bool newline = end != count;
    block[end] = 0;
    if (newline) {
      file.SeekAbsolute(static_cast<i32>(position + end));
      char byte;
      while (file.Read(&byte, 1))
        if (byte != '\r' && byte != '\n') {
          file.SeekRelative(-1);
          break;
        }
    }
    line.Append(block, static_cast<u32>(strlen(block)));
    if (newline || !remaining)
      break;
    position = file.Position();
  }
  if (!ScUtf8Validate(line.Data(), line.Strlen()))
    return false;
  *out = line;
  return read;
}

// Index physical line-start positions by uppercase section name. The first duplicate retains its original position.
bool GeIniFile::LoadSectionNames() {
  sections.Clear();
  if (!file.IsOpen()) {
    file.SetName(name);
    if (!Open(0))
      return 0;
  }
  file.SeekBegin();
  u32 position = file.Position();
  ScString line;
  while (GetFileLine(&line)) {
    GeTrimIniName(&line, true, false);
    if (line.Strlen() && line.begin[0] == '[') {
      u32 end = 1;
      while (end < line.Strlen() && line.begin[end] != ']')
        ++end;
      if (end >= 2 && end < line.Strlen()) {
        ScString key(line.begin + 1, end - 1);
        GeUpperIniName(&key);
        u32 i = 0;
        while (i < sections.entries.size && !sections.entries.data[i]->name.IsEqual(&key))
          ++i;
        if (i == sections.entries.size) {
          GeIniSection *section = new GeIniSection;
          section->name = key;
          section->position = position;
          *sections.entries.Append() = section;
        }
      }
    }
    position = file.Position();
  }
  indexed = 1;
  return 1;
}

// Seek the first matching section and return its first case-insensitive key, trimming only ASCII spaces.
// Close before retiring the live parser Strings; failed lookups leave the caller's output unchanged.
bool GeIniFile::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString section_name(section), key_name(key);
  GeUpperIniName(&section_name);
  GeUpperIniName(&key_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size ||
      file.SeekAbsolute(static_cast<i32>(sections.entries.data[index]->position)) >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  bool found = false;
  ScString parsed, line;
  if (GetFileLine(&line))
    while (GetFileLine(&line)) {
      GeTrimIniName(&line, true, false);
      if (!line.Strlen() || line.begin[0] == ';')
        continue;
      if (line.begin[0] == '[')
        break;
      u32 equals = 0;
      while (equals < line.Strlen() && line.begin[equals] != '=')
        ++equals;
      parsed.FromChar("", 0);
      if (equals < line.Strlen()) {
        parsed.FromChar(line.begin, equals);
        line.Erase(0, equals + 1);
      }
      GeTrimIniName(&parsed, false, true);
      GeUpperIniName(&parsed);
      if (parsed.IsEqual(&key_name)) {
        GeTrimIniName(&line, true, true);
        *out = line;
        found = 1;
        break;
      }
    }
  if (!keep_open)
    Close();
  return found;
}

// Own the original enumeration's independent name snapshot until all callbacks and optional Close complete.
struct GeIniNames {
  ScArray<ScString *> values;
  ~GeIniNames();
};

GeIniNames::~GeIniNames() {
  for (u32 i = 0; i < values.size; ++i)
    delete values.data[i];
}

// Compare complete names with the original string tree's bytewise lexical order, including prefix lengths.
// Equal prefixes place the shorter name before its longer continuation.
static i32 GeCompareIniNames(const ScString &first, const ScString &second) {
  u32 firstLength = first.Strlen(), secondLength = second.Strlen();
  u32 count = firstLength < secondLength ? firstLength : secondLength;
  i32 compared = memcmp(first.begin, second.begin, count);
  if (compared)
    return compared;
  return firstLength < secondLength ? -1 : firstLength != secondLength;
}

// Snapshot indexed names in tree order before dispatching callbacks with a shared empty value String.
// Close after callbacks when requested, keeping their independent names alive through that virtual call and unwinding.
bool GeIniFile::EnumSections(GeIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString empty;
  GeIniNames names;
  while (names.values.size < sections.entries.size) {
    GeIniSection *selected = nullptr;
    for (u32 i = 0; i < sections.entries.size; ++i) {
      GeIniSection *candidate = sections.entries.data[i];
      if (names.values.size && GeCompareIniNames(candidate->name, *names.values.data[names.values.size - 1]) <= 0)
        continue;
      if (!selected || GeCompareIniNames(candidate->name, selected->name) < 0)
        selected = candidate;
    }
    ScString *copy = new ScString(selected->name);
    *names.values.Append() = copy;
  }
  for (u32 i = 0; i < names.values.size; ++i)
    callback(*names.values.data[i], empty, context);
  if (!keep_open)
    Close();
  return true;
}

// Uppercase a copied interface String after ensuring the section index is loaded.
// Preserve the original Close-before-search ordering, which clears the index when keep-open is disabled.
bool GeIniFile::SectionExists(const ScStringView &section) {
  if (!indexed && !LoadSectionNames())
    return false;
  ScString sectionName(section);
  GeUpperIniName(&sectionName);
  if (!keep_open)
    Close();
  for (u32 i = 0; i < sections.entries.size; ++i)
    if (sections.entries.data[i]->name.IsEqual(&sectionName))
      return true;
  return false;
}

// Update the keep-open preference only when it changes, closing through the virtual method when disabling it.
// Return the original unconditional true result without opening or rebuilding an index when enabling it.
bool GeIniFile::SetLeaveFileOpenBetweenOperations(bool value) {
  if (keep_open != u8(value)) {
    keep_open = value;
    if (!value)
      Close();
  }
  return true;
}

bool GeIni::SectionExists(const ScStringView &) {
  return false;
}

// Select the source decimal/hexadecimal radix from the complete C string, then preserve 32-bit CRT strtoul behavior.
// The host-wide conversion keeps negative values and overflow consistent when unsigned long is wider than a DWORD.
u32 GeParseNumber(const char *value) {
  int base = 10;
  if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    base = 16;
  else
    for (const char *p = value; *p; ++p)
      if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        base = 16;
        break;
      }
  int saved = errno;
  errno = 0;
  u64 converted = strtoull(value, nullptr, base);
  const char *sign = value;
  while (isspace(static_cast<u8>(*sign)))
    ++sign;
  u64 magnitude = *sign == '-' ? 0 - converted : converted;
  if (errno == ERANGE || magnitude > UINT32_MAX) {
    errno = ERANGE;
    return UINT32_MAX;
  }
  if (!errno)
    errno = saved;
  return static_cast<u32>(converted);
}

static void GeIniUppercase(ScString *value) {
  GeUpperIniName(value);
}

// Read a text-bearing packaged field without changing the raw reader's record lengths, offsets or reuse behavior.
// Installed SYS.PAK fields are ASCII; native additions use UTF-8, and malformed text fails without a guessed code page.
static bool GeReadIniText(GeIni *reader, ScString *text) {
  reader->ReadString(text);
  return ScUtf8Validate(text->Data(), text->Strlen());
}

// Remove only ASCII spaces at the left edge. Tabs and other whitespace remain data in this parser.
static void GeIniTrimLeft(ScString *value) {
  u32 count = 0, length = static_cast<u32>(value->end - value->begin);
  while (count < length && value->begin[count] == 32)
    ++count;
  if (count)
    value->ScString::Erase(0, count);
}

// Drop trailing ASCII spaces from the bounded parser value. Tabs remain data, and the new end is zero terminated.
static void GeIniTrimRight(ScString *value) {
  while (value->end != value->begin && value->end[-1] == 32)
    --value->end;
  *value->end = 0;
}

// Take the first equals-delimited prefix and leave its suffix in the original line.
static ScString GeIniTakeKey(ScString *line) {
  u32 length = static_cast<u32>(line->end - line->begin), index = 0;
  while (index < length && line->begin[index] != '=')
    ++index;
  if (index == length)
    return ScString();
  ScString key(line->begin, index);
  line->ScString::Erase(0, index + 1);
  return key;
}

// Prefer the lazily selected package entry, matching its section header exactly and its trimmed uppercase key.
// Package values retain whitespace; loose fallback uses the inherited rules and misses leave the caller's output intact.
bool GeIni::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  InitFileType();
  if (mode == 1)
    return GeIniFile::ReadEntry(section, key, out);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.ScString::Append("]", 1);
  u32 count = RsReadPackageWord(&package_file);
  ScString line;
  bool selected = false;
  ScString key_name(key.begin, static_cast<u32>(key.end - key.begin));
  GeIniUppercase(&key_name);
  GeIniTrimLeft(&key_name);
  GeIniTrimRight(&key_name);
  for (u32 index = 0; index < count; ++index) {
    if (!GeReadIniText(this, &line))
      return false;
    if (selected) {
      if (line.begin != line.end && line.begin[0] == '[')
        break;
      ScString candidate = GeIniTakeKey(&line);
      GeIniUppercase(&candidate);
      GeIniTrimLeft(&candidate);
      GeIniTrimRight(&candidate);
      if (candidate.Strlen() == key_name.Strlen() && !memcmp(candidate.begin, key_name.begin, candidate.Strlen())) {
        out->ScString::FromChar(line.begin, static_cast<u32>(line.end - line.begin));
        return true;
      }
    } else if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
      selected = true;
  }
  return false;
}

// Keep every loose-file row owned until all callbacks complete, preserving mutable String identities.
struct GeIniPair {
  ScString key, value;
};

// Buffer valid loose-file pairs before dispatch, retaining case and closing only after the callbacks complete.
// Empty pairs are skipped in loose files; the packaged reader deliberately stops instead.
bool GeIniFile::EnumEntries(const ScString &section, GeIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString section_name(section);
  GeIniUppercase(&section_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size ||
      file.SeekAbsolute(static_cast<i32>(sections.entries.data[index]->position)) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString key, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  ScArray<GeIniPair *> rows;
  while (GetFileLine(&line)) {
    GeIniTrimLeft(&line);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    key = GeIniTakeKey(&line);
    GeIniTrimRight(&key);
    GeIniTrimLeft(&line);
    GeIniTrimRight(&line);
    if (key.begin != key.end && line.begin != line.end)
      *rows.Append() = new GeIniPair{key, line};
  }
  u32 count = rows.size;
  for (u32 row = 0; row < count; ++row)
    callback(rows.data[row]->key, rows.data[row]->value, context);
  if (!keep_open)
    Close();
  for (u32 row = 0; row < count; ++row)
    delete rows.data[row];
  return true;
}

// Enumerate through the original Boolean and generic-library callback contracts.
// Packaged rows call immediately and stop at the first empty pair; inherited loose enumeration buffers before dispatch.
bool GeIni::EnumEntries(const ScString &section, GeIniCallback callback, void *context) {
  InitFileType();
  if (mode == 1)
    return GeIniFile::EnumEntries(section, callback, context);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.ScString::Append("]", 1);
  u32 count = RsReadPackageWord(&package_file);
  ScString line;
  bool selected = false;
  for (u32 index = 0; index < count; ++index) {
    if (!GeReadIniText(this, &line))
      return false;
    if (!selected) {
      if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
        selected = true;
      continue;
    }
    if (line.begin != line.end && line.begin[0] == '[')
      break;
    ScString key = GeIniTakeKey(&line);
    GeIniTrimLeft(&key);
    GeIniTrimRight(&key);
    GeIniTrimLeft(&line);
    GeIniTrimRight(&line);
    if (key.begin == key.end || line.begin == line.end)
      break;
    callback(key, line, context);
  }
  return true;
}

// Form the source split-path basename plus extension, independently truncating each component to 255 bytes.
// Ignore the drive and directory prefix; native filename casing and component limits retain complete UTF-8 scalars.
static ScString GeIniPackageKey(const ScString &name) {
  const char *begin = name.begin, *base = begin, *dot = nullptr, *end = begin + strlen(begin);
  if (begin != end && begin[1] == ':')
    base += 2;
  for (const char *byte = base; byte != end; ++byte)
    if (*byte == '/' || *byte == '\\')
      base = byte + 1;
    else if (*byte == '.')
      dot = byte;
  if (!dot || dot < base)
    dot = end;
  usize stem = static_cast<usize>(dot - base), extension = static_cast<usize>(end - dot);
  stem = ScUtf8Prefix(base, static_cast<u32>(stem), stem > 255 ? 255 : static_cast<u32>(stem));
  extension = ScUtf8Prefix(dot, static_cast<u32>(extension), extension > 255 ? 255 : static_cast<u32>(extension));
  ScString result(base, static_cast<u32>(stem));
  result.ScString::Append(dot, static_cast<u32>(extension));
  GeIniUppercase(&result);
  return result;
}

GeIni::GeIni() : mode(0), buffer(nullptr), buffer_capacity(0) {
}

GeIni::~GeIni() {
  delete[] buffer;
}

void GeIni::SetCompressedPath(const ScString &input) {
  mode = 0;
  package_name = input;
}

// Read a length-prefixed string, initializing only its final terminator as in the original allocation.
// Capacity stays unchanged; zero-length records reuse the prior string, and a short NUL-terminated prefix remains valid.
void GeIni::ReadString(ScString *out) {
  u32 length = RsReadPackageWord(&package_file);
  if (length > buffer_capacity) {
    if (length == UINT32_MAX) {
      assert(false && "The original packaged INI allocation would wrap");
      abort();
    }
    delete[] buffer;
    buffer = new char[static_cast<usize>(length) + 1];
    buffer[length] = 0;
  }
  u32 written = length;
  if (!package_file.opened)
    written = 0;
  else
    package_file.Read(buffer, &written);
  if (!buffer || (written < length && !memchr(buffer, 0, written))) {
    assert(false && "The original packaged INI string would consume undefined storage");
    abort();
  }
  ScString value(buffer, static_cast<u32>(strlen(buffer)));
  out->ScString::FromChar(value.begin, static_cast<u32>(value.end - value.begin));
}

// Initialize the package index lazily using the original void operation.
// Cache loose mode after an absent package/index entry, or retain the matched section offset and package mode.
void GeIni::InitFileType() {
  if (mode)
    return;
  if (!package_name.Strlen() || !RsFile::FileExists(package_name)) {
    mode = 1;
    return;
  }
  package_file.SetName(package_name);
  if (!package_file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    mode = 1;
    return;
  }
  package_offset = 0;
  ScString expected = GeIniPackageKey(name);
  package_file.SeekBegin();
  u32 count = RsReadPackageWord(&package_file), offset = count;
  ScString entry_name;
  for (u32 index = 0; index < count; ++index) {
    if (!GeReadIniText(this, &entry_name)) {
      mode = 1;
      return;
    }
    GeIniUppercase(&entry_name);
    package_file.Read(&offset, 4);
    if (entry_name.Strlen() == expected.Strlen() && !memcmp(entry_name.begin, expected.begin, entry_name.Strlen())) {
      package_offset = offset;
      mode = 2;
      return;
    }
  }
  mode = 1;
}

// Preserve comments and following sections while inserting or replacing the first matching key.
// File writes and resize replies are deliberately ignored; shifted contents invalidate the cached section offsets.
bool GeIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  ScString heading, entry;
  file.SetName(name);
  if (!RsFile::FileExists(name)) {
    indexed = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(true))
      return false;
    heading.Sprintf("[%s]\r\n", section.begin);
    file.Write(heading.begin, u32(heading.end - heading.begin));
    entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
    file.Write(entry.begin, u32(entry.end - entry.begin));
    file.Close();
    return true;
  }
  if (!indexed && !LoadSectionNames())
    return false;
  if (file.IsOpen())
    file.Close();
  if (!Open(true))
    return false;
  ScString sectionName(section), keyName(key);
  GeUpperIniName(&sectionName);
  GeUpperIniName(&keyName);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&sectionName))
    ++index;
  if (index == sections.entries.size) {
    file.SeekEnd();
    u32 position = file.Position();
    heading.Sprintf("\r\n[%s]\r\n", section.begin);
    file.Write(heading.begin, u32(heading.end - heading.begin));
    entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
    file.Write(entry.begin, u32(entry.end - entry.begin));
    file.Flush();
    GeIniSection *added = new GeIniSection;
    added->name = sectionName;
    added->position = position + 2;
    *sections.entries.Append() = added;
    if (!keep_open)
      Close();
    return true;
  }
  if (file.SeekAbsolute(sections.entries.data[index]->position) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString line, parsed;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  for (;;) {
    u32 position = file.Position();
    if (!GetFileLine(&line))
      break;
    GeTrimIniName(&line, true, false);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    bool boundary = line.begin[0] == '[';
    if (!boundary) {
      parsed = GeIniTakeKey(&line);
      GeTrimIniName(&parsed, false, true);
      GeUpperIniName(&parsed);
      if (!parsed.IsEqual(&keyName))
        continue;
    }
    u32 remaining = file.Size() - (boundary ? position : file.Position());
    u8 *tail = new u8[remaining];
    if (boundary)
      file.SeekAbsolute(position);
    if (file.Read(tail, remaining)) {
      file.SeekAbsolute(position);
      entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
      file.Write(entry.begin, u32(entry.end - entry.begin));
      file.Write(tail, remaining);
      file.Resize(file.Position());
    }
    delete[] tail;
    indexed = 0;
    if (!keep_open)
      Close();
    return true;
  }
  file.SeekEnd();
  entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
  file.Write(entry.begin, u32(entry.end - entry.begin));
  file.Flush();
  if (!keep_open)
    Close();
  return true;
}

// Reject empty selectors before invalidating the index. Forward the formatted signed value through virtual WriteEntry.
bool GeIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  indexed = 0;
  ScString number;
  number.Sprintf("%ld", static_cast<long>(value));
  return WriteEntry(section, key, number);
}

// Encode each byte as two uppercase hexadecimal digits. The caller owns termination and the original Boolean result.
static void GeEncodeIniHex(const u8 *data, u32 length, char *out) {
  static const char digits[] = "0123456789ABCDEF";
  for (u32 i = 0; i < length; ++i) {
    out[2 * i] = digits[data[i] >> 4];
    out[2 * i + 1] = digits[data[i] & 15];
  }
}

// Decode every pair even after an invalid digit. Odd live lengths consume the defined String terminator as a bad digit.
static bool GeDecodeIniHex(const ScString &value, u32 length, u8 *out) {
  bool valid = true;
  for (u32 i = 0; i < length; i += 2) {
    u8 byte = 0;
    for (u32 part = 0; part < 2; ++part) {
      u8 digit = u8(value.begin[i + part]), nibble = 0;
      if (digit >= '0' && digit <= '9')
        nibble = digit - '0';
      else if (digit >= 'A' && digit <= 'F')
        nibble = digit - 'A' + 10;
      else if (digit >= 'a' && digit <= 'f')
        nibble = digit - 'a' + 10;
      else
        valid = false;
      byte = u8((byte << 4) + nibble);
    }
    out[i / 2] = byte;
  }
  return valid;
}

// Encode nonempty binary input before dispatching virtual WriteEntry.
// Assert the original DWORD allocation bound rather than extending overflowing inputs with native-width arithmetic.
bool GeIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length) {
  if (!length)
    return false;
  if (length > (UINT32_MAX - 1u) / 2u) {
    assert(false && "The original encoded INI allocation would wrap");
    abort();
  }
  char *encoded = new char[usize(length) * 2 + 1];
  GeEncodeIniHex(data, length, encoded);
  encoded[2 * length] = 0;
  bool result;
  {
    ScString value(encoded);
    result = WriteEntry(section, key, value);
  }
  delete[] encoded;
  return result;
}

// Clear the genuine signed output before virtual ReadEntry. Only one successful decimal scanf conversion succeeds.
bool GeIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString value;
  *out = 0;
  if (!ReadEntry(section, key, &value))
    return false;
#if defined(_MSC_VER)
  return sscanf_s(value.begin, "%d", out) == 1;
#else
  return sscanf(value.begin, "%d", out) == 1;
#endif
}

// Publish owned byte storage before decoding, clearing it only after a malformed hexadecimal value.
u32 GeIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString value;
  if (!ReadEntry(section, key, &value))
    return u32(value.end - value.begin) >> 1;
  u32 length = u32(value.end - value.begin);
  if (length & 1) {
    assert(false && "The original hexadecimal INI output would exceed its allocation");
    abort();
  }
  *out = new u8[length >> 1];
  if (GeDecodeIniHex(value, length, *out))
    return length >> 1;
  delete[] *out;
  *out = nullptr;
  return 0;
}

// Decode at most twice the supplied byte capacity. Invalid digits still write their zero nibble before failure.
u32 GeIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity) {
  ScString value;
  if (!ReadEntry(section, key, &value))
    return 0;
  u32 length = 2u * capacity, available = u32(value.end - value.begin);
  if (length > available)
    length = available;
  return GeDecodeIniHex(value, length, out) ? length >> 1 : 0;
}

bool GeIni::WriteEntry(const ScString &, const ScString &, const ScString &) {
  return false;
}

bool GeIni::WriteNumber(const ScString &, const ScString &, i32) {
  return false;
}

bool GeIni::WriteBinaryEntry(const ScString &, const ScString &, const u8 *, u32) {
  return false;
}

bool GeIni::ReadNumber(const ScString &, const ScString &, i32 *) {
  return false;
}

u32 GeIni::ReadBinaryEntry(const ScString &, const ScString &, u8 **) {
  return 0;
}

u32 GeIni::ReadBinaryEntry(const ScString &, const ScString &, u8 *, u32) {
  return 0;
}

// Borrow the configured compressed path without selecting a mode or opening the package.
const ScString *GeIni::GetCompressedPath() const {
  return &package_name;
}

bool GeIni::EnumEntriesOld(const ScString &, GeIniCallback, void *) {
  return false;
}

GeOverride::GeOverride() : lotSize(2), spriteKey{}, sprite(nullptr), guid(0) {
}

// The four strings and resource base then retire in order.
GeOverride::~GeOverride() {
  if (sprite) {
    static_cast<ScUnknown *>(sprite)->Release();
    sprite = nullptr;
  }
}

// Publish the adjusted override or serialized identity before retaining the shared resource count.
// Unknown and resource queries delegate to the genuine resource base without clearing refused outputs.
bool GeOverride::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_OcOverride, OcOverride);
  HANDLE_QUERY_INTERFACE(IID_RsSerializable, RsSerializable);
  return RsResource::QueryInterface(iid, out);
}

u32 GeOverride::AddRef() {
  return RsResource::AddRef();
}

u32 GeOverride::Release() {
  return RsResource::Release();
}

// Borrow the resource's own mutable key, independently of the selected sprite key and cache.
RsKey *GeOverride::Key() const {
  return const_cast<RsKey *>(&key);
}

void GeOverride::SetName(const ScStringView &value) {
  ScString copy(value);
  name = copy;
}

void GeOverride::SetArchitectName(const ScStringView &value) {
  ScString copy(value);
  architectName = copy;
}

void GeOverride::SetDescription(const ScStringView &value) {
  ScString copy(value);
  description = copy;
}

void GeOverride::SetLotSize(u8 value) {
  lotSize = value;
}

void GeOverride::SetSpriteAttribKey(const RsKey &value) {
  spriteKey = value;
}

bool GeOverride::GetName(ScStringView *out) {
  out->Copy(name);
  return true;
}

bool GeOverride::GetArchitectName(ScStringView *out) {
  out->Copy(architectName);
  return true;
}

u8 GeOverride::GetLotSize() {
  return lotSize;
}

// Borrow the cached sprite or publish the manager lookup directly into that slot using the current sprite key.
// False lookups clear even a published pointer; a true null result remains eligible for another lookup.
SpDefinition *GeOverride::GetSpriteAttrib() {
  if (!sprite && !GeGetResourceManager()->GetResource(spriteKey, IID_SpDefView, &sprite))
    sprite = nullptr;
  return static_cast<SpDefinition *>(sprite);
}

// Only layer bit zero selects this override's sprite. Eligible requests dispatch the unindexed virtual getter.
SpDefinition *GeOverride::GetSpriteAttrib(u32 layer) {
  return (layer & 1) ? GetSpriteAttrib() : nullptr;
}

// Read the input's length before its data, then copy that exact byte range into the stored serial number.
void GeOverride::SetAuthorSerialNumber(ScStringView *value) {
  u32 length = value->Strlen();
  const char *data = value->Data();
  authorSerial.FromChar(data, length);
}

// Reject only the original null output case. Otherwise clear it before allocating a separate retained string copy.
bool GeOverride::GetAuthorSerialNumber(ScStringView **out) {
  if (!out)
    return false;
  *out = nullptr;
  ScString *copy = new ScString(authorSerial);
  copy->AddRef();
  *out = copy;
  return true;
}

void GeOverride::SetGUID(u32 value) {
  guid = value;
}

bool GeOverride::GetGUID(u32 *out) {
  *out = guid;
  return true;
}

static u32 GeOverrideTextPage() {
  ScLanguageManager *manager = GeGetLanguageManager();
  return manager ? ScInstalledWesternTextPage(manager->GetCurrentLanguage()) : 0;
}

// Restore scalar fields and three displayed strings in order under the evidenced active Western font mapping.
// Preserve partial text publication, raw author serial/GUID data and the existing cached sprite.
u8 GeOverride::Read(RsStream *stream) {
  u32 page = GeOverrideTextPage();
  if (!page || !stream->ReadU8(&lotSize) || !stream->ReadKey(&spriteKey) ||
      !RsReadLegacyText(stream, &name, page) || !RsReadLegacyText(stream, &architectName, page) ||
      !RsReadLegacyText(stream, &description, page) || !stream->ReadString(&authorSerial))
    return 0;
  return stream->ReadU32(&guid);
}

// Encode displayed strings for the evidenced active Western glyph mapping, retaining original stream order.
// Author serial bytes and IDs stay raw; unsupported contexts or unrepresentable text report a recoverable failure.
u8 GeOverride::Write(RsStream *stream) {
  u32 page = GeOverrideTextPage();
  if (!page || !stream->WriteU8(lotSize) || !stream->WriteKey(&spriteKey) ||
      !RsWriteLegacyText(stream, &name, page) || !RsWriteLegacyText(stream, &architectName, page) ||
      !RsWriteLegacyText(stream, &description, page) || !stream->WriteString(&authorSerial))
    return 0;
  return stream->WriteU32(guid);
}

// Construct the actual override resource and return its occupant-override subobject without acquiring a reference.
ScUnknown *GeCreateOverride() {
  return static_cast<OcOverride *>(new GeOverride);
}

GeOverrideList::GeOverrideList() : references(0) {
}

GeOverrideList::~GeOverrideList() {
  GeOverrideList::Clear();
}

// Publish this same primary identity for Unknown or the original override-container IID, then acquire its reference.
// Unsupported queries clear the output on rejection.
bool GeOverrideList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcOverrideContainer);
  HANDLE_QUERY_INTERFACE(IID_OcOverrideContainer, OcOverrideContainer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeOverrideList::AddRef() {
  return ++references;
}

u32 GeOverrideList::Release() {
  return ScReleaseImpl(this, &references);
}

// Release the captured entry range in order, then erase the live vector after all callbacks.
// Reentrant appends beyond that range are erased without another Release, matching the original ownership behavior.
bool GeOverrideList::Clear() {
  u32 count = objects.size;
  for (u32 i = 0; i < count; ++i)
    objects.data[i]->Release();
  objects.Clear();
  return true;
}

// Return a borrowed indexed object only for a nonempty in-range request with an output pointer.
// The original failed cases leave that output unchanged.
bool GeOverrideList::GetObject(u32 index, OcOverride **out) const {
  if (!objects.size || index >= objects.size || !out)
    return false;
  *out = objects.data[index];
  return true;
}

// Search the captured range by the object's real key, comparing instance/group/type in that order.
bool GeOverrideList::GetObject(const RsKey &key, OcOverride **out) {
  u32 count = objects.size;
  for (u32 i = 0; i < count; ++i) {
    RsKey *candidate = objects.data[i]->Key();
    if (candidate->instance == key.instance && candidate->group == key.group && candidate->type == key.type) {
      *out = objects.data[i];
      return true;
    }
  }
  *out = nullptr;
  return false;
}

// Reject the original null-object case, then append before acquiring its reference so callbacks observe the new entry.
bool GeOverrideList::PutObject(OcOverride *value) {
  if (!value)
    return false;
  *objects.Append() = value;
  value->AddRef();
  return true;
}

u32 GeOverrideList::Size() {
  return objects.size;
}

// Find the first identical interface pointer in the captured range. The SDK's missing-index result is all one bits.
u32 GeOverrideList::GetIndexForObject(const OcOverride *value) {
  u32 count = objects.size;
  for (u32 i = 0; i < count; ++i)
    if (objects.data[i] == value)
      return i;
  return kGeOverrideIndexNotFound;
}

// Construct the actual override container and return its primary interface without acquiring a reference.
ScUnknown *GeCreateOverrideList() {
  return static_cast<OcOverrideContainer *>(new GeOverrideList);
}

// Initialize both independent seeds and invalidate the normal cache. The all-ones seed uses the original multimedia
// timer.
GeRandom::GeRandom(u32 seed) {
  if (seed == UINT32_MAX)
    seed = ScMultimediaMilliseconds();
  state = seed;
  realState = seed;
  hasNormal = 0;
}

// Advance the integer seed and return the original mixed high bits. Real draws and cached normal data are untouched.
u32 GeRandom::Draw() {
  u64 product = u64(state) * kScRandomMultiplier;
  u32 low = u32(product), high = u32(product >> 32) << 16;
  state = low + kScRandomIncrement;
  high += 65535u + (state < low);
  return (high & kScRandomRetainedHighWordMask) | (state >> 16);
}

// Preserve the small-bound modulo path and inclusive large-bound rejection. A zero bound does not advance any state.
u32 GeRandom::Next(u32 bound) {
  if (!bound)
    return 0;
  u32 next = state, value;
  do {
    u64 product = u64(next) * kScRandomMultiplier;
    u32 low = u32(product), high = u32(product >> 32) << 16;
    next = low + kScRandomIncrement;
    high += 65535u + (next < low);
    value = (high & kScRandomRetainedHighWordMask) | (next >> 16);
    if (bound < kScRandomModuloThreshold) {
      value %= bound;
      break;
    }
    // The original BSR/SHLD mask covers every set bit of value. Its AND result is value even for zero.
  } while (value > bound);
  state = next;
  return value;
}

GeTextResource::GeTextResource(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  LoadResource();
}

// Release the retained String while it remains published, then clear it. A failed lookup holds no reference to release.
GeTextResource::~GeTextResource() {
  if (text) {
    static_cast<ScStringView *>(text)->Release();
    text = nullptr;
  }
}

void GeTextResource::LoadResource() {
  if (!GeGetResourceManager()->GetResource(key, IID_ScStringView, &text))
    text = nullptr;
}

bool GeTextResource::IsStringPresent() {
  return text != nullptr;
}

// Copy the live String interface into an independently owned value, reading length before data. Missing acquisition
// yields the original empty value; hidden aggregate-return storage is represented by the ordinary C++ value return.
GeTextResource::operator ScString() {
  if (text)
    return ScString(*static_cast<ScStringView *>(text));
  return ScString();
}

ScString GeTextResource::Val() {
  if (!text)
    return ScString();
  ScString value(*static_cast<ScStringView *>(text));
  ReplaceParameters(&value);
  return value;
}

// Expand the retained localized value and copy its bytes through the supplied String interface.
// A missing resource leaves the destination untouched; The authenticated QAE_N decoration proves the Boolean reply.
bool GeTextResource::Detokenize(ScStringView *out) {
  if (!text)
    return 0;
  ScString value(*static_cast<ScStringView *>(text));
  ReplaceParameters(&value);
  out->FromChar(value.Data(), value.Strlen());
  return 1;
}

bool GeTextResource::Detokenize(ScStringView *out, GeTextToken **tokens, u32 count) {
  if (!text)
    return 0;
  ScString value(*static_cast<ScStringView *>(text));
  ReplaceParameters(&value, tokens, count);
  out->FromChar(value.Data(), value.Strlen());
  return 1;
}

GeTextResource::GeTextResource() : text(nullptr), key{} {
}

GeTextResource::GeTextResource(const GeTextResource &source) : text(nullptr), key(source.key) {
  LoadResource();
}

// Release the old publication before copying the normalized String key and loading again. Self-assignment has no effects.
// The original borrowed reference result uses pointer spelling; Windows SC3U484BA5 and its decoration prove the contract.
GeTextResource *GeTextResource::operator=(const GeTextResource &source) {
  if (this != &source) {
    UnloadResource();
    source.GetResKey(&key);
    LoadResource();
  }
  return this;
}

// Compare through the retained String interface only when present. A missing publication compares false.
// Windows SC3U484BCD and its original operator decoration prove bool; no standalone SIMGEOM entry is mapped.
bool GeTextResource::operator==(const ScStringView &other) {
  return text && static_cast<ScStringView *>(text)->IsEqual(&other);
}

// Borrow the retained const String interface without querying or retaining it.
// Windows SC3U484BEB and Value QBEPBVcIGZString prove the body; no standalone SIMGEOM entry is mapped.
const ScStringView *GeTextResource::Value() const {
  return static_cast<ScStringView *>(text);
}

// Borrow the unnormalized stored key. The original const-reference result uses an explicit pointer in this source.
// Windows SC3U484C76 and GetResKey QBEABVcGZResourceKey prove the body; no standalone SIMGEOM entry is mapped.
const RsKey *GeTextResource::GetResKey() const {
  return &key;
}

// Publish the String resource type before copying the current group and instance through the genuine output.
void GeTextResource::GetResKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
}

// Release while the old publication remains visible, then clear it. This neither resets the key nor queries services.
void GeTextResource::UnloadResource() {
  if (text) {
    static_cast<ScStringView *>(text)->Release();
    text = nullptr;
  }
}

// Locate the first C-string pattern within the complete live String range.
static u32 GeFindResourcePattern(const ScString &value, const char *pattern, u32 length) {
  u32 available = u32(value.end - value.begin);
  if (length > available)
    return UINT32_MAX;
  for (u32 i = 0; i <= available - length; ++i)
    if (!memcmp(value.begin + i, pattern, length))
      return i;
  return UINT32_MAX;
}

// Replace the first match directly in the retained concrete String payload. The original requires that payload to
// exist.
bool GeTextResource::ReplaceParameter(ScString *pattern, ScString *replacement) {
  ScString *stored = static_cast<ScString *>(static_cast<ScStringView *>(text));
  const char *needle = pattern->ToChar();
  u32 found = GeFindResourcePattern(*stored, needle, u32(strlen(needle)));
  if (found >= u32(stored->end - stored->begin))
    return false;
  u32 length = replacement->Strlen();
  const char *data = replacement->Data();
  u32 remove = pattern->Strlen();
  stored->Replace(found, remove, data, length);
  return true;
}

// Copy the retained String into the output before searching or reading replacement getters. Replace only its first
// match. Missing source publication remains outside its defined domain.
bool GeTextResource::ReplaceParameter(ScString *pattern, ScString *replacement, ScString *out) {
  ScStringView *stored = static_cast<ScStringView *>(text);
  u32 size = stored->Strlen();
  const char *source = stored->Data();
  *out = ScString(source, size);
  const char *needle = pattern->ToChar();
  u32 found = GeFindResourcePattern(*out, needle, u32(strlen(needle)));
  if (found >= u32(out->end - out->begin))
    return false;
  u32 length = replacement->Strlen();
  const char *data = replacement->Data();
  u32 remove = pattern->Strlen();
  out->Replace(found, remove, data, length);
  return true;
}

// Copy retained text before invoking the external resolver scanner and publishing through the genuine String output. No
// standalone SIMGEOM entry is mapped.
bool GeTextResource::Detokenize(ScStringView *out, GeTextResolver resolver, void *context) {
  if (!text)
    return false;
  ScStringView *stored = static_cast<ScStringView *>(text);
  u32 length = stored->Strlen();
  const char *data = stored->Data();
  ScString value(data, length);
  ReplaceParameters(&value, resolver, context);
  out->FromChar(value.begin, u32(value.end - value.begin));
  return true;
}

static bool GeTokenEquals(const ScString &token, const char *name, u32 length) {
  return (u32)(token.end - token.begin) == length && !memcmp(token.begin, name, length);
}

// The original checks the range before entering standard-string assignment; an oversized start does not throw.
static ScString GeTokenSlice(const ScString &value, u32 start, u32 count) {
  u32 length = u32(value.end - value.begin);
  if (start >= length)
    return ScString();
  if (count > length - start)
    count = length - start;
  return ScString(value.begin + start, count);
}

// Linux symbols establish the owning text handle, concrete String inputs and signed mutable cursor; PC defines
// behavior.
bool GeTextResource::ReplaceParameter(ScString *value, const ScString &token, i32 *position) {
  u32 tokenLength = token.Strlen();
  ScString candidate = GeTokenSlice(*value, u32(*position), tokenLength);
  if ((u32)(candidate.end - candidate.begin) != (u32)(token.end - token.begin) ||
      memcmp(candidate.begin, token.begin, (usize)(candidate.end - candidate.begin)))
    return 0;
  if (GeTokenEquals(token, "%MAYOR%", 7) || GeTokenEquals(token, "%YOURNAME%", 10)) {
    CtCity *city = GeGetCity();
    if (city) {
      ScStringView *sizeName = city->GetMayorName(), *dataName = city->GetMayorName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, data, size);
      *position = i32(u32(*position) + city->GetMayorName()->Strlen() - 1);
    }
    return 1;
  }
  if (GeTokenEquals(token, "%CITYNAME%", 10) || GeTokenEquals(token, "%YOURCITY%", 10)) {
    CtCity *city = GeGetCity();
    if (city) {
      ScStringView *sizeName = city->GetCityName(), *dataName = city->GetCityName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, data, size);
      *position = i32(u32(*position) + city->GetCityName()->Strlen() - 1);
    }
    return 1;
  }
  if (GeTokenEquals(token, "%POPULATION%", 12)) {
    ScString fallback("0");
    CtCity *city = GeGetCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", population);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, number.begin, (u32)(number.end - number.begin));
      *position = i32(u32(*position) + u32(number.end - number.begin) - 1);
    } else {
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position = i32(u32(*position) + u32(fallback.end - fallback.begin) - 1);
    }
    return 1;
  }
  if (GeTokenEquals(token, "%YEAR%", 6)) {
    CtCity *city = GeGetCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", (i32)year);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, number.begin, (u32)(number.end - number.begin));
      *position = i32(u32(*position) + u32(number.end - number.begin) - 1);
    } else {
      ScString fallback("1900");
      u32 remove = token.Strlen();
      value->Replace(u32(*position), remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position = i32(u32(*position) + u32(fallback.end - fallback.begin) - 1);
    }
    return 1;
  }
  if (GeTokenEquals(token, "%ANYNEIGHBOR%", 13)) {
    CtCity *city = GeGetCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return 0;
    GeRandom random(kScRandomSeedFromClock);
    ScString name;
    neighbors->GetNeighborName((u8)random.Next(4), &name);
    u32 remove = token.Strlen();
    value->Replace(u32(*position), remove, name.begin, (u32)(name.end - name.begin));
    *position = i32(u32(*position) + u32(name.end - name.begin) - 1);
    return 1;
  }
  if (GeTokenEquals(token, "%PARADENAME%", 12)) {
    ScString name;
    i32 country = GeGetPreferences()->GetCountry();
    u32 instance = 0;
    switch (country) {
    case 6:
      instance = 0x258;
      break;
    case 17:
      instance = 0x259;
      break;
    case 19:
      instance = 0x25a;
      break;
    case 31:
      instance = 0x25b;
      break;
    case 32:
      instance = 0x25c;
      break;
    case 45:
      instance = 0x25d;
      break;
    case 46:
      instance = 0x25e;
      break;
    case 49:
      instance = 0x25f;
      break;
    case 54:
      instance = 0x260;
      break;
    }
    if (instance) {
      GeTextResource resource(instance, kRsSupplementalGameTextGroup);
      name = resource;
    }
    u32 remove = token.Strlen();
    value->Replace(u32(*position), remove, name.begin, (u32)(name.end - name.begin));
    *position = i32(u32(*position) + u32(name.end - name.begin) - 1);
  }
  return 1;
}

// Ignore formatter replies, replace at the live mutable position and advance by replacement length minus one.
bool GeTextResource::ReplaceParameter(ScString *value, GeTextToken *token, i32 *position) {
  ScString name(static_cast<const ScStringView &>(token->name));
  u32 nameLength = u32(name.end - name.begin);
  ScString candidate = GeTokenSlice(*value, u32(*position), nameLength);
  if (!candidate.IsEqual(&name))
    return 0;
  ScString replacement;
  switch (token->kind) {
  case 1: {
    GeTextResource resource(token->first, token->second);
    ScString raw = resource;
    replacement = raw;
    break;
  }
  case 2: {
    ScLanguageUtility *utility = GeGetLanguageManager()->GetLanguageUtility(0);
    if (!utility)
      return 0;
    utility->MakeNumberString(i64(token->number), &replacement);
    break;
  }
  case 3: {
    ScLanguageUtility *utility = GeGetLanguageManager()->GetLanguageUtility(0);
    if (!utility)
      return 0;
    utility->MakeMoneyString(i64(token->number), &replacement);
    break;
  }
  case 4: {
    CtCity *city = GeGetCity();
    if (!city)
      return 0;
    CtNeighborsLayer *neighbors = city->NeighborsLayer();
    if (!neighbors)
      return 0;
    neighbors->GetNeighborName(u8(token->first), &replacement);
    break;
  }
  case 5: {
    char bytes[12];
    int length = snprintf(bytes, sizeof(bytes), "%d", token->number);
    replacement.FromChar(bytes, u32(length));
    break;
  }
  default:
    return 0;
  }
  value->Replace(u32(*position), nameLength, replacement.begin, u32(replacement.end - replacement.begin));
  *position = i32(u32(*position) + u32(replacement.end - replacement.begin) - 1);
  return 1;
}

static const ScString GeTokenNames[8] = {ScString("%MAYOR%", 7), ScString("%YOURNAME%", 10), ScString("%CITYNAME%", 10),
    ScString("%YOURCITY%", 10), ScString("%POPULATION%", 12), ScString("%YEAR%", 6), ScString("%PARADENAME%", 12),
    ScString("%ANYNEIGHBOR%", 13)};

static u32 GeFindPercent(const ScString &value, u32 start) {
  u32 length = u32(value.end - value.begin);
  const char *bytes = value.begin;
  for (u32 i = start; i < length; ++i)
    if (bytes[i] == '%')
      return i;
  return UINT32_MAX;
}

void GeTextResource::ReplaceParameters(ScString *value) {
  i32 position = i32(GeFindPercent(*value, 0));
  while (u32(position) < u32(value->end - value->begin)) {
    ReplaceCommonParameter(value, &position);
    position = i32(GeFindPercent(*value, u32(position) + 1));
  }
}

// Try the eight original names in their short-circuit order, with neighbor preceding parade despite static init order.
// The real leaf updates the shared text and position before the next live scan.
bool GeTextResource::ReplaceCommonParameter(ScString *value, i32 *position) {
  return ReplaceParameter(value, GeTokenNames[0], position) || ReplaceParameter(value, GeTokenNames[1], position) ||
      ReplaceParameter(value, GeTokenNames[2], position) || ReplaceParameter(value, GeTokenNames[3], position) ||
      ReplaceParameter(value, GeTokenNames[4], position) || ReplaceParameter(value, GeTokenNames[5], position) ||
      ReplaceParameter(value, GeTokenNames[7], position) || ReplaceParameter(value, GeTokenNames[6], position);
}

void GeTextResource::ReplaceParameters(ScString *value, GeTextToken **tokens, u32 count) {
  i32 position = i32(GeFindPercent(*value, 0));
  while (u32(position) < u32(value->end - value->begin)) {
    if (!ReplaceCommonParameter(value, &position) && tokens)
      for (u32 i = 0; i < count; ++i)
        ReplaceParameter(value, tokens[i], &position);
    position = i32(GeFindPercent(*value, u32(position) + 1));
  }
}

// Default-construct the owned name before setting kind, then copy the supplied String through its genuine interface.
// Destroy that temporary before publishing zero numeric value and the two separate resource or neighbor arguments.
GeTextToken::GeTextToken(u32 format, const ScStringView &value, u32 firstArgument, u32 secondArgument) {
  kind = format;
  {
    ScString copied(value);
    name = copied;
  }
  number = 0;
  first = firstArgument;
  second = secondArgument;
}

GeTextToken::~GeTextToken() {
}

void GeTextResource::ReplaceParameters(ScString *value, GeTextResolver resolver, void *context) {
  GeTextToken token;
  i32 position = -1;
  for (;;) {
    position = i32(GeFindPercent(*value, u32(position) + 1));
    if (u32(position) >= u32(value->end - value->begin))
      break;
    if (!ReplaceCommonParameter(value, &position) && resolver(*value, position, &token, context))
      ReplaceParameter(value, &token, &position);
  }
}

GeTextToken::GeTextToken() : kind(0), name("%TOKEN%", 7), first(0), second(0), number(0) {
}
