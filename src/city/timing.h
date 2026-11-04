#ifndef SC3_CITY_TIMING_H
#define SC3_CITY_TIMING_H

#include "city/ct_date.h"
#include "city/simulation.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

typedef void (*CtSourceCallback)(void *argument);

// Recovered source object embedded in the simulation clock. Callback void pointers are the original external ABI.
class CtTickSource {
public:
  CtTickSource();
  virtual ~CtTickSource();
  virtual bool SetSetting(u32 value);
  virtual bool GetSetting(u32 *out) const;
  virtual bool SetPeriod(u32 value);
  virtual bool GetPeriod(u32 *out) const;
  virtual bool Start(u32 period, ScUnknown *owner);
  virtual bool StartOnce(u32 period, ScUnknown *owner);
  virtual bool Stop();
  virtual u8 IsRunning() const;
  virtual bool SetCallback(CtSourceCallback function, void *argument);
  static i32 Dispatch(void *argument);
  ScUnknown *owner;
  CtSourceCallback function;
  void *argument;
  u32 setting, period;
  u8 running, oneShot;
  u32 id;
};

// Native numerical support for the original counter coefficients, preserving x87 rounding without changing host state.
struct ScCounterScale {
  u64 factors[5];
};

ScCounterScale ScBuildCounterScale(u64 frequency, u16 control);
ScCounterScale ScBuildInverseCounterScale(u64 frequency, u16 control);
i64 ScScaleCounterTicks(const ScCounterScale &scale, u32 unit, i64 ticks, u16 control);
// Native arithmetic boundaries preserve FILD's exact integer before binary32 multiplication/division and truncation.
// Converting the integer to binary64 first can change large neighbor-water accounting by one arithmetic ULP.
i64 ScMultiplyInt64Float(i64 value, f32 factor);
i64 ScMultiplyInt64Float(i64 value, f32 factor, u16 control);
i64 ScDivideInt64Float(i64 value, f32 factor);
void ScMultiplyUint32Float(u32 value, f32 factor, f32 threshold, u16 control, u32 *integer, f32 *stored,
    bool *less);
i64 ScFloatDifferenceQuotient(f32 value, u32 integer, f32 divisor, u16 control);

// Original cIGZSemaphore eight-slot interface, verified by retail CodeView5A34 and SIMCITY's concrete vtable.
class ScSemaphoreInterface {
public:
  virtual u32 Release() = 0;
  virtual bool Init(u32 count) = 0;
  virtual i32 GetSemaphore() = 0;
  virtual i32 GetSemaphoreAll() = 0;
  virtual i32 ReleaseSemaphore(u32 count) = 0;
  virtual i32 ReleaseSemaphoreAll() = 0;
  virtual i32 GetMaxReferenceCount() = 0;
  virtual i32 IsValid() = 0;
};

// The concrete cRZSemaphore adds a destructor at slot32 and its explicit handle output at slot36 on x86.
class ScSemaphore : public ScSemaphoreInterface {
public:
  ScSemaphore();
  ScSemaphore(const ScString &name);
  u32 Release() override;
  bool Init(u32 count) override;
  i32 GetSemaphore() override;
  i32 GetSemaphoreAll() override;
  i32 ReleaseSemaphore(u32 count) override;
  i32 ReleaseSemaphoreAll() override;
  i32 GetMaxReferenceCount() override;
  i32 IsValid() override;
  virtual ~ScSemaphore();
  virtual i32 GetSemaphoreHandle(void **out);
  ScString name;
  u32 maximum, initialized;
  void *handle;
};

// Clock interface declarations adapted from sc3k-gzcom-dll cIGZClock.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

class ScLock;
class ScMessageQueue;
typedef void (*CtClockCallback)(void *argument);
enum class CtClockUnit : i32 { Minute = 1, Hour, Day, Week, Month, Year };

struct CtClockRecord {
  union {
    CtClockCallback function;
    ScMessageQueue *queue;
  } target;

  CtDate date;

  union {
    void *argument;
    ScMsgCode messageType;
  } context;

  u32 direct;
  CtClockRecord(CtClockCallback function, void *argument, const CtDate &date);
  CtClockRecord(ScMessageQueue *queue, ScMsgCode messageType, const CtDate &date);
};

// The circular head owns links only; its unused payload never constructs a local-date receiver.
struct CtClockNode {
  CtClockNode *next, *previous;

  union {
    CtClockRecord entry;
  };

  CtClockNode();
  explicit CtClockNode(const CtClockRecord &entry);
};

class CtClockList {
public:
  CtClockList();
  ~CtClockList();
  void Append(const CtClockRecord &entry);
  CtClockNode *head;
};

// Original cIGZClock interface, with the SDK source declaration order. MSVC groups and reverses overloaded virtuals.
class CtClockInterface : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual bool GetCurrentClockDate(CtDate *out) = 0;
  virtual bool SetCurrentClockDate(CtDate *date) = 0;
  virtual bool GetCurrentClockTime(CtTime *out) = 0;
  virtual bool SetCurrentClockTime(CtTime *time) = 0;
  virtual bool GetClockRate(u32 *out) = 0;
  virtual bool SetClockRate(u32 rate) = 0;
  virtual bool PauseClock() = 0;
  virtual bool UnpauseClock() = 0;
  virtual u32 GetPauseCount() = 0;
  virtual bool SetPauseCount(u32 count) = 0;
  virtual bool AddNotification(CtClockCallback function, void *argument, CtDate *date) = 0;
  virtual bool RemoveNotification(CtClockCallback function, CtDate *date) = 0;
  virtual bool AddNotification(CtClockCallback function, void *argument, CtClockUnit unit) = 0;
  virtual bool RemoveNotification(CtClockCallback function, CtClockUnit unit) = 0;
  virtual bool AddNotification(ScMessageQueue *queue, CtDate *date, ScMsgCode messageType) = 0;
  virtual bool RemoveNotification(ScMessageQueue *queue, CtDate *date) = 0;
  virtual bool AddNotification(ScMessageQueue *queue, CtClockUnit unit, ScMsgCode messageType) = 0;
  virtual bool RemoveNotification(ScMessageQueue *queue, CtClockUnit unit) = 0;
};

// Recovered simulation clock, with its original abstract base and seven notification lists.
class CtClock : public CtClockInterface {
public:
  CtClock();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GetCurrentClockDate(CtDate *out) override;
  bool SetCurrentClockDate(CtDate *date) override;
  bool GetCurrentClockTime(CtTime *out) override;
  bool SetCurrentClockTime(CtTime *time) override;
  bool GetClockRate(u32 *out) override;
  bool SetClockRate(u32 rate) override;
  bool PauseClock() override;
  bool UnpauseClock() override;
  u32 GetPauseCount() override;
  bool SetPauseCount(u32 count) override;
  bool AddNotification(CtClockCallback function, void *argument, CtDate *date) override;
  bool RemoveNotification(CtClockCallback function, CtDate *date) override;
  bool AddNotification(CtClockCallback function, void *argument, CtClockUnit unit) override;
  bool RemoveNotification(CtClockCallback function, CtClockUnit unit) override;
  bool AddNotification(ScMessageQueue *queue, CtDate *date, ScMsgCode messageType) override;
  bool RemoveNotification(ScMessageQueue *queue, CtDate *date) override;
  bool AddNotification(ScMessageQueue *queue, CtClockUnit unit, ScMsgCode messageType) override;
  bool RemoveNotification(ScMessageQueue *queue, CtClockUnit unit) override;
  virtual ~CtClock();
  static void Dispatch(void *argument);
  void Pump();
  void TickMinute();
  void TickHour();
  void TickDay();
  CtClockList lists[7];
  CtTickSource source;
  u32 minute, hour;
  CtDate date;
  u32 rate, paused, baseline, accumulated, dayPeriod, hourPeriod, minutePeriod;
  ScLock *lock;
  u32 references;
  u8 running;

private:
  void Notify(u32 unit);
};

extern ScCounterScale ScCounterCoefficients;
extern ScCounterScale ScCounterInverseCoefficients;
extern u32 ScCounterResolution;
u16 ScCounterControl();
// Native platform query boundary used by the recovered timer's GetCurrentInternal method.
void ScReadCounter(i64 *out);
// Native platform initialization for the recovered module's process-wide coefficients and resolution calibration.
void ScInitializeCounterPlatform();

#endif
