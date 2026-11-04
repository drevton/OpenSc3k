#include "terrain/tr_dirt_bag.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/selection.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "terrain/api.h"
#include "terrain/tr_contour.h"
#include "terrain/tr_director.h"
#include "terrain/tr_tile.h"
#include "ui/api.h"

// Reachability initializes each row from its first and last edge-column bits before propagation.
enum TrReachabilityEdgeBit : u32 {
  kTrReachabilityLastColumnBit = 0x80000000,
};

// Terrain bit-grid loops run only when the cell count contains at least one complete 32-bit word.
enum TrBitGridWordCount : u32 {
  kTrBitGridWholeWordMask = 0xffffffe0,
};

static constexpr u32 kTrRecordFrameMarker = 0xdeadbeef;

// Read dirt before water through the original virtual methods. Publish the greater altitude in the caller's byte.
void TrDirtBag::GetVertexAltitude(u32 x, u32 z, u8 *altitude) const {
  u8 wet;
  GetVertexAltitudeDirt(x, z, altitude);
  GetVertexAltitudeWater(x, z, &wet);
  if (wet > *altitude)
    *altitude = wet;
}

void TrDirtBag::GetVertexAltitudeDirt(u32 x, u32 z, u8 *altitude) const {
  *altitude = ground->columns[x][z];
}

void TrDirtBag::GetVertexAltitudeWater(u32 x, u32 z, u8 *altitude) const {
  *altitude = water->columns[x][z];
}

void TrDirtBag::GetVertexLight(u32 x, u32 z, u8 *light) const {
  *light = ground->columns[x][z];
}

// Reject a cell without both diagonal vertices, then test its four corners in perimeter order.
// Equality counts as water; each sample retains its original virtual dispatch and early return.
bool TrDirtBag::IsWater(u32 x, u32 z) const {
  if (!InVertexBounds(x, z) || !InVertexBounds(x + 1, z + 1))
    return false;
  u8 dry, wet;
  GetVertexAltitudeDirt(x, z, &dry);
  GetVertexAltitudeWater(x, z, &wet);
  if (wet >= dry)
    return true;
  GetVertexAltitudeDirt(x + 1, z, &dry);
  GetVertexAltitudeWater(x + 1, z, &wet);
  if (wet >= dry)
    return true;
  GetVertexAltitudeDirt(x + 1, z + 1, &dry);
  GetVertexAltitudeWater(x + 1, z + 1, &wet);
  if (wet >= dry)
    return true;
  GetVertexAltitudeDirt(x, z + 1, &dry);
  GetVertexAltitudeWater(x, z + 1, &wet);
  return wet >= dry;
}

// Test the same ordered cell corners as IsWater. Only water strictly above ground qualifies here.
bool TrDirtBag::IsRealWater(u32 x, u32 z) const {
  if (!InVertexBounds(x, z) || !InVertexBounds(x + 1, z + 1))
    return false;
  u8 dry, wet;
  GetVertexAltitudeDirt(x, z, &dry);
  GetVertexAltitudeWater(x, z, &wet);
  if (wet > dry)
    return true;
  GetVertexAltitudeDirt(x + 1, z, &dry);
  GetVertexAltitudeWater(x + 1, z, &wet);
  if (wet > dry)
    return true;
  GetVertexAltitudeDirt(x + 1, z + 1, &dry);
  GetVertexAltitudeWater(x + 1, z + 1, &wet);
  if (wet > dry)
    return true;
  GetVertexAltitudeDirt(x, z + 1, &dry);
  GetVertexAltitudeWater(x, z + 1, &wet);
  return wet > dry;
}

// Count wet corners in perimeter order, validating each vertex independently. The SDK declares a byte result.
u8 TrDirtBag::CellWaterVertCount(u32 x, u32 z) const {
  u8 count = 0, dry, wet;
  if (InVertexBounds(x, z)) {
    GetVertexAltitudeDirt(x, z, &dry);
    GetVertexAltitudeWater(x, z, &wet);
    count = wet >= dry;
  }
  if (InVertexBounds(x + 1, z)) {
    GetVertexAltitudeDirt(x + 1, z, &dry);
    GetVertexAltitudeWater(x + 1, z, &wet);
    if (wet >= dry)
      ++count;
  }
  if (InVertexBounds(x + 1, z + 1)) {
    GetVertexAltitudeDirt(x + 1, z + 1, &dry);
    GetVertexAltitudeWater(x + 1, z + 1, &wet);
    if (wet >= dry)
      ++count;
  }
  if (InVertexBounds(x, z + 1)) {
    GetVertexAltitudeDirt(x, z + 1, &dry);
    GetVertexAltitudeWater(x, z + 1, &wet);
    if (wet >= dry)
      ++count;
  }
  return count;
}

u8 TrDirtBag::GetGlobalSeaLevel() const {
  return sea;
}

u32 TrDirtBag::CellCountX() const {
  return initialized ? ground->width - 1 : 0;
}

u32 TrDirtBag::CellCountZ() const {
  return initialized ? ground->height - 1 : 0;
}

u32 TrDirtBag::VertexCountX() const {
  return initialized ? ground->width : 0;
}

u32 TrDirtBag::VertexCountZ() const {
  return initialized ? ground->height : 0;
}

u8 TrDirtBag::MinAltitudeAllowed() const {
  return 0;
}

u8 TrDirtBag::MaxAltitudeAllowed() const {
  return 255;
}

u8 TrDirtBag::MaxAltitudeDeltaAllowed() const {
  return 4;
}

bool TrDirtBag::InCellBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool TrDirtBag::InCellBounds(u32 left, u32 top, u32 right, u32 bottom) const {
  return left <= right && top <= bottom && right < CellCountX() && bottom < CellCountZ();
}

bool TrDirtBag::InVertexBounds(u32 x, u32 z) const {
  return x < ground->width && z < ground->height;
}

bool TrDirtBag::InVertexBounds(u32 left, u32 top, u32 right, u32 bottom) const {
  return left <= right && top <= bottom && left < ground->width && top < ground->height && right < ground->width &&
      bottom < ground->height;
}

bool TrDirtBag::IsValidVertexAltitude(u8) const {
  return true;
}

bool TrDirtBag::IsValidVertexLight(u8 light) const {
  return light <= TrTerrainRenderer->GetMaxLightIndex();
}

u8 TrDirtBag::AltitudeMax() const {
  return MaxAltitudeAllowed();
}

u8 TrDirtBag::AltitudeMin() const {
  return MinAltitudeAllowed();
}

u8 TrDirtBag::MaxAltitudeDelta() const {
  return MaxAltitudeDeltaAllowed();
}

// Clear every allocated change-map word in X-major order. Tail bits are cleared together with valid cell bits.
void TrDirtBag::ResetChanges() const {
  for (u32 x = 0; x < changes->width; ++x)
    for (u32 word = 0; word < changes->words; ++word)
      changes->columns[x][word] = 0;
}

void TrDirtBag::MarkCellChanged(u32 x, u32 z) const {
  changes->columns[x][z >> 5] |= 1u << (z & 31);
}

bool TrDirtBag::ChangedByLastOperation(u32 x, u32 z) const {
  return (changes->columns[x][z >> 5] & (1u << (z & 31))) != 0;
}

// Invoke the live cell-bounds predicate for its effects, then return the original null string pointer.
// The discarded predicate result does not become this method's reply.
const ScStringView *TrDirtBag::GetQueryString(u32 x, u32 z) const {
  InCellBounds(x, z);
  return nullptr;
}

void TrDirtBag::Tick(i32, i32, const ScRect &, u32) {
}

// Construct all three interface identities and the empty circular filter chain, leaving sentinel payload unread.
TrDirtBag::TrDirtBag()
    : TrChangeSource(kTrDirtBagLayerType), initialized(false), references(0), filters(new TrFilterLink),
      ground(nullptr), shade(nullptr), water(nullptr), sea(246), changes(nullptr), flora_cache(nullptr),
      shore_cache(nullptr), distances(nullptr), water_reach(nullptr), water_open(nullptr), water_deep(nullptr),
      blocked_cells(nullptr), deep_water(false), city(nullptr) {
  filters->next = filters;
  filters->previous = filters;
}

// Shut down active terrain before retiring its filter chain. The source base then releases registered receivers.
TrDirtBag::~TrDirtBag() {
  if (initialized)
    Shutdown();
  TrFilterLink *node = filters->next;
  while (node != filters) {
    TrFilterLink *next = node->next;
    delete node;
    node = next;
  }
  filters->next = filters;
  filters->previous = filters;
  delete filters;
}

// Publish the actual primary, change-source or object-factory identity before acquiring the shared terrain reference.
bool TrDirtBag::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirtBag);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScDirtBag);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, ScDirtBag);
  HANDLE_QUERY_INTERFACE(IID_ScDirtBag, ScDirtBag);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScDirtBag);
  HANDLE_QUERY_INTERFACE(IID_CtChangeSource, TrChangeSource);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment under the separate reference lock, then acquire the director reference and reload the live object count.
u32 TrDirtBag::AddRef() {
  TrAdjustReference(&references, 1);
  TrGetDirector()->AddRef();
  return references;
}

// Drop the director reference before the locked decrement, then delete through the actual virtual destructor at zero.
u32 TrDirtBag::Release() {
  if (references) {
    TrGetDirector()->RemoveRef();
    TrAdjustReference(&references, -1);
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool TrDirtBag::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

bool TrDirtBag::DebugClassTag(ScStringView *) {
  return false;
}

bool TrDirtBag::DebugTypeTag(ScStringView *) {
  return false;
}

u32 TrDirtBag::GetMapSpriteCount() {
  return 1;
}

// Publish kind 10001 only for index zero; an unsupported index leaves output untouched.
bool TrDirtBag::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = kUiTerrainMapRasterId;
  return true;
}

// Create only the original terrain-map kind and query its requested interface. Failed queries free storage directly.
// The original failure omits destructors, so its retained terrain/palette references remain outstanding.
bool TrDirtBag::CreateMapSprite(u32 value, u32 iid, void **out) {
  if (value != kUiTerrainMapRasterId)
    return false;
  TrTerrainMapSprite *source = new TrTerrainMapSprite(this);
  if (source->QueryInterface(iid, out))
    return true;
  ::operator delete(source);
  return false;
}

u32 TrDirtBag::GetVariantValue(ScRect) {
  return 0;
}

TrChangeSource::TrChangeSource(u32 sender)
    : sender_id(sender), source_references(0), updates_enabled(1), receivers_begin(new CtChangeReceiver *[2]),
      receivers_end(receivers_begin), receivers_limit(receivers_begin + 2) {
}

TrChangeSource::~TrChangeSource() {
  RemoveAll();
  delete[] receivers_begin;
}

// Publish this secondary interface before acquiring its virtual reference; unsupported queries clear the output.
bool TrChangeSource::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtChangeSource);
  HANDLE_QUERY_INTERFACE(IID_CtChangeSource, CtChangeSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the source's independent count before acquiring a module reference, then reload the live count.
u32 TrChangeSource::AddRef() {
  ++source_references;
  TrGetDirector()->AddRef();
  return source_references;
}

u32 TrChangeSource::Release() {
  if (source_references) {
    TrGetDirector()->RemoveRef();
    --source_references;
  }
  if (!source_references) {
    delete this;
    return 0;
  }
  return source_references;
}

void TrChangeSource::SetEnabled(u8 enabled) {
  updates_enabled = enabled;
}

u8 TrChangeSource::GetEnabled() const {
  return updates_enabled;
}

void TrDirtBag::LockUpdates(bool locked) {
  SetEnabled(!locked);
}

u32 TrChangeSource::GetID() const {
  return sender_id;
}

// Grow only the original receiver pointer vector, retaining order and the existing retained references.
// This replaces its general-purpose vector allocation phase without adding game-level ownership operations.
void TrChangeSource::GrowReceivers() {
  usize count = receivers_begin ? (usize)(receivers_end - receivers_begin) : 0, capacity = count ? count * 2 : 1;
  if (capacity < count || capacity > SIZE_MAX / sizeof(*receivers_begin))
    ScFatal(SC_ERROR_NOMEM, "City-change receiver array cannot grow");
  CtChangeReceiver **data = new CtChangeReceiver *[capacity];
  if (count)
    memcpy(data, receivers_begin, count * sizeof(*data));
  delete[] receivers_begin;
  receivers_begin = data;
  receivers_end = data + count;
  receivers_limit = data + capacity;
}

// Publish the sender identifier before removing an existing registration, then append and retain the receiver.
// Removing first moves an existing receiver to the newest position while preserving the original callback ordering.
bool TrChangeSource::AddReceiver(CtChangeReceiver *receiver, u32 *sender) {
  if (!receiver)
    return false;
  *sender = sender_id;
  RemoveReceiver(receiver);
  if (receivers_end == receivers_limit)
    GrowReceivers();
  *receivers_end++ = receiver;
  receiver->AddRef();
  return true;
}

// Find the last matching registration and release it before shifting the live array and reducing its current end.
// Keep the captured search limit and reload the vector fields after the receiver callback, matching defined reentry.
bool TrChangeSource::RemoveReceiver(CtChangeReceiver *receiver) {
  if (!receiver)
    return false;
  isize last = receivers_end - receivers_begin - 1, selected = last;
  while (selected >= 0 && receivers_begin[selected] != receiver)
    --selected;
  if (selected < 0)
    return false;
  receiver->Release();
  while (selected < last) {
    receivers_begin[selected] = receivers_begin[selected + 1];
    ++selected;
  }
  --receivers_end;
  return true;
}

// Release the original receiver count in reverse order, decrementing the live end after each callback.
// A receiver callback may mutate the vector; only source-defined surviving entries are valid for subsequent iterations.
bool TrChangeSource::RemoveAll() {
  isize index = receivers_end - receivers_begin;
  while (--index >= 0) {
    receivers_begin[index]->Release();
    --receivers_end;
  }
  return true;
}

// Notify the captured receiver count in reverse order when the raw enabled byte was nonzero on entry.
// Reload the current array and sender for each callback, and ignore each receiver's Boolean reply.
bool TrChangeSource::NotifyAll() {
  if (updates_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChangedAll(sender_id);
  }
  return true;
}

// Notify one cell through the genuine city-change receiver interface, preserving reverse live-vector traversal.
bool TrChangeSource::Notify(u32 x, u32 z) {
  if (updates_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChanged(sender_id, x, z);
  }
  return true;
}

// Notify a rectangle through the original overload with the same four coordinate arguments and callback ordering.
bool TrChangeSource::Notify(u32 left, u32 top, u32 right, u32 bottom) {
  if (updates_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChanged(sender_id, left, top, right, bottom);
  }
  return true;
}

TrRecordGuard::TrRecordGuard(RsDatabaseSegment *source, RsKey key) : aborted(0), record(nullptr), segment(source) {
  if (source && !source->OpenRecord(key, &record))
    record = nullptr;
}

TrRecordGuard::TrRecordGuard(RsDatabaseSegment *source, RsKey key, i32) : aborted(0), record(nullptr), segment(source) {
  if (source && !source->OpenRecordForWrite(key, &record))
    record = nullptr;
}

TrRecordGuard::~TrRecordGuard() {
  if (record) {
    if (!aborted)
      aborted = !segment->CloseRecord(&record);
    if (record && aborted)
      segment->AbortRecord(&record);
  }
}

bool TrRecordGuard::IsValid() const {
  return record != nullptr;
}

// Return the borrowed record without acquiring a reference, as the retail source declaration and field getter establish.
// The identical DLL field getter has no proven class association, so no separate SIMDIRT entry is asserted here.
RsRecord *TrRecordGuard::GetDBRecordPointer() const {
  return record;
}

TrRecordStream::TrRecordStream(RsDatabaseSegment *source, RsKey key)
    : TrRecordGuard(source, key), stream_identity(nullptr) {
  if (TrRecordGuard::IsValid() && !record->QueryInterface(IID_RsStream, &stream_identity))
    stream_identity = nullptr;
}

// Acquire the same stream identity from the newly opened writable record, preserving the final constructor argument.
TrRecordStream::TrRecordStream(RsDatabaseSegment *source, RsKey key, i32 flags)
    : TrRecordGuard(source, key, flags), stream_identity(nullptr) {
  if (TrRecordGuard::IsValid() && !record->QueryInterface(IID_RsStream, &stream_identity))
    stream_identity = nullptr;
}

// Release the currently published stream before the inherited record close/abort sequence executes.
TrRecordStream::~TrRecordStream() {
  if (stream_identity) {
    static_cast<RsStream *>(stream_identity)->Release();
    stream_identity = nullptr;
  }
}

bool TrRecordStream::IsValid() const {
  return TrRecordGuard::IsValid() && stream_identity != nullptr;
}

// Return the borrowed stream currently held in the actual external QueryInterface publication slot.
RsStream *TrRecordStream::GetDBSerialRecordPointer() const {
  return static_cast<RsStream *>(stream_identity);
}

TrRecordVersion::TrRecordVersion() : version(0), debug(0), extended(0), magic(0) {
}

TrRecordVersion::TrRecordVersion(u16 stored_version, u8 tracing)
    : version(stored_version), debug(tracing), extended(0), magic(0) {
}

TrRecordVersion::~TrRecordVersion() {
}

// Read the optional versioned frame header, reopening the same record at its beginning for the original raw format.
TrRecordFrame::TrRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : TrRecordVersion(), record(nullptr), valid(0), city_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    city_version[component] = i32(city->GetVersionComponentFromSaveFile(component));
  record = new TrRecordStream(segment, key);
  valid = record->IsValid();
  if (!valid) {
    delete record;
    record = nullptr;
    return;
  }
  valid = record->GetDBSerialRecordPointer()->ReadU16(&version);
  if (valid) {
    u8 header = 0;
    valid = record->GetDBSerialRecordPointer()->ReadU8(&header);
    debug = header & 1;
    extended = (header & 2) != 0;
    if (extended && valid)
      valid = record->GetDBSerialRecordPointer()->ReadU8(&header);
  }
  if (valid)
    record->GetDBSerialRecordPointer()->ReadU32(&magic);
  if (valid)
    valid = magic == kTrRecordFrameMarker;
  if (magic != kTrRecordFrameMarker) {
    debug = 0;
    version = 0;
    magic = kTrRecordFrameMarker;
    delete record;
    record = nullptr;
    record = new TrRecordStream(segment, key);
    valid = record->IsValid();
    if (!valid) {
      delete record;
      record = nullptr;
    }
  }
}

// Capture current city version components, open the writable record and emit the original version/flags/magic header.
// Preserve both ignored magic-write failure and the immediate close of a stream whose earlier header write failed.
TrRecordFrame::TrRecordFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 stored_version, bool tracing)
    : TrRecordVersion(stored_version, tracing), record(nullptr), valid(0), city_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    city_version[component] = i32(city->GetVersionComponent(component));
  record = new TrRecordStream(segment, key, 0);
  valid = record->IsValid();
  valid = valid && record->GetDBSerialRecordPointer()->WriteU16(version) &&
      record->GetDBSerialRecordPointer()->WriteU16(debug ? 3 : 2);
  if (valid)
    record->GetDBSerialRecordPointer()->WriteU32(kTrRecordFrameMarker);
  if (!valid) {
    delete record;
    record = nullptr;
  }
}

TrRecordFrame::~TrRecordFrame() {
  ScDelete(&record);
}

// Return the current borrowed stream, or null when opening or header initialization already retired its owner.
RsStream *TrRecordFrame::GetDBSerialRecordPointer() const {
  return record ? record->GetDBSerialRecordPointer() : nullptr;
}

// Select abort on the current record before clearing frame validity. The original source declaration returns void.
void TrRecordFrame::SetFailed() {
  if (record)
    record->aborted = 1;
  valid = 0;
}

// Allocate the original X-major byte plane, filling columns and their bytes in reverse order.
// This native storage phase replaces only the inline concrete grid allocation, not a game method contract.
static TrByteGrid *TrAllocateByteGrid(u32 width, u32 height, u8 value) {
  TrByteGrid *grid = new TrByteGrid;
  grid->width = width;
  grid->height = height;
  grid->columns = new u8 *[width];
  for (i32 x = (i32)(width - 1); x >= 0; --x) {
    grid->columns[x] = new u8[height];
    for (i32 z = (i32)(height - 1); z >= 0; --z)
      grid->columns[x][z] = value;
  }
  return grid;
}

// Allocate all original packed-bit columns before clearing their complete words, including padding bits.
static TrChangeGrid *TrAllocateChangeGrid(u32 width, u32 height) {
  TrChangeGrid *grid = new TrChangeGrid;
  grid->width = width;
  grid->height = height;
  grid->words = (height >> 5) + ((height & 31) != 0);
  grid->columns = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    grid->columns[x] = new u32[grid->words];
  for (u32 x = 0; x < width; ++x)
    for (u32 word = 0; word < grid->words; ++word)
      grid->columns[x][word] = 0;
  return grid;
}

// Retire byte columns in the original reverse order, then their pointer array and concrete grid owner.
static void TrDeleteByteGrid(TrByteGrid *grid) {
  if (grid->columns) {
    for (i32 x = (i32)(grid->width - 1); x >= 0; --x)
      delete[] grid->columns[x];
    delete[] grid->columns;
  }
  delete grid;
}

// Retire packed-bit columns in the original ascending order before deleting their pointer array and grid owner.
static void TrDeleteChangeGrid(TrChangeGrid *grid) {
  if (grid->columns) {
    for (u32 x = 0; x < grid->width; ++x)
      delete[] grid->columns[x];
    delete[] grid->columns;
  }
  delete grid;
}

// Create the city-sized terrain and initialization caches with updates disabled, then publish generator data and shade.
// Bind the real renderer before enabling updates and subscribing this same primary message receiver to city/cheat events.
bool TrDirtBag::Init(CtCity *world) {
  TrLockScope lock;
  city = world;
  SetEnabled(0);
  if (initialized)
    Shutdown();
  initialized = true;
  TrGeneratorInterface *generator = world->DirtGenerator();
  TrAltitudeScale = world->CellSizeInWorldUnitsY();
  u32 height = world->CellCountZ(), width = world->CellCountX();
  changes = TrAllocateChangeGrid(width, height);
  height = world->CellCountZ() + 1;
  width = world->CellCountX() + 1;
  ground = TrAllocateByteGrid(width, height, 127);
  height = world->CellCountZ();
  width = world->CellCountX();
  water_reach = new u32[(width * height) >> 5];
  height = world->CellCountZ();
  width = world->CellCountX();
  water_open = new u32[(width * height) >> 5];
  height = world->CellCountZ();
  width = world->CellCountX();
  water_deep = new u32[(width * height) >> 5];
  height = world->CellCountZ();
  width = world->CellCountX();
  blocked_cells = new u32[(width * height) >> 5];
  u32 word = 0;
  height = world->CellCountZ();
  width = world->CellCountX();
  if ((width * height) & kTrBitGridWholeWordMask) {
    do {
      blocked_cells[word] = 0;
      water_deep[word] = 0;
      water_open[word] = 0;
      water_reach[word] = 0;
      ++word;
      height = world->CellCountZ();
      width = world->CellCountX();
    } while (word < (width * height) >> 5);
  }
  sea = 246;
  water = TrAllocateByteGrid(ground->width, ground->height, sea);
  flora_cache = TrAllocateByteGrid(ground->width, ground->height, 0);
  shore_cache = TrAllocateChangeGrid(ground->width, ground->height);
  shade = TrAllocateByteGrid(ground->width + 1, ground->height + 1, 0);
  if (generator && generator->IsReady())
    generator->ReflectMap(this);
  u32 bottom = VertexCountZ() - 1, right = VertexCountX() - 1;
  CalculateAndSetVertexLight(0, 0, right, bottom);
  TrTerrainRenderer->SetDirtBag(this);
  SetEnabled(1);
  TrGetMessageServer()->AddNotification(this, kTrMsg_RegenerateTerrain);
  TrGetMessageServer()->AddNotification(this, kWnMsg_EnterTerrainEditor);
  TrGetApplication()->GetCheatCodeManager()->AddNotification(this, 0);
  return true;
}

// Unsubscribe the same terrain message receiver before disabling updates and retiring the owned active planes/maps.
// Preserve the original untouched city, sea, flags and initialization-cache fields, then clear only initialized.
bool TrDirtBag::Shutdown() {
  TrLockScope lock;
  TrGetApplication()->GetCheatCodeManager()->RemoveNotification(this, 0);
  TrGetMessageServer()->RemoveNotification(this, kTrMsg_RegenerateTerrain);
  TrGetMessageServer()->RemoveNotification(this, kWnMsg_EnterTerrainEditor);
  SetEnabled(0);
  if (ground) {
    TrDeleteByteGrid(ground);
    ground = nullptr;
  }
  if (shade) {
    TrDeleteByteGrid(shade);
    shade = nullptr;
  }
  if (water) {
    TrDeleteByteGrid(water);
    water = nullptr;
  }
  if (distances) {
    delete[] distances;
    distances = nullptr;
  }
  if (water_reach) {
    delete[] water_reach;
    water_reach = nullptr;
  }
  if (water_open) {
    delete[] water_open;
    water_open = nullptr;
  }
  if (water_deep) {
    delete[] water_deep;
    water_deep = nullptr;
  }
  if (blocked_cells) {
    delete[] blocked_cells;
    blocked_cells = nullptr;
  }
  if (changes) {
    TrDeleteChangeGrid(changes);
    changes = nullptr;
  }
  initialized = false;
  return true;
}

// Clear every current cell through the global city's live pollution interface, reloading terrain dimensions in each loop.
// This native phase is shared by simulation initialization and the original regeneration/cheat message paths.
void TrDirtBag::ClearSaltFlags() const {
  EcLayer *layer = TrGetCity()->PollutionLayer();
  if (layer)
    for (u32 x = 0; x < VertexCountX() - 1; ++x)
      for (u32 z = 0; z < VertexCountZ() - 1; ++z)
        layer->SetIsSaltWater(x, z, false);
}

// Publish the retained initialization shore/flora grids through actual city layers before retiring their current owners.
// The caller supplies the original lock boundary: simulation startup holds it, while terrain message dispatch does not.
void TrDirtBag::PublishInitialLayers() {
  if (shore_cache) {
    EcLayer *layer = TrGetCity()->PollutionLayer();
    if (layer) {
      ClearSaltFlags();
      for (u32 x = 0; x < VertexCountX() - 1; ++x) {
        for (u32 z = 0; z < VertexCountZ() - 1; ++z) {
          u32 north = z >> 5, south = (z + 1) >> 5, north_bit = 1u << (z & 31), south_bit = 1u << ((z + 1) & 31);
          bool value = (shore_cache->columns[x][north] & north_bit) != 0;
          if (!value)
            value = (shore_cache->columns[x][south] & south_bit) != 0;
          if (!value)
            value = (shore_cache->columns[x + 1][south] & south_bit) != 0;
          bool north_east = (shore_cache->columns[x + 1][north] & north_bit) != 0;
          layer->SetIsSaltWater(x, z, value || north_east);
        }
      }
    }
    if (shore_cache)
      TrDeleteChangeGrid(shore_cache);
    shore_cache = nullptr;
  }
  if (flora_cache) {
    GeFlora *layer = TrGetCity()->FloraLayer();
    if (layer) {
      for (u32 x = 0; x < VertexCountX() - 1; ++x) {
        for (u32 z = 0; z < VertexCountZ() - 1; ++z) {
          u8 flora = flora_cache->columns[x][z];
          if (flora >= 32) {
            CtCoord point = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
            layer->SetDensity(point, flora >> 5);
          }
        }
      }
    }
    if (flora_cache)
      TrDeleteByteGrid(flora_cache);
    flora_cache = nullptr;
  }
}

// Apply initialization shore bits and cached interior flora while holding the terrain lock.
// The shared phase preserves separate live pollution receivers and retires shore storage before any flora callback.
bool TrDirtBag::SimulationBegin() {
  TrLockScope lock;
  PublishInitialLayers();
  return true;
}

bool TrDirtBag::SimulationEnd() {
  return true;
}

// Retire initialization caches before loading the versioned terrain record into the existing live grids and blocked map.
// Preserve descending column transfers, the sea-byte exact-one branch and last blocked-word result before recomputing shade.
bool TrDirtBag::Init(CtCity *world, RsDatabaseSegment *segment) {
  TrLockScope lock;
  city = world;
  SetEnabled(0);
  if (flora_cache)
    TrDeleteByteGrid(flora_cache);
  flora_cache = nullptr;
  if (shore_cache)
    TrDeleteChangeGrid(shore_cache);
  shore_cache = nullptr;
  TrRecordFrame frame(world, segment, RsKey{kRsCityLayerRecordType, kTrDirtBagLayerType, 0});
  u8 result = 1;
  if (frame.IsValid()) {
    RsStream *stream = frame.GetDBSerialRecordPointer();
    if (frame.IsDebug()) {
      ScString text;
      result = stream->ReadString(&text);
    }
    if (result)
      result = stream->ReadF32(&TrAltitudeScale);
    if (result) {
      TrByteGrid *grid = ground;
      result = 1;
      for (i32 x = (i32)(grid->width - 1); x >= 0; --x)
        if (!stream->ReadBytes(grid->columns[x], grid->height))
          result = 0;
      if (result) {
        result = stream->ReadU8(&sea);
        if (result == 1) {
          grid = water;
          result = 1;
          for (i32 x = (i32)(grid->width - 1); x >= 0; --x)
            if (!stream->ReadBytes(grid->columns[x], grid->height))
              result = 0;
        }
        if (result) {
          u32 word = 0, height = world->CellCountZ(), width = world->CellCountX();
          if ((width * height) & kTrBitGridWholeWordMask) {
            do {
              result = stream->ReadU32(blocked_cells + word);
              ++word;
              height = world->CellCountZ();
              width = world->CellCountX();
            } while (word < (width * height) >> 5);
          }
          if (result && frame.IsDebug()) {
            ScString text;
            result = stream->ReadString(&text);
          }
          if (result) {
            u32 bottom = VertexCountZ() - 1, right = VertexCountX() - 1;
            CalculateAndSetVertexLight(0, 0, right, bottom);
          }
        }
      }
    }
    if (!result)
      frame.SetFailed();
  }
  SetEnabled(1);
  return result != 0;
}

// Save current scale, descending ground/water columns and blocked words through the actual versioned record owner.
// Keep live dimension callbacks after failures, skip subsequent blocked writes and select record abort when saving fails.
bool TrDirtBag::Save(CtCity *world, RsDatabaseSegment *segment) {
  TrLockScope lock;
  TrRecordFrame frame(world, segment, RsKey{kRsCityLayerRecordType, kTrDirtBagLayerType, 0}, 3, false);
  u8 result = frame.IsValid();
  if (result) {
    RsStream *stream = frame.GetDBSerialRecordPointer();
    if (frame.IsDebug()) {
      ScString text("DirtBag Start", 13);
      result = stream->WriteString(&text);
    }
    if (result)
      result = stream->WriteF32(TrAltitudeScale);
    if (result) {
      TrByteGrid *grid = ground;
      result = 1;
      for (i32 x = (i32)(grid->width - 1); x >= 0; --x)
        if (!stream->WriteBytes(grid->columns[x], grid->height))
          result = 0;
      if (result) {
        result = stream->WriteU8(sea);
        if (result) {
          grid = water;
          result = 1;
          for (i32 x = (i32)(grid->width - 1); x >= 0; --x)
            if (!stream->WriteBytes(grid->columns[x], grid->height))
              result = 0;
        }
      }
    }
    u32 word = 0, height = world->CellCountZ(), width = world->CellCountX();
    if ((width * height) & kTrBitGridWholeWordMask) {
      do {
        if (result)
          result = stream->WriteU32(blocked_cells[word]);
        ++word;
        height = world->CellCountZ();
        width = world->CellCountX();
      } while (word < (width * height) >> 5);
    }
    if (result && frame.IsDebug()) {
      ScString text("DirtBag End", 11);
      result = stream->WriteString(&text);
    }
  }
  if (!result)
    frame.SetFailed();
  return result != 0;
}

// Register the seven original terrain commands through one borrowed cheat manager, retiring each temporary name in
// order.
void TrDirtBag::RegisterCheats() {
  ScCheatCodeManager *manager = TrGetApplication()->GetCheatCodeManager();
  const char *names[] = {"Load terrain", "Salt on", "Salt off", "Terrain one up", "Terrain one down", "Terrain ten up",
      "Terrain ten down"};
  const u32 identifiers[] = {kTrLoadTerrainCheatId, kTrSaltOnCheatId, kTrSaltOffCheatId, kTrTerrainOneUpCheatId,
      kTrTerrainOneDownCheatId, kTrTerrainTenUpCheatId, kTrTerrainTenDownCheatId};
  const u32 lengths[] = {12, 7, 8, 14, 16, 14, 16};
  for (u32 i = 0; i < 7; ++i) {
    ScString name(names[i], lengths[i]);
    manager->RegisterCheatCode(identifiers[i], name);
  }
}

// Publish and retain the fully constructed shared renderer, subscribe it, clear its grid flag and register terrain commands.
bool TrDirtBag::StaticInit(CtDefinition *) {
  TrTerrainRenderer = TrGetRenderer();
  if (!TrTerrainRenderer)
    return false;
  TrTerrainRenderer->AddRef();
  TrTerrainRenderer->Init();
  TrTerrainRenderer->SetGrid(0);
  RegisterCheats();
  return true;
}

// Shut down the current shared renderer before releasing it, then clear the terrain global even when it survived release.
bool TrDirtBag::StaticShutdown() {
  if (TrTerrainRenderer) {
    TrTerrainRenderer->Shutdown();
    TrTerrainRenderer->Release();
    TrTerrainRenderer = nullptr;
  }
  return true;
}

f32 TrAltitudeScale;

f32 TrDirtBag::AltitudeScale() const {
  return initialized ? TrAltitudeScale : 0.0f;
}

// Mark adjacent cells before changing one ground byte, then recompute its three-vertex lighting margin when enabled.
// Release the recursive terrain lock before publishing the affected changed cells; unchanged heights return true directly.
bool TrDirtBag::SetVertexAltitude(u32 x, u32 z, u8 height) {
  TrTerrainLock.Lock();
  if (ground->columns[x][z] == height) {
    TrTerrainLock.Unlock();
    return true;
  }
  MarkVertexChanged(x, z);
  ground->columns[x][z] = height;
  u8 enabled = GetEnabled();
  u32 left = 0, top = 0, right = 0, bottom = 0;
  if (enabled) {
    left = x >= 3 ? x - 3 : 0;
    top = z >= 3 ? z - 3 : 0;
    right = x + 3;
    if (right > VertexCountX() - 1)
      right = VertexCountX() - 1;
    bottom = z + 3;
    if (bottom > VertexCountZ() - 1)
      bottom = VertexCountZ() - 1;
    CalculateAndSetVertexLight(left, top, right, bottom);
  }
  TrTerrainLock.Unlock();
  if (enabled)
    PublishChanges(left, top, right - 1, bottom - 1, 1);
  return true;
}

// Scan and mark every differing ground vertex before filling the rectangle's captured columns.
// Enabled updates shade the expanded region while locked and publish changed cells only after unlocking.
bool TrDirtBag::SetVertexAltitude(u32 left, u32 top, u32 right, u32 bottom, u8 height) {
  TrTerrainLock.Lock();
  bool changed = false;
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      if (ground->columns[x][z] != height) {
        MarkVertexChanged(x, z);
        changed = true;
      }
    }
  }
  if (!changed) {
    TrTerrainLock.Unlock();
    return true;
  }
  TrByteGrid *grid = ground;
  for (u32 x = left; x <= right; ++x)
    memset(grid->columns[x] + top, height, bottom - top + 1);
  u8 enabled = GetEnabled();
  u32 shade_top = 0, shade_right = 0, shade_bottom = 0;
  if (enabled) {
    left = left >= 3 ? left - 3 : 0;
    shade_top = top >= 3 ? top - 3 : 0;
    shade_right = right + 3;
    if (shade_right > VertexCountX() - 1)
      shade_right = VertexCountX() - 1;
    shade_bottom = bottom + 3;
    if (shade_bottom > VertexCountZ() - 1)
      shade_bottom = VertexCountZ() - 1;
    CalculateAndSetVertexLight(left, shade_top, shade_right, shade_bottom);
  }
  TrTerrainLock.Unlock();
  if (enabled)
    PublishChanges(left, shade_top, shade_right - 1, shade_bottom - 1, 1);
  return true;
}

// Mark changed water vertices before filling the captured water columns, preserving the original notification bounds.
// A rectangle spanning an axis publishes the preceding cell on that axis; no hillshade is computed here.
bool TrDirtBag::SetWaterTable(u32 left, u32 top, u32 right, u32 bottom, u8 height) {
  TrTerrainLock.Lock();
  bool changed = false;
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      if (water->columns[x][z] != height) {
        MarkVertexChanged(x, z);
        changed = true;
      }
    }
  }
  if (!changed) {
    TrTerrainLock.Unlock();
    return true;
  }
  TrByteGrid *grid = water;
  for (u32 x = left; x <= right; ++x)
    memset(grid->columns[x] + top, height, bottom - top + 1);
  u8 enabled = GetEnabled();
  u32 notify_right = right <= left ? right : left - 1, notify_bottom = bottom <= top ? bottom : top - 1;
  TrTerrainLock.Unlock();
  if (enabled)
    PublishChanges(left, top, notify_right, notify_bottom, 1);
  return true;
}

// Store one water byte, then update its initialization shore bit or mark adjacent city cells as salt water.
// Preserve the live extent queries between pollution-layer calls and publish changes after releasing the terrain lock.
bool TrDirtBag::SetWaterTable(u32 x, u32 z, u8 height, bool shore) {
  TrTerrainLock.Lock();
  water->columns[x][z] = height;
  if (shore_cache) {
    u32 *word = &shore_cache->columns[x][z >> 5], bit = 1u << (z & 31);
    if (shore)
      *word |= bit;
    else
      *word &= ~bit;
  } else if (shore) {
    EcLayer *layer = TrGetCity()->PollutionLayer();
    if (layer) {
      if (x) {
        if (z)
          layer->SetIsSaltWater(x - 1, z - 1, true);
        if (z < VertexCountZ() - 1)
          layer->SetIsSaltWater(x - 1, z, true);
      }
      if (x < VertexCountX() - 1) {
        if (z)
          layer->SetIsSaltWater(x, z - 1, true);
        if (z < VertexCountZ() - 1)
          layer->SetIsSaltWater(x, z, true);
      }
    }
  }
  TrTerrainLock.Unlock();
  if (GetEnabled())
    PublishChanges(x, z, x, z, 1);
  return true;
}

// Store initialization flora bytes directly when the cache exists; otherwise place density through the live city layer.
// Preserve the first city query even below the placement threshold and the second query for qualifying levels.
bool TrDirtBag::InitFloraLevel(u32 x, u32 z, u8 flora) {
  TrTerrainLock.Lock();
  if (flora_cache) {
    flora_cache->columns[x][z] = flora;
    TrTerrainLock.Unlock();
    return true;
  }
  if (TrGetCity() && flora >= 32) {
    GeFlora *layer = TrGetCity()->FloraLayer();
    if (layer) {
      CtCoord point = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
      layer->SetDensity(point, flora >> 5);
      TrTerrainLock.Unlock();
      return true;
    }
  }
  TrTerrainLock.Unlock();
  return false;
}

// Clear salt-water cells, then project qualifying wet borders inward through the original quarter-extent limits.
// Each ray stops after five dry samples; notify the owning city's optional neighbors layer after all four passes.
bool TrDirtBag::RecalcSaltWater() {
  TrLockScope lock;
  EcLayer *layer = TrGetCity()->PollutionLayer();
  if (!layer)
    return false;
  for (u32 x = 0; x < VertexCountX() - 1; ++x)
    for (u32 z = 0; z < VertexCountZ() - 1; ++z)
      layer->SetIsSaltWater(x, z, false);
  u32 wet = 0;
  for (u32 x = 0; x < city->CellCountX(); ++x)
    if (IsWater(x, 0))
      ++wet;
  u8 edges = 100 * wet > 60 * VertexCountX() ? 1 : 0;
  wet = 0;
  for (u32 z = 0; z < city->CellCountZ(); ++z)
    if (IsWater(city->CellCountX() - 1, z))
      ++wet;
  if (100 * wet > 60 * VertexCountZ())
    edges |= 2;
  wet = 0;
  for (u32 x = 0; x < city->CellCountX(); ++x)
    if (IsWater(x, city->CellCountZ() - 1))
      ++wet;
  if (100 * wet > 60 * VertexCountX())
    edges |= 4;
  wet = 0;
  for (u32 z = 0; z < city->CellCountZ(); ++z)
    if (IsWater(0, z))
      ++wet;
  if (100 * wet > 60 * VertexCountZ())
    edges |= 8;
  if (edges & 1) {
    for (u32 x = 0; x < city->CellCountX(); ++x) {
      u32 remaining = VertexCountX() >> 2, z = 0, dry = 5;
      while (remaining) {
        u8 dirt, water_height;
        GetVertexAltitudeDirt(x, z, &dirt);
        GetVertexAltitudeWater(x, z, &water_height);
        if (water_height < dirt - 1)
          --dry;
        --remaining;
        layer->SetIsSaltWater(x, z++, true);
        if (!dry)
          break;
      }
    }
  }
  if (edges & 2) {
    for (u32 z = 0; z < city->CellCountZ(); ++z) {
      u32 dry = 5, x = city->CellCountX() - 1, remaining = VertexCountZ() >> 2;
      while (remaining) {
        u8 dirt, water_height;
        GetVertexAltitudeDirt(x, z, &dirt);
        GetVertexAltitudeWater(x, z, &water_height);
        if (water_height < dirt - 1)
          --dry;
        layer->SetIsSaltWater(x--, z, true);
        --remaining;
        if (!dry)
          break;
      }
    }
  }
  if (edges & 4) {
    for (u32 x = 0; x < city->CellCountX(); ++x) {
      u32 dry = 5, z = city->CellCountZ() - 1, remaining = VertexCountX() >> 2;
      while (remaining) {
        u8 dirt, water_height;
        GetVertexAltitudeDirt(x, z, &dirt);
        GetVertexAltitudeWater(x, z, &water_height);
        if (water_height < dirt - 1)
          --dry;
        layer->SetIsSaltWater(x, z--, true);
        --remaining;
        if (!dry)
          break;
      }
    }
  }
  if (edges & 8) {
    for (u32 z = 0; z < city->CellCountZ(); ++z) {
      u32 dry = 5, remaining = VertexCountZ() >> 2, x = 0;
      while (remaining) {
        u8 dirt, water_height;
        GetVertexAltitudeDirt(x, z, &dirt);
        GetVertexAltitudeWater(x, z, &water_height);
        if (water_height < dirt - 1)
          --dry;
        --remaining;
        layer->SetIsSaltWater(x++, z, true);
        if (!dry)
          break;
      }
    }
  }
  CtNeighborsLayer *neighbors = city->NeighborsLayer();
  if (neighbors)
    neighbors->InitializeNeighbors();
  return true;
}

static u8 TrImportSlope(u8 shape) {
  static const u8 corners[13] = {0, 3, 10, 12, 5, 11, 14, 13, 7, 2, 8, 4, 1};
  return shape <= 12 ? corners[shape] : 0;
}

// Import SC2 altitudes and slope corners into existing terrain storage, then copy shore bits and recompute shade.
// Retain byte outputs across failed reads: bias altitude in place and reuse the northwest scratch for shore flags.
// Read the optional sea property before filling water and projecting the water-bearing tile corners above it.
bool TrDirtBag::Init(CtCity *world, ScImporter *importer) {
  TrLockScope lock;
  city = world;
  SetEnabled(0);
  if (importer->MapIsPresent(kCtLegacyAltmPlane)) {
    u16 size = importer->GetMapSize(kCtLegacyAltmPlane);
    u8 cell, altitude, north_west;
    for (u32 x = 0; (u16)x < size; ++x) {
      for (u32 z = 0; z < size; ++z) {
        importer->GetTerrainAlt((u16)x, z, &altitude);
        altitude += 16;
        u8 base = (u8)(altitude * 2), south_east = base, north_east = base;
        north_west = base;
        if (importer->GetValue(kCtLegacyXterPlane, (u16)x, z, &cell)) {
          cell = cell >= 0x30 ? 0 : cell & 15;
          u8 raised = TrImportSlope(cell);
          north_east += (raised & 1) ? 2 : 0;
          north_west += (raised & 2) ? 2 : 0;
          south_east += (raised & 4) ? 2 : 0;
          base += (raised & 8) ? 2 : 0;
        }
        ground->columns[(u16)x + 1][z] = north_east;
        if (!cell) {
          ground->columns[(u16)x][z] = north_west;
          ground->columns[(u16)x + 1][z + 1] = south_east;
          ground->columns[(u16)x][z + 1] = base;
        } else {
          if (!(u16)x)
            ground->columns[0][z] = north_west;
          if (z == (u32)size - 1) {
            ground->columns[(u16)x + 1][z + 1] = south_east;
            if (!(u16)x)
              ground->columns[0][z + 1] = base;
          }
        }
      }
    }
    if (importer->MapIsPresent(kCtLegacyXbitPlane)) {
      for (u32 x = 0; x < size; ++x) {
        for (u32 z = 0; z < size; ++z) {
          importer->GetValue(kCtLegacyXbitPlane, x, z, &north_west);
          if (north_west & 1)
            shore_cache->columns[x][z >> 5] |= 1u << (z & 31);
        }
      }
    }
    initialized = true;
    u32 bottom = VertexCountZ() - 1, right = VertexCountX() - 1;
    CalculateAndSetVertexLight(0, 0, right, bottom);
  }
  u32 level;
  if (importer->MapIsPresent(kCtLegacyMiscPlane) && importer->GetMiscValue(0x390, reinterpret_cast<i32 *>(&level)))
    sea = (u8)((level + 16) * 2 - 1);
  else
    sea = 246;
  TrByteGrid *wet = water;
  u32 height = wet->height;
  for (u32 x = 0; x < wet->width; ++x)
    memset(wet->columns[x], sea, height);
  u32 x = 0;
  if (VertexCountX() - 1) {
    do {
      u32 z = 0;
      if (VertexCountZ() - 1) {
        do {
          u8 cell;
          if (importer->GetValue(kCtLegacyXterPlane, (u16)x, z, &cell) && cell >= 0x30) {
            u8 north_east = ground->columns[(u16)x + 1][z], north_west = ground->columns[(u16)x][z];
            u8 south_east = ground->columns[(u16)x + 1][z + 1], south_west = ground->columns[(u16)x][z + 1];
            if (cell < 0x42) {
              u8 corners = cell > 0x30 && cell < 0x40 ? (u8)(15 ^ TrImportSlope(cell & 15)) : 15;
              if ((corners & 1) && north_east > sea)
                water->columns[(u16)x + 1][z] = north_east;
              if ((corners & 2) && north_west > sea)
                water->columns[(u16)x][z] = north_west;
              if ((corners & 4) && south_east > sea)
                water->columns[(u16)x + 1][z + 1] = south_east;
              if ((corners & 8) && south_west > sea)
                water->columns[(u16)x][z + 1] = south_west;
            }
          }
          z = (u16)(z + 1);
        } while (z < VertexCountZ() - 1);
      }
      ++x;
    } while ((u16)x < VertexCountX() - 1);
  }
  SetEnabled(1);
  return true;
}

// Load an eight-bit heightmap into the existing terrain, reading image rows as X columns and defaulting outside it to sea30.
// Release the image before resetting water and shading; publish the entire cell rectangle after unlocking and enabling updates.
bool TrDirtBag::GenerateAltFromBMP(ScStringView *name) {
  TrTerrainLock.Lock();
  sea = 30;
  ScString path(*name);
  GxBuffer *image = nullptr;
  if (TrGetApplication() && TrGetApplication()->GetBufferResourceFactory()) {
    const GxColorType format(GxColorType::Color::Pal8);
    TrGetApplication()->GetBufferResourceFactory()->LoadFile(&path, &image, 0, &format, 0);
  }
  if (!image) {
    TrTerrainLock.Unlock();
    return false;
  }
  SetEnabled(0);
  i32 width = i32(VertexCountX()), height = i32(VertexCountZ());
  if (image->Lock(GxLockFlags::GetSetPixel)) {
    u8 *data = static_cast<u8 *>(image->GetColorSurfaceBits());
    i32 pitch = i32(image->GetColorSurfaceStride());
    for (i32 x = 0; x < width; ++x) {
      for (i32 z = 0; z < height; ++z) {
        u8 altitude;
        if (x >= (i32)image->Height() || z >= (i32)image->Width())
          altitude = sea;
        else
          altitude = data[iptr(x) * pitch + z];
        SetVertexAltitude(u32(x), u32(z), altitude);
      }
    }
    image->Unlock(GxLockFlags::GetSetPixel);
  }
  image->Release();
  image = nullptr;
  SetGlobalSeaLevel(30);
  CalculateAndSetVertexLight(0, 0, u32(width) - 1, u32(height) - 1);
  TrTerrainLock.Unlock();
  SetEnabled(1);
  u32 bottom = CellCountZ() - 1, right = CellCountX() - 1;
  PublishChanges(0, 0, right, bottom, 0);
  return true;
}

// Re-read virtual extents and obtain the real message server separately for every synchronous rectangle message.
void TrDirtBag::PublishChanges(u32 left, u32 top, u32 right, u32 bottom, u8 changed_only) {
  if (left)
    --left;
  if (top)
    --top;
  right = UintMin(right, CellCountX() - 1);
  bottom = UintMin(bottom, CellCountZ() - 1);
  if (!changed_only) {
    u32 rectangle[4] = {left, top, right, bottom};
    ScMessage message = {kTrMsg_TerrainChanged, (uptr)rectangle, 0, 0};
    TrGetMessageServer()->MessageSend(message);
    return;
  }
  for (u32 x = left; (i32)x <= (i32)right; ++x) {
    for (u32 z = top; (i32)z <= (i32)bottom; ++z) {
      if (ChangedByLastOperation(x, z)) {
        u32 rectangle[4] = {x, z, x, z};
        ScMessage message = {kTrMsg_TerrainChanged, (uptr)rectangle, 0, 0};
        TrGetMessageServer()->MessageSend(message);
      }
    }
  }
}

// Store the sea byte and fill the entire captured water grid while holding the real recursive terrain lock.
// Capture update enablement before unlocking, then publish the full cell rectangle only when that byte was nonzero.
void TrDirtBag::SetGlobalSeaLevel(u8 level) {
  TrTerrainLock.Lock();
  TrByteGrid *grid = water;
  u32 height = grid->height;
  sea = level;
  for (u32 x = 0; x < grid->width; ++x)
    memset(grid->columns[x], level, height);
  u8 enabled = GetEnabled();
  TrTerrainLock.Unlock();
  if (enabled) {
    u32 bottom = CellCountZ() - 1, right = CellCountX() - 1;
    PublishChanges(0, 0, right, bottom, 0);
  }
}

// Average the altitude above global sea level of vertices whose dirt is strictly above their water table.
// Keep the original virtual sample order, wrapped DWORD accumulation, signed division, and byte result across unlocking.
u8 TrDirtBag::GetAverageAltitude() const {
  TrTerrainLock.Lock();
  u32 total = 0, count = 0;
  for (u32 x = 0; x < VertexCountX(); ++x) {
    for (u32 z = 0; z < VertexCountZ(); ++z) {
      u8 dry, wet;
      GetVertexAltitudeDirt(x, z, &dry);
      GetVertexAltitudeWater(x, z, &wet);
      if (dry > wet) {
        ++count;
        total += (u32)dry - GetGlobalSeaLevel();
      }
    }
  }
  u8 result = count ? (u8)((i32)total / (i32)count) : 0;
  TrTerrainLock.Unlock();
  return result;
}

u32 TrDirtBag::GetLayerType() {
  return kTrDirtBagLayerType;
}

static f32 TrShadeFloat(f64 value) {
  volatile f32 rounded = (f32)value;
  return rounded;
}

static u8 TrMirrorHeight(u8 center, u8 neighbor) {
  i32 value = 2 * (i32)center - neighbor;
  return (u8)(value < 0 ? 0 : value > 255 ? 255 : value);
}

// Build the original averaged normal from five cardinal samples, preserving the installed float stores.
void TrDirtBag::GetVertexNormal(f32 step, f32 scale, const u8 *heights, ScPoint3F *normal) const {
  f64 side = step;
  f32 negative_side = -step, positive_z = TrShadeFloat((f64(heights[1]) - heights[0]) * scale),
      positive_x = TrShadeFloat((f64(heights[2]) - heights[0]) * scale),
      negative_z = TrShadeFloat((f64(heights[3]) - heights[0]) * scale);
  f64 negative_x = (f64(heights[4]) - heights[0]) * scale;
  ScPoint3F first = {
      TrShadeFloat(0.0 - positive_x * side), TrShadeFloat(side * side), TrShadeFloat(0.0 - positive_z * side)};
  ScPoint3F second = {TrShadeFloat(negative_side * f64(positive_x) - 0.0), TrShadeFloat(-(negative_side * side)),
      TrShadeFloat(negative_z * side - 0.0)};
  ScPoint3F third = {TrShadeFloat(0.0 - negative_x * negative_side), TrShadeFloat(f64(negative_side) * negative_side),
      TrShadeFloat(0.0 - negative_side * f64(negative_z))};
  f32 fourth_z = TrShadeFloat(negative_side * f64(positive_z) - 0.0);
  normal->x = TrShadeFloat((f64(first.x) + second.x + third.x + negative_x * side - 0.0) * 0.25);
  normal->y = TrShadeFloat((f64(first.y) + second.y + third.y + second.y) * 0.25);
  normal->z = TrShadeFloat((f64(first.z) + second.z + third.z + fourth_z) * 0.25);
}

void TrDirtBag::VecDotProduct(const ScPoint3F &first, const ScPoint3F &second, f32 *out) {
  *out = TrShadeFloat(f64(first.z) * second.z + f64(first.y) * second.y + f64(first.x) * second.x);
}

void TrDirtBag::VecUnit(const ScPoint3F &value, ScPoint3F *out) {
  f64 magnitude = sqrt(f64(value.x) * value.x + f64(value.y) * value.y + f64(value.z) * value.z);
  if (magnitude > 0.0) {
    out->x = TrShadeFloat(value.x / magnitude);
    out->y = TrShadeFloat(value.y / magnitude);
    out->z = TrShadeFloat(value.z / magnitude);
  }
}

// Normalize both nonzero directions, reverse the light vector, then clamp the cosine and call the actual renderer.
void TrDirtBag::GetVertexLightIndex(const ScPoint3F &normal, const ScPoint3F &light, u8 *out) const {
  assert(!isnan(normal.x) && !isnan(normal.y) && !isnan(normal.z) &&
      (normal.x != 0.0f || normal.y != 0.0f || normal.z != 0.0f));
  assert(
      !isnan(light.x) && !isnan(light.y) && !isnan(light.z) && (light.x != 0.0f || light.y != 0.0f || light.z != 0.0f));
  ScPoint3F unit, direction;
  VecUnit(normal, &unit);
  VecUnit(light, &direction);
  direction.x = -direction.x;
  direction.y = -direction.y;
  direction.z = -direction.z;
  f32 cosine;
  VecDotProduct(unit, direction, &cosine);
  if (!(cosine >= 0.0f))
    cosine = 0.0f;
  *out = TrTerrainRenderer->GetLightIndex(cosine);
}

// Execute the private hillshade effects in inclusive X-major order with the original live extent and scale calls.
// Mirror and clamp missing cardinal neighbors, then write only the reached shade vertices in their real columns.
void TrDirtBag::CalculateAndSetVertexLight(u32 left, u32 top, u32 right, u32 bottom) {
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      u8 heights[5];
      GetVertAltForLightCalc(x, z, heights);
      ScPoint3F normal;
      GetVertexNormal(16.0f, AltitudeScale(), heights, &normal);
      const ScPoint3F first_light = {1.0f, -1.0f, 0.0f}, second_light = {-1.0f, -1.0f, 0.0f};
      u8 first, second;
      GetVertexLightIndex(normal, first_light, &first);
      GetVertexLightIndex(normal, second_light, &second);
      shade->columns[x][z] = first >= second ? first : second;
    }
  }
}

// Read center and cardinal ground heights in Windows order, extrapolating missing edge samples with byte saturation.
// Windows inlines it and omits the Linux unused bounds probe.
void TrDirtBag::GetVertAltForLightCalc(u32 x, u32 z, u8 *heights) const {
  heights[0] = ground->columns[x][z];
  if (z && z < VertexCountZ() - 1) {
    heights[1] = ground->columns[x][z + 1];
    heights[3] = ground->columns[x][z - 1];
  } else if (!z) {
    heights[1] = ground->columns[x][1];
    heights[3] = TrMirrorHeight(heights[0], heights[1]);
  } else if (z == VertexCountZ() - 1) {
    heights[3] = ground->columns[x][z - 1];
    heights[1] = TrMirrorHeight(heights[0], heights[3]);
  }
  if (x && x < VertexCountX() - 1) {
    heights[2] = ground->columns[x + 1][z];
    heights[4] = ground->columns[x - 1][z];
  } else if (!x) {
    heights[2] = ground->columns[1][z];
    heights[4] = TrMirrorHeight(heights[0], heights[2]);
  } else if (x == VertexCountX() - 1) {
    heights[4] = ground->columns[x - 1][z];
    heights[2] = TrMirrorHeight(heights[0], heights[4]);
  }
}

// Read the requested surface altitude before allocating its dry, water or relief sprite.
// Publish before the live AddRef callback and preserve the selected construction method's byte result.
u8 TrDirtBag::GetDirtClod(
    u8 palette_kind, u32 cell_x, u32 cell_z, bool dry_only, SpSprite **out, u8 *altitude, u8 overlay) const {
  u32 count = dry_only ? 0 : CellWaterVertCount(cell_x, cell_z);
  if (!count)
    return GetDirtClodLand(palette_kind, cell_x, cell_z, out, altitude, overlay);
  else if (count == 4)
    return GetDirtClodWater(palette_kind, cell_x, cell_z, out, altitude, overlay);
  else
    return GetDirtClodShore(palette_kind, cell_x, cell_z, out, altitude, overlay);
}

u8 TrDirtBag::GetDirtClodLand(u8 palette_kind, u32 cell_x, u32 cell_z, SpSprite **out, u8 *altitude, u8 overlay) const {
  *altitude = ground->columns[cell_x][cell_z];
  return TrTerrainRenderer->GetDirtClodLand(cell_x, cell_z, palette_kind, overlay, out);
}

// Publish ground altitude and then any higher water altitude before creating and retaining the mixed-surface tile.
u8 TrDirtBag::GetDirtClodShore(
    u8 palette_kind, u32 cell_x, u32 cell_z, SpSprite **out, u8 *altitude, u8 overlay) const {
  u8 height = ground->columns[cell_x][cell_z];
  *altitude = height;
  u8 water_height = water->columns[cell_x][cell_z];
  if (water_height > height)
    *altitude = water_height;
  return TrTerrainRenderer->GetDirtClodShore(cell_x, cell_z, palette_kind, overlay, out);
}

// Publish the water altitude before constructing the water tile and retaining its already published identity.
u8 TrDirtBag::GetDirtClodWater(
    u8 palette_kind, u32 cell_x, u32 cell_z, SpSprite **out, u8 *altitude, u8 overlay) const {
  *altitude = water->columns[cell_x][cell_z];
  return TrTerrainRenderer->GetDirtClodWater(cell_x, cell_z, palette_kind, overlay, out);
}

// Read ground and optional higher water altitude before constructing the single-row tile and acquiring its reference.
u8 TrDirtBag::GetDirtClodCursor(
    u8 palette_kind, u32 cell_x, u32 cell_z, u16 row, bool dry_only, SpSprite **out, u8 *altitude) const {
  u8 height = ground->columns[cell_x][cell_z];
  *altitude = height;
  if (!dry_only) {
    u8 water_height = water->columns[cell_x][cell_z];
    if (water_height > height)
      *altitude = water_height;
  }
  return TrTerrainRenderer->GetDirtClodCursor(cell_x, cell_z, palette_kind, row, dry_only, out);
}

u8 TrDirtBag::GetDirtClodSingle(
    u8 palette_kind, u16 row, u32 cell_x, u32 cell_z, bool dry_only, SpSprite **out, u8 *altitude) const {
  return GetDirtClodCursor(palette_kind, cell_x, cell_z, row, dry_only, out, altitude);
}

// Read the edge altitude from ground and optional higher water before constructing its independent side/dry flags.
// Publish before acquiring its first object and module reference, then return the original byte value one.
u8 TrDirtBag::GetDirtClodEdge(
    u32 cell_x, u32 cell_z, bool reverse_side, bool dry_only, SpSprite **out, u8 *altitude) const {
  u8 height = ground->columns[cell_x][cell_z];
  *altitude = height;
  if (!dry_only) {
    u8 water_height = water->columns[cell_x][cell_z];
    if (water_height > height)
      *altitude = water_height;
  }
  return TrTerrainRenderer->GetDirtClodEdge(cell_x, cell_z, reverse_side, dry_only, out);
}

static ScString TrReadHeightText(u32 instance) {
  TrTextResource resource(instance, kRsCityServiceTextGroup);
  return resource;
}

static void TrFormatHeightValue(ScString *out, const char *format, const char *value) {
  out->Sprintf(format, value);
}

u8 TrDirtBag::GetInfoStrings(u32 cell_x, u32 cell_z, ScStringView *label, ScStringView *value, u32 flags) {
  if (flags)
    return 0;
  ScLanguageUtility *utility = TrGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  bool wet = IsWater(cell_x, cell_z);
  ScString text = TrReadHeightText(wet ? 383 : 2), number, formatted;
  label->Copy(text);
  f32 distance;
  if (wet) {
    u8 ground_height, water_height;
    GetVertexAltitudeDirt(cell_x, cell_z, &ground_height);
    GetVertexAltitudeWater(cell_x, cell_z, &water_height);
    f64 depth = f64(city->CellSizeInWorldUnitsY()) * (i32(water_height) - ground_height);
    distance = depth < 0.0 ? 0.0f : f32(depth);
  } else {
    u8 level = GetGlobalSeaLevel(), height;
    GetVertexAltitudeDirt(cell_x, cell_z, &height);
    distance = f32(f64(city->CellSizeInWorldUnitsY()) * (i32(height) - level));
  }
  text = TrReadHeightText(kRsCityServiceMetersFormatText);
  i32 displayed = i32(city->WorldToMeter(distance));
  utility->MakeNumberString(i64(displayed), &number);
  TrFormatHeightValue(&formatted, text.Data(), number.Data());
  value->Copy(formatted);
  return 1;
}

// A reported location succeeds even when formatting declines; the AddText reply is deliberately ignored.
u8 TrDirtBag::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  CtCoord location = {INT32_MAX, INT32_MAX, INT32_MAX};
  ScString label, value;
  if (!query || !query->GetLocation(&location))
    return 0;
  if (GetInfoStrings(
          u32(location.x >> kCtMicroCoordinateShift), u32(location.z >> kCtMicroCoordinateShift), &label, &value, 0))
    query->AddText(&label, &value, 32, 0);
  return 1;
}

u8 TrDirtBag::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 TrDirtBag::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

// Resolve only the interface required by the requested query command and retire it after the actual helper result.
// Raster publication releases its temporary source even after factory failure; height queries use the live locale/text path.
bool TrDirtBag::DoQueryInfo(ScMessage *message, ScUnknown *context) {
  if (message->type != kWnMsg_QueryOpened)
    return false;
  u32 request = u32(message->data1), iid;
  switch (request) {
  case kUiQueryTagDialog:
    iid = IID_UiQueryWindow;
    break;
  case kUiQueryTagDebug:
  case kUiQueryTagStandard:
    iid = IID_UiQuerySection;
    break;
  case kUiQueryTagMaps:
    iid = IID_UiRasterQuery;
    break;
  default:
    return false;
  }
  void *object = nullptr;
  if (!context || !context->QueryInterface(iid, &object))
    return false;
  bool result;
  if (request == kUiQueryTagMaps) {
    UiRasterQuery *query = static_cast<UiRasterQuery *>(object);
    void *source = nullptr;
    result = CreateMapSprite(kUiTerrainMapRasterId, IID_GxRasterSource, &source);
    if (result)
      result = query->AddRaster(static_cast<GxRasterSource *>(source)) != 0;
    if (source) {
      static_cast<GxRasterSource *>(source)->Release();
      source = nullptr;
    }
    query->Release();
  } else {
    if (request == kUiQueryTagStandard)
      result = DoStandardQueryViewInfo(message, static_cast<UiQuery *>(object)) != 0;
    else if (request == kUiQueryTagDebug)
      result = DoDebugQueryViewInfo(message, static_cast<UiQuery *>(object)) != 0;
    else
      result = DoQueryViews(message, static_cast<UiQueryWindow *>(object)) != 0;
    static_cast<ScUnknown *>(object)->Release();
  }
  return result;
}

// Terrain message handling and complete private cheat effects; discarded private return declarations remain unresolved.
#include "common/framework.h"

// Clear the complete city footprint through the actual zone layer, preserving Z-before-X dimension lookups.
// This native phase uses the original separate zone argument, bounds, genuine cost output and free-operation flag.
void TrDirtBag::ClearCityZones() {
  GeBounds bounds = {{0, 0, 0}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  u32 bottom = city->CellCountZ() - 1, width = city->CellCountX();
  bounds.maximum.altitude = 0;
  bounds.maximum.x = (width - 1) << kCtMicroCoordinateShift;
  bounds.maximum.z = bottom << kCtMicroCoordinateShift;
  i32 cost;
  TrGetCity()->ZoneLayer()->PlaceZone(0, bounds, &cost, true);
}

// Restore the complete effects of private EF30: remove the twelve-byte command prefix and trim literal spaces only.
// A usable filename clears occupants, zones and salt flags under the graphics lock before invoking real bitmap import.
void TrDirtBag::CheatLoadTerrain(ScStringView *command) {
  if (!command)
    return;
  ScString text(*command), filename;
  u32 length = text.Strlen();
  if (length > 12)
    filename.FromChar(text.Data() + 12, length - 12);
  u32 leading = 0;
  length = filename.Strlen();
  while (leading < length && filename.Data()[leading] == ' ')
    ++leading;
  if (leading)
    filename.Erase(0, leading);
  length = filename.Strlen();
  u32 end = length;
  while (end && filename.Data()[end - 1] == ' ')
    --end;
  if (end != length)
    filename.Erase(end, length - end);
  if (filename.Strlen() > 1) {
    TrGetCity()->LockGraphics();
    TrGetCity()->RemoveAllOccupants();
    ClearCityZones();
    ClearSaltFlags();
    GenerateAltFromBMP(&filename);
    TrGetCity()->UnlockGraphics();
  }
}

static u8 TrShiftedHeight(u8 height, i32 delta) {
  i32 shifted = i32(u32(height) + u32(delta));
  return shifted < 2 ? 2 : shifted > 253 ? 253 : u8(shifted);
}

// Clear city content before disabling terrain notifications; mutate dry then water per vertex while recursively locked.
void TrDirtBag::CheatTerrainShift(i32 delta) {
  TrGetCity()->LockGraphics();
  SetEnabled(0);
  TrGetCity()->RemoveAllOccupants();
  ClearCityZones();
  TrTerrainLock.Lock();
  for (u32 x = 0; x < VertexCountX(); ++x) {
    for (u32 z = 0; z < VertexCountZ(); ++z) {
      u8 height;
      GetVertexAltitudeDirt(x, z, &height);
      SetVertexAltitude(x, z, TrShiftedHeight(height, delta));
      GetVertexAltitudeWater(x, z, &height);
      SetWaterTable(x, z, TrShiftedHeight(height, delta), false);
    }
  }
  sea = TrShiftedHeight(sea, delta);
  TrTerrainLock.Unlock();
  SetEnabled(1);
  u32 bottom = CellCountZ() - 1, right = CellCountX() - 1;
  PublishChanges(0, 0, right, bottom, 0);
  TrGetCity()->UnlockGraphics();
}

// Handle complete terrain regeneration, terrain cheats and initialization-cache publication through live game
// receivers.
bool TrDirtBag::DoMessage(ScMessage *message) {
  if (message->type == kTrMsg_RegenerateTerrain) {
    TrGetCityWindow()->SetActiveCursor(14);
    LockUpdates(true);
    u32 controls = u32(message->data1), flags = u32(message->data2);
    void *object = nullptr;
    if (TrGetCom()->GetClassObject(kTrGeneratorClassId, IID_TrGenerator, &object)) {
      TrGeneratorInterface *generator = static_cast<TrGeneratorInterface *>(object);
      TrGetCity()->RemoveAllOccupants();
      u32 height = CellCountZ(), width = CellCountX();
      generator->Init(width, height);
      generator->SetDifficulty(TrGetCity()->GetDifficulty());
      generator->GenerateRandom(-1, u8(controls), u8(controls >> 8), u8(controls >> 16), u8(flags));
      ClearSaltFlags();
      generator->ReflectMap(this);
      generator->Release();
      object = nullptr;
      ScMessage generated = {kTrMsg_TerrainRegenerated, 0, 0, 0};
      TrGetMessageServer()->MessagePost(generated, false);
    }
    LockUpdates(false);
    u32 bottom = VertexCountZ() - 1, right = VertexCountX() - 1;
    CalculateAndSetVertexLight(0, 0, right, bottom);
    bottom = CellCountZ() - 1;
    right = CellCountX() - 1;
    PublishChanges(0, 0, right, bottom, 0);
    TrGetCityWindow()->SetActiveCursor(kSpCenterCursorId);
  } else if (message->type == kWnMsg_CheatCommand) {
    switch (u32(message->data1)) {
    case kTrLoadTerrainCheatId:
      CheatLoadTerrain(reinterpret_cast<ScStringView *>(message->data2));
      return true;
    case kTrTerrainOneDownCheatId:
      CheatTerrainShift(-1);
      return true;
    case kTrTerrainTenUpCheatId:
      CheatTerrainShift(10);
      return true;
    case kTrTerrainOneUpCheatId:
      CheatTerrainShift(1);
      return true;
    case kTrSaltOnCheatId:
      RecalcSaltWater();
      return true;
    case kTrTerrainTenDownCheatId:
      CheatTerrainShift(-10);
      return true;
    case kTrSaltOffCheatId:
      ClearSaltFlags();
      return true;
    default:
      return false;
    }
  }
  if (message->type == kWnMsg_EnterTerrainEditor || message->type == kTrMsg_RegenerateTerrain)
    PublishInitialLayers();
  return true;
}

static TrContour TrPlacementContour;

static u32 TrPlacementTile(u32 value) {
  return u32(i32(value) >> kCtMicroCoordinateShift);
}
