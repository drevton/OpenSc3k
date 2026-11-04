#ifndef SC3_RESOURCES_FILE_H
#define SC3_RESOURCES_FILE_H

#include <vector>

#include <stdio.h>

#include "common/api.h"
#include "common/sc3_string.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"

// The concrete game file owns both Strings, its native stream/handle, open flag, reference count and last error.
ScString RsApplicationFilePath();

class RsFile : public RsFileInterface {
public:
  RsFile();
  explicit RsFile(const ScStringView &name);
  explicit RsFile(const char *name);
  virtual ~RsFile();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Open(i32 access, i32 creation, i32 share) override;
  bool IsOpen() const override;
  bool Close() override;
  u32 Position() override;
  u32 Size() override;
  bool Resize(u32 size) override;
  u32 SeekBegin() override;
  u32 SeekEnd() override;
  u32 SeekRelative(i32 distance) override;
  u32 SeekAbsolute(u32 offset) override;
  u32 Seek(i32 distance, i32 origin) override;
  bool Read(void *out, u32 size) override;
  bool Read(void *out, u32 *size) override;
  bool Write(const void *bytes, u32 size) override;
  bool Write(const void *bytes, u32 *size) override;
  bool Flush() override;
  bool Delete() override;
  bool Rename(const ScStringView &name) override;
  bool Exists() override;
  bool Copy(const ScStringView &source, bool overwrite) override;
  bool Copy(const RsFileInterface &source, bool overwrite) override;
  void GetName(ScStringView *out) const override;
  const ScStringView *GetName() const override;
  const char *GetNameData() const override;
  void SetName(const ScStringView &name) override;
  void SetName(const char *name) override;
  static bool Copy(const ScStringView &source, const ScStringView &target, bool overwrite);
  static bool Delete(const ScStringView &path);
  static bool FileExists(const ScStringView &path);
  static bool FileCreate(const ScStringView &path, bool truncate);
  static bool GetAttribute(const ScStringView &path, i32 attributes);
  static bool SetAttribute(const ScStringView &path, i32 attributes, bool enabled);
  static bool IsFileWritable(const ScStringView &path);
  static bool MakeTempFileName(const ScStringView &directory, ScStringView *out);
  static bool GetTempDirectory(ScStringView *out);
  static u32 GetVolumeSerialNumber(const ScStringView &path);
  static u32 GetMaximumPathLength(const ScStringView &path);
  static u32 Checksum(const ScStringView &path);
  static u64 GetFreeDiskSpace(const ScStringView &path);
  u32 GetLastError();
  ScString *GetLastErrorMessage();
  static bool Rename(const ScStringView &source, const ScStringView &target);
  static bool MakeTempPathName(ScStringView *out);
  static bool GetPathName(const ScStringView &path, ScStringView *out, bool includeExtension);
  static bool GetPathExtension(const ScStringView &path, ScStringView *out);
  static bool GetPathDirectory(const ScStringView &path, ScStringView *out);
  static bool GetPathDrive(const ScStringView &path, ScStringView *out);
  static bool GetVolumeName(const ScStringView &path, ScStringView *out);
  ScString name;
  bool opened;
  FILE *handle;
  u32 references, error;
  ScString error_text;

protected:
  void SaveLastError();
};

void RsSplitFilePath(const char *path, char *drive, char *directory, char *name, char *extension);
u32 RsResolveFilePathAlias(const char *path, char *out, u32 capacity);

// Actual record implementation base, distinct from RsResource's module-owning resource base.
// Its reference counter is shared by the stream and record views; it does not retain a module director.
class RsRecordObject : public RsRecord {
public:
  RsRecordObject();
  explicit RsRecordObject(const RsKey &key);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *GetKey() override;
  virtual u32 RefCount();
  virtual ~RsRecordObject();
  virtual void SetKey(const RsKey &key);
  u32 references;
  RsKey key;
};

// Native package-input boundary shared by the original INI consumers; incomplete words are not game failure values.
u32 RsReadPackageWord(RsFile *file);

// The primary stream and separate controlling record share one key and reference counter. Their installed operation
// order is preserved, including counted transfers on this implementation rather than the public stream interface.
class RsRecordStream : public RsStream, public RsRecordObject {
public:
  RsRecordStream();
  explicit RsRecordStream(const RsKey &key);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *GetKey() override;
  bool GetSize(u32 *out) override = 0;                              // Original primary16; Boolean size-output contract.
  virtual bool ReadElements(void *out, u32 count, u32 elementSize); // Original primary184.
  virtual bool WriteElements(const void *data, u32 count, u32 elementSize); // Original primary188.
  ~RsRecordStream() override;
};

// The executable filesystem retains its real service/module bases and a distinct filesystem interface.
class RsFileSystemObject : public ScServiceBase, public RsFileSystem {
public:
  RsFileSystemObject();
  ~RsFileSystemObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  const char *AppDrive() override;
  const char *AppDirectory() override;
  const char *AppName() override;
  const char *AppExtension() override;
  const char *DataDirectory() override;
  const char *PlugInDirectory() override;
  const char *CDAppDrive() override;
  const char *CDAppDirectory() override;
  const char *CDDataDirectory() override;
  const char *CDPlugInDirectory() override;
  bool FindCDDrive() override;
  void UseCDIniFile(bool value) override;
  void SetExpectedCDName(const ScStringView &value) override;
  void SetExpectedCDAppPath(const ScStringView &value) override;
  virtual bool SetDataDir(const ScStringView &value);

protected:
  virtual void GetAndSplitApplicationPath();
  virtual bool ValidateCDPath();
  u8 dataReady, pluginReady, applicationReady;
  ScString appDrive, appDirectory, appName, appExtension, dataDirectory, pluginDirectory;
  u8 cdReady;
  ScString cdDrive, cdAppDirectory, cdDataDirectory, cdPluginDirectory, expectedCDName, expectedCDAppPath;
  u8 useCDIni, allowRecursiveCDSearch;
};

// Registered class23599096 owns the LZO1.01 workspace; its installed compression methods deliberately return false.
class RsLzoCodec final : public RsCompression {
public:
  RsLzoCodec();
  ~RsLzoCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Compress(const void *source, u32 size, u8 *destination, u32 *written) override;
  bool Decompress(const u8 *source, u32 size, void *destination, u32 *written) override;
  u32 GetBufferSize(u32 size) override;
  u32 GetCompressedSize(const u8 *source) override;
  u32 GetUncompressedSize(const u8 *source) override;
  void GetVersionString(ScStringView *out) override;

private:
  u32 references;
  u8 *workspace, *allocation;
};

class ScStringView;

// Resource-module QFS codec with its own counter; it does not retain the DLL director.
// The shared compression base retains its actual operation order before the concrete deleting method.
class RsQfsCodec : public RsCompression {
public:
  RsQfsCodec();
  virtual ~RsQfsCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Compress(const void *source, u32 size, u8 *destination, u32 *written) override;
  bool Decompress(const u8 *source, u32 size, void *destination, u32 *written) override;
  u32 GetBufferSize(u32 size) override;
  u32 GetCompressedSize(const u8 *source) override;
  u32 GetUncompressedSize(const u8 *source) override;
  // Original slot32; the shared SDK declaration proves the void return.
  void GetVersionString(ScStringView *out) override;

private:
  u32 references;
};

struct RsIndexRow {
  RsIndexRow *next;
  RsKey key;
  u32 offset, size;
};

// Native storage for the indexed segment's actual unique TGI table. Preserve its hash, prime bucket growth,
// first-equal selection and traversal order without recreating a general STL hash container.
class RsRecordIndex {
public:
  RsRecordIndex();
  ~RsRecordIndex();
  RsRecordIndex(const RsRecordIndex &) = delete;
  RsRecordIndex &operator=(const RsRecordIndex &) = delete;
  void Clear();
  RsIndexRow *Find(const RsKey &key) const;
  void Insert(const RsKey &key, u32 offset, u32 size);
  void Erase(const RsKey &key);
  RsIndexRow *First() const;
  RsIndexRow *Next(RsIndexRow *row) const;
  u32 count, bucket_count;

private:
  void Grow(u32 minimum);
  RsIndexRow **buckets;
};

// Sequential MIFF storage owns the original chunk cursor, header, mode flags and concrete Windows file.
class RsMiffFile {
public:
  explicit RsMiffFile(const ScStringView *name);
  virtual ~RsMiffFile();
  virtual const ScStringView *GetName();
  virtual void SetName(const ScStringView *name);
  virtual u8 OpenRead();
  virtual u8 OpenWrite(u32 first, u32 second, u32 third);
  virtual u8 OpenAppend();
  virtual u8 IsOpen();
  virtual u8 Close();
  virtual RsKey *GetKey(RsKey *out);
  virtual void SetKey(const RsKey &key);
  virtual u32 GetChunkSize();
  virtual u32 GetPayloadSize();
  virtual u32 GetIndex();
  virtual u8 MatchesDeclaredSize();
  virtual u8 ReadHeader();
  virtual u8 ReadRecordHeader();
  virtual u8 GetFirstFlag();
  virtual u8 GetSecondFlag();
  virtual u8 GetThirdFlag();
  virtual u8 ReadKeyChunk(void *out, u32 length);
  virtual u8 ReadPayload(void *out, u32 length);
  virtual u8 First();
  virtual u8 Next();
  virtual u32 Previous();
  virtual u8 Find(const RsKey &key);
  virtual u8 FindNext(const RsKey &key);
  virtual u8 SeekKey(const RsKey &key);
  virtual u8 SeekIndex(u32 index);
  virtual u32 CountFollowingChunks();
  virtual u8 WriteChunkBlock(void *data, u32 payloadLength);
  virtual u8 WriteChunk(const RsKey &key, const void *data, u32 length);
  virtual u8 WriteHeader(u32 first, u32 second, u32 third);
  virtual u8 EndWrite();
  virtual u8 DeleteCurrent();
  virtual u8 Delete(const RsKey &key);
  virtual u8 Compact();
  virtual u32 GetMode();
  virtual u8 BeginRead();
  virtual u8 BeginWrite(const RsKey &key);
  virtual u8 Read(void *out, u32 length);
  virtual u8 Skip(i32 distance);
  virtual u8 Write(const void *data, u32 length);
  u32 offset;
  RsKey key;
  u32 chunkSize, index, mode;
  u8 finalized, failed;
  u32 header[7];
  RsFile file;
};

class RsIndexedFile;
using RsChunkVisitor = bool (*)(const void *bytes, u32 size, RsIndexedFile *owner);

// Actual IID20E485D4 data/chunk view, distinct from primary IID00199627. Its size/read slots are12/16, not16/92.
class RsRecordData : public ScUnknown {
public:
  virtual bool GetSize(u32 *out) = 0;
  virtual bool ReadBlock(void *out, u32 size) = 0;
  virtual bool EnumChunks(RsChunkVisitor visitor, RsIndexedFile *owner) = 0;
  virtual bool IsDirty() = 0;
};

struct RsDataChunk {
  u8 *bytes;
  u32 size;
};

// Real indexed-file record with primary stream, controlling record and data/chunk views. The parent is borrowed;
// ordinary open/close operations acquire/release the parent's file separately from this object's shared reference count.
class RsFileRecord : public RsRecordStream, public RsRecordData {
public:
  RsFileRecord(RsIndexedFile *owner, const RsKey &key);
  RsFileRecord(RsIndexedFile *owner, const RsKey &key, u32 offset, u32 size);
  ~RsFileRecord() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 size) override;
  bool WriteBlock(const void *source, u32 size) override;
  bool EnumChunks(RsChunkVisitor visitor, RsIndexedFile *owner) override;
  bool IsDirty() override;
  bool SkipBytes(u32 distance) override;
  // Original Reset192 has a void return; DataSize follows it at196.
  virtual void Reset(const RsKey &key, u32 offset, u32 size);
  virtual u32 DataSize();
  RsIndexedFile *owner;
  std::vector<RsDataChunk> chunks;
  u8 buffered;
  u32 position, indexed_size;
};

// Actual file-record identityA2438BDB; the stream and controlling record are distinct secondary bases.
// Native virtual declarations describe operations, not an overlay of the original x86 tables.
class RsLooseAccess : public ScUnknown {
public:
  virtual bool GetSize(u32 *out) = 0;
  virtual bool ReadBlock(void *out, u32 size) = 0;
  virtual bool WriteBlock(const void *data, u32 size) = 0;
  virtual u32 ReadCount(void *out, u32 size) = 0;
  virtual u32 WriteCount(const void *data, u32 size) = 0;
  virtual u8 GetPosition(u32 *out) = 0;
  virtual u8 Seek(u32 distance, u32 origin) = 0;
  virtual bool Flush() = 0;
  virtual u8 GetName(ScStringView **out) = 0;
  virtual u8 GetFileName(ScStringView **out) = 0;
  virtual u8 GetExtension(ScStringView **out) = 0;
  virtual bool Open(const RsKey *key, const ScStringView *name, u32 mode) = 0;
  virtual u8 Close() = 0;
  virtual u8 Abort() = 0;
};

// Real loose-file record, including transactional backup and the shared record counter rather than a module reference.
class RsLooseRecord : public RsLooseAccess, public RsRecordStream {
public:
  RsLooseRecord();
  ~RsLooseRecord() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 size) override;
  bool WriteBlock(const void *data, u32 size) override;
  u32 ReadCount(void *out, u32 size) override;
  u32 WriteCount(const void *data, u32 size) override;
  u8 GetPosition(u32 *out) override;
  u8 Seek(u32 distance, u32 origin) override;
  bool Flush() override;
  u8 GetName(ScStringView **out) override;
  u8 GetFileName(ScStringView **out) override;
  u8 GetExtension(ScStringView **out) override;
  bool Open(const RsKey *key, const ScStringView *name, u32 mode) override;
  u8 Close() override;
  u8 Abort() override;
  bool SkipBytes(u32 distance) override;

private:
  RsFile file, backup;
  u32 mode;
  bool opened;
};

class RsPackedSegment;

// Original IID035B2D97 shares the record counter and exposes sizes, transport and destination-buffer operations.
// The last slot reports whether the concrete record owns its backing allocation.
class RsPackedData : public ScUnknown {
public:
  virtual RsKey *GetKey() = 0;
  virtual bool GetSize(u32 *out) = 0;
  virtual bool ReadBlock(void *out, u32 size) = 0;
  virtual bool WriteBlock(const void *source, u32 size) = 0;
  virtual u8 IsWritable() = 0;
  virtual u8 SetBuffer(u8 *data, u32 capacity) = 0;
  virtual bool StoreData(u8 *data, u32 capacity) = 0;
  virtual u8 DoesRecordAllocateMemory() const = 0;
};

// A direct record borrows the backing segment's buffer and tracks its physical cursor, remaining bytes and write mode.
class RsPackedRecord : public RsRecordStream, public RsPackedData {
public:
  RsPackedRecord(RsPackedSegment *owner, const RsKey &key, u8 *data, u32 capacity, u8 writable);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *GetKey() override;
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 size) override;
  bool WriteBlock(const void *source, u32 size) override;
  u8 IsWritable() override;
  u8 SetBuffer(u8 *data, u32 capacity) override;
  bool StoreData(u8 *data, u32 capacity) override;
  u8 DoesRecordAllocateMemory() const override;
  bool SkipBytes(u32 distance) override;
  RsPackedSegment *owner;
  u8 *data, *cursor;
  u8 writable;
  u32 remaining, capacity;
};

struct RsPackedChunk {
  u32 size;
  u8 *data;
};

// Additional simultaneous writers own one allocated chunk per write, keeping the original chunk vector and total count.
class RsPackedBufferedRecord : public RsRecordStream, public RsPackedData {
public:
  explicit RsPackedBufferedRecord(const RsKey &key);
  ~RsPackedBufferedRecord() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *GetKey() override;
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 size) override;
  bool WriteBlock(const void *source, u32 size) override;
  u8 IsWritable() override;
  u8 SetBuffer(u8 *data, u32 capacity) override;
  bool StoreData(u8 *data, u32 capacity) override;
  u8 DoesRecordAllocateMemory() const override;
  bool SkipBytes(u32 distance) override;
  u32 size;
  std::vector<RsPackedChunk> chunks;
};

#endif
