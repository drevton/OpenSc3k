#include "resources/manager_object.h"

#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "resources/api.h"
#include "resources/factory.h"
#include "resources/file.h"
#include "resources/module_object.h"
#include "resources/resource.h"

// Directory key allocation stops at the all-ones instance candidate without assigning it.
enum RsDirectoryKeyLimit : u32 {
  kRsDirectoryInstanceLimit = 0xffffffff,
};

// The manager recognizes redirect records and loads their target through the retained resource factory.
enum RsRedirectType : u32 {
  kRsRedirectRecordType = 0x605a0adc,
};

// The service constructor supplies this original startup ordering priority.
enum RsResourceManagerOrder : i32 {
  kRsResourceManagerPriority = 0xf4240,
};

static ScString RsDirectoryNativePath(const ScString &path) {
  return ScNativeFilePath(path.Data());
}

// Share the game's all-files and extensionless DOS wildcard handling with the common directory implementation.
static char **RsDirectoryGlob(const ScString &path, const ScString &filter, int *count) {
  return ScNativeDirectoryGlob(path.Data(), filter.Data(), count);
}

// Query one enumerated entry's kind through its actual native full path.
static bool RsDirectoryIsFolder(const ScString &path, const char *name) {
  ScString native = RsDirectoryNativePath(path);
  if (native.begin != native.end && native.end[-1] != '/')
    native.Append("/");
  native.Append(name);
  SDL_PathInfo info;
  return SDL_GetPathInfo(native.Data(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

// Replace both lists with logical drive roots and their corresponding drive types in Windows enumeration order.
// Normalize the leading drive letter to lowercase before querying its type.
bool RsDirectoryListing::ReadAvailableDrivesIntoStringList(std::list<ScString> *names, std::list<DriveType> *types) {
  names->clear();
  types->clear();
#ifdef _WIN32
  unsigned long drives = _getdrives();
  for (u32 drive = 0; drive < 26; ++drive)
    if (drives & (1ul << drive)) {
      char root[] = "a:/";
      root[0] = char('a' + drive);
      names->emplace_back(root);
      types->push_back(DriveType::Unknown);
    }
  return drives != 0;
#else
  SDL_PathInfo info;
  if (!SDL_GetPathInfo("/", &info) || info.type != SDL_PATHTYPE_DIRECTORY)
    return false;
  names->emplace_back("/");
  types->push_back(DriveType::Unknown);
  return true;
#endif
}

// Copy the concrete String range without invoking its overridden interface getters, then create the default wildcard.
// Append a missing separator and enumerate only when the original Boolean flag requests it.
RsDirectoryListing::RsDirectoryListing(const ScString &name, bool refresh, bool parent, bool current)
    : path(name), wildcard("*.*"), includeParent(parent), includeCurrent(current) {
  EnsureSeparator();
  if (refresh)
    Refresh();
}

// Leave empty paths and existing slash/backslash endings untouched; append one backslash to other concrete paths.
void RsDirectoryListing::EnsureSeparator() {
  u32 length = path.ScString::Strlen();
  if (length && path.begin[length - 1] != '\\' && path.begin[length - 1] != '/')
    path.Append("\\");
}


// Empty listings return false without reading the input range; no virtual String getter is called.
bool RsDirectoryListing::ContainsEntry(const ScString &name) {
  for (const auto &entry : entries) {
    u32 length = entry.name.ScString::Strlen();
    if (length == name.ScString::Strlen() && !memcmp(entry.name.begin, name.begin, length))
      return true;
  }
  return false;
}

// Replace the list in platform enumeration order, tagging directories and filtering the first dot/parent entries.
// Requested missing dot entries are inserted at the front only for the default wildcard, preserving source order.
bool RsDirectoryListing::Refresh() {
  entries.clear();
  int count = 0;
  char **names = RsDirectoryGlob(path, wildcard, &count);
  if (!names)
    return false;
  bool all = !strcmp(wildcard.Data(), "*.*");
  if (!count && !all) {
    SDL_free(names);
    return false;
  }
  for (int i = 0; i < count; ++i) {
    ScString name(names[i]);
    entries.emplace_back(this, &name, RsDirectoryIsFolder(path, names[i]) ? 4 : 8);
  }
  SDL_free(names);
  if (all && includeParent) {
    ScString name("..");
    entries.emplace_front(this, &name, 4);
  }
  if (all && includeCurrent) {
    ScString name(".");
    entries.emplace_front(this, &name, 4);
  }
  return true;
}

// Count the actual linked entries in order; this source operation does not trigger a refresh.
u32 RsDirectoryListing::Size() {
  u32 count = 0;
  for (auto it = entries.begin(); it != entries.end(); ++it)
    ++count;
  return count;
}

// Return a borrowed indexed entry after a linear walk, or null when the index is beyond the stored list.
RsDirectoryEntry *RsDirectoryListing::GetEntry(u32 index) {
  u32 count = 0, size = Size();
  if (!size)
    return nullptr;
  for (auto &entry : entries) {
    if (count == index)
      return &entry;
    if (++count >= size)
      return nullptr;
  }
  return nullptr;
}

// Find the last dot across the complete owned filename range, without interpreting directory separators.
static u32 RsDirectoryLastDot(const ScString &name) {
  for (u32 i = name.ScString::Strlen(); i; --i)
    if (name.begin[i - 1] == '.')
      return i - 1;
  return kScStringNotFound;
}

// Assign bytes following the last dot into the concrete output String, bypassing its virtual interface methods.
// A missing dot erases the output's backing range and reports zero without replacing its String identity.
u32 RsDirectoryEntry::GetExtension(ScString *out) {
  u32 dot = RsDirectoryLastDot(name), length = name.ScString::Strlen();
  if (dot >= length) {
    out->Erase(0, out->ScString::Strlen());
    return 0;
  }
  out->ScString::FromChar(name.begin + dot + 1, length - dot - 1);
  return out->ScString::Strlen();
}

// Assign the concrete substring before the last dot, or use the original concrete String assignment when no dot exists.
// Report the output's live length without dispatching an overridden String-interface getter.
u32 RsDirectoryEntry::GetStem(ScString *out) {
  u32 dot = RsDirectoryLastDot(name);
  if (dot < name.ScString::Strlen())
    out->ScString::FromChar(name.begin, dot);
  else
    *out = name;
  return out->ScString::Strlen();
}

RsDirectoryEntry::RsDirectoryEntry() {
  owner = nullptr;
}

bool RsDirectoryEntry::IsMediaRemovable() {
  return false;
}

u32 RsDirectoryEntry::GetSize() {
  return 0;
}

u32 RsDirectoryEntry::GetCreationDate() {
  return 0;
}

u32 RsDirectoryEntry::GetLastModificationDate() {
  return 0;
}

u32 RsDirectoryEntry::GetSerialNumber() {
  return 0;
}

// Replace the concrete output with the owner's path, or erase it when detached, then append the live filename range.
// Re-read the filename after replacement so defined output aliases retain the original ordering.
void RsDirectoryEntry::GetFullPath(ScString *out) {
  if (owner)
    *out = owner->path;
  else
    out->Erase(0, out->ScString::Strlen());
  out->Append(name.begin, static_cast<u32>(name.end - name.begin));
}

bool RsDirectoryEntry::CreatePhysicalEntry() {
  return false;
}

// Original executable recursion depth is shared by directory searches in this module, rather than per receiver.
static u32 RsDirectoryDepth;


// Replace the concrete path, normalize its trailing separator and optionally refresh the owned entry cache.
bool RsDirectoryListing::ChangeDirectory(const ScString &directory, bool refresh) {
  path = directory;
  EnsureSeparator();
  if (refresh)
    Refresh();
  return true;
}

// Copy the concrete entry filter, replace an empty filter with the original default and optionally refresh the cache.
void RsDirectoryListing::SetNewDirectoryEntryFilter(const ScString &filter, bool refresh) {
  wildcard = filter;
  if (wildcard.begin == wildcard.end)
    wildcard = ScString("*.*");
  if (refresh)
    Refresh();
}

// Append requested file/directory names in platform order without clearing the caller's list or altering the cache.
// Dot entries use flags1/2, directories4 and files8; absent requested dots are prepended only for the default filter.
bool RsDirectoryListing::ReadDirectorySpecificEntriesIntoStringList(std::list<ScString> *out, u32 flags) {
  int count = 0;
  char **names = RsDirectoryGlob(path, wildcard, &count);
  if (!names)
    return false;
  bool all = !strcmp(wildcard.Data(), "*.*");
  if (!count && !all) {
    SDL_free(names);
    return false;
  }
  for (int i = 0; i < count; ++i)
    if (flags & (RsDirectoryIsFolder(path, names[i]) ? 4 : 8))
      out->emplace_back(names[i]);
  SDL_free(names);
  if (all && (flags & 2))
    out->emplace_front("..");
  if (all && (flags & 1))
    out->emplace_front(".");
  return true;
}

bool RsDirectoryListing::ReadDirectoryFileEntriesIntoStringList(std::list<ScString> *out) {
  return ReadDirectorySpecificEntriesIntoStringList(out, 8);
}

bool RsDirectoryListing::ReadDirectoryDirectoryEntriesIntoStringList(std::list<ScString> *out, u32 flags) {
  return ReadDirectorySpecificEntriesIntoStringList(out, (flags & 3) | 4);
}

// Append matching full filenames depth first, preserving the shared depth counter and the live receiver's path changes.
// Enumerate child directories with the default filter, restore the search filter before recursing and always return true.
bool RsDirectoryListing::ReadDirectorySpecificEntriesIntoStringListRecursive(
    ScString *directory, std::list<ScString> *out, bool first) {
  std::list<ScString> directories, files;
  ScString next, savedFilter;
  ++RsDirectoryDepth;
  ChangeDirectory(*directory, true);
  if (first || RsDirectoryDepth > 1) {
    ReadDirectoryFileEntriesIntoStringList(&files);
    for (const ScString &name : files) {
      ScString joined(*directory);
      joined.Append(name.begin, name.ScString::Strlen());
      out->push_back(joined);
    }
  }
  savedFilter = wildcard;
  wildcard = ScString("*.*");
  ReadDirectoryDirectoryEntriesIntoStringList(&directories, 0);
  wildcard = savedFilter;
  for (const ScString &name : directories) {
    {
      ScString joined(*directory);
      joined.Append(name.begin, name.ScString::Strlen());
      next = joined;
    }
    next.Append("\\");
    ReadDirectorySpecificEntriesIntoStringListRecursive(&next, out, first);
  }
  --RsDirectoryDepth;
  return true;
}


RsDirectoryEntry::RsDirectoryEntry(RsDirectoryListing *directory, ScString *filename, i32 type)
    : owner(directory), name(*filename), kind(type) {
}

RsDirectoryEntry::RsDirectoryEntry(const RsDirectoryEntry &source)
    : owner(source.owner), name(source.name), kind(source.kind) {
}

// Return the live concrete filename buffer without virtual String queries or a temporary allocation.
RsDirectoryEntry::operator const char *() const {
  return name.begin;
}

// Require the same owner and kind before comparing every concrete filename byte, including embedded zeros.
int operator==(const RsDirectoryEntry &left, const RsDirectoryEntry &right) {
  if (left.owner != right.owner || left.kind != right.kind)
    return 0;
  u32 length = left.name.ScString::Strlen();
  return length == right.name.ScString::Strlen() && !memcmp(left.name.begin, right.name.begin, length);
}

// Order entries by the complete concrete filename range alone, ignoring their owner and file/directory kind.
int operator<(const RsDirectoryEntry &left, const RsDirectoryEntry &right) {
  u32 first = left.name.ScString::Strlen(), second = right.name.ScString::Strlen();
  i32 order = memcmp(left.name.begin, right.name.begin, first < second ? first : second);
  return order < 0 || (!order && first < second);
}

ScString RsDirectoryListing::GetPath() {
  return ScString(path);
}

bool RsDirectoryListing::IsDirectoryRoot() {
  return path.ScString::Strlen() == 1 && (path.begin[0] == '/' || path.begin[0] == '\\');
}

// Borrow the directory's current concrete character buffer without refreshing its entries.
RsDirectoryListing::operator const char *() const {
  return path.begin;
}

// Copy the directory, add a missing separator and search its original single-star wildcard.
// This deliberately retains wildcard-search behavior rather than replacing it with an attribute test.
bool RsDirectoryListing::DoesDirectoryExist(const ScString &directory) {
  ScString search(directory);
  u32 length = search.ScString::Strlen();
  if (!length || (search.begin[length - 1] != '\\' && search.begin[length - 1] != '/'))
    search.Append("\\");
  search.Append("*");
  return DoesAnyEntryExistThatMatchesPattern(search);
}

// Test the supplied concrete wildcard with one platform enumeration and close a successful search handle.
bool RsDirectoryListing::DoesAnyEntryExistThatMatchesPattern(const ScString &pattern) {
  ScString native = RsDirectoryNativePath(pattern);
  const char *separator = strrchr(native.Data(), '/');
  ScString directory, filter;
  if (separator) {
    directory.FromChar(native.Data(), u32(separator - native.Data()) + 1);
    filter.FromChar(separator + 1);
  } else {
    directory.FromChar(".");
    filter = native;
  }
  int count = 0;
  char **names = RsDirectoryGlob(directory, filter, &count);
  if (!names)
    return false;
  bool found = count != 0 || !strcmp(filter.Data(), "*") || !strcmp(filter.Data(), "*.*");
  SDL_free(names);
  return found;
}

// Try the directory itself first, then recursively remove its child directories before deleting its files.
// Preserve the original refusal for missing/write-protected paths and stop at the first failed child removal.
bool RsDirectoryListing::RemoveDirectoryA(const ScString &directory) {
  ScString native = RsDirectoryNativePath(directory);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(native.Data(), &info) || info.type != SDL_PATHTYPE_DIRECTORY)
    return false;
  if (ScRemoveNativeDirectory(native.Data()))
    return true;
  RsDirectoryListing listing(directory, false, false, false);
  {
    std::list<ScString> names;
    if (listing.ReadDirectoryDirectoryEntriesIntoStringList(&names, 0))
      for (const ScString &name : names) {
        ScString child(listing.path);
        child.Append(name.Data(), name.Strlen());
        if (!RemoveDirectoryA(child))
          return false;
      }
  }
  {
    std::list<ScString> names;
    if (listing.ReadDirectoryFileEntriesIntoStringList(&names))
      for (const ScString &name : names) {
        ScString child(listing.path);
        child.Append(name.Data(), name.Strlen());
        if (!RsFile::Delete(child))
          return false;
      }
  }
  return ScRemoveNativeDirectory(native.Data());
}

// Create each missing path prefix through the original CRT while preserving its UNC server prefix handling.
// Failure attempts rollback in creation order, including the original parent-before-child removal behavior.
bool RsDirectoryListing::CreateDirectoryA(const ScString &directory) {
  ScString work = RsDirectoryNativePath(directory), prefix;
  if (work.begin == work.end)
    return false;
  if (work.end[-1] != '/')
    work.Append("/");
  std::list<ScString> created;
  for (const char *byte = work.begin; byte != work.end; ++byte) {
    prefix.Append(byte, 1);
    if (*byte == '/' && !DoesDirectoryExist(prefix)) {
      if (!SDL_CreateDirectory(prefix.Data())) {
        for (const ScString &name : created)
          SDL_RemovePath(name.Data());
        return false;
      }
      created.push_back(prefix);
    }
  }
  return true;
}

// Split before the last nonterminal backslash, searching for slash only when no backslash qualifies.
// Empty input preserves both outputs; a missing separator publishes backslash and then the live source copy.
void RsDirectoryListing::SplitDirectoryPath(const ScString &source, ScString *directory, ScString *leaf) {
  u32 length = source.ScString::Strlen();
  if (!length)
    return;
  u32 limit = length - 2, split = UINT32_MAX;
  if (limit >= length)
    limit = length - 1;
  for (u32 i = limit + 1; i; --i)
    if (source.begin[i - 1] == '\\') {
      split = i;
      break;
    }
  if (split == UINT32_MAX)
    for (u32 i = limit + 1; i; --i)
      if (source.begin[i - 1] == '/') {
        split = i;
        break;
      }
  if (split == UINT32_MAX) {
    *directory = ScString("\\");
    *leaf = source;
  } else {
    ScString first(source.begin, split);
    *directory = first;
    // Re-read the source after the first output to preserve defined aliases between the source and either output.
    ScString second(source.begin + split, source.ScString::Strlen() - split);
    *leaf = second;
  }
}

// Compare unsigned group and instance before type, retaining the original resource tree's unique-key ordering.
static bool RsDirectoryKeyBefore(const RsKey &left, const RsKey &right) {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

bool RsDirectoryKeyLess::operator()(const RsKey &left, const RsKey &right) const {
  return RsDirectoryKeyBefore(left, right);
}

RsDirectorySegment::RsDirectorySegment() : listing(nullptr), ready(0), nextInstance(1) {
}

RsDirectorySegment::~RsDirectorySegment() {
  Shutdown();
}

bool RsDirectorySegment::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsDirectoryAccess, RsDirectoryAccess);
  return RsSegment::QueryInterface(iid, out);
}

u32 RsDirectorySegment::AddRef() {
  return RsSegment::AddRef();
}

u32 RsDirectorySegment::Release() {
  return RsSegment::Release();
}

static u32 RsDirectoryToken(const ScString &text, const char *token) {
  u32 length = static_cast<u32>(strlen(token)), size = text.Strlen();
  if (length <= size)
    for (u32 i = 0; i <= size - length; ++i)
      if (!memcmp(text.Data() + i, token, length))
        return i;
  return kScStringNotFound;
}

// Parse a DBDFSeg registry value into type-to-extension associations, keeping all three Strings alive across
// iterations.
void RsDirectoryRegistryCallback(const ScStringView &, const ScStringView &value, void *context) {
  RsDirectorySegment *segment = static_cast<RsDirectorySegment *>(context);
  ScString remaining(value), pair, number;
  do {
    pair = remaining.SplitToken(",");
    if (pair.ScString::Strlen()) {
      number = pair.SplitToken(":");
      number.Append("\0", 1);
      segment->extensions.Insert(ScConvertToUint32(number.begin), &pair);
    }
  } while (pair.ScString::Strlen() && remaining.ScString::Strlen());
  if (remaining.ScString::Strlen()) {
    number = remaining.SplitToken(":");
    number.Append("\0", 1);
    segment->extensions.Insert(ScConvertToUint32(number.begin), &remaining);
  }
}

// A missing extension map leaves readiness set; successful setup owns a fresh cached listing and refreshes known keys.
bool RsDirectorySegment::Init(u32 id, const ScStringView *name, bool resolved) {
  identifier = id;
  ready = 1;
  if (resolved) {
    ScString copy(*name);
    path = copy;
  } else {
    path.FromChar(name->ToChar());
    u32 position = RsDirectoryToken(path, "%DataDirectory%");
    if (position < path.Strlen()) {
      path.SplitToken("%DataDirectory%");
      ScString joined(RsGetFileSystem()->DataDirectory());
      joined.Append(path.Data(), path.Strlen());
      path = joined;
    } else {
      position = RsDirectoryToken(path, "%PluginDirectory%");
      if (position < path.Strlen()) {
        path.SplitToken("%PluginDirectory%");
        ScString joined(RsGetFileSystem()->PlugInDirectory());
        joined.Append("PlugIn\\");
        joined.Append(path.Data(), path.Strlen());
        path = joined;
      }
    }
    position = RsDirectoryToken(path, "%Language%");
    if (position < path.Strlen()) {
      // Retire the token return before constructing the language output, keeping its separate prefix copy alive.
      ScString prefix(static_cast<const ScString &>(path.SplitToken("%Language%")));
      ScString language;
      RsGetLanguageManager()->GetLanguageDirectoryName(&language, 0);
      prefix.Append(language.Data(), language.Strlen());
      prefix.Append(path.Data(), path.Strlen());
      path = prefix;
    }
  }
  ScRegistry *registry = nullptr;
  if (RsGetFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry))) {
    registry->EnumCategory("DBDFSeg", RsDirectoryRegistryCallback, this);
    registry->Release();
  }
  if (!extensions.Size())
    return false;
  listing = new RsDirectoryListing(path, false, false, false);
  RefreshDirectory();
  return true;
}

// Delete open record objects directly, clear known keys/path/extensions and free the cached listing.
// Clearing the native listing pointer excludes the original repeated-shutdown dangling-pointer deletion.
bool RsDirectorySegment::Shutdown() {
  for (auto &entry : records) {
    if (entry.second) {
      delete entry.second;
      entry.second = nullptr;
    }
  }
  records.clear();
  path.FromChar("");
  ready = 0;
  extensions.Clear();
  delete listing;
  listing = nullptr;
  return true;
}

// Return the borrowed owned directory String; no retain or implicit separator normalization occurs.
ScStringView *RsDirectorySegment::GetDirectory() {
  return &path;
}

u32 RsDirectorySegment::GetID() {
  return identifier;
}

// Return the borrowed embedded extension map without creating another object or changing its reference count.
RsExtensionMap *RsDirectorySegment::GetExtensionMap() {
  return &extensions;
}

u8 RsDirectorySegment::KeyName(const RsKey &key, ScString *instance, ScStringView *extension) {
  if (key.group != identifier || !extensions.GetExtension(&key.type, extension))
    return 0;
  instance->Sprintf("%8.8x", key.instance);
  return 1;
}

// Search cached names by the first eight instance characters and accept either mapped type or exact extension.
// If initialization did not create a listing, fail before the original null-listing dereference.
u8 RsDirectorySegment::FindName(const RsKey &key, ScStringView *out) {
  ScString instance, expected;
  if (!KeyName(key, &instance, &expected))
    return 0;
  if (!listing)
    return 0;
  u32 size = listing->Size();
  if (!size)
    return 0;
  for (u32 i = 0; i < size; ++i) {
    RsDirectoryEntry *entry = listing->GetEntry(i);
    if (ScCompareNoCase(entry->name.ToChar(), instance.begin, 8))
      continue;
    ScString extension;
    entry->GetExtension(&extension);
    u32 type;
    if ((!extensions.GetType(&extension, &type) || type != key.type) &&
        ScCompareNoCase(extension.begin, expected.begin))
      continue;
    out->FromChar(entry->name.begin);
    return 1;
  }
  return 0;
}

// Append a found cached filename to the owned directory bytes. Failed lookup leaves the caller's output unchanged.
u8 RsDirectorySegment::GetFileName(const RsKey &key, ScStringView *out) {
  ScString full(path), name;
  if (!FindName(key, &name))
    return 0;
  full.Append(name.Data(), name.Strlen());
  out->FromChar(full.Data(), full.Strlen());
  return 1;
}

// Resolve the extension type and parse its original Left(8) result in hexadecimal.
// Retire that prefix before publishing the complete key; the full stem copy survives the output assignment.
u8 RsDirectorySegment::MakeKey(const ScStringView *name, const ScStringView *extension, RsKey *out) {
  RsKey key(0, identifier, 0);
  u8 result = extensions.GetType(extension, &key.type);
  if (!result)
    return result;
  ScString copy(*name);
  {
    ScString prefix = copy.Left(8);
    key.instance = strtoul(prefix.ToChar(), nullptr, 16);
  }
  *out = key;
  return 1;
}

u8 RsDirectorySegment::NewKey(u32 type, RsKey *out) {
  if (!ready)
    return 0;
  while (nextInstance != kRsDirectoryInstanceLimit) {
    RsKey key(type, identifier, nextInstance++);
    if (records.find(key) == records.end()) {
      *out = key;
      return 1;
    }
  }
  return 0;
}

// Publish a new zero-reference extension map, copy through its real virtual operations, then retain the published object.
u8 RsDirectorySegment::CloneExtensionMap(RsExtensionMap **out) {
  *out = new RsExtensionMap;
  (*out)->CopyFrom(&extensions);
  (*out)->AddRef();
  return 1;
}

// Return the separate controlling database-segment identity without changing its reference count.
RsDatabaseSegment *RsDirectorySegment::GetSegment() {
  return static_cast<RsSegment *>(this);
}

// Enumerate every retained key in original TGI order once ready. File deletion does not remove these entries.
bool RsDirectorySegment::GetResourceKeyList(RsKeyListInterface *out) {
  if (!ready)
    return false;
  for (const auto &entry : records)
    out->Insert(entry.first);
  return true;
}

bool RsDirectorySegment::OpenDBSegment(bool, bool) {
  return true;
}

bool RsDirectorySegment::DoOpenRecord(const RsKey &key, RsRecord **out) {
  return OpenFileRecord(key, out, 1);
}

// Find the caller's controlling record view, query its real loose-file interface, close and release both references.
bool RsDirectorySegment::DoCloseRecord(RsRecord **slot) {
  if (!ready || !*slot)
    return 0;
  for (auto &entry : records) {
    RsLooseRecord *record = entry.second;
    RsRecord *view = record ? static_cast<RsRecordObject *>(record) : nullptr;
    if (view != *slot)
      continue;
    RsLooseAccess *access = nullptr;
    if (!(*slot)->QueryInterface(IID_RsLooseAccess, reinterpret_cast<void **>(&access)))
      return 0;
    access->Close();
    access->Release();
    (*slot)->Release();
    entry.second = nullptr;
    *slot = nullptr;
    return 1;
  }
  return 0;
}

// Follow the same ownership path as close, selecting the actual backup-restoring abort operation instead.
bool RsDirectorySegment::DoAbortRecord(RsRecord **slot) {
  if (!ready || !*slot)
    return 0;
  for (auto &entry : records) {
    RsLooseRecord *record = entry.second;
    RsRecord *view = record ? static_cast<RsRecordObject *>(record) : nullptr;
    if (view != *slot)
      continue;
    RsLooseAccess *access = nullptr;
    if (!(*slot)->QueryInterface(IID_RsLooseAccess, reinterpret_cast<void **>(&access)))
      return 0;
    access->Abort();
    access->Release();
    (*slot)->Release();
    entry.second = nullptr;
    *slot = nullptr;
    return 1;
  }
  return 0;
}

bool RsDirectorySegment::DoTestForRecord(const RsKey &key) {
  return ready && key.group == identifier && records.find(key) != records.end();
}

bool RsDirectorySegment::DoOpenRecordForWrite(const RsKey &key, RsRecord **out) {
  return OpenFileRecord(key, out, 2);
}

bool RsDirectorySegment::DoCreateNewRecord(const RsKey &key, RsRecord **out) {
  return OpenFileRecord(key, out, 3);
}

// Ignore filename lookup failure, append the reached name and attempt the actual path deletion without erasing the key.
bool RsDirectorySegment::DoDeleteRecord(const RsKey &key) {
  ScString name;
  FindName(key, &name);
  ScString full(path);
  full.Append(name.Data(), name.Strlen());
  return RsFile::Delete(full);
}

// Open one loose-file record per retained key. Only create mode admits an absent key, and failure preserves caller output.
u8 RsDirectorySegment::OpenFileRecord(const RsKey &key, RsRecord **out, u32 mode) {
  if (!ready || key.group != identifier)
    return 0;
  auto found = records.find(key);
  if (found != records.end() && found->second)
    return 0;
  if (found == records.end() && mode != 3)
    return 0;
  ScString name, full(path);
  if (mode == 3) {
    ScString extension;
    KeyName(key, &name, &extension);
    name.Append(".");
    name.Append(extension.Data(), extension.Strlen());
  } else {
    FindName(key, &name);
  }
  full.Append(name.Data(), name.Strlen());
  RsLooseRecord *record = new RsLooseRecord;
  if (!record->Open(&key, &full, mode)) {
    delete record;
    return 0;
  }
  record->AddRef();
  *out = static_cast<RsRecordObject *>(record);
  if (mode == 3) {
    records[key] = record;
    listing->Refresh();
  } else {
    found->second = record;
  }
  return 1;
}

// Refresh physical names and insert newly recognized keys, retaining old keys and currently open record pointers.
u8 RsDirectorySegment::RefreshDirectory() {
  if (!listing)
    return 0;
  listing->Refresh();
  u32 size = listing->Size();
  ScString stem, extension;
  for (u32 i = 0; i < size; ++i) {
    RsDirectoryEntry *entry = listing->GetEntry(i);
    if (!entry->GetExtension(&extension) || !entry->GetStem(&stem))
      continue;
    RsKey key;
    if (MakeKey(&stem, &extension, &key) && records.find(key) == records.end())
      records[key] = nullptr;
  }
  return 1;
}

// Construct the real directory-files producer and return its zero-reference primary directory identity.
ScUnknown *RsCreateClassA079CE1B() {
  return static_cast<RsDirectoryAccess *>(new RsDirectorySegment);
}

bool RsManagerObject::GetResourceList(RsKeyListInterface **out) {
  if (!RsGetCom()->GetClassObject(kRsKeyListInterfaceClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(out)))
    return false;
  RsManagerLock lock(&cacheLock);
  for (auto &entry : cache)
    (*out)->Insert(*entry.second->GetKey());
  return true;
}

// Filter by one live key query, then query again for insertion. The original ignores individual Insert results.
bool RsManagerObject::GetResourceListForType(RsKeyListInterface **out, u32 type) {
  if (!RsGetCom()->GetClassObject(kRsKeyListInterfaceClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(out)))
    return false;
  RsManagerLock lock(&cacheLock);
  for (auto &entry : cache)
    if (entry.second->GetKey()->type == type)
      (*out)->Insert(*entry.second->GetKey());
  return true;
}

// Merge type-matching copies from each priority segment's temporary key list, with each list count captured once.
bool RsManagerObject::GetAvailableResourceListForType(RsKeyListInterface **out, u32 type) {
  if (!RsGetCom()->GetClassObject(kRsKeyListInterfaceClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(out)))
    return false;
  RsManagerLock lock(&segmentLock);
  if (segments.empty())
    return false;
  for (auto segment : segments) {
    RsKeyListInterface *keys = nullptr;
    if (!RsGetCom()->GetClassObject(
            kRsKeyListInterfaceClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(&keys)))
      return false;
    segment->GetResourceKeyList(keys);
    u32 count = keys->Size();
    for (u32 index = 0; index < count; ++index) {
      RsKey key = *keys->GetKey(index);
      if (key.type == type)
        (*out)->Insert(key);
    }
    keys->Release();
  }
  return true;
}

// Load a cache miss into an owned resource reference before requested QI, retaining the original first-insert behavior.
bool RsManagerObject::GetResource(const RsKey &key, u32 iid, void **out) {
  RsManagerLock lock(&cacheLock);
  RsResource *resource;
  auto entry = cache.find(key);
  if (entry != cache.end())
    resource = entry->second;
  else {
    if (!LoadResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
      return false;
    cache.emplace(key, resource);
  }
  return resource->QueryInterface(iid, out);
}

// Load an uncached object and query its requested view. The original keeps the loader's acquired reference as well.
bool RsManagerObject::GetPrivateResource(const RsKey &key, u32 iid, void **out) {
  RsResource *resource;
  if (!LoadResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  return resource->QueryInterface(iid, out);
}

// Assign the requested key before unique insertion. Only a new entry with successful QI acquires a cache reference.
bool RsManagerObject::GetNewResource(const RsKey &key, u32 iid, void **out) {
  RsResource *resource = nullptr;
  if (!CreateResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  *resource->GetKey() = key;
  bool result = false;
  {
    RsManagerLock lock(&cacheLock);
    auto inserted = cache.emplace(key, resource);
    if (inserted.second) {
      if (resource->QueryInterface(iid, out)) {
        resource->AddRef();
        result = true;
      } else
        cache.erase(inserted.first);
    }
  }
  resource->Release();
  return result;
}

bool RsManagerObject::RemoveResource(const RsKey &key) {
  RsManagerLock lock(&cacheLock);
  auto entry = cache.find(key);
  if (entry != cache.end()) {
    if (entry->second)
      entry->second->Release();
    cache.erase(entry);
  }
  return true;
}

// Select owned factory and segment references, save the load result, then release the factory before the segment.
u8 RsManagerObject::LoadResource(const RsKey &key, u32 iid, void **out) {
  RsFactoryInterface *factory = nullptr;
  RsDatabaseSegment *segment = nullptr;
  if (!FindObjectFactory(key, &factory))
    return 0;
  if (!FindDBSegment(key, &segment)) {
    factory->Release();
    return 0;
  }
  u8 result = LoadRecord(key, factory, segment, iid, out);
  if (factory)
    factory->Release();
  if (segment)
    segment->Release();
  return result;
}

// Open and load the real record, preserving the original key copy, publication and close/release sequence.
u8 RsManagerObject::LoadRecord(
    const RsKey &key, RsFactoryInterface *factory, RsDatabaseSegment *segment, u32 iid, void **out) {
  RsRecord *record = nullptr;
  if (!segment->OpenRecord(key, &record)) {
    segment->AbortRecord(&record);
    return 0;
  }
  if (record->GetKey()->type == kRsRedirectRecordType) {
    RsStream *stream = nullptr;
    if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      segment->AbortRecord(&record);
      return 0;
    }
    u8 result = LoadRedirect(key, factory, segment, stream, iid, out);
    stream->Release();
    return result;
  }
  RsResource *resource = nullptr;
  if (!factory->Load(record, IID_RsResource, reinterpret_cast<void **>(&resource))) {
    segment->AbortRecord(&record);
    return 0;
  }
  RsKey *source = record->GetKey(), *destination = resource->GetKey();
  *destination = *source;
  u8 result = resource->QueryInterface(iid, out);
  if (!segment->CloseRecord(&record))
    result = 0;
  resource->Release();
  return result;
}

// Read a replacement segment's class/path, open it for the current group, and close the old record before recursion.
// The extra inner stream release precedes that close; the outer loader still releases its captured stream afterward.
u8 RsManagerObject::LoadRedirect(
    const RsKey &key, RsFactoryInterface *factory, RsDatabaseSegment *segment, RsStream *stream, u32 iid, void **out) {
  ScString path;
  u32 classId = 0;
  u8 result = 0;
  if (stream->ReadU32(&classId) && stream->ReadString(&path)) {
    RsDatabaseSegment *replacement = nullptr;
    if (RsGetCom()->GetClassObject(classId, IID_RsDatabaseSegment, reinterpret_cast<void **>(&replacement))) {
      if (replacement->Init(key.group, &path, false)) {
        RsRecord *record = nullptr;
        if (stream->QueryInterface(IID_RsRecord, reinterpret_cast<void **>(&record))) {
          stream->Release();
          if (segment->CloseRecord(&record))
            result = LoadRecord(key, factory, replacement, iid, out);
        }
      }
      replacement->Release();
    }
  }
  return result;
}

static u32 RsCollectionTicks;

// Keep the original unsigned group, instance, type ordering used by the resource cache tree.
bool RsCacheKeyLess::operator()(const RsKey &first, const RsKey &second) const {
  if (first.group != second.group)
    return first.group < second.group;
  if (first.instance != second.instance)
    return first.instance < second.instance;
  return first.type < second.type;
}

RsManagerObject::RsManagerObject()
    : RsServiceBase(kRsManagerServiceId, kRsResourceManagerPriority), nextInstance(70000), newResourceGroup(0),
      collectionActive(false) {
}

RsManagerObject::~RsManagerObject() {
}

bool RsManagerObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsManager, RsManager);
  return RsServiceBase::QueryInterface(iid, out);
}

u32 RsManagerObject::AddRef() {
  return RsServiceBase::AddRef();
}

u32 RsManagerObject::Release() {
  return RsServiceBase::Release();
}

bool RsManagerObject::Init() {
  return RsServiceBase::Init();
}

// Count enabled idle notifications globally and collect after each101st call; disabled managers do not advance it.
bool RsManagerObject::OnIdle(u32) {
  if (collectionActive && ++RsCollectionTicks > 100) {
    RsCollectionTicks = 0;
    CollectGarbage();
  }
  return true;
}

// Obtain the factory through COM before locking, then retain its acquired reference in the ordered registry and alias
// map.
bool RsManagerObject::RegisterObjectFactory(u32 classId, u32 type) {
  RsFactoryInterface *factory;
  if (!RsGetCom()->GetClassObject(classId, IID_RsFactoryInterface, reinterpret_cast<void **>(&factory)))
    return false;
  RsManagerLock lock(&factoryLock);
  factories.emplace(classId, factory);
  aliases.emplace(type, classId);
  return true;
}

// Release and erase only the first equal class registration. Historical type aliases remain in their original map.
bool RsManagerObject::UnregisterObjectFactory(u32 classId) {
  RsManagerLock lock(&factoryLock);
  auto entry = factories.lower_bound(classId);
  if (entry == factories.end() || entry->first != classId)
    return false;
  entry->second->Release();
  factories.erase(entry);
  return true;
}

// Select the first equal alias and registration, then publish the factory pointer before its ownership acquisition.
bool RsManagerObject::FindObjectFactory(u32 type, RsFactoryInterface **out) {
  RsManagerLock lock(&factoryLock);
  auto alias = aliases.lower_bound(type);
  if (alias == aliases.end() || alias->first != type)
    return false;
  auto entry = factories.lower_bound(alias->second);
  if (entry == factories.end() || entry->first != alias->second)
    return false;
  *out = entry->second;
  entry->second->AddRef();
  return true;
}

bool RsManagerObject::FindObjectFactory(const RsKey &key, RsFactoryInterface **out) {
  return FindObjectFactory(key.type, out);
}

u32 RsManagerObject::GetFactoryCount() {
  RsManagerLock lock(&factoryLock);
  return (u32)factories.size();
}

// Borrow the indexed factory in unsigned class order without retaining it; out-of-range indices return null.
RsFactoryInterface *RsManagerObject::GetFactoryByIndex(u32 index) {
  RsManagerLock lock(&factoryLock);
  if (index >= factories.size())
    return nullptr;
  auto entry = factories.begin();
  while (index--)
    ++entry;
  return entry->second;
}

u8 RsManagerObject::CreateResource(const RsKey &key, u32 iid, void **out) {
  RsFactoryInterface *factory = nullptr;
  u8 result = FindObjectFactory(key, &factory);
  if (result) {
    result = factory->New(key.type, iid, out);
    factory->Release();
  }
  return result;
}

bool RsManagerObject::IsGarbageCollectionActive() {
  RsManagerLock lock(&collectionLock);
  return collectionActive;
}

void RsManagerObject::SetGarbageCollectionActive(bool value) {
  RsManagerLock lock(&collectionLock);
  collectionActive = value;
}

// Hold cache, factory and segment locks in the original order. Release cached resources to zero, swallowing per-object
// exceptions, then clear factories and segments with their distinct single-release ownership rules.
bool RsManagerObject::Shutdown() {
  cacheLock.Lock();
  factoryLock.Lock();
  segmentLock.Lock();
  ForceGarbageCollection();
  for (auto &entry : cache)
    if (entry.second) {
      while (entry.second->Release()) {
      }
      entry.second = nullptr;
    }
  cache.clear();
  cacheLock.Unlock();
  for (auto &entry : factories)
    entry.second->Release();
  factories.clear();
  factoryLock.Unlock();
  for (auto segment : segments)
    segment->Release();
  segments.clear();
  segmentLock.Unlock();
  return true;
}

// Insert under the generated key, then retain the entry selected by the factory-created resource's own live key.
// A missing selected entry would be an undefined original node read; the native boundary reports failure instead.
bool RsManagerObject::GetNewResource(u32 type, u32 iid, void **out) {
  u32 instance = NextInstance(), group = NewResourceGroup();
  RsKey key(type, group, instance);
  RsResource *resource;
  if (!CreateResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  RsManagerLock lock(&cacheLock);
  cache.emplace(key, resource);
  auto entry = cache.find(*resource->GetKey());
  if (entry == cache.end())
    return false;
  entry->second->AddRef();
  return resource->QueryInterface(iid, out);
}

bool RsManagerObject::FindObjectFactory(const RsResource &resource, RsFactoryInterface **out) {
  return FindObjectFactory(*resource.Type(), out);
}

// Preserve the SDK's void interface while executing the installed maximum-eight-pass collection sequence.
void RsManagerObject::ForceGarbageCollection() {
  for (u32 pass = 0; pass < 8; ++pass)
    if (!CollectGarbage())
      break;
}

// Keep resources with more than one reference; release single-owner entries before clearing and erasing their nodes.
// Null entries are erased without setting the collected flag, and iterator advancement precedes saved-node deletion.
u8 RsManagerObject::CollectGarbage() {
  RsManagerLock lock(&cacheLock);
  u8 result = 0;
  for (auto entry = cache.begin(); entry != cache.end();) {
    RsResource *resource = entry->second;
    if (resource && resource->RefCount() > 1) {
      ++entry;
      continue;
    }
    if (resource) {
      result = 1;
      resource->Release();
      entry->second = nullptr;
    }
    auto removed = entry++;
    cache.erase(removed);
  }
  return result;
}

// Return the original zero-reference manager interface, distinct from the service and module identities.
ScUnknown *RsCreateManager() {
  return static_cast<RsManager *>(new RsManagerObject());
}

u32 RsManagerObject::NextInstance() {
  RsManagerLock lock(&collectionLock);
  return nextInstance++;
}

u32 RsManagerObject::NewResourceGroup() {
  RsManagerLock lock(&collectionLock);
  return newResourceGroup;
}

// Acquire the resource through the manager, normalize the private save byte by nonzero, then release the acquired view.
bool RsManagerObject::Save(const RsKey &key, RsDatabaseSegment *segment) {
  RsResource *resource = nullptr;
  if (!GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  bool result = SaveResource(resource, key, segment) != 0;
  resource->Release();
  return result;
}

// Capture the key count once and attempt every indexed key. Accumulate failures without short-circuiting later saves.
bool RsManagerObject::Save(RsKeyListInterface *keys, RsDatabaseSegment *segment) {
  u32 count = keys->Size();
  u8 failed = 0;
  for (u32 index = 0; index < count; ++index)
    failed |= Save(*keys->GetKey(index), segment) == 0;
  return failed == 0;
}

// Test and reopen the resource's live key, or create the requested key. Release the factory before aborting a failed
// write/close; an unsuccessful open returns its byte without abort.
u8 RsManagerObject::SaveResource(RsResource *resource, const RsKey &key, RsDatabaseSegment *segment) {
  RsRecord *record = nullptr;
  RsFactoryInterface *factory = nullptr;
  u8 result;
  if (segment->TestForRecord(*resource->GetKey()))
    result = segment->OpenRecordForWrite(*resource->GetKey(), &record);
  else
    result = segment->CreateNewRecord(key, &record);
  if (result != 1)
    return result;
  result = FindObjectFactory(*resource, &factory);
  if (result == 1) {
    result = factory->WriteRecord(resource, record);
    if (result == 1) {
      result = segment->CloseRecord(&record);
      if (result == 1) {
        factory->Release();
        return 1;
      }
    }
    factory->Release();
  }
  segment->AbortRecord(&record);
  return result;
}

bool RsManagerObject::RegisterDBSegment(RsDatabaseSegment *segment) {
  return RegisterDBSegmentFront(segment);
}

// Remove the previous identity under the nested lock, publish at the front, then acquire its stored reference.
bool RsManagerObject::RegisterDBSegmentFront(RsDatabaseSegment *segment) {
  RsManagerLock lock(&segmentLock);
  UnregisterDBSegment(segment);
  segments.push_front(segment);
  segment->AddRef();
  return true;
}

// Move the identity to the back priority position before taking its replacement registration reference.
bool RsManagerObject::RegisterDBSegmentBack(RsDatabaseSegment *segment) {
  RsManagerLock lock(&segmentLock);
  UnregisterDBSegment(segment);
  segments.push_back(segment);
  segment->AddRef();
  return true;
}

bool RsManagerObject::UnregisterDBSegment(RsDatabaseSegment *segment) {
  RsManagerLock lock(&segmentLock);
  for (auto entry = segments.begin(); entry != segments.end(); ++entry)
    if (*entry == segment) {
      (*entry)->Release();
      segments.erase(entry);
      return true;
    }
  return false;
}

// Test pointer identity in priority order without retaining or querying any segment.
bool RsManagerObject::TestDBSegment(RsDatabaseSegment *segment) {
  RsManagerLock lock(&segmentLock);
  for (auto entry : segments)
    if (entry == segment)
      return true;
  return false;
}

// Select the first segment reporting the key, publish before retaining, and explicitly clear the output on a miss.
bool RsManagerObject::FindDBSegment(const RsKey &key, RsDatabaseSegment **out) {
  RsManagerLock lock(&segmentLock);
  for (auto entry : segments)
    if (entry->TestForRecord(key)) {
      *out = entry;
      entry->AddRef();
      return true;
    }
  *out = nullptr;
  return false;
}

// Select the first live segment identifier match with the same publication and ownership ordering.
bool RsManagerObject::FindDBSegment(u32 segmentId, RsDatabaseSegment **out) {
  RsManagerLock lock(&segmentLock);
  for (auto entry : segments)
    if (entry->GetID() == segmentId) {
      *out = entry;
      entry->AddRef();
      return true;
    }
  *out = nullptr;
  return false;
}

u32 RsManagerObject::GetSegmentCount() {
  RsManagerLock lock(&segmentLock);
  return (u32)segments.size();
}

// Borrow the indexed priority entry without AddRef; unsigned out-of-range indices return null.
RsDatabaseSegment *RsManagerObject::GetSegmentByIndex(u32 index) {
  RsManagerLock lock(&segmentLock);
  return index < segments.size() ? segments[index] : nullptr;
}

// Probe segment records in priority order under the original lock, without factory selection or ownership changes.
bool RsManagerObject::TestForKey(RsKey *key) {
  RsManagerLock lock(&segmentLock);
  for (auto entry : segments)
    if (entry->TestForRecord(*key))
      return true;
  return false;
}

// Acquire the selected segment, choose ordinary or writable open, and preserve its result across segment Release.
bool RsManagerObject::OpenDBRecord(const RsKey &key, RsRecord **out, bool write) {
  RsDatabaseSegment *segment = nullptr;
  bool result = FindDBSegment(key, &segment);
  if (result) {
    result = write ? segment->OpenRecordForWrite(key, out) : segment->OpenRecord(key, out);
    segment->Release();
  }
  return result;
}

bool RsManagerObject::CloseDBRecord(const RsKey &key, RsRecord **record) {
  RsDatabaseSegment *segment = nullptr;
  bool result = FindDBSegment(key, &segment);
  if (result) {
    result = segment->CloseRecord(record);
    segment->Release();
  }
  return result;
}

RsModuleObject::RsModuleObject() : references(0) {
}

RsModuleObject::~RsModuleObject() {
}

bool RsModuleObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsModuleObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsModuleObject::AddRef() {
  RsAddModuleRef();
  return ++references;
}

// Capture the final-reference branch before module release; only the nonfinal branch decrements afterward.
u32 RsModuleObject::Release() {
  bool one = references == 1;
  RsRemoveModuleRef();
  if (one) {
    delete this;
    return 0;
  }
  return --references;
}
