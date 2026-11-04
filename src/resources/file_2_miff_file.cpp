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

RsLooseRecord::RsLooseRecord() : mode(0), opened(false) {
}

RsLooseRecord::~RsLooseRecord() {
  if (file.IsOpen() || backup.IsOpen())
    Abort();
}

bool RsLooseRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsLooseAccess, RsLooseAccess);
  return RsRecordStream::QueryInterface(iid, out);
}

u32 RsLooseRecord::AddRef() {
  return RsRecordStream::AddRef();
}

u32 RsLooseRecord::Release() {
  return RsRecordStream::Release();
}

// Set mode before the opened test, preparing a backup only for transactional writes and copying the key after success.
bool RsLooseRecord::Open(const RsKey *input, const ScStringView *name, u32 value) {
  mode = value;
  if (opened)
    return false;
  file.SetName(*name);
  if (mode == 2) {
    ScString temporary;
    if (!RsFile::MakeTempPathName(&temporary))
      return false;
    backup.SetName(temporary);
    if (!backup.Copy(file, true))
      return false;
  }
  bool result = false;
  if (mode == 1)
    result = file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareNone);
  else if (mode == 2)
    result = file.Open(kRsFileAccessWrite, kRsFileTruncateExisting, kRsFileShareNone);
  else if (mode == 3)
    result = file.Open(kRsFileAccessWrite, kRsFileCreateNew, kRsFileShareNone);
  if (!result) {
    Abort();
    return false;
  }
  RsRecordObject::key = *input;
  opened = true;
  return true;
}

u8 RsLooseRecord::Close() {
  if (!opened)
    return 0;
  opened = false;
  if (file.IsOpen())
    file.Close();
  return mode != 2 || backup.Delete() != 0;
}

// Close before restoring a transactional backup; aborting another mode reports success without deleting that file.
u8 RsLooseRecord::Abort() {
  opened = false;
  if (file.IsOpen())
    file.Close();
  return mode != 2 || file.Copy(backup, true);
}

// Publish the actual file size only while the record is open; a closed record preserves the caller's output.
bool RsLooseRecord::GetSize(u32 *out) {
  if (!opened)
    return 0;
  *out = file.Size();
  return 1;
}

bool RsLooseRecord::ReadBlock(void *out, u32 size) {
  return opened && mode == 1 && file.Read(out, size);
}

bool RsLooseRecord::WriteBlock(const void *data, u32 size) {
  return opened && (mode == 2 || mode == 3) && file.Write(data, size);
}

// Return the live file's updated byte count, ignoring its separate Boolean result as the original does.
u32 RsLooseRecord::ReadCount(void *out, u32 size) {
  if (!opened || mode != 1)
    return 0;
  file.Read(out, &size);
  return size;
}

// Preserve the count-returning writer's broader non-read-mode gate and its original partial-count result.
u32 RsLooseRecord::WriteCount(const void *data, u32 size) {
  if (!opened || mode == 1)
    return 0;
  file.Write(data, &size);
  return size;
}

bool RsLooseRecord::Flush() {
  return opened && mode != 1 && file.Flush();
}

// Publish the current position for an open record, explicitly clearing the output when closed.
u8 RsLooseRecord::GetPosition(u32 *out) {
  if (!opened) {
    *out = 0;
    return 0;
  }
  *out = file.Position();
  return 1;
}

// Reject distances beyond file size, then seek using the original origin table while ignoring seek failure.
// An origin outside that table fails before the original undefined table access.
u8 RsLooseRecord::Seek(u32 distance, u32 origin) {
  if (!opened || distance > file.Size())
    return 0;
  if (origin >= 3)
    return 0;
  file.Seek((i32)distance, origin);
  return 1;
}

bool RsLooseRecord::SkipBytes(u32 distance) {
  return opened && mode == 1 && file.SeekRelative(static_cast<i32>(distance)) != kRsInvalidFilePosition;
}

// Allocate and retain an independent name String before copying the actual file's full name into it.
u8 RsLooseRecord::GetName(ScStringView **out) {
  if (!opened || !out)
    return 0;
  ScString *name = new ScString();
  name->AddRef();
  file.GetName(name);
  *out = name;
  return 1;
}

// Publish an independently retained basename-with-extension String, preserving the original temporary String ordering.
u8 RsLooseRecord::GetFileName(ScStringView **out) {
  if (!opened || !out)
    return 0;
  ScString *value = new ScString();
  value->AddRef();
  ScString path;
  file.GetName(&path);
  RsFile::GetPathName(path, value, true);
  *out = value;
  return 1;
}

// Publish a retained extension String even when that extension is empty, preserving closed/null-output failure behavior.
u8 RsLooseRecord::GetExtension(ScStringView **out) {
  if (!opened || !out)
    return 0;
  ScString *value = new ScString();
  value->AddRef();
  ScString path;
  file.GetName(&path);
  RsFile::GetPathExtension(path, value);
  *out = value;
  return 1;
}

RsLzoCodec::RsLzoCodec() : references(0), allocation(new u8[65552]) {
  workspace = allocation + ((16 - (reinterpret_cast<uptr>(allocation) & 15)) & 15);
}

RsLzoCodec::~RsLzoCodec() {
  delete[] allocation;
}

// Publish the same receiver for Unknown and the shared compression interface, retaining it before returning success. An
// unsupported identifier clears the output.
bool RsLzoCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsLzoCodec);
  HANDLE_QUERY_INTERFACE(IID_RsCompression, RsLzoCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsLzoCodec::AddRef() {
  return ++references;
}

// Delete at zero or one reference; otherwise decrement and return the remaining reference count.
u32 RsLzoCodec::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

bool RsLzoCodec::Compress(const void *, u32, u8 *, u32 *) {
  return false;
}

bool RsLzoCodec::Decompress(const u8 *, u32, void *, u32 *) {
  return false;
}

u32 RsLzoCodec::GetBufferSize(u32 size) {
  return (size >> 6) + size + 27;
}

// Read the second little-endian header word as the stored compressed length without imposing a new header check.
u32 RsLzoCodec::GetCompressedSize(const u8 *source) {
  u32 size;
  memcpy(&size, source + 4, 4);
  return size;
}

// Read the first header word as the decompressed length, keeping its distinct order from the QFS codec.
u32 RsLzoCodec::GetUncompressedSize(const u8 *source) {
  u32 size;
  memcpy(&size, source, 4);
  return size;
}

// Build the original version and release-date text before copying the temporary String through the supplied interface.
void RsLzoCodec::GetVersionString(ScStringView *out) {
  ScString version("1.01");
  version.Append(" ");
  version.Append("Aug 10 1997");
  out->Copy(version);
}

// Allocate the actual zero-reference LZO codec registered for class23599096 by the resource module.
ScUnknown *RsCreateLzoCodec() {
  return new RsLzoCodec;
}

RsMiffFile::RsMiffFile(const ScStringView *name)
    : offset(0), chunkSize(0), index(0), mode(0), finalized(0), failed(0), header{} {
  file.SetName(*name);
}

RsMiffFile::~RsMiffFile() {
  if (IsOpen())
    Close();
}

u8 RsMiffFile::IsOpen() {
  return file.IsOpen();
}

// Open existing storage for reading, then read its header and select the first chunk. Both setup results are ignored.
u8 RsMiffFile::OpenRead() {
  if (!IsOpen() && !file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return 0;
  mode = 1;
  ReadHeader();
  First();
  return 1;
}

// Reuse an open handle or create writable storage, stamping the supplied flags before selecting its first chunk.
u8 RsMiffFile::OpenWrite(u32 first, u32 second, u32 third) {
  if (!IsOpen() && !file.Open(kRsFileAccessReadWrite, kRsFileOpenAlways, kRsFileShareRead))
    return 0;
  mode = 2;
  WriteHeader(first, second, third);
  First();
  return 1;
}

// Finalize an unfinished writer before closing its file. Neither operation changes this entry's success result.
u8 RsMiffFile::Close() {
  if (mode == 2 && !finalized)
    EndWrite();
  file.Close();
  return 1;
}

// Copy the current chunk's three key words to the supplied output and return that same output address.
RsKey *RsMiffFile::GetKey(RsKey *out) {
  *out = key;
  return out;
}

void RsMiffFile::SetKey(const RsKey &value) {
  key = value;
}

// Read the seven-word header from offset zero. The original ignores the physical read result once the file is open.
u8 RsMiffFile::ReadHeader() {
  if (!IsOpen())
    return 0;
  file.SeekBegin();
  file.Read(header, sizeof(header));
  return 1;
}

// Read the current key and chunk length, then restore the chunk start. Oversized extents latch the failure flag.
u8 RsMiffFile::ReadRecordHeader() {
  file.Read(&key, sizeof(key));
  u8 result = file.Read(&chunkSize, 4);
  file.Seek((i32)offset, kRsSeekBegin);
  if (!result)
    return 0;
  if (offset + chunkSize > file.Size()) {
    failed = 1;
    return 0;
  }
  return 1;
}

// Publish the four chunk header words, then read the requested prefix of its payload and restore the chunk start.
u8 RsMiffFile::ReadKeyChunk(void *out, u32 length) {
  if (!IsOpen() || failed)
    return 0;
  u32 size = length - 16;
  if (chunkSize < size)
    size = chunkSize;
  memcpy(out, &key, 12);
  memcpy(static_cast<u8 *>(out) + 12, &chunkSize, 4);
  u8 result = file.Read(static_cast<u8 *>(out) + 16, size);
  file.Seek((i32)offset, kRsSeekBegin);
  return result;
}

u8 RsMiffFile::First() {
  if (!IsOpen() || failed)
    return 0;
  file.Seek(28, kRsSeekBegin);
  index = 0;
  offset = 28;
  ReadRecordHeader();
  return 1;
}

// Failure restores the previous physical cursor while retaining any changed header fields and latched failure flag.
u8 RsMiffFile::Next() {
  u32 previous = offset;
  if (IsOpen() && !failed) {
    if (chunkSize && chunkSize <= 0x8000) {
      u32 next = file.Seek((i32)chunkSize, kRsSeekCurrent);
      if (next != kRsInvalidFilePosition) {
        offset = next;
        if (next > previous && ReadRecordHeader()) {
          ++index;
          return 1;
        }
        file.Seek((i32)previous, kRsSeekBegin);
        return 0;
      }
    }
    file.Seek((i32)offset, kRsSeekBegin);
  }
  return 0;
}

// Compare the first selected key even when selection fails, then search each successfully advanced chunk in order.
u8 RsMiffFile::Find(const RsKey &value) {
  First();
  RsKey current;
  GetKey(&current);
  for (;;) {
    if (current.instance == value.instance && current.group == value.group && current.type == value.type)
      return 1;
    if (!Next())
      return 0;
    GetKey(&current);
  }
}

// Stamp the MIFF signature, version3, caller flags and initial length28. Storage stays marked unfinished.
u8 RsMiffFile::WriteHeader(u32 first, u32 second, u32 third) {
  if (!IsOpen() || mode != 2)
    return 0;
  header[0] = kRsMiffSignature;
  header[1] = 3;
  header[2] = first;
  header[3] = second;
  header[4] = third;
  header[5] = 28;
  header[6] = 1;
  finalized = 0;
  file.Seek(0, kRsSeekBegin);
  return file.Write(header, sizeof(header));
}

// Write a chunk key and inclusive length before its payload, ignoring both header transport replies.
u8 RsMiffFile::WriteChunk(const RsKey &value, const void *data, u32 length) {
  if (!IsOpen() || mode != 2)
    return 0;
  finalized = 0;
  u32 size = length + 16;
  file.Write(&value, 12);
  file.Write(&size, 4);
  return file.Write(data, length);
}

// Append a zero-payload chunk at end of file, capturing its start and key for finalization after subsequent writes.
u8 RsMiffFile::BeginWrite(const RsKey &value) {
  if (!IsOpen() || mode != 2)
    return 0;
  file.Seek(0, kRsSeekEnd);
  offset = file.Position();
  key = value;
  return WriteChunk(key, nullptr, 0);
}

// Capture the current chunk start, then skip its16-byte header. No payload bound is imposed by this operation.
u8 RsMiffFile::BeginRead() {
  u8 result = IsOpen();
  if (result) {
    offset = file.Position();
    return file.Seek(16, kRsSeekCurrent) != kRsInvalidFilePosition;
  }
  return result;
}

u8 RsMiffFile::Read(void *out, u32 length) {
  u8 result = IsOpen();
  return result ? file.Read(out, length) : result;
}

u8 RsMiffFile::Skip(i32 distance) {
  u8 result = IsOpen();
  return result ? file.SeekRelative(distance) != kRsInvalidFilePosition : result;
}

u8 RsMiffFile::Write(const void *data, u32 length) {
  return IsOpen() && mode == 2 ? file.Write(data, length) : 0;
}

// Patch the current chunk's inclusive length and disk file-size word, ignoring each write result, then seek to end.
u8 RsMiffFile::EndWrite() {
  if (!IsOpen() || mode != 2)
    return 0;
  u32 start = offset, size = file.Position() - start;
  file.Seek((i32)start, kRsSeekBegin);
  file.Write(&key, 12);
  file.Write(&size, 4);
  file.Position();
  u32 total = file.Size();
  file.Seek(20, kRsSeekBegin);
  file.Write(&total, 4);
  finalized = 1;
  file.Seek(0, kRsSeekEnd);
  return 1;
}

// Tombstone the current chunk by overwriting its key with zero words, without requiring write mode separately.
u8 RsMiffFile::DeleteCurrent() {
  if (!IsOpen() || failed)
    return 0;
  RsKey zero;
  return file.Write(&zero, sizeof(zero));
}

// Reject the empty key, locate the first equal chunk and overwrite that chunk's key at its restored start.
u8 RsMiffFile::Delete(const RsKey &value) {
  if ((value.type || value.group || value.instance) && Find(value))
    return DeleteCurrent();
  return 0;
}

// Skip the chunk header, read a prefix capped by its inclusive size, then restore the original chunk start.
u8 RsMiffFile::ReadPayload(void *out, u32 length) {
  if (!IsOpen() || failed)
    return 0;
  u32 size = length;
  if (chunkSize < size)
    size = chunkSize;
  file.Seek(16, kRsSeekCurrent);
  u8 result = file.Read(out, size);
  file.Seek((i32)offset, kRsSeekBegin);
  return result;
}

// Copy nonzero chunks through the source's payload-size-plus-one protocol into a reserved temporary MIFF file.
// Only a completed rewrite closes/deletes the original and renames the temporary; early exits retain source state.
u8 RsMiffFile::Compact() {
  if (file.IsOpen() || !OpenRead())
    return 0;
  ScString temporary;
  if (!RsFile::MakeTempPathName(&temporary))
    return 0;
  RsMiffFile output(&temporary);
  if (!output.OpenWrite(0, 0, 0) || !output.WriteHeader(0, 0, 0))
    return 0;
  if (!First())
    return 1;
  do {
    if (key.type || key.group || key.instance) {
      u32 length = chunkSize - 16 + 1;
      u8 *data = new u8[length];
      u8 result = ReadPayload(data, length);
      if (result)
        result = output.WriteChunk(key, data, length);
      delete[] data;
      if (!result)
        return 0;
    }
  } while (Next());
  if (!output.EndWrite() || !file.Close() || !file.Delete())
    return 0;
  const ScStringView *name = file.GetName();
  return output.file.Rename(*name) != 0;
}

// Borrow the embedded file's actual filename interface without copying or retaining it.
const ScStringView *RsMiffFile::GetName() {
  return file.GetName();
}

void RsMiffFile::SetName(const ScStringView *name) {
  if (IsOpen())
    Close();
  file.SetName(*name);
}

// Open writable storage and attempt the original1/1/1 header when the MIFF signature is absent. That attempt precedes
// selecting write mode, so its mode check may reject it; setup replies are ignored before seeking to the end.
u8 RsMiffFile::OpenAppend() {
  if (!IsOpen() && !file.Open(kRsFileAccessReadWrite, kRsFileOpenAlways, kRsFileShareRead))
    return 0;
  ReadHeader();
  if (header[0] != kRsMiffSignature)
    WriteHeader(1, 1, 1);
  mode = 2;
  file.SeekEnd();
  return 1;
}

u32 RsMiffFile::GetChunkSize() {
  return chunkSize;
}

u32 RsMiffFile::GetPayloadSize() {
  return chunkSize - 16;
}

u32 RsMiffFile::GetIndex() {
  return index;
}

// Lazily read-open when necessary, then compare the stored file-length word with the actual file length.
// A successful lazy open stays open and retains its ordinary header/first-chunk setup effects.
u8 RsMiffFile::MatchesDeclaredSize() {
  if (!IsOpen()) {
    OpenRead();
    u8 result = IsOpen();
    if (!result)
      return result;
  }
  return header[5] == file.Size();
}

// Close that temporary open before the original unconditional seek back to the live chunk offset, even though the file
// may now be closed.
u8 RsMiffFile::GetFirstFlag() {
  bool temporary = false;
  if (!IsOpen()) {
    u8 result = OpenRead();
    if (!result)
      return result;
    temporary = true;
  }
  u8 result = header[2] != 0;
  if (temporary)
    Close();
  file.Seek(static_cast<i32>(offset), kRsSeekBegin);
  return result;
}

// Read the second stored header flag with the original temporary-open, close and final live-offset seek ordering.
u8 RsMiffFile::GetSecondFlag() {
  bool temporary = false;
  if (!IsOpen()) {
    u8 result = OpenRead();
    if (!result)
      return result;
    temporary = true;
  }
  u8 result = header[3] != 0;
  if (temporary)
    Close();
  file.Seek(static_cast<i32>(offset), kRsSeekBegin);
  return result;
}

// Read the third stored header flag without retaining a newly opened handle after the query completes.
// Preserve the final seek after a temporary close rather than silently moving it ahead of that close.
u8 RsMiffFile::GetThirdFlag() {
  bool temporary = false;
  if (!IsOpen()) {
    u8 result = OpenRead();
    if (!result)
      return result;
    temporary = true;
  }
  u8 result = header[4] != 0;
  if (temporary)
    Close();
  file.Seek(static_cast<i32>(offset), kRsSeekBegin);
  return result;
}

// Seek to one index before the current selection, clamped at zero, and return the requested index despite seek failure.
u32 RsMiffFile::Previous() {
  u32 previous = index;
  if (previous)
    --previous;
  SeekIndex(previous);
  return previous;
}

// Advance before comparing the requested key, then inspect each selected key through the live getter until matched.
u8 RsMiffFile::FindNext(const RsKey &value) {
  if (!Next())
    return 0;
  for (;;) {
    RsKey current;
    RsKey *selected = GetKey(&current);
    if (selected->type == value.type && selected->group == value.group && selected->instance == value.instance)
      return 1;
    if (!Next())
      return 0;
  }
}

u8 RsMiffFile::SeekKey(const RsKey &value) {
  return Find(value);
}

// Select the first chunk, then attempt every requested advance even after a failure. The original always returns one.
u8 RsMiffFile::SeekIndex(u32 selected) {
  First();
  for (; selected; --selected)
    Next();
  return 1;
}

// Count successful advances after the first chunk, then reselect the first chunk and close only a temporary open.
// Preserve this advance count rather than adding one for the initially selected chunk.
u32 RsMiffFile::CountFollowingChunks() {
  bool temporary = false;
  u32 count = 0;
  if (IsOpen()) {
    if (failed)
      return 0;
  } else {
    temporary = true;
    if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
      return 0;
  }
  First();
  while (Next())
    ++count;
  First();
  if (temporary)
    Close();
  return count;
}

// Stamp the caller's inclusive chunk length before writing its existing header and payload as one block.
// Preserve DWORD length wrapping and the actual mutation of the caller's header even if the write fails.
u8 RsMiffFile::WriteChunkBlock(void *data, u32 payloadLength) {
  if (!IsOpen() || mode != 2)
    return 0;
  finalized = 0;
  u32 length = payloadLength + 16;
  memcpy(static_cast<u8 *>(data) + 12, &length, 4);
  return file.Write(data, length);
}

u32 RsMiffFile::GetMode() {
  return mode;
}

// Construct the real segment bases and empty key set. A supplied C filename creates the backend without opening it.
RsMiffSegment::RsMiffSegment(const char *name) : file(nullptr), writing(0) {
  if (name) {
    path.FromChar(name);
    ScString copy(name);
    file = new RsMiffFile(&copy);
  }
}

RsMiffSegment::~RsMiffSegment() {
  delete file;
  file = nullptr;
}

bool RsMiffSegment::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsMiffAccess, RsMiffAccess);
  return RsSegment::QueryInterface(iid, out);
}

u32 RsMiffSegment::AddRef() {
  return RsSegment::AddRef();
}

u32 RsMiffSegment::Release() {
  return RsSegment::Release();
}

static u32 RsMiffLanguageToken(const ScString &path) {
  u32 size = path.Strlen();
  if (size >= 10)
    for (u32 i = 0; i <= size - 10; ++i)
      if (!memcmp(path.Data() + i, "%Language%", 10))
        return i;
  return kScStringNotFound;
}

// Resolve a direct path or data-directory/language path, replace an open backend and enumerate its keys.
// The original does not clear existing keys or delete a previously closed backend before overwriting its pointer.
bool RsMiffSegment::Init(u32 id, const ScStringView *name, bool resolved) {
  if (resolved) {
    ScString copy(*name);
    path = copy;
  } else {
    ScString joined(RsGetFileSystem()->DataDirectory());
    joined.Append(name->ToChar());
    path = joined;
    u32 position = RsMiffLanguageToken(path);
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
  identifier = id;
  if (file && file->IsOpen()) {
    file->Close();
    delete file;
    file = nullptr;
  }
  file = new RsMiffFile(&path);
  GetResourceKeyList(&keys);
  return true;
}

bool RsMiffSegment::Shutdown() {
  return true;
}

u32 RsMiffSegment::GetID() {
  return identifier;
}

// Enumerate sequential nonempty chunk keys into the supplied list. Stop on header-transfer failure, then close storage.
bool RsMiffSegment::GetResourceKeyList(RsKeyListInterface *out) {
  if (!file || (!file->IsOpen() && !file->OpenRead()))
    return false;
  u8 current = file->First();

  struct RsMiffKeyChunk {
    RsKey key;
    u32 size, prefix;
  } chunk;

  while (current) {
    if (!file->ReadKeyChunk(&chunk, sizeof(chunk)))
      break;
    if (chunk.key.type || chunk.key.group || chunk.key.instance)
      out->Insert(chunk.key);
    current = file->Next();
  }
  file->Close();
  return true;
}

// Reject simultaneous read/write requests and forward the selected mode to the actual MIFF backend.
bool RsMiffSegment::OpenDBSegment(bool read, bool write) {
  if ((read && write) || !file)
    return false;
  if (read)
    return file->OpenRead() != 0;
  if (write)
    return file->OpenWrite(0, 0, 0) != 0;
  return false;
}

// Open, locate and enter a sequential chunk before publishing an owned primary stream view; failure clears output.
bool RsMiffSegment::DoOpenRecord(const RsKey &key, RsRecord **out) {
  if (file && file->OpenRead() && file->Find(key) && file->BeginRead()) {
    RsMiffRecord *record = new RsMiffRecord(this, key);
    *out = static_cast<RsStream *>(record);
    record->AddRef();
    return 1;
  }
  *out = nullptr;
  return 0;
}

bool RsMiffSegment::DoCloseRecord(RsRecord **slot) {
  if (!file || !slot || !*slot)
    return 0;
  RsKey key = *(*slot)->GetKey();
  if (!keys.IsPresent(key))
    keys.Insert(key);
  u8 finished = 1;
  if (writing)
    finished = file->EndWrite();
  bool result = file->Close() && finished;
  (*slot)->Release();
  *slot = nullptr;
  writing = 0;
  return result;
}

// Clear the backend's current key before recursively entering the public close operation under the same mutex.
bool RsMiffSegment::DoAbortRecord(RsRecord **slot) {
  if (!file)
    return 0;
  RsKey zero;
  file->SetKey(zero);
  return CloseRecord(slot);
}

bool RsMiffSegment::DoTestForRecord(const RsKey &key) {
  return keys.IsPresent(key);
}

// Publish a retained record before opening write storage, tombstoning the old key and starting its replacement.
// The original ignores all three backend replies after publication and reports success whenever a backend exists.
bool RsMiffSegment::DoOpenRecordForWrite(const RsKey &key, RsRecord **out) {
  if (!file) {
    *out = nullptr;
    return 0;
  }
  RsMiffRecord *record = new RsMiffRecord(this, key);
  *out = static_cast<RsStream *>(record);
  record->AddRef();
  writing = 1;
  file->OpenWrite(0, 0, 0);
  file->Delete(key);
  file->BeginWrite(key);
  return 1;
}

bool RsMiffSegment::DoCreateNewRecord(const RsKey &key, RsRecord **out) {
  return OpenRecordForWrite(key, out);
}

bool RsMiffSegment::DoDeleteRecord(const RsKey &key) {
  return file && file->IsOpen() && file->mode == 2 ? file->Delete(key) : 0;
}

u8 RsMiffSegment::ReadRecord(void *out, u32 length) {
  return file && file->IsOpen() ? file->Read(out, length) : 0;
}

u8 RsMiffSegment::SkipRecord(i32 distance) {
  return file && file->IsOpen() ? file->Skip(distance) : 0;
}

u8 RsMiffSegment::WriteRecord(const void *data, u32 length) {
  return file && file->IsOpen() && file->mode == 2 ? file->Write(data, length) : 0;
}

u8 RsMiffSegment::Compact() {
  return file ? file->Compact() : 0;
}

RsMiffRecord::RsMiffRecord(RsMiffSegment *segment, const RsKey &value) : owner(segment) {
  RsRecordObject::SetKey(value);
}

bool RsMiffRecord::GetSize(u32 *out) {
  *out = 0;
  return 0;
}

bool RsMiffRecord::ReadBlock(void *out, u32 length) {
  return owner->ReadRecord(out, length);
}

bool RsMiffRecord::WriteBlock(const void *data, u32 length) {
  return owner->WriteRecord(data, length);
}

bool RsMiffRecord::SkipBytes(u32 distance) {
  return owner->SkipRecord(static_cast<i32>(distance)) != 0;
}

// Return the original registered producer's secondary compaction identity at zero initial references.
ScUnknown *RsCreateClass199925() {
  return static_cast<RsMiffAccess *>(new RsMiffSegment(nullptr));
}

static bool RsPackedCheckExtent(const RsPackedSegment *owner, const u8 *position, u32 length) {
  uptr begin = reinterpret_cast<uptr>(owner->data), current = reinterpret_cast<uptr>(position);
  return current >= begin && current - begin <= owner->capacity && length <= owner->capacity - (current - begin);
}

RsPackedRecord::RsPackedRecord(RsPackedSegment *owner, const RsKey &value, u8 *data, u32 capacity, u8 writable)
    : RsRecordStream(value), owner(owner), data(data), cursor(data), writable(writable), remaining(capacity),
      capacity(capacity) {
}

RsPackedBufferedRecord::RsPackedBufferedRecord(const RsKey &value) : RsRecordStream(value), size(0) {
}

RsPackedBufferedRecord::~RsPackedBufferedRecord() {
  for (auto &chunk : chunks)
    delete[] chunk.data;
}

bool RsPackedRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsPackedData, RsPackedData);
  return RsRecordStream::QueryInterface(iid, out);
}

u32 RsPackedRecord::AddRef() {
  return RsRecordStream::AddRef();
}

u32 RsPackedRecord::Release() {
  return RsRecordStream::Release();
}

// Borrow the original key from the single controlling record base.
RsKey *RsPackedRecord::GetKey() {
  return RsRecordStream::GetKey();
}

bool RsPackedBufferedRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsPackedData, RsPackedData);
  return RsRecordStream::QueryInterface(iid, out);
}

u32 RsPackedBufferedRecord::AddRef() {
  return RsRecordStream::AddRef();
}

u32 RsPackedBufferedRecord::Release() {
  return RsRecordStream::Release();
}

// Borrow the original key from the single controlling record base.
RsKey *RsPackedBufferedRecord::GetKey() {
  return RsRecordStream::GetKey();
}

bool RsPackedRecord::GetSize(u32 *out) {
  *out = writable ? static_cast<u32>(cursor - data) : capacity;
  return 1;
}

bool RsPackedRecord::ReadBlock(void *out, u32 size) {
  if (writable || static_cast<i32>(size) > static_cast<i32>(remaining))
    return 0;
  if (!RsPackedCheckExtent(owner, cursor, size))
    return false;
  if (size)
    memcpy(out, cursor, size);
  cursor += size;
  remaining -= size;
  return 1;
}

bool RsPackedRecord::SkipBytes(u32 distance) {
  i32 displacement = static_cast<i32>(distance);
  if (writable || displacement > static_cast<i32>(remaining))
    return 0;
  iptr next = reinterpret_cast<iptr>(cursor) + displacement;
  u8 *position = reinterpret_cast<u8 *>(next);
  if (!RsPackedCheckExtent(owner, position, 0))
    return false;
  cursor = position;
  remaining -= distance;
  return 1;
}

// Grow the owner for an undersized writable extent, then reload the relocated cursor before copying and advancing.
bool RsPackedRecord::WriteBlock(const void *source, u32 size) {
  if (!writable)
    return 0;
  if (static_cast<i32>(size) > static_cast<i32>(remaining) && !owner->GrowBuffer(size - remaining))
    return 0;
  if (!RsPackedCheckExtent(owner, cursor, size))
    return false;
  if (size)
    memcpy(cursor, source, size);
  cursor += size;
  remaining -= size;
  return 1;
}

u8 RsPackedRecord::IsWritable() {
  return writable;
}

// Preserve already written bytes when installing a larger backing extent; readonly records reject relocation.
u8 RsPackedRecord::SetBuffer(u8 *replacement, u32 size) {
  if (!writable)
    return 0;
  u32 written = 0;
  GetSize(&written);
  if (size < written)
    return 0;
  capacity = size;
  remaining = size - written;
  if (data != replacement) {
    data = replacement;
    cursor = replacement + written;
  }
  return 1;
}

bool RsPackedRecord::StoreData(u8 *destination, u32 size) {
  return writable && data == destination && capacity >= size;
}

u8 RsPackedRecord::DoesRecordAllocateMemory() const {
  return 0;
}

bool RsPackedBufferedRecord::GetSize(u32 *out) {
  *out = size;
  return 1;
}

bool RsPackedBufferedRecord::ReadBlock(void *, u32) {
  return 0;
}

// Allocate and copy one independent chunk for each call before increasing the source's wrapping DWORD total.
bool RsPackedBufferedRecord::WriteBlock(const void *source, u32 length) {
  u8 *bytes = new u8[length];
  if (length)
    memcpy(bytes, source, length);
  chunks.push_back({length, bytes});
  size += length;
  return 1;
}

bool RsPackedBufferedRecord::SkipBytes(u32) {
  return 0;
}

u8 RsPackedBufferedRecord::IsWritable() {
  return 1;
}

u8 RsPackedBufferedRecord::SetBuffer(u8 *, u32) {
  return 1;
}

// Copy chunks in write order only when their recorded total fits the supplied destination extent.
bool RsPackedBufferedRecord::StoreData(u8 *destination, u32 capacity) {
  if (size > capacity)
    return false;
  u32 remaining = capacity;
  for (const auto &chunk : chunks)
    if (chunk.data) {
      if (chunk.size > remaining)
        return false;
      if (chunk.size)
        memcpy(destination, chunk.data, chunk.size);
      destination += chunk.size;
      remaining -= chunk.size;
    }
  return true;
}

u8 RsPackedBufferedRecord::DoesRecordAllocateMemory() const {
  return 1;
}

// Hold the source's recursive segment mutex through actual record and codec calls.
class RsPackedLock {
public:
  // Acquire the supplied real mutex before entering the guarded operation.
  explicit RsPackedLock(ScLock *lock) : lock(lock) {
    lock->Lock();
  }

  // Release the same mutex when the original stack guard leaves scope.
  ~RsPackedLock() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

bool RsPackedKeyLess::operator()(const RsKey &left, const RsKey &right) const {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

// Retain the original list's copied interface pointer before appending its node. This is a native storage phase.
static void RsPackedAppend(std::list<RsPackedData *> *list, RsPackedData *record) {
  if (record)
    record->AddRef();
  list->push_back(record);
}

// Release each owned interface pointer before clearing the native list, matching the source element destructors.
static void RsPackedClearList(std::list<RsPackedData *> *list) {
  for (RsPackedData *record : *list)
    if (record)
      record->Release();
  list->clear();
}

static bool RsPackedSameKey(const RsKey &left, const RsKey &right) {
  return left.instance == right.instance && left.group == right.group && left.type == right.type;
}

RsPackedSegment::RsPackedSegment(u32 version)
    : version(version), packedSize(0), ready(0), writable(0), initialSize(0), growth(0), capacity(0), used(0),
      data(nullptr), activeRecord(nullptr) {
}

// Free the data before destroying the offset tree and retained pending/open records, then destroy the codec String/base.
// The original does not release a separately active record here; normal completion or abort owns that transition.
RsPackedSegment::~RsPackedSegment() {
  ClearData();
  index.clear();
  RsPackedClearList(&pending);
  RsPackedClearList(&records);
}

bool RsPackedSegment::Init(u32, const ScStringView *, bool) {
  return false;
}

bool RsPackedSegment::Shutdown() {
  return false;
}

u32 RsPackedSegment::GetID() {
  return key.instance;
}

// Insert every indexed key in the source's ordered traversal without checking readiness or individual insert replies.
bool RsPackedSegment::GetResourceKeyList(RsKeyListInterface *out) {
  for (const auto &entry : index)
    out->Insert(entry.first);
  return true;
}

bool RsPackedSegment::OpenDBSegment(bool, bool) {
  return false;
}

// Select a layer key and read its payload only while uninitialized, publishing readiness after successful buffer decoding.
u8 RsPackedSegment::Read(const RsKey &value, RsStream *stream) {
  RsPackedLock guard(&lock);
  if (ready)
    return 0;
  key = value;
  writable = 0;
  capacity = 0;
  used = 0;
  if (!ReadBuffer(stream))
    return 0;
  ready = 1;
  return 1;
}

// Allocate the initial writable extent and reserve its first eight index-header bytes without opening a record.
// Native zero initialization excludes the source's unwritten reserved/trailing bytes from later buffer copies.
u8 RsPackedSegment::Create(const RsKey &value, u32 initial, u32 increment) {
  RsPackedLock guard(&lock);
  if (ready)
    return 0;
  key = value;
  writable = 1;
  used = 0;
  capacity = 0;
  packedSize = 0;
  initialSize = initial;
  growth = increment;
  activeRecord = nullptr;
  data = new u8[initial]{};
  capacity = initialSize;
  used = 8;
  ready = 1;
  return 1;
}

// Read the source version/codec/length fields, decode the actual QFS bytes and build the original offset index.
// Failed decoding frees the owned output buffer but preserves the source's already read metadata fields.
u8 RsPackedSegment::ReadBuffer(RsStream *stream) {
  if (writable)
    return 0;
  u8 result = stream->ReadU32(&version);
  result = result && stream->ReadString(&codecName) && stream->ReadU32(&packedSize) && stream->ReadU32(&capacity);
  used = capacity;
  if (result) {
    u32 compressedCapacity = packedSize;
    u8 *compressed = new u8[compressedCapacity]();
    result = stream->ReadBlock(compressed, packedSize);
    if (result) {
      data = new u8[capacity]();
      u32 written = capacity;
      if (version == 101) {
        result = RsDecodeImageQfs(compressed, packedSize, data, &written);
      } else if (version == 103) {
        RsQfsCodec *codec = new RsQfsCodec;
        codec->AddRef();
        result = codec->Decompress(compressed, packedSize, data, &written);
        if (written != capacity)
          result = 0;
        codec->Release();
      } else
        result = 0;
      if (written != capacity)
        result = 0;
    }
    delete[] compressed;
    if (result && ReadIndex())
      return 1;
  }
  if (data) {
    delete[] data;
    data = nullptr;
    capacity = 0;
    used = 0;
  }
  return 0;
}

u8 RsPackedSegment::ReadIndex() {
  RsPackedLock guard(&lock);
  u32 count, begin;
  memcpy(&count, data, 4);
  memcpy(&begin, data + 4, 4);
  if (begin + 16u * count > capacity)
    return 0;
  for (u32 i = 0; i < count; ++i) {
    const u8 *source = data + begin + 16u * i;
    u32 row[4];
    memcpy(&row[0], source, 4);
    memcpy(&row[3], source + 12, 4);
    if (row[3] < begin) {
      memcpy(&row[1], source + 4, 8);
      index.insert({RsKey(row[0], row[1], row[2]), row[3]});
    }
  }
  return 1;
}

// Finish records and append the index before encoding a writable buffer, ignoring the original preparation replies.
// Readonly mode releases data and succeeds; writable mode preserves the original preparation call ordering.
u8 RsPackedSegment::Write(RsStream *stream) {
  RsPackedLock guard(&lock);
  if (!writable) {
    ClearData();
    return 1;
  }
  if (!ready)
    return 0;
  FinishRecords();
  WriteIndex();
  bool result = WriteBuffer(stream);
  ClearData();
  return result;
}

// Encode the actual buffer with the selected version's QFS producer, then write the original ordered stream fields.
bool RsPackedSegment::WriteBuffer(RsStream *stream) {
  if (!writable)
    return false;
  u8 *compressed;
  u8 result;
  if (version == 101) {
    u32 size = (10u * used) / 9u + 1000u;
    compressed = new u8[size];
    codecName.FromChar("0.90");
    packedSize = size;
    i32 fast = 1;
    packedSize = RsQfsEncode(data, static_cast<i32>(used), compressed + 4, &fast) + 4;
    memcpy(compressed, &packedSize, 4);
    result = packedSize != 0;
  } else if (version == 103) {
    RsQfsCodec *codec = new RsQfsCodec;
    codec->AddRef();
    u32 size = codec->GetBufferSize(used);
    compressed = new u8[size];
    codec->GetVersionString(&codecName);
    packedSize = size;
    result = codec->Compress(data, used, compressed, &packedSize);
    codec->Release();
  } else
    return false;
  result = result && stream->WriteU32(version) && stream->WriteString(&codecName) && stream->WriteU32(packedSize) &&
      stream->WriteU32(used) && stream->WriteBlock(compressed, packedSize);
  delete[] compressed;
  return result != 0;
}

// Append ordered index rows after payload data, publishing total count even when an invalid future offset is skipped.
u8 RsPackedSegment::WriteIndex() {
  RsPackedLock guard(&lock);
  u32 count = static_cast<u32>(index.size()), bytes = 16u * count;
  if (capacity - used < bytes)
    ResizeBuffer(used + bytes);
  memcpy(data, &count, 4);
  memcpy(data + 4, &used, 4);
  u32 position = used;
  for (const auto &entry : index)
    if (entry.second < used) {
      u32 row[] = {entry.first.type, entry.first.group, entry.first.instance, entry.second};
      memcpy(data + position, row, sizeof(row));
      position += 16;
    }
  used += bytes;
  return 1;
}

// Round the required additional capacity up by the source's configured growth quantum, retaining DWORD arithmetic.
u8 RsPackedSegment::GrowBuffer(u32 additional) {
  u32 size = growth;
  for (; size < additional; size += growth)
    ;
  return ResizeBuffer(size + capacity);
}

// Grow owned storage and relocate the active direct record before releasing its old allocation.
// Ordinary array allocation replaces realloc while retaining defined bytes and avoiding dangling-pointer subtraction.
u8 RsPackedSegment::ResizeBuffer(u32 size) {
  RsPackedLock guard(&lock);
  if (!writable)
    return 0;
  if (size <= capacity)
    return 1;
  u8 *old = data, *replacement = new u8[size]{};
  if (capacity)
    memcpy(replacement, old, capacity);
  data = replacement;
  capacity = size;
  u8 result = activeRecord ? activeRecord->SetBuffer(data + used, size - used) : 1;
  delete[] old;
  return result;
}

// Finish an active direct record, transfer open buffered records into the pending list and commit pending data.
u8 RsPackedSegment::FinishRecords() {
  if (!writable)
    return 0;
  u8 result = activeRecord ? FinishActive() : 1;
  for (RsPackedData *record : records)
    RsPackedAppend(&pending, record);
  RsPackedClearList(&records);
  if (!pending.empty()) {
    result = FinishPending();
    RsPackedClearList(&pending);
  }
  return result;
}

// Index the active direct record only when its bytes already occupy the current packed-buffer tail; release it afterward.
u8 RsPackedSegment::FinishActive() {
  if (!activeRecord)
    return 0;
  u32 size = 0;
  activeRecord->GetSize(&size);
  bool result = data && used <= capacity && activeRecord->StoreData(data + used, capacity - used);
  if (result) {
    index.insert({*activeRecord->GetKey(), used});
    used += size;
  }
  activeRecord->Release();
  activeRecord = nullptr;
  return result;
}

// Retain a pending copy of a buffered record and defer copying while a direct record remains active.
u8 RsPackedSegment::QueueRecord(RsPackedData *record) {
  record->AddRef();
  RsPackedAppend(&pending, record);
  record->Release();
  return activeRecord ? 1 : FinishPending();
}

// Reserve space for all pending bytes and commit records in reverse list order. Individual copy rejections are ignored.
// A pending tail outside the actual allocation fails after preserving the already reached record effects.
u8 RsPackedSegment::FinishPending() {
  u32 total = 0;
  for (auto it = pending.rbegin(); it != pending.rend(); ++it)
    if (*it) {
      u32 size = 0;
      (*it)->GetSize(&size);
      total += size;
    }
  u32 required = used + total, size = capacity;
  u8 result = 1;
  if (size < required) {
    do {
      size += growth;
    } while (size < required);
    result = ResizeBuffer(size);
  }
  if (result) {
    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
      if (*it) {
        u32 length = 0;
        (*it)->GetSize(&length);
        if (used > capacity) {
          result = 0;
          break;
        }
        if ((*it)->StoreData(data + used, capacity - used)) {
          index.insert({*(*it)->GetKey(), used});
          used += length;
        }
      }
    RsPackedClearList(&pending);
  }
  return result;
}

// Clear readiness and buffer lengths, then free the owned data. Indexed keys and record-list ownership remain untouched.
u8 RsPackedSegment::ClearData() {
  ready = 0;
  capacity = 0;
  used = 0;
  delete[] data;
  data = nullptr;
  return 1;
}

// Publish a readonly controlling record spanning its indexed offset through the remaining backing allocation.
// The open-record list keeps it alive until the caller retires it through segment close or abort.
bool RsPackedSegment::DoOpenRecord(const RsKey &value, RsRecord **out) {
  RsPackedLock guard(&lock);
  auto found = index.find(value);
  if (writable || found == index.end())
    return 0;
  u32 offset = found->second;
  if (offset > capacity || !data)
    return false;
  RsPackedRecord *record = new RsPackedRecord(this, value, data + offset, capacity - offset, writable);
  bool result = record->QueryInterface(IID_RsRecord, reinterpret_cast<void **>(out));
  if (result) {
    RsPackedData *access = record;
    access->AddRef();
    RsPackedAppend(&records, access);
    access->Release();
  }
  record->Release();
  return result;
}

// Query packed-data identity, match keys against active/open records and apply the source's mode-specific commit ownership.
bool RsPackedSegment::DoCloseRecord(RsRecord **slot) {
  RsPackedLock guard(&lock);
  if (!ready || !*slot)
    return 0;
  RsPackedData *access = nullptr;
  if (!(*slot)->QueryInterface(IID_RsPackedData, reinterpret_cast<void **>(&access)))
    return 0;
  RsKey value = *access->GetKey();
  if (!writable) {
    for (auto it = records.begin(); it != records.end(); ++it)
      if (*it && RsPackedSameKey(*(*it)->GetKey(), value)) {
        RsPackedData *record = *it;
        records.erase(it);
        record->Release();
        access->Release();
        *slot = nullptr;
        return 1;
      }
    access->Release();
    return 0;
  }
  if (activeRecord && RsPackedSameKey(*activeRecord->GetKey(), value)) {
    u8 result = FinishActive();
    if (result) {
      activeRecord = nullptr;
      *slot = nullptr;
    }
    access->Release();
    return result;
  }
  for (auto it = records.begin(); it != records.end(); ++it)
    if (*it && RsPackedSameKey(*(*it)->GetKey(), value)) {
      u8 result = QueueRecord(*it);
      RsPackedData *record = *it;
      records.erase(it);
      record->Release();
      access->Release();
      if (result)
        *slot = nullptr;
      return 1;
    }
  access->Release();
  return 0;
}

bool RsPackedSegment::DoAbortRecord(RsRecord **slot) {
  RsPackedLock guard(&lock);
  if (!ready || !*slot)
    return 0;
  RsKey value = *(*slot)->GetKey();
  if (activeRecord && RsPackedSameKey(*activeRecord->GetKey(), value)) {
    activeRecord->Release();
    activeRecord = nullptr;
    *slot = nullptr;
    return 1;
  }
  for (auto it = records.begin(); it != records.end(); ++it)
    if (*it && RsPackedSameKey(*(*it)->GetKey(), value)) {
      RsPackedData *record = *it;
      records.erase(it);
      record->Release();
      *slot = nullptr;
      return 1;
    }
  return 0;
}

// Test indexed keys before pending buffered records. The currently active and open lists are not searched here.
bool RsPackedSegment::DoTestForRecord(const RsKey &value) {
  RsPackedLock guard(&lock);
  if (index.find(value) != index.end())
    return true;
  for (RsPackedData *record : pending)
    if (record && RsPackedSameKey(*record->GetKey(), value))
      return true;
  return false;
}

// Reject duplicate indexed/active/open/pending keys, then use a direct writer or an independently buffered extra writer.
// Active/list ownership keeps returned records alive until close or abort performs their reference retirement.
bool RsPackedSegment::DoOpenRecordForWrite(const RsKey &value, RsRecord **out) {
  RsPackedLock guard(&lock);
  if (!writable || index.find(value) != index.end())
    return 0;
  if (activeRecord && RsPackedSameKey(*activeRecord->GetKey(), value))
    return 0;
  for (RsPackedData *record : records)
    if (record && RsPackedSameKey(*record->GetKey(), value))
      return 0;
  for (RsPackedData *record : pending)
    if (record && RsPackedSameKey(*record->GetKey(), value))
      return 0;
  if (!activeRecord) {
    if (used > capacity || !data)
      return false;
    RsPackedRecord *record = new RsPackedRecord(this, value, data + used, capacity - used, writable);
    activeRecord = record;
    bool result = activeRecord->QueryInterface(IID_RsRecord, reinterpret_cast<void **>(out));
    if (!result) {
      delete record;
      activeRecord = nullptr;
    }
    return result;
  }
  RsPackedBufferedRecord *record = new RsPackedBufferedRecord(value);
  bool result = record->QueryInterface(IID_RsRecord, reinterpret_cast<void **>(out));
  if (result) {
    RsPackedData *access = record;
    access->AddRef();
    RsPackedAppend(&records, access);
    access->Release();
  }
  record->Release();
  return result;
}

bool RsPackedSegment::DoCreateNewRecord(const RsKey &value, RsRecord **out) {
  return DoOpenRecordForWrite(value, out);
}

bool RsPackedSegment::DoDeleteRecord(const RsKey &) {
  return 0;
}

RsQfsCodec::RsQfsCodec() : references(0) {
}

RsQfsCodec::~RsQfsCodec() {
}

// Publish the same primary receiver for Unknown and the codec IID, clearing the output on rejection.
bool RsQfsCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsQfsCodec);
  HANDLE_QUERY_INTERFACE(IID_RsCompression, RsQfsCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsQfsCodec::AddRef() {
  return ++references;
}

// Delete at zero or one reference, otherwise decrement and return the remaining count.
u32 RsQfsCodec::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Encode after the outer length word, then publish the caller's length before writing the leading stored length.
bool RsQfsCodec::Compress(const void *source, u32 size, u8 *destination, u32 *written) {
  i32 mode = 1;
  u32 result = RsQfsEncodeAlt(static_cast<const u8 *>(source), (i32)size, destination + 4, &mode) + 4;
  *written = result;
  memcpy(destination, &result, 4);
  return *written != 0;
}

// Decode after the outer length word and publish the declared output size. Preserve the original catch-all rejection.
bool RsQfsCodec::Decompress(const u8 *source, u32 size, void *destination, u32 *written) {
  u32 result = ScQfsDecompress(source + 4, &size, static_cast<u8 *>(destination));
  *written = result;
  return result != 0;
}

u32 RsQfsCodec::GetBufferSize(u32 size) {
  return (10u * size) / 9u + 1000u;
}

// Borrow the outer little-endian length word without imposing an additional stream-header check.
u32 RsQfsCodec::GetCompressedSize(const u8 *source) {
  u32 size;
  memcpy(&size, source, sizeof(size));
  return size;
}

u32 RsQfsCodec::GetUncompressedSize(const u8 *source) {
  return ScQfsGetSize(source + 4);
}

// Allocate the concrete zero-reference codec registered for class035990A3 by the resource director.
ScUnknown *RsCreateClass35990A3() {
  return new RsQfsCodec();
}

void RsQfsCodec::GetVersionString(ScStringView *out) {
  ScString version("0.90");
  out->Copy(version);
}

// Adapted from the published SIMBABLD encoder after matching both resource-module copies in IDA.

static u32 RsQfsHash(const u8 *source) {
  return (static_cast<u32>(source[0]) << 8 | source[2]) ^ (static_cast<u32>(source[1]) << 4);
}

// Emit aligned literal runs in the original maximum 112-byte packet size.
static void RsQfsWriteLiterals(const u8 **source, u32 *count, u8 **destination) {
  while (*count > 3) {
    u32 size = *count & ~3u;
    if (size > 112)
      size = 112;
    *count -= size;
    *(*destination)++ = static_cast<u8>((size >> 2) - 33);
    memcpy(*destination, *source, size);
    *source += size;
    *destination += size;
  }
}

// Encode QFS literal and back-reference packets using the original hash chains and match-cost selection.
// The final one or two input bytes skip hash probes that would read outside the defined input.
u32 RsQfsEncodeBody(const u8 *source, i32 length, u8 *destination, i32, i32 fast) {
  i32 *heads = new i32[0x10000], *chains = new i32[0x20000];
  for (u32 i = 0; i < 0x10000; ++i)
    heads[i] = -1;
  const u8 *cursor = source, *literals = source;
  u8 *output = destination;
  u32 pending = 0;
  i32 remaining = length;
  while (remaining > 0) {
    u32 position = static_cast<u32>(cursor - source);
    if (remaining < 3) {
      pending += static_cast<u32>(remaining);
      cursor += remaining;
      break;
    }
    u32 hash = RsQfsHash(cursor), best_length = 2, best_header = 2, best_distance = 0;
    u32 limit = remaining < 1028 ? static_cast<u32>(remaining) : 1028;
    i32 candidate = heads[hash];
    i32 oldest = position > 0x1ffff ? static_cast<i32>(position - 0x1ffff) : 0;
    while (candidate >= oldest && best_length < limit) {
      const u8 *match = source + candidate;
      if (cursor[best_length] == match[best_length]) {
        u32 matched = 0;
        while (matched < limit && cursor[matched] == match[matched])
          ++matched;
        if (matched > best_length) {
          u32 distance = position - static_cast<u32>(candidate) - 1;
          u32 header = distance >= 0x400 || matched > 10 ? (distance >= 0x4000 || matched > 67 ? 4 : 3) : 2;
          if (matched - header + 4 > best_length - best_header + 4) {
            best_header = header;
            best_length = matched;
            best_distance = distance;
            if (matched >= 1028)
              break;
          }
        }
      }
      candidate = chains[static_cast<u32>(candidate) & 0x1ffff];
    }
    if (best_header >= best_length) {
      ++pending;
      --remaining;
      chains[position & 0x1ffff] = heads[hash];
      heads[hash] = static_cast<i32>(position);
      ++cursor;
      continue;
    }
    RsQfsWriteLiterals(&literals, &pending, &output);
    if (best_header == 2) {
      *output++ = static_cast<u8>(pending + 4 * (best_length + 61) + ((best_distance >> 3) & 0xe0));
      *output++ = static_cast<u8>(best_distance);
    } else if (best_header == 3) {
      *output++ = static_cast<u8>(best_length + 124);
      *output++ = static_cast<u8>((pending << 6) + (best_distance >> 8));
      *output++ = static_cast<u8>(best_distance);
    } else {
      *output++ = static_cast<u8>(pending + ((best_distance >> 12) & 0xf0) + (((best_length - 5) >> 6) & 0xfc) - 64);
      *output++ = static_cast<u8>(best_distance >> 8);
      *output++ = static_cast<u8>(best_distance);
      *output++ = static_cast<u8>(best_length - 5);
    }
    if (pending) {
      memcpy(output, literals, pending);
      output += pending;
      pending = 0;
    }
    if (fast) {
      chains[position & 0x1ffff] = heads[hash];
      heads[hash] = static_cast<i32>(position);
      cursor += best_length;
    } else {
      for (u32 i = 0; i < best_length; ++i) {
        u32 offset = static_cast<u32>(cursor - source);
        if (remaining - static_cast<i32>(i) >= 3) {
          u32 next_hash = RsQfsHash(cursor);
          chains[offset & 0x1ffff] = heads[next_hash];
          heads[next_hash] = static_cast<i32>(offset);
        }
        ++cursor;
      }
    }
    literals = cursor;
    remaining -= static_cast<i32>(best_length);
  }
  RsQfsWriteLiterals(&literals, &pending, &output);
  *output++ = static_cast<u8>(pending - 4);
  if (pending) {
    memcpy(output, literals, pending);
    output += pending;
  }
  delete[] chains;
  delete[] heads;
  return static_cast<u32>(output - destination);
}

u32 SC3_STDCALL RsQfsEncodeBodyAlt(const u8 *source, i32 length, u8 *destination, i32 window, i32 fast) {
  return RsQfsEncodeBody(source, length, destination, window, fast);
}

// Write the short QFS header, selecting fast chain updates only when the supplied flag is present and nonzero.
u32 RsQfsEncode(const u8 *source, i32 length, u8 *destination, const i32 *fast) {
  bool enabled = fast && *fast != 0;
  destination[0] = 0x10;
  destination[1] = 0xfb;
  destination[2] = static_cast<u8>(static_cast<u32>(length) >> 16);
  destination[3] = static_cast<u8>(static_cast<u32>(length) >> 8);
  destination[4] = static_cast<u8>(length);
  return RsQfsEncodeBody(source, length, destination + 5, 0x20000, enabled) + 5;
}

// Write the original QFS signature and 24-bit uncompressed length before encoding the packet stream.
// The fourth argument is unused by this source implementation.
u32 SC3_STDCALL RsQfsEncodeAlt(const u8 *source, i32 length, u8 *destination, const i32 *) {
  destination[0] = 0x10;
  destination[1] = 0xfb;
  destination[2] = static_cast<u8>(static_cast<u32>(length) >> 16);
  destination[3] = static_cast<u8>(static_cast<u32>(length) >> 8);
  destination[4] = static_cast<u8>(length);
  return RsQfsEncodeBodyAlt(source, length, destination + 5, 0x20000, 1) + 5;
}

// These are the installed SIMSPR10062430 bucket sizes, not a replacement hash policy.
static constexpr u32 kRsIndexPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613,
    393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457,
    1610612741, 3221225473, 4294967291};

static u32 RsIndexBucket(const RsKey &key, u32 count) {
  return (key.type + key.group + key.instance) % count;
}

RsRecordIndex::RsRecordIndex() : count(0), bucket_count(193), buckets(new RsIndexRow *[193]{}) {
}

RsRecordIndex::~RsRecordIndex() {
  Clear();
  delete[] buckets;
}

// Delete every live row while retaining buckets, matching reinitialization of the source's unique index.
void RsRecordIndex::Clear() {
  for (u32 i = 0; i < bucket_count; ++i) {
    while (buckets[i]) {
      RsIndexRow *row = buckets[i];
      buckets[i] = row->next;
      delete row;
    }
  }
  count = 0;
}

// Select the first equal TGI from its actual bucket chain. Offsets and sizes do not participate in key equality.
RsIndexRow *RsRecordIndex::Find(const RsKey &key) const {
  for (RsIndexRow *row = buckets[RsIndexBucket(key, bucket_count)]; row; row = row->next)
    if (row->key.type == key.type && row->key.group == key.group && row->key.instance == key.instance)
      return row;
  return nullptr;
}

// Grow through the installed prime table and move old chains head-first into new bucket heads. This preserves
// the original reversal and cross-bucket merge order that callers can observe during resource-key enumeration.
void RsRecordIndex::Grow(u32 minimum) {
  if (minimum <= bucket_count)
    return;
  u32 size = kRsIndexPrimes[27];
  for (u32 prime : kRsIndexPrimes)
    if (prime >= minimum) {
      size = prime;
      break;
    }
  if (size <= bucket_count)
    return;
  RsIndexRow **next = new RsIndexRow *[size] {};
  for (u32 i = 0; i < bucket_count; ++i)
    while (buckets[i]) {
      RsIndexRow *row = buckets[i];
      buckets[i] = row->next;
      u32 slot = RsIndexBucket(row->key, size);
      row->next = next[slot];
      next[slot] = row;
    }
  delete[] buckets;
  buckets = next;
  bucket_count = size;
}

// Resize before duplicate lookup, as the source does, and retain the first equal record's original extent.
void RsRecordIndex::Insert(const RsKey &key, u32 offset, u32 size) {
  Grow(count + 1);
  if (Find(key))
    return;
  u32 slot = RsIndexBucket(key, bucket_count);
  buckets[slot] = new RsIndexRow{buckets[slot], key, offset, size};
  ++count;
}

// Find the first row in ascending bucket order. Empty buckets contribute no enumerated keys.
RsIndexRow *RsRecordIndex::First() const {
  for (u32 i = 0; i < bucket_count; ++i)
    if (buckets[i])
      return buckets[i];
  return nullptr;
}

// Reload the current chain link, then continue after the live key's bucket. Comparison excludes invalidated iterators.
RsIndexRow *RsRecordIndex::Next(RsIndexRow *row) const {
  if (row->next)
    return row->next;
  for (u32 i = RsIndexBucket(row->key, bucket_count) + 1; i < bucket_count; ++i)
    if (buckets[i])
      return buckets[i];
  return nullptr;
}

// Unlink only the first equal TGI node, retaining all other chain links and the current bucket allocation.
void RsRecordIndex::Erase(const RsKey &key) {
  RsIndexRow **link = &buckets[RsIndexBucket(key, bucket_count)];
  while (*link) {
    RsIndexRow *row = *link;
    if (row->key.type == key.type && row->key.group == key.group && row->key.instance == key.instance) {
      *link = row->next;
      delete row;
      --count;
      return;
    }
    link = &row->next;
  }
}

RsRecordObject::RsRecordObject() : references(0), key{} {
}

RsRecordObject::~RsRecordObject() {
}

// Publish the controlling record pointer for Unknown or the record IID before the virtual retain. Failure clears the
// output.
bool RsRecordObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsRecord);
  HANDLE_QUERY_INTERFACE(IID_RsRecord, RsRecord);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsRecordObject::AddRef() {
  return ++references;
}

// Delete the complete dynamic object at zero or one reference. Larger counts decrement without destroying it.
u32 RsRecordObject::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Borrow the record's actual mutable key. Both stream and record views expose this same storage.
RsKey *RsRecordObject::GetKey() {
  return &key;
}

u32 RsRecordObject::RefCount() {
  return references;
}

RsRecordStream::RsRecordStream() {
}

RsRecordStream::~RsRecordStream() {
}

bool RsRecordStream::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsStream, RsStream);
  return RsRecordObject::QueryInterface(iid, out);
}

u32 RsRecordStream::AddRef() {
  return RsRecordObject::AddRef();
}

u32 RsRecordStream::Release() {
  return RsRecordObject::Release();
}

// Borrow the controlling record's key through the primary implementation. No copy or extra key identity is introduced.
RsKey *RsRecordStream::GetKey() {
  return RsRecordObject::GetKey();
}

RsRecordObject::RsRecordObject(const RsKey &value) : references(0), key(value) {
}

RsRecordStream::RsRecordStream(const RsKey &value) : RsRecordObject(value) {
}

void RsRecordObject::SetKey(const RsKey &value) {
  key = value;
}

bool RsRecordStream::ReadElements(void *out, u32 count, u32 elementSize) {
  return ReadBlock(out, count * elementSize) != 0;
}

bool RsRecordStream::WriteElements(const void *data, u32 count, u32 elementSize) {
  return WriteBlock(data, count * elementSize) != 0;
}
