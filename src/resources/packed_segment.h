#ifndef SC3_RESOURCES_PACKED_SEGMENT_H
#define SC3_RESOURCES_PACKED_SEGMENT_H

#include <list>
#include <map>

#include "resources/file.h"
#include "resources/indexed_file.h"

// Original packed offsets use the unsigned group/instance/type tree order shared by resource keys.
struct RsPackedKeyLess {
  bool operator()(const RsKey &left, const RsKey &right) const;
};

// Actual140-byte source segment owns a QFS buffer, offset index, active direct writer and two retained record lists.
// Native STL layouts differ from the source; no original-memory overlay is used.
class RsPackedSegment : public RsSegment {
public:
  explicit RsPackedSegment(u32 version);
  ~RsPackedSegment() override;
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
  u8 Read(const RsKey &key, RsStream *stream);
  u8 Create(const RsKey &key, u32 initialSize, u32 growth);
  u8 ReadBuffer(RsStream *stream);
  u8 ReadIndex();
  u8 Write(RsStream *stream);
  bool WriteBuffer(RsStream *stream);
  u8 WriteIndex();
  u8 ResizeBuffer(u32 capacity);
  u8 GrowBuffer(u32 additional);
  u8 FinishRecords();
  u8 FinishActive();
  u8 QueueRecord(RsPackedData *record);
  u8 FinishPending();
  u8 ClearData();
  RsKey key;
  u32 version, packedSize;
  ScString codecName;
  u8 ready, writable;
  u32 initialSize, growth, capacity, used;
  u8 *data;
  std::list<RsPackedData *> records;
  RsPackedData *activeRecord;
  std::list<RsPackedData *> pending;
  std::map<RsKey, u32, RsPackedKeyLess> index;
};

#endif
