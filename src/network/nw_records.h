#ifndef SC3_NETWORK_NW_RECORDS_H
#define SC3_NETWORK_NW_RECORDS_H

#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"

// Network descriptors retain the tile identity, extension and individually present damage/teleport metadata.
enum NwPiecePersistenceKey : u32 {
  kNwPieceTileIdKey = 0x6355941d,
  kNwPieceExtensionKey = 0x63559422,
  kNwPieceDamageKey = 0x63559421,
  kNwPieceTeleportPresentKey = 0x6355941e,
  kNwPieceTeleportLengthKey = 0x63559420,
  kNwPieceTeleportDirectionKey = 0x6355941f,
};

// Tile catalog entries retain their installed resource type and group.
enum NwTileResourceKey : u32 {
  kNwTileResourceType = 0xe223741f,
  kNwTileResourceGroup = 0xa317745f,
};

// Recovered record owner. The segment is borrowed, and close/abort receive its actual live record slot.
// The write constructor retains its unused final word; the literal private formal type remains unresolved.
class NwDbRecord {
public:
  NwDbRecord(RsDatabaseSegment *segment, RsKey key);
  NwDbRecord(RsDatabaseSegment *segment, RsKey key, u32 value);
  virtual ~NwDbRecord();
  virtual bool IsValid();

  u8 aborted;

protected:
  RsRecord *record;
  RsDatabaseSegment *segment;
};

// The derived owner releases its queried stream before the base owner closes or aborts the record.
// Read and write constructors retain the same interface and inherited close/abort protocol.
class NwStreamRecord : public NwDbRecord {
public:
  NwStreamRecord(RsDatabaseSegment *segment, RsKey key);
  NwStreamRecord(RsDatabaseSegment *segment, RsKey key, u32 value);
  ~NwStreamRecord() override;
  bool IsValid() override;
  RsStream *Stream();

protected:
  // QueryInterface writes this genuine external void** ABI slot before typed stream access.
  void *stream;
};

class CtCity;
class ScStringView;
class ScVariant;

// Persisted network descriptor: two packed words, reference count and caller-owned flag.
// References retain the actual network module director; the caller-owned byte controls final descriptor deletion.
class NwRecord : public OcPersistInfo {
public:
  explicit NwRecord(u8 callerOwned);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetAttribKey(RsKey *out) const override;
  u8 GetTypeId(u32 *out) const override;
  u8 GetLocationInCells(u32 *x, u32 *z, u32 *altitude) const override;
  u8 GetSizeInCells(u32 *first, u32 *second) const override;
  u8 GetRotationFlag(u32 *out) const override;
  u8 GetCanSimRemoveFlag() const override;
  u8 GetCanUserRemoveFlag() const override;
  void SetAttribKey(const RsKey &key) override;
  void SetTypeId(u32 type) override;
  void SetLocationInCells(u32 x, u32 z, u32 altitude) override;
  void SetSizeInCells(u32 first, u32 second) override;
  void SetRotationFlag(u32 rotation) override;
  void SetRemoveFlags(u8 first, u8 second) override;
  u32 GetPersistRecordVersion() const override;
  u8 DidSameKeyWriteResource() override;
  u8 SetSameKeyWroteResource(u8 enabled) override;
  u8 HasNameField() const override;
  u8 GetNameField(ScStringView *out) const override;
  u8 SetNameField(ScStringView *value) override;
  u8 HasSimCreateDate() const override;
  u8 GetSimCreateDate(i32 *year, i32 *day) const override;
  u8 SetSimCreateDate(i32 year, i32 day) override;
  u8 HasLayerData1() const override;
  u8 GetLayerData1(u32 *out) const override;
  u8 SetLayerData1(u32 value) override;
  u8 HasLayerData2() const override;
  u8 GetLayerData2(u32 *out) const override;
  u8 SetLayerData2(u32 value) override;
  u32 GetIntDataCount() const override;
  u8 GetIntData(u32 key, u32 *out) override;
  u8 SetIntData(u32 key, u32 value) override;
  u32 GetStringDataCount() const override;
  u8 GetStringData(u32 key, ScStringView *out) override;
  u8 SetStringData(u32 key, ScStringView *value) override;
  u32 GetFloatDataCount() const override;
  u8 GetFloatData(u32 key, f32 *out) override;
  u8 SetFloatData(u32 key, f32 value) override;
  u32 GetTokenCount() const override;
  u8 GetToken(u32 key, ScVariant **out) override;
  u8 AddToken(u32 key, ScVariant *value) override;
  u8 Clear() override;
  u8 SerialRead(CtCity *city, u32 codec, RsStream *stream) override;
  u8 SerialWrite(CtCity *city, RsStream *stream) override;
  virtual u8 GetTransitTileId(u32 value) const;
  virtual u32 GetNeedsRepair() const;
  virtual u32 HasTeleportData() const;
  virtual u8 TeleportDirection(i32 value) const;
  virtual u8 TeleportDistance(i32 value) const;
  virtual ~NwRecord();
  u32 position, piece, references;
  u8 callerOwned;
};

class CtCity;

// Empty virtual base identified by the original frame's ordinary and unwind destructor transitions.
class NwFrameBase {
public:
  virtual ~NwFrameBase();
};

// Real network frame state and owned stream record. Private constructor nominal declarations remain unresolved.
// Native scratch initialization does not establish original behavior for unwritten source marker/header bytes.
class NwRecordFrame : public NwFrameBase {
public:
  NwRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  NwRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 debug);
  ~NwRecordFrame() override;
  RsStream *Stream();
  u16 version;
  u8 debug, extended;
  u32 magic;
  NwStreamRecord *record;
  u8 valid;
  u32 cityVersion[3];
};

#endif
