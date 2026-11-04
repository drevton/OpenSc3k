#include "economy/ec_history_model.h"

#include <string.h>

#include "common/sc3_director.h"
#include "economy/ec_module.h"
#include "resources/stream.h"

// Convert through the original PC53 magic-add store, retaining its low-DWORD wrap rather than a host integer cast.
static u32 EcRoundHistory(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return static_cast<u32>(bits) + kScDoubleIntegerLowWordBias;
}

EcHistoryModel::EcHistoryModel(u32 initial)
    : value_5c(0), value_60(0), value_64(0), limit(initial), automatic(false), minimum(10), multiplier(1),
      monthly(12, initial), annual(10, initial), decadal(10, initial) {
}

// Retire the histories before destroying the four owned strings in reverse construction order.
EcHistoryModel::~EcHistoryModel() = default;

// Publish the same model view for both supported interfaces before acquiring its ownership reference.
bool EcHistoryModel::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScGraphInfoView);
  HANDLE_QUERY_INTERFACE(IID_ScGraphInfoView, ScGraphInfoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcHistoryLifetime::AddRef() {
  EcGetDirector()->AddRef();
  return ++references;
}

// Release a module reference only for a nonzero entry count, then decrement the live count after the callback.
u32 EcHistoryLifetime::RemoveRef() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

// Dispatch the separate virtual decrement and destroy this model only when that method returns zero.
u32 EcHistoryLifetime::Release() {
  u32 remaining = RemoveRef();
  if (!remaining)
    delete this;
  return remaining;
}

u8 EcHistoryModel::GetMonthData(u32 *value) {
  *value = monthly[0];
  return 1;
}

// Sum all twelve monthly DWORDs modulo32 before the original unsigned division by twelve.
u32 EcHistoryModel::GetMonthAverage() const {
  u32 sum = 0;
  for (u32 index = 0; index < 12; ++index)
    sum += monthly[index];
  return sum / 12;
}

u8 EcHistoryModel::GetYearData(u32 *value) {
  *value = annual[0];
  return 1;
}

// Sum the ten annual DWORDs with unsigned wrap before dividing by ten.
u32 EcHistoryModel::GetYearAverage() const {
  u32 sum = 0;
  for (u32 index = 0; index < 10; ++index)
    sum += annual[index];
  return sum / 10;
}

u8 EcHistoryModel::GetDecadeData(u32 *value) {
  *value = decadal[0];
  return 1;
}

// Copy the supplied string before replacing the selected readout, then retire the temporary after assignment.
u8 EcHistoryModel::SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) {
  u32 index = static_cast<u32>(set);
  if (index > 2)
    return 0;
  ScString copy(value);
  readouts[index] = copy;
  return 1;
}

// Recalculate the automatic limit from all32 history entries before any new value is inserted.
// Preserve the binary32 multiplier, PC53 rounding, unsigned wrap and minimum applied after rounding.
void EcHistoryModel::RecalculateLimit() {
  u32 maximum = 0;
  for (u32 index = 0; index < 12; ++index)
    if (monthly[index] > maximum)
      maximum = monthly[index];
  for (u32 index = 0; index < 10; ++index)
    if (annual[index] > maximum)
      maximum = annual[index];
  for (u32 index = 0; index < 10; ++index)
    if (decadal[index] > maximum)
      maximum = decadal[index];
  volatile f64 multiplication = static_cast<f64>(maximum) * multiplier;
  f64 product = multiplication;
  if (product > 4294967296.0)
    product = 4294967296.0;
  u32 rounded = EcRoundHistory(product);
  limit = rounded < minimum ? minimum : rounded;
}

// Apply the original automatic threshold or manual clamp, then push the new value before dropping the oldest month.
u8 EcHistoryModel::SetMonthData(u32 value) {
  u32 maximum = limit;
  if (automatic) {
    if (value > EcRoundHistory(static_cast<f64>(maximum) / multiplier))
      RecalculateLimit();
  } else if (value > maximum) {
    value = maximum;
  }
  monthly.push_front(value);
  monthly.pop_back();
  return 1;
}

// Recalculate automatically before every annual insertion, or clamp against the existing manual limit.
// Push the annual value before dropping its oldest entry and retain the raw byte-one result.
u8 EcHistoryModel::SetYearData(u32 value) {
  if (automatic)
    RecalculateLimit();
  else if (value > limit)
    value = limit;
  annual.push_front(value);
  annual.pop_back();
  return 1;
}

// Apply the decadal automatic threshold before insertion, mirroring the monthly rule on its separate ten-entry history.
u8 EcHistoryModel::SetDecadeData(u32 value) {
  u32 maximum = limit;
  if (automatic) {
    if (value > EcRoundHistory(static_cast<f64>(maximum) / multiplier))
      RecalculateLimit();
  } else if (value > maximum) {
    value = maximum;
  }
  decadal.push_front(value);
  decadal.pop_back();
  return 1;
}

u8 EcHistoryModel::VerifyData() {
  if (automatic) {
    u32 previous = limit;
    RecalculateLimit();
    return previous == limit;
  }
  u8 unchanged = 1;
  for (u32 index = 0; index < 12; ++index)
    if (monthly[index] > limit) {
      monthly[index] = limit;
      unchanged = 0;
    }
  for (u32 index = 0; index < 10; ++index)
    if (annual[index] > limit) {
      annual[index] = limit;
      unchanged = 0;
    }
  for (u32 index = 0; index < 10; ++index)
    if (decadal[index] > limit) {
      decadal[index] = limit;
      unchanged = 0;
    }
  return unchanged;
}

u8 EcHistoryModel::SerialRead(CtCity *, RsStream *record) {
  u8 result = 1;
  for (u32 index = 0; index < 12 && result; ++index)
    result = record->ReadU32(&monthly[index]);
  for (u32 index = 0; index < 10 && result; ++index)
    result = record->ReadU32(&annual[index]);
  for (u32 index = 0; index < 10 && result; ++index)
    result = record->ReadU32(&decadal[index]);
  if (result)
    result = record->ReadU32(&limit);
  if (automatic)
    RecalculateLimit();
  else
    VerifyData();
  return result;
}

u8 EcHistoryModel::SerialWrite(CtCity *, RsStream *record) const {
  u8 result = 1;
  for (u32 index = 0; index < 12 && result; ++index)
    result = record->WriteU32(monthly[index]);
  for (u32 index = 0; index < 10 && result; ++index)
    result = record->WriteU32(annual[index]);
  for (u32 index = 0; index < 10 && result; ++index)
    result = record->WriteU32(decadal[index]);
  if (result)
    result = record->WriteU32(limit);
  return result;
}

// Return169B7's borrowed title-string view without retaining or copying its storage.
// Its private source pointer-versus-reference and const nominal declarations remain unresolved.
const ScStringView *EcHistoryModel::GetSelectionLabel() const {
  return &title;
}

// Return16DA3's borrowed readout view, selecting the third string for every index other than zero or one.
// Preserve the original selector behavior without adding a bounds guard or asserting its private nominal type.
const ScStringView *EcHistoryModel::GetCurrentValueLabel(ScGraphDataSet set) const {
  if (set == ScGraphDataSet::Month)
    return readouts;
  if (set == ScGraphDataSet::Year)
    return readouts + 1;
  return readouts + 2;
}

u32 EcHistoryModel::GetMaxValue() const {
  return limit;
}

u8 EcHistoryModel::SetSelectionLabel(const ScStringView &value) {
  ScString copy(value);
  title = copy;
  return 1;
}

u8 EcHistoryModel::SetMaxValue(u32 value) {
  limit = value;
  return 1;
}

u8 EcHistoryModel::SetAutoMax(u8 enabled) {
  automatic = enabled;
  return enabled;
}

void EcHistoryModel::SetAutoMaxFactors(u32 floor, f32 factor) {
  minimum = floor;
  multiplier = factor >= 1 ? factor : 1;
  RecalculateLimit();
}

u8 EcHistoryModel::GetMonthDataSet(u32 *values) const {
  for (u32 index = 0; index < 12; ++index)
    values[index] = monthly[index];
  return 1;
}

u8 EcHistoryModel::GetYearDataSet(u32 *values) const {
  for (u32 index = 0; index < 10; ++index)
    values[index] = annual[index];
  return 1;
}

u8 EcHistoryModel::GetDecadeDataSet(u32 *values) const {
  for (u32 index = 0; index < 10; ++index)
    values[index] = decadal[index];
  return 1;
}

// Sum the ten decadal DWORDs modulo32 before dividing by ten, matching the monthly and annual averages.
u32 EcHistoryModel::GetDecadeAverage() const {
  u32 sum = 0;
  for (u32 index = 0; index < 10; ++index)
    sum += decadal[index];
  return sum / 10;
}

u8 EcHistoryModel::SetMonthDataSet(u32 *values) {
  for (u32 index = 0; index < 12; ++index)
    monthly[index] = values[index];
  if (automatic)
    RecalculateLimit();
  else
    VerifyData();
  return 1;
}

u8 EcHistoryModel::SetYearDataSet(u32 *values) {
  for (u32 index = 0; index < 10; ++index)
    annual[index] = values[index];
  if (automatic)
    RecalculateLimit();
  else
    VerifyData();
  return 1;
}

u8 EcHistoryModel::SetDecadeDataSet(u32 *values) {
  for (u32 index = 0; index < 10; ++index)
    decadal[index] = values[index];
  if (automatic)
    RecalculateLimit();
  else
    VerifyData();
  return 1;
}

// Borrow169BB's actual first metadata word so changes through the returned pointer update this model.
// Its original nominal aggregate/pointer-or-reference spelling remains unresolved.
const u32 *EcHistoryModel::GetSelectionLabelID() const {
  return &value_5c;
}

// Borrow169BF's separate second metadata word without copying it or changing ownership.
// No semantic identity is assigned to the original four-byte metadata type.
const u32 *EcHistoryModel::GetValueLabelID() const {
  return &value_60;
}

u32 EcHistoryModel::GetLineColor() const {
  return value_64;
}

u8 EcHistoryModel::SetSelectionLabelID(const u32 &value) {
  value_5c = value;
  return 1;
}

u8 EcHistoryModel::SetValueLabelID(const u32 &value) {
  value_60 = value;
  return 1;
}

u8 EcHistoryModel::SetColor(u32 value) {
  value_64 = value;
  return 1;
}

EcHistoryLifetime::EcHistoryLifetime() : references(0) {
}

// Destroy the actual primary lifetime after the graph fields and secondary view have retired.
EcHistoryLifetime::~EcHistoryLifetime() = default;

u32 EcHistoryLifetime::RefCount() {
  return references;
}

u32 EcHistoryModel::AddRef() {
  return EcHistoryLifetime::AddRef();
}

u32 EcHistoryModel::Release() {
  return EcHistoryLifetime::Release();
}
