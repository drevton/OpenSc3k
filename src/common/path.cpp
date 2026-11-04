#include <SDL3/SDL.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <wchar.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <limits.h>
#include <sys/attr.h>
#include <sys/mount.h>
#endif
#endif
#include <assert.h>
#include <errno.h>
#if defined(__linux__)
#include <sys/xattr.h>
#endif
#include <string.h>

#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"

// Carry only this filesystem component lookup's borrowed name and owned result through SDL's external callback ABI.
struct ScPathCaseSearch {
  const char *name;
  ScString *match;
};

// Prefer an exact directory entry, retaining the first case-insensitive match only while no exact match is found.
static SDL_EnumerationResult SDLCALL ScFindPathCase(void *userdata, const char *, const char *name) {
  auto *search = static_cast<ScPathCaseSearch *>(userdata);
  if (!strcmp(name, search->name)) {
    search->match->FromChar(name);
    return SDL_ENUM_SUCCESS;
  }
  if (search->match->begin == search->match->end && !SDL_strcasecmp(name, search->name))
    search->match->FromChar(name);
  return SDL_ENUM_CONTINUE;
}

// Resolve the game's slash/backslash and case-insensitive path spelling against the actual local filesystem.
// Existing component names are corrected only in this returned native path; a new leaf retains the requested spelling.
ScString ScNativeFilePath(const char *path) {
  ScString input(path), result;
  for (char *byte = input.begin; byte != input.end; ++byte)
    if (*byte == '\\')
      *byte = '/';
#ifdef _WIN32
  if (input.Strlen() > 1 && input.begin[0] == '/' && input.begin[1] == '/') {
    errno = ENOTSUP;
    return ScString();
  }
#endif
  SDL_PathInfo info;
  if (SDL_GetPathInfo(input.Data(), &info))
    return input;
  const char *position = input.begin;
#ifdef _WIN32
  if (position[0] && position[1] == ':') {
    result.FromChar(position, 2);
    position += 2;
  }
#endif
  if (*position == '/') {
    result.Append("/");
    while (*position == '/')
      ++position;
  }
  while (*position) {
    const char *end = strchr(position, '/');
    if (!end)
      end = position + strlen(position);
    ScString component(position, u32(end - position)), candidate(result);
    candidate.Append(component.Data(), component.Strlen());
    if (!SDL_GetPathInfo(candidate.Data(), &info)) {
      ScString match;
      ScPathCaseSearch search{component.Data(), &match};
      SDL_EnumerateDirectory(result.begin == result.end ? "." : result.Data(), ScFindPathCase, &search);
      if (match.begin != match.end)
        component = match;
    }
    result.Append(component.Data(), component.Strlen());
    position = end;
    if (*position == '/') {
      result.Append("/");
      while (*position == '/')
        ++position;
    }
  }
  return result;
}

// Resolve local asset spelling and open a real CRT stream, converting UTF-8 names for Windows' wide file API.
// Preserve recoverable open errors across conversion-buffer cleanup rather than adding a new game error contract.
FILE *ScOpenNativeFile(const char *path, const char *mode) {
  ScString native = ScNativeFilePath(path);
#ifdef _WIN32
  auto *widePath = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", native.Data(),
      usize(native.Strlen()) + 1));
  auto *wideMode = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", mode, strlen(mode) + 1));
  FILE *file = nullptr;
  if (widePath && wideMode)
    file = _wfopen(widePath, wideMode);
  else
    errno = EILSEQ;
  i32 error = errno;
  SDL_free(widePath);
  SDL_free(wideMode);
  errno = error;
  return file;
#else
  return fopen(native.Data(), mode);
#endif
}

// Retire a known directory without following a trailing-slash symlink into its target's contents.
// The caller determines directory kind before recursion; this operation itself reports actual native removal errors.
bool ScRemoveNativeDirectory(const char *path) {
  ScString native = ScNativeFilePath(path);
#ifdef _WIN32
  u32 root = native.Strlen() >= 3 && native.begin[1] == ':' && native.begin[2] == '/' ? 3 : 1;
#else
  u32 root = 1;
#endif
  while (native.Strlen() > root && native.end[-1] == '/')
    native.Erase(native.Strlen() - 1, 1);
#ifdef _WIN32
  auto *wide = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", native.Data(),
      usize(native.Strlen()) + 1));
  if (!wide) {
    errno = EILSEQ;
    return false;
  }
  bool removed = _wrmdir(wide) == 0;
  i32 error = errno;
  SDL_free(wide);
  errno = error;
  return removed;
#else
  return remove(native.Data()) == 0;
#endif
}

// Unlink only a native file or symbolic link, preserving missing-file and access failures.
bool ScRemoveNativeFile(const char *path) {
  ScString native = ScNativeFilePath(path);
#ifdef _WIN32
  auto *wide = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", native.Data(),
      usize(native.Strlen()) + 1));
  if (!wide) {
    errno = EILSEQ;
    return false;
  }
  bool removed = _wunlink(wide) == 0;
  i32 error = errno;
  SDL_free(wide);
  errno = error;
  return removed;
#else
  return unlink(native.Data()) == 0;
#endif
}

// Keep the original no-replacement rename contract atomic instead of checking a target before a clobbering rename.
// Kernel/filesystem support failures remain native errors; only the caller handles cross-volume copy fallback.
bool ScRenameNativeFile(const char *source, const char *target) {
  ScString input = ScNativeFilePath(source), destination = ScNativeFilePath(target);
#ifdef _WIN32
  auto *from = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", input.Data(),
      usize(input.Strlen()) + 1));
  auto *to = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", destination.Data(),
      usize(destination.Strlen()) + 1));
  bool moved = false;
  if (from && to)
    moved = _wrename(from, to) == 0;
  else
    errno = EILSEQ;
  i32 error = errno;
  SDL_free(from);
  SDL_free(to);
  errno = error;
  return moved;
#elif defined(__APPLE__)
  return renamex_np(input.Data(), destination.Data(), RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
  return syscall(SYS_renameat2, AT_FDCWD, input.Data(), AT_FDCWD, destination.Data(), RENAME_NOREPLACE) == 0;
#else
  errno = ENOTSUP;
  return false;
#endif
}

// Preserve the all-files and extensionless DOS wildcard forms used by game directory searches.
// A trailing '*.' matches names without an extension; SDL's literal final dot would omit radio station directories.
char **ScNativeDirectoryGlob(const char *directory, const char *filter, int *count) {
  ScString native = ScNativeFilePath(directory), pattern(!strcmp(filter, "*.*") ? "*" : filter);
  bool extensionless = pattern.Strlen() >= 2 && pattern.end[-2] == '*' && pattern.end[-1] == '.';
  if (extensionless)
    pattern.Erase(pattern.Strlen() - 1, 1);
  char **names = SDL_GlobDirectory(native.Data(), pattern.Data(), SDL_GLOB_CASEINSENSITIVE, count);
  if (names && extensionless) {
    int retained = 0;
    for (int index = 0; index < *count; ++index)
      if (!strchr(names[index], '.'))
        names[retained++] = names[index];
    names[retained] = nullptr;
    *count = retained;
  }
  return names;
}

// Enumerate this directory's filter through the shared native wildcard boundary.
static char **ScGlobLocalDirectory(const ScString &directory, const ScString &filter, int *count) {
  return ScNativeDirectoryGlob(directory.Data(), filter.Data(), count);
}

// Obtain the kind of one native entry without altering the directory object's stored path.
static bool ScLocalEntryIsDirectory(const ScString &directory, const char *name) {
  ScString native = ScNativeFilePath(directory.Data());
  if (native.begin != native.end && native.end[-1] != '/')
    native.Append("/");
  native.Append(name);
  SDL_PathInfo info;
  return SDL_GetPathInfo(native.Data(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

// Expand a path without imposing the original fixed output capacity, retaining a trailing separator when supplied.
static ScString ScAbsoluteLocalPath(const char *path) {
  ScString native = ScNativeFilePath(path);
  if (*path && !native.Strlen())
    return native;
  bool rooted = native.begin != native.end && native.begin[0] == '/';
#ifdef _WIN32
  bool driveRoot = native.Strlen() >= 3 && native.begin[1] == ':' && native.begin[2] == '/';
  if (rooted) {
    char *current = SDL_GetCurrentDirectory();
    if (!current)
      return ScString();
    ScString prefix(current, 2);
    SDL_free(current);
    prefix.Append(native.Data(), native.Strlen());
    native = prefix;
    driveRoot = true;
  }
#else
  bool driveRoot = false;
#endif
  if (!rooted && !driveRoot) {
    char *current;
    const char *relative = native.Data();
#ifdef _WIN32
    if (native.Strlen() >= 2 && native.begin[1] == ':') {
      wchar_t *drivePath = _wgetdcwd(SDL_toupper(native.begin[0]) - 'A' + 1, nullptr, 0);
      current = drivePath ? SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char *>(drivePath),
          (wcslen(drivePath) + 1) * sizeof(wchar_t)) : nullptr;
      free(drivePath);
      relative += 2;
    } else
#endif
      current = SDL_GetCurrentDirectory();
    if (!current)
      return ScString();
    ScString joined = ScNativeFilePath(current);
    SDL_free(current);
    if (joined.begin != joined.end && joined.end[-1] != '/')
      joined.Append("/");
    joined.Append(relative);
    native = joined;
  }
  ScString result;
  const char *position = native.Data();
#ifdef _WIN32
  if (position[0] && position[1] == ':') {
    result.FromChar(position, 2);
    position += 2;
  }
#else
  char *resolved = realpath(native.Data(), nullptr);
  if (resolved) {
    result.FromChar(resolved);
    free(resolved);
    if (native.Strlen() && native.end[-1] == '/' && result.Strlen() > 1)
      result.Append("/");
    return result;
  }
  if (errno != ENOENT)
    return ScString();
#endif
  if (*position == '/') {
    result.Append("/");
    ++position;
  }
  u32 rootLength = result.Strlen();
  while (*position) {
    const char *end = strchr(position, '/');
    if (!end)
      end = position + strlen(position);
    usize length = usize(end - position);
    if (length == 2 && position[0] == '.' && position[1] == '.') {
      u32 size = result.Strlen();
      while (size > rootLength && result.begin[size - 1] != '/')
        --size;
      if (size > rootLength)
        --size;
      result.Erase(size, kScStringToEnd);
    } else if (length && !(length == 1 && *position == '.')) {
      if (result.Strlen() > rootLength)
        result.Append("/");
      result.Append(position, u32(length));
    }
#ifndef _WIN32
    char *physical = realpath(result.Data(), nullptr);
    if (physical) {
      result.FromChar(physical);
      free(physical);
    } else if (errno != ENOENT)
      return ScString();
#endif
    position = *end ? end + 1 : end;
  }
  if (native.Strlen() && native.end[-1] == '/' && result.Strlen() > rootLength)
    result.Append("/");
  return ScNativeFilePath(result.Data());
}

#ifdef _WIN32
// Build the original drive-root volume query as a native wide path, without inventing a drive for unsupported paths.
static wchar_t *ScWideVolumeRoot(const char *path) {
  if (!path[0] || path[1] != ':') {
    errno = EINVAL;
    return nullptr;
  }
  char root[] = {path[0], ':', '/', 0};
  auto *wide = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", root, sizeof(root)));
  if (!wide)
    errno = EILSEQ;
  return wide;
}
#else
// Find the real containing filesystem for a not-yet-created leaf while retaining native errors from invalid parents.
static ScString ScExistingVolumePath(const char *path) {
  if (!*path) {
    errno = ENOENT;
    return ScString();
  }
  ScString native = ScAbsoluteLocalPath(path);
  if (!native.Strlen())
    return native;
  struct stat info;
  while (stat(native.Data(), &info)) {
    if (errno != ENOENT)
      return ScString();
    while (native.Strlen() > 1 && native.end[-1] == '/')
      native.Erase(native.Strlen() - 1, 1);
    char *separator = strrchr(native.begin, '/');
    if (!separator)
      return ScString();
    native.Erase(separator == native.begin ? 1 : u32(separator - native.begin), kScStringToEnd);
  }
  return native;
}
#endif

bool ScNativeVolumeName([[maybe_unused]] const char *path, [[maybe_unused]] ScString *out) {
#ifdef _WIN32
  wchar_t *root = ScWideVolumeRoot(path), label[256];
  if (!root)
    return false;
  bool found = GetVolumeInformationW(root, label, 256, nullptr, nullptr, nullptr, nullptr, 0) != FALSE;
  DWORD error = GetLastError();
  SDL_free(root);
  SetLastError(error);
  if (!found)
    return false;
  char *utf8 = SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char *>(label),
      (wcslen(label) + 1) * sizeof(wchar_t));
  if (!utf8) {
    errno = EILSEQ;
    return false;
  }
  out->FromChar(utf8);
  SDL_free(utf8);
  return true;
#elif defined(__APPLE__)
  ScString native = ScExistingVolumePath(path);
  if (!native.Strlen())
    return false;
  struct statfs volume;
  if (statfs(native.Data(), &volume))
    return false;
  struct attrlist attributes = {};
  attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
  attributes.volattr = ATTR_VOL_INFO | ATTR_VOL_NAME;
  struct ScVolumeLabel {
    u32 length;
    attrreference_t name;
  };
  usize capacity = sizeof(ScVolumeLabel) + NAME_MAX + 1;
  for (;;) {
    u8 *bytes = new u8[capacity];
    if (getattrlist(volume.f_mntonname, &attributes, bytes, capacity, 0)) {
      i32 error = errno;
      delete[] bytes;
      errno = error;
      return false;
    }
    auto *label = reinterpret_cast<ScVolumeLabel *>(bytes);
    if (label->length < sizeof(ScVolumeLabel) || label->length > capacity) {
      delete[] bytes;
      errno = ERANGE;
      return false;
    }
    isize offset = isize(offsetof(ScVolumeLabel, name)) + label->name.attr_dataoffset;
    usize length = label->name.attr_length;
    if (offset < isize(sizeof(ScVolumeLabel)) || !length || length > SIZE_MAX - usize(offset)) {
      delete[] bytes;
      errno = ERANGE;
      return false;
    }
    usize required = usize(offset) + length;
    if (required > capacity) {
      delete[] bytes;
      capacity = required;
      continue;
    }
    if (label->length < required || bytes[required - 1]) {
      delete[] bytes;
      errno = ERANGE;
      return false;
    }
    out->FromChar(reinterpret_cast<const char *>(bytes) + offset, u32(length - 1));
    delete[] bytes;
    return true;
  }
#else
  errno = ENOTSUP;
  return false;
#endif
}

// Return the host's actual volume identity in the game's DWORD result width; failure remains zero.
u32 ScNativeVolumeSerial(const char *path) {
#ifdef _WIN32
  wchar_t *root = ScWideVolumeRoot(path);
  if (!root)
    return 0;
  DWORD serial;
  bool found = GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) != FALSE;
  DWORD error = GetLastError();
  SDL_free(root);
  SetLastError(error);
  return found ? serial : 0;
#else
  ScString native = ScExistingVolumePath(path);
  struct stat info;
  return !native.Strlen() || stat(native.Data(), &info) ? 0 : u32(info.st_dev);
#endif
}

// Query the real per-component name limit on the containing volume, including a new save-file pathname.
u32 ScNativeComponentLimit(const char *path) {
#ifdef _WIN32
  wchar_t *root = ScWideVolumeRoot(path);
  if (!root)
    return 0;
  DWORD limit;
  bool found = GetVolumeInformationW(root, nullptr, 0, nullptr, &limit, nullptr, nullptr, 0) != FALSE;
  DWORD error = GetLastError();
  SDL_free(root);
  SetLastError(error);
  return found ? limit : 0;
#else
  ScString native = ScExistingVolumePath(path);
  if (!native.Strlen())
    return 0;
  errno = 0;
  long limit = pathconf(native.Data(), _PC_NAME_MAX);
  return limit < 0 ? 0 : u32(limit);
#endif
}

#if defined(__linux__)
// This private byte persists the game's writable hidden bit without changing a Linux filename or foreign xattrs.
static constexpr char kScHiddenAttributeName[] = "user.sc3.hidden";

// Distinguish an absent hidden flag from an unavailable/malformed attribute so neither is silently overwritten.
static bool ScReadLinuxHidden(const char *path, u8 *value, bool *present) {
  ssize_t size = getxattr(path, kScHiddenAttributeName, value, sizeof(*value));
  if (size < 0) {
    if (errno != ENODATA)
      return false;
    *value = 0;
    *present = false;
    return true;
  }
  if (size != sizeof(*value) || *value > 1) {
    errno = EINVAL;
    return false;
  }
  *present = true;
  return true;
}
#endif

// Query native attributes at the path boundary; failed queries retain Windows' all-bits-set sentinel.
// POSIX dot-prefixed names remain unchanged and do not alias this separately writable game attribute.
u32 ScGetNativeFileAttributes(const char *path) {
  ScString native = ScNativeFilePath(path);
  const char *target = native.Data();
#ifdef _WIN32
  auto *wide = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", target, strlen(target) + 1));
  if (!wide) {
    errno = EILSEQ;
    SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    return kScFileAttributesInvalid;
  }
  u32 attributes = GetFileAttributesW(wide);
  DWORD error = GetLastError();
  SDL_free(wide);
  SetLastError(error);
  return attributes;
#else
  struct stat info;
  if (stat(target, &info))
    return kScFileAttributesInvalid;
  u32 attributes = 0;
  if (!(info.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
    attributes |= kScFileAttributeReadOnly;
#ifdef __APPLE__
  if (info.st_flags & UF_HIDDEN)
    attributes |= kScFileAttributeHidden;
#elif defined(__linux__)
  u8 hidden;
  bool present;
  if (!ScReadLinuxHidden(target, &hidden, &present))
    return kScFileAttributesInvalid;
  if (hidden)
    attributes |= kScFileAttributeHidden;
#else
  errno = ENOTSUP;
  return kScFileAttributesInvalid;
#endif
  return attributes;
#endif
}

// Apply only read-only/hidden metadata on POSIX, preserving other mode/flag bits, ownership and file contents.
// Hidden updates use Darwin UF_HIDDEN or a private Linux xattr; failures remain recoverable native failures.
i32 ScSetNativeFileAttributes(const char *path, u32 attributes) {
  ScString native = ScNativeFilePath(path);
  const char *target = native.Data();
#ifdef _WIN32
  auto *wide = reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", target, strlen(target) + 1));
  if (!wide) {
    errno = EILSEQ;
    SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    return 0;
  }
  i32 result = SetFileAttributesW(wide, attributes);
  DWORD error = GetLastError();
  SDL_free(wide);
  SetLastError(error);
  return result;
#else
  if (attributes & ~(kScFileAttributeReadOnly | kScFileAttributeHidden)) {
    errno = EINVAL;
    return 0;
  }
  struct stat info;
  if (stat(target, &info))
    return 0;
  bool hidden = (attributes & kScFileAttributeHidden) != 0;
#ifdef __APPLE__
  u32 flags = hidden ? info.st_flags | UF_HIDDEN : info.st_flags & ~UF_HIDDEN;
  bool changed_hidden = flags != info.st_flags;
#elif defined(__linux__)
  u8 old_hidden;
  bool present;
  if (!ScReadLinuxHidden(target, &old_hidden, &present))
    return 0;
  bool changed_hidden = hidden != (old_hidden != 0);
#else
  errno = ENOTSUP;
  return 0;
#endif
  mode_t mode = info.st_mode;
  if (attributes & kScFileAttributeReadOnly)
    mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
  else if (!(mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
    mode |= S_IWUSR;
  // Restore requested write access before a Linux xattr update; enabling read-only must happen afterward.
  bool mode_first = mode != info.st_mode && !(attributes & kScFileAttributeReadOnly);
  if (mode_first && chmod(target, mode))
    return 0;
  i32 hidden_result = 0;
#ifdef __APPLE__
  if (changed_hidden)
    hidden_result = chflags(target, flags);
#elif defined(__linux__)
  if (changed_hidden) {
    u8 value = 1;
    hidden_result = hidden ? setxattr(target, kScHiddenAttributeName, &value, sizeof(value), 0)
                           : removexattr(target, kScHiddenAttributeName);
  }
#endif
  if (hidden_result) {
    int error = errno;
    if (mode_first)
      chmod(target, info.st_mode);
    errno = error;
    return 0;
  }
  if (mode_first || chmod(target, mode) == 0)
    return 1;
  int error = errno;
#ifdef __APPLE__
  if (changed_hidden)
    chflags(target, info.st_flags);
#elif defined(__linux__)
  if (changed_hidden) {
    if (present)
      setxattr(target, kScHiddenAttributeName, &old_hidden, sizeof(old_hidden), 0);
    else
      removexattr(target, kScHiddenAttributeName);
  }
#endif
  errno = error;
  return 0;
#endif
}

// Remove a directory, recursively removing its children only when the initial removal requires it.
bool ScDirectory::RemoveDirectoryA(const ScString &path) {
  ScString native = ScNativeFilePath(path.Data());
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(native.Data(), &info) || info.type != SDL_PATHTYPE_DIRECTORY)
    return false;
  if (ScRemoveNativeDirectory(native.Data()))
    return true;
  ScDirectory directory(path, false, false, false);
  ScString prefix(path);
  if (prefix.begin == prefix.end || (prefix.end[-1] != '/' && prefix.end[-1] != '\\'))
    prefix.Append("/");
  std::list<ScString> names;
  if (directory.ReadDirectoryDirectoryEntriesIntoStringList(&names, 0))
    for (const ScString &name : names) {
      ScString child(prefix);
      child.Append(name.Data(), name.Strlen());
      if (!RemoveDirectoryA(child))
        return false;
    }
  names.clear();
  if (directory.ReadDirectoryFileEntriesIntoStringList(&names))
    for (const ScString &name : names) {
      ScString child(prefix);
      child.Append(name.Data(), name.Strlen());
      if (!ScRemoveNativeFile(child.Data()))
        return false;
    }
  return ScRemoveNativeDirectory(native.Data());
}

// Create missing path components in order and remove the newly created components if any creation fails.
bool ScDirectory::CreateDirectoryA(const ScString &path) {
  ScString input = ScNativeFilePath(path.Data()), current;
  if (input.begin == input.end)
    return false;
  if (input.end[-1] != '/')
    input.Append("/");
  std::list<ScString> created;
  for (const char *byte = input.begin; byte != input.end; ++byte) {
    current.Append(byte, 1);
    if (*byte == '/' && !DoesDirectoryExist(current)) {
      if (!SDL_CreateDirectory(current.Data())) {
        for (const ScString &entry : created)
          ScRemoveNativeDirectory(entry.Data());
        return false;
      }
      created.push_back(current);
    }
  }
  return true;
}

// Borrow the stored entry name as a character buffer.
ScDirectoryEntry::operator const char *() const {
  return name.begin;
}

// Copy the part of the entry name after its final dot, or clear the output when there is no dot.
u32 ScDirectoryEntry::GetEntryExtension(ScString *out) {
  u32 length = name.ScString::Strlen(), position = length;
  while (position && name.begin[position - 1] != '.')
    --position;
  if (!position) {
    out->Erase(0, kScStringToEnd);
    return 0;
  }
  ScString result(name.begin + position, length - position);
  *out = result;
  return out->ScString::Strlen();
}

// Copy the entry name before its final dot, retaining the complete name when no dot exists.
u32 ScDirectoryEntry::GetEntryMinusExtension(ScString *out) {
  u32 length = name.ScString::Strlen(), position = length;
  while (position && name.begin[position - 1] != '.')
    --position;
  ScString result(name.begin, position ? position - 1 : length);
  *out = result;
  return out->ScString::Strlen();
}

void ScDirectoryEntry::GetFullPath(ScString *out) {
  if (owner)
    *out = owner->directory;
  else
    out->Erase(0, kScStringToEnd);
  out->Append(name.begin, name.ScString::Strlen());
}

bool ScDirectoryEntry::IsMediaRemovable() {
  return false;
}

u32 ScDirectoryEntry::GetSize() {
  return 0;
}

u32 ScDirectoryEntry::GetCreationDate() {
  return 0;
}

u32 ScDirectoryEntry::GetLastModificationDate() {
  return 0;
}

u32 ScDirectoryEntry::GetSerialNumber() {
  return 0;
}

bool ScDirectoryEntry::CreatePhysicalEntry() {
  return false;
}

ScDirectoryEntries::ScDirectoryEntries() : head(new ScDirectoryLink) {
  head->next = head;
  head->previous = head;
}

ScDirectoryEntries::~ScDirectoryEntries() {
  Clear();
  delete head;
}

// Delete the current entry nodes in forward order while preserving the sentinel identity.
void ScDirectoryEntries::Clear() {
  ScDirectoryLink *node = head->next;
  while (node != head) {
    ScDirectoryLink *next = node->next;
    delete static_cast<ScDirectoryNode *>(node);
    node = next;
  }
  head->next = head;
  head->previous = head;
}

// Insert an owned name and its actual directory receiver immediately before the selected stable link.
void ScDirectoryEntries::Insert(ScDirectoryLink *before, ScDirectory *owner, const char *name, i32 kind) {
  ScDirectoryNode *node = new ScDirectoryNode;
  node->entry.owner = owner;
  node->entry.name.FromChar(name);
  node->entry.kind = kind;
  node->next = before;
  node->previous = before->previous;
  before->previous->next = node;
  before->previous = node;
}

// Search only real nodes for the source's exact case-sensitive special-directory name.
bool ScDirectoryEntries::Contains(const char *name) const {
  for (ScDirectoryLink *node = head->next; node != head; node = node->next) {
    if (!strcmp(static_cast<ScDirectoryNode *>(node)->entry.name.ScString::Data(), name))
      return true;
  }
  return false;
}

ScDirectoryNames::~ScDirectoryNames() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
}

void ScDirectoryNames::Append(const char *name) {
  *entries.Append() = new ScString(name);
}

// Place a synthesized special-directory name before all previously collected outputs.
void ScDirectoryNames::Prepend(const char *name) {
  ScString *value = new ScString(name);
  entries.Append();
  for (u32 i = entries.size - 1; i; --i)
    entries.data[i] = entries.data[i - 1];
  entries.data[0] = value;
}

// Sort by the source String's full byte ordering, retaining equal-name order and each String object's identity.
void ScDirectoryNames::Sort() {
  for (u32 i = 1; i < entries.size; ++i) {
    ScString *value = entries.data[i];
    u32 j = i;
    while (j) {
      ScString *other = entries.data[j - 1];
      u32 first = value->ScString::Strlen(), second = other->ScString::Strlen();
      int order = memcmp(value->ScString::Data(), other->ScString::Data(), first < second ? first : second);
      if (order > 0 || (!order && first >= second))
        break;
      entries.data[j] = other;
      --j;
    }
    entries.data[j] = value;
  }
}

// Capture the directory and default filter, normalize its trailing separator, then perform the optional initial scan.
ScDirectory::ScDirectory(const ScString &path, bool scan, bool parent, bool current)
    : directory(path.ScString::Data(), path.ScString::Strlen()), filter("*.*"),
      allow_parent(parent), allow_current(current) {
  u32 length = directory.ScString::Strlen();
  if (length && directory.ScString::Data()[length - 1] != '/' && directory.ScString::Data()[length - 1] != '\\')
    directory.Append("\\");
  if (scan)
    ReadDirectoryEntries();
}

// Retire the filter and directory before the stable entry list, matching actual member destruction order.
ScDirectory::~ScDirectory() = default;

bool ScDirectory::IsDirectoryRoot() {
  return directory.ScString::Strlen() == 1 && (directory.begin[0] == '/' || directory.begin[0] == '\\');
}

// Borrow the stored directory's character buffer.
ScDirectory::operator const char *() const {
  return directory.begin;
}

ScString ScDirectory::GetPath() {
  return ScString(directory);
}

// Return the indexed retained entry, or null when the index is outside the snapshot.
ScDirectoryEntry *ScDirectory::GetNthEntry(u32 index) {
  ScDirectoryLink *node = entries.head->next;
  while (node != entries.head && index) {
    node = node->next;
    --index;
  }
  return node == entries.head ? nullptr : &static_cast<ScDirectoryNode *>(node)->entry;
}

void ScDirectory::MakeSurePathEndsWithSeparator() {
  u32 size = directory.ScString::Strlen();
  if (size && directory.begin[size - 1] != '\\' && directory.begin[size - 1] != '/')
    directory.Append("\\");
}

// Replace the directory path, normalize its separator and optionally rebuild the retained entries.
bool ScDirectory::ChangeDirectory(const ScString &path, bool scan) {
  directory = path;
  MakeSurePathEndsWithSeparator();
  if (scan)
    ReadDirectoryEntries();
  return true;
}

// Find an exact concrete string match in the retained directory entries.
bool ScDirectory::DoesDirectoryListContainEntry(const ScString &name) {
  u32 length = name.ScString::Strlen();
  for (ScDirectoryLink *node = entries.head->next; node != entries.head; node = node->next) {
    const ScString &entry = static_cast<ScDirectoryNode *>(node)->entry.name;
    if (entry.ScString::Strlen() == length && !memcmp(entry.begin, name.begin, length))
      return true;
  }
  return false;
}

// Ask Windows whether any filesystem entry matches the supplied search pattern.
bool ScDirectory::DoesAnyEntryExistThatMatchesPattern(const ScString &pattern) {
  ScString native = ScNativeFilePath(pattern.Data()), directory, filter;
  const char *separator = strrchr(native.Data(), '/');
  if (separator) {
    directory.FromChar(native.Data(), u32(separator - native.Data()) + 1);
    filter.FromChar(separator + 1);
  } else {
    directory.FromChar(".");
    filter = native;
  }
  int count = 0;
  char **names = ScGlobLocalDirectory(directory, filter, &count);
  if (!names)
    return false;
  bool found = count != 0 || !strcmp(filter.Data(), "*") || !strcmp(filter.Data(), "*.*");
  SDL_free(names);
  return found;
}

// Check for an entry beneath the supplied directory using the original wildcard search.
bool ScDirectory::DoesDirectoryExist(const ScString &path) {
  ScString pattern(path);
  u32 size = pattern.ScString::Strlen();
  if (!size || (pattern.begin[size - 1] != '\\' && pattern.begin[size - 1] != '/'))
    pattern.Append("\\");
  pattern.Append("*");
  return DoesAnyEntryExistThatMatchesPattern(pattern);
}

// Sort the original output list by the concrete string's byte ordering.
void ScDirectory::AlphabetizeEntryList(std::list<ScString> *values) {
  values->sort([](const ScString &first, const ScString &second) {
    u32 first_size = first.ScString::Strlen(), second_size = second.ScString::Strlen();
    int order = memcmp(first.begin, second.begin, first_size < second_size ? first_size : second_size);
    return order < 0 || (!order && first_size < second_size);
  });
}

// Append matching directory and file names to the caller's original String list.
bool ScDirectory::ReadDirectorySpecificEntriesIntoStringList(std::list<ScString> *output, u32 flags) {
  int count = 0;
  char **names = ScGlobLocalDirectory(directory, filter, &count);
  if (!names)
    return false;
  bool all = !strcmp(filter.Data(), "*.*");
  if (!count && !all) {
    SDL_free(names);
    return false;
  }
  for (int i = 0; i < count; ++i)
    if (flags & (ScLocalEntryIsDirectory(directory, names[i]) ? 4 : 8))
      output->emplace_back(names[i]);
  SDL_free(names);
  if (all && (flags & 2))
    output->emplace_front("..");
  if (all && (flags & 1))
    output->emplace_front(".");
  return true;
}

bool ScDirectory::ReadDirectoryFileEntriesIntoStringList(std::list<ScString> *output) {
  return ReadDirectorySpecificEntriesIntoStringList(output, 8);
}

bool ScDirectory::ReadDirectoryDirectoryEntriesIntoStringList(std::list<ScString> *output, u32 flags) {
  return ReadDirectorySpecificEntriesIntoStringList(output, (flags & 3) | 4);
}

// Count the current snapshot nodes; a later independent name enumeration does not replace this retained inventory.
u32 ScDirectory::GetDirectoryEntryCount() {
  u32 count = 0;
  for (ScDirectoryLink *node = entries.head->next; node != entries.head; node = node->next)
    ++count;
  return count;
}

void ScDirectory::SetNewDirectoryEntryFilter(const ScString &value, bool scan) {
  filter = value;
  if (!filter.ScString::Strlen()) {
    ScString wildcard("*.*");
    filter = wildcard;
  }
  if (scan)
    ReadDirectoryEntries();
}

// Replace the retained entry snapshot from the original Windows enumeration, preserving dot-entry inclusion flags.
bool ScDirectory::ReadDirectoryEntries() {
  entries.Clear();
  int count = 0;
  char **names = ScGlobLocalDirectory(directory, filter, &count);
  if (!names)
    return false;
  bool all = !strcmp(filter.Data(), "*.*");
  if (!count && !all) {
    SDL_free(names);
    return false;
  }
  for (int i = 0; i < count; ++i)
    entries.Insert(entries.head, this, names[i], ScLocalEntryIsDirectory(directory, names[i]) ? 4 : 8);
  SDL_free(names);
  if (all && allow_parent)
    entries.Insert(entries.head->next, this, "..", 4);
  if (all && allow_current)
    entries.Insert(entries.head->next, this, ".", 4);
  return true;
}

// Append a fresh name enumeration without replacing the retained entry inventory or clearing the caller's snapshot.
bool ScDirectory::ReadNames(ScDirectoryNames *output, u32 flags) {
  int count = 0;
  char **names = ScGlobLocalDirectory(directory, filter, &count);
  if (!names)
    return false;
  bool all = !strcmp(filter.Data(), "*.*");
  if (!count && !all) {
    SDL_free(names);
    return false;
  }
  for (int i = 0; i < count; ++i)
    if (flags & (ScLocalEntryIsDirectory(directory, names[i]) ? 4 : 8))
      output->Append(names[i]);
  SDL_free(names);
  if (all && (flags & 2))
    output->Prepend("..");
  if (all && (flags & 1))
    output->Prepend(".");
  return true;
}

#include <stdlib.h>



// Resolve a drive-relative path using its current drive directory or a rooted drive prefix.
char *ScFullpath(char *output, const char *input, u32 capacity) {
  ScString full = ScAbsoluteLocalPath(input);
  if (full.begin == full.end)
    return nullptr;
  usize bytes = usize(full.Strlen()) + 1;
  if (output) {
    if (bytes > capacity) {
      errno = ERANGE;
      return nullptr;
    }
  } else {
    output = static_cast<char *>(malloc(bytes));
    if (!output)
      ScFatal(SC_ERROR_NOMEM, "Out of memory expanding a local path");
  }
  memcpy(output, full.Data(), bytes);
  return output;
}

// Copy one split component through an independent String before assigning its destination cache.
// SDL retains complete UTF-8 components; reference builds retain 255-byte truncation and temporary retirement order.
static void ScAssignPathPart(ScString *output, const char *first, const char *last) {
  ScString value(first, u32(last - first));
  *output = value;
}

ScPath::ScPath() : concat(0), split(0) {
}

// Copy the four components and mark them valid while leaving the joined spelling to be built lazily.
ScPath::ScPath(const ScString &drive_value, const ScString &directory_value, const ScString &name_value,
    const ScString &extension_value)
    : concat(0), drive(drive_value), directory(directory_value), file_name(name_value), extension(extension_value),
      split(1) {
}

ScPath::ScPath(const ScPath &source)
    : path(source.path), concat(source.concat), drive(source.drive), directory(source.directory),
      file_name(source.file_name), extension(source.extension), split(source.split) {
}

ScPath::ScPath(const ScString &value)
    : path(value.ScString::Data(), value.ScString::Strlen()), concat(1), split(0) {
}

// Retire extension, filename, directory and drive caches before the owned full-path String.
ScPath::~ScPath() = default;

// Return the mutable full path after joining cached components, invalidating the component cache.
ScString *ScPath::Path() {
  if (!concat)
    ImplementMakePath();
  split = 0;
  return &path;
}

// Expose the drive after lazy splitting and invalidate the joined spelling before it can be changed.
ScString *ScPath::Drive() {
  if (!split)
    ImplementSplitPath();
  concat = 0;
  return &drive;
}

// Borrow the cached drive after splitting the full spelling when necessary.
const ScString &ScPath::Drive() const {
  if (!split)
    ImplementSplitPath();
  return drive;
}

// Borrow the cached directory after splitting the full spelling when necessary.
const ScString &ScPath::Directory() const {
  if (!split)
    ImplementSplitPath();
  return directory;
}

// Rebuild the full spelling only when a mutable component has invalidated it, retaining split-cache validity.
const ScString &ScPath::Path() const {
  if (!concat)
    ImplementMakePath();
  return path;
}

// Expose the directory component after lazy splitting, invalidating the full spelling before a caller can mutate it.
ScString &ScPath::Directory() {
  if (!split)
    ImplementSplitPath();
  concat = 0;
  return directory;
}

// Expose the filename component after lazy splitting and require rebuilding the next full-path read.
ScString &ScPath::FileName() {
  if (!split)
    ImplementSplitPath();
  concat = 0;
  return file_name;
}

// Populate missing component caches before borrowing the stored filename without its extension.
const ScString &ScPath::FileName() const {
  if (!split)
    ImplementSplitPath();
  return file_name;
}

// Populate missing component caches before borrowing the stored extension, including its leading dot.
const ScString &ScPath::Extension() const {
  if (!split)
    ImplementSplitPath();
  return extension;
}

void ScPath::SplitPath() {
  ImplementSplitPath();
}

void ScPath::SplitPath(const ScString &value) {
  *Path() = value;
}

void ScPath::MakePath() {
  ImplementMakePath();
}

// Replace the four components in source order and invalidate the joined spelling.
void ScPath::MakePath(const ScString &drive_value, const ScString &directory_value, const ScString &name_value,
    const ScString &extension_value) {
  concat = 0;
  drive = drive_value;
  directory = directory_value;
  file_name = name_value;
  extension = extension_value;
  split = 1;
}

void ScPath::FullPath(const ScString &value) {
  *Path() = value;
  FullPath();
}

// Join pending components before expanding the current path spelling.
void ScPath::FullPath() {
  if (!concat)
    ImplementMakePath();
  ImplementFullPath();
}

// Expand the stored spelling through the executable's path routine and replace the cached full path.
void ScPath::ImplementFullPath() const {
  ScString full = ScAbsoluteLocalPath(path.Data());
  if (full.begin != full.end)
    path = full;
}

// Split the null-terminated path at its optional drive, last separator and last filename dot.
// Commit four separately owned components in source order, then mark their shared cache valid.
void ScPath::ImplementSplitPath() const {
  const char *first = path.ScString::Data(), *start = first;
#if defined(_WIN32)
  if (*first && first[1] == ':')
    start += 2;
#endif
  const char *end = start, *file = start, *dot = nullptr;
  while (*end) {
    if (*end == '/' || *end == '\\')
      file = end + 1;
    else if (*end == '.')
      dot = end;
    ++end;
  }
  ScAssignPathPart(&drive, first, start);
  ScAssignPathPart(&directory, start, file);
  if (dot && dot >= file) {
    ScAssignPathPart(&file_name, file, dot);
    ScAssignPathPart(&extension, dot, end);
  } else {
    ScAssignPathPart(&file_name, file, end);
    ScAssignPathPart(&extension, end, end);
  }
  split = 1;
}

// Join cached components with the source drive, directory and extension separators, then validate the full spelling.
void ScPath::ImplementMakePath() const {
  ScString result;
  if (*drive.ScString::Data()) {
    result.Append(drive.ScString::Data(), 1);
    result.Append(":");
  }
  const char *directory_bytes = directory.ScString::Data();
  usize length = strlen(directory_bytes);
  if (length) {
    result.Append(directory_bytes);
    if (directory_bytes[length - 1] != '/' && directory_bytes[length - 1] != '\\')
      result.Append("/");
  }
  result.Append(file_name.ScString::Data());
  const char *extension_bytes = extension.ScString::Data();
  if (*extension_bytes && *extension_bytes != '.')
    result.Append(".");
  result.Append(extension_bytes);
  for (char *byte = result.begin; byte != result.end; ++byte)
    if (*byte == '\\')
      *byte = '/';
  path = result;
  concat = 1;
}

// Probe the full path, appending the source wildcard only when both filename and extension are empty.
// Close a successful search immediately and leave lazy component/full-path caches in their source states.
bool ScPath::Exists() const {
  ScString query(Path());
  if (!FileName().ScString::Strlen() && !Extension().ScString::Strlen())
    query.Append("*.*");
  return ScDirectory::DoesAnyEntryExistThatMatchesPattern(query);
}
