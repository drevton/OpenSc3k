#ifndef SC3_DISASTERS_DS_MODULE_H
#define SC3_DISASTERS_DS_MODULE_H

#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "resources/stream.h"

class CtCity;
class RsDatabaseSegment;
class RsStream;
class DsStreamRecord;

// Recovered read/write archive state and record lifetime; the empty base's nominal identity remains unresolved.
// Native storage is independent of the installed class layout.
class DsArchive {
public:
  DsArchive(CtCity *city, RsDatabaseSegment *segment, RsKey key);
  // Write overload preserves the stored word/byte inputs; nominal private parameter spellings remain unresolved.
  DsArchive(CtCity *city, RsDatabaseSegment *segment, RsKey key, u16 version, u8 option);
  virtual ~DsArchive();
  RsStream *Stream();

  u16 version; // Original header word+4 is consumed directly by versioned manager loading.

private:
  u8 option;
  bool extension;
  u32 marker;
  DsStreamRecord *record;

public:
  u8 valid; // Original +16 retains raw stream result bytes; loaders test zero/nonzero.

private:
  u32 versions[3];
};

class DsDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct DsClassRegistration {
  DsClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct DsDirectorClasses {
  DsDirectorClasses();
  ~DsDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  DsClassRegistration head;
};

// Recovered disaster-module director base; child directors and registered factory functions are borrowed.
class DsDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  DsDllDirector();
  virtual ~DsDllDirector();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  u32 RefCount() override;
  bool InitializeCOM(ScCom *com, ScStringView *path) override;
  bool OnStart(ScCom *com) override;
  void EnumClassObjects(void (*callback)(u32, u32, void *), void *context) override;
  bool GetClassObject(u32 clsid, u32 iid, void **out) override;
  bool CanUnloadNow() override;
  bool OnUnload() override;
  ScFramework *FrameWork() override;
  ScCom *GZCOM() override;
  bool ModulePath(ScStringView *out) override;
  virtual bool AppRegister(ScRegistry *registry);
  virtual void AddDirector(DsDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;

protected:
  ScArray<DsDllDirector *> children;
  DsDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class DsDirector : public DsDllDirector {
public:
  DsDirector();
  ~DsDirector() override;
  u32 GetDirectorID() const override;
};

ScUnknown *DsCreateLayer();
ScUnknown *DsCreateFireManager();
ScUnknown *DsCreateTornadoManager();
ScUnknown *DsCreateTrackManager();
ScUnknown *DsCreateClassE2FE8D38();
ScUnknown *DsCreateClass22FE8D3E();
ScUnknown *DsCreateClass84C92CBE();
ScUnknown *DsCreateClassC2FE8D43();
ScUnknown *DsCreateClass52FE8D50();
ScUnknown *DsCreateLocustManager();
ScUnknown *DsCreateClass52FE8D52();
ScUnknown *DsCreateClass52FE8D53();

// Original numeric parser used by disaster description rows and global tunables.
u32 DsParseNumber(const char *value);

// The original public reader ABI passes two const String references and an opaque caller context.
using DsIniCallback = void (*)(const ScString &key, const ScString &value, void *context);

struct DsIniSection {
  ScString name;
  u32 position;
};

// Native storage replacing only the private section tree, with destruction ordered after the embedded file.
struct DsIniIndex {
  ScArray<DsIniSection *> entries;
  ~DsIniIndex();
  void Clear();
};

// The installed virtual order and original read, write and enumeration contracts are restored.
class DsIniFile {
public:
  DsIniFile();
  virtual ~DsIniFile();
  virtual const ScString *GetPath() const;
  // Original cRZIniFile contracts are recovered from matching retail CodeView and installed bodies.
  virtual void SetPath(const ScString &input);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool ReadEntry(const ScString &section, const ScString &key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  // MSVC reverses this overload group: the buffer form is at +36 and the allocated-output form at +40.
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 length);
  virtual bool EnumEntries(const ScString &section, DsIniCallback callback, void *context);
  virtual bool EnumSections(DsIniCallback callback, void *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool keep);
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);

  ScString name;
  u8 keep_open, indexed;
  DsIniIndex sections;
  RsFile file;
};

// Packaged tuning reader extends the loose-file reader with its actual filename, stream and lazy selection fields.
// Retail CodeView and matching installed bodies establish the reached read and package-selection contracts.
class DsIni : public DsIniFile {
public:
  DsIni();
  ~DsIni() override;
  bool WriteEntry(const ScString &section, const ScString &key, const ScString &value) override;
  bool ReadEntry(const ScString &section, const ScString &key, ScString *out) override;
  bool WriteNumber(const ScString &section, const ScString &key, i32 value) override;
  bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 length) override;
  bool ReadNumber(const ScString &section, const ScString &key, i32 *out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 length) override;
  bool EnumEntries(const ScString &section, DsIniCallback callback, void *context) override;
  bool SectionExists(const ScStringView &section) override;
  virtual void SetCompressedPath(const ScString &input);
  virtual const ScString *GetCompressedPath() const;
  virtual bool EnumEntriesOld(const ScString &section, DsIniCallback callback, void *context);
  void InitFileType();
  void ReadString(ScString *out);
  ScString package_name;
  RsFile package_file;
  u32 mode;
  char *buffer;
  u32 buffer_capacity, package_offset;
};

// Original 24-byte random receiver: independent integer/real seeds, normal-cache flag and cached double.
// Construction seeds both streams together; the normal cache is populated only by the first uncached normal draw.
struct DsRandom {
  explicit DsRandom(u32 seed);

  u32 state, real_state, has_normal;
  f64 normal;

  u32 Next(u32 bound);
  f64 Unit();
  f64 RealRange(f64 minimum, f64 maximum);
  f64 Gaussian();
  f64 NormalRange(f64 minimum, f64 maximum);
  f64 TriangularReal(f64 minimum, f64 maximum);
  f64 InverseSqrtRange(f64 minimum, f64 maximum);
  i32 Range(i32 minimum, i32 maximum);
  i32 Centered(i32 minimum, i32 maximum);
  // Original22C8D callers consume the low signed DWORD; private nominal source return width remains unresolved.
  i32 Triangular(i32 minimum, i32 maximum);
  // Original22CF6 callers consume the low DWORD of the CRT wide result; nominal source return width is unresolved.
  i32 SqrtRange(i32 minimum, i32 maximum);
};

// Read/write record owner. The key is an original
// three-word value, not a native request bundle; the segment retains its existing borrowed lifetime.
class DsRecord {
public:
  DsRecord(RsDatabaseSegment *segment, RsKey key);
  // Write overload: unused trailing stack word is preserved; its nominal private source type remains unresolved.
  DsRecord(RsDatabaseSegment *segment, RsKey key, u32 mode);
  virtual ~DsRecord();
  virtual bool IsValid();

protected:
  bool aborted;
  RsRecord *record;
  RsDatabaseSegment *segment;
};

// The actual stream owner adds one queried IID00199627 reference to the underlying record owner.
// One canonical RsStream supplies both directions; no second stream identity is manufactured.
class DsStreamRecord : public DsRecord {
public:
  DsStreamRecord(RsDatabaseSegment *segment, RsKey key);
  DsStreamRecord(RsDatabaseSegment *segment, RsKey key, u32 mode);
  ~DsStreamRecord() override;
  bool IsValid() override;
  RsStream *Stream();

private:
  // QueryInterface publishes directly to its genuine void** ABI output before typed stream access.
  void *stream_interface;
};

class CtCity;
class ApPreferences;
class ScFramework;
class ScCom;
class RsManager;
class ScDirector;
class ScMessageServer;
class ScLanguageManager;
class ScVariableManager;
class ApGameInterface;
class CtAgentTree;
class ApBoxX;
class SpCityWindowView;
class SpViewInterface;
class SpCellMap;
class GxGraphics;
class WnWindowManager;

// Original module director lookup. Concrete disaster factory bodies remain explicit dependencies.
ScDirector *DsGetDirector();
// Original framework helper borrows from that actual director.
ScFramework *DsFrameWork();
ScMessageServer *DsGetMessageServer();
ScLanguageManager *DsGetLanguageManager();
ScVariableManager *DsGetPropertyProvider();
RsManager *DsGetResourceManager();
ScCom *DsGetCom();
// Original application singleton lookup through the actual module framework.
ApGameInterface *DsGetApplication();
CtAgentTree *DsGetAgentTree();
ApBoxX *DsGetBoxX();
SpCityWindowView *DsGetCityWindow();
SpViewInterface *DsGetCityView();
SpCellMap *DsGetCellMap();
GxGraphics *DsGetGraphics();
WnWindowManager *DsGetWindowManager();
CtCity *DsGetCity();
ApPreferences *DsGetPreferences();

// Original one-byte automatic-cursor guard; construction remembers only whether the manager accepted its push.
class DsCursorScope {
public:
  explicit DsCursorScope(u32 cursor);
  ~DsCursorScope();
  bool acquired;
};

// Original five-word localized text handle, retaining a String identity separately from its actual resource key.
class DsTextResource {
public:
  DsTextResource(u32 instance, u32 group);
  virtual ~DsTextResource();
  ScString GetText();
  // Original byte predicate and complete copy effects are known; private nominal bool/BYTE remains unresolved.
  u8 CopyTo(ScStringView *out);
  // The resource lookup's Boolean reply is preserved; its private nominal source declaration remains unresolved.
  bool Resolve();
  ScStringView *text;
  RsKey key;
};

// Original one-argument stdcall pass returns the final search position; nominal private return remains unresolved.
u32 SC3_STDCALL DsExpandTextTokens(ScString *text);
bool SC3_STDCALL DsSubstituteTextToken(ScString *text, u32 *position);
// Original value resolver uses separate String and in/out position operands; nominal bool/BYTE remains unresolved.
u8 SC3_STDCALL DsTryTextToken(ScString *text, const ScString &token, u32 *position);

// Original bounded spiral receiver used by fire and toxic-cloud map scans. The unused original word36 is omitted;
// only fields actually initialized/consumed by this family are represented, so this is not a foreign memory overlay.
class DsTileScan {
public:
  DsTileScan();
  // Reset and Turn preserve observed EAX words; their private nominal void/integer returns remain unresolved.
  i32 Reset(i32 origin_x, i32 origin_z);
  bool Next(i32 *output_x, i32 *output_z);
  // Original AL is0/1, but private nominal bool/BYTE spelling remains unresolved.
  u8 AdvanceLeg();
  i32 Turn();

  i32 x, z, target_x, target_z;              // Original0..12.
  i32 left, top, right, bottom, orientation; // Original16..32.
  i32 step;                                  // Original40, signed leg length and direction.
  u8 state;                                  // Original44: initial0, running1, exhausted2.
  bool horizontal;                           // Original45.
  u8 edges;                                  // Original46: right1,left2,bottom4,top8 completion bits.
};

#endif
