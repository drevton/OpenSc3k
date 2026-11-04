#ifndef SC3_CITY_CT_DATE_H
#define SC3_CITY_CT_DATE_H

#include "common/sc3.h"

// March-based calendar arithmetic corrects Julian four-century periods to Gregorian periods and retains the time epoch.
enum CtCalendarConstant : u32 {
  kCtGregorianFourCenturyDays = 0x23ab1,
  kCtJulianFourCenturyDays = 0x23ab4,
  kCtMarchCalendarSerialOffset = 0x1a431f,
  kCtTimeEpochDay = 0x24db1a,
};

class ScString;
class CtDateInterface;
class CtTime;
class GeDate;

// Recovered cRZDate receiver: three formatting virtuals followed by the Julian-day word.
// String outputs use the project's concrete cRZString reconstruction.
class CtDate {
public:
  CtDate();
  CtDate(u32 day, u32 year);
  CtDate(u32 month, u32 day, u32 year);
  CtDate(const CtDate &source);
  CtDate &operator=(const CtDate &) = default;
  explicit CtDate(const CtDateInterface &source);
  void Set(u32 day, u32 year);
  void Set(u32 month, u32 day, u32 year);
  void Set(const CtTime &time);
  void Set(u32 serial);
  void Set(const char *month, u32 day, u32 year);
  explicit CtDate(const GeDate &source);
  explicit CtDate(u32 value);
  explicit CtDate(const CtTime &time);
  CtDate(const char *month, u32 day, u32 year);
  i32 Between(const CtDate &first, const CtDate &second) const;
  u32 Hash() const;
  i32 IsValid() const;
  i32 Leap() const;
  u32 YearLastTwoDigits() const;
  u32 DayNumber() const;
  i32 operator<(const CtDate &other) const;
  i32 operator<=(const CtDate &other) const;
  i32 operator>(const CtDate &other) const;
  i32 operator>=(const CtDate &other) const;
  i32 operator==(const CtDate &other) const;
  i32 operator!=(const CtDate &other) const;
  u32 operator-(const CtDate &other) const;
  CtDate *operator++();
  CtDate operator++(int);
  CtDate *operator--();
  CtDate operator--(int);
  CtDate *operator+=(i32 days);
  CtDate *operator-=(i32 days);
  u32 FirstDayOfMonth() const;
  static u32 DaysInYear(u32 year);
  static i32 AssertWeekDayNumber(u32 weekday);
  static i32 AssertIndexOfMonth(u32 month);
  void Mdy(u32 *month, u32 *day, u32 *year) const;
  u32 DayOfMonth() const;
  u32 DayOfYear() const;
  u32 Month() const;
  u32 Year() const;
  u32 DayOfWeek() const;
  CtDate OnOrBefore(u32 weekday) const;
  i32 CompareTo(const CtDate &other) const;
  u32 FirstDayOfMonth(u32 month) const;
  CtDate Maximum(const CtDate &other) const;
  CtDate Minimum(const CtDate &other) const;
  virtual void NameOfDayString(ScString *out, u32 format) const;
  virtual void NameOfMonthString(ScString *out, u32 format) const;
  virtual void DateString(ScString *out, u32 format) const;
  u32 serial;
};

#endif
