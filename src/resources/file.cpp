#include "resources/file.h"

#include <assert.h>

#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <share.h>
#include <wchar.h>
#include <windows.h>
#else
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include <string.h>

#include "application/application_core.h"
#include "common/api.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_qfs.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "resources/api.h"
#include "resources/indexed_file.h"
#include "resources/layered_file.h"
#include "resources/manager_object.h"
#include "resources/miff_segment.h"
#include "resources/packed_segment.h"
#include "resources/resource.h"

// The service constructor supplies this original startup ordering priority.
enum RsFileSystemOrder : i32 {
  kRsFileSystemPriority = 0x1e8480,
};

// The MIFF header stores this four-byte signature before its version and directory words.
enum RsMiffTag : u32 {
  kRsMiffSignature = 0x4646494d,
};

// Layer-directory records use distinct type and group fields with the same installed value.
enum RsLayerDirectoryRecordKey : u32 {
  kRsLayerDirectoryRecordType = 0x35f62a4,
  kRsLayerDirectoryRecordGroup = 0x35f62a4,
};


static ScString RsNativeFilePath(const char *path) {
  return ScNativeFilePath(path);
}

#ifdef _WIN32
// Convert the native UTF-8 path once for Windows CRT filesystem calls, retaining errno across buffer retirement.
struct RsWideFilePath {
  wchar_t *text;

  // SDL owns the conversion buffer; an invalid UTF-8 filename remains a recoverable filesystem error.
  explicit RsWideFilePath(const char *path)
      : text(reinterpret_cast<wchar_t *>(SDL_iconv_string("UTF-16LE", "UTF-8", path, strlen(path) + 1))) {
    if (!text)
      errno = EILSEQ;
  }

  // Preserve the failing CRT operation's error while releasing SDL's conversion storage.
  ~RsWideFilePath() {
    i32 error = errno;
    SDL_free(text);
    errno = error;
  }
};
#endif

// Obtain the actual executable filename through the host's local process interface, without imposing MAX_PATH.
// The cached game components are split only after this native UTF-8 path has been obtained. When the launcher chose an
// installation (SC3_APP_DIR), that installation's SC3U.exe is reported so data lookups follow it.
ScString RsApplicationFilePath() {
  // The launcher resolves the installed game's Apps directory, so the executable need not live beside its data.
  if (const char *apps = SDL_getenv("SC3_APP_DIR")) {
    ScString path(apps);
    while (path.Strlen() && (path.end[-1] == '/' || path.end[-1] == '\\'))
      path.FromChar(path.begin, path.Strlen() - 1);
    path.Append("/SC3U.exe");
    return RsNativeFilePath(path.Data());
  }
#ifdef _WIN32
  u32 size = 512;
  for (;;) {
    wchar_t *path = new wchar_t[size];
    u32 length = GetModuleFileNameW(nullptr, path, size);
    if (!length) {
      delete[] path;
      return ScString();
    }
    if (length < size) {
      char *utf8 = SDL_iconv_string(
          "UTF-8", "UTF-16LE", reinterpret_cast<const char *>(path), (usize(length) + 1) * sizeof(wchar_t));
      delete[] path;
      ScString result;
      if (utf8) {
        result = RsNativeFilePath(utf8);
        SDL_free(utf8);
      }
      return result;
    }
    delete[] path;
    size *= 2;
  }
#elif defined(__APPLE__)
  u32 size = 0;
  _NSGetExecutablePath(nullptr, &size);
  char *path = new char[size];
  bool found = _NSGetExecutablePath(path, &size) == 0;
  ScString result;
  if (found)
    result = RsNativeFilePath(path);
  delete[] path;
  return result;
#else
  usize size = 512;
  for (;;) {
    char *path = new char[size + 1];
    isize length = readlink("/proc/self/exe", path, size);
    if (length < 0) {
      delete[] path;
      return ScString();
    }
    if (usize(length) < size) {
      path[length] = 0;
      ScString result = RsNativeFilePath(path);
      delete[] path;
      return result;
    }
    delete[] path;
    size *= 2;
  }
#endif
}

// Open binary descriptors with the game's creation/access modes; the Windows CRT also preserves its share flags.
static FILE *RsOpenLocalFile(const char *path, i32 access, i32 creation, [[maybe_unused]] i32 share) {
  i32 flags = (access & kRsFileAccessReadWrite) == kRsFileAccessReadWrite ? O_RDWR
      : access & kRsFileAccessWrite                                       ? O_WRONLY
                                                                          : O_RDONLY;
  switch (creation) {
  case kRsFileCreateNew:
    flags |= O_CREAT | O_EXCL;
    break;
  case kRsFileCreateAlways:
    flags |= O_CREAT | O_TRUNC;
    break;
  case kRsFileOpenExisting:
    break;
  case kRsFileOpenAlways:
    flags |= O_CREAT;
    break;
  case kRsFileTruncateExisting:
    flags |= O_TRUNC;
    break;
  default:
    return nullptr;
  }
  i32 descriptor;
#ifdef _WIN32
  i32 sharing = (share & kRsFileShareReadWrite) == kRsFileShareReadWrite ? _SH_DENYNO
      : share & kRsFileShareRead                                         ? _SH_DENYWR
      : share & kRsFileShareWrite                                        ? _SH_DENYRD
                                                                         : _SH_DENYRW;
  RsWideFilePath wide(path);
  if (!wide.text)
    return nullptr;
  descriptor = _wsopen(wide.text, flags | O_BINARY, sharing, _S_IREAD | _S_IWRITE);
#else
  descriptor = open(path, flags, 0666);
#endif
  if (descriptor < 0)
    return nullptr;
  const char *mode = (access & kRsFileAccessReadWrite) == kRsFileAccessReadWrite ? "r+b"
      : access & kRsFileAccessWrite                                              ? "wb"
                                                                                 : "rb";
#ifdef _WIN32
  FILE *file = _fdopen(descriptor, mode);
#else
  FILE *file = fdopen(descriptor, mode);
#endif
  if (!file) {
    i32 error = errno;
#ifdef _WIN32
    _close(descriptor);
#else
    close(descriptor);
#endif
    errno = error;
    return nullptr;
  }
  setvbuf(file, nullptr, _IONBF, 0);
  return file;
}

// Preserve signed byte displacements and report the resulting wide file position before the game narrows it.
static i64 RsSeekLocalFile(FILE *file, i32 distance, i32 origin) {
#ifdef _WIN32
  return _fseeki64(file, distance, origin) ? -1 : _ftelli64(file);
#else
  return fseeko(file, distance, origin) ? -1 : ftello(file);
#endif
}

// Change the file extent through its existing descriptor, retaining the current stream position.
static bool RsResizeLocalFile(FILE *file, i64 size) {
#ifdef _WIN32
  i32 result = _chsize_s(_fileno(file), size);
  if (result)
    errno = result;
  return result == 0;
#else
  return ftruncate(fileno(file), size) == 0;
#endif
}

// Query descriptor metadata without disturbing the stream's current byte position.
static i64 RsLocalFileSize(FILE *file) {
#ifdef _WIN32
  struct _stat64 data;
  return _fstat64(_fileno(file), &data) ? -1 : data.st_size;
#else
  struct stat data;
  return fstat(fileno(file), &data) ? -1 : data.st_size;
#endif
}

static bool RsDeleteLocalFile(const char *path) {
  return ScRemoveNativeFile(path);
}

// Copy bytes between local streams while retaining exclusive destination creation when overwrite is disabled.
static bool RsCopyLocalFile(const char *source, const char *target, bool overwrite) {
  FILE *input = RsOpenLocalFile(source, 1, 2, 3);
  if (!input)
    return false;
  SDL_PathInfo info;
  bool exists = SDL_GetPathInfo(target, &info);
  FILE *output = RsOpenLocalFile(target, 2, overwrite && exists ? 2 : 0, 0);
  if (!output) {
    i32 error = errno;
    fclose(input);
    errno = error;
    return false;
  }
#ifdef _WIN32
  RsWideFilePath sourceWide(source), targetWide(target);
  wchar_t *from = sourceWide.text ? _wfullpath(nullptr, sourceWide.text, 0) : nullptr;
  wchar_t *to = targetWide.text ? _wfullpath(nullptr, targetWide.text, 0) : nullptr;
  bool same = from && to && _wcsicmp(from, to) == 0;
  free(from);
  free(to);
#else
  struct stat from, to;
  bool same = !fstat(fileno(input), &from) && !fstat(fileno(output), &to) && from.st_dev == to.st_dev &&
      from.st_ino == to.st_ino;
#endif
  bool result = !same && RsResizeLocalFile(output, 0);
  if (same)
    errno = EINVAL;
  char bytes[8192];
  while (result) {
    usize count = fread(bytes, 1, sizeof(bytes), input);
    if (count && fwrite(bytes, 1, count, output) != count)
      result = false;
    if (ferror(input))
      result = false;
    if (count < sizeof(bytes))
      break;
  }
  i32 error = errno;
  if (fclose(output) && result) {
    error = errno;
    result = false;
  }
  fclose(input);
  errno = error;
  return result;
}

// Keep the closed-handle representation local to the selected platform backend.
static auto RsClosedFileHandle() {
  return static_cast<FILE *>(nullptr);
}

RsFile::RsFile() : opened(false), handle(RsClosedFileHandle()), references(0), error(kRsFileErrorNone) {
}

RsFile::RsFile(const ScStringView &input)
    : name(input), opened(false), handle(RsClosedFileHandle()), references(0), error(kRsFileErrorNone) {
}

// A null C filename leaves the constructed String empty; otherwise assign through the temporary String.
RsFile::RsFile(const char *input) : RsFile() {
  if (input) {
    ScString temporary(input, (u32)strlen(input));
    name = temporary;
  }
}

RsFile::~RsFile() {
  if (opened)
    RsFile::Close();
}

// Publish this file for either supported interface before the virtual retain.
bool RsFile::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsFile);
  HANDLE_QUERY_INTERFACE(IID_RsFile, RsFile);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsFile::AddRef() {
  return ++references;
}

// Delete at zero or one; only larger counts decrement and return the remaining count.
u32 RsFile::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

bool RsFile::IsOpen() const {
  return opened;
}

// Resolve Shell Links before CreateFile and capture errors on failure or CREATE_ALWAYS/OPEN_ALWAYS success.
bool RsFile::Open(i32 access, i32 creation, i32 share) {
  if (IsOpen())
    return true;
  if (static_cast<u32>(creation) > kRsFileTruncateExisting)
    return false;
  ScString path = RsNativeFilePath(name.Data());
  SDL_PathInfo info;
  bool existed = SDL_GetPathInfo(path.Data(), &info);
  errno = 0;
  handle = RsOpenLocalFile(path.Data(), access, creation, share);
  if (!handle) {
    SaveLastError();
    return false;
  }
  opened = true;
  if (creation == kRsFileCreateAlways || creation == kRsFileOpenAlways) {
    error = existed ? kRsFileErrorAlreadyExists : 0;
    error_text.FromChar(existed ? strerror(EEXIST) : "");
  }
  return true;
}

// Clear the open flag before closing; preserve true for an initially open file even when CloseHandle fails.
bool RsFile::Close() {
  if (!opened)
    return false;
  opened = false;
  if (fclose(handle))
    SaveLastError();
  handle = nullptr;
  return true;
}

// Query the low current position and capture the source sentinel error without a high-word query.
u32 RsFile::Position() {
  if (!opened)
    return UINT32_MAX;
  i64 result = RsSeekLocalFile(handle, 0, SEEK_CUR);
  if (result < 0 || u32(result) == UINT32_MAX)
    SaveLastError();
  return u32(result);
}

u32 RsFile::Size() {
  if (!opened)
    return UINT32_MAX;
  i64 result = RsLocalFileSize(handle);
  if (result < 0 || u32(result) == UINT32_MAX)
    SaveLastError();
  return u32(result);
}

// Interpret the DWORD length through the Win32 signed low-word seek, then truncate after a successful seek.
bool RsFile::Resize(u32 size) {
  if (!opened)
    return false;
  if (RsSeekLocalFile(handle, i32(size), SEEK_SET) < 0 || !RsResizeLocalFile(handle, i32(size))) {
    SaveLastError();
    return false;
  }
  return true;
}

u32 RsFile::SeekBegin() {
  return Seek(0, kRsSeekBegin);
}

u32 RsFile::SeekEnd() {
  return Seek(0, kRsSeekEnd);
}

u32 RsFile::SeekRelative(i32 distance) {
  return Seek(distance, kRsSeekCurrent);
}

u32 RsFile::SeekAbsolute(u32 offset) {
  return Seek(static_cast<i32>(offset), kRsSeekBegin);
}

// Unknown origins query the live virtual position; valid origins use the supplied signed displacement.
u32 RsFile::Seek(i32 distance, i32 origin) {
  if (!opened)
    return UINT32_MAX;
  if (u32(origin) > kRsSeekEnd)
    return Position();
  i32 nativeOrigin = origin == kRsSeekBegin ? SEEK_SET : origin == kRsSeekCurrent ? SEEK_CUR : SEEK_END;
  i64 result = RsSeekLocalFile(handle, distance, nativeOrigin);
  if (result < 0 || u32(result) == UINT32_MAX)
    SaveLastError();
  return u32(result);
}

// A successful short read leaves its written prefix but returns false, without assigning an error code.
bool RsFile::Read(void *out, u32 size) {
  if (!opened)
    return false;
  clearerr(handle);
  errno = 0;
  usize count = fread(out, 1, size, handle);
  if (ferror(handle)) {
    SaveLastError();
    return false;
  }
  return count == size;
}

// Read the full word consumed by packaged INI readers, preserving bytes supplied even when I/O reports failure.
// Keep incomplete external input outside the original consumer's defined domain without inventing a normal value.
u32 RsReadPackageWord(RsFile *file) {
  u32 value, count = 4;
  bool opened = file->opened;
  file->Read(&value, &count);
  if (!opened || count != 4) {
    assert(false && "The original packaged INI word would consume undefined storage");
    abort();
  }
  return value;
}

// This overload passes the genuine in/out count directly to Windows and returns normalized BOOL success.
bool RsFile::Read(void *out, u32 *size) {
  if (!opened)
    return false;
  clearerr(handle);
  errno = 0;
  *size = u32(fread(out, 1, *size, handle));
  if (ferror(handle)) {
    SaveLastError();
    return false;
  }
  return true;
}

// Write the complete requested buffer; only an API failure updates the file's error field.
bool RsFile::Write(const void *bytes, u32 size) {
  if (!opened)
    return false;
  clearerr(handle);
  errno = 0;
  usize count = fwrite(bytes, 1, size, handle);
  if (ferror(handle)) {
    SaveLastError();
    return false;
  }
  return count == size;
}

// Preserve the mutable count argument and the API's ordinary Boolean success rather than requiring a complete write.
bool RsFile::Write(const void *bytes, u32 *size) {
  if (!opened)
    return false;
  clearerr(handle);
  errno = 0;
  *size = u32(fwrite(bytes, 1, *size, handle));
  if (ferror(handle)) {
    SaveLastError();
    return false;
  }
  return true;
}

// Flush only an open file and capture the Windows failure before returning its normalized Boolean result.
bool RsFile::Flush() {
  if (!opened)
    return false;
  bool result = fflush(handle) == 0;
#ifdef _WIN32
  if (result)
    result = _commit(_fileno(handle)) == 0;
#else
  if (result)
    result = fsync(fileno(handle)) == 0;
#endif
  if (!result)
    SaveLastError();
  return result;
}

// Query attributes through the filename's ToChar view; a missing path captures the ordinary file error.
bool RsFile::Exists() {
  bool result = FileExists(name);
  if (!result)
    SaveLastError();
  return result;
}

// Capture the current Windows error and apply the two source-specific remappings to the stored error field.
void RsFile::SaveLastError() {
  i32 value = errno ? errno : EIO;
  error = value == EACCES ? kRsFileErrorAccessFailure : value == EEXIST ? kRsFileErrorAlreadyExists : u32(value);
  error_text.FromChar(strerror(value));
}

u32 RsResolveFilePathAlias(const char *path, [[maybe_unused]] char *out, [[maybe_unused]] u32 capacity) {
  const char *extension = strrchr(path, '.');
  return extension && !SDL_strcasecmp(extension, ".lnk") ? UINT32_MAX : 0;
}

// Close an open file before deleting its owned filename. Preserve the original Boolean result and capture failure.
bool RsFile::Delete() {
  if (IsOpen())
    Close();
  ScString path = RsNativeFilePath(name.Data());
  bool result = RsDeleteLocalFile(path.Data());
  if (!result)
    SaveLastError();
  return result;
}

// Query length before data and replace the owned filename without implicitly closing its existing handle.
void RsFile::SetName(const ScStringView &source) {
  u32 size = source.Strlen();
  const char *bytes = source.Data();
  name.FromChar(bytes, size);
}

// Query the destination before the source and apply the original overwrite policy through the actual Win32 copy.
bool RsFile::Copy(const ScStringView &source, const ScStringView &target, bool overwrite) {
  ScString destination = RsNativeFilePath(target.ToChar()), input = RsNativeFilePath(source.ToChar());
  return RsCopyLocalFile(input.Data(), destination.Data(), overwrite);
}

// Delete the path supplied through the actual String interface and return the original Boolean success value.
bool RsFile::Delete(const ScStringView &path) {
  ScString native = RsNativeFilePath(path.ToChar());
  return RsDeleteLocalFile(native.Data());
}

// Query the supplied String through its original ToChar operation. Any existing path, including a directory, succeeds.
bool RsFile::FileExists(const ScStringView &path) {
  ScString native = RsNativeFilePath(path.ToChar());
  SDL_PathInfo info;
  return SDL_GetPathInfo(native.Data(), &info);
}

// Reuse the original concrete String across attempts and publish its bytes only after the reserved file is closed.
bool RsFile::MakeTempPathName(ScStringView *out) {
  u32 candidate = u32(time(nullptr));
  ScString name;
  for (u32 attempt = 0; attempt < 10000; ++attempt, --candidate) {
    name.Sprintf("RZ_%d.tmp", i32(candidate));
    FILE *file = RsOpenLocalFile(name.Data(), 1, 0, 3);
    if (!file)
      continue;
    fclose(file);
    out->FromChar(name.Data(), name.ScString::Strlen());
    return true;
  }
  return false;
}

// Try copy-capable MoveFileEx first, then plain MoveFile. A successful fallback permanently disables the extended call.
bool RsFile::Rename(const ScStringView &source, const ScStringView &target) {
  ScString destination = RsNativeFilePath(target.ToChar()), input = RsNativeFilePath(source.ToChar());
  if (ScRenameNativeFile(input.Data(), destination.Data()))
    return true;
  if (errno == EXDEV && RsCopyLocalFile(input.Data(), destination.Data(), false))
    return RsDeleteLocalFile(input.Data());
  return false;
}

// Close before moving the path and capture move failure before replacing the owned name, even when the move failed.
bool RsFile::Rename(const ScStringView &target) {
  if (IsOpen())
    Close();
  bool result = RsFile::Rename(name, target);
  if (!result)
    SaveLastError();
  ScString copy(target);
  name = copy;
  return result;
}

// Return the borrowed embedded filename String. This tiny offset-four getter is folded with unrelated class accessors.
const ScStringView *RsFile::GetName() const {
  return &name;
}

// Copy the supplied path to the owned destination name, then capture the platform error only when copying failed.
bool RsFile::Copy(const ScStringView &source, bool overwrite) {
  bool result = RsFile::Copy(source, name, overwrite);
  if (!result)
    SaveLastError();
  return result;
}

// Snapshot the source file's live borrowed path before copying it to this filename. Destroy that temporary String
// before capturing a failed copy's platform error, preserving the original cleanup and observation order.
bool RsFile::Copy(const RsFileInterface &source, bool overwrite) {
  bool result;
  {
    ScString input(*source.GetName());
    result = RsFile::Copy(input, name, overwrite);
  }
  if (!result)
    SaveLastError();
  return result;
}

void RsFile::GetName(ScStringView *out) const {
  out->FromChar(name.begin, static_cast<u32>(name.end - name.begin));
}

// Borrow the original filename byte pointer directly, without retaining or copying the embedded String.
const char *RsFile::GetNameData() const {
  return name.begin;
}

void RsFile::SetName(const char *source) {
  ScString temporary(source);
  name = temporary;
}

// Extract only the filename and extension effects used by the original pathname helpers, clamping each to255 bytes.
static void RsSplitFileName(const char *path, char *name, char *extension) {
  const char *start = path, *separator = nullptr, *dot = nullptr;
  if (*path && path[1] == ':')
    start += 2;
  const char *end = start;
  for (; *end; ++end)
    if (*end == '\\' || *end == '/')
      separator = end + 1;
    else if (*end == '.')
      dot = end;
  if (separator)
    start = separator;
  usize nameSize = (dot && dot >= start ? dot : end) - start, extensionSize = 0;
  if (dot && dot >= start)
    extensionSize = (usize)(end - dot);
  if (nameSize > 255)
    nameSize = 255;
  if (extensionSize > 255)
    extensionSize = 255;
  memcpy(name, start, nameSize);
  name[nameSize] = 0;
  if (extensionSize)
    memcpy(extension, dot, extensionSize);
  extension[extensionSize] = 0;
}

// Copy the path's last extension, including its dot, through the output String even when empty. Return whether
// the extracted extension had bytes, retaining that result across the output callback.
bool RsFile::GetPathExtension(const ScStringView &path, ScStringView *out) {
  char filename[256], extension[256];
  RsSplitFileName(path.ToChar(), filename, extension);
  u32 length = static_cast<u32>(strlen(extension));
  out->FromChar(extension, length);
  return length != 0;
}

// Build the clamped basename in the original temporary-String order, optionally append its extension, then copy
// the complete range through the output interface. Empty names are still copied before returning false.
bool RsFile::GetPathName(const ScStringView &path, ScStringView *out, bool includeExtension) {
  char filename[256], extension[256];
  ScString result;
  RsSplitFileName(path.ToChar(), filename, extension);
  {
    ScString part(filename);
    result = part;
  }
  if (includeExtension)
    result.Append(extension);
  out->FromChar(result.begin, result.Strlen());
  return result.Strlen() != 0;
}

// Retain the original ASCII splitter's five output effects, including independent 255-byte component limits.
void RsSplitFilePath(const char *path, char *drive, char *directory, char *name, char *extension) {
  const char *start = path, *separator = nullptr, *dot = nullptr;
  drive[0] = 0;
  if (*path && path[1] == ':') {
    memcpy(drive, path, 2);
    drive[2] = 0;
    start += 2;
  }
  const char *end = start;
  for (; *end; ++end)
    if (*end == '\\' || *end == '/')
      separator = end + 1;
    else if (*end == '.')
      dot = end;
  usize length = separator ? (usize)(separator - start) : 0;
  if (length > 255)
    length = 255;
  memcpy(directory, start, length);
  directory[length] = 0;
  if (separator)
    start = separator;
  length = (usize)((dot && dot >= start ? dot : end) - start);
  if (length > 255)
    length = 255;
  memcpy(name, start, length);
  name[length] = 0;
  length = dot && dot >= start ? (usize)(end - dot) : 0;
  if (length > 255)
    length = 255;
  if (length)
    memcpy(extension, dot, length);
  extension[length] = 0;
}

// Publish the containing directory through the original String output, including an empty result when absent.
// SDL retains the full native UTF-8 range; reference builds retain the original clamped drive-plus-directory split.
bool RsFile::GetPathDirectory(const ScStringView &path, ScStringView *out) {
  ScString native = RsNativeFilePath(path.ToChar()), result;
  const char *separator = strrchr(native.Data(), '/');
  if (separator)
    result.FromChar(native.Data(), u32(separator + 1 - native.Data()));
  else if (native.Strlen() >= 2 && native.begin[1] == ':')
    result.FromChar(native.Data(), 2);
  out->FromChar(result.Data(), result.Strlen());
  return result.begin != result.end;
}

// Publish the native drive prefix, with an empty drive on POSIX paths.
// Reference builds retain the original counted-NUL result when the Windows splitter finds no drive.
bool RsFile::GetPathDrive(const ScStringView &path, ScStringView *out) {
  ScString native = RsNativeFilePath(path.ToChar()), result;
  if (native.Strlen() >= 2 && native.begin[1] == ':') {
    result.FromChar(native.begin, 2);
    result.Append("/");
  }
  out->FromChar(result.Data(), result.Strlen());
  return result.begin != result.end;
}

// Resolve the drive before querying its volume information, then publish the label only after Win32 succeeds.
// Keep the temporary root alive across the caller's output assignment and preserve failure output.
bool RsFile::GetVolumeName(const ScStringView &path, ScStringView *out) {
  ScString label;
  if (!ScNativeVolumeName(path.ToChar(), &label))
    return false;
  out->FromChar(label.Data(), label.Strlen());
  return true;
}

// Open or create the named file with the original read access and sharing. Optional truncation results are discarded.
bool RsFile::FileCreate(const ScStringView &path, bool truncate) {
  ScString native = RsNativeFilePath(path.ToChar());
  FILE *file = RsOpenLocalFile(native.Data(), 1, 3, 3);
  if (!file)
    return false;
  if (truncate) {
    RsSeekLocalFile(file, 0, SEEK_SET);
    RsResizeLocalFile(file, 0);
  }
  fclose(file);
  return true;
}

// Test only the requested read-only and hidden bits. Preserve the original all-bits-set behavior for a missing file.
bool RsFile::GetAttribute(const ScStringView &path, i32 attributes) {
  ScString native = RsNativeFilePath(path.ToChar());
#ifdef _WIN32
  RsWideFilePath wide(native.Data());
  if (!wide.text)
    return false;
  DWORD value = GetFileAttributesW(wide.text);
  return (!(attributes & 1) || (value & FILE_ATTRIBUTE_READONLY)) &&
      (!(attributes & 2) || (value & FILE_ATTRIBUTE_HIDDEN));
#else
  struct stat info;
  if (stat(native.Data(), &info))
    return true;
  bool readonly = !(info.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH));
  const char *name = strrchr(native.Data(), '/');
  name = name ? name + 1 : native.Data();
  bool hidden = name[0] == '.' && name[1] && strcmp(name, "..") != 0;
#ifdef __APPLE__
  hidden = hidden || (info.st_flags & UF_HIDDEN) != 0;
#endif
  return (!(attributes & 1) || readonly) && (!(attributes & 2) || hidden);
#endif
}

// Change only the two supported attribute bits, retaining all other platform bits and the second live path query.
bool RsFile::SetAttribute(const ScStringView &path, i32 attributes, bool enabled) {
  ScString native = RsNativeFilePath(path.ToChar());
#ifdef _WIN32
  RsWideFilePath wide(native.Data());
  if (!wide.text)
    return false;
  DWORD value = GetFileAttributesW(wide.text);
  if (attributes & 1)
    value = enabled ? value | FILE_ATTRIBUTE_READONLY : value & ~FILE_ATTRIBUTE_READONLY;
  if (attributes & 2)
    value = enabled ? value | FILE_ATTRIBUTE_HIDDEN : value & ~FILE_ATTRIBUTE_HIDDEN;
  ScString target = RsNativeFilePath(path.ToChar());
  RsWideFilePath output(target.Data());
  return output.text && SetFileAttributesW(output.text, value) != FALSE;
#else
  struct stat info;
  if (stat(native.Data(), &info))
    return false;
  ScString target = RsNativeFilePath(path.ToChar());
  if (attributes & 2) {
#ifdef __APPLE__
    if (chflags(target.Data(), enabled ? info.st_flags | UF_HIDDEN : info.st_flags & ~UF_HIDDEN))
      return false;
#else
    errno = ENOTSUP;
    return false;
#endif
  }
  if (!(attributes & 1))
    return true;
  mode_t mode = enabled ? info.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH) : info.st_mode | S_IWUSR;
  return chmod(target.Data(), mode) == 0;
#endif
}

// Probe an existing file through its actual write-open operation. A missing path is created and removed on success.
bool RsFile::IsFileWritable(const ScStringView &path) {
  if (FileExists(path)) {
    RsFile file(path);
    if (!file.Open(kRsFileAccessWrite, kRsFileOpenExisting, kRsFileShareRead))
      return false;
    file.Close();
    return true;
  }
  if (!FileCreate(path, false))
    return false;
  Delete(path);
  return true;
}

// Reserve a descending-time filename after the supplied directory bytes, without adding a separator.
// Publish the complete concrete range only after the original exclusive CRT handle has been closed.
bool RsFile::MakeTempFileName(const ScStringView &directory, ScStringView *out) {
  u32 candidate = u32(time(nullptr));
  ScString name;
  for (u32 attempt = 0; attempt < 10000; ++attempt, --candidate) {
    name.Sprintf("%sRZ_%d.tmp", directory.ToChar(), i32(candidate));
    ScString native = RsNativeFilePath(name.Data());
    FILE *file = RsOpenLocalFile(native.Data(), 1, 0, 3);
    if (!file)
      continue;
    fclose(file);
    out->FromChar(name.Data(), name.ScString::Strlen());
    return true;
  }
  return false;
}

// Query the platform temporary directory and optionally expand its long name through the original lazy import.
// Keep failed initial queries separate from a reached long-name query that clears the output but still returns true.
bool RsFile::GetTempDirectory(ScStringView *out) {
  const char *path = SDL_getenv("TMPDIR");
#ifdef _WIN32
  if (!path || !*path)
    path = SDL_getenv("TEMP");
  if (!path || !*path)
    path = SDL_getenv("TMP");
#else
  if (!path || !*path)
    path = P_tmpdir;
#endif
  if (!path || !*path)
    return false;
  ScString native = RsNativeFilePath(path);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(native.Data(), &info) || info.type != SDL_PATHTYPE_DIRECTORY)
    return false;
  if (native.end == native.begin || native.end[-1] != '/')
    native.Append("/");
  out->FromChar(native.Data(), native.Strlen());
  return true;
}

// Resolve the source drive and return its volume serial only after the actual platform query succeeds.
u32 RsFile::GetVolumeSerialNumber(const ScStringView &path) {
  return ScNativeVolumeSerial(path.ToChar());
}

// Return the platform's maximum component length for the split drive, preserving the original zero failure result.
u32 RsFile::GetMaximumPathLength(const ScStringView &path) {
  return ScNativeComponentLimit(path.ToChar());
}

// Read original 32000-byte chunks and add the single byte selected by each successful Boolean Read result.
// The source uses that Boolean as a byte count; preserve this checksum rather than summing every file byte.
u32 RsFile::Checksum(const ScStringView &path) {
  RsFile file;
  file.SetName(path);
  u32 sum = 0;
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    i32 length = static_cast<i32>(file.Size());
    u8 *bytes = new u8[32000];
    if (length > 0)
      for (u32 position = 0; position < static_cast<u32>(length); position += 32000) {
        u32 remaining = static_cast<u32>(length) - position, size = remaining < 32000 ? remaining : 32000;
        if (file.Read(bytes, size))
          sum += bytes[0];
      }
    delete[] bytes;
    file.Close();
  }
  return sum;
}

// Use the optional extended query only on the original late-Windows-9x version branch.
// Other platforms retain the source's wrapping 32-bit cluster product, zero-extended into its 64-bit result.
u64 RsFile::GetFreeDiskSpace(const ScStringView &path) {
  ScString native = RsNativeFilePath(path.ToChar());
#ifdef _WIN32
  u32 drive = native.begin[0] && native.begin[1] == ':' ? u32(SDL_tolower(native.begin[0]) - 'a' + 1) : 0;
  struct _diskfree_t info;
  if (_getdiskfree(drive, &info))
    return 0;
  return u64(info.avail_clusters) * info.sectors_per_cluster * info.bytes_per_sector;
#else
  struct statvfs info;
  while (statvfs(native.Data(), &info)) {
    if (errno != ENOENT && errno != ENOTDIR)
      return 0;
    char *separator = strrchr(native.begin, '/');
    if (!separator) {
      native.FromChar(".");
      if (statvfs(native.Data(), &info))
        return 0;
      break;
    }
    if (separator == native.begin)
      separator[1] = 0;
    else
      *separator = 0;
  }
  return u64(info.f_bavail) * info.f_frsize;
#endif
}

u32 RsFile::GetLastError() {
  return error;
}

// Replace the owned error String with the system's allocated message, free that message and return the owned String.
// Initialize the allocation pointer to exclude the source's undefined read when formatting writes no pointer.
ScString *RsFile::GetLastErrorMessage() {
  return &error_text;
}

RsFileRecord::RsFileRecord(RsIndexedFile *parent, const RsKey &input) : owner(parent), buffered(1), position(0) {
  key = input;
}

RsFileRecord::RsFileRecord(RsIndexedFile *parent, const RsKey &input, u32 offset, u32 size)
    : owner(parent), buffered(0), position(offset), indexed_size(size) {
  key = input;
}

// Free every still-present buffered chunk before releasing vector storage and the record bases. The original
// destructor receives the adjusted controlling-record view; native virtual deletion performs that adjustment.
RsFileRecord::~RsFileRecord() {
  for (u32 i = 0; i < chunks.size(); ++i) {
    delete[] chunks[i].bytes;
    chunks[i].bytes = nullptr;
  }
  chunks.clear();
}

bool RsFileRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsRecordData, RsRecordData);
  return RsRecordStream::QueryInterface(iid, out);
}

u32 RsFileRecord::AddRef() {
  return RsRecordStream::AddRef();
}

u32 RsFileRecord::Release() {
  return RsRecordStream::Release();
}

bool RsFileRecord::GetSize(u32 *out) {
  *out = buffered ? DataSize() : indexed_size;
  return 1;
}

bool RsFileRecord::ReadBlock(void *out, u32 size) {
  return owner->ReadRecord(out, size, &position);
}

// Append an owned copy as one chunk, even for an empty write. The extra allocation byte is left unwritten and
// the original does not change buffered/dirty mode merely because data was appended.
bool RsFileRecord::WriteBlock(const void *source, u32 size) {
  u8 *bytes = new u8[(usize)size + 1];
  if (size)
    memcpy(bytes, source, size);
  chunks.push_back({bytes, size});
  return 1;
}

// Visit chunks in order and stop when the original Boolean callback rejects one.
bool RsFileRecord::EnumChunks(RsChunkVisitor visitor, RsIndexedFile *context) {
  for (u32 i = 0; i < chunks.size(); ++i)
    if (!visitor(chunks[i].bytes, chunks[i].size, context))
      return false;
  return true;
}

bool RsFileRecord::IsDirty() {
  return buffered != 0;
}

bool RsFileRecord::SkipBytes(u32 distance) {
  return owner->SeekRecord(distance, &position);
}

// Sum the current chunk lengths with original DWORD wrapping. File-mode GetSize ignores this independent buffer.
u32 RsFileRecord::DataSize() {
  u32 size = 0;
  for (u32 i = 0; i < chunks.size(); ++i)
    size += chunks[i].size;
  return size;
}

// Replace the key and file extent, then erase buffered descriptors without freeing their allocations.
// The original void method switches to file mode after clearing those descriptors.
void RsFileRecord::Reset(const RsKey &input, u32 offset, u32 size) {
  key = input;
  position = offset;
  indexed_size = size;
  if (buffered) {
    chunks.clear();
    buffered = 0;
  }
}

RsFileSystemObject::RsFileSystemObject()
    : ScServiceBase(RsFileSystem::ServiceId, kRsFileSystemPriority), dataReady(0), pluginReady(0), applicationReady(0),
      cdReady(0), useCDIni(0), allowRecursiveCDSearch(1) {
}

RsFileSystemObject::~RsFileSystemObject() {
}

bool RsFileSystemObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsFileSystem, RsFileSystem);
  return ScServiceBase::QueryInterface(iid, out);
}

u32 RsFileSystemObject::AddRef() {
  return ScServiceBase::AddRef();
}

u32 RsFileSystemObject::Release() {
  return ScServiceBase::Release();
}

bool RsFileSystemObject::Init() {
  return true;
}

bool RsFileSystemObject::Shutdown() {
  return true;
}

// Capture the native executable path once and cache its directory, basename, extension and actual drive prefix.
// POSIX paths retain an empty drive; reference builds retain the original Win32 split and buffer boundaries.
void RsFileSystemObject::GetAndSplitApplicationPath() {
  ScString path = RsApplicationFilePath();
  if (path.begin == path.end)
    return;
  const char *name = strrchr(path.Data(), '/');
  name = name ? name + 1 : path.Data();
  const char *extension = strrchr(name, '.');
  if (!extension)
    extension = path.end;
  appDrive.FromChar("");
  if (path.Strlen() >= 3 && path.begin[1] == ':' && path.begin[2] == '/')
    appDrive.FromChar(path.begin, 3);
  appDirectory.FromChar(path.begin, u32(name - path.begin));
  appName.FromChar(name, u32(extension - name));
  appExtension.FromChar(extension, u32(path.end - extension));
  applicationReady = 1;
}

// Lazily split the application path through its real virtual, then borrow this cached component's bytes.
const char *RsFileSystemObject::AppDrive() {
  if (!applicationReady) {
    GetAndSplitApplicationPath();
    applicationReady = 1;
  }
  return appDrive.begin;
}

// Lazily split the application path through its real virtual, then borrow this cached component's bytes.
const char *RsFileSystemObject::AppDirectory() {
  if (!applicationReady) {
    GetAndSplitApplicationPath();
    applicationReady = 1;
  }
  return appDirectory.begin;
}

// Lazily split the application path through its real virtual, then borrow this cached component's bytes.
const char *RsFileSystemObject::AppName() {
  if (!applicationReady) {
    GetAndSplitApplicationPath();
    applicationReady = 1;
  }
  return appName.begin;
}

// Lazily split the application path through its real virtual, then borrow this cached component's bytes.
const char *RsFileSystemObject::AppExtension() {
  if (!applicationReady) {
    GetAndSplitApplicationPath();
    applicationReady = 1;
  }
  return appExtension.begin;
}

// Preserve the original application-name-before-directory reads and retire concat inputs before INI construction.
// This native expression phase returns an independently owned concrete String rather than inventing a service API.
static ScString RsFileSystemIniPath(RsFileSystem *fileSystem) {
  ScString name(fileSystem->AppName()), directory(fileSystem->AppDirectory()), joined(directory);
  joined.Append(name.begin, name.ScString::Strlen());
  joined.Append(".ini");
  return ScString(joined);
}

// Read the executable's Directories/Data entry once, retaining a nonempty value or falling back to AppDirectory.
// Publish readiness before destroying the INI owner, then borrow the stored path after all temporaries retire.
const char *RsFileSystemObject::DataDirectory() {
  if (!dataReady) {
    ScString iniName = RsFileSystemIniPath(this);
    ApIniFile ini(iniName);
    {
      ScString key("Data"), section("Directories");
      ini.ReadEntry(section, key, &dataDirectory);
    }
    if (dataDirectory.begin == dataDirectory.end) {
      ScString fallback(AppDirectory());
      dataDirectory = fallback;
    }
    dataReady = 1;
  }
  return dataDirectory.begin;
}

// Resolve Directories/PlugIn independently of Data readiness and retain the original application-directory fallback.
// Key and section Strings retire before fallback evaluation; the returned pointer is read after INI cleanup.
const char *RsFileSystemObject::PlugInDirectory() {
  if (!pluginReady) {
    ScString iniName = RsFileSystemIniPath(this);
    ApIniFile ini(iniName);
    {
      ScString key("PlugIn"), section("Directories");
      ini.ReadEntry(section, key, &pluginDirectory);
    }
    if (pluginDirectory.begin == pluginDirectory.end) {
      ScString fallback(AppDirectory());
      pluginDirectory = fallback;
    }
    pluginReady = 1;
  }
  return pluginDirectory.begin;
}

// Resolve application, data and plugin state in order before the first media search, then borrow the cached CD drive.
const char *RsFileSystemObject::CDAppDrive() {
  if (!cdReady) {
    if (!applicationReady)
      GetAndSplitApplicationPath();
    if (!dataReady)
      DataDirectory();
    if (!pluginReady)
      PlugInDirectory();
    FindCDDrive();
  }
  return cdDrive.begin;
}

// Reach the original CD-drive virtual only while uncached, then borrow this media path component.
const char *RsFileSystemObject::CDAppDirectory() {
  if (!cdReady)
    CDAppDrive();
  return cdAppDirectory.begin;
}

// Reach the original CD-drive virtual only while uncached, then borrow this media path component.
const char *RsFileSystemObject::CDDataDirectory() {
  if (!cdReady)
    CDAppDrive();
  return cdDataDirectory.begin;
}

// Reach the original CD-drive virtual only while uncached, then borrow this media path component.
const char *RsFileSystemObject::CDPlugInDirectory() {
  if (!cdReady)
    CDAppDrive();
  return cdPluginDirectory.begin;
}

// Search optical and eligible removable drives in reference builds; SDL uses the installed local path fallback.
// A failed media search caches local paths and returns false without claiming that a disc was found.
bool RsFileSystemObject::FindCDDrive() {
  bool found = false;
  if (!found) {
    cdDrive = appDrive;
    cdAppDirectory = appDirectory;
    cdDataDirectory = dataDirectory;
    cdPluginDirectory = pluginDirectory;
  }
  cdReady = 1;
  return found;
}

void RsFileSystemObject::UseCDIniFile(bool value) {
  useCDIni = value;
}

void RsFileSystemObject::SetExpectedCDName(const ScStringView &value) {
  ScString copy(value);
  expectedCDName = copy;
}

void RsFileSystemObject::SetExpectedCDAppPath(const ScStringView &value) {
  ScString copy(value);
  expectedCDAppPath = copy;
}

// Capture length before data, replace the stored Data range directly, mark it ready, and test the actual path attributes.
bool RsFileSystemObject::SetDataDir(const ScStringView &value) {
  u32 length = value.Strlen();
  const char *data = value.Data();
  dataDirectory.ScString::FromChar(data, length);
  dataReady = 1;
  return RsFile::FileExists(dataDirectory);
}

// Validate the actual media INI, explicit expected paths or recursively located executable in the original branch order.
// Preserve partial path updates and ignored per-key failures rather than inventing an all-or-nothing disc result.
bool RsFileSystemObject::ValidateCDPath() {
  if (cdDrive.begin == cdDrive.end)
    return false;
  if (useCDIni) {
    ScString iniName(cdDrive);
    iniName.Append("Maxis.ini");
    if (!RsFile::FileExists(iniName))
      return false;
    ScString section(ApGetFramework()->Application()->ModuleName()), value;
    if (section.begin == section.end)
      return false;
    ApIniFile ini(iniName);
    const char *keys[3] = {"App", "Data", "PlugIn"};
    ScString *paths[3] = {&cdAppDirectory, &cdDataDirectory, &cdPluginDirectory};
    for (u32 i = 0; i < 3; ++i) {
      bool found;
      {
        ScString key(keys[i]);
        found = ini.ReadEntry(section, key, &value);
      }
      if (found) {
        ScString joined(cdDrive);
        joined.Append(value.begin, value.ScString::Strlen());
        *paths[i] = joined;
      }
    }
    return true;
  }
  if (!allowRecursiveCDSearch)
    return false;
  RsDirectoryListing listing(cdDrive, false, false, false);
  if (expectedCDName.begin == expectedCDName.end && expectedCDAppPath.begin == expectedCDAppPath.end) {
    ScString filter(appName);
    filter.Append(appExtension.begin, appExtension.ScString::Strlen());
    std::list<ScString> matches;
    listing.SetNewDirectoryEntryFilter(filter, false);
    if (listing.ReadDirectorySpecificEntriesIntoStringListRecursive(&cdDrive, &matches, true) && !matches.empty()) {
      RsFile::GetPathDirectory(matches.front(), &cdAppDirectory);
      cdDataDirectory = ScString(DataDirectory());
      cdPluginDirectory = ScString(PlugInDirectory());
      return true;
    }
    return false;
  }
  bool valid = true;
  ScString volume;
  if (expectedCDName.begin == expectedCDName.end ||
      (RsFile::GetVolumeName(cdDrive, &volume) && !ScCompareNoCase(volume.begin, expectedCDName.ToChar()))) {
    if (expectedCDAppPath.begin != expectedCDAppPath.end) {
      ScString joined(cdDrive);
      joined.Append(expectedCDAppPath.begin, expectedCDAppPath.ScString::Strlen());
      if (RsFile::FileExists(joined)) {
        RsFile::GetPathDirectory(joined, &cdAppDirectory);
        cdDataDirectory = cdAppDirectory;
        cdPluginDirectory = cdAppDirectory;
      } else
        valid = false;
    }
  } else
    valid = false;
  return valid;
}

static constexpr RsKey RsLayerDirectoryKey(kRsLayerDirectoryRecordType, kRsLayerDirectoryRecordGroup, 0);

// The original methods use a stack guard over the same recursive mutex, including their inner record operations.
class RsLayerLock {
public:
  // Acquire the supplied real mutex before the guarded method reaches any resource calls.
  explicit RsLayerLock(ScLock *lock) : lock(lock) {
    lock->Lock();
  }

  // Release on normal return or exception, matching the original stack guard's lifetime.
  ~RsLayerLock() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

RsStoredLayer::RsStoredLayer() : segment(nullptr) {
}

RsStoredLayer::RsStoredLayer(RsSegment *value, const RsKey &key) : segment(value), key(key) {
  segment->AddRef();
}

RsStoredLayer::RsStoredLayer(const RsStoredLayer &source) : segment(source.segment), key(source.key) {
  if (segment)
    segment->AddRef();
}

RsStoredLayer::~RsStoredLayer() {
  ScRelease(&segment);
}

// Retain the replacement before releasing the old segment, then copy the replacement pointer and key words.
RsStoredLayer &RsStoredLayer::operator=(const RsStoredLayer &source) {
  RsSegment *replacement = source.segment;
  if (replacement)
    replacement->AddRef();
  if (segment)
    segment->Release();
  segment = replacement;
  key = source.key;
  return *this;
}

RsLayerRecord::RsLayerRecord(RsRecord *value, RsSegment *segment) : record(nullptr), owner(nullptr) {
  if (segment->QueryInterface(IID_RsDatabaseSegment, reinterpret_cast<void **>(&owner)))
    record = ScAddRefNotNull(value);
}

// Retain the copied controlling owner before the copied record, preserving the original list-node copy order.
RsLayerRecord::RsLayerRecord(const RsLayerRecord &source) : record(source.record), owner(source.owner) {
  if (owner)
    owner->AddRef();
  if (record)
    record->AddRef();
}

RsLayerRecord::~RsLayerRecord() {
  if (record)
    record->Release();
  if (owner)
    owner->Release();
}

RsLayeredFile::RsLayeredFile() : primary(nullptr), writing(0), sourceFlag(1), sequence(0), version(103) {
}

RsLayeredFile::~RsLayeredFile() {
  Shutdown();
}

bool RsLayeredFile::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsFileSegment, RsFileSegment);
  return RsSegment::QueryInterface(iid, out);
}

u32 RsLayeredFile::AddRef() {
  return RsSegment::AddRef();
}

u32 RsLayeredFile::Release() {
  return RsSegment::Release();
}

// Replace the owned primary indexed file under the recursive lock, retaining it before its original Init call.
// Successful initialization resets the active descriptor and flags but leaves stored layers and sequence untouched.
bool RsLayeredFile::Init(u32 identifier, const ScStringView *name, bool resolved) {
  RsLayerLock guard(&lock);
  if (primary) {
    primary->Shutdown();
    primary->Release();
    primary = nullptr;
  }
  primary = ScAddRefNotNull(new RsIndexedFile(nullptr));
  if (!primary->Init(identifier, name, resolved)) {
    primary->Release();
    primary = nullptr;
    return false;
  }
  writing = 0;
  sourceFlag = 1;
  RsStoredLayer empty;
  active = empty;
  return true;
}

// In write mode save metadata before flushing payload layers, then release stored segment references and the primary.
// Failed write/flush replies do not prevent the source's remaining ownership cleanup.
bool RsLayeredFile::Shutdown() {
  RsLayerLock guard(&lock);
  u8 result = 1;
  if (primary) {
    if (writing) {
      result = WriteDirectory();
      if (result)
        result = FlushLayers();
    }
    for (auto &layer : layers)
      if (layer.segment) {
        layer.segment->Release();
        layer.segment = nullptr;
      }
    if (primary) {
      primary->Shutdown();
      primary->Release();
      primary = nullptr;
    }
  }
  return result != 0;
}

u32 RsLayeredFile::GetID() {
  return primary ? primary->GetID() : 0;
}

// Obtain the primary path and dispatch the requested exclusive read/write mode through the secondary operations.
bool RsLayeredFile::OpenDBSegment(bool read, bool write) {
  RsLayerLock guard(&lock);
  if ((read && write) || !primary)
    return false;
  ScString name;
  if (!primary->GetPath(&name))
    return false;
  if (read)
    return OpenRead(&name) != 0;
  if (write && primary)
    return OpenWrite(&name) != 0;
  return false;
}

// Try the primary inner read-open first, then stored layers in vector order, retaining a new owner/record list pair.
bool RsLayeredFile::DoOpenRecord(const RsKey &key, RsRecord **out) {
  RsLayerLock guard(&lock);
  if (!primary)
    return 0;
  if (primary->DoOpenRecord(key, out)) {
    RsLayerRecord pair(*out, primary);
    records.push_back(pair);
    return 1;
  }
  for (auto &layer : layers)
    if (layer.segment && layer.segment->DoOpenRecord(key, out)) {
      RsLayerRecord pair(*out, layer.segment);
      records.push_back(pair);
      return 1;
    }
  return 0;
}

// Locate the first retained record pointer, ignore its owner's close reply and erase the pair after that close call.
bool RsLayeredFile::DoCloseRecord(RsRecord **slot) {
  RsLayerLock guard(&lock);
  if (!slot || !*slot)
    return 0;
  for (auto it = records.begin(); it != records.end(); ++it)
    if (it->record == *slot) {
      if (!it->owner)
        return 0;
      it->owner->CloseRecord(slot);
      records.erase(it);
      return 1;
    }
  return 0;
}

// Select the actual owner abort operation, preserving the same erase and retained-reference ordering as close.
bool RsLayeredFile::DoAbortRecord(RsRecord **slot) {
  RsLayerLock guard(&lock);
  if (!slot || !*slot)
    return 0;
  for (auto it = records.begin(); it != records.end(); ++it)
    if (it->record == *slot) {
      if (!it->owner)
        return 0;
      it->owner->AbortRecord(slot);
      records.erase(it);
      return 1;
    }
  return 0;
}

// Test the primary inner key lookup before each live stored layer while holding the wrapper's recursive lock.
bool RsLayeredFile::DoTestForRecord(const RsKey &key) {
  RsLayerLock guard(&lock);
  if (!primary)
    return false;
  if (primary->DoTestForRecord(key))
    return true;
  for (auto &layer : layers)
    if (layer.segment && layer.segment->DoTestForRecord(key))
      return true;
  return false;
}

// Write through the selected active layer when present, otherwise through the primary. Do not fall back after failure.
bool RsLayeredFile::DoOpenRecordForWrite(const RsKey &key, RsRecord **out) {
  if (!primary || !writing)
    return 0;
  RsSegment *owner = active.segment ? active.segment : primary;
  if (!owner->DoOpenRecordForWrite(key, out))
    return 0;
  RsLayerRecord pair(*out, owner);
  records.push_back(pair);
  return 1;
}

bool RsLayeredFile::DoCreateNewRecord(const RsKey &key, RsRecord **out) {
  return DoOpenRecordForWrite(key, out);
}

bool RsLayeredFile::DoDeleteRecord(const RsKey &) {
  return 0;
}

u8 RsLayeredFile::GetPath(ScStringView *out) {
  return primary ? primary->GetPath(out) : 0;
}

// Initialize a resolved read path, then probe its metadata version. Probe failure releases the primary but retains Init's reply.
u8 RsLayeredFile::OpenRead(const ScStringView *name) {
  RsLayerLock guard(&lock);
  sourceFlag = 0;
  u8 result = Init(0, name, true);
  writing = 0;
  if (!ReadVersion() && primary) {
    primary->Shutdown();
    primary->Release();
    primary = nullptr;
  }
  return result;
}

// Initialize a resolved path and mark write mode afterward, even when initialization fails.
u8 RsLayeredFile::OpenWrite(const ScStringView *name) {
  RsLayerLock guard(&lock);
  sourceFlag = 0;
  u8 result = Init(0, name, true);
  writing = 1;
  return result;
}

u32 RsLayeredFile::GetVersion() {
  return primary ? version : 0;
}

// Probe the metadata record's first word in read mode, close it before releasing the queried stream and abort on failure.
u8 RsLayeredFile::ReadVersion() {
  if (writing)
    return 1;
  if (!primary)
    return 0;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = primary->OpenRecord(RsLayerDirectoryKey, &record);
  result =
      result && record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)) && stream->ReadU32(&version);
  if (result)
    result = primary->CloseRecord(&record);
  if (!result && record)
    primary->AbortRecord(&record);
  if (stream)
    stream->Release();
  return result;
}

// Merge primary keys with a temporary real key list for each live stored layer. Later failures do not stop enumeration.
bool RsLayeredFile::GetResourceKeyList(RsKeyListInterface *out) {
  if (!primary)
    return false;
  RsLayerLock guard(&lock);
  u8 result = primary->GetResourceKeyList(out);
  if (result)
    for (auto &layer : layers)
      if (layer.segment) {
        RsKeyList keys;
        if (!layer.segment->GetResourceKeyList(&keys))
          result = 0;
        if (!out->Insert(keys))
          result = 0;
      }
  return result != 0;
}

u8 RsLayeredFile::LoadRecords() {
  return ReadDirectory() ? LoadLayers() : 0;
}

// Create and retain a writable packed layer, append its descriptor, select it and publish its generated sequence key.
u8 RsLayeredFile::StoreRecords(RsKey *out, u32 initialSize, u32 growth) {
  RsLayerLock guard(&lock);
  if (!writing)
    return 0;
  RsKey key(kRsLayerDirectoryRecordType, kRsLayerDirectoryRecordGroup, sequence + 1);
  RsPackedSegment *segment = new RsPackedSegment(version);
  segment->AddRef();
  if (!segment->Create(key, initialSize, growth)) {
    segment->Release();
    return 0;
  }
  RsStoredLayer layer(segment, key);
  layers.push_back(layer);
  SetActiveLayer(segment);
  *out = key;
  ++sequence;
  segment->Release();
  return 1;
}

// Select the first live stored layer with an equal serialized key, requiring the wrapper to remain in write mode.
u8 RsLayeredFile::SelectLayer(const RsKey &key) {
  RsLayerLock guard(&lock);
  if (!writing)
    return 0;
  for (auto &layer : layers)
    if (layer.key.instance == key.instance && layer.key.group == key.group && layer.key.type == key.type &&
        layer.segment) {
      SetActiveLayer(layer.segment);
      return 1;
    }
  return 0;
}

// Clear an existing active layer only in write mode; absence or read mode returns zero.
u8 RsLayeredFile::ClearSelection() {
  RsLayerLock guard(&lock);
  if (!writing || !active.segment)
    return 0;
  SetActiveLayer(nullptr);
  return 1;
}

// Preserve the original active-pointer setter's effects: retain replacement, release/clear old and install replacement.
void RsLayeredFile::SetActiveLayer(RsSegment *replacement) {
  if (replacement)
    replacement->AddRef();
  if (active.segment) {
    active.segment->Release();
    active.segment = nullptr;
  }
  active.segment = replacement;
}

// Read version101/103 and a signed layer count into empty descriptors, retaining any prefix read before failure.
// Release the queried stream before close; an abort reply replaces the failed read/close reply when a record exists.
u8 RsLayeredFile::ReadDirectory() {
  if (writing || !primary)
    return 0;
  if (!layers.empty())
    return 1;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = primary->OpenRecord(RsLayerDirectoryKey, &record);
  if (result && record)
    result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
  if (result && stream) {
    result = stream->ReadU32(&version);
    if (version != 101 && version != 103)
      result = 0;
    i32 count = 0;
    if (result)
      result = stream->ReadI32(&count);
    for (i32 i = 0; i < count && result; ++i) {
      RsStoredLayer layer;
      result = stream->ReadKey(&layer.key);
      if (result)
        layers.push_back(layer);
    }
  }
  if (stream)
    stream->Release();
  if (result)
    result = primary->CloseRecord(&record);
  if (!result && record)
    return primary->AbortRecord(&record);
  return result;
}

// Write version, layer count and descriptor keys in order. Release the stream before close and return a reached abort reply.
u8 RsLayeredFile::WriteDirectory() {
  if (!writing || !primary)
    return 0;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = primary->OpenRecordForWrite(RsLayerDirectoryKey, &record);
  if (result && record)
    result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
  if (result && stream) {
    result = stream->WriteU32(version);
    if (result)
      result = stream->WriteI32(static_cast<i32>(layers.size()));
    for (auto &layer : layers) {
      if (!result)
        break;
      result = stream->WriteKey(&layer.key);
    }
  }
  if (stream)
    stream->Release();
  if (result)
    result = primary->CloseRecord(&record);
  if (!result && record)
    return primary->AbortRecord(&record);
  return result;
}

// Load every descriptor in read mode, continuing after individual failures and retaining their aggregate result.
u8 RsLayeredFile::LoadLayers() {
  RsLayerLock guard(&lock);
  if (writing || layers.empty())
    return 0;
  u8 result = 1;
  for (auto &layer : layers)
    if (!ReadLayer(&layer))
      result = 0;
  return result;
}

// Close live listed records through each retained owner, then flush every stored layer without erasing the open-record list.
u8 RsLayeredFile::FlushLayers() {
  RsLayerLock guard(&lock);
  for (auto &pair : records)
    if (pair.record && pair.owner)
      pair.owner->CloseRecord(&pair.record);
  u8 result = 1;
  for (auto &layer : layers)
    if (!WriteLayer(&layer))
      result = 0;
  layers.clear();
  return result;
}

// Decode an unbound stored descriptor through its actual packed segment, retaining even a failed decode rescued by abort.
u8 RsLayeredFile::ReadLayer(RsStoredLayer *layer) {
  RsLayerLock guard(&lock);
  if (writing || layer->segment || !primary)
    return 0;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  RsPackedSegment *segment = nullptr;
  u8 result = primary->OpenRecord(layer->key, &record);
  if (result && record)
    result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
  if (result) {
    segment = ScAddRefNotNull(new RsPackedSegment(version));
    result = segment->Read(layer->key, stream);
  }
  if (stream)
    stream->Release();
  bool accepted = false;
  if (result) {
    result = primary->CloseRecord(&record);
    accepted = result != 0;
  }
  if (!accepted && record) {
    result = primary->AbortRecord(&record);
    accepted = result != 0;
  }
  if (accepted) {
    assert(segment); // Stock publishes the segment and immediately dereferences it through AddRef.
    layer->segment = segment;
    segment->AddRef();
  }
  if (segment)
    segment->Release();
  return result;
}

// Write one packed segment through the primary inner record operations, then drop its stored reference regardless of reply.
u8 RsLayeredFile::WriteLayer(RsStoredLayer *layer) {
  RsLayerLock guard(&lock);
  if (!writing || !layer->segment || !primary)
    return 0;
  if (active.segment == layer->segment)
    SetActiveLayer(nullptr);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = primary->DoOpenRecordForWrite(layer->key, &record);
  if (result) {
    result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
    if (result)
      result = static_cast<RsPackedSegment *>(layer->segment)->Write(stream);
  }
  if (stream)
    stream->Release();
  if (result)
    result = primary->DoCloseRecord(&record);
  if (!result) {
    assert(record); // The original indexed-file abort releases the record without a null test.
    result = primary->DoAbortRecord(&record);
  }
  layer->segment->Release();
  layer->segment = nullptr;
  return result;
}

// Construct the genuine layered-file segment and return its zero-reference secondary file identity.
ScUnknown *RsCreateClass835F62CF() {
  return static_cast<RsFileSegment *>(new RsLayeredFile);
}
