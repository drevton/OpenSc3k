#include "network/nw_records.h"

#include "city/api.h"
#include "network/nw_module.h"
#include "resources/stream.h"

// Persisted descriptor setters replace only removal permissions, teleport metadata or damage while retaining the other fields.
enum NwRecordFieldMask : u32 {
  kNwRecordWithoutRemovalFlags = 0xcfffffff,
  kNwRecordWithoutTeleportPresent = 0xfffeffff,
  kNwRecordWithoutTeleportDirection = 0xfff9ffff,
  kNwRecordWithoutTeleportLength = 0xf807ffff,
  kNwRecordWithoutDamage = 0xf7ffffff,
};

// Serialized piece descriptors keep independent simulation-removal and user-removal permission bits.
enum NwRecordRemovalFlag : u32 {
  kNwRecordCanSimRemove = 0x10000000,
  kNwRecordCanUserRemove = 0x20000000,
};

// Serialized piece updates replace the low tile word while retaining the upper piece-data flags.
enum NwRecordPieceMask : u32 {
  kNwRecordPieceWithoutTileId = 0xffff0000,
};

static constexpr u32 kNwRecordFrameMarker = 0xdeadbeef;

NwDbRecord::NwDbRecord(RsDatabaseSegment *owner, RsKey key) : aborted(0), record(nullptr), segment(owner) {
  if (owner && !owner->OpenRecord(key, &record))
    record = nullptr;
}

// Close an existing record unless already marked for abort. A failed close marks the owner for abort.
// Recheck the live record and flag after that callback before aborting through the live borrowed segment.
NwDbRecord::~NwDbRecord() {
  if (record) {
    if (!aborted)
      aborted = !segment->CloseRecord(&record);
    if (record && aborted)
      segment->AbortRecord(&record);
  }
}

NwStreamRecord::NwStreamRecord(RsDatabaseSegment *owner, RsKey key) : NwDbRecord(owner, key), stream(nullptr) {
  if (NwDbRecord::IsValid() && !record->QueryInterface(IID_RsStream, &stream))
    stream = nullptr;
}

// Release and clear the live stream before automatic base destruction performs the record's close/abort protocol.
NwStreamRecord::~NwStreamRecord() {
  if (stream) {
    static_cast<RsStream *>(stream)->Release();
    stream = nullptr;
  }
}

bool NwDbRecord::IsValid() {
  return record != nullptr;
}

bool NwStreamRecord::IsValid() {
  return NwDbRecord::IsValid() && stream != nullptr;
}

// Borrow the queried stream without acquiring a reference or changing its published member.
RsStream *NwStreamRecord::Stream() {
  return static_cast<RsStream *>(stream);
}

NwDbRecord::NwDbRecord(RsDatabaseSegment *owner, RsKey key, u32) : aborted(0), record(nullptr), segment(owner) {
  if (owner && !owner->OpenRecordForWrite(key, &record))
    record = nullptr;
}

// Acquire the same queried stream after writable record construction, passing the original final word unchanged.
NwStreamRecord::NwStreamRecord(RsDatabaseSegment *owner, RsKey key, u32 value)
    : NwDbRecord(owner, key, value), stream(nullptr) {
  if (NwDbRecord::IsValid() && !record->QueryInterface(IID_RsStream, &stream))
    stream = nullptr;
}

NwRecord::NwRecord(u8 owned) : position(0), piece(0), references(0), callerOwned(owned) {
}

NwRecord::~NwRecord() {
}

// Publish the descriptor only for its actual two interface identifiers, then retain through the live virtual method.
bool NwRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwRecord);
  HANDLE_QUERY_INTERFACE(IID_OcPersistInfo, NwRecord);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the descriptor before retaining the module; return the live count after the director call.
u32 NwRecord::AddRef() {
  ++references;
  NwGetDirector()->AddRef();
  return references;
}

// Drop a nonfinal reference before releasing the module, preserving the original live-field reads afterward.
// At zero or one references, release the module and delete only descriptors whose caller-owned byte is zero.
u32 NwRecord::Release() {
  if (references > 1) {
    --references;
    NwGetDirector()->RemoveRef();
    return references;
  }
  NwGetDirector()->RemoveRef();
  if (!callerOwned)
    delete this;
  return 0;
}

// Clear the two persisted words without changing references or caller ownership.
u8 NwRecord::Clear() {
  position = 0;
  piece = 0;
  return 1;
}

// Publish the network resource type and group before the descriptor's low-word piece identifier.
u8 NwRecord::GetAttribKey(RsKey *key) const {
  key->group = kNwTileResourceGroup;
  key->type = kNwTileResourceType;
  key->instance = (u16)piece;
  return 1;
}

u8 NwRecord::GetTypeId(u32 *) const {
  return 0;
}

void NwRecord::SetTypeId(u32) {
}

void NwRecord::SetSizeInCells(u32, u32) {
}

// Unpack X, Z and altitude into the genuine outputs in their original store order.
// Reload the live packed word before each store so aliased output pointers retain their effects.
u8 NwRecord::GetLocationInCells(u32 *x, u32 *z, u32 *altitude) const {
  *x = position & 0x7ff;
  *z = (position >> 11) & 0x7ff;
  *altitude = (u8)(position >> 22);
  return 1;
}

u8 NwRecord::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> 30;
  return 1;
}

u8 NwRecord::GetCanSimRemoveFlag() const {
  return (piece & kNwRecordCanSimRemove) != 0;
}

u8 NwRecord::GetCanUserRemoveFlag() const {
  return (piece & kNwRecordCanUserRemove) != 0;
}

void NwRecord::SetAttribKey(const RsKey &key) {
  piece = (piece & kNwRecordPieceWithoutTileId) | u16(key.instance);
}

void NwRecord::SetLocationInCells(u32 x, u32 z, u32 altitude) {
  position = (position & kOcPositionRotationMask) | (x & 0x7ff) | ((z & 0x7ff) << 11) | (u32(u8(altitude)) << 22);
}

void NwRecord::SetRotationFlag(u32 rotation) {
  position = (position & kOcPositionWithoutRotation) | (rotation << 30);
}

void NwRecord::SetRemoveFlags(u8 first, u8 second) {
  piece = (piece & kNwRecordWithoutRemovalFlags) | (u32(first & 1) << 28) | (u32(second & 1) << 29);
}

u8 NwRecord::GetLayerData1(u32 *value) const {
  *value = position;
  return 1;
}

u8 NwRecord::SetLayerData1(u32 value) {
  position = value;
  return 1;
}

u8 NwRecord::HasNameField() const {
  return 0;
}

u8 NwRecord::GetNameField(ScStringView *) const {
  return 0;
}

u8 NwRecord::SetNameField(ScStringView *) {
  return 0;
}

u8 NwRecord::HasSimCreateDate() const {
  return 0;
}

u8 NwRecord::GetSimCreateDate(i32 *, i32 *) const {
  return 0;
}

u8 NwRecord::SetSimCreateDate(i32, i32) {
  return 0;
}

u8 NwRecord::HasLayerData1() const {
  return 1;
}

u8 NwRecord::GetLayerData2(u32 *value) const {
  *value = piece;
  return 1;
}

u8 NwRecord::SetLayerData2(u32 value) {
  piece = value;
  return 1;
}

u8 NwRecord::GetIntData(u32 property, u32 *value) {
  switch (property) {
  case kNwPieceTileIdKey:
    *value = (u16)piece;
    return 1;
  case kNwPieceTeleportPresentKey:
    *value = (piece >> 16) & 1;
    return 1;
  case kNwPieceTeleportDirectionKey:
    *value = (piece >> 17) & 3;
    return 1;
  case kNwPieceTeleportLengthKey:
    *value = (u8)(piece >> 19);
    return 1;
  case kNwPieceDamageKey:
    *value = (piece >> 27) & 1;
    return 1;
  default:
    return property - kNwPieceExtensionKey < 2;
  }
}

// Replace the selected packed field and preserve every other bit.
// Accepted extension keys return success without storing into the descriptor.
u8 NwRecord::SetIntData(u32 property, u32 value) {
  switch (property) {
  case kNwPieceTileIdKey:
    piece = (piece & kNwRecordPieceWithoutTileId) | (u16)value;
    return 1;
  case kNwPieceTeleportPresentKey:
    piece = (piece & kNwRecordWithoutTeleportPresent) | ((value & 1) << 16);
    return 1;
  case kNwPieceTeleportDirectionKey:
    piece = (piece & kNwRecordWithoutTeleportDirection) | ((value & 3) << 17);
    return 1;
  case kNwPieceTeleportLengthKey:
    piece = (piece & kNwRecordWithoutTeleportLength) | ((u32)(u8)value << 19);
    return 1;
  case kNwPieceDamageKey:
    piece = (piece & kNwRecordWithoutDamage) | ((value & 1) << 27);
    return 1;
  default:
    return property - kNwPieceExtensionKey < 2;
  }
}

// Read position then piece for codec one, preserving the raw reader byte and short-circuit order.
// Other codecs fail before accessing the stream or changing either packed word.
u8 NwRecord::SerialRead(CtCity *, u32 codec, RsStream *stream) {
  if (codec != 1)
    return 0;
  u8 result = stream->ReadU32(&position);
  if (result)
    result = stream->ReadU32(&piece);
  return result;
}

// Write position before the live piece word, stopping when the first writer returns zero.
// Preserve the writer's raw byte result and any callback changes to the second persisted word.
u8 NwRecord::SerialWrite(CtCity *, RsStream *stream) {
  u8 result = stream->WriteU32(position);
  if (result)
    result = stream->WriteU32(piece);
  return result;
}

u8 NwRecord::GetSizeInCells(u32 *, u32 *) const {
  return 0;
}

u32 NwRecord::GetPersistRecordVersion() const {
  return 1;
}

u8 NwRecord::DidSameKeyWriteResource() {
  return 1;
}

u8 NwRecord::SetSameKeyWroteResource(u8) {
  return 0;
}

u8 NwRecord::HasLayerData2() const {
  return 1;
}

u32 NwRecord::GetIntDataCount() const {
  return 4;
}

u32 NwRecord::GetStringDataCount() const {
  return 0;
}

u8 NwRecord::GetStringData(u32, ScStringView *) {
  return 0;
}

u8 NwRecord::SetStringData(u32, ScStringView *) {
  return 0;
}

u32 NwRecord::GetFloatDataCount() const {
  return 0;
}

u8 NwRecord::GetFloatData(u32, f32 *) {
  return 0;
}

u8 NwRecord::SetFloatData(u32, f32) {
  return 0;
}

u32 NwRecord::GetTokenCount() const {
  return 0;
}

u8 NwRecord::GetToken(u32, ScVariant **) {
  return 0;
}

u8 NwRecord::AddToken(u32, ScVariant *) {
  return 0;
}

u8 NwRecord::GetTransitTileId(u32) const {
  return 1;
}

u32 NwRecord::GetNeedsRepair() const {
  return (piece >> 27) & 1;
}

u32 NwRecord::HasTeleportData() const {
  return (piece >> 16) & 1;
}

u8 NwRecord::TeleportDirection(i32) const {
  return u8(HasTeleportData()) != 0;
}

u8 NwRecord::TeleportDistance(i32) const {
  return u8(HasTeleportData()) != 0;
}

NwFrameBase::~NwFrameBase() {
}

// Read the optional versioned frame header, reopening the same record at its beginning for the original raw format.
NwRecordFrame::NwRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : version(0), debug(0), extended(0), magic(0), record(nullptr), valid(0), cityVersion{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    cityVersion[component] = city->GetVersionComponentFromSaveFile(component);
  record = new NwStreamRecord(segment, key);
  valid = record->IsValid();
  if (!valid) {
    delete record;
    record = nullptr;
    return;
  }
  valid = record->Stream()->ReadU16(&version);
  if (valid) {
    u8 header = 0;
    valid = record->Stream()->ReadU8(&header);
    debug = header & 1;
    extended = (header & 2) != 0;
    if (extended && valid)
      valid = record->Stream()->ReadU8(&header);
  }
  if (valid)
    record->Stream()->ReadU32(&magic);
  if (valid)
    valid = magic == kNwRecordFrameMarker;
  if (magic != kNwRecordFrameMarker) {
    debug = 0;
    version = 0;
    magic = kNwRecordFrameMarker;
    delete record;
    record = nullptr;
    record = new NwStreamRecord(segment, key);
    valid = record->IsValid();
    if (!valid) {
      delete record;
      record = nullptr;
    }
  }
}

// Capture current city version components, open the writable record and emit the original version/flags/magic header.
// Preserve both ignored magic-write failure and the immediate close of a stream whose earlier header write failed.
NwRecordFrame::NwRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 stored_version, u8 tracing)
    : version(stored_version), debug(tracing), extended(0), magic(0), record(nullptr), valid(0), cityVersion{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    cityVersion[component] = city->GetVersionComponent(component);
  record = new NwStreamRecord(segment, key, 0);
  valid = record->IsValid();
  valid = valid && record->Stream()->WriteU16(version) && record->Stream()->WriteU16(debug ? 3 : 2);
  if (valid)
    record->Stream()->WriteU32(kNwRecordFrameMarker);
  if (!valid) {
    delete record;
    record = nullptr;
  }
}

NwRecordFrame::~NwRecordFrame() {
  ScDelete(&record);
}

// Return the current borrowed stream, or null when opening or header initialization already retired its owner.
RsStream *NwRecordFrame::Stream() {
  return record ? record->Stream() : nullptr;
}
