#ifndef SC3_RESOURCES_LAYERED_FILE_H
#define SC3_RESOURCES_LAYERED_FILE_H

#include <list>
#include <vector>

#include "resources/api.h"
#include "resources/indexed_file.h"

// A stored layer retains its actual segment and the key of the record containing its serialized payload.
struct RsStoredLayer {
  RsStoredLayer();
  RsStoredLayer(RsSegment *segment, const RsKey &key);
  RsStoredLayer(const RsStoredLayer &source);
  ~RsStoredLayer();
  RsStoredLayer &operator=(const RsStoredLayer &source);
  RsSegment *segment;
  RsKey key;
};

// The original open-record list owns both the record and its queried controlling database-segment view.
struct RsLayerRecord {
  RsLayerRecord(RsRecord *record, RsSegment *owner);
  RsLayerRecord(const RsLayerRecord &source);
  ~RsLayerRecord();
  RsRecord *record;
  RsDatabaseSegment *owner;
};

// Registered class835F62CF wraps a primary indexed file and its stored layers, retaining one shared open-record list.
class RsLayeredFile : public RsSegment, public RsFileSegment {
public:
  RsLayeredFile();
  ~RsLayeredFile() override;
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
  u8 GetPath(ScStringView *out) override;
  u8 OpenRead(const ScStringView *name) override;
  u8 OpenWrite(const ScStringView *name) override;
  u32 GetVersion() override;
  u8 LoadRecords() override;
  u8 StoreRecords(RsKey *out, u32 firstLimit, u32 secondLimit) override;
  u8 SelectLayer(const RsKey &key) override;
  u8 ClearSelection() override;
  u8 ReadVersion();
  u8 ReadDirectory();
  u8 WriteDirectory();
  u8 LoadLayers();
  u8 FlushLayers();
  u8 ReadLayer(RsStoredLayer *layer);
  u8 WriteLayer(RsStoredLayer *layer);
  void SetActiveLayer(RsSegment *segment);
  std::vector<RsStoredLayer> layers;
  ScString path;
  RsIndexedFile *primary;
  RsStoredLayer active;
  u8 writing, sourceFlag;
  u32 sequence, version;
  std::list<RsLayerRecord> records;
};

#endif
