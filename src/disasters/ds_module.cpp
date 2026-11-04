#include "disasters/ds_module.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL_timer.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/utf8.h"
#include "common/value.h"
#include "disasters/api.h"
#include "economy/api.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/module.h"
#include "terrain/api.h"
#include "window/api.h"

// Negative line coordinates subtract the original integer bias after the shifted floating-point truncation.
enum DsLineCoordinateRounding : i32 {
  kDsNegativeLineCoordinateBias = 0x186a0,
};

static constexpr u32 kDsRecordFrameMarker = 0xdeadbeef;

DsArchive::~DsArchive() {
  ScDelete(&record);
}

// Borrow the retained record's stream, or return null when the archive has no live wrapper.
RsStream *DsArchive::Stream() {
  return record ? record->Stream() : nullptr;
}

// Read the version and flag bytes before checking the marker; a nonmarker closes and reopens the raw record.
DsArchive::DsArchive(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : version(0), option(0), marker(0), record(nullptr), valid(0), versions{0, 0, 0} {
  if (!city)
    return;
  versions[0] = city->GetVersionComponentFromSaveFile(0);
  versions[1] = city->GetVersionComponentFromSaveFile(1);
  versions[2] = city->GetVersionComponentFromSaveFile(2);
  record = new DsStreamRecord(segment, key);
  valid = record->IsValid();
  if (!valid) {
    delete record;
    record = nullptr;
    return;
  }
  valid = record->Stream()->ReadU16(&version);
  if (valid) {
    u8 flags = 0;
    valid = record->Stream()->ReadU8(&flags);
    option = flags & 1;
    extension = (flags & 2) != 0;
    if (extension && valid)
      valid = record->Stream()->ReadU8(&flags);
  }
  if (valid)
    record->Stream()->ReadU32(&marker);
  if (valid)
    valid = marker == kDsRecordFrameMarker;
  if (marker != kDsRecordFrameMarker) {
    option = 0;
    version = 0;
    marker = kDsRecordFrameMarker;
    delete record;
    record = nullptr;
    record = new DsStreamRecord(segment, key);
    valid = record->IsValid();
    if (!valid) {
      delete record;
      record = nullptr;
    }
  }
}

// Query current city version components before opening a write record, then emit version, two-byte flags and marker.
// Preserve raw write bytes; the marker result is ignored, and failed prefix writes destroy the wrapper immediately.
DsArchive::DsArchive(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 input_version, u8 input_option)
    : version(input_version), option(input_option), record(nullptr), versions{0, 0, 0} {
  if (!city) {
    valid = 0;
    return;
  }
  versions[0] = city->GetVersionComponent(0);
  versions[1] = city->GetVersionComponent(1);
  versions[2] = city->GetVersionComponent(2);
  record = new DsStreamRecord(segment, key, 0);
  valid = record->IsValid();
  if (valid) {
    valid = record->Stream()->WriteU16(version);
    if (valid)
      valid = record->Stream()->WriteU16(u16(option ? 3 : 2));
    if (valid)
      record->Stream()->WriteU32(kDsRecordFrameMarker);
  }
  if (!valid) {
    delete record;
    record = nullptr;
  }
}

DsDirectorClasses::DsDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
DsDirectorClasses::~DsDirectorClasses() {
  while (head.next != &head) {
    DsClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void DsDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  DsClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  DsClassRegistration *entry = new DsClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

DsDllDirector::DsDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

DsDllDirector::~DsDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool DsDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsDllDirector::AddRef() {
  return ++references;
}

u32 DsDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 DsDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 DsDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *DsDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *DsDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool DsDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its own children, then append the child without a retain.
void DsDllDirector::AddDirector(DsDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void DsDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (DsClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool DsDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (DsClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
    if (entry->clsid != clsid)
      continue;
    ScUnknown *object = entry->create();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let live children veto unloading before checking this director's own reference word.
// The installed PC body differs from the available SDK's reference-count shortcut.
bool DsDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool DsDllDirector::OnUnload() {
  return true;
}

bool DsDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool DsDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool DsDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  DsFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool DsDllDirector::PreFrameWorkInit() {
  return true;
}

bool DsDllDirector::PreAppInit() {
  return true;
}

bool DsDllDirector::PostAppInit() {
  return true;
}

bool DsDllDirector::PreAppShutdown() {
  return true;
}

bool DsDllDirector::PostAppShutdown() {
  return true;
}

bool DsDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool DsDllDirector::AbortiveQuit() {
  return true;
}

bool DsDllDirector::OnInstall() {
  return true;
}

// Register the original twelve disaster factories without constructing their game objects.
DsDirector::DsDirector() {
  classes.Insert(kDsControllerClassId, DsCreateLayer, 0);
  classes.Insert(kDsFireManagerClassId, DsCreateFireManager, 0);
  classes.Insert(kDsTornadoManagerClassId, DsCreateTornadoManager, 0);
  classes.Insert(kDsTrackManagerClassId, DsCreateTrackManager, 0);
  classes.Insert(kDsRiotManagerClassId, DsCreateClassE2FE8D38, 0);
  classes.Insert(kDsUfoManagerClassId, DsCreateClass22FE8D3E, 0);
  classes.Insert(kDsParadeManagerClassId, DsCreateClass84C92CBE, 0);
  classes.Insert(kDsToxicManagerClassId, DsCreateClassC2FE8D43, 0);
  classes.Insert(kDsWaterManagerClassId, DsCreateClass52FE8D50, 0);
  classes.Insert(kDsLocustManagerClassId, DsCreateLocustManager, 0);
  classes.Insert(kDsImpactManagerClassId, DsCreateClass52FE8D52, 0);
  classes.Insert(kDsEventManagerClassId, DsCreateClass52FE8D53, 0);
}

DsDirector::~DsDirector() {
}

u32 DsDirector::GetDirectorID() const {
  return kDsDirectorId;
}

// Construct and register the module director on first access, with normal static destruction at process exit.
ScDirector *DsGetDirector() {
  static DsDirector director;
  return &director;
}

static void DsUpperIniName(ScString *name) {
  name->MakeUpper();
}

// Remove only ASCII spaces from the requested edges. Bound the scan to exclude the source's all-space overread.
static void DsTrimIniName(ScString *name, bool leading, bool trailing) {
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

DsIniIndex::~DsIniIndex() {
  Clear();
}

// Delete each owned section record. Retain the simple backing allocation for a later index rebuild.
void DsIniIndex::Clear() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
  entries.Clear();
}

DsIniFile::DsIniFile() : keep_open(1), indexed(0) {
}

DsIniFile::~DsIniFile() {
  if (file.IsOpen())
    file.Close();
}

// Borrow the reader's selected filename. This does not open the file or add a reference.
const ScString *DsIniFile::GetPath() const {
  return &name;
}

// Invalidate an already loaded section index through virtual Close. Then replace the complete selected path.
void DsIniFile::SetPath(const ScString &input) {
  if (indexed)
    Close();
  name = input;
}

// Release the open handle and each section record. Retain the selected filename and keep-open preference.
bool DsIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  sections.Clear();
  indexed = 0;
  return 1;
}

// Open the concrete file with the source access/share modes, retaining the bounded retry for existing locked files.
// A path absent from the native filesystem fails without sleeping or retrying.
bool DsIniFile::Open(bool write) {
  time_t start = time(nullptr);
  while (!file.Open(write ? 3 : 1, write ? 3 : 2, write ? 0 : 1)) {
    if (RsFile::FileExists(name)) {
      time_t now = time(nullptr);
      SDL_Delay(250);
      if (i32(u32(now) - u32(start)) < 3)
        continue;
    }
    return 0;
  }

  return 1;
}

// Preserve the early output clear and validate native UTF-8 only after assembling the complete logical line.
bool DsIniFile::GetFileLine(ScString *out) {
  ScString line;
  u32 position = file.Position(), remaining = file.Size() - position;
  out->ScString::FromChar("", 0);
  u8 read = 0;
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
    if (newline)
      break;
    position = file.Position();
    if (!remaining)
      break;
  }
  if (!ScUtf8Validate(line.Data(), line.Strlen()))
    return false;
  *out = line;
  return read;
}

// Index physical line-start positions by uppercase section name. The first duplicate retains its original position.
bool DsIniFile::LoadSectionNames() {
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
    DsTrimIniName(&line, true, false);
    if (line.Strlen() && line.begin[0] == '[') {
      u32 end = 1;
      while (end < line.Strlen() && line.begin[end] != ']')
        ++end;
      if (end >= 2 && end < line.Strlen()) {
        ScString key(line.begin + 1, end - 1);
        DsUpperIniName(&key);
        u32 i = 0;
        while (i < sections.entries.size && !sections.entries.data[i]->name.IsEqual(&key))
          ++i;
        if (i == sections.entries.size) {
          DsIniSection *section = new DsIniSection;
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
bool DsIniFile::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString section_name(section.begin, static_cast<u32>(section.end - section.begin));
  ScString key_name(key.begin, static_cast<u32>(key.end - key.begin));
  DsUpperIniName(&section_name);
  DsUpperIniName(&key_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size ||
      file.SeekAbsolute(static_cast<i32>(sections.entries.data[index]->position)) >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  u8 found = 0;
  ScString parsed, line;
  if (GetFileLine(&line))
    while (GetFileLine(&line)) {
      DsTrimIniName(&line, true, false);
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
      DsTrimIniName(&parsed, false, true);
      DsUpperIniName(&parsed);
      if (parsed.IsEqual(&key_name)) {
        DsTrimIniName(&line, true, true);
        *out = line;
        found = 1;
        break;
      }
    }
  if (!keep_open)
    Close();
  return found;
}

// Reject empty section or key names, invalidate the section index and forward the formatted signed value.
bool DsIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  indexed = 0;
  ScString text;
  text.Sprintf("%ld", static_cast<long>(value));
  return WriteEntry(section, key, text);
}

// Encode nonempty bytes as uppercase hexadecimal and forward the temporary String to the virtual entry writer.
// Assert the original DWORD allocation bound instead of extending overflowing inputs with native-width arithmetic.
bool DsIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length) {
  if (!length)
    return false;
  if (length > (UINT32_MAX - 1u) / 2u) {
    assert(false && "The original encoded INI allocation would wrap");
    abort();
  }
  char *encoded = new char[usize(length) * 2 + 1];
  for (u32 index = 0; index < length; ++index) {
    u8 high = data[index] >> 4, low = data[index] & 15;
    encoded[usize(index) * 2] = char(high < 10 ? high + '0' : high + 'A' - 10);
    encoded[usize(index) * 2 + 1] = char(low < 10 ? low + '0' : low + 'A' - 10);
  }
  encoded[usize(length) * 2] = 0;
  bool result;
  {
    ScString text(encoded);
    result = WriteEntry(section, key, text);
  }
  delete[] encoded;
  return result;
}

// Clear the numeric output, read the named entry and accept only a successful signed decimal conversion.
bool DsIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString text;
  *out = 0;
  if (!ReadEntry(section, key, &text))
    return false;
#ifdef _MSC_VER
  return sscanf_s(text.begin, "%d", out) == 1;
#else
  return sscanf(text.begin, "%d", out) == 1;
#endif
}

// Decode pairs in source order, substituting zero for each invalid nibble while retaining the complete output.
// An odd final digit consumes the String's initialized terminator and makes the result invalid.
static bool DsIniDecodeBinary(const char *input, u32 length, u8 *out) {
  bool valid = true;
  for (u32 index = 0; index < length; index += 2) {
    u8 value = 0;
    for (u32 part = 0; part < 2; ++part) {
      i32 byte = i8(input[index + part]), digit;
      if (byte >= '0' && byte <= '9')
        digit = byte - '0';
      else if (byte >= 'A' && byte <= 'F')
        digit = byte - 'A' + 10;
      else if (byte >= 'a' && byte <= 'f')
        digit = byte - 'a' + 10;
      else {
        digit = 0;
        valid = false;
      }
      value = u8(value + (u32(digit) << (part ? 0 : 4)));
    }
    *out++ = value;
  }
  return valid;
}

// Read and decode an allocated hexadecimal entry, clearing the published buffer if a digit is invalid.
// Preserve the floor-size allocation; odd lengths would make the original decoder write beyond it.
u32 DsIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return u32(text.end - text.begin) >> 1;
  u32 length = u32(text.end - text.begin), result = length >> 1;
  if (length & 1) {
    assert(false && "The original hexadecimal INI output would exceed its allocation");
    abort();
  }
  *out = new u8[result];
  if (!DsIniDecodeBinary(text.begin, length, *out)) {
    delete[] *out;
    *out = nullptr;
    return 0;
  }
  return result;
}

// Decode no more than the requested output capacity and return the number of valid complete byte pairs.
u32 DsIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 length) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 limit = length * 2, available = u32(text.end - text.begin);
  if (limit > available)
    limit = available;
  return DsIniDecodeBinary(text.begin, limit, out) ? limit >> 1 : 0;
}

// Select the source decimal/hexadecimal radix from the complete C string, then preserve 32-bit CRT strtoul behavior.
// The host-wide conversion keeps negative values and overflow consistent when unsigned long is wider than a DWORD.
u32 DsParseNumber(const char *value) {
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

static void DsIniUppercase(ScString *value) {
  DsUpperIniName(value);
}

// Read text-bearing packaged filenames/rows without changing raw record lengths, offsets or buffer reuse.
// Installed SYS.PAK text is ASCII; native additions use UTF-8, and malformed text fails without a guessed code page.
static bool DsReadIniText(DsIni *reader, ScString *text) {
  reader->ReadString(text);
  return ScUtf8Validate(text->Data(), text->Strlen());
}

// Remove only ASCII spaces at the left edge. Tabs and other whitespace remain data in this parser.
static void DsIniTrimLeft(ScString *value) {
  u32 count = 0, length = static_cast<u32>(value->end - value->begin);
  while (count < length && value->begin[count] == 32)
    ++count;
  if (count)
    value->ScString::Erase(0, count);
}

// Drop trailing ASCII spaces from the bounded parser value. Tabs remain data, and the new end is zero terminated.
static void DsIniTrimRight(ScString *value) {
  while (value->end != value->begin && value->end[-1] == 32)
    --value->end;
  *value->end = 0;
}

// Take the first equals-delimited prefix and leave its suffix in the original line.
static ScString DsIniTakeKey(ScString *line) {
  u32 length = static_cast<u32>(line->end - line->begin), index = 0;
  while (index < length && line->begin[index] != '=')
    ++index;
  if (index == length)
    return ScString();
  ScString key(line->begin, index);
  line->ScString::Erase(0, index + 1);
  return key;
}

// Copy the current formatted String into its retained byte vector before the original synchronous file write.
static void DsIniWriteText(RsFile *file, ScArray<char> *bytes, const ScString &text) {
  u32 length = u32(text.end - text.begin);
  bytes->Reserve(length);
  if (length)
    memcpy(bytes->data, text.begin, length);
  bytes->size = length;
  file->Write(bytes->data, length);
}

// Rewrite or insert the first matching INI entry while preserving comments and subsequent sections.
// The installed writer ignores write and resize replies and invalidates shifted section offsets.
bool DsIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  ScString heading, entry;
  ScArray<char> heading_bytes, entry_bytes;
  file.SetName(name);
  if (!RsFile::FileExists(name)) {
    indexed = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(true))
      return false;
    heading.Sprintf("[%s]\r\n", section.begin);
    DsIniWriteText(&file, &heading_bytes, heading);
    entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
    DsIniWriteText(&file, &entry_bytes, entry);
    file.Close();
    return true;
  }
  if (!indexed && !LoadSectionNames())
    return false;
  if (file.IsOpen())
    file.Close();
  if (!Open(true))
    return false;
  ScString section_name(section), key_name(key);
  DsUpperIniName(&section_name);
  DsUpperIniName(&key_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size) {
    file.SeekEnd();
    u32 position = file.Position();
    heading.Sprintf("\r\n[%s]\r\n", section.begin);
    DsIniWriteText(&file, &heading_bytes, heading);
    entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
    DsIniWriteText(&file, &entry_bytes, entry);
    file.Flush();
    index = 0;
    while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
      ++index;
    if (index == sections.entries.size) {
      DsIniSection *added = new DsIniSection;
      added->name = section_name;
      added->position = position + 2;
      *sections.entries.Append() = added;
    }
    if (!keep_open)
      Close();
    return true;
  }
  u32 section_position = file.SeekAbsolute(sections.entries.data[index]->position);
  u32 file_size = file.Size();
  if (section_position >= file_size) {
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
    DsTrimIniName(&line, true, false);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    bool boundary = line.begin[0] == '[';
    if (!boundary) {
      parsed = DsIniTakeKey(&line);
      DsTrimIniName(&parsed, false, true);
      DsUpperIniName(&parsed);
      if (!parsed.IsEqual(&key_name))
        continue;
    }
    u32 tail_start = boundary ? position : file.Position();
    u32 remaining = file.Size() - tail_start;
    u8 *tail = new u8[remaining];
    if (boundary)
      file.SeekAbsolute(position);
    if (file.Read(tail, remaining)) {
      file.SeekAbsolute(position);
      entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
      DsIniWriteText(&file, &entry_bytes, entry);
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
  file.Position();
  entry.Sprintf("%s = %s\r\n", key.begin, value.begin);
  DsIniWriteText(&file, &entry_bytes, entry);
  file.Flush();
  if (!keep_open)
    Close();
  return true;
}

// Prefer the lazily selected package entry, matching its section header exactly and its trimmed uppercase key.
// Package values retain whitespace; loose fallback uses the inherited rules and misses leave the caller's output intact.
bool DsIni::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  InitFileType();
  if (mode == 1)
    return DsIniFile::ReadEntry(section, key, out);
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
  DsIniUppercase(&key_name);
  DsIniTrimLeft(&key_name);
  DsIniTrimRight(&key_name);
  for (u32 index = 0; index < count; ++index) {
    if (!DsReadIniText(this, &line))
      return false;
    if (selected) {
      if (line.begin != line.end && line.begin[0] == '[')
        break;
      ScString candidate = DsIniTakeKey(&line);
      DsIniUppercase(&candidate);
      DsIniTrimLeft(&candidate);
      DsIniTrimRight(&candidate);
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
struct DsIniPair {
  ScString key, value;
};

// Buffer valid loose-file pairs before dispatch, retaining case and closing only after the callbacks complete.
// Empty pairs are skipped in loose files; the packaged reader deliberately stops instead.
bool DsIniFile::EnumEntries(const ScString &section, DsIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString section_name(section.begin, static_cast<u32>(section.end - section.begin));
  DsIniUppercase(&section_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size ||
      file.SeekAbsolute(static_cast<i32>(sections.entries.data[index]->position)) >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString key, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScArray<DsIniPair *> rows;
  while (GetFileLine(&line)) {
    DsIniTrimLeft(&line);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    key = DsIniTakeKey(&line);
    DsIniTrimRight(&key);
    DsIniTrimLeft(&line);
    DsIniTrimRight(&line);
    if (key.begin != key.end && line.begin != line.end)
      *rows.Append() = new DsIniPair{key, line};
  }
  u32 count = rows.size;
  for (u32 row = 0; row < count; ++row)
    callback(rows.data[row]->key, rows.data[row]->value, context);
  if (!keep_open)
    Close();
  for (u32 row = 0; row < count; ++row)
    delete rows.data[row];
  return 1;
}

static bool DsIniNameLess(const ScString &left, const ScString &right) {
  u32 left_length = u32(left.end - left.begin), right_length = u32(right.end - right.begin);
  u32 common = left_length < right_length ? left_length : right_length;
  int order = memcmp(left.begin, right.begin, common);
  return order < 0 || (order == 0 && left_length < right_length);
}

// Snapshot the sorted section names before callbacks and pass the same empty value String with each name.
// Close after callbacks when requested, then retire the copied names in their traversal order.
bool DsIniFile::EnumSections(DsIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString value;
  ScArray<ScString *> names;
  for (u32 index = 0; index < sections.entries.size; ++index) {
    ScString *item = new ScString(sections.entries.data[index]->name);
    u32 position = 0;
    while (position < names.size && DsIniNameLess(*names.data[position], *item))
      ++position;
    names.Append();
    for (u32 next = names.size - 1; next > position; --next)
      names.data[next] = names.data[next - 1];
    names.data[position] = item;
  }
  for (u32 index = 0; index < names.size; ++index)
    callback(*names.data[index], value, context);
  if (!keep_open)
    Close();
  for (u32 index = 0; index < names.size; ++index)
    delete names.data[index];
  return true;
}

// Uppercase a copy of the requested section, closing first when configured, then query the live section index.
bool DsIniFile::SectionExists(const ScStringView &section) {
  if (!indexed && !LoadSectionNames())
    return false;
  ScString section_name(section);
  DsUpperIniName(&section_name);
  if (!keep_open)
    Close();
  for (u32 index = 0; index < sections.entries.size; ++index)
    if (sections.entries.data[index]->name.IsEqual(&section_name))
      return true;
  return false;
}

// Store a changed keep-open preference and close immediately when it changes to false.
bool DsIniFile::SetLeaveFileOpenBetweenOperations(bool keep) {
  if (keep_open != u8(keep)) {
    keep_open = keep;
    if (!keep)
      Close();
  }
  return true;
}

// Packaged rows call immediately and stop at the first empty pair; inherited loose enumeration buffers before dispatch.
bool DsIni::EnumEntries(const ScString &section, DsIniCallback callback, void *context) {
  InitFileType();
  if (mode == 1)
    return DsIniFile::EnumEntries(section, callback, context);
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
    if (!DsReadIniText(this, &line))
      return false;
    if (!selected) {
      if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
        selected = true;
      continue;
    }
    if (line.begin != line.end && line.begin[0] == '[')
      break;
    ScString key = DsIniTakeKey(&line);
    DsIniTrimLeft(&key);
    DsIniTrimRight(&key);
    DsIniTrimLeft(&line);
    DsIniTrimRight(&line);
    if (key.begin == key.end || line.begin == line.end)
      break;
    callback(key, line, context);
  }
  return true;
}

// Form the source split-path basename plus extension, independently truncating each component to 255 bytes.
// Ignore the drive and directory prefix; native filename casing and component limits retain complete UTF-8 scalars.
static ScString DsIniPackageKey(const ScString &name) {
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
  DsIniUppercase(&result);
  return result;
}

DsIni::DsIni() : mode(0), buffer(nullptr), buffer_capacity(0) {
}

DsIni::~DsIni() {
  delete[] buffer;
}

bool DsIni::WriteEntry(const ScString &, const ScString &, const ScString &) {
  return false;
}

bool DsIni::WriteNumber(const ScString &, const ScString &, i32) {
  return false;
}

bool DsIni::WriteBinaryEntry(const ScString &, const ScString &, const u8 *, u32) {
  return false;
}

bool DsIni::ReadNumber(const ScString &, const ScString &, i32 *) {
  return false;
}

u32 DsIni::ReadBinaryEntry(const ScString &, const ScString &, u8 **) {
  return 0;
}

u32 DsIni::ReadBinaryEntry(const ScString &, const ScString &, u8 *, u32) {
  return 0;
}

bool DsIni::SectionExists(const ScStringView &) {
  return false;
}

// Borrow the selected compressed-package path without opening the package or acquiring another reference.
const ScString *DsIni::GetCompressedPath() const {
  return &package_name;
}

bool DsIni::EnumEntriesOld(const ScString &, DsIniCallback, void *) {
  return false;
}

void DsIni::SetCompressedPath(const ScString &input) {
  mode = 0;
  package_name = input;
}

// Read the original length-prefixed string, initializing only the allocation's final terminator.
// Zero-size records reuse prior storage; a short read remains defined only when its supplied prefix contains a NUL.
void DsIni::ReadString(ScString *out) {
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

// Select the lazy package index using the original void InitFileType contract.
// Cache loose mode after an absent package/index entry, or retain the matched section offset and package mode.
void DsIni::InitFileType() {
  if (mode)
    return;
  bool exists = package_name.Strlen() && RsFile::FileExists(package_name);
  if (!exists) {
    mode = 1;
    return;
  }
  package_file.SetName(package_name);
  if (!package_file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    mode = 1;
    return;
  }
  package_offset = 0;
  ScString expected = DsIniPackageKey(name);
  package_file.SeekBegin();
  u32 count = RsReadPackageWord(&package_file), offset = count;
  ScString entry_name;
  for (u32 index = 0; index < count; ++index) {
    if (!DsReadIniText(this, &entry_name)) {
      mode = 1;
      return;
    }
    DsIniUppercase(&entry_name);
    package_file.Read(&offset, 4);
    if (entry_name.Strlen() == expected.Strlen() && !memcmp(entry_name.begin, expected.begin, entry_name.Strlen())) {
      package_offset = offset;
      mode = 2;
      return;
    }
  }
  mode = 1;
}

// Generate the original floating-step path, or report its required length when capacity is zero. Negative
// coordinates retain the source's 100000-unit truncation bias; city Z occupies the common point's Y field.
u32 DsRasterPath(i32 start_x, i32 start_z, i32 end_x, i32 end_z, ScPoint *points, u32 capacity) {
  i32 distance_x = abs(end_x - start_x), distance_z = abs(end_z - start_z);
  u32 count = u32((distance_x >= distance_z ? distance_x : distance_z) + 1);
  if (!capacity)
    return count;
  if (capacity < count)
    count = capacity;
  f64 step_x, step_z;
  if (end_z == start_z) {
    step_x = 1.0;
    step_z = 0.0;
  } else if (end_x == start_x) {
    step_x = 0.0;
    step_z = 1.0;
  } else if (distance_x > distance_z) {
    step_x = 1.0;
    step_z = f64(distance_z) / f64(distance_x);
  } else {
    step_x = f64(distance_x) / f64(distance_z);
    step_z = 1.0;
  }
  if (end_x < start_x)
    step_x = -step_x;
  if (end_z < start_z)
    step_z = -step_z;
  f64 x = f64(start_x) + 0.5, z = f64(start_z) + 0.5;
  for (u32 i = 0; i < count; ++i) {
    points[i].x = x > 0.0 ? i32(x) : i32(x + 100000.0) - kDsNegativeLineCoordinateBias;
    points[i].y = z > 0.0 ? i32(z) : i32(z + 100000.0) - kDsNegativeLineCoordinateBias;
    x += step_x;
    z += step_z;
  }
  return count;
}



// Query the uptime millisecond clock used for the original sentinel seed.
// SDL uses the system counter rather than the elapsed time since this process started.
static u32 DsSeedMilliseconds() {
  return ScSystemMilliseconds();
}

// Seed both independent streams, resolving the all-ones sentinel through one platform clock query. Clear the
// normal-cache flag while leaving the cached double unwritten, matching the original seeding helper's field order.
DsRandom::DsRandom(u32 seed) {
  if (seed == kScRandomSeedFromClock)
    seed = DsSeedMilliseconds();
  state = seed;
  real_state = seed;
  has_normal = 0;
}

// Advance the original multiply/add generator and select its mixed high bits. Large bounds use rejection,
// including the upper endpoint; a zero bound returns without advancing the state.
u32 DsRandom::Next(u32 bound) {
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
    // BSR/SHLD constructs a mask covering all set bits of value, so the AND leaves value unchanged.
    // This also avoids importing the undefined BSR destination when value is zero.
  } while (value > bound);
  state = next;
  return value;
}

i32 DsRandom::Range(i32 minimum, i32 maximum) {
  return i32(u32(minimum) + Next(u32(maximum) - u32(minimum)));
}

// Pick the smaller of two half-range draws, then choose its sign with a third draw. The signed half-width
// truncates toward zero, and the original call order is retained even for a zero-width interval.
i32 DsRandom::Centered(i32 minimum, i32 maximum) {
  i32 half = i32(u32(maximum) - u32(minimum)) / 2;
  i32 first = i32(Next(u32(half))), distance = i32(Next(u32(half)));
  if (first < distance)
    distance = first;
  if (Next(2))
    distance = i32(0u - u32(distance));
  return i32(u32(minimum) + u32(distance) + u32(half));
}

// Advance the independent multiplicative seed used by real draws. A zero seed is replaced before multiplication;
// integer draws and the cached normal sample are unaffected.
f64 DsRandom::Unit() {
  if (!real_state)
    real_state = kScRandomFallbackRealSeed;
  real_state *= kScRandomRealMultiplier;
  return f64(real_state) * 2.3283064365387e-10;
}

f64 DsRandom::RealRange(f64 minimum, f64 maximum) {
  return Unit() * (maximum - minimum) + minimum;
}

// Return a saved normal draw without advancing the stream, or generate a pair by polar rejection sampling.
// The first generated component is cached while the second is returned; both use the independent real seed.
f64 DsRandom::Gaussian() {
  if (has_normal) {
    has_normal = 0;
    return normal;
  }
  f64 first, second, radius;
  do {
    first = Unit() * 2.0 - 1.0;
    second = Unit() * 2.0 - 1.0;
    radius = second * second + first * first;
  } while (radius >= 1.0 || radius == 0.0);
  f64 factor = sqrt(log(radius) * -2.0 / radius);
  has_normal = 1;
  normal = factor * first;
  return factor * second;
}

// Map a normal draw into the supplied interval after the original six-sigma shift. The upper clamp is 11.99,
// preserving the source's deliberate gap below the upper endpoint rather than clamping to twelve.
f64 DsRandom::NormalRange(f64 minimum, f64 maximum) {
  f64 value = Gaussian() + 6.0;
  if (value >= 12.0)
    value = 11.99;
  if (value < 0.0)
    value = 0.0;
  return (maximum - minimum) * (value * 0.08333333333333333) + minimum;
}

// Transform one real draw into a symmetric triangular distribution before interval scaling. The branch at
// one half and its subtraction/addition order retain the original floating-point evaluation.
f64 DsRandom::TriangularReal(f64 minimum, f64 maximum) {
  f64 value = Unit(), centered;
  if (value > 0.5)
    centered = 1.0 - sqrt((1.0 - value) + (1.0 - value));
  else
    centered = sqrt(value + value) - 1.0;
  return (centered + 1.0) * 0.5 * (maximum - minimum) + minimum;
}

f64 DsRandom::InverseSqrtRange(f64 minimum, f64 maximum) {
  return (1.0 - sqrt(Unit())) * (maximum - minimum) + minimum;
}

// Draw from the real stream and map its triangular distribution through the original wrapped signed interval width.
// Truncate through the CRT's wide integer carrier before retaining the DWORD consumed by the game's callers.
i32 DsRandom::Triangular(i32 minimum, i32 maximum) {
  f64 value = Unit(), centered;
  if (value > 0.5)
    centered = 1.0 - sqrt((1.0 - value) + (1.0 - value));
  else
    centered = sqrt(value + value) - 1.0;
  f64 result = (centered + 1.0) * 0.5 * i32(u32(maximum) - u32(minimum)) + minimum;
  return i32(u32(i64(result)));
}

// Bias one independent real draw toward the upper endpoint and truncate through the original wide CRT carrier.
// The interval subtraction wraps as a DWORD before its signed conversion to floating point.
i32 DsRandom::SqrtRange(i32 minimum, i32 maximum) {
  f64 value = Unit();
  f64 result = sqrt(value) * i32(u32(maximum) - u32(minimum)) + minimum;
  return i32(u32(i64(result)));
}

DsRecord::DsRecord(RsDatabaseSegment *input, RsKey key) : aborted(false), record(nullptr), segment(input) {
  if (segment && !segment->OpenRecord(key, &record))
    record = nullptr;
}

DsRecord::DsRecord(RsDatabaseSegment *input, RsKey key, u32) : aborted(false), record(nullptr), segment(input) {
  if (segment && !segment->OpenRecordForWrite(key, &record))
    record = nullptr;
}

// Abort only when closing failed and left a record present; successful closure never triggers an abort.
DsRecord::~DsRecord() {
  if (record) {
    if (!aborted)
      aborted = !segment->CloseRecord(&record);
    if (record && aborted)
      segment->AbortRecord(&record);
  }
}

bool DsRecord::IsValid() {
  return record != nullptr;
}

// Open the record through the original base, then acquire its single canonical stream identity.
// A refused query clears the stream output while preserving the record for normal close/abort cleanup.
DsStreamRecord::DsStreamRecord(RsDatabaseSegment *segment, RsKey key)
    : DsRecord(segment, key), stream_interface(nullptr) {
  if (DsRecord::IsValid() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

// Open through the write-mode base overload and acquire the same single canonical stream identity. A refused query
// clears its output while retaining the record for ordinary close/abort cleanup.
DsStreamRecord::DsStreamRecord(RsDatabaseSegment *segment, RsKey key, u32 mode)
    : DsRecord(segment, key, mode), stream_interface(nullptr) {
  if (DsRecord::IsValid() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

// Release the stream while its pointer is still published, then clear it before base record cleanup begins.
// Native destruction supplies the original virtual dispatch and inherited close/abort ordering.
DsStreamRecord::~DsStreamRecord() {
  if (stream_interface) {
    static_cast<RsStream *>(stream_interface)->Release();
    stream_interface = nullptr;
  }
}

bool DsStreamRecord::IsValid() {
  return DsRecord::IsValid() && stream_interface != nullptr;
}

// Return the borrowed queried stream without acquiring or releasing another interface reference.
RsStream *DsStreamRecord::Stream() {
  return static_cast<RsStream *>(stream_interface);
}

static ScMessageServer *DsMessages;
static ScLanguageManager *DsLanguages;
static ScVariableManager *DsProperties;
static RsManager *DsResources;
static GxGraphics *DsGraphics;
static WnWindowManager *DsWindows;
static bool DsApplicationQueried;
// The actual QueryInterface ABI writes this persistent output directly, including during reentrant lookup.
static void *DsApplicationInterface;

// Borrow the module's message server, retrying a missing service on later calls. Publish the queried pointer
// before releasing its temporary reference, preserving reentrant lookups during that release.
ScMessageServer *DsGetMessageServer() {
  if (!DsMessages) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &value);
    DsMessages = static_cast<ScMessageServer *>(value);
    if (value)
      static_cast<ScMessageServer *>(value)->Release();
  }
  return DsMessages;
}

// Borrow the application's actual agent-type tree when the module application lookup is available.
// A missing application returns null exactly as in the installed helper.
CtAgentTree *DsGetAgentTree() {
  ApGameInterface *application = DsGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

static void DsApplicationExit() {
}

// Query the framework application's game interface once, setting the guard before any external calls. The
// persistent QueryInterface output makes reentrant lookups observe its publication immediately; failures stay cached.
ApGameInterface *DsGetApplication() {
  if (!DsApplicationQueried) {
    DsApplicationQueried = true;
    DsFrameWork()->Application()->QueryInterface(IID_ApGameInterface, &DsApplicationInterface);
    atexit(DsApplicationExit);
  }
  return static_cast<ApGameInterface *>(DsApplicationInterface);
}

// Borrow the real framework through the module director's original interface operation.
ScFramework *DsFrameWork() {
  return DsGetDirector()->FrameWork();
}

// Borrow the module resource manager, retrying unavailable lookups. Publish the returned interface before
// releasing its acquisition reference so reentrant access sees the same borrowed manager.
RsManager *DsGetResourceManager() {
  if (!DsResources) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kRsManagerServiceId, IID_RsManager, &value);
    DsResources = static_cast<RsManager *>(value);
    if (value)
      static_cast<RsManager *>(value)->Release();
  }
  return DsResources;
}

// Borrow the module director's actual COM root on every call, preserving a missing root without acquiring it.
ScCom *DsGetCom() {
  return DsGetDirector()->GZCOM();
}

// Borrow the application's city window on each call, preserving a missing application without a window lookup.
// The window belongs to the application and does not acquire a reference through this helper.
SpCityWindowView *DsGetCityWindow() {
  ApGameInterface *application = DsGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the current city view from the live window on each call, returning null when the window is absent.
// Re-querying the window preserves changes to the active view without a separate module cache.
SpViewInterface *DsGetCityView() {
  SpCityWindowView *window = DsGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the application's actual BoxX interface through the established singleton lookup.
// A missing application returns null without requesting another service.
ApBoxX *DsGetBoxX() {
  ApGameInterface *application = DsGetApplication();
  return application ? application->GetBoxX() : nullptr;
}

// Borrow the current city through the module's application singleton, preserving a missing application as null.
CtCity *DsGetCity() {
  ApGameInterface *application = DsGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow application preferences on each call, leaving ownership with the application and returning null without it.
ApPreferences *DsGetPreferences() {
  ApGameInterface *application = DsGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Publish the borrowed graphics service before releasing its lookup hold, retrying later while no service is available.
GxGraphics *DsGetGraphics() {
  if (!DsGraphics) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    DsGraphics = static_cast<GxGraphics *>(value);
    if (value)
      static_cast<GxGraphics *>(value)->Release();
  }
  return DsGraphics;
}

// Cache the borrowed window manager before dropping its temporary reference, preserving reentrant service lookups.
WnWindowManager *DsGetWindowManager() {
  if (!DsWindows) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &value);
    DsWindows = static_cast<WnWindowManager *>(value);
    if (value)
      static_cast<WnWindowManager *>(value)->Release();
  }
  return DsWindows;
}

// Borrow the isometric view's actual sprite cell map; a missing current view returns null without a map lookup.
SpCellMap *DsGetCellMap() {
  SpViewInterface *view = DsGetCityView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteCellMap() : nullptr;
}

// Push the requested automatic cursor through the current manager, retaining only its acceptance byte for scope exit.
DsCursorScope::DsCursorScope(u32 cursor) : acquired(false) {
  WnWindowManager *manager = DsGetWindowManager();
  if (manager) {
    WnCursorManager *cursors = manager->GetCursorManager();
    if (cursors)
      acquired = cursors->PushAutoCursor(cursor);
  }
}

// Pop an accepted cursor through newly fetched managers, preserving the original missing-manager gates at scope exit.
DsCursorScope::~DsCursorScope() {
  if (acquired) {
    WnWindowManager *manager = DsGetWindowManager();
    if (manager) {
      WnCursorManager *cursors = manager->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
}

// Cache the borrowed language manager after a temporary service acquisition, retrying while no manager is available.
// Publish before releasing the acquisition reference so reentrant access observes the original cached identity.
ScLanguageManager *DsGetLanguageManager() {
  if (!DsLanguages) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &value);
    DsLanguages = static_cast<ScLanguageManager *>(value);
    if (value)
      static_cast<ScLanguageManager *>(value)->Release();
  }
  return DsLanguages;
}

// Borrow the actual variable manager, retrying unavailable lookups through the original service and interface IDs.
// Publish the cache before releasing the temporary acquisition so reentrant lookup observes the same identity.
ScVariableManager *DsGetPropertyProvider() {
  if (!DsProperties) {
    void *value = nullptr;
    DsFrameWork()->GetSystemService(kScVariableManagerServiceId, IID_ScVariableManager, &value);
    DsProperties = static_cast<ScVariableManager *>(value);
    if (value)
      DsProperties->Release();
  }
  return DsProperties;
}

// Query the real city's terrain at signed fixed coordinates divided toward zero. Return the altitude byte in
// the same 24.8 fixed units as the moving-disaster position triples.
i32 DsFixedAltitude(CtCity *city, i32 x, i32 z) {
  ScDirtBag *terrain = city->DirtBag();
  u8 altitude;
  terrain->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
  return i32(altitude) << 8;
}

// Apply the original wrapped height delta, lower clamp and then upper clamp to dirt altitude. Reject heights at or
// below the supplied sea level; otherwise forward only the genuine altitude byte and return the setter's result.
u8 SC3_STDCALL DsAdjustDirtAltitude(
    u32 x, u32 z, i32 delta, i32 minimum, i32 maximum, i32 sea_level, ScDirtBag *terrain) {
  u8 altitude;
  terrain->GetVertexAltitudeDirt(x, z, &altitude);
  i32 height = i32(u32(altitude) + u32(delta));
  if (height < minimum)
    height = minimum;
  if (height > maximum)
    height = maximum;
  if (height <= sea_level)
    return 0;
  return terrain->SetVertexAltitude(x, z, u8(height));
}

u32 DsImpactMaximumCornerDifference = 10;

// Scan the inclusive rectangle with X outside Z, returning immediately at the first water cell.
u8 DsContainsWater(ScDirtBag *terrain, const ScRect *bounds) {
  for (i32 x = bounds->left; x <= bounds->right; x = i32(u32(x) + 1))
    for (i32 z = bounds->top; z <= bounds->bottom; z = i32(u32(z) + 1))
      if (terrain->IsWater(u32(x), u32(z)))
        return 1;
  return 0;
}

// Sample the corners clockwise from the upper left before testing the two opposite diagonal differences.
// Both absolute byte-height differences must fit the live unsigned threshold; edges and interior are not sampled.
bool DsHasLevelCorners(ScDirtBag *terrain, const ScRect *bounds) {
  u8 first, second, third, fourth;
  terrain->GetVertexAltitude(u32(bounds->left), u32(bounds->top), &first);
  terrain->GetVertexAltitude(u32(bounds->right), u32(bounds->top), &second);
  terrain->GetVertexAltitude(u32(bounds->right), u32(bounds->bottom), &third);
  terrain->GetVertexAltitude(u32(bounds->left), u32(bounds->bottom), &fourth);
  i32 diagonal_one = i32(first) - i32(third), diagonal_two = i32(second) - i32(fourth);
  if (diagonal_one < 0)
    diagonal_one = -diagonal_one;
  if (diagonal_two < 0)
    diagonal_two = -diagonal_two;
  return u32(diagonal_one) <= DsImpactMaximumCornerDifference && u32(diagonal_two) <= DsImpactMaximumCornerDifference;
}

// Scan real-water runs in row order, shrinking their spans down successive rows and reserving accepted rectangles.
// Bounds limit origins only; each rectangle can grow to the map edge, and its two dimensions may exchange roles.
u8 DsFindWaterRectangles(
    ScDirtBag *terrain, ScArray<GeCellRect> *rectangles, u32 first, u32 second, const GeCellRect *bounds) {
  u32 minimum = first < second ? first : second, maximum = first < second ? second : first;
  u32 width = terrain->CellCountX(), height = terrain->CellCountZ();
  u32 start_x = bounds ? bounds->left : 0, start_z = bounds ? bounds->top : 0;
  u32 end_x = bounds ? bounds->right : width - minimum + 1;
  u32 end_z = bounds ? bounds->bottom : height - minimum + 1;
  rectangles->Clear();
  // Original clearing covers only [start_x,width) x [start_x,height), using X for both lower bounds.
  // Initialize native storage completely so uncovered original stack bytes never become game inputs.
  u8 used[256][256] = {};
  for (u32 z = start_z; z < end_z; ++z) {
    for (u32 x = start_x; x < end_x; ++x) {
      if (used[x][z] || !terrain->IsRealWater(x, z))
        continue;
      u32 left = x, right = x;
      while (right < width && !used[right][z] && terrain->IsRealWater(right, z))
        ++right;
      if (right - x < minimum)
        continue;
      u32 previous_left = left, previous_right = right, bottom = height;
      for (u32 row = z; row < height; ++row) {
        bool found = false;
        for (u32 column = left; column < right; ++column) {
          if (terrain->IsRealWater(column, row) && !used[column][row])
            found = true;
          else if (!found)
            ++left;
          else {
            right = column;
            break;
          }
        }
        if (right - left < minimum) {
          left = previous_left;
          right = previous_right;
          bottom = row;
          break;
        }
        previous_left = left;
        previous_right = right;
      }
      u32 span_x = right - left, span_z = bottom - z;
      u32 smaller = span_x < span_z ? span_x : span_z, larger = span_x < span_z ? span_z : span_x;
      if (smaller < minimum || larger < maximum)
        continue;
      for (u32 column = left; column < right; ++column)
        for (u32 row = z; row < bottom; ++row)
          used[column][row] = 1;
      *rectangles->Append() = {left, z, right, bottom};
    }
  }
  return rectangles->size != 0;
}

DsTextResource::DsTextResource(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

DsTextResource::~DsTextResource() {
  ScRelease(&text);
}

// Query directly into the original retained field. A refused lookup discards any identity written by the provider.
bool DsTextResource::Resolve() {
  bool result = DsGetResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text));
  if (!result)
    text = nullptr;
  return result;
}

// Copy resolved text through its genuine interface, or return an independently owned empty String when unavailable.
ScString DsTextResource::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Expand a copy of the retained resource and publish its entire live range, leaving the output untouched when missing.
u8 DsTextResource::CopyTo(ScStringView *out) {
  if (!text)
    return 0;
  ScString expanded(*text);
  DsExpandTextTokens(&expanded);
  out->FromChar(expanded.Data(), expanded.Strlen());
  return 1;
}

static bool DsTokenEquals(const ScString &token, const char *name, u32 length) {
  return u32(token.end - token.begin) == length && !memcmp(token.begin, name, length);
}

// The original substring is empty at or beyond the end, so oversized positions simply fail to match a keyword.
u8 SC3_STDCALL DsTryTextToken(ScString *text, const ScString &token, u32 *position) {
  ScString candidate = text->Mid(*position, token.Strlen());
  if (u32(candidate.end - candidate.begin) != u32(token.end - token.begin) ||
      memcmp(candidate.begin, token.begin, usize(candidate.end - candidate.begin)))
    return 0;
  if (DsTokenEquals(token, "%MAYOR%", 7) || DsTokenEquals(token, "%YOURNAME%", 10)) {
    CtCity *city = DsGetCity();
    if (city) {
      ScStringView *sizeName = city->GetMayorName(), *dataName = city->GetMayorName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetMayorName()->Strlen() - 1;
    }
    return 1;
  }
  if (DsTokenEquals(token, "%CITYNAME%", 10) || DsTokenEquals(token, "%YOURCITY%", 10)) {
    CtCity *city = DsGetCity();
    if (city) {
      ScStringView *sizeName = city->GetCityName(), *dataName = city->GetCityName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetCityName()->Strlen() - 1;
    }
    return 1;
  }
  if (DsTokenEquals(token, "%POPULATION%", 12)) {
    ScString fallback("0");
    CtCity *city = DsGetCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", population);
      number.FromChar(buffer, u32(size));
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, u32(number.end - number.begin));
      *position += u32(number.end - number.begin) - 1;
    } else {
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, u32(fallback.end - fallback.begin));
      *position += u32(fallback.end - fallback.begin) - 1;
    }
    return 1;
  }
  if (DsTokenEquals(token, "%YEAR%", 6)) {
    CtCity *city = DsGetCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", i32(year));
      number.FromChar(buffer, u32(size));
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, u32(number.end - number.begin));
      *position += u32(number.end - number.begin) - 1;
    } else {
      ScString fallback("1900");
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, u32(fallback.end - fallback.begin));
      *position += u32(fallback.end - fallback.begin) - 1;
    }
    return 1;
  }
  if (DsTokenEquals(token, "%ANYNEIGHBOR%", 13)) {
    CtCity *city = DsGetCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return 0;
    DsRandom random(kScRandomSeedFromClock);
    ScString name;
    neighbors->GetNeighborName(u8(random.Next(4)), &name);
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, u32(name.end - name.begin));
    *position += u32(name.end - name.begin) - 1;
    return 1;
  }
  if (DsTokenEquals(token, "%PARADENAME%", 12)) {
    ScString name;
    i32 country = DsGetPreferences()->GetCountry();
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
      DsTextResource resource(instance, kRsSupplementalGameTextGroup);
      name = resource.GetText();
    }
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, u32(name.end - name.begin));
    *position += u32(name.end - name.begin) - 1;
  }
  return 1;
}

static const ScString DsTokenNames[8] = {ScString("%MAYOR%", 7), ScString("%YOURNAME%", 10), ScString("%CITYNAME%", 10),
    ScString("%YOURCITY%", 10), ScString("%POPULATION%", 12), ScString("%YEAR%", 6), ScString("%PARADENAME%", 12),
    ScString("%ANYNEIGHBOR%", 13)};

static u32 DsFindPercent(const ScString &text, u32 start) {
  u32 length = u32(text.end - text.begin);
  for (u32 i = start; i < length; ++i)
    if (text.begin[i] == '%')
      return i;
  return UINT32_MAX;
}

// Scan live text and resume after the resolver's mutable position, preserving unsigned wrap for empty values.
u32 SC3_STDCALL DsExpandTextTokens(ScString *text) {
  u32 position = DsFindPercent(*text, 0);
  while (position < u32(text->end - text->begin)) {
    DsSubstituteTextToken(text, &position);
    position = DsFindPercent(*text, position + 1);
  }
  return position;
}

// Try the eight original names with short-circuiting, placing neighbor before parade despite static construction order.
bool SC3_STDCALL DsSubstituteTextToken(ScString *text, u32 *position) {
  return DsTryTextToken(text, DsTokenNames[0], position) || DsTryTextToken(text, DsTokenNames[1], position) ||
      DsTryTextToken(text, DsTokenNames[2], position) || DsTryTextToken(text, DsTokenNames[3], position) ||
      DsTryTextToken(text, DsTokenNames[4], position) || DsTryTextToken(text, DsTokenNames[5], position) ||
      DsTryTextToken(text, DsTokenNames[7], position) || DsTryTextToken(text, DsTokenNames[6], position);
}

DsTileScan::DsTileScan() : left(-INT32_MAX), top(-INT32_MAX), right(INT32_MAX), bottom(INT32_MAX), orientation(1) {
  Reset(0, 0);
}

// Reset the current point and first horizontal corner according to orientation, clearing completion state. An origin
// outside the inclusive bounds exhausts immediately; preserve its original X word in the result.
i32 DsTileScan::Reset(i32 origin_x, i32 origin_z) {
  x = origin_x;
  z = origin_z;
  target_z = origin_z;
  if (orientation == 1) {
    target_x = i32(u32(origin_x) + 1u);
    step = 1;
  } else {
    step = -1;
    target_x = i32(u32(origin_x) - 1u);
  }
  horizontal = true;
  edges = 0;
  state = 0;
  if (origin_x < left || origin_z < top || origin_x > right || origin_z > bottom) {
    state = 2;
    edges = 15;
  }
  return origin_x;
}

// Return the origin first, then step along each spiral leg and skip legs outside completed edges.
// Exhaustion leaves both outputs untouched; successful writes occur in X/Z order, including aliased outputs.
bool DsTileScan::Next(i32 *output_x, i32 *output_z) {
  if (state == 2)
    return false;
  if (!state) {
    state = 1;
  } else {
    bool corner;
    if (horizontal) {
      if (step > 0) {
        x = i32(u32(x) + 1u);
        if (x > right)
          x = target_x;
      } else {
        x = i32(u32(x) - 1u);
        if (x < left)
          x = target_x;
      }
      corner = x == target_x;
    } else {
      if (step > 0) {
        z = i32(u32(z) + 1u);
        if (z > bottom)
          z = target_z;
      } else {
        z = i32(u32(z) - 1u);
        if (z < top)
          z = target_z;
      }
      corner = z == target_z;
    }
    if (corner)
      AdvanceLeg();
  }
  if (edges == 15) {
    state = 2;
    return false;
  }
  *output_x = x;
  *output_z = z;
  return true;
}

// Mark completed edges while skipping outer legs, then clip the next leg's starting coordinate into the bounds. Return
// the observed byte0 at full completion and1 when a usable leg remains.
u8 DsTileScan::AdvanceLeg() {
  while (edges != 15) {
    if (horizontal) {
      if (step > 0) {
        if (x <= right) {
          Turn();
          if (z < top)
            z = top;
          return 1;
        }
        edges |= 1;
      } else {
        if (x >= left) {
          Turn();
          if (z > bottom)
            z = bottom;
          return 1;
        }
        edges |= 2;
      }
    } else if (step > 0) {
      if (z <= bottom) {
        Turn();
        if (x > right)
          x = right;
        return 1;
      }
      edges |= 4;
    } else {
      if (z >= top) {
        Turn();
        if (x < left)
          x = left;
        return 1;
      }
      edges |= 8;
    }
    Turn();
    x = target_x;
    z = target_z;
  }
  return 0;
}

// Turn between horizontal and vertical legs, reversing direction on the orientation's alternating turn. Extend every
// new horizontal leg and preserve the observed target-Z result.
i32 DsTileScan::Turn() {
  if (horizontal) {
    horizontal = false;
    target_x = x;
    if (orientation == 0)
      step = i32(0u - u32(step));
    target_z = i32(u32(z) + u32(step));
  } else {
    horizontal = true;
    if (orientation == 1)
      step = i32(0u - u32(step));
    step = i32(u32(step) + (step > 0 ? 1u : kScBackwardGridStep));
    target_x = i32(u32(x) + u32(step));
    target_z = z;
  }
  return target_z;
}
