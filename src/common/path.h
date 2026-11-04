#ifndef SC3_COMMON_PATH_H
#define SC3_COMMON_PATH_H

#include <list>

#include <stdio.h>

#include "common/sc3_array.h"
#include "common/sc3_string.h"

// Portable game attribute bits; failed native queries preserve the original all-bits-set sentinel.
enum ScFileAttribute : u32 {
  kScFileAttributeReadOnly = 1,
  kScFileAttributeHidden = 2,
  kScFileAttributesInvalid = 0xffffffff,
};

// Windows retains its complete DWORD attributes. POSIX maps permissions and its persistent hidden metadata.
u32 ScGetNativeFileAttributes(const char *path);
// Set the mapped bits without renaming the file or changing unrelated metadata; retain the native BOOL/failure result.
i32 ScSetNativeFileAttributes(const char *path, u32 attributes);


// Native filesystem boundary: normalize separators and resolve existing components without changing game identifiers.
ScString ScNativeFilePath(const char *path);
// Enumerate native names using the game's proven DOS wildcard forms. Release the returned block with SDL_free.
char **ScNativeDirectoryGlob(const char *directory, const char *filter, int *count);
// Open a CRT stream at the UTF-8 native path boundary. The caller retains ordinary fclose ownership.
FILE *ScOpenNativeFile(const char *path, const char *mode);
// Remove one native directory or file, retaining real failure results rather than treating absence as success.
bool ScRemoveNativeDirectory(const char *path);
bool ScRemoveNativeFile(const char *path);
// Rename without replacing a destination that appears concurrently. Unsupported native operations fail explicitly.
bool ScRenameNativeFile(const char *source, const char *target);
// Query host volume metadata without changing the caller's stored game path spelling.
bool ScNativeVolumeName(const char *path, ScString *out);
u32 ScNativeVolumeSerial(const char *path);
u32 ScNativeComponentLimit(const char *path);

class ScDirectory;

struct ScDirectoryEntry {
  operator const char *() const;
  u32 GetEntryExtension(ScString *out);
  u32 GetEntryMinusExtension(ScString *out);
  void GetFullPath(ScString *out);
  bool IsMediaRemovable();
  u32 GetSize();
  u32 GetCreationDate();
  u32 GetLastModificationDate();
  u32 GetSerialNumber();
  bool CreatePhysicalEntry();
  ScDirectory *owner;
  ScString name;
  i32 kind;
};

struct ScDirectoryLink {
  ScDirectoryLink *next, *previous;
};

struct ScDirectoryNode : ScDirectoryLink {
  ScDirectoryEntry entry;
};

// Native stable nodes replace only the original directory-entry list allocator and unused sentinel payload.
class ScDirectoryEntries {
public:
  ScDirectoryEntries();
  ~ScDirectoryEntries();
  // Keep this native node pool under one owner; source copy operations remain unreconstructed.
  ScDirectoryEntries(const ScDirectoryEntries &) = delete;
  // Assignment cannot duplicate the owned sentinel and node allocations.
  ScDirectoryEntries &operator=(const ScDirectoryEntries &) = delete;
  void Clear();
  void Insert(ScDirectoryLink *before, ScDirectory *owner, const char *name, i32 kind);
  bool Contains(const char *name) const;
  ScDirectoryLink *head;
};

// Native name snapshots replace the source's internal String-list storage without claiming its std::list declaration.
class ScDirectoryNames {
public:
  // Begin an empty native snapshot without constructing unused source-list sentinel payload.
  ScDirectoryNames() = default;
  ~ScDirectoryNames();
  // Each snapshot owns its String objects independently of the search inventory.
  ScDirectoryNames(const ScDirectoryNames &) = delete;
  // Native snapshot assignment cannot alias another owner's String allocations.
  ScDirectoryNames &operator=(const ScDirectoryNames &) = delete;
  void Append(const char *name);
  void Prepend(const char *name);
  void Sort();
  ScArray<ScString *> entries;
};

// Actual cRZDirectory owns its entry list, directory, filter and two special-directory inclusion bytes.
// Retail CodeView confirms its public enumeration methods use list<cRZString>.
class ScDirectory {
public:
  ScDirectory(const ScString &path, bool scan, bool parent, bool current);
  ~ScDirectory();
  bool ReadDirectoryEntries();
  bool IsDirectoryRoot();
  operator const char *() const;
  ScString GetPath();
  u32 GetDirectoryEntryCount();
  ScDirectoryEntry *GetNthEntry(u32 index);
  bool ChangeDirectory(const ScString &path, bool scan);
  void SetNewDirectoryEntryFilter(const ScString &value, bool scan);
  bool ReadDirectorySpecificEntriesIntoStringList(std::list<ScString> *output, u32 flags);
  bool ReadDirectoryFileEntriesIntoStringList(std::list<ScString> *output);
  bool ReadDirectoryDirectoryEntriesIntoStringList(std::list<ScString> *output, u32 flags);
  static void AlphabetizeEntryList(std::list<ScString> *entries);
  static bool DoesDirectoryExist(const ScString &path);
  static bool CreateDirectoryA(const ScString &path);
  static bool RemoveDirectoryA(const ScString &path);
  static bool DoesAnyEntryExistThatMatchesPattern(const ScString &pattern);
  // Native effects API for the original list-output method; std::list output spelling is not asserted here.
  bool ReadNames(ScDirectoryNames *output, u32 flags);

private:
  friend struct ScDirectoryEntry;
  bool DoesDirectoryListContainEntry(const ScString &name);
  void MakeSurePathEndsWithSeparator();
  ScDirectoryEntries entries;
  ScString directory, filter;
  u8 allow_parent, allow_current;
};

static_assert(sizeof(uptr) != 4 || sizeof(ScDirectoryEntry) == 28);
static_assert(sizeof(uptr) != 4 || sizeof(ScDirectory) == 48);

// Actual cRZPath owns five Strings; CodeView proves composition rather than String inheritance.
// Const accessors lazily update the mutable path/component caches while preserving their original reference returns.
class ScPath {
public:
  ScPath();
  explicit ScPath(const ScString &value);
  ScPath(const ScString &drive, const ScString &directory, const ScString &file_name, const ScString &extension);
  ~ScPath();
  ScPath(const ScPath &value);
  ScString *Path();
  const ScString &Path() const;
  ScString *Drive();
  const ScString &Drive() const;
  ScString &Directory();
  const ScString &Directory() const;
  ScString &FileName();
  const ScString &FileName() const;
  const ScString &Extension() const;
  void SplitPath();
  void SplitPath(const ScString &value);
  void MakePath();
  void MakePath(const ScString &drive, const ScString &directory, const ScString &file_name, const ScString &extension);
  void FullPath();
  void FullPath(const ScString &value);
  bool Exists() const;

private:
  void ImplementSplitPath() const;
  void ImplementMakePath() const;
  void ImplementFullPath() const;
  mutable ScString path;
  mutable u8 concat;
  mutable ScString drive, directory, file_name, extension;
  mutable u8 split;
};

static_assert(sizeof(uptr) != 4 || sizeof(ScPath) == 108);

#endif
