#include "city/timing.h"

#include <float.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <fenv.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>

#include "city/sc_random.h"
#include "city/sc_stopwatch.h"
#include "city/simulation.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"

// Clock rates express simulated days per real day, converted using milliseconds per day.
enum CtClockPeriod : u32 {
  kCtClockMillisecondsPerDay = 0x5265c00,
};

static_assert(sizeof(CtClockCallback) == sizeof(ScMessageQueue *));

// Read the platform millisecond counter, retaining the original low-word wrap used by game clocks and seed sentinels.
static u32 CtPlatformTicks() {
  return static_cast<u32>(SDL_GetTicks());
}

// Retain the original lock scope through nested callbacks and C++ exception unwinding.
class CtClockGuard {
public:
  // Enter the clock's actual recursive lock before any guarded state access.
  explicit CtClockGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Leave the same lock when the scope ends, including when a notification throws.
  ~CtClockGuard() {
    lock->Unlock();
  }

  ScLock *lock;
};

CtClockRecord::CtClockRecord(CtClockCallback function, void *argument, const CtDate &value)
    : target{function}, date(value), context{argument}, direct(1) {
}

CtClockRecord::CtClockRecord(ScMessageQueue *queue, ScMsgCode messageType, const CtDate &value)
    : target{.queue = queue}, date(value), context{.messageType = messageType}, direct(0) {
}

CtClockNode::CtClockNode() : next(this), previous(this) {
}

CtClockNode::CtClockNode(const CtClockRecord &value) : entry(value) {
}

CtClockList::CtClockList() : head(new CtClockNode) {
}

// Unlink the node after its callback has returned and release its owned storage.
static void CtEraseClockNode(CtClockNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

// Release live registrations and the head after the clock's later members have been destroyed.
CtClockList::~CtClockList() {
  while (head->next != head)
    CtEraseClockNode(head->next);
  delete head;
}

// Append a copied registration in the original callback insertion order.
void CtClockList::Append(const CtClockRecord &entry) {
  CtClockNode *node = new CtClockNode(entry);
  node->next = head;
  node->previous = head->previous;
  head->previous->next = node;
  head->previous = node;
}

// Deliver the active typed variant through its actual callback or message-queue interface.
static void CtDeliverClock(const CtClockRecord &entry, u32 unit) {
  if (entry.direct)
    entry.target.function(entry.context.argument);
  else {
    ScMessage message = {entry.context.messageType, unit, 0, 0};
    entry.target.queue->PushBack(message);
  }
}

CtClock::CtClock()
    : minute(0), hour(0), rate(1440), paused(0), accumulated(0), lock(nullptr), references(0), running(0) {
  baseline = CtPlatformTicks();
  dayPeriod = kCtClockMillisecondsPerDay / rate;
  hourPeriod = dayPeriod / 24u;
  minutePeriod = hourPeriod / 60u;
  if (minutePeriod < 50)
    minutePeriod = 50;
  if (hourPeriod < 50)
    hourPeriod = 50;
  lock = new ScLock;
}

CtClock::~CtClock() {
  if (running)
    Shutdown();
  delete lock;
  lock = nullptr;
}

// Publish this clock for the two original interface identifiers, retaining it only on success.
bool CtClock::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtClock);
  HANDLE_QUERY_INTERFACE(IID_CtClock, CtClock);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtClock::AddRef() {
  CtClockGuard guard(lock);
  return ++references;
}

// Decrement shared ownership or unlock before destroying the final receiver, including a zero-count receiver.
u32 CtClock::Release() {
  lock->Lock();
  if (references > 1) {
    --references;
    lock->Unlock();
    return references;
  }
  lock->Unlock();
  delete this;
  return 0;
}

bool CtClock::Init() {
  CtClockGuard guard(lock);
  source.SetCallback(Dispatch, this);
  source.Start(1000, nullptr);
  running = 1;
  return true;
}

bool CtClock::Shutdown() {
  source.Stop();
  running = 0;
  return true;
}

// Copy only the serial date word, retaining the output object's existing virtual interface.
bool CtClock::GetCurrentClockDate(CtDate *out) {
  CtClockGuard guard(lock);
  out->serial = date.serial;
  return true;
}

// Replace the current date word without advancing time or sending notifications.
bool CtClock::SetCurrentClockDate(CtDate *value) {
  CtClockGuard guard(lock);
  date.serial = value->serial;
  return true;
}

bool CtClock::GetCurrentClockTime(CtTime *out) {
  CtClockGuard guard(lock);
  u32 savedMinute = minute, savedHour = hour;
  CtDate today;
  out->timestamp = CtTime(today, savedHour, savedMinute, 0).timestamp;
  return true;
}

// Snapshot the input word before assigning the decoded hour and minute, preserving input aliases.
bool CtClock::SetCurrentClockTime(CtTime *value) {
  CtClockGuard guard(lock);
  CtTime time(value->timestamp);
  hour = time.Hour();
  minute = time.Minute();
  return true;
}

// Read the configured day-rate through the original output pointer under the clock lock.
bool CtClock::GetClockRate(u32 *out) {
  CtClockGuard guard(lock);
  *out = rate;
  return true;
}

// Recalculate the original day, hour and minute periods, retaining the fifty-millisecond lower bounds.
bool CtClock::SetClockRate(u32 value) {
  CtClockGuard guard(lock);
  rate = value;
  dayPeriod = kCtClockMillisecondsPerDay / value;
  hourPeriod = dayPeriod / 24u;
  minutePeriod = hourPeriod / 60u;
  if (minutePeriod < 50)
    minutePeriod = 50;
  if (hourPeriod < 50)
    hourPeriod = 50;
  return true;
}

// Accumulate elapsed wall time on the first pause while preserving wrapped nested pause counts.
bool CtClock::PauseClock() {
  CtClockGuard guard(lock);
  if (++paused == 1)
    accumulated += CtPlatformTicks() - baseline;
  return true;
}

// Remove one existing pause and reset the wall-time baseline only when the final pause is removed.
bool CtClock::UnpauseClock() {
  CtClockGuard guard(lock);
  if (!paused)
    return false;
  if (!--paused)
    baseline = CtPlatformTicks();
  return true;
}

u32 CtClock::GetPauseCount() {
  CtClockGuard guard(lock);
  return paused;
}

// Retain the original virtual pause/unpause transitions when assigning a new nesting count.
bool CtClock::SetPauseCount(u32 count) {
  CtClockGuard guard(lock);
  if (paused != count) {
    if (paused && !count) {
      paused = 1;
      UnpauseClock();
      return true;
    }
    if (!paused)
      PauseClock();
    paused = count;
  }
  return true;
}

bool CtClock::AddNotification(CtClockCallback function, void *argument, CtClockUnit type) {
  CtClockGuard guard(lock);
  CtDate today;
  u32 unit = (u32)type;
  if (unit < 1 || unit > 6)
    return false;
  lists[unit - 1].Append(CtClockRecord(function, argument, today));
  return true;
}

bool CtClock::AddNotification(CtClockCallback function, void *argument, CtDate *value) {
  CtClockGuard guard(lock);
  if (value->serial <= date.serial)
    return false;
  lists[6].Append(CtClockRecord(function, argument, *value));
  return true;
}

bool CtClock::AddNotification(ScMessageQueue *queue, CtClockUnit type, ScMsgCode messageType) {
  CtClockGuard guard(lock);
  CtDate today;
  u32 unit = (u32)type;
  if (unit < 1 || unit > 6)
    return false;
  lists[unit - 1].Append(CtClockRecord(queue, messageType, today));
  return true;
}

bool CtClock::AddNotification(ScMessageQueue *queue, CtDate *value, ScMsgCode messageType) {
  CtClockGuard guard(lock);
  if (value->serial <= date.serial)
    return false;
  lists[6].Append(CtClockRecord(queue, messageType, *value));
  return true;
}

static bool CtClockTargetMatches(const CtClockRecord &entry, const void *representation, usize size) {
  return memcmp(&entry.target, representation, size) == 0;
}

// Remove the first matching target representation and optional date from a selected original list.
static bool CtRemoveClock(CtClockList *list, const void *target, usize size, const CtDate *date) {
  CtClockNode *head = list->head;
  for (CtClockNode *node = head->next; node != head; node = node->next)
    if (CtClockTargetMatches(node->entry, target, size) && (!date || node->entry.date.serial == date->serial)) {
      CtEraseClockNode(node);
      return true;
    }
  return false;
}

// Remove the first recurring callback identity match, regardless of the stored direct flag.
bool CtClock::RemoveNotification(CtClockCallback function, CtClockUnit type) {
  CtClockGuard guard(lock);
  u32 unit = (u32)type;
  return unit >= 1 && unit <= 6 && CtRemoveClock(lists + unit - 1, &function, sizeof(function), nullptr);
}

bool CtClock::RemoveNotification(CtClockCallback function, CtDate *value) {
  CtClockGuard guard(lock);
  return CtRemoveClock(lists + 6, &function, sizeof(function), value);
}

bool CtClock::RemoveNotification(ScMessageQueue *queue, CtClockUnit type) {
  CtClockGuard guard(lock);
  u32 unit = (u32)type;
  return unit >= 1 && unit <= 6 && CtRemoveClock(lists + unit - 1, &queue, sizeof(queue), nullptr);
}

bool CtClock::RemoveNotification(ScMessageQueue *queue, CtDate *value) {
  CtClockGuard guard(lock);
  return CtRemoveClock(lists + 6, &queue, sizeof(queue), value);
}

// Walk the live recurring list in insertion order, loading each next link after callback completion.
void CtClock::Notify(u32 unit) {
  CtClockNode *head = lists[unit - 1].head;
  for (CtClockNode *node = head->next; node != head; node = node->next)
    CtDeliverClock(node->entry, unit);
}

// Advance one minute, deliver minute callbacks, then roll exactly sixty minutes into an hour.
void CtClock::TickMinute() {
  CtClockGuard guard(lock);
  ++minute;
  Notify(1);
  if (minute == 60) {
    minute = 0;
    TickHour();
  }
}

// Advance one hour, deliver hour callbacks, then roll exactly twenty-four hours into a day.
void CtClock::TickHour() {
  CtClockGuard guard(lock);
  ++hour;
  Notify(2);
  if (hour == 24) {
    hour = 0;
    TickDay();
  }
}

// Advance the date and publish day/week/month/year notifications before removing completed dated notifications.
void CtClock::TickDay() {
  CtClockGuard guard(lock);
  ++date.serial;
  Notify(3);
  if (date.DayOfWeek() == 1)
    Notify(4);
  if (date.DayOfMonth() == 1)
    Notify(5);
  if (date.DayOfYear() == 1)
    Notify(6);
  CtClockNode *head = lists[6].head, *node = head->next;
  while (node != head)
    if (date.serial == node->entry.date.serial) {
      CtDeliverClock(node->entry, 7);
      CtClockNode *next = node->next;
      CtEraseClockNode(node);
      node = next;
    } else
      node = node->next;
}

// Consume elapsed wall time using the original day-only fast path or minute path and callback-adjusted periods.
void CtClock::Pump() {
  CtClockGuard guard(lock);
  if (!paused) {
    u32 now = CtPlatformTicks(), elapsed = now - baseline;
    baseline = now;
    accumulated += elapsed;
    if (lists[0].head->next == lists[0].head && lists[1].head->next == lists[1].head) {
      while (accumulated >= dayPeriod) {
        TickDay();
        accumulated -= dayPeriod;
      }
    } else
      while (accumulated >= minutePeriod) {
        TickMinute();
        accumulated -= minutePeriod;
      }
  }
}

void CtClock::Dispatch(void *argument) {
  static_cast<CtClock *>(argument)->Pump();
}

ScCounterScale ScCounterCoefficients = {};
ScCounterScale ScCounterInverseCoefficients = {};
u32 ScCounterResolution;

// Read the native runtime's rounding direction with the game's established 53-bit precision configuration.
u16 ScCounterControl() {
  switch (fegetround()) {
  case FE_DOWNWARD:
    return 0x067f;
  case FE_UPWARD:
    return 0x0a7f;
  case FE_TOWARDZERO:
    return 0x0e7f;
  default:
    return 0x027f;
  }
}

void ScStopwatch::ConvertInternalToCycles(i64 ticks, i64 *out) const {
  *out = ScScaleCounterTicks(ScCounterCoefficients, 0, ticks, ScCounterControl());
}

void ScStopwatch::ConvertInternalToMicroseconds(i64 ticks, i64 *out) const {
  *out = ScScaleCounterTicks(ScCounterCoefficients, 1, ticks, ScCounterControl());
}

void ScStopwatch::ConvertInternalToMilliseconds(i64 ticks, i64 *out) const {
  *out = ScScaleCounterTicks(ScCounterCoefficients, 2, ticks, ScCounterControl());
}

void ScStopwatch::ConvertInternalToSeconds(i64 ticks, i64 *out) const {
  *out = ScScaleCounterTicks(ScCounterCoefficients, 3, ticks, ScCounterControl());
}

void ScStopwatch::ConvertInternalToMinutes(i64 ticks, i64 *out) const {
  *out = ScScaleCounterTicks(ScCounterCoefficients, 4, ticks, ScCounterControl());
}

// The original timer stores the same five coefficients used by its integer converters.
static f64 ScCounterFloatFactor(u32 unit) {
  f64 value;
  memcpy(&value, &ScCounterCoefficients.factors[unit], sizeof(value));
  return value;
}

void ScStopwatch::ConvertInternalToCycles(i64 ticks, f32 *out) const {
  *out = (f32)((f64)ticks * ScCounterFloatFactor(0));
}

void ScStopwatch::ConvertInternalToMicroseconds(i64 ticks, f32 *out) const {
  *out = (f32)((f64)ticks * ScCounterFloatFactor(1));
}

void ScStopwatch::ConvertInternalToMilliseconds(i64 ticks, f32 *out) const {
  *out = (f32)((f64)ticks * ScCounterFloatFactor(2));
}

void ScStopwatch::ConvertInternalToSeconds(i64 ticks, f32 *out) const {
  *out = (f32)((f64)ticks * ScCounterFloatFactor(3));
}

void ScStopwatch::ConvertInternalToMinutes(i64 ticks, f32 *out) const {
  *out = (f32)((f64)ticks * ScCounterFloatFactor(4));
}

typedef struct ScCounterWide {
  u64 low, high;
} ScCounterWide;

// Count the significant bits of a nonnegative counter magnitude.
static u32 ScCounterBits64(u64 n) {
  u32 bits = 0;
  while (n) {
    ++bits;
    n >>= 1;
  }
  return bits;
}

// Decode the supported original x87 significand widths from the native configuration word.
static u32 ScCounterPrecision(u16 control) {
  switch ((control >> 8) & 3) {
  case 0:
    return 24;
  case 2:
    return 53;
  case 3:
    return 64;
  default:
    return 0;
  }
}

// Shift a single word into the two-limb product representation without undefined full-width shifts.
static ScCounterWide ScCounterShiftLeft(u64 n, u32 shift) {
  ScCounterWide out;
  if (!shift) {
    out.low = n;
    out.high = 0;
  } else if (shift >= 64) {
    out.low = 0;
    out.high = n << (shift - 64);
  } else {
    out.low = n << shift;
    out.high = n >> (64 - shift);
  }
  return out;
}

// Multiply the counter magnitude and stored coefficient significand using two ordinary integer limbs.
static ScCounterWide ScCounterMultiply(u64 a, u64 b) {
  const u64 mask = UINT32_MAX;
  u64 a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
  u64 t = a0 * b0, low = t & mask, carry = t >> 32, mid, high;
  ScCounterWide out;
  t = a1 * b0 + carry;
  mid = t & mask;
  high = t >> 32;
  t = a0 * b1 + mid;
  out.high = a1 * b1 + high + (t >> 32);
  out.low = (t << 32) + low;
  return out;
}

static u64 ScCounterDivide(ScCounterWide n, u64 denominator, u64 *remainder) {
  u64 q = 0, r = 0;
  u32 i = 128;
  /* Callers ensure denominator <= INT64_MAX and quotient fits 64 bits. */
  while (i--) {
    u64 bit = i >= 64 ? (n.high >> (i - 64)) & 1 : (n.low >> i) & 1;
    r = (r << 1) | bit;
    q <<= 1;
    if (r >= denominator) {
      r -= denominator;
      q |= 1;
    }
  }
  *remainder = r;
  return q;
}

// Round a significand upward and renormalize its exponent when the carry crosses its precision.
static u64 ScCounterIncrement(u64 q, u32 p, i32 *exponent) {
  ++q;
  if (!q || (p < 64 && q == (UINT64_C(1) << p))) {
    q = UINT64_C(1) << (p - 1);
    ++*exponent;
  }
  return q;
}

// Apply the original rounding direction to the discarded fraction and retained least-significant bit.
static i32 ScCounterRoundUp(u32 rounding, i32 negative, i32 half, i32 sticky, u64 q) {
  switch (rounding) {
  case 0:
    return half && (sticky || (q & 1));
  case 1:
    return negative && (half || sticky);
  case 2:
    return !negative && (half || sticky);
  default:
    return 0;
  }
}

// Compute a rounded rational coefficient at the selected original significand precision.
static u64 ScCounterRatio(u64 numerator, u64 denominator, u32 p, u32 rounding, i32 *exponent) {
  i32 e = (i32)ScCounterBits64(numerator) - (i32)ScCounterBits64(denominator);
  i32 shift;
  u64 remainder, q;
  ScCounterWide n;
  if (e >= 0) {
    if (numerator < (denominator << e))
      --e;
  } else if ((numerator << -e) < denominator)
    --e;
  shift = (i32)p - 1 - e;
  if (shift >= 0)
    n = ScCounterShiftLeft(numerator, (u32)shift);
  else {
    n.low = numerator;
    n.high = 0;
    denominator <<= -shift;
  }
  q = ScCounterDivide(n, denominator, &remainder);
  *exponent = e;
  if (rounding == 0) {
    if (remainder > denominator - remainder || (remainder == denominator - remainder && (q & 1)))
      q = ScCounterIncrement(q, p, exponent);
  } else if (rounding == 2 && remainder)
    q = ScCounterIncrement(q, p, exponent);
  return q;
}

// Convert an original binary64 numerator/frequency ratio into the stored binary64 coefficient bits.
static u64 ScCounterFactor(u64 numerator_bits, u64 frequency, u32 p, u32 rounding) {
  u64 numerator = (numerator_bits & UINT64_C(0x000fffffffffffff)) | (UINT64_C(1) << 52);
  i32 exponent, source_exponent = (i32)(numerator_bits >> 52) - 1023 - 52;
  u64 q = ScCounterRatio(numerator, frequency, p, rounding, &exponent);
  exponent += source_exponent;
  if (p > 53) {
    u64 lost = q & ((UINT64_C(1) << (p - 53)) - 1);
    u64 halfway = UINT64_C(1) << (p - 54);
    q >>= p - 53;
    if (ScCounterRoundUp(rounding, 0, lost >= halfway, lost && lost != halfway, q))
      q = ScCounterIncrement(q, 53, &exponent);
  } else if (p < 53)
    q <<= 53 - p;
  return ((u64)(exponent + 1023) << 52) | (q & UINT64_C(0x000fffffffffffff));
}

// Build coefficients from a native frequency/configuration boundary. Undefined original frequency/control inputs are fatal.
ScCounterScale ScBuildCounterScale(u64 frequency, u16 control) {
  static const u64 kNumerators[5] = {UINT64_C(0x41cdcd6500000000), UINT64_C(0x412e848000000000),
      UINT64_C(0x408f400000000000), UINT64_C(0x3ff0000000000000), UINT64_C(0x3f91111111111111)};
  ScCounterScale value;
  u32 p = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  if (!frequency || frequency > INT64_MAX || !p)
    ScFatal(SC_ERROR_INVALID, "Invalid native performance-counter configuration");
  for (u32 i = 0; i < 5; ++i)
    value.factors[i] = ScCounterFactor(kNumerators[i], frequency, p, rounding);
  return value;
}

// Report whether discarded product bits contain a nonzero fraction below the rounding bit.
static i32 ScCounterLowBits(ScCounterWide n, u32 count) {
  if (!count)
    return 0;
  if (count > 64)
    return n.low || (n.high & ((UINT64_C(1) << (count - 64)) - 1));
  if (count == 64)
    return n.low != 0;
  return (n.low & ((UINT64_C(1) << count) - 1)) != 0;
}

// Extract the retained word of a two-limb product without undefined full-width shifts.
static u64 ScCounterShiftRight(ScCounterWide n, u32 count) {
  if (!count)
    return n.low;
  if (count >= 64)
    return n.high >> (count - 64);
  return (n.low >> count) | (n.high << (64 - count));
}

// Truncate the rounded arithmetic significand toward zero, preserving the full defined signed range.
static i64 ScCounterTruncate(u64 q, u32 p, i32 exponent, bool negative) {
  if (exponent < 0)
    return 0;
  assert((exponent < 63 || (exponent == 63 && negative && q == (UINT64_C(1) << (p - 1)))) &&
      "Floating conversion exceeds the defined signed range");
  u64 magnitude = exponent >= i32(p) - 1 ? q << (exponent - i32(p) + 1) : q >> (i32(p) - 1 - exponent);
  if (negative)
    return magnitude == (UINT64_C(1) << 63) ? INT64_MIN : -i64(magnitude);
  return i64(magnitude);
}

// Multiply an exact signed integer by a binary64 coefficient before the original precision rounding and truncation.
// Both counter scaling and binary32 water arithmetic reuse this boundary without first narrowing the integer.
static i64 ScMultiplyInt64Factor(u64 f, i64 ticks, u16 control) {
  u64 mantissa, magnitude, q;
  ScCounterWide product;
  u32 length, p = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  i32 exponent, negative = (ticks < 0) != bool(f >> 63);
  assert(p && "Unsupported counter precision");
  f &= UINT64_C(0x7fffffffffffffff);
  // A zero coefficient produces zero without normalizing a nonexistent significand.
  if (!(f & UINT64_C(0x7fffffffffffffff)))
    return 0;
  assert(!(f >> 63) && (f >> 52) && (f >> 52) != 0x7ff && "Invalid counter coefficient");
  if (!ticks)
    return 0;
  magnitude = ticks < 0 ? UINT64_C(0) - (u64)ticks : (u64)ticks;
  mantissa = (f & UINT64_C(0x000fffffffffffff)) | (UINT64_C(1) << 52);
  product = ScCounterMultiply(magnitude, mantissa);
  length = product.high ? 64 + ScCounterBits64(product.high) : ScCounterBits64(product.low);
  exponent = (i32)length - 1 + (i32)(f >> 52) - 1023 - 52;
  if (length > p) {
    u32 lost = length - p;
    i32 half = (i32)(ScCounterShiftRight(product, lost - 1) & 1);
    i32 sticky = ScCounterLowBits(product, lost - 1);
    q = ScCounterShiftRight(product, lost);
    if (ScCounterRoundUp(rounding, negative, half, sticky, q))
      q = ScCounterIncrement(q, p, &exponent);
  } else
    q = product.low << (p - length);
  return ScCounterTruncate(q, p, exponent, negative != 0);
}

i64 ScScaleCounterTicks(const ScCounterScale &scale, u32 unit, i64 ticks, u16 control) {
  return ScMultiplyInt64Factor(scale.factors[unit < 4 ? unit : 4], ticks, control);
}

i64 ScMultiplyInt64Float(i64 value, f32 factor) {
  return ScMultiplyInt64Float(value, factor, ScCounterControl());
}

// This uses integer significands and does not change the host floating environment.
i64 ScMultiplyInt64Float(i64 value, f32 factor, u16 control) {
  f64 promoted = factor;
  u64 bits;
  memcpy(&bits, &promoted, sizeof(bits));
  return ScMultiplyInt64Factor(bits, value, control);
}

// Extract a positive normal binary32 significand and its power-of-two multiplier.
// The credits inputs are finite and positive; zero is handled before normalization.
static u32 ScCounterFloatParts(f32 value, i32 *power) {
  u32 bits;
  memcpy(&bits, &value, sizeof(bits));
  assert(!(bits >> 31) && (bits >> 23) && (bits >> 23) < 255);
  *power = i32(bits >> 23) - 150;
  return (bits & 0x7fffff) | 0x800000;
}

// Normalize an exact integer significand and round it to the selected x87 arithmetic precision.
static u64 ScCounterRoundInteger(u64 value, i32 power, u32 precision, u32 rounding, bool negative, i32 *exponent) {
  u32 length = ScCounterBits64(value);
  assert(length && precision);
  *exponent = i32(length) - 1 + power;
  if (length <= precision)
    return value << (precision - length);
  u32 lost = length - precision;
  u64 result = value >> lost, fraction = value & ((UINT64_C(1) << lost) - 1), halfway = UINT64_C(1) << (lost - 1);
  if (ScCounterRoundUp(rounding, negative, fraction >= halfway, fraction && fraction != halfway, result))
    result = ScCounterIncrement(result, precision, exponent);
  return result;
}

void ScMultiplyUint32Float(u32 value, f32 factor, f32 threshold, u16 control, u32 *integer, f32 *stored,
    bool *less) {
  u32 precision = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  i32 power, exponent, threshold_power;
  u64 mantissa = ScCounterFloatParts(factor, &power),
      threshold_mantissa = ScCounterFloatParts(threshold, &threshold_power);
  assert(precision);
  if (!value) {
    *integer = 0;
    *stored = 0;
    *less = true;
    return;
  }
  u64 q = ScCounterRoundInteger(u64(value) * mantissa, power, precision, rounding, false, &exponent);
  *integer = u32(ScCounterTruncate(q, precision, exponent, false));
  i32 threshold_exponent = threshold_power + 23;
  u64 comparable = threshold_mantissa << (precision - 24);
  *less = exponent < threshold_exponent || (exponent == threshold_exponent && q < comparable);
  u32 lost = precision - 24;
  u64 stored_q = q >> lost;
  if (lost) {
    u64 fraction = q & ((UINT64_C(1) << lost) - 1), halfway = UINT64_C(1) << (lost - 1);
    if (ScCounterRoundUp(rounding, false, fraction >= halfway, fraction && fraction != halfway, stored_q))
      stored_q = ScCounterIncrement(stored_q, 24, &exponent);
  }
  assert(exponent >= -126 && exponent <= 127);
  u32 bits = (u32(exponent + 127) << 23) | (u32(stored_q) & 0x7fffff);
  memcpy(stored, &bits, sizeof(bits));
}

// The credits operands keep the exact subtraction within a signed 64-bit significand.
i64 ScFloatDifferenceQuotient(f32 value, u32 integer, f32 divisor, u16 control) {
  u32 precision = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  i32 power, divisor_power, exponent, quotient_exponent;
  u64 divisor_mantissa = ScCounterFloatParts(divisor, &divisor_power), magnitude;
  bool negative = false;
  assert(precision);
  if (!value) {
    if (!integer)
      return 0;
    magnitude = integer;
    negative = true;
    power = 0;
  } else {
    u64 mantissa = ScCounterFloatParts(value, &power);
    if (!integer) {
      magnitude = mantissa;
    } else {
      i64 difference;
      if (power >= 0) {
        assert(power < 39 && (mantissa << power) <= INT64_MAX);
        difference = i64(mantissa << power) - i64(integer);
        power = 0;
      } else {
        assert(-power < 32 && (u64(integer) << -power) <= INT64_MAX);
        difference = i64(mantissa) - i64(u64(integer) << -power);
      }
      if (!difference)
        return 0;
      negative = difference < 0;
      magnitude = negative ? u64(0) - u64(difference) : u64(difference);
    }
  }
  u64 q = ScCounterRoundInteger(magnitude, power, precision, rounding, negative, &exponent);
  u32 quotient_rounding = negative && (rounding == 1 || rounding == 2) ? 3 - rounding : rounding;
  u64 quotient = ScCounterRatio(q, divisor_mantissa, precision, quotient_rounding, &quotient_exponent);
  quotient_exponent += exponent - i32(precision) + 1 - divisor_power;
  return ScCounterTruncate(quotient, precision, quotient_exponent, negative);
}

// Divide the exact integer by the binary32 significand before rounding to the original arithmetic precision.
// Signed rounding precedes the separate truncation used by the original floating-to-integer helper.
i64 ScDivideInt64Float(i64 value, f32 factor) {
  f64 promoted = factor;
  u64 bits;
  memcpy(&bits, &promoted, sizeof(bits));
  bool negative = (value < 0) != bool(bits >> 63);
  bits &= UINT64_C(0x7fffffffffffffff);
  assert((bits >> 52) && (bits >> 52) != 0x7ff && "Invalid floating divisor");
  if (!value)
    return 0;
  u16 control = ScCounterControl();
  u32 precision = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  if (negative && (rounding == 1 || rounding == 2))
    rounding = 3 - rounding;
  i32 exponent;
  u64 magnitude = value < 0 ? u64(0) - u64(value) : u64(value),
      significand = (bits & UINT64_C(0x000fffffffffffff)) | (UINT64_C(1) << 52),
      quotient = ScCounterRatio(magnitude, significand, precision, rounding, &exponent);
  exponent -= i32(bits >> 52) - 1023 - 52;
  return ScCounterTruncate(quotient, precision, exponent, negative);
}

// The two-limb product keeps all discarded bits so directed rounding and double rounding match the original stores.
static u64 ScCounterProductFactor(u64 frequency, u64 factor, u32 precision, u32 rounding) {
  u64 mantissa = (factor & UINT64_C(0x000fffffffffffff)) | (UINT64_C(1) << 52), q;
  ScCounterWide product = ScCounterMultiply(frequency, mantissa);
  u32 length = product.high ? 64 + ScCounterBits64(product.high) : ScCounterBits64(product.low);
  i32 exponent = (i32)length - 1 + (i32)(factor >> 52) - 1023 - 52;
  if (length > precision) {
    u32 lost = length - precision;
    i32 half = (i32)(ScCounterShiftRight(product, lost - 1) & 1);
    i32 sticky = ScCounterLowBits(product, lost - 1);
    q = ScCounterShiftRight(product, lost);
    if (ScCounterRoundUp(rounding, 0, half, sticky, q))
      q = ScCounterIncrement(q, precision, &exponent);
  } else
    q = product.low << (precision - length);
  if (precision > 53) {
    u32 lost = precision - 53;
    u64 remainder = q & ((UINT64_C(1) << lost) - 1), halfway = UINT64_C(1) << (lost - 1);
    q >>= lost;
    if (ScCounterRoundUp(rounding, 0, remainder >= halfway, remainder && remainder != halfway, q))
      q = ScCounterIncrement(q, 53, &exponent);
  } else if (precision < 53)
    q <<= 53 - precision;
  return ((u64)(exponent + 1023) << 52) | (q & UINT64_C(0x000fffffffffffff));
}

// Build the five inverse coefficients written by the original initializer after its forward divisions.
// Ticks per second is a direct FILD/FST store, so it skips the arithmetic precision reduction used by the multiplications.
ScCounterScale ScBuildInverseCounterScale(u64 frequency, u16 control) {
  static const u64 kFactors[5] = {UINT64_C(0x3e112e0be826d695), UINT64_C(0x3eb0c6f7a0b5ed8d),
      UINT64_C(0x3f50624dd2f1a9fc), UINT64_C(0x3ff0000000000000), UINT64_C(0x404e000000000000)};
  ScCounterScale value;
  u32 precision = ScCounterPrecision(control), rounding = (control >> 10) & 3;
  if (!frequency || frequency > INT64_MAX || !precision)
    ScFatal(SC_ERROR_INVALID, "Invalid native performance-counter configuration");
  for (u32 i = 0; i < 5; ++i)
    value.factors[i] = ScCounterProductFactor(frequency, kFactors[i], i == 3 ? 64 : precision, rounding);
  return value;
}

// Query the native high-resolution counter for the timer's platform boundary. Failed queries write zero.
void ScReadCounter(i64 *out) {
  *out = static_cast<i64>(SDL_GetPerformanceCounter());
}

// Prime the original stopwatch sequence and retain the smaller of its final two raw calibration differences.
static u32 ScCalibrateCounter() {
  ScStopwatch timer(ScStopwatch::Cycles, 0);
  ScThread::SleepCurrentThread(0);
  timer.Start();
  timer.Stop();
  i64 first, second;
  timer.GetElapsedInternal(&first);
  timer.Restart();
  timer.Stop();
  timer.GetElapsedInternal(&first);
  timer.Restart();
  timer.Stop();
  timer.GetElapsedInternal(&first);
  timer.Restart();
  timer.Stop();
  timer.GetElapsedInternal(&second);
  if (second < first)
    first = second;
  ScCounterResolution = (u32)first;
  return (u32)first;
}

// Initialize both original conversion directions before calibrating; a failed frequency query retains all prior values.
// This restores the complete effect phase of CRT target1000D927; its private source declaration remains unresolved.
void ScInitializeCounterPlatform() {
  u64 frequency = SDL_GetPerformanceFrequency();
  if (frequency) {
    u16 control = ScCounterControl();
    ScCounterCoefficients = ScBuildCounterScale(frequency, control);
    ScCounterInverseCoefficients = ScBuildInverseCounterScale(frequency, control);
    ScCounterResolution = ScCalibrateCounter();
  }
}

struct ScCounterBootstrap {
  // Run frequency calibration during module startup, matching CRT table17040 -> D922 -> D927.
  // The tick scheduler then starts with populated conversion coefficients.
  ScCounterBootstrap() {
    ScInitializeCounterPlatform();
  }
};

static ScCounterBootstrap ScCounterBootstrapState;

ScRandom::ScRandom(u32 seed) {
  SetSeed(seed);
  iset = 0;
}

// Replace the sentinel seed with the original multimedia tick, then copy it to both seed words.
// Retail cRZRandom::Seed(unsigned long) proves the void return; cached Gaussian state is intentionally preserved.
void ScRandom::SetSeed(u32 seed) {
  if (seed == kScRandomSeedFromClock)
    seed = ScMultimediaMilliseconds();
  integerSeed = seed;
  currentDoubleSeed = seed;
}

// Advance the original linear recurrence and return the middle word of its full 64-bit product and carry.
u32 ScRandom::Next() {
  u64 next = u64(kScRandomMultiplier) * integerSeed + kScRandomIncrement;
  integerSeed = (u32)next;
  return (u32)(next >> 16);
}

// Combine the CRT time relative to the original normalized January-2000 anchor with four actual random bits.
u32 CtSeedFromTime() {
  i32 now = ScTime32();
  ScRandom random(kScRandomSeedFromClock);
  tm anchor = {};
  anchor.tm_mon = 1;
  anchor.tm_wday = 1;
  anchor.tm_yday = 1;
  anchor.tm_isdst = 1;
  anchor.tm_year = 100;
  i32 epoch = ScMakeLocalTime32(&anchor), zone = ScTimezoneSeconds();
  u32 base = 16u * ((u32)zone + (u32)now - (u32)epoch);
  return base | (random.Next() & 15);
}

// Generate a bounded word using modulo below the original threshold and rejection sampling at or above it.
u32 ScRandom::Bounded(u32 bound) {
  if (!bound)
    return 0;
  u32 candidate;
  do {
    candidate = Next();
  } while (bound >= kScRandomModuloThreshold && candidate > bound);
  return bound < kScRandomModuloThreshold ? candidate % bound : candidate;
}

i32 ScRandom::Range(i32 minimum, i32 maximum) {
  return (i32)((u32)minimum + Bounded((u32)maximum - (u32)minimum));
}

// Select the smaller of two bounded integer samples, then choose its sign with a third random draw.
i32 ScRandom::RandomSint32RangeGaussian(i32 minimum, i32 maximum) {
  u32 half = u32(i32(u32(maximum) - u32(minimum)) / 2);
  i32 first = i32(Bounded(half)), second = i32(Bounded(half));
  if (first < second)
    second = first;
  if (Bounded(2))
    second = i32(0u - u32(second));
  return i32(u32(minimum) + u32(second) + half);
}

// Advance the independent double stream, substituting the original nonzero seed when that stream is zero.
// Preserve the source multiplier constant, which is slightly above the exact reciprocal of2^32.
f64 ScRandom::RandomDoubleUniform() {
  if (!currentDoubleSeed)
    currentDoubleSeed = kScRandomFallbackRealSeed;
  currentDoubleSeed *= kScRandomRealMultiplier;
  return f64(currentDoubleSeed) * kScRandomRealScale;
}

// Consume a cached Gaussian without advancing either seed, or generate a pair using the original polar rejection loop.
// Initialize the saved companion only on an accepted pair; constructor and reseeding never read or overwrite it.
f64 ScRandom::RandomDoubleGaussian() {
  if (iset) {
    iset = 0;
    return gset;
  }
  f64 first, second, radius;
  do {
    f64 value = RandomDoubleUniform();
    first = value + value - 1.0;
    value = RandomDoubleUniform();
    second = value + value - 1.0;
    radius = second * second + first * first;
  } while (radius >= 1.0 || radius == 0.0);
  f64 scale = sqrt(log(radius) * -2.0 / radius);
  iset = 1;
  gset = scale * first;
  return scale * second;
}

f64 ScRandom::RandomDoubleRangeUniform(f64 minimum, f64 maximum) {
  f64 value = RandomDoubleUniform();
  return value * (maximum - minimum) + minimum;
}

// Shift the real Gaussian by six and clamp to the source0/11.99 limits before interpolating across the supplied range.
// This uses the cached polar distribution, independently of the separate approximate GaussianFast routines.
f64 ScRandom::RandomDoubleRangeGaussian(f64 minimum, f64 maximum) {
  f64 value = RandomDoubleGaussian() + 6.0;
  if (value >= 12.0)
    value = 11.99;
  if (value < 0.0)
    value = 0.0;
  return (maximum - minimum) * (value * 0.08333333333333333) + minimum;
}

// Draw the original fast approximate distribution from two uniform samples and an independent sign bit.
// The installed routine centers on half the span without adding minimum; preserve that behavior rather than correcting it.
f64 ScRandom::RandomDoubleRangeGaussianFast(f64 minimum, f64 maximum) {
  f64 half = (maximum - minimum) * 0.5;
  f64 first = RandomDoubleUniform() * half, second = RandomDoubleUniform() * half;
  if (first < second)
    second = first;
  if (Bounded(2))
    second = -second;
  return second + half;
}

#if !defined(_WIN32)
struct ScSdlSemaphore {
  SDL_Mutex *mutex;
  SDL_Condition *condition;
  u32 permits;
};

// Create the SDL semaphore state while leaving unsupported named interprocess semaphores explicit.
static void *ScPlatformSemaphoreCreate(i32 count, const ScString &name) {
  if (count <= 0)
    return nullptr;
  if (name.Strlen())
    ScNotImpl("named interprocess SDL semaphore");
  ScSdlSemaphore *semaphore = new ScSdlSemaphore{SDL_CreateMutex(), SDL_CreateCondition(), static_cast<u32>(count)};
  if (!semaphore->mutex || !semaphore->condition) {
    SDL_DestroyCondition(semaphore->condition);
    SDL_DestroyMutex(semaphore->mutex);
    delete semaphore;
    return nullptr;
  }
  return semaphore;
}

static void ScPlatformSemaphoreDestroy(void *handle) {
  if (ScSdlSemaphore *semaphore = static_cast<ScSdlSemaphore *>(handle)) {
    SDL_DestroyCondition(semaphore->condition);
    SDL_DestroyMutex(semaphore->mutex);
    delete semaphore;
  }
}

// Wait until the SDL semaphore can consume one permit.
static i32 ScPlatformSemaphoreWait(void *handle) {
  ScSdlSemaphore *semaphore = static_cast<ScSdlSemaphore *>(handle);
  if (!semaphore)
    return 0;
  SDL_LockMutex(semaphore->mutex);
  while (!semaphore->permits)
    SDL_WaitCondition(semaphore->condition, semaphore->mutex);
  --semaphore->permits;
  SDL_UnlockMutex(semaphore->mutex);
  return 1;
}

// Release permits up to the original maximum and wake waiting threads.
static i32 ScPlatformSemaphoreSignal(void *handle, i32 count, u32 maximum) {
  ScSdlSemaphore *semaphore = static_cast<ScSdlSemaphore *>(handle);
  if (!semaphore || count <= 0)
    return 0;
  SDL_LockMutex(semaphore->mutex);
  if (static_cast<u32>(count) > maximum - semaphore->permits) {
    SDL_UnlockMutex(semaphore->mutex);
    return 0;
  }
  semaphore->permits += static_cast<u32>(count);
  SDL_BroadcastCondition(semaphore->condition);
  SDL_UnlockMutex(semaphore->mutex);
  return 1;
}
#else
// Create the original Windows semaphore with identical initial and maximum counts.
// Windows SDL builds use the same kernel object so named handles retain cross-process sharing and native limits.
static void *ScPlatformSemaphoreCreate(i32 count, const ScString &name) {
  void *handle = CreateSemaphoreA(nullptr, count, count, name.Strlen() ? name.ToChar() : nullptr);
  // Preserve the installed code's handle comparison rather than replacing it with a GetLastError query.
  return !handle || handle == reinterpret_cast<void *>(uptr(ERROR_ALREADY_EXISTS)) ? nullptr : handle;
}

static void ScPlatformSemaphoreDestroy(void *handle) {
  if (handle)
    CloseHandle(handle);
}

static i32 ScPlatformSemaphoreWait(void *handle) {
  return WaitForSingleObject(handle, INFINITE) != WAIT_FAILED;
}

static i32 ScPlatformSemaphoreSignal(void *handle, i32 count, u32) {
  LONG previous = count;
  return ReleaseSemaphore(handle, count, &previous);
}
#endif

ScSemaphore::ScSemaphore() : maximum(1), initialized(0), handle(nullptr) {
}

ScSemaphore::ScSemaphore(const ScString &value) : name(value), maximum(1), initialized(0), handle(nullptr) {
}

ScSemaphore::~ScSemaphore() {
  ScPlatformSemaphoreDestroy(handle);
}

u32 ScSemaphore::Release() {
  delete this;
  return 0;
}

// Create a named or unnamed semaphore with identical initial and maximum counts, retaining an existing initialization.
bool ScSemaphore::Init(u32 count) {
  if (initialized)
    return true;
  maximum = count;
  handle = ScPlatformSemaphoreCreate(static_cast<i32>(count), name);
  if (!handle)
    return false;
  initialized = 1;
  return true;
}

i32 ScSemaphore::GetSemaphore() {
  return ScPlatformSemaphoreWait(handle);
}

// Drain the current maximum count through virtual waits, rereading that count after each successful wait.
i32 ScSemaphore::GetSemaphoreAll() {
  u32 waited = 0;
  if (!maximum)
    return 1;
  while (GetSemaphore())
    if (++waited >= maximum)
      return 1;
  return 0;
}

i32 ScSemaphore::ReleaseSemaphore(u32 count) {
  return ScPlatformSemaphoreSignal(handle, static_cast<i32>(count), maximum);
}

i32 ScSemaphore::ReleaseSemaphoreAll() {
  return ReleaseSemaphore(maximum);
}

i32 ScSemaphore::GetMaxReferenceCount() {
  return static_cast<i32>(maximum);
}

i32 ScSemaphore::IsValid() {
  return static_cast<i32>(initialized);
}

i32 ScSemaphore::GetSemaphoreHandle(void **out) {
  *out = handle;
  return 1;
}

ScStopwatch::ScStopwatch(TimerResolution value, u32 count)
    : unit(value), baseline(0), accumulated(0), samplesBegin(nullptr), samplesEnd(nullptr), samplesLimit(nullptr) {
  SetSampleBufferSize(count);
}

ScStopwatch::~ScStopwatch() {
  delete[] samplesBegin;
}

// Clear elapsed and accumulation words, bookkeeping and every configured sample without releasing capacity.
void ScStopwatch::Reset() {
  baseline = 0;
  accumulated = 0;
  sampleIndex = 0;
  sampleCount = 0;
  for (u32 i = 0; i < sampleBufferSize; ++i)
    samplesBegin[i] = {};
}

void ScStopwatch::Restart() {
  Reset();
  GetCurrentInternal(&baseline);
}

// Return zero without a baseline; otherwise scale the signed elapsed difference and return its low word.
u32 ScStopwatch::GetElapsedTime() const {
  if (!IsTimerRunning())
    return 0;
  i64 ticks, value;
  GetElapsedInternal(&ticks);
  ConvertInternalToUnitResolution(ticks, &value);
  return (u32)value;
}

bool ScStopwatch::IsTimerRunning() const {
  return baseline != 0;
}

// Start only when no baseline is present. An existing baseline and accumulation are retained.
void ScStopwatch::Start() {
  if (!IsTimerRunning()) {
    accumulated = 0;
    GetCurrentInternal(&baseline);
  }
}

// Wait using a fresh local stopwatch in this receiver's unit, leaving the receiver's timing and samples untouched.
// Yield through the platform scheduler between elapsed checks until the requested unsigned duration is reached.
void ScStopwatch::Delay(u32 duration) {
  ScStopwatch timer(unit, 0);
  timer.Start();
  while (timer.GetElapsedTime() < duration)
    ScYieldThread();
}

// Add the raw elapsed difference without clearing the baseline. Repeated calls keep the original cumulative behavior.
void ScStopwatch::Stop() {
  if (IsTimerRunning()) {
    i64 ticks;
    GetElapsedInternal(&ticks);
    accumulated += ticks;
  }
}

void ScStopwatch::GetElapsedInternal(i64 *out) const {
  GetCurrentInternal(out);
  *out -= baseline;
}

void ScStopwatch::GetCurrentInternal(i64 *out) const {
  ScReadCounter(out);
}

// Select the original unit converter; every unrecognized unit uses minutes.
void ScStopwatch::ConvertInternalToUnitResolution(i64 ticks, i64 *out) const {
  switch (unit) {
  case Cycles:
    return ConvertInternalToCycles(ticks, out);
  case Microseconds:
    return ConvertInternalToMicroseconds(ticks, out);
  case Milliseconds:
    return ConvertInternalToMilliseconds(ticks, out);
  case Seconds:
    return ConvertInternalToSeconds(ticks, out);
  default:
    return ConvertInternalToMinutes(ticks, out);
  }
}

void ScStopwatch::SetResolution(TimerResolution value) {
  if (unit != value)
    unit = value;
}

// Reserve the original trivial sample records and clear the configured range. The original reserve machinery is
// replaced by new[].
// Assert before a wrapped original allocation would be overrun while clearing the configured sample entries.
void ScStopwatch::SetSampleBufferSize(u32 count) {
  assert(count <= UINT32_MAX / sizeof(ScCounterSample));
  sampleIndex = 0;
  sampleCount = 0;
  sampleBufferSize = count;
  u32 capacity = samplesBegin ? (u32)(samplesLimit - samplesBegin) : 0;
  if (capacity < count) {
    u32 used = samplesBegin ? (u32)(samplesEnd - samplesBegin) : 0;
    ScCounterSample *replacement = new ScCounterSample[count];
    if (used)
      memcpy(replacement, samplesBegin, (usize)used * sizeof(ScCounterSample));
    delete[] samplesBegin;
    samplesBegin = replacement;
    samplesEnd = replacement + used;
    samplesLimit = replacement + count;
  }
  for (u32 i = 0; i < count; ++i)
    samplesBegin[i] = {};
}

// Publish elapsed time in the selected floating-point unit only when the timer is running.
// The original stopped-timer path leaves the caller's output untouched rather than writing zero.
void ScStopwatch::GetElapsedTime(f32 *out) const {
  if (IsTimerRunning()) {
    i64 ticks;
    GetElapsedInternal(&ticks);
    ConvertInternalToUnitResolution(ticks, out);
  }
}

// Dispatch the original float-output unit converters, retaining fractional elapsed units.
// Every unrecognized resolution selects minutes, matching the integer-output overload.
void ScStopwatch::ConvertInternalToUnitResolution(i64 ticks, f32 *out) const {
  switch (unit) {
  case Cycles:
    return ConvertInternalToCycles(ticks, out);
  case Microseconds:
    return ConvertInternalToMicroseconds(ticks, out);
  case Milliseconds:
    return ConvertInternalToMilliseconds(ticks, out);
  case Seconds:
    return ConvertInternalToSeconds(ticks, out);
  default:
    return ConvertInternalToMinutes(ticks, out);
  }
}

static ScLock CtSourceLock;
static ScArray<CtTickSource *> CtSourceMembers;

// Test membership without reading the source object. Pending deliveries may carry an already removed address.
static u32 CtFindSource(CtTickSource *source) {
  for (u32 i = 0; i < CtSourceMembers.size; ++i)
    if (CtSourceMembers.data[i] == source)
      return i;
  return CtSourceMembers.size;
}

// Establish original defaults and publish the source in the process registry under its recursive lock.
// The callback argument remains uninitialized until SetCallback, matching the original constructor.
CtTickSource::CtTickSource()
    : owner(nullptr), function(nullptr), setting(100), period(1000), running(0), oneShot(0), id(0) {
  CtSourceLock.Lock();
  *CtSourceMembers.Append() = this;
  CtSourceLock.Unlock();
}

// Remove registry membership before stopping and releasing the retained owner. Pending deliveries then skip this address.
CtTickSource::~CtTickSource() {
  CtSourceLock.Lock();
  u32 index = CtFindSource(this);
  if (index != CtSourceMembers.size) {
    for (u32 i = index + 1; i < CtSourceMembers.size; ++i)
      CtSourceMembers.data[i - 1] = CtSourceMembers.data[i];
    --CtSourceMembers.size;
  }
  CtSourceLock.Unlock();
  if (running)
    Stop();
  ScRelease(&owner);
  function = nullptr;
  argument = nullptr;
}

// Retain the current owner across a running-source restart. Inspect the one-shot flag after virtual Stop has run.
bool CtTickSource::SetSetting(u32 value) {
  ScUnknown *savedOwner = owner;
  if (savedOwner)
    savedOwner->AddRef();
  setting = value;
  if (running) {
    Stop();
    if (oneShot)
      StartOnce(0, nullptr);
    else
      Start(0, nullptr);
  }
  if (savedOwner) {
    if (owner)
      savedOwner->Release();
    owner = savedOwner;
  }
  return true;
}

bool CtTickSource::GetSetting(u32 *out) const {
  *out = setting;
  return true;
}

// Store the period and restart a running source while preserving the source's owner-reference ordering.
bool CtTickSource::SetPeriod(u32 value) {
  ScUnknown *savedOwner = owner;
  if (savedOwner)
    savedOwner->AddRef();
  period = value;
  if (running) {
    Stop();
    if (oneShot)
      StartOnce(0, nullptr);
    else
      Start(0, nullptr);
  }
  if (savedOwner) {
    if (owner)
      savedOwner->Release();
    owner = savedOwner;
  }
  return true;
}

bool CtTickSource::GetPeriod(u32 *out) const {
  *out = period;
  return true;
}

// Start periodic dispatch when a callback exists and the source is idle. Retain the supplied owner before scheduling.
bool CtTickSource::Start(u32 value, ScUnknown *retainedOwner) {
  if (running || !function)
    return false;
  if (retainedOwner) {
    retainedOwner->AddRef();
    owner = retainedOwner;
  }
  if (value)
    period = value;
  running = 1;
  id = CtSourceScheduler.Add(Dispatch, this, period, 0, 0);
  if (id)
    return true;
  running = 0;
  if (retainedOwner)
    retainedOwner->Release();
  owner = nullptr;
  return false;
}

// Schedule one delivery, using the stored period when the supplied delay is zero. The stored period is unchanged.
bool CtTickSource::StartOnce(u32 value, ScUnknown *retainedOwner) {
  if (running || !function)
    return false;
  if (!value)
    value = period;
  if (retainedOwner) {
    retainedOwner->AddRef();
    owner = retainedOwner;
  }
  running = 1;
  oneShot = 1;
  id = CtSourceScheduler.Add(Dispatch, this, value, 1, 0);
  if (id)
    return true;
  running = 0;
  oneShot = 0;
  if (retainedOwner)
    retainedOwner->Release();
  owner = nullptr;
  return false;
}

// Cancel a nonzero registration before releasing the retained owner. A zero registration leaves all other state intact.
bool CtTickSource::Stop() {
  if (id) {
    CtSourceScheduler.Cancel(id);
    ScRelease(&owner);
    id = 0;
    oneShot = 0;
    running = 0;
  }
  return true;
}

u8 CtTickSource::IsRunning() const {
  return running;
}

// Replace the callback and its original external context pointer in order.
bool CtTickSource::SetCallback(CtSourceCallback callback, void *context) {
  function = callback;
  argument = context;
  return true;
}

// Snapshot a registered delivery under the registry lock, then invoke the callback after unlocking.
// Preserve the original retained-owner side effect when the snapshotted callback is null.
i32 CtTickSource::Dispatch(void *context) {
  CtTickSource *source = static_cast<CtTickSource *>(context);
  CtSourceLock.Lock();
  if (CtFindSource(source) == CtSourceMembers.size) {
    CtSourceLock.Unlock();
    return 0;
  }
  ScUnknown *retainedOwner = source->owner;
  CtSourceCallback callback = source->function;
  void *argument = source->argument;
  u8 once = source->oneShot;
  if (retainedOwner)
    retainedOwner->AddRef();
  if (once) {
    if (source->owner) {
      source->owner->Release();
      source->owner = nullptr;
    }
    source->running = 0;
    source->oneShot = 0;
    source->id = 0;
  }
  CtSourceLock.Unlock();
  if (callback) {
    callback(argument);
    if (retainedOwner)
      retainedOwner->Release();
  }
  return 0;
}
