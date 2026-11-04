#include "resources/indexed_file.h"

#include <string.h>

#include "common/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"

static ScLock RsSegmentRefLock;

// Adjust the unsigned count under the module's independent counter lock. Return the lock release result unchanged.
static int RsUpdateSegmentRef(u32 *counter, i32 delta) {
  RsSegmentRefLock.Lock();
  *counter += (u32)delta;
  return RsSegmentRefLock.Unlock();
}

RsSegment::RsSegment() : lock_ready(lock.initialized != 0), references(0) {
}

RsSegment::~RsSegment() {
}

// Publish the controlling database-segment view before the virtual retain. Unsupported queries clear the output.
bool RsSegment::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsDatabaseSegment);
  HANDLE_QUERY_INTERFACE(IID_RsDatabaseSegment, RsDatabaseSegment);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsSegment::AddRef() {
  RsUpdateSegmentRef(&references, 1);
  return references;
}

// Delete the complete object at zero or one reference. Larger counts decrement under the lock and reread afterward.
u32 RsSegment::Release() {
  if (references > 1) {
    RsUpdateSegmentRef(&references, -1);
    return references;
  }
  delete this;
  return 0;
}

RsIndexedFile::RsIndexedFile(char *name)
    : opened(0), file(name), depth(0), physical(0), cache(nullptr, {}), busy(0), reuse(0) {
  cache.AddRef();
}

RsIndexedFile::~RsIndexedFile() {
  Shutdown();
}

bool RsIndexedFile::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsIndexedAccess, RsIndexedAccess);
  return RsSegment::QueryInterface(iid, out);
}

u32 RsIndexedFile::AddRef() {
  return RsSegment::AddRef();
}

u32 RsIndexedFile::Release() {
  return RsSegment::Release();
}

// Select read-only or write-only access through the live secondary interface. Equal flags return failure immediately.
bool RsIndexedFile::OpenDBSegment(bool read, bool write) {
  if (read && !write)
    return OpenRead(path);
  if (!read && write)
    return OpenWrite(path);
  return false;
}

void RsIndexedFile::SetUseRecordCaching(bool enabled) {
  reuse = enabled;
}

bool RsIndexedFile::GetPath(ScStringView *out) {
  out->Copy(path);
  return 1;
}

// Reuse initialized file state immediately; otherwise require the supplied path to exist before resolved Init.
// Acquisition occurs only after successful initialization and preserves its live virtual dispatch.
bool RsIndexedFile::OpenRead(const ScStringView &name) {
  if (opened)
    return AcquireFile(0);
  if (!RsFile::FileExists(name) || !Init(0, &name, true))
    return 0;
  return AcquireFile(0);
}

// Initialize a fresh path in resolved mode, ignoring Init's result as the original does. Always attempt write acquisition.
bool RsIndexedFile::OpenWrite(const ScStringView &name) {
  if (!opened)
    Init(0, &name, true);
  return AcquireFile(1);
}

// Seek to the supplied absolute cursor, then advance its live value by the requested length before reading.
// The original ignores seek failure and does not bound this read by the record's indexed size.
bool RsIndexedFile::ReadRecord(void *out, u32 length, u32 *position) {
  file.SeekAbsolute(*position);
  *position += length;
  return file.Read(out, length);
}

// Move the file to the wrapped cursor-plus-distance position. Advance the live record cursor only after success.
bool RsIndexedFile::SeekRecord(u32 distance, u32 *position) {
  if (file.SeekAbsolute(*position + distance) == kRsInvalidFilePosition)
    return false;
  *position += distance;
  return true;
}

// Acquire an already-open file or open it in the original read/write mode. Write acquisition stamps the archive
// signature before incrementing depth, retaining the original open file on a failed signature write.
bool RsIndexedFile::AcquireFile(bool write) {
  if (!file.IsOpen()) {
    if (write) {
      if (!file.Open(kRsFileAccessReadWrite, kRsFileOpenAlways, kRsFileShareRead))
        return 0;
      if (file.Position())
        file.SeekBegin();
      u32 signature = kRsIndexedFileSignature;
      if (!file.Write(&signature, sizeof(signature)))
        return 0;
    } else if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
      return 0;
  }
  ++depth;
  return 1;
}

// Release one acquisition and close only on the transition to zero. An initially zero depth returns success.
bool RsIndexedFile::ReleaseFile() {
  if (!depth || --depth)
    return 1;
  return file.Close();
}

// Insert every live index key in the original bucket traversal order. Ignore individual insertion results and
// retain the source's success return after enumeration, without acquiring the public record-operation lock.
bool RsIndexedFile::GetResourceKeyList(RsKeyListInterface *out) {
  for (RsIndexRow *row = index.First(); row; row = index.Next(row))
    out->Insert(row->key);
  return true;
}

bool RsIndexedFile::DoTestForRecord(const RsKey &key) {
  return index.Find(key) != nullptr;
}

// Hold the segment's actual recursive lock around the virtual inner lookup. Save its result before unlocking.
bool RsSegment::TestForRecord(const RsKey &key) {
  lock.Lock();
  bool result = DoTestForRecord(key);
  lock.Unlock();
  return result;
}

u32 RsIndexedFile::GetRecordSize(const RsKey &key) {
  RsIndexRow *row = index.Find(key);
  return row ? row->size : 0;
}

bool RsIndexedFile::WriteToRecord(const void *bytes, u32 size, RsIndexedFile *owner) {
  return owner->file.Write(bytes, size);
}

// Find the original indexed extent and acquire the file before publishing a primary record/stream view. Read opens
// leave the cache-busy flag unchanged, so sequential cached reads reuse the same record identity.
bool RsIndexedFile::DoOpenRecord(const RsKey &key, RsRecord **out) {
  RsIndexRow *row = index.Find(key);
  if (!row || !AcquireFile(0))
    return 0;
  if (busy || !reuse) {
    RsFileRecord *record = new RsFileRecord(this, key, row->offset, row->size);
    *out = static_cast<RsStream *>(record);
    record->AddRef();
  } else {
    cache.Reset(key, row->offset, row->size);
    *out = static_cast<RsStream *>(&cache);
  }
  return 1;
}

// Flush a dirty record through the actual index-write dependency and chunk visitor, ignoring their results.
bool RsIndexedFile::DoCloseRecord(RsRecord **slot) {
  RsFileRecord *record = static_cast<RsFileRecord *>(static_cast<RsStream *>(*slot));
  RsRecordData *data = record;
  if (data->IsDirty()) {
    BeginRecordWrite(record);
    data->EnumChunks(WriteToRecord, this);
  }
  if (record == &cache)
    busy = 0;
  else
    (*slot)->Release();
  ReleaseFile();
  return 1;
}

bool RsIndexedFile::DoAbortRecord(RsRecord **slot) {
  if (*slot == static_cast<RsStream *>(&cache))
    busy = 0;
  else
    (*slot)->Release();
  ReleaseFile();
  return 1;
}

// Serialize the original virtual open entry through the segment's recursive lock and preserve its success Boolean.
bool RsSegment::OpenRecord(const RsKey &key, RsRecord **out) {
  lock.Lock();
  bool result = DoOpenRecord(key, out);
  lock.Unlock();
  return result;
}

bool RsSegment::CloseRecord(RsRecord **slot) {
  lock.Lock();
  bool result = DoCloseRecord(slot);
  lock.Unlock();
  return result;
}

// Hold the same recursive lock around the actual abort entry and save its result before unlocking.
bool RsSegment::AbortRecord(RsRecord **slot) {
  lock.Lock();
  bool result = DoAbortRecord(slot);
  lock.Unlock();
  return result;
}

u32 RsIndexedFile::GetDataStart(u32 count) {
  return 20480 * (count >> 10) + 20484;
}

// Prepare the on-disk index before chunk output, extending reserved space and shifting existing payloads when
// the next entry crosses a1024-record boundary. Failure retains the original already-written and indexed prefix.
bool RsIndexedFile::BeginRecordWrite(RsFileRecord *record) {
  u32 reserved = GetDataStart(physical), end = file.Size();
  if (end < reserved) {
    file.SeekEnd();
    u32 length = reserved - end;
    u8 *zeros = new u8[length]{};
    u8 result = file.Write(zeros, length);
    delete[] zeros;
    if (!result)
      return 0;
    end = reserved;
  }
  if (((physical + 1) & 0x3ff) == 0) {
    file.SeekAbsolute(4);
    for (RsIndexRow *row = index.First(); row; row = index.Next(row)) {
      row->offset += 20480;
      u32 entry[] = {row->key.group, row->key.instance, row->key.type, row->offset, row->size};
      if (!file.Write(entry, sizeof(entry)))
        return 0;
    }
    u32 length = end - reserved;
    u8 *bytes = new u8[length];
    file.SeekAbsolute((i32)reserved);
    if (!file.Read(bytes, length)) {
      delete[] bytes;
      return 0;
    }
    file.SeekAbsolute((i32)GetDataStart(physical + 1));
    u8 result = file.Write(bytes, length);
    delete[] bytes;
    if (!result)
      return 0;
    end = file.Size();
  }
  file.SeekAbsolute((i32)(20 * physical + 4));
  RsKey *key = record->GetKey();
  u32 size;
  record->GetSize(&size);
  index.Insert(*key, end, size);
  ++physical;
  u32 entry[] = {key->group, key->instance, key->type, end, size, 0, 0, 0, 0, 0};
  if (!file.Write(entry, sizeof(entry)))
    return 0;
  file.SeekAbsolute((i32)end);
  return 1;
}

// Delete a preexisting key before write acquisition, then publish a buffered heap record or reset the shared cache.
// The source cache reset clears buffered mode even on this path; writing chunks does not change that mode.
bool RsIndexedFile::DoOpenRecordForWrite(const RsKey &key, RsRecord **out) {
  if (index.Find(key) && !DoDeleteRecord(key)) {
    *out = nullptr;
    return 0;
  }
  if (!AcquireFile(1))
    return 0;
  if (busy || !reuse) {
    RsFileRecord *record = new RsFileRecord(this, key);
    *out = static_cast<RsStream *>(record);
    record->AddRef();
  } else {
    cache.Reset(key, 0, 0);
    *out = static_cast<RsStream *>(&cache);
  }
  return 1;
}

bool RsIndexedFile::DoCreateNewRecord(const RsKey &key, RsRecord **out) {
  return DoOpenRecordForWrite(key, out);
}

// Keep the actual recursive segment lock across deletion, write acquisition and publication of the record.
bool RsSegment::OpenRecordForWrite(const RsKey &key, RsRecord **out) {
  lock.Lock();
  bool result = DoOpenRecordForWrite(key, out);
  lock.Unlock();
  return result;
}

// Serialize creation through its separate virtual entry, preserving the original call ordering under the same lock.
bool RsSegment::CreateNewRecord(const RsKey &key, RsRecord **out) {
  lock.Lock();
  bool result = DoCreateNewRecord(key, out);
  lock.Unlock();
  return result;
}

// Release one acquisition and explicitly close an initialized segment before clearing its index and physical count.
// Outstanding acquisition depth is not forcibly reset, and an uninitialized segment is left untouched.
bool RsIndexedFile::Shutdown() {
  if (opened) {
    ReleaseFile();
    if (file.IsOpen())
      file.Close();
    index.Clear();
    physical = 0;
    opened = 0;
  }
  return true;
}

u32 RsIndexedFile::GetID() {
  return identifier;
}

// Remove an open segment's entry by tombstoning its on-disk row. Without an acquisition, copy the file aside
// and compact through the original temporary-file path, retaining its unusual offset and four-word rewrite rules.
bool RsIndexedFile::DoDeleteRecord(const RsKey &key) {
  RsIndexRow *found = index.Find(key);
  if (!found)
    return 0;
  if (depth) {
    index.Erase(key);
    file.SeekAbsolute(4);
    u32 entry[5];
    while (file.Read(entry, sizeof(entry))) {
      if (!entry[0] && !entry[1] && !entry[2])
        return 1;
      if (entry[0] == key.group && entry[1] == key.instance && entry[2] == key.type) {
        memset(entry, 0xff, sizeof(entry));
        file.SeekRelative(-20);
        file.Write(entry, sizeof(entry));
        return 1;
      }
    }
    return 0;
  }
  if (!AcquireFile(1))
    return 0;
  u32 removed = found->offset, end = file.Size();
  ReleaseFile();
  u32 next = end;
  for (RsIndexRow *row = index.First(); row; row = index.Next(row))
    if (row->offset > removed && row->offset <= next)
      next = row->offset;
  ScString backup;
  RsFile output(path);
  u8 result = 0;
  u32 zero = 0, magic = kRsIndexedFileSignature, reserved = GetDataStart(physical), position = reserved;
  RsFile::MakeTempPathName(&backup);
  if (!RsFile::Copy(path, backup, true))
    goto cleanup;
  file.SetName(backup);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    goto cleanup;
  file.SeekAbsolute(0);
  if (!output.Open(kRsFileAccessReadWrite, kRsFileOpenAlways, kRsFileShareRead))
    goto cleanup;
  output.Resize(0);
  output.SeekAbsolute(0);
  if (!output.Write(&magic, sizeof(magic)))
    goto cleanup;
  output.SeekAbsolute((i32)reserved);
  file.SeekAbsolute((i32)reserved);
  while (position < end) {
    u8 byte;
    if (!file.Read(&byte, 1))
      goto cleanup;
    if ((position < removed || position >= next) && !output.Write(&byte, 1))
      goto cleanup;
    ++position;
  }
  file.Close();
  file.Delete();
  index.Erase(key);
  output.SeekAbsolute(4);
  for (RsIndexRow *row = index.First(); row; row = index.Next(row)) {
    u32 offset = row->offset;
    if (offset > removed) {
      offset -= next;
      row->offset = offset;
    }
    u32 type = row->key.type, group = row->key.group, instance = row->key.instance;
    if (!output.Write(&group, 4) || !output.Write(&instance, 4) || !output.Write(&type, 4) || !output.Write(&offset, 4))
      goto cleanup;
  }
  if (!output.Write(&zero, 4) || !output.Write(&zero, 4) || !output.Write(&zero, 4) || !output.Write(&zero, 4))
    goto cleanup;
  output.Close();
  --physical;
  output.Close();
  file.SetName(path);
  result = 1;
  goto done;
cleanup:
  file.Close();
  output.Close();
  RsFile::Delete(backup);
done:
  return result;
}

// Hold the segment's recursive lock while the virtual delete entry changes the live index and backing files.
bool RsSegment::DeleteRecord(const RsKey &key) {
  lock.Lock();
  bool result = DoDeleteRecord(key);
  lock.Unlock();
  return result;
}

// This replaces only the source's internal string-find storage operation, without changing its public method contract.
static u32 RsPathToken(const ScString &text, const char *token) {
  u32 size = text.Strlen(), length = (u32)strlen(token);
  if (length <= size)
    for (u32 i = 0; i <= size - length; ++i)
      if (!memcmp(text.Data() + i, token, length))
        return i;
  return kScStringNotFound;
}

// Preserve the source's early opened flag, duplicate physical count, partial index on failure and short-magic open
// handle.
bool RsIndexedFile::Init(u32 id, const ScStringView *input, bool resolved) {
  if (opened)
    return true;
  opened = 1;
  cache.owner = this;
  while (depth)
    ReleaseFile();
  index.Clear();
  if (resolved) {
    ScString copy(*input);
    path = copy;
  } else {
    ScString work(*input);
    u32 position = RsPathToken(work, "%DataDirectory%");
    if (position < work.Strlen()) {
      work.SplitToken("%DataDirectory%");
      ScString joined(SpGetFileSystem()->DataDirectory());
      joined.Append(work.Data(), work.Strlen());
      path = joined;
    } else {
      position = RsPathToken(work, "%PluginDirectory%");
      if (position < work.Strlen()) {
        work.SplitToken("%PluginDirectory%");
        ScString joined(SpGetFileSystem()->PlugInDirectory());
        joined.Append("PlugIn\\");
        joined.Append(work.Data(), work.Strlen());
        path = joined;
      } else {
        ScString joined(SpGetFileSystem()->DataDirectory());
        joined.Append(input->ToChar());
        path = joined;
      }
    }
    position = RsPathToken(path, "%Language%");
    if (position < path.Strlen()) {
      // Retire the token return before constructing the language output, keeping its separate prefix copy alive.
      ScString prefix(static_cast<const ScString &>(path.SplitToken("%Language%")));
      ScString language;
      SpGetLanguageManager()->GetLanguageDirectoryName(&language, 0);
      prefix.Append(language.Data(), language.Strlen());
      prefix.Append(path.Data(), path.Strlen());
      path = prefix;
    }
  }
  file.SetName(path);
  identifier = id;
  if (!file.IsOpen() && !file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return true;
  file.SeekBegin();
  if (!file.Size()) {
    file.Close();
    return true;
  }
  u32 magic;
  if (file.Read(&magic, sizeof(magic))) {
    if (magic == kRsIndexedFileSignature) {
      physical = 0;
      u32 entry[5];
      while (file.Read(entry, sizeof(entry))) {
        if (!entry[0] && !entry[1] && !entry[2]) {
          file.Close();
          return true;
        }
        if (entry[3] != kRsIndexedFileUnusedEntryValue && entry[4] != kRsIndexedFileUnusedEntryValue) {
          ++physical;
          RsKey key = {entry[2], entry[0], entry[1]};
          index.Insert(key, entry[3], entry[4]);
        }
      }
    }
    file.Close();
  }
  return false;
}
