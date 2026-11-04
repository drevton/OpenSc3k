#include "economy/ec_module.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <list>
#include <new>
#include <vector>

#include <SDL3/SDL_timer.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/maps.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_civic.h"
#include "economy/ec_pollution.h"
#include "economy/ec_rci_module.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/stream.h"
#include "window/api.h"

static constexpr u32 kEcRecordFrameMarker = 0xdeadbeef;

// Retire the empty polymorphic archive base after its derived frame has released the owned record scope.
EcArchive::~EcArchive() = default;

EcArchiveFrame::EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : version(0), text_flag(0), extended_flags(false), marker(0), record(nullptr), good(0), game_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    game_version[component] = city->GetVersionComponentFromSaveFile(component);
  record = new EcStreamRef(segment, key);
  good = record->IsOpen();
  if (!good) {
    delete record;
    record = nullptr;
    return;
  }
  good = record->GetStream()->ReadU16(&version);
  if (good) {
    u8 flags = 0;
    good = record->GetStream()->ReadU8(&flags);
    text_flag = flags & 1;
    extended_flags = (flags & 2) != 0;
    if (extended_flags && good)
      good = record->GetStream()->ReadU8(&flags);
  }
  if (good)
    record->GetStream()->ReadU32(&marker);
  if (good)
    good = marker == kEcRecordFrameMarker;
  if (marker != kEcRecordFrameMarker) {
    text_flag = 0;
    version = 0;
    marker = kEcRecordFrameMarker;
    delete record;
    record = nullptr;
    record = new EcStreamRef(segment, key);
    good = record->IsOpen();
    if (!good) {
      delete record;
      record = nullptr;
    }
  }
}

// Keep raw byte results from the first two writes; the marker write's return is deliberately ignored.
EcArchiveFrame::EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 text_flag)
    : version(version), text_flag(text_flag), extended_flags(false), marker(0), record(nullptr), good(0),
      game_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    game_version[component] = city->GetVersionComponent(component);
  record = new EcStreamRef(segment, key, 0);
  good = record->IsOpen();
  if (good) {
    good = record->GetStream()->WriteU16(this->version);
    if (good)
      good = record->GetStream()->WriteU16(static_cast<u16>(this->text_flag ? 3 : 2));
    if (good)
      record->GetStream()->WriteU32(kEcRecordFrameMarker);
  }
  if (!good) {
    delete record;
    record = nullptr;
  }
}

EcArchiveFrame::~EcArchiveFrame() {
  ScDelete(&record);
}

// Borrow17D31's live stream pointer, returning null only when no stream scope is retained by this frame.
RsStream *EcArchiveFrame::GetStream() {
  return record ? record->GetStream() : nullptr;
}

void EcArchiveFrame::MarkFailed() {
  if (record)
    record->failed = 1;
  good = 0;
}

EcByteMap::EcByteMap() : references(0), initialized(false), cells(nullptr) {
}

// Retire an initialized map through its actual cleanup body before ordinary storage deletion. An uninitialized receiver
// leaves the stored pointer alone, matching the installed destructor.
EcByteMap::~EcByteMap() {
  if (initialized)
    EcByteMap::Shutdown();
}

// Publish the same primary identity for Unknown, the cell-map base
// and the unsigned-byte specialization. Retain only after publishing; an unsupported IID clears the output.
bool EcByteMap::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScCellMapBase && iid != IID_ScCellMapU8)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScCellMap<u8> *>(this);
  AddRef();
  return true;
}

// Increment the map count before retaining its own module, then reload the callback-visible count.
u32 EcByteMap::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a positive count. Zero destroys through the real virtual destructor.
u32 EcByteMap::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 count = references;
  if (!count)
    delete this;
  return count;
}

u32 EcByteMap::CellCountX() const {
  return cells->column_count;
}

u32 EcByteMap::CellCountZ() const {
  return cells->row_count;
}

void EcByteMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x][z];
}

bool EcByteMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool EcByteMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool EcByteMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool EcByteMap::SerialRead(RsStream *record) {
  return cells->SerialRead(record);
}

bool EcByteMap::SerialWrite(RsStream *record) const {
  return cells->SerialWrite(record);
}

// Shut down an active receiver through its virtual method before sampling city height and then width.
// Allocate the original full-resolution column map and publish readiness only after construction returns.
bool EcByteMap::Init(CtCity *city, const u8 *fill) {
  if (initialized)
    Shutdown();
  u32 rows = city->CellCountZ(), columns = city->CellCountX();
  cells = new CtByteCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Retire active column storage and clear its pointer after destruction. Readiness clears even without storage.
bool EcByteMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void EcByteMap::GetValue(u32 x, u32 z, u8 *value) const {
  *value = cells->columns[x][z];
}

void EcByteMap::SetValue(u32 x, u32 z, const u8 *value) {
  cells->columns[x][z] = *value;
}

void EcByteMap::SetAllCells(const u8 *value) {
  cells->SetAllCells(value);
}

// Fill the inclusive rectangle in ascending X order, retaining a live fill byte and the original row span.
void EcByteMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  CtByteCells *grid = cells;
  for (u32 x = x0; x <= x1; ++x)
    memset(grid->columns[x] + z0, *value, z1 - z0 + 1);
}

EcCommandOwner::EcCommandOwner() : references(0) {
}

EcCommandOwner::~EcCommandOwner() {
}

// Consult the virtual removal result and destroy the concrete command when its lifetime count reaches zero.
u32 EcCommandOwner::Release() {
  u32 value = RemoveRef();
  if (!value)
    delete this;
  return value;
}

u32 EcCommandOwner::AddRef() {
  EcGetRciDirector()->AddRef();
  return ++references;
}

// Release a nonzero module hold before decrementing the live command count.
u32 EcCommandOwner::RemoveRef() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 EcCommandOwner::RefCount() {
  return references;
}

EcRciCommand::EcRciCommand() {
}

// Drain retained inputs and outputs before their queue storage is retired.
EcRciCommand::~EcRciCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool EcRciCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcRciCommand::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcRciCommand::Release() {
  return EcCommandOwner::Release();
}

// Retain the input argument before appending it to the pending input sequence.
bool EcRciCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Transfer the oldest input to the caller without releasing it; an empty queue leaves the output untouched.
bool EcRciCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 index = 0; index < inputs.size; ++index)
    inputs.data[index] = inputs.data[index + 1];
  return true;
}

u32 EcRciCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate the original input type and flag pairs in their live insertion order.
bool EcRciCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 index = 0; index < input_parameters.size; ++index) {
    UiCommandParameter parameter = input_parameters.data[index];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain the output argument before appending it to the pending result sequence.
bool EcRciCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

bool EcRciCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 index = 0; index < outputs.size; ++index)
    outputs.data[index] = outputs.data[index + 1];
  return true;
}

u32 EcRciCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate the output descriptor pairs in their live insertion order.
bool EcRciCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 index = 0; index < output_parameters.size; ++index) {
    UiCommandParameter parameter = output_parameters.data[index];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool EcRciCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool EcRciCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

EcCommandFactory::EcCommandFactory() : references(0) {
}

// Cleanup is reached through the deleting destructor.
EcCommandFactory::~EcCommandFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

// Publish the common command-factory identity for either supported IID and retain it after publication.
bool EcCommandFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increase the factory's count before retaining SIMRCI and then return the callback-visible count.
u32 EcCommandFactory::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Decrement before releasing the module hold and destroy the concrete factory when the resulting count is zero.
u32 EcCommandFactory::Release() {
  --references;
  EcGetRciDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Ask child factories in their live order and fall back to the real derived command dispatcher.
bool EcCommandFactory::GetCommand(u32 class_id, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 index = 0; index < children.size; ++index, ++cursor)
    if ((*cursor)->GetCommand(class_id, iid, out))
      return true;
  return CreateCommand(class_id, iid, out);
}

// Retain a non-null child factory before appending it to the delegation sequence.
bool EcCommandFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

EcCursor::EcCursor()
    : phase(1), mode(1), refill(1), min_x(0), min_z(0), max_x(0), max_z(0), count(0), used(0), current(0), stride(1) {
}

EcCursor::~EcCursor() = default;

// Advance arithmetic or packed-list traversal, leaving outputs untouched on exhaustion.
// Preserve the original byte return and sequential output stores, including callers that alias x and z.
u8 EcCursor::Next(i32 *x, i32 *z) {
  if (phase == 2)
    return 0;
  if (mode) {
    ++used;
    if (used <= count) {
      current = (current + stride) % count;
      *x = min_x + current % (max_x - min_x + 1);
      *z = min_z + current / (max_x - min_x + 1);
      if (used == count)
        phase = 2;
      return 1;
    }
  } else {
    i32 index = used, length = static_cast<i32>(positions.size());
    if (index < length) {
      used = index + 1;
      u16 packed = positions[index];
      *x = packed >> 8;
      *z = packed & 255;
      if (used == length)
        phase = 2;
      return 1;
    }
  }
  phase = 2;
  return 0;
}

void EcCursor::PrepareTraversal(u8 shuffle) {
  static const i16 kStrides[] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79,
      83, 89, 97, 101, 103, 107, 109, 113, 179, 211, 257, 307, 373, 479, 577, 641, 719, 857, 977, 1063, 1237, 1471,
      1663, 1993, 2221, 2539, 2909, 3259, 3733, 4507, 5281, 5861, 6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431,
      19801, 21569, 23633, 26003, 27529, 29683, 32051, 32611};
  phase = 1;
  used = 0;
  count = (max_x - min_x + 1) * (max_z - min_z + 1);
  if (!mode) {
    if (positions.size() != static_cast<u32>(count)) {
      positions.resize(static_cast<u32>(count), 0);
      refill = 1;
    }
    if (refill) {
      u32 index = 0;
      for (i32 x = min_x; x <= max_x; ++x)
        for (i32 z = min_z; z <= max_z; ++z)
          positions[index++] = static_cast<u16>((static_cast<u8>(x) << 8) | static_cast<u8>(z));
    }
    if (shuffle)
      for (u32 index = 1; index < positions.size(); ++index) {
        u32 selected = static_cast<u32>(ScRand()) % (index + 1);
        u16 value = positions[index];
        positions[index] = positions[selected];
        positions[selected] = value;
      }
  } else {
    u32 index;
    if (count < 29)
      index = 0;
    else if (count < 71)
      index = 10;
    else if (count < 113)
      index = 20;
    else if (count < 857)
      index = 30;
    else if (count < 3259)
      index = 40;
    else if (count < 12227)
      index = 50;
    else {
      index = 60;
      if (count >= kScPermutationMaximumCount)
        count = kScPermutationMaximumCount;
    }
    while (kStrides[index] < count / 4) {
      ++index;
      assert(index < sizeof(kStrides) / sizeof(*kStrides));
    }
    while (count % kStrides[index] == 0) {
      ++index;
      assert(index < sizeof(kStrides) / sizeof(*kStrides));
    }
    stride = kStrides[index];
    current = ScRand() % count;
  }
}

// Keep the mode/count temporary shared and preserve failed output writes without changing logical vector size.
u8 EcCursorRecord::SerialRead(CtCity *, RsStream *record) {
  u32 stored = 1;
  u8 result = record->ReadI8(&phase);
  if (result)
    result = record->ReadU32(&stored);
  if (!result)
    return result;
  mode = static_cast<i32>(stored);
  u8 enabled;
  result = record->ReadU8(&enabled);
  if (!result)
    return result;
  refill = enabled != 0;
  result = record->ReadI32(&min_x);
  result = result && record->ReadI32(&max_x) && record->ReadI32(&min_z) && record->ReadI32(&max_z) &&
      record->ReadI32(&count) && record->ReadU32(&stored);
  if (!result)
    return result;
  assert(stored <= positions.capacity());
  while (result) {
    u32 remaining = stored--;
    if (!remaining)
      break;
    result = record->ReadU16(positions.data() + stored);
  }
  result = result && record->ReadI32(&used) && record->ReadI32(&current) && record->ReadI32(&stride);
  return result;
}

u8 EcCursorRecord::SerialWrite(CtCity *, RsStream *record) {
  u8 result = record->WriteI8(phase);
  result = result && record->WriteU32(static_cast<u32>(mode)) && record->WriteU8(refill != 0) &&
      record->WriteI32(min_x) && record->WriteI32(max_x) && record->WriteI32(min_z) && record->WriteI32(max_z) &&
      record->WriteI32(count);
  u32 length = static_cast<u32>(positions.size());
  if (result)
    result = record->WriteU32(length);
  while (result) {
    u32 remaining = length--;
    if (!remaining)
      break;
    result = record->WriteU16(positions[length]);
  }
  result = result && record->WriteI32(used) && record->WriteI32(current) && record->WriteI32(stride);
  return result;
}

EcDate::EcDate() : references(0) {
}

EcDate::EcDate(u32 day, u32 year) : CtDate(day, year), references(0) {
}

EcDate::EcDate(const EcDate &source) : CtDate(static_cast<const CtDateInterface &>(source)), references(0) {
}

EcDate::~EcDate() {
}

// Select Unknown or the real date interface without clearing failed outputs. Successful queries retain the adjusted view.
bool EcDate::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtDateInterface);
  HANDLE_QUERY_INTERFACE(IID_CtDateInterface, CtDateInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcDate::AddRef() {
  return ++references;
}

// Decrement counts above one. Counts zero and one both destroy the wrapper without publishing a zero counter first.
u32 EcDate::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

EcDate::EcDate(const CtDate &source) : CtDate(source), references(0) {
}

void EcDate::Set(u32 day, u32 year) {
  CtDate::Set(day, year);
}

void EcDate::Set(u32 month, u32 day, u32 year) {
  CtDate::Set(month, day, year);
}

void EcDate::Set(const CtTime &time) {
  CtDate::Set(time);
}

void EcDate::Set(u32 value) {
  CtDate::Set(value);
}

void EcDate::Set(const char *month, u32 day, u32 year) {
  CtDate::Set(month, day, year);
}

void EcDate::Copy(const CtDateInterface &source) {
  Set(source.DayNumber());
}

void EcDate::Clone(CtDateInterface **out) {
  *out = static_cast<CtDateInterface *>(new EcDate(*this));
  (*out)->AddRef();
}

i32 EcDate::Between(const CtDateInterface &first, const CtDateInterface &second) const {
  CtDate last(second), firstValue(first);
  return CtDate::Between(firstValue, last);
}

i32 EcDate::CompareTo(const CtDateInterface &other) const {
  return CtDate::CompareTo(CtDate(other));
}

u32 EcDate::WeekDay() const {
  return CtDate::DayOfWeek();
}

u32 EcDate::Year() const {
  return CtDate::Year();
}

u32 EcDate::DayOfYear() const {
  return CtDate::DayOfYear();
}

u32 EcDate::DayOfMonth() const {
  return CtDate::DayOfMonth();
}

u32 EcDate::FirstDayOfMonth() const {
  return CtDate::FirstDayOfMonth();
}

u32 EcDate::FirstDayOfMonth(u32 month) const {
  return CtDate::FirstDayOfMonth(month);
}

u32 EcDate::Hash() const {
  return CtDate::Hash();
}

i32 EcDate::IsValid() const {
  return CtDate::IsValid();
}

i32 EcDate::Leap() const {
  return CtDate::Leap();
}

// Choose the larger value after the source callback, then copy through the output interface before retiring the temporary.
void EcDate::MaxDate(const CtDateInterface &other, CtDateInterface *out) const {
  CtDate value(other);
  EcDate selected(CtDate::Maximum(value));
  out->Copy(selected);
}

// Choose the smaller value after the source callback, then copy through the output interface before retiring the temporary.
void EcDate::MinDate(const CtDateInterface &other, CtDateInterface *out) const {
  CtDate value(other);
  EcDate selected(CtDate::Minimum(value));
  out->Copy(selected);
}

u32 EcDate::Month() const {
  return CtDate::Month();
}

void EcDate::Previous(u32 weekday, CtDateInterface *out) const {
  EcDate selected(CtDate::OnOrBefore(weekday));
  out->Copy(selected);
}

u32 EcDate::YearLastTwoDigits() const {
  return CtDate::YearLastTwoDigits();
}

u32 EcDate::DayNumber() const {
  return CtDate::DayNumber();
}

// Read the other date virtually before testing whether this live unsigned value precedes it.
i32 EcDate::operator<(const CtDateInterface &other) const {
  return CtDate::operator<(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value precedes or equals it.
i32 EcDate::operator<=(const CtDateInterface &other) const {
  return CtDate::operator<=(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value follows it.
i32 EcDate::operator>(const CtDateInterface &other) const {
  return CtDate::operator>(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value follows or equals it.
i32 EcDate::operator>=(const CtDateInterface &other) const {
  return CtDate::operator>=(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value equals it.
i32 EcDate::operator==(const CtDateInterface &other) const {
  return CtDate::operator==(CtDate(other));
}

// Read the other date virtually before testing whether this live unsigned value differs from it.
i32 EcDate::operator!=(const CtDateInterface &other) const {
  return CtDate::operator!=(CtDate(other));
}

// Read the other date virtually, then subtract its date word with unsigned wraparound.
u32 EcDate::operator-(const CtDateInterface &other) const {
  return CtDate::operator-(CtDate(other));
}

// Dispatch through this same virtual day-subtraction slot, preserving the installed forwarding behavior.
u32 EcDate::operator-(i32 days) const {
  return static_cast<const CtDateInterface *>(this)->operator-(days);
}

// Dispatch through this same virtual date-addition slot, preserving the installed forwarding behavior.
u32 EcDate::operator+(const CtDateInterface &other) const {
  return static_cast<const CtDateInterface *>(this)->operator+(other);
}

// Dispatch through this same virtual day-addition slot, preserving the installed forwarding behavior.
u32 EcDate::operator+(i32 days) const {
  return static_cast<const CtDateInterface *>(this)->operator+(days);
}

// Increment the stored day with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *EcDate::operator++() {
  CtDate::operator++();
  return this;
}

// Exclude the original postfix result, whose stack date has already been destroyed before it is returned.
// A live replacement object would invent ownership and a lifetime absent from the installed implementation.
const CtDateInterface *EcDate::operator++(int) {
  assert(false && "EcDate postfix increment returns an expired stack date");
  abort();
}

// Decrement the stored day with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *EcDate::operator--() {
  CtDate::operator--();
  return this;
}

// Exclude the original postfix result, whose stack date has already been destroyed before it is returned.
// A live replacement object would invent ownership and a lifetime absent from the installed implementation.
const CtDateInterface *EcDate::operator--(int) {
  assert(false && "EcDate postfix decrement returns an expired stack date");
  abort();
}

// Add the signed displacement with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *EcDate::operator+=(i32 days) {
  CtDate::operator+=(days);
  return this;
}

// Subtract the signed displacement with unsigned wraparound and return the borrowed const interface.
const CtDateInterface *EcDate::operator-=(i32 days) {
  CtDate::operator-=(days);
  return this;
}

// Format through the direct value base, copy through the output string interface, then retire the temporary.
void EcDate::DateString(ScStringView *out, u32 format) const {
  ScString text;
  CtDate::DateString(&text, format);
  out->Copy(text);
}

i32 EcDate::DayWithinMonth(u32 month, u32 day, u32 year) {
  return CtCalendarValid(month, day, year);
}

u32 EcDate::DaysInYear(u32 year) {
  return CtDate::DaysInYear(year);
}

u32 EcDate::Jday(u32 month, u32 day, u32 year) {
  return CtCalendarEncode(month, day, year);
}

i32 EcDate::LeapYear(u32 year) {
  return CtCalendarLeapYear(year);
}

static void EcUpperIniName(ScString *name) {
  name->MakeUpper();
}

// Remove only ASCII spaces from the requested edges. Bound the scan to exclude the source's all-space overread.
static void EcTrimIniName(ScString *name, bool leading, bool trailing) {
  if (leading) {
    u32 count = 0;
    while (count < name->Strlen() && name->begin[count] == ' ')
      ++count;
    if (count)
      name->Erase(0, count);
  }
  if (trailing) {
    u32 length = name->Strlen(), end = length;
    while (end && name->begin[end - 1] == ' ')
      --end;
    if (end != length)
      name->Erase(end, length - end);
  }
}

// Compare the original tree's complete String ranges lexicographically, then order an equal prefix by length.
bool EcIniNameLess::operator()(const ScString &first, const ScString &second) const {
  u32 first_length = static_cast<u32>(first.end - first.begin),
      second_length = static_cast<u32>(second.end - second.begin);
  u32 shared = first_length < second_length ? first_length : second_length;
  int order = shared ? memcmp(first.begin, second.begin, shared) : 0;
  return order < 0 || (!order && first_length < second_length);
}

EcIniFile::EcIniFile() : keep_open(1), indexed(0) {
}

EcIniFile::~EcIniFile() {
  if (file.IsOpen())
    file.Close();
}

// Borrow the reader's selected filename. This does not open the file or add a reference.
const ScString *EcIniFile::GetPath() const {
  return &name;
}

// Invalidate an already loaded section index through virtual Close. Then replace the complete selected path.
void EcIniFile::SelectFile(const ScString *input) {
  if (indexed)
    Close();
  name = *input;
}

// Release the open handle and each section record. Retain the selected filename and keep-open preference.
bool EcIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  sections.clear();
  indexed = 0;
  return 1;
}

// Open the concrete file with the source access/share modes. Existing locked files retain the bounded retry.
bool EcIniFile::Open(bool write) {
  time_t start = time(nullptr);
  while (!file.Open(write ? 3 : 1, write ? 3 : 2, write ? 0 : 1)) {
    if (RsFile::FileExists(name)) {
      time_t now = time(nullptr);
      SDL_Delay(250);
      if (now - start < 3)
        continue;
    }
    return 0;
  }
  return 1;
}

// A final unterminated line succeeds; an exhausted file clears the output and returns zero.
bool EcIniFile::GetFileLine(ScString *out) {
  ScString line;
  u32 position = file.Position(), remaining = file.Size() - position;
  u8 read = 0;
  char block[44] = {};
  while (remaining) {
    u32 count = remaining > 40 ? 40 : remaining;
    if (!file.Read(block, count))
      break;
    read = 1;
    remaining -= count;
    u32 end = 0;
    while (end < count && block[end] != '\r' && block[end] != '\n')
      ++end;
    bool newline = end != count;
    block[end] = 0;
    if (newline) {
      file.SeekAbsolute(static_cast<i32>(position + end));
      char byte;
      while (file.Read(&byte, 1))
        if (byte != '\r' && byte != '\n') {
          file.SeekRelative(-1);
          break;
        }
    }
    line.Append(block, static_cast<u32>(strlen(block)));
    if (newline || !remaining)
      break;
    position = file.Position();
  }
  *out = line;
  return read;
}

// Index physical line-start positions by uppercase section name. The first duplicate retains its original position.
bool EcIniFile::LoadSectionNames() {
  sections.clear();
  if (!file.IsOpen()) {
    file.SetName(name);
    if (!Open(0))
      return 0;
  }
  file.SeekBegin();
  u32 position = file.Position();
  ScString line;
  while (GetFileLine(&line)) {
    EcTrimIniName(&line, true, false);
    if (line.Strlen() && line.begin[0] == '[') {
      u32 end = 1;
      while (end < line.Strlen() && line.begin[end] != ']')
        ++end;
      if (end >= 2 && end < line.Strlen()) {
        ScString key(line.begin + 1, end - 1);
        EcUpperIniName(&key);
        sections.insert({key, position});
      }
    }
    position = file.Position();
  }
  indexed = 1;
  return 1;
}

// Seek the first matching section and return its first case-insensitive key, trimming only ASCII spaces.
// Close before retiring the live parser Strings; failed lookups leave the caller's output unchanged.
bool EcIniFile::GetValue(const ScString *section, const ScString *key, ScString *out) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString section_name(*section), key_name(*key);
  EcUpperIniName(&section_name);
  EcUpperIniName(&key_name);
  auto entry = sections.find(section_name);
  if (entry == sections.end() || file.SeekAbsolute(static_cast<i32>(entry->second)) >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  u8 found = 0;
  ScString parsed, line;
  if (GetFileLine(&line))
    while (GetFileLine(&line)) {
      EcTrimIniName(&line, true, false);
      if (!line.Strlen() || line.begin[0] == ';')
        continue;
      if (line.begin[0] == '[')
        break;
      u32 equals = 0;
      while (equals < line.Strlen() && line.begin[equals] != '=')
        ++equals;
      parsed.FromChar("", 0);
      if (equals < line.Strlen()) {
        parsed.FromChar(line.begin, equals);
        line.Erase(0, equals + 1);
      }
      EcTrimIniName(&parsed, false, true);
      EcUpperIniName(&parsed);
      if (parsed.IsEqual(&key_name)) {
        EcTrimIniName(&line, true, true);
        *out = line;
        found = 1;
        break;
      }
    }
  if (!keep_open)
    Close();
  return found;
}

// Clear the original signed output before virtual lookup, then accept a decimal prefix as scanf("%d") does.
// Use strtol for the defined integer domain and return the original Boolean success result.
bool EcIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString value;
  *out = 0;
  if (!GetValue(&section, &key, &value))
    return 0;
  const char *text = value.begin;
  char *end;
  long number = strtol(text, &end, 10);
  if (end == text)
    return 0;
  *out = static_cast<i32>(number);
  return 1;
}

// Load the index before copying and uppercasing the requested section.
bool EcIniFile::SectionExists(const ScStringView &section) {
  if (!indexed && !LoadSectionNames())
    return 0;
  ScString key(section);
  EcUpperIniName(&key);
  if (!keep_open)
    Close();
  return sections.find(key) != sections.end();
}

// Change the Boolean file-retention preference and close virtually when it changes to false.
// Compare the stored byte directly with the promoted input rather than normalizing its bits.
bool EcIniFile::SetLeaveFileOpenBetweenOperations(bool enabled) {
  if (keep_open != i32(enabled)) {
    keep_open = enabled;
    if (!enabled)
      Close();
  }
  return 1;
}

bool EcIni::ReadNumber(const ScString &, const ScString &, i32 *) {
  return 0;
}

bool EcIni::SectionExists(const ScStringView &) {
  return 0;
}

// Borrow the packaged reader's actual selected archive filename at original offset100, without lazy initialization.
const ScString *EcIni::GetCompressedPath() const {
  return &package_name;
}

bool EcIni::EnumEntriesOld(const ScString &, EcIniCallback, void *) {
  return false;
}

// Decode every requested hexadecimal pair, retaining partial writes even after an invalid digit is seen.
// An odd character count consumes the String's initialized terminator as its last low digit and reports failure.
static bool EcDecodeIniBytes(const char *text, u32 count, u8 *out) {
  bool valid = true;
  for (u32 i = 0; i < count; i += 2) {
    *out = 0;
    for (i32 shift = 4; shift >= 0; shift -= 4) {
      u8 value = static_cast<u8>(*text++), digit;
      if (value >= '0' && value <= '9')
        digit = value - '0';
      else if (value >= 'A' && value <= 'F')
        digit = value - 'A' + 10;
      else if (value >= 'a' && value <= 'f')
        digit = value - 'a' + 10;
      else {
        digit = 0;
        valid = false;
      }
      *out += static_cast<u8>(digit << shift);
    }
    ++out;
  }
  return valid;
}

// Allocate binary storage after a successful virtual lookup; a missing key leaves output untouched.
// Reserve the odd final byte so consuming the terminator preserves the decoder's delete/null/zero failure safely.
u32 EcIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString value;
  if (!GetValue(&section, &key, &value))
    return 0;
  u32 length = static_cast<u32>(value.end - value.begin);
  *out = new u8[length / 2 + (length & 1)];
  if (EcDecodeIniBytes(value.begin, length, *out))
    return length / 2;
  delete[] *out;
  *out = nullptr;
  return 0;
}

// Limit the source character count to twice the caller's byte capacity, preserving wrapped DWORD arithmetic.
// A failed conversion retains all reached output writes; missing keys leave the caller's buffer untouched.
u32 EcIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity) {
  ScString value;
  if (!GetValue(&section, &key, &value))
    return 0;
  u32 count = capacity * 2, length = static_cast<u32>(value.end - value.begin);
  if (count > length)
    count = length;
  return EcDecodeIniBytes(value.begin, count, out) ? count / 2 : 0;
}

u32 EcIni::ReadBinaryEntry(const ScString &, const ScString &, u8 **) {
  return 0;
}

u32 EcIni::ReadBinaryEntry(const ScString &, const ScString &, u8 *, u32) {
  return 0;
}

// Format the original percent-s row using zero-terminated input bytes, retaining the caller's spelling and whitespace.
static void EcFormatIniRow(ScString *out, const ScString *key, const ScString *value) {
  out->FromChar(key->begin);
  out->Append(" = ", 3);
  out->Append(value->begin, static_cast<u32>(strlen(value->begin)));
  out->Append("\r\n", 2);
}

// Update the first matching key or insert a row before the next section, retaining untouched trailing file bytes.
// New files and appended sections have distinct header prefixes; source write results are ignored and index changes differ.
bool EcIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (section.begin == section.end || key.begin == key.end)
    return 0;
  ScString header, row;
  file.SetName(name);
  if (!RsFile::FileExists(name)) {
    indexed = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(1))
      return 0;
    header.FromChar("[", 1);
    header.Append(section.begin, static_cast<u32>(strlen(section.begin)));
    header.Append("]\r\n", 3);
    file.Write(header.begin, static_cast<u32>(header.end - header.begin));
    EcFormatIniRow(&row, &key, &value);
    file.Write(row.begin, static_cast<u32>(row.end - row.begin));
    file.Close();
    return 1;
  }
  if (!indexed && !LoadSectionNames())
    return 0;
  if (file.IsOpen())
    file.Close();
  if (!Open(1))
    return 0;
  ScString section_name(section), key_name(key);
  EcUpperIniName(&section_name);
  EcUpperIniName(&key_name);
  auto entry = sections.find(section_name);
  if (entry == sections.end()) {
    file.SeekEnd();
    u32 position = file.Position();
    header.FromChar("\r\n[", 3);
    header.Append(section.begin, static_cast<u32>(strlen(section.begin)));
    header.Append("]\r\n", 3);
    file.Write(header.begin, static_cast<u32>(header.end - header.begin));
    EcFormatIniRow(&row, &key, &value);
    file.Write(row.begin, static_cast<u32>(row.end - row.begin));
    file.Flush();
    sections.insert({section_name, position + 2});
    if (!keep_open)
      Close();
    return 1;
  }
  u32 position = file.SeekAbsolute(static_cast<i32>(entry->second));
  if (position >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString candidate, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return 0;
  }
  for (;;) {
    position = file.Position();
    if (!GetFileLine(&line))
      break;
    EcTrimIniName(&line, true, false);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    bool next_section = line.begin[0] == '[';
    if (!next_section) {
      u32 equals = 0, length = static_cast<u32>(line.end - line.begin);
      while (equals < length && line.begin[equals] != '=')
        ++equals;
      candidate.FromChar(line.begin, equals < length ? equals : 0);
      EcTrimIniName(&candidate, false, true);
      EcUpperIniName(&candidate);
      if (!candidate.IsEqual(&key_name))
        continue;
    }
    u32 tail_start = next_section ? position : file.Position(), count = file.Size() - tail_start;
    u8 *tail = new u8[count];
    if (next_section)
      file.SeekAbsolute(static_cast<i32>(tail_start));
    if (file.Read(tail, count)) {
      file.SeekAbsolute(static_cast<i32>(position));
      EcFormatIniRow(&row, &key, &value);
      file.Write(row.begin, static_cast<u32>(row.end - row.begin));
      file.Write(tail, count);
      file.Resize(static_cast<i32>(file.Position()));
    }
    delete[] tail;
    indexed = 0;
    if (!keep_open)
      Close();
    return 1;
  }
  file.SeekEnd();
  file.Position();
  EcFormatIniRow(&row, &key, &value);
  file.Write(row.begin, static_cast<u32>(row.end - row.begin));
  file.Flush();
  if (!keep_open)
    Close();
  return 1;
}

// Reject empty names before invalidating the section index.
bool EcIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (section.begin == section.end || key.begin == key.end)
    return 0;
  indexed = 0;
  ScString text;
  char decimal[12];
  snprintf(decimal, sizeof(decimal), "%ld", static_cast<long>(value));
  text.FromChar(decimal);
  return WriteEntry(section, key, text);
}

// Encode a nonempty byte block as uppercase hexadecimal, then normalize the virtual writer's raw byte indication.
// The text storage outlives the temporary String; widen its extent instead of reproducing a wrapped allocation.
bool EcIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 count) {
  if (!count)
    return 0;
  char *encoded = new char[static_cast<u64>(count) * 2 + 1];
  static const char digits[] = "0123456789ABCDEF";
  for (u32 i = 0; i < count; ++i) {
    encoded[static_cast<usize>(i) * 2] = digits[bytes[i] >> 4];
    encoded[static_cast<usize>(i) * 2 + 1] = digits[bytes[i] & 15];
  }
  encoded[static_cast<usize>(count) * 2] = 0;
  u8 result;
  {
    ScString value(encoded);
    result = WriteEntry(section, key, value) != 0;
  }
  delete[] encoded;
  return result;
}

bool EcIni::WriteEntry(const ScString &, const ScString &, const ScString &) {
  return 0;
}

bool EcIni::WriteNumber(const ScString &, const ScString &, i32) {
  return 0;
}

bool EcIni::WriteBinaryEntry(const ScString &, const ScString &, const u8 *, u32) {
  return 0;
}

// Form the source split-path basename plus extension, independently truncating each component to 255 bytes.
// Ignore the drive and last slash/backslash prefix, then uppercase through the same CRT string conversion.
static ScString EcIniPackageKey(const ScString &name) {
  const char *begin = name.begin, *base = begin, *dot = nullptr, *end = begin + strlen(begin);
  if (begin != end && begin[1] == ':')
    base += 2;
  for (const char *byte = base; byte != end; ++byte)
    if (*byte == '/' || *byte == '\\')
      base = byte + 1;
    else if (*byte == '.')
      dot = byte;
  if (!dot || dot < base)
    dot = end;
  usize stem = static_cast<usize>(dot - base), extension = static_cast<usize>(end - dot);
  if (stem > 255)
    stem = 255;
  if (extension > 255)
    extension = 255;
  ScString result(base, static_cast<u32>(stem));
  result.ScString::Append(dot, static_cast<u32>(extension));
  EcUpperIniName(&result);
  return result;
}

EcIni::EcIni() : mode(0), buffer(nullptr), buffer_capacity(0) {
}

EcIni::~EcIni() {
  delete[] buffer;
}

void EcIni::SelectPackage(const ScString &input) {
  mode = 0;
  package_name = input;
}

// Read the length-prefixed package String; zero-size records reuse the previous buffer.
// Preserve a short NUL-terminated prefix; exclude only consumption of uninitialized input bytes.
void EcIni::ReadString(ScString *out) {
  u32 length = RsReadPackageWord(&package_file);
  if (length > buffer_capacity) {
    if (length == UINT32_MAX) {
      assert(false && "The original INI string allocation wraps before its terminator write");
      abort();
    }
    delete[] buffer;
    buffer = new char[static_cast<usize>(length) + 1];
    buffer[length] = 0;
  }
  u32 written = length;
  if (!package_file.opened)
    written = 0;
  else
    package_file.Read(buffer, &written);
  if (!buffer || (written < length && !memchr(buffer, 0, written))) {
    assert(false && "The original packaged INI string would consume undefined storage");
    abort();
  }
  ScString value(buffer, static_cast<u32>(strlen(buffer)));
  out->ScString::FromChar(value.begin, static_cast<u32>(value.end - value.begin));
}

// Perform the lazy package-index selection using the configured package and filename.
// Cache loose mode after an absent package/index entry, or retain the matched section offset and package mode.
void EcIni::InitFileType() {
  if (mode)
    return;
  if (!package_name.Strlen() || !RsFile::FileExists(package_name)) {
    mode = 1;
    return;
  }
  package_file.SetName(package_name);
  if (!package_file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    mode = 1;
    return;
  }
  package_offset = 0;
  ScString expected = EcIniPackageKey(name);
  package_file.SeekBegin();
  u32 count = RsReadPackageWord(&package_file), offset = count;
  ScString entry_name;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&entry_name);
    EcUpperIniName(&entry_name);
    package_file.Read(&offset, 4);
    if (entry_name.Strlen() == expected.Strlen() && !memcmp(entry_name.begin, expected.begin, entry_name.Strlen())) {
      package_offset = offset;
      mode = 2;
      return;
    }
  }
  mode = 1;
}

// Remove only ASCII spaces at the left edge. Tabs and other whitespace remain data in this parser.
static void EcIniTrimLeft(ScString *value) {
  u32 count = 0, length = static_cast<u32>(value->end - value->begin);
  while (count < length && value->begin[count] == 32)
    ++count;
  if (count)
    value->ScString::Erase(0, count);
}

// Drop trailing ASCII spaces from the bounded parser value. Tabs remain data, and the new end is zero terminated.
static void EcIniTrimRight(ScString *value) {
  while (value->end != value->begin && value->end[-1] == 32)
    --value->end;
  *value->end = 0;
}

// Take the first equals-delimited prefix and leave its suffix in the original line.
static ScString EcIniTakeKey(ScString *line) {
  u32 length = static_cast<u32>(line->end - line->begin), index = 0;
  while (index < length && line->begin[index] != '=')
    ++index;
  if (index == length)
    return ScString();
  ScString key(line->begin, index);
  line->ScString::Erase(0, index + 1);
  return key;
}

// Prefer the lazily selected package entry, matching its section header exactly and its trimmed uppercase key.
// Package values retain whitespace; loose fallback uses the inherited rules and misses leave the caller's output intact.
bool EcIni::GetValue(const ScString *section, const ScString *key, ScString *out) {
  InitFileType();
  if (mode == 1)
    return EcIniFile::GetValue(section, key, out);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section->begin, static_cast<u32>(section->end - section->begin));
  header.ScString::Append("]", 1);
  u32 count = RsReadPackageWord(&package_file);
  ScString line;
  bool selected = false;
  ScString key_name(key->begin, static_cast<u32>(key->end - key->begin));
  EcUpperIniName(&key_name);
  EcIniTrimLeft(&key_name);
  EcIniTrimRight(&key_name);
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (selected) {
      if (line.begin != line.end && line.begin[0] == '[')
        break;
      ScString candidate = EcIniTakeKey(&line);
      EcUpperIniName(&candidate);
      EcIniTrimLeft(&candidate);
      EcIniTrimRight(&candidate);
      if (candidate.Strlen() == key_name.Strlen() && !memcmp(candidate.begin, key_name.begin, candidate.Strlen())) {
        out->ScString::FromChar(line.begin, static_cast<u32>(line.end - line.begin));
        return true;
      }
    } else if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
      selected = true;
  }
  return false;
}

// Copy ordered section names before invoking callbacks, then apply the original file-retention policy.
// Each callback retains a stable name even if it changes the live section index.
bool EcIniFile::EnumSections(EcIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString empty;
  std::list<ScString> names;
  for (const auto &entry : sections)
    names.push_back(entry.first);
  for (const ScString &sectionName : names)
    callback(sectionName, empty, context);
  if (!keep_open)
    Close();
  return true;
}

// Keep every loose-file row owned until all callbacks complete, preserving String identities.
struct EcIniPair {
  ScString key, value;
};

// Buffer valid loose-file pairs before dispatch, retaining case and closing only after the callbacks complete.
// Empty pairs are skipped in loose files; the packaged reader deliberately stops instead.
bool EcIniFile::EnumerateSection(const ScString *section, EcIniCallback callback, void *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString section_name(*section);
  EcUpperIniName(&section_name);
  auto entry = sections.find(section_name);
  if (entry == sections.end() || file.SeekAbsolute(static_cast<i32>(entry->second)) >= file.Size()) {
    if (!keep_open)
      Close();
    return 0;
  }
  ScString key, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return 0;
  }
  std::vector<EcIniPair> rows;
  while (GetFileLine(&line)) {
    EcIniTrimLeft(&line);
    if (line.begin == line.end || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    key = EcIniTakeKey(&line);
    EcIniTrimRight(&key);
    EcIniTrimLeft(&line);
    EcIniTrimRight(&line);
    if (key.begin != key.end && line.begin != line.end)
      rows.push_back({key, line});
  }
  u32 count = static_cast<u32>(rows.size());
  for (u32 row = 0; row < count; ++row)
    callback(rows[row].key, rows[row].value, context);
  if (!keep_open)
    Close();
  return 1;
}

// Packaged rows call immediately and stop at the first empty pair; inherited loose enumeration buffers before dispatch.
bool EcIni::EnumerateSection(const ScString *section, EcIniCallback callback, void *context) {
  InitFileType();
  if (mode == 1)
    return EcIniFile::EnumerateSection(section, callback, context);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(static_cast<i32>(package_offset));
  ScString header("[", 1);
  header.ScString::Append(section->begin, static_cast<u32>(section->end - section->begin));
  header.ScString::Append("]", 1);
  u32 count = RsReadPackageWord(&package_file);
  ScString line;
  bool selected = false;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (!selected) {
      if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
        selected = true;
      continue;
    }
    if (line.begin != line.end && line.begin[0] == '[')
      break;
    ScString key = EcIniTakeKey(&line);
    EcIniTrimLeft(&key);
    EcIniTrimRight(&key);
    EcIniTrimLeft(&line);
    EcIniTrimRight(&line);
    if (key.begin == key.end || line.begin == line.end)
      break;
    callback(key, line, context);
  }
  return true;
}

// Creates one bit-vector column per X coordinate and fills its words.
EcLandfillGrid::EcLandfillGrid(u32 column_count, u32 row_count, const u8 *fill)
    : columns(column_count), rows(row_count), words_per_column((row_count >> 5) + ((row_count & 31) != 0)),
      data(new u32 *[column_count]) {
  for (u32 index = 0; index < columns; ++index)
    data[index] = new u32[words_per_column];
  Fill(fill);
}

// Destroys every allocated column before releasing the outer pointer array.
EcLandfillGrid::~EcLandfillGrid() {
  if (data) {
    for (u32 index = 0; index < columns; ++index)
      delete[] data[index];
    delete[] data;
  }
}

// Fill every packed word with all ones or all zeros according to the caller's original byte.
void EcLandfillGrid::Fill(const u8 *value) {
  u32 word = *value ? kScFilledBitGridWord : 0;
  for (u32 x = 0; x < columns; ++x)
    for (u32 index = 0; index < words_per_column; ++index)
      data[x][index] = word;
}

// Its returned storage address is discarded by callers.
// Set or clear one packed cell bit according to the caller's byte.
// Coordinates must address allocated words; the original performs the direct access without an error return.
void EcLandfillGrid::Set(u32 x, u32 z, const u8 *value) {
  assert(x < columns && (z >> 5) < words_per_column);
  u32 *word = &data[x][z >> 5], mask = 1u << (z & 31);
  if (*value)
    *word |= mask;
  else
    *word &= ~mask;
}

static ScLanguageManager *EcLanguageManager;
static WnResources *EcWindowResources;
static GxGraphics *EcGraphics;
static ScMessageServer *EcMessageServer;
static RsManager *EcTextManager;
static bool EcApplicationInitialized;
static void *EcApplicationInterface;

// Cache the actual graphics service before dropping its acquisition reference; recursive lookup sees the published pointer.
// An empty acquisition remains retryable without inventing a successful graphics provider.
GxGraphics *EcGetGraphics() {
  if (!EcGraphics) {
    void *object = nullptr;
    EcGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &object);
    GxGraphics *graphics = static_cast<GxGraphics *>(object);
    EcGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return EcGraphics;
}

// Obtain the module's framework through its actual director without adding a reference.
ScFramework *EcGetFramework() {
  return EcGetDirector()->FrameWork();
}

// Cache the acquired language manager before releasing its temporary reference so recursive lookup sees the cache.
// An unavailable service leaves the cache empty and a subsequent lookup retries the same original request.
ScLanguageManager *EcGetLanguageManager() {
  if (!EcLanguageManager) {
    void *object = nullptr;
    EcGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(object);
    EcLanguageManager = manager;
    if (manager)
      manager->Release();
  }
  return EcLanguageManager;
}

// Publish the acquired message service before releasing its temporary reference so reentrant lookup sees the cache.
ScMessageServer *EcGetMessageServer() {
  if (!EcMessageServer) {
    void *object = nullptr;
    EcGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    EcMessageServer = server;
    if (server)
      server->Release();
  }
  return EcMessageServer;
}

// Cache the acquired resource manager before releasing its temporary reference, retaining the original retry-on-null behavior.
RsManager *EcGetTextManager() {
  if (!EcTextManager) {
    void *object = nullptr;
    EcGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    EcTextManager = manager;
    if (manager)
      manager->Release();
  }
  return EcTextManager;
}

static void EcApplicationExit() {
}

// Set the one-time guard before querying directly into the live application slot, ignoring the query Boolean.
// Failed or empty acquisition never retries; recursive queries observe the pointer as soon as it is published.
ApGameInterface *EcGetApplication() {
  if (!EcApplicationInitialized) {
    EcApplicationInitialized = true;
    EcGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &EcApplicationInterface);
    atexit(EcApplicationExit);
  }
  return static_cast<ApGameInterface *>(EcApplicationInterface);
}

// Borrow the application's current agent-type tree on every call, preserving the original missing-application result.
CtAgentTree *EcGetAgentTree() {
  ApGameInterface *application = EcGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Borrow the application's current city on each lookup. An unavailable application leaves the result null.
CtCity *EcGetCity() {
  ApGameInterface *application = EcGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Decode ASCII decimal and hexadecimal digits. Other bytes terminate conversion with an out-of-radix result.
static u32 EcTuningDigit(u8 value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return 255;
}

// Select hexadecimal for a leading0x or any A-F/a-f byte, then retain the original 32-bit CRT saturation and sign order.
// A local integer conversion avoids the host unsigned-long width changing the game's DWORD result.
u32 EcParseTuningNumber(const char *text) {
  u32 radix = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    radix = 16;
  else
    for (const char *byte = text; *byte; ++byte)
      if ((*byte >= 'a' && *byte <= 'f') || (*byte >= 'A' && *byte <= 'F')) {
        radix = 16;
        break;
      }
  while (isspace(static_cast<u8>(*text)))
    ++text;
  bool negative = false, overflow = false;
  if (*text == '+' || *text == '-')
    negative = *text++ == '-';
  if (radix == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    text += 2;
  u32 value = 0;
  for (u32 digit = EcTuningDigit(static_cast<u8>(*text)); digit < radix;
      digit = EcTuningDigit(static_cast<u8>(*++text))) {
    if (value > (UINT32_MAX - digit) / radix)
      overflow = true;
    if (!overflow)
      value = value * radix + digit;
  }
  if (overflow) {
    errno = ERANGE;
    value = UINT32_MAX;
  }
  return negative ? 0u - value : value;
}

// Borrow the application occupant-attribute cache on every call. An absent application produces the original null result.
OcAttribCache *EcGetAttribCache() {
  ApGameInterface *application = EcGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Borrow the actual COM service through this module director without adding a reference.
ScCom *EcGetCom() {
  return EcGetDirector()->GZCOM();
}

// Publish the acquired window-resource service before releasing its temporary hold. Empty acquisition remains retryable.
WnResources *EcGetWindowResources() {
  if (!EcWindowResources) {
    void *object = nullptr;
    EcGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    WnResources *resources = static_cast<WnResources *>(object);
    EcWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return EcWindowResources;
}

// Restore E5C8's allocation and actual returned interface: the pollution view at original whole+28, not primary Unknown.
// This native factory preserves pointer identity; the private concrete source return spelling remains unresolved.
ScUnknown *EcCreatePollution() {
  return static_cast<EcLayer *>(new EcPollution);
}

// Restore E606's real incinerator construction and primary building identity. The SDK callback boundary consumes its
// Unknown prefix; the private concrete source return spelling remains unproved.
ScUnknown *EcCreateIncinerator() {
  return static_cast<BdBuilding *>(new EcIncinerator);
}

// Restore E638's independently constructed recycler and primary building identity, preserving its separate class tuning.
// This native callback does not assert the unresolved private concrete return spelling.
ScUnknown *EcCreateRecycler() {
  return static_cast<BdBuilding *>(new EcRecycler);
}

// Original registry acquisition scope121E9/1220A; the framework comes from this module's global root director.
class EcRegistryRef {
public:
  EcRegistryRef();
  ~EcRegistryRef();
  void *object; // Genuine external GetSystemService output storage.
};

EcRegistryRef::EcRegistryRef() : object(nullptr) {
  EcGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
}

EcRegistryRef::~EcRegistryRef() {
  if (object)
    static_cast<ScRegistry *>(object)->Release();
}

// SIMECO's concrete director adds only its class registrations and original module-specific lifecycle behavior.
class EcDirector final : public ScDllDirector {
public:
  EcDirector();
  ~EcDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

// Register the three actual economy constructors in source order without constructing any game receiver yet.
EcDirector::EcDirector() {
  AddClass(kEcPollutionClassId, EcCreatePollution, 0);
  AddClass(kEcIncineratorClassId, EcCreateIncinerator, 0);
  AddClass(kEcRecyclerClassId, EcCreateRecycler, 0);
}

EcDirector::~EcDirector() {
}

u32 EcDirector::GetDirectorID() const {
  return kEcDirectorId;
}

bool EcDirector::OnStart(ScCom *) {
  EcRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

bool EcDirector::AppRegister(ScRegistry *) {
  return true;
}

alignas(EcDirector) static u8 EcDirectorBytes[sizeof(EcDirector)];
static u8 EcDirectorInitialized;

static void EcDestroyDirector() {
  reinterpret_cast<EcDirector *>(EcDirectorBytes)->~EcDirector();
}

// Publish the original initialization bit before constructing the static receiver, then register its exit destructor.
// Placement construction preserves this real static object's lazy lifetime and reentrant lookup order.
ScDirector *EcGetDirector() {
  if (!(EcDirectorInitialized & 1)) {
    EcDirectorInitialized |= 1;
    new (static_cast<void *>(EcDirectorBytes)) EcDirector;
    atexit(EcDestroyDirector);
  }
  return static_cast<ScDirector *>(reinterpret_cast<EcDirector *>(EcDirectorBytes));
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Expose the genuine DLL-loader C entry, borrowing the same module singleton without changing its reference count.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return EcGetDirector();
}
#endif

namespace EcRci {

// Retire the empty polymorphic archive base after its derived frame has released the owned record scope.
EcArchive::~EcArchive() = default;

EcArchiveFrame::EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key)
    : version(0), text_flag(0), extended_flags(false), marker(0), record(nullptr), good(0), game_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    game_version[component] = city->GetVersionComponentFromSaveFile(component);
  record = new EcStreamRef(segment, key);
  good = record->IsOpen();
  if (!good) {
    delete record;
    record = nullptr;
    return;
  }
  good = record->GetStream()->ReadU16(&version);
  if (good) {
    u8 flags = 0;
    good = record->GetStream()->ReadU8(&flags);
    text_flag = flags & 1;
    extended_flags = (flags & 2) != 0;
    if (extended_flags && good)
      good = record->GetStream()->ReadU8(&flags);
  }
  if (good)
    record->GetStream()->ReadU32(&marker);
  if (good)
    good = marker == kEcRecordFrameMarker;
  if (marker != kEcRecordFrameMarker) {
    text_flag = 0;
    version = 0;
    marker = kEcRecordFrameMarker;
    delete record;
    record = nullptr;
    record = new EcStreamRef(segment, key);
    good = record->IsOpen();
    if (!good) {
      delete record;
      record = nullptr;
    }
  }
}

// Keep raw byte results from the first two writes; the marker write's return is deliberately ignored.
EcArchiveFrame::EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 text_flag)
    : version(version), text_flag(text_flag), extended_flags(false), marker(0), record(nullptr), good(0),
      game_version{} {
  if (!city)
    return;
  for (u8 component = 0; component < 3; ++component)
    game_version[component] = city->GetVersionComponent(component);
  record = new EcStreamRef(segment, key, 0);
  good = record->IsOpen();
  if (good) {
    good = record->GetStream()->WriteU16(this->version);
    if (good)
      good = record->GetStream()->WriteU16(static_cast<u16>(this->text_flag ? 3 : 2));
    if (good)
      record->GetStream()->WriteU32(kEcRecordFrameMarker);
  }
  if (!good) {
    delete record;
    record = nullptr;
  }
}

EcArchiveFrame::~EcArchiveFrame() {
  ScDelete(&record);
}

// Borrow3FF09's live stream pointer, returning null only when no stream scope is retained by this frame.
RsStream *EcArchiveFrame::GetStream() {
  return record ? record->GetStream() : nullptr;
}

void EcArchiveFrame::MarkFailed() {
  if (record)
    record->failed = 1;
  good = 0;
}

} // namespace EcRci

namespace EcRci {

EcRecordRef::EcRecordRef(RsDatabaseSegment *segment, RsKey key) : failed(0), record(nullptr), segment(segment) {
  if (segment && !segment->OpenRecord(key, &record))
    record = nullptr;
}

EcRecordRef::EcRecordRef(RsDatabaseSegment *segment, RsKey key, u32) : failed(0), record(nullptr), segment(segment) {
  if (segment && !segment->OpenRecordForWrite(key, &record))
    record = nullptr;
}

EcRecordRef::~EcRecordRef() {
  if (record) {
    if (!failed)
      failed = !segment->CloseRecord(&record);
    if (record && failed)
      segment->AbortRecord(&record);
  }
}

bool EcRecordRef::IsOpen() {
  return record != nullptr;
}

EcStreamRef::EcStreamRef(RsDatabaseSegment *segment, RsKey key) : EcRecordRef(segment, key), stream_interface(nullptr) {
  if (EcRecordRef::IsOpen() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

// Record ownership remains with the base and is independent from a missing or failed stream interface.
EcStreamRef::EcStreamRef(RsDatabaseSegment *segment, RsKey key, u32 option)
    : EcRecordRef(segment, key, option), stream_interface(nullptr) {
  if (EcRecordRef::IsOpen() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

EcStreamRef::~EcStreamRef() {
  if (stream_interface) {
    static_cast<RsStream *>(stream_interface)->Release();
    stream_interface = nullptr;
  }
}

bool EcStreamRef::IsOpen() {
  return EcRecordRef::IsOpen() && stream_interface != nullptr;
}

// Borrow42DD2's stream field used by the archive frame without acquiring another ownership reference.
// That machine body is folded with unrelated classes' field getters; it does not imply shared class ownership.
RsStream *EcStreamRef::GetStream() {
  return static_cast<RsStream *>(stream_interface);
}

} // namespace EcRci

EcRecordRef::EcRecordRef(RsDatabaseSegment *segment, RsKey key) : failed(0), record(nullptr), segment(segment) {
  if (segment && !segment->OpenRecord(key, &record))
    record = nullptr;
}

EcRecordRef::EcRecordRef(RsDatabaseSegment *segment, RsKey key, u32) : failed(0), record(nullptr), segment(segment) {
  if (segment && !segment->OpenRecordForWrite(key, &record))
    record = nullptr;
}

EcRecordRef::~EcRecordRef() {
  if (record) {
    if (!failed)
      failed = !segment->CloseRecord(&record);
    if (record && failed)
      segment->AbortRecord(&record);
  }
}

bool EcRecordRef::IsOpen() {
  return record != nullptr;
}

EcStreamRef::EcStreamRef(RsDatabaseSegment *segment, RsKey key) : EcRecordRef(segment, key), stream_interface(nullptr) {
  if (EcRecordRef::IsOpen() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

// Record ownership remains with the base and is independent from a missing or failed stream interface.
EcStreamRef::EcStreamRef(RsDatabaseSegment *segment, RsKey key, u32 option)
    : EcRecordRef(segment, key, option), stream_interface(nullptr) {
  if (EcRecordRef::IsOpen() && !record->QueryInterface(IID_RsStream, &stream_interface))
    stream_interface = nullptr;
}

EcStreamRef::~EcStreamRef() {
  if (stream_interface) {
    static_cast<RsStream *>(stream_interface)->Release();
    stream_interface = nullptr;
  }
}

bool EcStreamRef::IsOpen() {
  return EcRecordRef::IsOpen() && stream_interface != nullptr;
}

// Borrow185EE's stream field used by the archive frame without acquiring another ownership reference.
// That machine body is folded with unrelated classes' field getters; it does not imply shared class ownership.
RsStream *EcStreamRef::GetStream() {
  return static_cast<RsStream *>(stream_interface);
}

EcTextRef::EcTextRef(u32 instance, u32 group) : string_interface(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

EcTextRef::~EcTextRef() {
  if (string_interface) {
    static_cast<ScStringView *>(string_interface)->Release();
    string_interface = nullptr;
  }
}

ScString EcTextRef::CopyString() const {
  if (string_interface)
    return ScString(*static_cast<ScStringView *>(string_interface));
  ScString empty;
  return ScString(empty);
}

void EcTextRef::Resolve() {
  if (!EcGetTextManager()->GetResource(key, 105, &string_interface))
    string_interface = nullptr;
}

void EcFormatText(ScString *out, const char *format, const char *value) {
  out->Sprintf(format, value);
}

void EcFormatText(ScString *out, const char *format, const char *first, const char *second) {
  out->Sprintf(format, first, second);
}
