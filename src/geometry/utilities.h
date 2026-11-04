#ifndef SC3_GEOMETRY_UTILITIES_H
#define SC3_GEOMETRY_UTILITIES_H

#include "city/ct_date.h"
#include "city/simulation.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "occupants/api.h"
#include "resources/factory.h"
#include "resources/file.h"
#include "resources/resource.h"

// The local 16-byte COM date has the original value base at 0, date interface at 8 and independent count at 12.
// The complete public table is recovered; postfix results are excluded because the original returns expired objects.
class GeDate : public CtDate, public CtDateInterface {
public:
  GeDate();
  GeDate(const GeDate &source);
  explicit GeDate(const CtDate &source);
  GeDate(u32 day, u32 year);
  GeDate(u32 month, u32 day, u32 year);
  virtual ~GeDate();
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

// Original numeric parser used by both building-default and tracking-group rows.
u32 GeParseNumber(const char *value);

// Actual cRZIniFile/cSC3CompressibleINIFile callback ABI: cdecl void, two const String references and generic context.
// The opaque pointer belongs to this external library boundary; callbacks interpret used contexts through typed adapters.
using GeIniCallback = void (*)(const ScString &key, const ScString &value, void *context);

struct GeIniSection {
  ScString name;
  u32 position;
};

// Native storage replacing only the private section tree, with destruction ordered after the embedded file.
struct GeIniIndex {
  ScArray<GeIniSection *> entries;
  ~GeIniIndex();
  void Clear();
};

// Original18-slot reader/writer interface, verified against SIMGEOM1002A9D0 and authenticated retail decorations.
// The private tree is represented by the existing native section array; no foreign vtable overlay is used.
class GeIniFile {
public:
  GeIniFile();
  virtual ~GeIniFile();
  // Original borrowed const reference uses a pointer under the project source-spelling rule.
  virtual const ScString *GetPath() const;
  virtual void SetPath(const ScString &input);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool ReadEntry(const ScString &section, const ScString &key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity);
  virtual bool EnumEntries(const ScString &section, GeIniCallback callback, void *context);
  virtual bool EnumSections(GeIniCallback callback, void *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool value);
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);

  ScString name;
  u8 keep_open, indexed;
  GeIniIndex sections;
  RsFile file;
};

// Packaged tuning reader keeps the full inherited interface. Its six numeric/binary/write adapters genuinely refuse.
class GeIni : public GeIniFile {
public:
  GeIni();
  ~GeIni() override;
  bool WriteEntry(const ScString &section, const ScString &key, const ScString &value) override;
  bool ReadEntry(const ScString &section, const ScString &key, ScString *out) override;
  bool WriteNumber(const ScString &section, const ScString &key, i32 value) override;
  bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length) override;
  bool ReadNumber(const ScString &section, const ScString &key, i32 *out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity) override;
  bool EnumEntries(const ScString &section, GeIniCallback callback, void *context) override;
  bool SectionExists(const ScStringView &section) override;
  virtual void SetCompressedPath(const ScString &input);
  virtual const ScString *GetCompressedPath() const;
  virtual bool EnumEntriesOld(const ScString &section, GeIniCallback callback, void *context);
  void InitFileType();
  void ReadString(ScString *out);
  ScString package_name;
  RsFile package_file;
  u32 mode;
  char *buffer;
  u32 buffer_capacity, package_offset;
};

// Actual132-byte occupant override: resource0, serialized-resource20 and occupant-override24 identities.
// Its resource key and retained sprite cache stay independent from the mutable sprite-selection key.
class GeOverride : public RsResource, public RsSerializable, public OcOverride {
public:
  GeOverride();
  ~GeOverride() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *Key() const override;
  void SetName(const ScStringView &value) override;
  void SetArchitectName(const ScStringView &value) override;
  void SetDescription(const ScStringView &value) override;
  void SetLotSize(u8 value) override;
  void SetSpriteAttribKey(const RsKey &value) override;
  bool GetName(ScStringView *out) override;
  bool GetArchitectName(ScStringView *out) override;
  u8 GetLotSize() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  void SetAuthorSerialNumber(ScStringView *value) override;
  bool GetAuthorSerialNumber(ScStringView **out) override;
  void SetGUID(u32 value) override;
  bool GetGUID(u32 *out) override;
  u8 Read(RsStream *stream) override;
  u8 Write(RsStream *stream) override;

private:
  u8 lotSize;
  ScString name, architectName, description;
  RsKey spriteKey;
  void *sprite; // Actual COM publication slot, typed only at use; false lookups clear it after publication.
  ScString authorSerial;
  u32 guid;
};

#ifdef GetObject
#undef GetObject
#endif
// Actual20-byte override container, confirmed by SIMINIT's typed resource acquisitions and BAT-building enumeration.
// Each stored pointer owns one reference; successful getters borrow that same interface without an extra AddRef.
class GeOverrideList : public OcOverrideContainer {
public:
  GeOverrideList();
  virtual ~GeOverrideList();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Clear() override;
  bool GetObject(u32 index, OcOverride **out) const override;
  bool GetObject(const RsKey &key, OcOverride **out) override;
  bool PutObject(OcOverride *value) override;
  u32 Size() override;
  u32 GetIndexForObject(const OcOverride *value) override;

private:
  ScArray<OcOverride *> objects;
  u32 references;
};

// Original 24-byte geometry random receiver. Only the three proven state words are interpreted by these methods.
struct GeRandom {
  GeRandom(u32 seed);
  u32 Draw();
  u32 Next(u32 bound);
  u32 state, realState, hasNormal;
  u8 unrecovered[12];
};

static_assert(sizeof(GeRandom) == 24);

class GeTextToken;

// Original external cdecl resolver receives live text, position, mutable token storage and its opaque library context.
using GeTextResolver = bool (*)(const ScString &value, i32 position, GeTextToken *token, void *context);

// Original five-word localized String handle, independent from the city DLL's corresponding resource wrapper.
class GeTextResource {
public:
  GeTextResource();
  GeTextResource(const GeTextResource &source);
  GeTextResource(u32 instance, u32 group);
  virtual ~GeTextResource();
  GeTextResource *operator=(const GeTextResource &source);
  bool operator==(const ScStringView &other);
  const ScStringView *Value() const;
  const RsKey *GetResKey() const;
  void GetResKey(RsKey *out) const;
  bool IsStringPresent();
  operator ScString();
  ScString Val();
  // Original decorations prove Boolean replies. Missing resources leave the output untouched.
  bool Detokenize(ScStringView *out);
  bool Detokenize(ScStringView *out, GeTextResolver resolver, void *context);
  bool Detokenize(ScStringView *out, GeTextToken **tokens, u32 count);
  // Original nonvirtual token members use the localized handle as receiver; mutable inputs use typed pointers.
  // Original decorations establish void scanners and Boolean replacement replies; old CodeView alone is ambiguous.
  void ReplaceParameters(ScString *value);
  void ReplaceParameters(ScString *value, GeTextResolver resolver, void *context);
  void ReplaceParameters(ScString *value, GeTextToken **tokens, u32 count);
  bool ReplaceCommonParameter(ScString *value, i32 *position);
  bool ReplaceParameter(ScString *value, const ScString &token, i32 *position);
  bool ReplaceParameter(ScString *value, GeTextToken *token, i32 *position);
  bool ReplaceParameter(ScString *pattern, ScString *replacement);
  bool ReplaceParameter(ScString *pattern, ScString *replacement, ScString *out);
  void *text; // Actual COM output storage; the acquired IID105 interface is typed when used.
  RsKey key;

protected:
  void LoadResource();
  void UnloadResource();
};

// Original cSC3StringTokenData stores its formatter kind, owned name and three separate formatter arguments.
// Its sole virtual operation is destruction; this is the actual 40-byte token, not a replacement request bundle.
class GeTextToken {
public:
  GeTextToken();
  GeTextToken(u32 kind, const ScStringView &name, u32 first, u32 second);
  virtual ~GeTextToken();
  u32 kind;
  ScString name;
  u32 first, second;
  i32 number;
};

#endif
