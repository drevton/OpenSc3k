#ifndef SC3_RESOURCES_INDEXED_FILE_H
#define SC3_RESOURCES_INDEXED_FILE_H

#include "common/thread.h"
#include "resources/api.h"
#include "resources/file.h"

// An unused indexed-file entry stores all-ones offset and length words; readers skip those entries.
enum RsIndexedFileEntryMarker : u32 {
  kRsIndexedFileUnusedEntryValue = 0xffffffff,
};

// The indexed-file header begins with this signature; it is independent of the factory class identity.
enum RsIndexedFileTag : u32 {
  kRsIndexedFileSignature = 0x80c381d7,
};

class RsKeyListInterface;

// The recovered base owns its mutex, reference count and locked record entry points; derived segments supply inner work.
// Its native declaration models actual ownership, not the original x86 subobject offsets.
class RsSegment : public RsDatabaseSegment, public ScObject {
public:
  virtual ~RsSegment();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool OpenRecord(const RsKey &key, RsRecord **record) override;
  bool CloseRecord(RsRecord **record) override;
  bool AbortRecord(RsRecord **record) override;
  bool TestForRecord(const RsKey &key) override;
  bool OpenRecordForWrite(const RsKey &key, RsRecord **record) override;
  bool CreateNewRecord(const RsKey &key, RsRecord **record) override;
  bool DeleteRecord(const RsKey &key) override;
  virtual bool DoOpenRecord(const RsKey &key, RsRecord **record) = 0;
  virtual bool DoCloseRecord(RsRecord **record) = 0;
  virtual bool DoAbortRecord(RsRecord **record) = 0;
  virtual bool DoTestForRecord(const RsKey &key) = 0;
  virtual bool DoOpenRecordForWrite(const RsKey &key, RsRecord **record) = 0;
  virtual bool DoCreateNewRecord(const RsKey &key, RsRecord **record) = 0;
  virtual bool DoDeleteRecord(const RsKey &key) = 0;
  ScLock lock;
  u32 lock_ready, references;

protected:
  RsSegment();
};

// Original cIGZDBSegmentIndexedFile secondary IID C2420840, with Boolean path/open results and const String inputs.
// These are reached operations, not invented file-service callbacks or a foreign vtable overlay.
class RsIndexedAccess : public ScUnknown {
public:
  virtual bool Init(u32 identifier, const ScStringView *name, bool resolved) = 0;
  virtual bool GetPath(ScStringView *out) = 0;
  virtual bool Shutdown() = 0;
  virtual bool OpenRead(const ScStringView &name) = 0;
  virtual bool OpenWrite(const ScStringView &name) = 0;
};

// Recovered cGZDBSegmentIndexedFile with its controlling segment and actual secondary file-access identity.
// Native member types and inheritance provide the required adjustments without x86 offset adapters.
class RsIndexedFile : public RsSegment, public RsIndexedAccess {
public:
  explicit RsIndexedFile(char *name);
  ~RsIndexedFile() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 identifier, const ScStringView *name, bool resolved) override;
  bool OpenDBSegment(bool read, bool write) override;
  bool GetPath(ScStringView *out) override;
  bool OpenRead(const ScStringView &name) override;
  bool OpenWrite(const ScStringView &name) override;
  bool Shutdown() override;
  u32 GetID() override;
  bool DoOpenRecord(const RsKey &key, RsRecord **record) override;
  bool DoCloseRecord(RsRecord **record) override;
  bool DoAbortRecord(RsRecord **record) override;
  bool DoOpenRecordForWrite(const RsKey &key, RsRecord **record) override;
  bool DoCreateNewRecord(const RsKey &key, RsRecord **record) override;
  bool DoDeleteRecord(const RsKey &key) override;
  virtual void SetUseRecordCaching(bool enabled);
  virtual bool ReadRecord(void *out, u32 length, u32 *position);
  virtual bool SeekRecord(u32 distance, u32 *position);
  static bool WriteToRecord(const void *bytes, u32 size, RsIndexedFile *owner);
  virtual u32 GetRecordSize(const RsKey &key);
  bool GetResourceKeyList(RsKeyListInterface *out) override;
  bool DoTestForRecord(const RsKey &key) override;
  u8 opened;
  ScString path;
  u32 identifier;
  RsFile file;
  u32 depth, physical;
  RsRecordIndex index;
  RsFileRecord cache;
  u8 busy, reuse;

protected:
  virtual bool AcquireFile(bool write);
  virtual bool ReleaseFile();
  virtual bool BeginRecordWrite(RsFileRecord *record);
  u32 GetDataStart(u32 count);
};

#endif
