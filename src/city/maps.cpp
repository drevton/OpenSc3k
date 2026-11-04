#include "city/maps.h"

#include <stdlib.h>
#include <string.h>

#include <cassert>

#include "city/api.h"
#include "city/ct_graph_info.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "resources/stream.h"

CtByteMap::CtByteMap() : references(0), initialized(false), cells(nullptr) {
}

CtByteMap::~CtByteMap() {
  if (initialized)
    CtByteMap::Shutdown();
}

// Publish the primary map view for the three actual common/byte identities before virtual retention. Failed queries
// clear the output.
bool CtByteMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, CtByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapU8, CtByteMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this map's reference word before acquiring its module director hold; return the live count afterward.
u32 CtByteMap::AddRef() {
  ++references;
  CtGetServiceDirector()->AddRef();
  return references;
}

// Remove a module hold before decrementing a nonzero count, then destroy through the real virtual destructor at zero.
// A release already at zero also destroys the receiver without removing a module hold.
u32 CtByteMap::Release() {
  if (references) {
    CtGetServiceDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

u32 CtByteMap::CellCountX() const {
  return cells->CellCountX();
}

u32 CtByteMap::CellCountZ() const {
  return cells->CellCountY();
}

void CtByteMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x][z];
}

bool CtByteMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtByteMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtByteMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool CtByteMap::SerialRead(RsStream *record) {
  return cells->SerialRead(record);
}

bool CtByteMap::SerialWrite(RsStream *record) const {
  return cells->SerialWrite(record);
}

// Dispatch virtual shutdown on reinitialization, then query city height before width and create full-resolution cells.
// Publish the replacement pointer before setting the initialized byte.
bool CtByteMap::Init(CtCity *city, const u8 *fill) {
  if (initialized)
    Shutdown();
  u32 rows = city->CellCountZ(), columns = city->CellCountX();
  cells = new CtByteCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Delete a nonnull cell object only while initialized, clear its pointer after destruction and clear the ready flag last.
bool CtByteMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void CtByteMap::GetValue(u32 x, u32 z, u8 *value) const {
  cells->GetValue(x, z, value);
}

void CtByteMap::SetValue(u32 x, u32 z, const u8 *value) {
  cells->SetValue(x, z, value);
}

void CtByteMap::SetAllCells(const u8 *value) {
  cells->SetAllCells(value);
}

void CtByteMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  cells->SetValue(x0, z0, x1, z1, value);
}

namespace CtService {

// Preserve the original PC53 magic-add rounding and low-DWORD wrap instead of using a host integer conversion.
static u32 ScRoundGraphValue(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Construct the real lifetime and four Strings before setting graph defaults and filling all three histories with
// initial.
// The supplied unsigned value is both the first maximum and each queue element, not a zero-fill request.
ScGraphInfo::ScGraphInfo(u32 initial)
    : selectionLabelId(0), valueLabelId(0), color(0), maximum(initial), automaticScale(false), minimum(10),
      scaleFactor(1) {
  for (u32 i = 0; i < 12; ++i)
    monthly[i] = initial;
  for (u32 i = 0; i < 10; ++i)
    yearly[i] = initial;
  for (u32 i = 0; i < 10; ++i)
    decades[i] = initial;
}

// Retire the three histories before the four Strings and real module lifetime base in reverse member order.
ScGraphInfo::~ScGraphInfo() = default;

// Publish the original secondary graph identity for either accepted IID and retain through the real lifetime primary.
// Unsupported requests clear the output.
bool ScGraphInfo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScGraphInfoView);
  HANDLE_QUERY_INTERFACE(IID_ScGraphInfoView, ScGraphInfoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScGraphInfo::AddRef() {
  return ScRefCount::AddRef();
}

u32 ScGraphInfo::Release() {
  return ScRefCount::Release();
}

// Retain binary32 multiplication input, the exact2^32 float cap and source rounding before the final minimum clamp.
void ScGraphInfo::ComputeAutoMax() {
  u32 largest = 0;
  for (u32 i = 0; i < 12; ++i)
    if (monthly[i] > largest)
      largest = monthly[i];
  for (u32 i = 0; i < 10; ++i)
    if (yearly[i] > largest)
      largest = yearly[i];
  for (u32 i = 0; i < 10; ++i)
    if (decades[i] > largest)
      largest = decades[i];
  volatile f64 multiplication = f64(largest) * scaleFactor;
  f64 product = multiplication;
  if (product > 4294967296.0)
    product = 4294967296.0;
  u32 rounded = ScRoundGraphValue(product);
  maximum = rounded < minimum ? minimum : rounded;
}

// Borrow the owned selection String without copying or retaining it; the original pointer-versus-reference spelling is
// unresolved.
const ScStringView *ScGraphInfo::GetSelectionLabel() const {
  return &selectionLabel;
}

// Borrow the selected current-value String, using the third String for every selector other than month or year.
const ScStringView *ScGraphInfo::GetCurrentValueLabel(ScGraphDataSet set) const {
  u32 index = set == ScGraphDataSet::Month ? 0 : set == ScGraphDataSet::Year ? 1 : 2;
  return currentLabels + index;
}

// Borrow the original label identifier word through its const receiver; pointer-versus-reference return spelling
// remains unresolved.
const u32 *ScGraphInfo::GetSelectionLabelID() const {
  return &selectionLabelId;
}

// Borrow the original label identifier word through its const receiver; pointer-versus-reference return spelling
// remains unresolved.
const u32 *ScGraphInfo::GetValueLabelID() const {
  return &valueLabelId;
}

u32 ScGraphInfo::GetLineColor() const {
  return color;
}

u32 ScGraphInfo::GetMaxValue() const {
  return maximum;
}

u8 ScGraphInfo::GetMonthData(u32 *out) {
  *out = monthly[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetMonthDataSet(u32 *out) const {
  for (u32 i = 0; i < 12; ++i)
    out[i] = monthly[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetMonthAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 12; ++i)
    sum += monthly[i];
  return sum / 12;
}

u8 ScGraphInfo::GetYearData(u32 *out) {
  *out = yearly[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetYearDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    out[i] = yearly[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetYearAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 10; ++i)
    sum += yearly[i];
  return sum / 10;
}

u8 ScGraphInfo::GetDecadeData(u32 *out) {
  *out = decades[0];
  return 1;
}

// Copy this complete history in logical newest-first order without modifying its queue.
u8 ScGraphInfo::GetDecadeDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    out[i] = decades[i];
  return 1;
}

// Sum the history modulo32 before its original unsigned division by the fixed queue length.
u32 ScGraphInfo::GetDecadeAverage() const {
  u32 sum = 0;
  for (u32 i = 0; i < 10; ++i)
    sum += decades[i];
  return sum / 10;
}

// Report whether automatic recalculation preserves the maximum, or clamp all manual histories and report any change.
u8 ScGraphInfo::VerifyData() {
  if (automaticScale) {
    u32 previous = maximum;
    ComputeAutoMax();
    return previous == maximum;
  }
  u8 unchanged = 1;
  for (u32 i = 0; i < 12; ++i)
    if (monthly[i] > maximum) {
      monthly[i] = maximum;
      unchanged = 0;
    }
  for (u32 i = 0; i < 10; ++i)
    if (yearly[i] > maximum) {
      yearly[i] = maximum;
      unchanged = 0;
    }
  for (u32 i = 0; i < 10; ++i)
    if (decades[i] > maximum) {
      decades[i] = maximum;
      unchanged = 0;
    }
  return unchanged;
}

// Copy through the original temporary String before replacing the owned selection label, retaining alias-safe lifetime
// ordering.
u8 ScGraphInfo::SetSelectionLabel(const ScStringView &value) {
  ScString copy(value);
  selectionLabel = copy;
  return 1;
}

// Reject unsupported datasets before constructing the temporary String, then replace the selected owned label.
u8 ScGraphInfo::SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) {
  u32 index = u32(set);
  if (index > 2)
    return 0;
  ScString copy(value);
  currentLabels[index] = copy;
  return 1;
}

u8 ScGraphInfo::SetSelectionLabelID(const u32 &value) {
  selectionLabelId = value;
  return 1;
}

u8 ScGraphInfo::SetValueLabelID(const u32 &value) {
  valueLabelId = value;
  return 1;
}

u8 ScGraphInfo::SetColor(u32 value) {
  color = value;
  return 1;
}

u8 ScGraphInfo::SetMaxValue(u32 value) {
  maximum = value;
  return 1;
}

u8 ScGraphInfo::SetAutoMax(u8 enabled) {
  automaticScale = enabled;
  return enabled;
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetMonthData(u32 value) {
  if (automaticScale) {
    if (value > ScRoundGraphValue(f64(maximum) / scaleFactor))
      ComputeAutoMax();
  } else if (value > maximum) {
    value = maximum;
  }
  for (u32 i = 11; i; --i)
    monthly[i] = monthly[i - 1];
  monthly[0] = value;
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetMonthDataSet(u32 *values) {
  for (u32 i = 0; i < 12; ++i)
    monthly[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetYearData(u32 value) {
  if (automaticScale)
    ComputeAutoMax();
  else if (value > maximum)
    value = maximum;
  for (u32 i = 9; i; --i)
    yearly[i] = yearly[i - 1];
  yearly[0] = value;
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetYearDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    yearly[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Apply the original automatic rule to old histories or clamp manually, then push the newest value before dropping the
// oldest.
u8 ScGraphInfo::SetDecadeData(u32 value) {
  if (automaticScale) {
    if (value > ScRoundGraphValue(f64(maximum) / scaleFactor))
      ComputeAutoMax();
  } else if (value > maximum) {
    value = maximum;
  }
  for (u32 i = 9; i; --i)
    decades[i] = decades[i - 1];
  decades[0] = value;
  return 1;
}

// Replace every queue value before automatic recalculation or the original live virtual manual verification.
u8 ScGraphInfo::SetDecadeDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    decades[i] = values[i];
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return 1;
}

// Read histories and maximum with short circuiting, then recalculate or virtually verify even after a failed transport.
u8 ScGraphInfo::SerialRead(CtCity *, RsStream *record) {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = record->ReadU32(&monthly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->ReadU32(&yearly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->ReadU32(&decades[i]);
  if (result)
    result = record->ReadU32(&maximum);
  if (automaticScale)
    ComputeAutoMax();
  else
    VerifyData();
  return result;
}

// Write all32 history words and the current maximum in source order, retaining each live value load before its
// callback.
u8 ScGraphInfo::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = record->WriteU32(monthly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->WriteU32(yearly[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = record->WriteU32(decades[i]);
  if (result)
    result = record->WriteU32(maximum);
  return result;
}

void ScGraphInfo::SetAutoMaxFactors(u32 floor, f32 factor) {
  minimum = floor;
  scaleFactor = factor >= 1 ? factor : 1;
  ComputeAutoMax();
}

ScRefCount::ScRefCount() : references(0) {
}

ScRefCount::~ScRefCount() {
}

// Dispatch virtual removal first and destroy through the real lifetime interface when its returned count is zero.
u32 ScRefCount::Release() {
  u32 count = RemoveRef();
  if (!count)
    delete this;
  return count;
}

u32 ScRefCount::AddRef() {
  CtGetServiceDirector()->AddRef();
  return ++references;
}

// Leave zero unchanged; otherwise remove the actual module hold before decrementing the live receiver count.
u32 ScRefCount::RemoveRef() {
  if (references) {
    CtGetServiceDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScRefCount::RefCount() {
  return references;
}
} // namespace CtService

// Actual seventy signed stride words end immediately before the next interface table.
static const i16 CtIteratorSteps[] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
    79, 83, 89, 97, 101, 103, 107, 109, 113, 179, 211, 257, 307, 373, 479, 577, 641, 719, 857, 977, 1063, 1237, 1471,
    1663, 1993, 2221, 2539, 2909, 3259, 3733, 4507, 5281, 5861, 6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431,
    19801, 21569, 23633, 26003, 27529, 29683, 32051, 32611};

// Shuffle from the second element forward, consuming one CRT draw for each element and retaining insertion order.
// Equal endpoints perform no pointer arithmetic or random draws, including an empty unallocated vector.
template <class T> static void CtShuffleIteratorCells(T *begin, T *end) {
  if (begin != end) {
    for (T *cell = begin + 1; cell != end; ++cell) {
      i32 draw = ScRand();
      T value = *cell, *other = begin + u32(draw) % (u32(cell - begin) + 1);
      *cell = *other;
      *other = value;
    }
  }
}

// Choose the original signed stride for either iterator without consuming a CRT random draw.
// A nonempty permutation and an available table entry are required by both original traversal classes.
static i32 CtIteratorStride(i32 *count) {
  u32 step = 0;
  if (*count >= 12227) {
    if (*count >= kScPermutationMaximumCount)
      *count = kScPermutationMaximumCount;
    step = 60;
  } else if (*count >= 3259)
    step = 50;
  else if (*count >= 857)
    step = 40;
  else if (*count >= 113)
    step = 30;
  else if (*count >= 71)
    step = 20;
  else if (*count >= 29)
    step = 10;
  assert(*count);
  while (step < 70 && CtIteratorSteps[step] < *count / 4)
    ++step;
  while (step < 70 && *count % CtIteratorSteps[step] == 0)
    ++step;
  assert(step < 70);
  return CtIteratorSteps[step];
}

CtRandomIterator::CtRandomIterator()
    : state(1), mode(1), dirty(1), left(0), top(0), right(0), bottom(0), count(0), cursor(0), index(0), stride(1) {
}

// Retire only the actual packed-coordinate owner through the project array. Installed crime destruction inlines this
// vector cleanup. No standalone PC destructor entry is asserted for that library helper.
CtRandomIterator::~CtRandomIterator() = default;

// Reset the cursor, then refill/shuffle packed cells or choose the original arithmetic stride and CRT start index.
// Undefined divisors and out-of-array stride reads stay explicit native boundaries rather than fabricated scan values.
void CtRandomIterator::Begin(u8 shuffle) {
  state = 1;
  cursor = 0;
  count = i32((u32(right) - u32(left) + 1) * (u32(bottom) - u32(top) + 1));
  if (!mode) {
    if (i32(cells.size) != count) {
      if (count < 0)
        ScFatal(SC_ERROR_INVALID, "Negative packed city-service scan length.");
      u32 old_size = cells.size;
      cells.Reserve(u32(count));
      cells.size = u32(count);
      for (u32 offset = old_size; offset < cells.size; ++offset)
        cells.data[offset] = 0;
      dirty = 1;
    }
    if (dirty) {
      u32 offset = 0;
      for (i32 x = left; x <= right; x = i32(u32(x) + 1))
        for (i32 z = top; z <= bottom; z = i32(u32(z) + 1))
          cells.data[offset++] = u16((u32(u8(x)) << 8) | u8(z));
    }
    if (shuffle)
      CtShuffleIteratorCells(cells.data, cells.size ? cells.data + cells.size : cells.data);
  } else {
    stride = CtIteratorStride(&count);
    index = ScRand() % count;
  }
}

// Advance before reporting arithmetic coordinates, or decode packed X from the high byte and Z from the low byte.
// The final successful output publishes exhaustion; subsequent calls leave both original outputs untouched.
u8 CtRandomIterator::Next(i32 *output_x, i32 *output_z) {
  if (state == 2)
    return 0;
  if (mode) {
    cursor = i32(u32(cursor) + 1);
    if (cursor > count) {
      state = 2;
      return 0;
    }
    index = i32(u32(index) + u32(stride)) % count;
    *output_x = i32(u32(left) + u32(index % i32(u32(right) - u32(left) + 1)));
    *output_z = i32(u32(top) + u32(index / i32(u32(right) - u32(left) + 1)));
    if (cursor == count)
      state = 2;
  } else {
    i32 total = i32(cells.size);
    if (cursor >= total) {
      state = 2;
      return 0;
    }
    u32 previous = u32(cursor);
    cursor = i32(u32(cursor) + 1);
    u16 packed = cells.data[previous];
    *output_x = u8(packed >> 8);
    *output_z = u8(packed);
    if (cursor == total)
      state = 2;
  }
  return 1;
}

CtSavedIterator::CtSavedIterator() {
}

// Read into the original preallocated packed-cell storage.
// Read the original scalar order and reversed packed words into existing storage without resizing the vector.
// The record count must fit that allocation; failed stream reads retain their original false result.
bool CtSavedIterator::SerialRead(CtCity *, RsStream *record) {
  u32 value = 1;
  if (!record->ReadI8(&state) || !record->ReadU32(&value))
    return false;
  mode = value;
  u8 loaded_dirty;
  if (!record->ReadU8(&loaded_dirty))
    return false;
  dirty = loaded_dirty != 0;
  if (!record->ReadI32(&left) || !record->ReadI32(&right) || !record->ReadI32(&top) || !record->ReadI32(&bottom) ||
      !record->ReadI32(&count) || !record->ReadU32(&value))
    return false;
  assert(value <= cells.capacity);
  while (value) {
    --value;
    if (!record->ReadU16(cells.data + value))
      return false;
  }
  return record->ReadI32(&cursor) && record->ReadI32(&index) && record->ReadI32(&stride);
}

// Write state/mode/dirty, left/right/top/bottom/count, the captured vector size and reversed words, then three cursors.
// Stop at the first refused scalar while rereading live fields and vector storage after preceding record callbacks.
bool CtSavedIterator::SerialWrite(CtCity *, RsStream *record) const {
  bool result = record->WriteI8(state);
  result = result && record->WriteU32(mode) && record->WriteU8(dirty != 0) && record->WriteI32(left) &&
      record->WriteI32(right) && record->WriteI32(top) && record->WriteI32(bottom) && record->WriteI32(count);
  u32 size = cells.size;
  if (result)
    result = record->WriteU32(size);
  for (u32 remaining = size; result && remaining;)
    result = record->WriteU16(cells.data[--remaining]);
  result = result && record->WriteI32(cursor) && record->WriteI32(index) && record->WriteI32(stride);
  return result;
}

// Replace the original four scan bounds and clamp each reversed endpoint to its matching start.
// Mark the packed coordinate cache dirty even when the bounds did not change.
void CtRandomIterator::SetBounds(const ScRect &bounds) {
  left = bounds.left;
  top = bounds.top;
  right = bounds.right;
  bottom = bounds.bottom;
  if (right < left)
    right = left;
  if (bottom < top)
    bottom = top;
  dirty = 1;
}

CtLinearIterator::CtLinearIterator() : state(1), mode(1), dirty(1), count(0), cursor(0), index(0), stride(1) {
}

// Release only the actual index-vector storage. No sprite, occupant or game-resource references belong to this class.
CtLinearIterator::~CtLinearIterator() = default;

// Begin an arithmetic or stored-index traversal, retaining the source's independent CRT stream.
// An empty index range immediately publishes exhaustion and clears its start and stride.
void CtLinearIterator::Begin(u8 shuffle) {
  state = 1;
  cursor = 0;
  if (!count) {
    state = 2;
    stride = 0;
    index = 0;
    dirty = 1;
    return;
  }
  if (mode) {
    stride = CtIteratorStride(&count);
    index = ScRand() % count;
  } else {
    if (i32(indices.size) != count) {
      if (count < 0)
        ScFatal(SC_ERROR_INVALID, "Negative sprite pool traversal length.");
      u32 old_size = indices.size;
      indices.Reserve(u32(count));
      indices.size = u32(count);
      for (u32 at = old_size; at < indices.size; ++at)
        indices.data[at] = 0;
      dirty = 1;
    }
    if (dirty)
      for (i32 at = 0; at < count; ++at)
        indices.data[at] = u32(at);
    if (shuffle)
      CtShuffleIteratorCells(indices.data, indices.size ? indices.data + indices.size : indices.data);
  }
}

// Advance before reporting the next pool index and leave the output untouched on failure.
// Unlike the rectangular iterator, the final successful value does not yet change state to exhausted.
u8 CtLinearIterator::Next(i32 *output) {
  if (state == 2)
    return 0;
  if (mode) {
    if (cursor < count) {
      index = i32(u32(index) + u32(stride)) % count;
      cursor = i32(u32(cursor) + 1);
      *output = index;
      return 1;
    }
  } else if (cursor < i32(indices.size)) {
    i32 previous = cursor;
    cursor = i32(u32(cursor) + 1);
    *output = i32(indices.data[previous]);
    return 1;
  }
  state = 2;
  return 0;
}

// Allocate the column pointer array, then allocate and fill columns in descending order. Each fill reads the original
// input byte, and empty dimensions retain their actual empty allocations without creating initialized cell values.
CtByteCells::CtByteCells(u32 count, u32 rows, const u8 *fill) : column_count(count), row_count(rows) {
  columns = new u8 *[count];
  for (i32 x = i32(column_count - 1u); x >= 0; --x) {
    columns[x] = new u8[row_count];
    for (i32 z = i32(row_count - 1u); z >= 0; --z)
      columns[x][z] = *fill;
  }
}

// Retire allocated columns from last to first before deleting the pointer array. No cell values or lengths are changed.
CtByteCells::~CtByteCells() {
  if (columns) {
    for (i32 x = i32(column_count - 1u); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Read every column in descending order, retaining earlier failures without skipping later record calls.
bool CtByteCells::SerialRead(RsStream *record) {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->ReadBytes(columns[x], row_count))
      result = false;
  return result;
}

// Write every column in descending order even after a refused write, combining the actual record replies.
bool CtByteCells::SerialWrite(RsStream *record) const {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->WriteBytes(columns[x], row_count))
      result = false;
  return result;
}

u32 CtByteCells::CellCountX() const {
  return column_count;
}

u32 CtByteCells::CellCountY() const {
  return row_count;
}

u8 CtByteCells::InBounds(u32 x, u32 z) const {
  return x < column_count && z < row_count;
}

u8 CtByteCells::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

u8 CtByteCells::ValidCellRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void CtByteCells::GetValue(u32 x, u32 z, u8 *value) const {
  *value = columns[x][z];
}

void CtByteCells::SetValue(u32 x, u32 z, const u8 *value) {
  columns[x][z] = *value;
}

void CtByteCells::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  for (u32 x = x0; x <= x1; ++x)
    memset(columns[x] + z0, *value, u32(z1 - z0 + 1));
}

// Fill columns in ascending order, capturing the row length once and rereading the caller's byte for each column.
void CtByteCells::SetAllCells(const u8 *value) {
  u32 rows = row_count;
  for (u32 x = 0; x < column_count; ++x)
    memset(columns[x], *value, rows);
}

CtSizedByteMap::CtSizedByteMap(i32 value)
    : references(0), initialized(false), width(0), height(0), shift(value), cells(nullptr) {
}

CtSizedByteMap::~CtSizedByteMap() {
}

// Expose the primary Unknown, map-base and unsigned-byte map identities, retaining after publishing the output.
// Unsupported queries clear the output.
bool CtSizedByteMap::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScCellMapBase && iid != IID_ScCellMapU8)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScCellMap<u8> *>(this);
  AddRef();
  return true;
}

// Increment the shared map count before acquiring the actual SIMSERV director hold. Return the current count after
// that synchronous callback so reentrant changes retain their original visibility.
u32 CtSizedByteMap::AddRef() {
  ++references;
  CtGetServiceDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a positive count, then delete through the real virtual destructor at zero.
// A zero entry still deletes without underflowing or releasing another director hold.
u32 CtSizedByteMap::Release() {
  if (references) {
    CtGetServiceDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

u32 CtSizedByteMap::CellCountX() const {
  return width;
}

u32 CtSizedByteMap::CellCountZ() const {
  return height;
}

void CtSizedByteMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

bool CtSizedByteMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtSizedByteMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtSizedByteMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool CtSizedByteMap::SerialRead(RsStream *record) {
  return cells->SerialRead(record);
}

bool CtSizedByteMap::SerialWrite(RsStream *record) const {
  return cells->SerialWrite(record);
}

// Invoke the real virtual shutdown on reinitialization, then query width before height and allocate shifted storage.
// Publish initialized only after the new cell object is assigned; the city pointer is borrowed for these queries only.
bool CtSizedByteMap::Init(CtCity *city, const u8 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  u32 columns = width >> shift, rows = height >> shift;
  cells = new CtByteCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Delete cell storage only while initialized, clearing its pointer after destruction. Always clear the ready flag last,
// while retaining the original dimensions and resolution for subsequent queries or reinitialization.
bool CtSizedByteMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void CtSizedByteMap::GetValue(u32 x, u32 z, u8 *value) const {
  cells->GetValue(x >> shift, z >> shift, value);
}

void CtSizedByteMap::SetValue(u32 x, u32 z, const u8 *value) {
  cells->SetValue(x >> shift, z >> shift, value);
}

void CtSizedByteMap::SetAllCells(const u8 *value) {
  cells->SetAllCells(value);
}

// Fill both inclusive shifted ranges with X outside Z, rereading the input byte for every written cell.
// Reversed ranges perform no writes, and no clipping or synthetic bounds checks alter the original traversal.
void CtSizedByteMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  u32 first_x = x0 >> shift, first_z = z0 >> shift, last_x = x1 >> shift, last_z = z1 >> shift;
  for (u32 x = first_x; x <= last_x; ++x)
    for (u32 z = first_z; z <= last_z; ++z)
      cells->columns[x][z] = *value;
}

// Allocate the column pointer array, then allocate and fill columns in descending order. Each fill reads the original
// input integer, and empty dimensions retain their actual empty allocations without creating initialized cell values.
CtIntCells::CtIntCells(u32 count, u32 rows, const i32 *fill) : column_count(count), row_count(rows) {
  columns = new i32 *[count];
  for (i32 x = i32(column_count - 1u); x >= 0; --x) {
    columns[x] = new i32[row_count];
    for (i32 z = i32(row_count - 1u); z >= 0; --z)
      columns[x][z] = *fill;
  }
}

// Retire allocated columns from last to first before deleting the pointer array. No cell values or lengths are changed.
CtIntCells::~CtIntCells() {
  if (columns) {
    for (i32 x = i32(column_count - 1u); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Read every column in descending order, retaining earlier failures without skipping later record calls.
bool CtIntCells::SerialRead(RsStream *record) {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->ReadBlock(columns[x], row_count * 4u))
      result = false;
  return result;
}

// Write every column in descending order even after a refused write, combining the actual record replies.
bool CtIntCells::SerialWrite(RsStream *record) const {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->WriteBlock(columns[x], row_count * 4u))
      result = false;
  return result;
}

// Fill columns in ascending order using the original byte-oriented memset over four times the row count.
// The signed input integer supplies only its low byte to each stored byte, so value 1 produces integer cells.
void CtIntCells::SetAllCells(const i32 *value) {
  u32 bytes = row_count * 4u;
  for (u32 x = 0; x < column_count; ++x)
    memset(columns[x], *value, bytes);
}

CtSizedIntMap::CtSizedIntMap(i32 value)
    : references(0), initialized(false), width(0), height(0), shift(value), cells(nullptr) {
}

CtSizedIntMap::~CtSizedIntMap() {
}

// Expose the primary Unknown, map-base and signed-int map identities, retaining after publishing the output.
// Unsupported queries clear the output.
bool CtSizedIntMap::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScCellMapBase && iid != IID_ScCellMapI32)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScCellMap<i32> *>(this);
  AddRef();
  return true;
}

// Increment the shared map count before acquiring the actual SIMSERV director hold. Return the current count after
// that synchronous callback so reentrant changes retain their original visibility.
u32 CtSizedIntMap::AddRef() {
  ++references;
  CtGetServiceDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a positive count, then delete through the real virtual destructor at zero.
// A zero entry still deletes without underflowing or releasing another director hold.
u32 CtSizedIntMap::Release() {
  if (references) {
    CtGetServiceDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

u32 CtSizedIntMap::CellCountX() const {
  return width;
}

u32 CtSizedIntMap::CellCountZ() const {
  return height;
}

void CtSizedIntMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

bool CtSizedIntMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtSizedIntMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtSizedIntMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool CtSizedIntMap::SerialRead(RsStream *record) {
  return cells->SerialRead(record);
}

bool CtSizedIntMap::SerialWrite(RsStream *record) const {
  return cells->SerialWrite(record);
}

// Invoke the real virtual shutdown on reinitialization, then query width before height and allocate shifted storage.
// Publish initialized only after the new cell object is assigned; the city pointer is borrowed for these queries only.
bool CtSizedIntMap::Init(CtCity *city, const i32 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  u32 columns = width >> shift, rows = height >> shift;
  cells = new CtIntCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Delete cell storage only while initialized, clearing its pointer after destruction. Always clear the ready flag last,
// while retaining the original dimensions and resolution for subsequent queries or reinitialization.
bool CtSizedIntMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void CtSizedIntMap::GetValue(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

void CtSizedIntMap::SetValue(u32 x, u32 z, const i32 *value) {
  cells->columns[x >> shift][z >> shift] = *value;
}

void CtSizedIntMap::SetAllCells(const i32 *value) {
  cells->SetAllCells(value);
}

// Fill both inclusive shifted ranges with X outside Z, rereading the input integer for every written cell.
// Reversed ranges perform no writes, and no clipping or synthetic bounds checks alter the original traversal.
void CtSizedIntMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i32 *value) {
  u32 first_x = x0 >> shift, first_z = z0 >> shift, last_x = x1 >> shift, last_z = z1 >> shift;
  for (u32 x = first_x; x <= last_x; ++x)
    for (u32 z = first_z; z <= last_z; ++z)
      cells->columns[x][z] = *value;
}

// Allocate the column pointer array, then allocate and fill columns in descending order. Each fill reads the original
// input word, and empty dimensions retain their actual empty allocations without creating initialized cell values.
CtShortCells::CtShortCells(u32 count, u32 rows, const i16 *fill) : column_count(count), row_count(rows) {
  columns = new i16 *[count];
  for (i32 x = i32(column_count - 1u); x >= 0; --x) {
    columns[x] = new i16[row_count];
    for (i32 z = i32(row_count - 1u); z >= 0; --z)
      columns[x][z] = *fill;
  }
}

// Retire allocated columns from last to first before deleting the pointer array. No cell values or lengths are changed.
CtShortCells::~CtShortCells() {
  if (columns) {
    for (i32 x = i32(column_count - 1u); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Read every column in descending order, retaining earlier failures without skipping later record calls.
bool CtShortCells::SerialRead(RsStream *record) {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->ReadBlock(columns[x], row_count * 2u))
      result = false;
  return result;
}

// Write every column in descending order even after a refused write, combining the actual record replies.
bool CtShortCells::SerialWrite(RsStream *record) const {
  bool result = true;
  for (i32 x = i32(column_count - 1u); x >= 0; --x)
    if (!record->WriteBlock(columns[x], row_count * 2u))
      result = false;
  return result;
}

// Fill columns in ascending order using the original byte-oriented memset over twice the row count.
// The signed input word supplies only its low byte to each stored byte, so value 1 produces 0x0101 short cells.
void CtShortCells::SetAllCells(const i16 *value) {
  u32 bytes = row_count * 2u;
  for (u32 x = 0; x < column_count; ++x)
    memset(columns[x], *value, bytes);
}

CtSizedShortMap::CtSizedShortMap(i32 value)
    : references(0), initialized(false), width(0), height(0), shift(value), cells(nullptr) {
}

CtSizedShortMap::~CtSizedShortMap() {
}

// Expose the primary Unknown, map-base and signed-short map identities, retaining after publishing the output.
// Unsupported queries clear the output.
bool CtSizedShortMap::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScCellMapBase && iid != IID_ScCellMapI16)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScCellMap<i16> *>(this);
  AddRef();
  return true;
}

// Increment the shared map count before acquiring the actual SIMSERV director hold. Return the current count after
// that synchronous callback so reentrant changes retain their original visibility.
u32 CtSizedShortMap::AddRef() {
  ++references;
  CtGetServiceDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a positive count, then delete through the real virtual destructor at zero.
// A zero entry still deletes without underflowing or releasing another director hold.
u32 CtSizedShortMap::Release() {
  if (references) {
    CtGetServiceDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

u32 CtSizedShortMap::CellCountX() const {
  return width;
}

u32 CtSizedShortMap::CellCountZ() const {
  return height;
}

void CtSizedShortMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

bool CtSizedShortMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtSizedShortMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtSizedShortMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool CtSizedShortMap::SerialRead(RsStream *record) {
  return cells->SerialRead(record);
}

bool CtSizedShortMap::SerialWrite(RsStream *record) const {
  return cells->SerialWrite(record);
}

// Invoke the real virtual shutdown on reinitialization, then query width before height and allocate shifted storage.
// Publish initialized only after the new cell object is assigned; the city pointer is borrowed for these queries only.
bool CtSizedShortMap::Init(CtCity *city, const i16 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  u32 columns = width >> shift, rows = height >> shift;
  cells = new CtShortCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Delete cell storage only while initialized, clearing its pointer after destruction. Always clear the ready flag last,
// while retaining the original dimensions and resolution for subsequent queries or reinitialization.
bool CtSizedShortMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void CtSizedShortMap::GetValue(u32 x, u32 z, i16 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

void CtSizedShortMap::SetValue(u32 x, u32 z, const i16 *value) {
  cells->columns[x >> shift][z >> shift] = *value;
}

void CtSizedShortMap::SetAllCells(const i16 *value) {
  cells->SetAllCells(value);
}

// Fill both inclusive shifted ranges with X outside Z, rereading the input short for every written cell.
// Reversed ranges perform no writes, and no clipping or synthetic bounds checks alter the original traversal.
void CtSizedShortMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value) {
  u32 first_x = x0 >> shift, first_z = z0 >> shift, last_x = x1 >> shift, last_z = z1 >> shift;
  for (u32 x = first_x; x <= last_x; ++x)
    for (u32 z = first_z; z <= last_z; ++z)
      cells->columns[x][z] = *value;
}
