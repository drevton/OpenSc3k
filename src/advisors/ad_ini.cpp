#define _CRT_SECURE_NO_WARNINGS
#include "advisors/ad_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL_timer.h>

#include <list>

#include "common/sc3_error.h"
#include "common/utf8.h"
#include "resources/api.h"

// Compare owned section names by unsigned bytes and use length only after a matching prefix.
// The result preserves the original cache ordering without a generic comparator or tree implementation.
static i32 AdCompareIniNames(const ScString &first, const ScString &second) {
  u32 first_length = static_cast<u32>(first.end - first.begin);
  u32 second_length = static_cast<u32>(second.end - second.begin);
  u32 length = first_length < second_length ? first_length : second_length;
  int compared = memcmp(first.begin, second.begin, length);
  return compared ? compared : first_length < second_length ? -1 : first_length > second_length;
}

// Release the cached section names while retaining the native pointer-array allocation for later reloads.
// No file state or keep-open preference changes here.
void AdIniFile::ClearSections() {
  for (u32 index = 0; index < sections.size; ++index)
    delete sections.data[index];
  sections.Clear();
}

// Find an existing section by its normalized name, stopping once the sorted cache passes the requested name.
// The returned record remains owned by this reader until its cache is cleared.
AdIniSection *AdIniFile::FindSection(const ScString &name) {
  for (u32 index = 0; index < sections.size; ++index) {
    i32 order = AdCompareIniNames(sections.data[index]->name, name);
    if (order == 0)
      return sections.data[index];
    if (order > 0)
      break;
  }
  return nullptr;
}

// Insert a copied section name in the original byte order while retaining the first offset for duplicate names.
// Cache storage is private parser state; the actual file position remains the separate original DWORD argument.
void AdIniFile::AddSection(const ScString &name, u32 offset) {
  u32 index = 0;
  while (index < sections.size) {
    i32 order = AdCompareIniNames(sections.data[index]->name, name);
    if (order == 0)
      return;
    if (order > 0)
      break;
    ++index;
  }
  AdIniSection *entry = new AdIniSection{name, offset};
  sections.Append();
  for (u32 next = sections.size - 1; next > index; --next)
    sections.data[next] = sections.data[next - 1];
  sections.data[index] = entry;
}

AdIniFile::AdIniFile() : keep_open(1), loaded(0) {
}

AdIniFile::~AdIniFile() {
  if (file.IsOpen())
    file.Close();
  ClearSections();
}

// Close the file if open, clear the cached section offsets and reset loaded without changing the keep-open preference.
bool AdIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  ClearSections();
  loaded = 0;
  return true;
}

// Read a logical line in forty-byte chunks, discard a whole CR/LF run and restore the first following non-newline byte.
// Validate assembled native UTF-8; retain defined partial prefixes, early output clearing and per-chunk NUL truncation.
bool AdIniFile::GetFileLine(ScString *out) {
  ScString text;
  bool read_any = false, newline = false;
  u32 position = file.Position(), remaining = file.Size() - position;
  out->ScString::FromChar("", 0);
  char bytes[42]{};
  while (remaining) {
    u32 count = remaining <= 40 ? remaining : 40;
    if (!file.Read(bytes, count))
      break;
    read_any = true;
    remaining -= count;
    u32 index = 0;
    while (index < count && bytes[index] != 13 && bytes[index] != 10)
      ++index;
    if (index < count) {
      newline = true;
      bytes[index] = 0;
      file.SeekAbsolute(static_cast<i32>(position + index));
      char next;
      while (file.Read(&next, 1)) {
        if (next != 13 && next != 10) {
          file.SeekRelative(-1);
          break;
        }
      }
    }
    bytes[index] = 0;
    text.ScString::Append(bytes, static_cast<u32>(strlen(bytes)));
    if (newline)
      break;
    position = file.Position();
  }
  if (!ScUtf8Validate(text.Data(), text.Strlen()))
    return false;
  out->ScString::FromChar(text.begin, static_cast<u32>(text.end - text.begin));
  return read_any;
}

// Select hexadecimal for a leading0x or any ASCII A-F/a-f byte; otherwise parse decimal through the original CRT.
u32 AdParseIniNumber(char *text) {
  u32 radix = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    radix = 16;
  else
    for (char *byte = text; *byte; ++byte)
      if ((*byte >= 'a' && *byte <= 'f') || (*byte >= 'A' && *byte <= 'F')) {
        radix = 16;
        break;
      }
  return static_cast<u32>(strtoul(text, nullptr, static_cast<int>(radix)));
}

// Apply the original string-wide uppercase operation to native UTF-8 text, allowing the owned String to resize.
static void AdIniUppercase(ScString *value) {
  value->MakeUpper();
}

// Remove only ASCII spaces at the left edge; tabs and other whitespace remain data in this parser.
static void AdIniTrimLeft(ScString *value) {
  u32 count = 0, length = static_cast<u32>(value->end - value->begin);
  while (count < length && value->begin[count] == 32)
    ++count;
  if (count)
    value->ScString::Erase(0, count);
}

// Drop trailing ASCII spaces from the already bounded parser value and keep its terminating zero.
static void AdIniTrimRight(ScString *value) {
  while (value->end != value->begin && value->end[-1] == 32)
    --value->end;
  *value->end = 0;
}

// Take the first equals-delimited prefix and leave its suffix in the original line.
static ScString AdIniTakeKey(ScString *line) {
  u32 length = static_cast<u32>(line->end - line->begin), index = 0;
  while (index < length && line->begin[index] != '=')
    ++index;
  if (index == length)
    return ScString();
  ScString key(line->begin, index);
  line->ScString::Erase(0, index + 1);
  return key;
}

// Borrow the owned path without copying the string or changing its lifetime.
const ScString *AdIniFile::GetPath() const {
  return &filename;
}

void AdIniFile::SetPath(const ScString &input) {
  if (loaded)
    Close();
  filename = input;
}

// Update the file-retention policy and close the file when switching to the non-retaining mode.
bool AdIniFile::SetLeaveFileOpenBetweenOperations(bool leave_open) {
  u8 value = static_cast<u8>(leave_open);
  if (keep_open != value) {
    keep_open = value;
    if (!leave_open)
      Close();
  }
  return true;
}

// Insert or replace an INI entry while preserving the surrounding file data and section cache behavior.
bool AdIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  ScString heading, entry;
  file.SetName(filename);
  if (!RsFile::FileExists(filename)) {
    loaded = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(true))
      return false;
    heading.Sprintf("[%s]\r\n", section.Data());
    file.Write(heading.Data(), heading.Strlen());
    entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(entry.Data(), entry.Strlen());
    file.Close();
    return true;
  }
  if (!loaded && !LoadSectionNames())
    return false;
  if (file.IsOpen())
    file.Close();
  if (!Open(true))
    return false;
  ScString section_name(section), key_name(key);
  AdIniUppercase(&section_name);
  AdIniUppercase(&key_name);
  AdIniSection *cached = FindSection(section_name);
  if (!cached) {
    file.SeekEnd();
    u32 position = file.Position();
    heading.Sprintf("\r\n[%s]\r\n", section.Data());
    file.Write(heading.Data(), heading.Strlen());
    entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(entry.Data(), entry.Strlen());
    file.Flush();
    AddSection(section_name, position + 2);
    if (!keep_open)
      Close();
    return true;
  }
  if (file.SeekAbsolute(cached->offset) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString candidate, search_key(key), line;
  AdIniUppercase(&search_key);
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  while (true) {
    u32 position = file.Position();
    if (!GetFileLine(&line))
      break;
    AdIniTrimLeft(&line);
    u32 length = line.Strlen();
    if (!length || line.begin[0] == ';')
      continue;
    bool next_section = line.begin[0] == '[';
    if (!next_section) {
      u32 delimiter = 0;
      while (delimiter < length && line.begin[delimiter] != '=')
        ++delimiter;
      candidate = line.Left(delimiter);
      AdIniTrimRight(&candidate);
      AdIniUppercase(&candidate);
      if (candidate.Strlen() != search_key.Strlen() || memcmp(candidate.begin, search_key.begin, candidate.Strlen()))
        continue;
    }
    u32 tail_position = next_section ? position : file.Position(), size = file.Size() - tail_position;
    u8 *tail = new u8[size];
    if (next_section)
      file.SeekAbsolute(position);
    if (file.Read(tail, size)) {
      file.SeekAbsolute(position);
      entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
      file.Write(entry.Data(), entry.Strlen());
      file.Write(tail, size);
      file.Resize(file.Position());
    }
    delete[] tail;
    loaded = 0;
    if (!keep_open)
      Close();
    return true;
  }
  file.SeekEnd();
  file.Position();
  entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
  file.Write(entry.Data(), entry.Strlen());
  file.Flush();
  if (!keep_open)
    Close();
  return true;
}

// Format a signed decimal value and send it through the virtual text-entry writer.
bool AdIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  loaded = 0;
  ScString text;
  text.Sprintf("%ld", static_cast<long>(value));
  return WriteEntry(section, key, text);
}

bool AdIni::WriteEntry(const ScString &, const ScString &, const ScString &) {
  return false;
}

bool AdIni::WriteNumber(const ScString &, const ScString &, i32) {
  return false;
}

// Encode bytes as uppercase hexadecimal pairs and write the original trailing zero character.
static void AdEncodeIniBinary(const u8 *bytes, u32 length, char *out) {
  for (u32 index = 0; index < length; ++index) {
    u8 high = static_cast<u8>((bytes[index] >> 4) + '0'), low = static_cast<u8>((bytes[index] & 15) + '0');
    *out++ = static_cast<char>(high > '9' ? high + 7 : high);
    *out++ = static_cast<char>(low > '9' ? low + 7 : low);
  }
  *out = '0';
}

// Decode hexadecimal pairs, substituting zero digits while reporting any invalid character.
static bool AdDecodeIniBinary(const char *text, u32 length, u8 *out) {
  bool valid = true;
  for (u32 index = 0; index < length; index += 2) {
    u8 byte = 0;
    for (i32 shift = 4; shift >= 0; shift -= 4) {
      i32 digit = *text++;
      if (digit < '0' || (digit > '9' && digit < 'A') || (digit > 'F' && digit < 'a') || digit > 'f') {
        digit = '0';
        valid = false;
      } else if (digit > 'F')
        digit -= 39;
      else if (digit > '9')
        digit -= 7;
      byte = static_cast<u8>(byte + ((digit - '0') << shift));
    }
    *out++ = byte;
  }
  return valid;
}

// Encode a nonempty binary entry and pass the resulting text to the virtual writer.
bool AdIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size) {
  if (!size)
    return false;
  // Widen before allocation: wrapping the original 32-bit size led to an undersized buffer, not a game result.
  char *encoded = new char[static_cast<u64>(size) * 2 + 1];
  AdEncodeIniBinary(bytes, size, encoded);
  encoded[static_cast<usize>(size) * 2] = 0;
  bool result;
  {
    ScString text(encoded);
    result = WriteEntry(section, key, text);
  }
  delete[] encoded;
  return result;
}

// Clear the output and parse the retrieved entry as a signed decimal integer.
bool AdIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString text;
  *out = 0;
  return ReadEntry(section, key, &text) && sscanf(text.Data(), "%d", out) == 1;
}

// Decode a retrieved binary entry into newly allocated bytes, leaving the output untouched when absent.
u32 AdIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = static_cast<u32>(text.end - text.begin), count = length >> 1;
  // The decoder consumes the terminating NUL for an odd length and rejects it. Reserve that final byte as well.
  *out = new u8[count + (length & 1)];
  if (!AdDecodeIniBinary(text.begin, length, *out)) {
    delete[] *out;
    *out = nullptr;
    return 0;
  }
  return count;
}

// Decode at most the requested output capacity and return the number of complete pairs.
u32 AdIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = static_cast<u32>(text.end - text.begin), limit = size * 2;
  if (limit > length)
    limit = length;
  return AdDecodeIniBinary(text.begin, limit, out) ? limit >> 1 : 0;
}

bool AdIni::WriteBinaryEntry(const ScString &, const ScString &, const u8 *, u32) {
  return false;
}

bool AdIni::ReadNumber(const ScString &, const ScString &, i32 *) {
  return false;
}

u32 AdIni::ReadBinaryEntry(const ScString &, const ScString &, u8 **) {
  return 0;
}

u32 AdIni::ReadBinaryEntry(const ScString &, const ScString &, u8 *, u32) {
  return 0;
}

// Copy the ordered section names before invoking callbacks, then honor the file-retention policy.
bool AdIniFile::EnumSections(AdIniCallback callback, void *owner) {
  if (!loaded && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString empty;
  std::list<ScString> names;
  for (u32 index = 0; index < sections.size; ++index)
    names.push_back(sections.data[index]->name);
  for (const ScString &name : names)
    callback(name, empty, owner);
  if (!keep_open)
    Close();
  return true;
}

// Normalize the requested name and search the cached sections after applying the file-retention policy.
bool AdIniFile::SectionExists(const ScStringView &section) {
  if (!loaded && !LoadSectionNames())
    return false;
  ScString key(section);
  AdIniUppercase(&key);
  if (!keep_open)
    Close();
  return FindSection(key) != nullptr;
}

bool AdIni::SectionExists(const ScStringView &) {
  return false;
}

// Retry the actual file open while the named path exists, with the original read/write masks and three-second cutoff.
// Capture each retry time before the alertable250ms sleep; an existing open file still follows the initial clock query.
bool AdIniFile::Open(bool write) {
  time_t start = time(nullptr);
  u32 access = write ? 3 : 1, creation = write ? 3 : 2, sharing = write ? 0 : 1;
  while (!file.Open(access, creation, sharing)) {
    if (RsFile::FileExists(filename)) {
      time_t now = time(nullptr);
      SDL_Delay(250);
      if (now - start < 3)
        continue;
    }
    return false;
  }
  return true;
}

// Index uppercase bracketed section names at their physical line-start offsets, retaining the first duplicate section.
// Reuse an open stream or bind/open the filename, scan through EOF and mark the resulting cache loaded.
bool AdIniFile::LoadSectionNames() {
  ClearSections();
  if (!file.IsOpen()) {
    file.name = filename;
    if (!Open(0))
      return false;
  }
  ScString line, section;
  file.SeekBegin();
  u32 position = file.Position();
  while (GetFileLine(&line)) {
    AdIniTrimLeft(&line);
    u32 length = static_cast<u32>(line.end - line.begin);
    if (length && line.begin[0] == '[') {
      u32 end = 1;
      while (end < length && line.begin[end] != ']')
        ++end;
      if (end < length && end >= 2) {
        section.ScString::FromChar(line.begin + 1, end - 1);
        AdIniUppercase(&section);
        AddSection(section, position);
      }
    }
    position = file.Position();
  }
  loaded = 1;
  return true;
}

// Seek the cached section, discard its header and return the first matching uppercase key before the next section.
// Retain output on failure, preserve value case/semicolons and close through the virtual method only when keep_open is zero.
bool AdIniFile::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  bool found = false;
  if (loaded || LoadSectionNames()) {
    ScString section_name(section.begin, static_cast<u32>(section.end - section.begin));
    AdIniUppercase(&section_name);
    ScString key_name(key.begin, static_cast<u32>(key.end - key.begin));
    AdIniUppercase(&key_name);
    AdIniSection *entry = FindSection(section_name);
    if (entry) {
      u32 position = file.SeekAbsolute(static_cast<i32>(entry->offset));
      if (position < file.Size()) {
        ScString candidate, line;
        if (GetFileLine(&line)) {
          while (GetFileLine(&line)) {
            AdIniTrimLeft(&line);
            if (line.begin == line.end || line.begin[0] == ';')
              continue;
            if (line.begin[0] == '[') {
              line.ScString::FromChar("", 0);
              break;
            }
            candidate = AdIniTakeKey(&line);
            AdIniTrimRight(&candidate);
            AdIniUppercase(&candidate);
            if (candidate.Strlen() == key_name.Strlen() &&
                !memcmp(candidate.begin, key_name.begin, candidate.Strlen())) {
              AdIniTrimLeft(&line);
              AdIniTrimRight(&line);
              out->ScString::FromChar(line.begin, static_cast<u32>(line.end - line.begin));
              found = true;
              break;
            }
          }
        }
      }
    }
  }
  if (!keep_open)
    Close();
  return found;
}

// Prefer the lazily selected package entry, matching its section header exactly and its trimmed uppercase key.
// Validate consumed native text without changing raw reads, value whitespace, loose fallback or missed outputs.
bool AdIni::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  InitFileType();
  if (mode == 1)
    return AdIniFile::ReadEntry(section, key, out);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.ScString::Append("]", 1);
  u32 count;
  if (!package_file.Read(&count, 4))
    ScFatal(SC_ERROR_TRUNCATED, "INI package did not initialize its section count");
  ScString line;
  bool selected = false;
  ScString key_name(key.begin, static_cast<u32>(key.end - key.begin));
  AdIniUppercase(&key_name);
  AdIniTrimLeft(&key_name);
  AdIniTrimRight(&key_name);
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (!ScUtf8Validate(line.Data(), line.Strlen()))
      return false;
    if (selected) {
      if (line.begin != line.end && line.begin[0] == '[')
        break;
      ScString candidate = AdIniTakeKey(&line);
      AdIniUppercase(&candidate);
      AdIniTrimLeft(&candidate);
      AdIniTrimRight(&candidate);
      if (candidate.Strlen() == key_name.Strlen() && !memcmp(candidate.begin, key_name.begin, candidate.Strlen())) {
        out->ScString::FromChar(line.begin, static_cast<u32>(line.end - line.begin));
        return true;
      }
    } else if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
      selected = true;
  }
  return false;
}

// Original buffered enumeration stores two concrete Strings per40-byte source vector element.
// Keep rows owned until every callback finishes, including callback mutations to either String.
struct AdIniPair {
  ScString key, value;
};

// Enumerate the requested section through the actual primary Unknown identity and three separate callback arguments.
// Loose parsing buffers all valid rows before callbacks, skips empty pairs and retains case after ASCII-space trimming.
bool AdIniFile::EnumEntries(const ScString &section, AdIniCallback callback, void *owner) {
  if (!loaded && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString section_name(section.begin, static_cast<u32>(section.end - section.begin));
  AdIniUppercase(&section_name);
  AdIniSection *entry = FindSection(section_name);
  if (!entry || file.SeekAbsolute(static_cast<i32>(entry->offset)) >= file.Size()) {
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
  ScArray<AdIniPair *> rows;
  while (GetFileLine(&line)) {
    AdIniTrimLeft(&line);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    key = AdIniTakeKey(&line);
    AdIniTrimRight(&key);
    AdIniTrimLeft(&line);
    AdIniTrimRight(&line);
    if (key.begin != key.end && line.begin != line.end)
      *rows.Append() = new AdIniPair{key, line};
  }
  for (u32 index = 0; index < rows.size; ++index)
    callback(rows.data[index]->key, rows.data[index]->value, owner);
  if (!keep_open)
    Close();
  for (u32 index = 0; index < rows.size; ++index)
    delete rows.data[index];
  return true;
}

// Enumerate package entries through the typed primary Unknown callback boundary.
// Packaged rows call immediately and stop at the first empty pair; inherited loose enumeration buffers before dispatch.
bool AdIni::EnumEntries(const ScString &section, AdIniCallback callback, void *owner) {
  InitFileType();
  if (mode == 1)
    return AdIniFile::EnumEntries(section, callback, owner);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.ScString::Append("]", 1);
  u32 count;
  if (!package_file.Read(&count, 4))
    ScFatal(SC_ERROR_TRUNCATED, "INI package did not initialize its section count");
  ScString line;
  bool selected = false;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (!ScUtf8Validate(line.Data(), line.Strlen()))
      return false;
    if (!selected) {
      if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
        selected = true;
      continue;
    }
    if (line.begin != line.end && line.begin[0] == '[')
      break;
    ScString key = AdIniTakeKey(&line);
    AdIniTrimLeft(&key);
    AdIniTrimRight(&key);
    AdIniTrimLeft(&line);
    AdIniTrimRight(&line);
    if (key.begin == key.end || line.begin == line.end)
      break;
    callback(key, line, owner);
  }
  return true;
}

// Read a previously uninitialized format word only after the complete external input has been supplied.
static u32 AdIniReadWord(RsFile *file) {
  u32 value;
  if (!file->Read(&value, 4))
    ScFatal(SC_ERROR_TRUNCATED, "INI package did not initialize its format word");
  return value;
}

// Form the source split-path basename plus extension, independently truncating each component to 255 bytes.
// Ignore the drive and directory prefix; native filename casing and component limits retain complete UTF-8 scalars.
static ScString AdIniPackageKey(const ScString &filename) {
  const char *begin = filename.begin, *base = begin, *dot = nullptr, *end = begin + strlen(begin);
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
  AdIniUppercase(&result);
  return result;
}

AdIni::AdIni() : mode(0), buffer(nullptr), buffer_capacity(0) {
}

AdIni::~AdIni() {
  delete[] buffer;
}

void AdIni::SetCompressedPath(const ScString &input) {
  mode = 0;
  package_name = input;
}

// Borrow the stored package path without copying or retaining its String.
const ScString *AdIni::GetCompressedPath() const {
  return &package_name;
}

bool AdIni::EnumEntriesOld(const ScString &, AdIniCallback, void *) {
  return false;
}

// Read a length-prefixed package String with genuine byte-count input validation.
void AdIni::ReadString(ScString *out) {
  u32 length = AdIniReadWord(&package_file);
  if (length > buffer_capacity) {
    if (length == UINT32_MAX)
      ScFatal(SC_ERROR_INVALID, "INI package string length overflows its allocation");
    delete[] buffer;
    buffer = new char[static_cast<usize>(length) + 1];
    buffer[length] = 0;
  }
  u32 written = length;
  if (!package_file.opened)
    written = 0;
  else
    package_file.Read(buffer, &written);
  if (written < length && !memchr(buffer, 0, written))
    ScFatal(SC_ERROR_TRUNCATED, "INI package string would read undefined input bytes");
  if (!buffer)
    ScFatal(SC_ERROR_TRUNCATED, "INI package string has no initialized storage");
  ScString value(buffer, static_cast<u32>(strlen(buffer)));
  out->ScString::FromChar(value.begin, static_cast<u32>(value.end - value.begin));
}

// Perform the lazy package-index selection using the configured package and filename.
// Validate native filename text; retain byte offsets and fall back to loose input for unsupported text.
void AdIni::InitFileType() {
  if (mode)
    return;
  if (!package_name.Strlen() || !RsFile::FileExists(package_name)) {
    mode = 1;
    return;
  }
  package_file.name = package_name;
  if (!package_file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    mode = 1;
    return;
  }
  package_offset = 0;
  ScString expected = AdIniPackageKey(filename);
  package_file.SeekBegin();
  u32 count = AdIniReadWord(&package_file), offset = count;
  ScString name;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&name);
    if (!ScUtf8Validate(name.Data(), name.Strlen())) {
      mode = 1;
      return;
    }
    AdIniUppercase(&name);
    package_file.Read(&offset, 4);
    if (name.Strlen() == expected.Strlen() && !memcmp(name.begin, expected.begin, name.Strlen())) {
      package_offset = offset;
      mode = 2;
      return;
    }
  }
  mode = 1;
}
