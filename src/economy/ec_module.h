#ifndef SC3_ECONOMY_EC_MODULE_H
#define SC3_ECONOMY_EC_MODULE_H

#include <map>
#include <vector>

#include "city/ct_date.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "resources/file.h"
#include "resources/resource.h"

class CtCity;
class EcStreamRef;
class RsDatabaseSegment;
class RsStream;

// Original empty polymorphic archive base, table1001CC90 and destructor17B77.
class EcArchive {
public:
  virtual ~EcArchive();
};

// Actual SIMECO archive-frame header and owned stream scope, table1001CC8C.
// Constructor nominal key/version/flag declarations remain unresolved; this is not an installed-vtable overlay.
class EcArchiveFrame : public EcArchive {
public:
  EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 text_flag);
  ~EcArchiveFrame() override;
  RsStream *GetStream();
  void MarkFailed();

  u16 version;
  u8 text_flag;
  bool extended_flags;
  u32 marker;
  EcStreamRef *record;
  u8 good;
  u32 game_version[3];
};

class CtByteCells;

// SIMRCI's unsized byte-map receiver owns a reference count, readiness byte and column-storage pointer.
// Unlike the separate sized-map family, dimensions come directly from the allocated columns and no shift is stored.
class EcByteMap : public ScCellMap<u8> {
public:
  EcByteMap();
  virtual ~EcByteMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;

protected:
  u32 references;
  bool initialized;
  CtByteCells *cells;
};

class CtCity;
class RsStream;

// Original polling state and its STL vector of packed-WORD positions.
// Original private Build return spelling remains unresolved.
class EcCursor {
  friend class EcPollution;

public:
  EcCursor();
  ~EcCursor();
  u8 Next(i32 *x, i32 *z);
  void PrepareTraversal(u8 shuffle); // Native preparation phase, not an asserted original Build return contract.

protected:
  i8 phase;
  i32 mode;
  u8 refill;
  i32 min_x, min_z, max_x, max_z, count;
  std::vector<u16> positions;
  i32 used, current, stride;
};

// Specialized serializable cursor: the owner constructs the plain state before installing table1001BAE8.
// It has exactly two virtual record operations and no virtual destructor; the original template identity is unresolved.
class EcCursorRecord : public EcCursor {
public:
  virtual u8 SerialRead(CtCity *city, RsStream *record);
  virtual u8 SerialWrite(CtCity *city, RsStream *record);
};

// Arithmetic declaration reference: Scion cIGZDate.h, Copyright (C) 2021 Nelson Gomez; LGPL-2.1-or-later.

// Actual SIMECO date wrapper: original CtDate at0, cIGZDate interface at8 and independent reference count at12.
// The full public date table is recovered; postfix results are excluded because the original returns expired objects.
class EcDate : public CtDate, public CtDateInterface {
public:
  EcDate();
  EcDate(u32 day, u32 year);
  EcDate(const EcDate &source);
  explicit EcDate(const CtDate &source);
  virtual ~EcDate();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Set(u32 day, u32 year) override;
  void Set(u32 month, u32 day, u32 year) override;
  void Set(const CtTime &time) override;
  void Set(u32 serial) override;
  void Set(const char *month, u32 day, u32 year) override;
  void Copy(const CtDateInterface &source) override;
  void Clone(CtDateInterface **out) override;
  i32 Between(const CtDateInterface &first, const CtDateInterface &second) const override;
  i32 CompareTo(const CtDateInterface &other) const override;
  u32 WeekDay() const override;
  u32 Year() const override;
  u32 DayOfYear() const override;
  u32 DayOfMonth() const override;
  u32 FirstDayOfMonth() const override;
  u32 FirstDayOfMonth(u32 month) const override;
  u32 Hash() const override;
  i32 IsValid() const override;
  i32 Leap() const override;
  void MaxDate(const CtDateInterface &other, CtDateInterface *out) const override;
  void MinDate(const CtDateInterface &other, CtDateInterface *out) const override;
  u32 Month() const override;
  void Previous(u32 weekday, CtDateInterface *out) const override;
  u32 YearLastTwoDigits() const override;
  u32 DayNumber() const override;
  i32 operator<(const CtDateInterface &other) const override;
  i32 operator<=(const CtDateInterface &other) const override;
  i32 operator>(const CtDateInterface &other) const override;
  i32 operator>=(const CtDateInterface &other) const override;
  i32 operator==(const CtDateInterface &other) const override;
  i32 operator!=(const CtDateInterface &other) const override;
  u32 operator-(const CtDateInterface &other) const override;
  u32 operator-(i32 days) const override;
  u32 operator+(const CtDateInterface &other) const override;
  u32 operator+(i32 days) const override;
  const CtDateInterface *operator++() override;
  const CtDateInterface *operator++(int) override;
  const CtDateInterface *operator--() override;
  const CtDateInterface *operator--(int) override;
  const CtDateInterface *operator+=(i32 days) override;
  const CtDateInterface *operator-=(i32 days) override;
  void DateString(ScStringView *out, u32 format) const override;
  i32 DayWithinMonth(u32 month, u32 day, u32 year) override;
  u32 DaysInYear(u32 year) override;
  u32 Jday(u32 month, u32 day, u32 year) override;
  i32 LeapYear(u32 year) override;

private:
  u32 references;
};

// Original generic enumeration ABI; the callback alone interprets its actual external owner context.
using EcIniCallback = void (*)(const ScString &key, const ScString &value, void *context);

// Original section keys compare their complete String byte ranges before length, independently of the CRT locale.
struct EcIniNameLess {
  bool operator()(const ScString &first, const ScString &second) const;
};

// Original unique-key STL tree stores each uppercase section name beside its physical file offset.
using EcIniIndex = std::map<ScString, u32, EcIniNameLess>;

// Recovered loose-file INI operations with the original String-key section map, not a foreign vtable overlay.
class EcIniFile {
public:
  EcIniFile();
  virtual ~EcIniFile();
  virtual const ScString *GetPath() const;
  // String input pointers retain the existing caller boundary while source-reference spelling is migrated.
  virtual void SelectFile(const ScString *input);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool GetValue(const ScString *section, const ScString *key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 count);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  // MSVC reverses this overload group: bounded output is original36, allocated output original40.
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity);
  virtual bool EnumerateSection(const ScString *section, EcIniCallback callback, void *context);
  virtual bool EnumSections(EcIniCallback callback, void *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool enabled);

protected:
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);

public:
  ScString name;
  u8 keep_open, indexed;
  EcIniIndex sections;
  RsFile file;
};

// Packaged tuning reader extends the loose-file reader with its actual filename, stream and lazy selection fields.
// String input pointers retain the existing caller boundary until its source-reference spelling is migrated.
class EcIni : public EcIniFile {
public:
  EcIni();
  ~EcIni() override;
  bool GetValue(const ScString *section, const ScString *key, ScString *out) override;
  bool EnumerateSection(const ScString *section, EcIniCallback callback, void *context) override;
  bool ReadNumber(const ScString &section, const ScString &key, i32 *out) override;
  bool SectionExists(const ScStringView &section) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity) override;
  bool WriteEntry(const ScString &section, const ScString &key, const ScString &value) override;
  bool WriteNumber(const ScString &section, const ScString &key, i32 value) override;
  bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 count) override;
  virtual void SelectPackage(const ScString &input);
  virtual const ScString *GetCompressedPath() const;
  virtual bool EnumEntriesOld(const ScString &section, EcIniCallback callback, void *context);

protected:
  void InitFileType();
  void ReadString(ScString *out);

public:
  ScString package_name;
  RsFile package_file;
  u32 mode;
  char *buffer;
  u32 buffer_capacity, package_offset;
};

// Original landfill tiling scratch grid stores one bit per row in separately owned columns.
class EcLandfillGrid {
public:
  EcLandfillGrid(u32 columns, u32 rows, const u8 *fill);
  virtual ~EcLandfillGrid();
  // Original 10019C71 returns an unused incidental loop value; only its fill effects are established.
  void Fill(const u8 *value);
  // The original setter's returned word address is discarded by its callers.
  void Set(u32 x, u32 z, const u8 *value);

private:
  friend class EcLandfillDeveloper;
  u32 columns, rows, words_per_column;
  u32 **data;
};

class ScUnknown;
class ScDirector;
class ScFramework;
class ScLanguageManager;
class ScMessageServer;
class GxGraphics;
class RsManager;
class ApGameInterface;
class CtAgentTree;
class CtCity;
class OcAttribCache;
class ScCom;
class WnResources;

ScDirector *EcGetDirector();
ScFramework *EcGetFramework();
ScLanguageManager *EcGetLanguageManager();
WnResources *EcGetWindowResources();
ScMessageServer *EcGetMessageServer();
GxGraphics *EcGetGraphics();
RsManager *EcGetTextManager();
ApGameInterface *EcGetApplication();
CtAgentTree *EcGetAgentTree();
CtCity *EcGetCity();
OcAttribCache *EcGetAttribCache();
ScCom *EcGetCom();
u32 EcParseTuningNumber(const char *text);

// Class-registration callbacks return their actual selected Unknown view; private concrete return spelling is unproved.
ScUnknown *EcCreatePollution();
ScUnknown *EcCreateIncinerator();
ScUnknown *EcCreateRecycler();

class CtCity;
class RsDatabaseSegment;
class RsStream;

namespace EcRci {
class EcStreamRef;

// Original empty polymorphic archive base, table1004DE5C and destructor3FD4F.
class EcArchive {
public:
  virtual ~EcArchive();
};

// Actual SIMRCI archive-frame header and owned stream scope, table1004DE58.
// Constructor nominal key/version/flag declarations remain unresolved; this is not an installed-vtable overlay.
class EcArchiveFrame : public EcArchive {
public:
  EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  EcArchiveFrame(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 text_flag);
  ~EcArchiveFrame() override;
  RsStream *GetStream();
  void MarkFailed();

  u16 version;
  u8 text_flag;
  bool extended_flags;
  u32 marker;
  EcStreamRef *record;
  u8 good;
  u32 game_version[3];
};

} // namespace EcRci

class RsDatabaseSegment;
class RsStream;

namespace EcRci {
// SIMRCI's actual record holder, original fields failure4/record8/borrowed segmentC and table1004DE60.
// The twelve-byte key is passed by value; private constructor nominal spellings remain unproved.
class EcRecordRef {
  friend class EcArchiveFrame;

public:
  EcRecordRef(RsDatabaseSegment *segment, RsKey key);
  EcRecordRef(RsDatabaseSegment *segment, RsKey key, u32 option);
  virtual ~EcRecordRef();
  virtual bool IsOpen();

protected:
  u8 failed;
  RsRecord *record;
  RsDatabaseSegment *segment;
};

// The real derived holder queries the record's separate stream interface and owns that acquisition until destruction.
class EcStreamRef : public EcRecordRef {
public:
  EcStreamRef(RsDatabaseSegment *segment, RsKey key);
  EcStreamRef(RsDatabaseSegment *segment, RsKey key, u32 option);
  ~EcStreamRef() override;
  bool IsOpen() override;
  RsStream *GetStream();

protected:
  void *stream_interface; // Actual external QueryInterface void** output storage, exposed as RsStream only on use.
};

} // namespace EcRci

class RsDatabaseSegment;
class RsStream;

// SIMECO's actual record holder, original fields failure4/record8/borrowed segmentC and table1001CC94.
// The twelve-byte key is passed by value; private constructor nominal spellings remain unproved.
class EcRecordRef {
  friend class EcArchiveFrame;

public:
  EcRecordRef(RsDatabaseSegment *segment, RsKey key);
  EcRecordRef(RsDatabaseSegment *segment, RsKey key, u32 option);
  virtual ~EcRecordRef();
  virtual bool IsOpen();

protected:
  u8 failed;
  RsRecord *record;
  RsDatabaseSegment *segment;
};

// The real derived holder queries the record's separate stream interface and owns that acquisition until destruction.
class EcStreamRef : public EcRecordRef {
public:
  EcStreamRef(RsDatabaseSegment *segment, RsKey key);
  EcStreamRef(RsDatabaseSegment *segment, RsKey key, u32 option);
  ~EcStreamRef() override;
  bool IsOpen() override;
  RsStream *GetStream();

protected:
  void *stream_interface; // Actual external QueryInterface void** output storage, exposed as RsStream only on use.
};

// SIMECO's scoped localized text holder, original constructor16427 and single destructor table1001CAC8.
class EcTextRef {
public:
  EcTextRef(u32 instance, u32 group);
  virtual ~EcTextRef();
  ScString CopyString() const;

protected:
  // Direct storage for the real GetResource void** output preserves publication without incompatible pointer aliasing.
  void *string_interface;
  RsKey key;
  void Resolve();
};

// Native bounded projection of the shared ECDC formatter, with its actual separate String/format/value arguments.
void EcFormatText(ScString *out, const char *format, const char *value);
void EcFormatText(ScString *out, const char *format, const char *first, const char *second);

#endif
