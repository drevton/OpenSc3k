#ifndef SC3_RESOURCES_MIFF_SEGMENT_H
#define SC3_RESOURCES_MIFF_SEGMENT_H

#include "resources/file.h"
#include "resources/indexed_file.h"
#include "resources/resource.h"

// Secondary identity40674664 exposes the original no-argument compaction entry, rather than a file-path getter.
class RsMiffAccess : public ScUnknown {
public:
  virtual u8 Compact() = 0;
};

class RsMiffSegment;

// The MIFF record is a real stream/record object with a borrowed owner; all data uses the owner's single cursor.
class RsMiffRecord : public RsRecordStream {
public:
  RsMiffRecord(RsMiffSegment *owner, const RsKey &key);
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 length) override;
  bool WriteBlock(const void *data, u32 length) override;
  bool SkipBytes(u32 distance) override;
  RsMiffSegment *owner;
};

// Registered class00199925 owns its MIFF file, path, identifier and persistent key-list view.
class RsMiffSegment : public RsSegment, public RsMiffAccess {
public:
  explicit RsMiffSegment(const char *name);
  ~RsMiffSegment() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 identifier, const ScStringView *name, bool resolved) override;
  bool Shutdown() override;
  u32 GetID() override;
  bool GetResourceKeyList(RsKeyListInterface *out) override;
  bool OpenDBSegment(bool read, bool write) override;
  bool DoOpenRecord(const RsKey &key, RsRecord **record) override;
  bool DoCloseRecord(RsRecord **record) override;
  bool DoAbortRecord(RsRecord **record) override;
  bool DoTestForRecord(const RsKey &key) override;
  bool DoOpenRecordForWrite(const RsKey &key, RsRecord **record) override;
  bool DoCreateNewRecord(const RsKey &key, RsRecord **record) override;
  bool DoDeleteRecord(const RsKey &key) override;
  virtual u8 ReadRecord(void *out, u32 length);
  virtual u8 SkipRecord(i32 distance);
  virtual u8 WriteRecord(const void *data, u32 length);
  u8 Compact() override;
  ScString path;
  u32 identifier;
  RsMiffFile *file;
  RsKeyList keys;
  u8 writing;
};

#endif
