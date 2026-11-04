#ifndef SC3_COMMON_API_H
#define SC3_COMMON_API_H

#include <exception>
#include <list>
#include <map>

#include "common/framework.h"
#include "common/sc3.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

class ScComCore;

void ScRegisterBuiltInDirectors(ScComCore *com);

// SDK declarations adapted from cISC3CityCellMapBase.h; Copyright Nicholas Hayes, LGPL-2.1-or-later.
class RsStream;

// SDK cISC3CityCellMapBase: preserve the genuine map interface shared by the primary and pollution subobjects.
class ScCellMapBase : public ScUnknown {
public:
  virtual u32 CellCountX() const = 0;
  virtual u32 CellCountZ() const = 0;
  virtual void GetValueSint32(u32 x, u32 z, i32 *value) const = 0;
  virtual bool InBounds(u32 x, u32 z) const = 0;
  virtual bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const = 0;
  virtual bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const = 0;
  virtual bool SerialRead(RsStream *record) = 0;
  virtual bool SerialWrite(RsStream *record) const = 0;
};

class ScCommandFactory;

// Command-service identity24B5E543 returns its borrowed20685AA3 factory through original slot12.
// SC3U4748A9 publishes owner+44;47489C returns the embedded owner+24 factory without retaining it.
class ScCommandService : public ScUnknown {
public:
  virtual ScCommandFactory *GetClassFactory() = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cIGZRealtimeDebugDisplay.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class ScDebugDisplay : public ScUnknown {

public:
  virtual void Show() = 0;
  virtual void Hide() = 0;
  virtual bool IsVisible() = 0;

  virtual bool AddLine(ScStringView *pLine) = 0;
  virtual bool RemoveLine(ScStringView *pLine) = 0;
  // MSVC emits overload groups in reverse declaration order, matching the installed four-argument slot first.
  virtual bool AddBarGraph(f32 *unknown1, u32 unknown2) = 0;
  virtual bool AddBarGraph(f32 *unknown1, u32 unknown2, ScStringView **unknown3, u32 unknown4) = 0;
  virtual bool RemoveBarGraph(f32 *unknown1) = 0;
  // Keep the same physical order for the strip-meter overloads.
  virtual bool AddStripMeter(f32 *unknown1, u32 unknown2) = 0;
  virtual bool AddStripMeter(f32 *unknown1, u32 unknown2, ScStringView **unknown3, u32 unknown4) = 0;
  virtual bool RemoveStripMeter(f32 *unknown1) = 0;
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// Copyright (C) 2023, 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScVariant;
class ScValue;
class ScStringView;

// Original cIGZSystemService order: retail CodeView260e places OnIdle at40 and OnTick at44.
// Installed GZServiceD message-server primary44 and secondary48 share the same tick body.
class ScSystemService : public ScUnknown {
public:
  virtual u32 GetServiceID() const = 0;
  virtual void SetServiceID(u32 identifier) = 0;
  virtual i32 GetServicePriority() const = 0;
  virtual bool IsServiceRunning() const = 0;
  virtual void SetServiceRunning(bool running) = 0;
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual bool OnIdle(u32 argument) = 0;
  virtual bool OnTick(u32 argument) = 0;
  virtual bool OnInstall() = 0;
};

// Reached cISC3VariableMgr prefix, identified by SL::VM and SIMVARIABLES IID64232F0F.
// PC table1000B240 slots12/16/20/24/28 prove these reached operations; the later tail remains undeclared.
class ScVariableManager : public ScUnknown {
public:
  virtual ScSystemService *AsSystemService() = 0;
  virtual u8 HasVariable(u32 key) = 0;
  virtual u8 GetVariable(u32 key, ScValue *out) = 0;
  // Original slot24 reads the source through its string interface; Boolean/BYTE and const spelling remain unresolved.
  virtual u8 LoadTuning(const ScStringView &path) = 0;
  // Original slot28 borrows the retained source-string view; its source name and pointer spelling remain unresolved.
  virtual ScStringView *GetSourcePath() = 0;
};

// Actual SC3U reference-count base, retaining the executable's module director before changing its own count.
class ScExeObject : public ScUnknown {
public:
  ScExeObject();
  virtual ~ScExeObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Original common24-byte service base. Its distinct module identity owns the shared reference count.
class ScServiceBase : public ScSystemService, public ScExeObject {
public:
  ScServiceBase(u32 identifier, i32 priority);
  ~ScServiceBase() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 value) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool value) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool service_running;
};

class ScCmdLine;
class ScCommandLine;
class ApApp;
class ScFrameworkHooks;
class ScDebugStream;
class ScOutputStream;
class ScInputStream;

// Original registry virtual order, with explicit pointers for mutable string outputs.
class ScRegistry : public ScUnknown {
public:
  // SDK cIGZRegistry.h:30, original slot12.
  virtual bool Register(const ScStringView &name) = 0;
  virtual bool Unregister(const ScStringView &name) = 0;
  virtual bool Lookup(const char *category, const ScStringView &key, ScStringView *result) = 0;
  // SDK cIGZRegistry.h:35, original slot24; keep its separate four arguments.
  virtual bool MakeRegistryName(
      const char *category, const ScStringView &key, const ScStringView &value, ScStringView *result) = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  typedef void (*ScRegistryCallback)(ScStringView const &, ScStringView const &, void *);
  virtual bool EnumCategory(const char *category, ScRegistryCallback pCallback, void *pContext) = 0;
};

// Original SC3U virtual order through132, independently checked against table4D6BF4.
// Unlimited's idle entry45D9CE has no stack argument; the SDK's later one-argument overload is not another slot.
class ScFramework : public ScUnknown {
public:
  enum ScStreamType : i32 { kStreamStdOut, kStreamStdErr, kStreamStdIn };

  virtual bool AddSystemService(ScSystemService *service) = 0;
  virtual bool GetSystemService(u32 service, u32 iid, void **out) = 0;
  virtual bool EnumSystemServices(ScUnknownEnumerator *enumerator, ScUnknown *pUnknown, u32 dwUnknown) = 0;
  // PC declaration: sc3k-gzcom-dll/src/gzcom-dll/include/cIGZFrameWork.h:93.
  // Installed GZWIND 100010E5 calls original slot24 with its typed secondary view.
  virtual bool AddHook(ScFrameworkHooks *hooks) = 0;
  virtual bool RemoveHook(ScFrameworkHooks *pHooks) = 0;
  // SDK cIGZFrameWork.h107; installed graphics Init reaches original32 with its system-service view.
  virtual bool AddToTick(ScSystemService *service) = 0;
  virtual bool RemoveFromTick(ScSystemService *pService) = 0;
  virtual bool AddToOnIdle(ScSystemService *pService) = 0;
  virtual bool RemoveFromOnIdle(ScSystemService *pService) = 0;
  virtual void SetOnIdleInterval(i32 nInterval) = 0;
  virtual i32 GetOnIdleInterval() = 0;
  virtual void OnTick(u32 dwTimeElapsed) = 0;
  // CodeView cIGZFrameWork: these are void notifications; incidental return-register values are not game results.
  virtual void OnIdle() = 0;
  virtual void Quit(i32 reason) = 0;
  virtual void AbortiveQuit(i32 nQuitReason) = 0;
  // SDK cIGZFrameWork.h156; installed graphics Init100114B8 reaches original slot72.
  virtual ScCmdLine *CommandLine() = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  enum ScFrameworkState {
    kStatePreFrameWorkInit = 1,
    kStatePreAppInit = 3,
    kStatePostAppInit = 6,
    kStatePreAppShutdown = 10,
    kStatePostAppShutdown = 11,
    kStatePostSystemServiceShutdown = 12
  };

  virtual bool IsInstall() = 0;
  virtual ScCom *GetCOMObject() = 0;
  virtual ScDebugStream *GetDebugStream() = 0;
  virtual ScDebugStream *DefaultDebugStream() = 0;
  virtual ScDebugStream *DebugStream() = 0;
  virtual void SetDebugStream(ScDebugStream *stream) = 0;
  // Original mutable reference returns use explicit pointers under the project's source-spelling rule.
  virtual ScOutputStream *StdOut() = 0;
  virtual ScOutputStream *StdErr() = 0;
  virtual ScInputStream *StdIn() = 0;
  virtual bool GetStream(ScStreamType type, u32 iid, void **out) = 0;
  virtual bool SetStream(ScStreamType type, ScUnknown *stream) = 0;
  virtual void SetApplication(ApApp *const application) = 0;
  virtual ApApp *Application() = 0;
  virtual void ReportException(char const *szExcText) = 0;
  virtual ScExceptionNotification *ExceptionNotificationObj() = 0;
};

// SDK callbacks received by DLL directors during application and framework lifetime transitions.
class ScFrameworkHooks : public ScUnknown {
public:
  virtual bool PreFrameWorkInit() = 0;
  virtual bool PreAppInit() = 0;
  virtual bool PostAppInit() = 0;
  virtual bool PreAppShutdown() = 0;
  virtual bool PostAppShutdown() = 0;
  virtual bool PostSystemServiceShutdown() = 0;
  virtual bool AbortiveQuit() = 0;
  virtual bool OnInstall() = 0;
};

// Original notification object created by the framework, with its own reference count and deleting identity.
class ScExceptionNotification : public ScUnknown {
public:
  ScExceptionNotification();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Notify(const char *module, i32 line);
  virtual ~ScExceptionNotification();
  u32 references;
};

// Shutdown failure uses this actual game exception, independently of allocation failures handled by ScFatal.
class ScFrameworkException : public std::exception {
public:
  // Construct the original exception category with its default standard-exception message.
  // Shutdown callers distinguish this category after the complete service pass has finished.
  ScFrameworkException() = default;
};

// The original framework collections store retaining COM-pointer values, not borrowed service pointers.
template <class T> struct ScFrameworkRef {
  T *object;

  // Retain an interface while a temporary or collection value owns it. Empty values hold no reference.
  explicit ScFrameworkRef(T *value) : object(value) {
    if (object)
      object->AddRef();
  }

  // Copying a collection value acquires another real reference. The new node is published after this retain.
  ScFrameworkRef(const ScFrameworkRef &other) : object(other.object) {
    if (object)
      object->AddRef();
  }

  // Release the stored interface when its temporary or collection node is destroyed. Other owners keep their references.
  ~ScFrameworkRef() {
    if (object)
      object->Release();
  }

  // Node ownership is constructed and destroyed directly. Shallow pointer assignment is deliberately unavailable.
  ScFrameworkRef &operator=(const ScFrameworkRef &) = delete;
};

// Actual common framework state. Its original STL maps/lists retain their interface values independently.
// Concrete platform identity, stream objects and COM ownership remain explicit implementation dependencies.
class ScFrameworkCore : public ScFramework {
public:
  ScFrameworkCore();
  virtual ~ScFrameworkCore();
  bool AddSystemService(ScSystemService *service) override;
  bool GetSystemService(u32 service, u32 iid, void **out) override;
  bool EnumSystemServices(ScUnknownEnumerator *enumerator, ScUnknown *unknown, u32 iid) override;
  bool AddHook(ScFrameworkHooks *hook) override;
  bool RemoveHook(ScFrameworkHooks *hook) override;
  bool AddToTick(ScSystemService *service) override;
  bool RemoveFromTick(ScSystemService *service) override;
  bool AddToOnIdle(ScSystemService *service) override;
  bool RemoveFromOnIdle(ScSystemService *service) override;
  void SetOnIdleInterval(i32 value) override;
  i32 GetOnIdleInterval() override;
  void OnTick(u32 frame) override;
  void OnIdle() override;
  ScCmdLine *CommandLine() override;
  bool IsInstall() override;
  ScDebugStream *GetDebugStream() override;
  ScDebugStream *DefaultDebugStream() override;
  ScDebugStream *DebugStream() override;
  void SetDebugStream(ScDebugStream *stream) override;
  ScOutputStream *StdOut() override;
  ScOutputStream *StdErr() override;
  ScInputStream *StdIn() override;
  bool GetStream(ScStreamType type, u32 iid, void **out) override;
  bool SetStream(ScStreamType type, ScUnknown *stream) override;
  void SetApplication(ApApp *value) override;
  ApApp *Application() override;
  ScExceptionNotification *ExceptionNotificationObj() override;
  void SetConsoleMode(bool value);
  void SetCommandLine(ScCommandLine *source);
  bool HookPreFrameWorkInit();
  bool HookPreAppInit();
  bool HookPostAppInit();
  bool HookPreAppShutdown();
  bool HookPostAppShutdown();
  bool HookPostSystemServiceShutdown();
  bool HookAbortiveQuit();
  bool HookOnInstall();
  virtual bool PreAppInit();
  virtual bool AppInit();
  virtual bool PostAppInit();
  virtual bool PreAppShutdown();
  virtual bool AppShutdown();
  virtual bool PostAppShutdown();
  virtual void OnInstall();
  virtual void ShutdownGZCOM() = 0;
  virtual void Run() = 0;
  static ScFrameworkCore *FrameWork();
  static i32 Main(ScCommandLine *command_line, bool mode);
  static bool SInit(ScCommandLine *command_line, bool mode);
  static bool SRun();
  static bool SShutdown();
  static ScFramework *CreateFrameWork();
  static void DestroyFrameWork(ScFramework *framework);
  static void ShutdownFrameWorkHelper();
  bool HookRelease();
  bool SystemServiceRelease();

protected:
  ApApp *application;
  std::list<ScFrameworkRef<ScFrameworkHooks>> hooks;
  std::multimap<i32, ScFrameworkRef<ScSystemService>> priority_services;
  std::map<u32, ScFrameworkRef<ScSystemService>> identified_services;
  std::list<ScFrameworkRef<ScSystemService>> idle_services, tick_services;
  i32 idle_interval;
  bool running, installing;
  ScCommandLine command_line;
  bool mode;
  ScDebugStream *debug_stream;
  ScUnknown *output_stream, *error_stream, *input_stream;
  ScExceptionNotification *exception_notification;
};

// Actual SC3U process startup boundaries. The initialization/loop/shutdown bodies remain explicit dependencies.
extern i32 ScApplicationExitCode;
extern ScFrameworkCore *ScCurrentFramework, *ScAllocatedFramework;

// Retail CodeView30A1 gives cSC3GeomFlag one DWORD of flag bits. Its original inline helpers are not needed here.
struct ScGeomFlag {
  u32 bits;
};

// Retail CodeView30AA/30AC proves the real geometry-flag filter and its mutable input reference.
// Explicit pointers retain the original pointer ABI while following the project's mutable-output spelling rule.
class ScGeomFilter : public ScUnknown {
public:
  virtual u8 Test(ScGeomFlag *flags) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original utility declarations follow retail CodeView438E; money/number methods follow the calendar/separator prefix.
class ScLanguageUtility : public ScUnknown {
public:
  enum class DateFormat : i32 {
    NumericMonthDayYear = 0,
    NumericMonthYear = 1,
    AlphaMonthDayYear = 2,
    AlphaMonthYear = 3,
    AbbrevAlphaMonthDayYear = 4,
    AbbrevAlphaMonthYear = 5,
    Count = 6
  };
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual bool GetDayName(u8, ScStringView *) = 0;
  virtual bool GetAbbrDayName(u8, ScStringView *) = 0;
  virtual bool GetMonthName(u8, ScStringView *) = 0;
  virtual bool GetAbbrMonthName(u8, ScStringView *) = 0;
  virtual bool MakeTimeString(i32, i32, i32, ScStringView *) = 0;
  virtual bool MakeDateString(i32, i32, u32, ScStringView *, DateFormat) = 0;
  virtual bool GetCurrencySymbol(ScStringView *) = 0;
  virtual bool DoesCurrencySymbolPrecedeAmount() = 0;
  virtual bool IsSpaceBetweenCurrencySymbolAndAmount() = 0;
  virtual bool GetThousandSeparator(ScStringView *) = 0;
  virtual bool MakeMoneyString(i64 value, ScStringView *out) = 0;
  virtual bool MakeNumberString(i64 value, ScStringView *out) = 0;
  virtual void ConvertToLowerCase(ScStringView *) = 0;
  virtual void ConvertToUpperCase(ScStringView *) = 0;
  virtual void ConvertToLowerCase(char *) = 0;
  virtual void ConvertToUpperCase(char *) = 0;
  virtual bool IsCharLowerA(char) = 0;
  virtual bool IsCharUpperA(char) = 0;
  virtual bool IsCharAlphaA(char) = 0;
  virtual bool IsCharNumeric(char) = 0;
  virtual bool IsCharAlphaNumericA(char) = 0;
  virtual bool IsCharPrintable(char) = 0;
  virtual bool CanCharEndLine(char) = 0;
  virtual bool CanCharStartLine(char) = 0;
  virtual bool IsCharPunct(char) = 0;
  virtual u32 GetCharacterSetFromLanguage() = 0;
  virtual bool DoesLanguageUseCharacterSet(i32) = 0;
  virtual bool GetLanguageRoadDrivingSide() = 0;
  virtual u32 GetMeasurementSystem() = 0;
  virtual bool AreFirstAndLastNamesReversed() = 0;
  virtual bool DoesLanguageUseInputMethodEditing() = 0;
  virtual bool DoesLanguageUseMultiByteCharacters() = 0;
};

// Game language identifiers from the indexed directory-name table; zero resolves through the current language.
enum ScLanguageId : i32 {
  kScLanguageCurrent = 0x00000000,
  kScLanguageEnglish = 0x00000001,
  kScLanguageBritishEnglish = 0x00000002,
  kScLanguageFrench = 0x00000003,
  kScLanguageGerman = 0x00000004,
  kScLanguageItalian = 0x00000005,
  kScLanguageSpanish = 0x00000006,
  kScLanguageDutch = 0x00000007,
  kScLanguageDanish = 0x00000008,
  kScLanguageSwedish = 0x00000009,
  kScLanguageNorwegian = 0x0000000a,
  kScLanguageFinnish = 0x0000000b,
  kScLanguageHebrew = 0x0000000c,
  kScLanguageRussian = 0x0000000d,
  kScLanguagePortuguese = 0x0000000e,
  kScLanguageJapanese = 0x0000000f,
  kScLanguagePolish = 0x00000010,
  kScLanguageSimplifiedChinese = 0x00000011,
  kScLanguageTraditionalChinese = 0x00000012,
  kScLanguageThai = 0x00000013,
  kScLanguageKorean = 0x00000014,
};

// Original cIGZLanguageManager order and DWORD declarations from retail CodeView5C49/7D53.
// Unlimited1000B3EC forwards the directory lookup's Boolean result; the newer SDK's void declaration is incorrect.
class ScLanguageManager : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual void AddAvailableLanguage(u32) = 0;
  virtual void RemoveAvailableLanguage(u32) = 0;
  virtual u32 ConvertLanguageIDToLanguageType(u32) = 0;
  virtual u32 GetCurrentLanguage() = 0;
  virtual bool SetCurrentLanguage(u32) = 0;
  virtual bool CanSwitchToLanguage(u32) = 0;
  virtual u32 GetCurrentSystemLanguage() = 0;
  virtual bool GetLanguageRuntimeLibraryName(ScStringView *, u32) = 0;
  virtual bool GetNextAvailableLanguage(u32 *) = 0;
  virtual bool GetLanguageDirectoryName(ScStringView *out, u32 language) = 0;
  virtual bool GetLanguageEnglishName(ScStringView *, u32) = 0;
  virtual bool GetLanguageLocalName(ScStringView *, u32) = 0;
  virtual bool GetLanguageIDFromLanguageEnglishName(ScStringView *, u32 *) = 0;
  virtual bool GetLanguageIDFromLanguageLocalName(ScStringView *, u32 *) = 0;
  virtual bool GetLanguageIDFromEitherEnglishOrLocalName(ScStringView *, u32 *) = 0;
  virtual bool GetLanguageEnglishNameFromLanguageID(ScStringView *, u32) = 0;
  virtual bool GetLanguageLocalNameFromLanguageID(ScStringView *, u32) = 0;
  virtual u32 GetLanguageIDAlias(u32) = 0;
  virtual bool GetISO639LanguageCodeFromLanguageID(ScStringView *, u32) = 0;
  virtual bool GetISO639LanguageIDFromLanguageCode(ScStringView *, u32 *) = 0;
  virtual bool GetISO3166CountryCodeFromCountryID(ScStringView *out, u32 country) = 0;
  virtual bool GetISO3166CountryIDFromCountryCode(ScStringView *, u32 *) = 0;
  virtual u32 GetCurrentCountry() = 0;
  virtual bool SetCurrentCountry(u32) = 0;
  virtual u32 GetPrimaryLanguageForCountry(u32) = 0;
  virtual u32 GetDefaultCountryForLanguage(u32) = 0;
  virtual u32 GetCurrentSystemCountry() = 0;
  virtual bool CanSwitchToCountry(i32) = 0;
  virtual bool GetCountryEnglishName(ScStringView *, u32) = 0;
  virtual bool GetCountryLocalName(ScStringView *, u32) = 0;
  virtual bool GetCountryIDFromCountryEnglishName(ScStringView *, u32 *) = 0;
  virtual bool GetCountryIDFromCountryLocalName(ScStringView *, u32 *) = 0;
  virtual ScLanguageUtility *GetLanguageUtility(u32 language) = 0;
  virtual ScLanguageUtility *GetNewLanguageUtility(u32) = 0;
  virtual f32 GetExchangeRateForLocale(u32) = 0;
  virtual void SetExchangeRateForLocale(f32, u32) = 0;
  virtual bool ConvertMoneyFromBaseToLocale(i32, i32 *, u32) = 0;
  virtual bool ConvertMoneyFromBaseToLocale(i64, i64 *, u32) = 0;
  virtual bool ConvertMoneyFromLocaleToLocale(i32, u32, i32 *, u32) = 0;
  virtual bool ConvertMoneyFromLocaleToLocale(i64, u32, i64 *, u32) = 0;
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Selectors can be numeric IDs or pointer identities (SIMUI); the transport must retain either.
// Numeric ID enums remain u32. Only this pointer-bearing word and its matching APIs use native width.
using ScMsgCode = uptr;
// Queue keys also include owner addresses (CtSimulator::Init), independently of numeric service/interface IDs.
using ScQueueKey = uptr;

// Original cGZMessage aggregate, with four native-width words for pointer-bearing selectors and payloads.
// Scalar consumers narrow each payload to the original DWORD/WORD/BYTE width at the point of use.
typedef struct ScMessage {
#ifdef __cplusplus
  // Initialize a typed message with empty payloads. The native default argument preserves zero-message initialization.
  constexpr ScMessage(ScMsgCode type = 0) : type(type), data1(0), data2(0), data3(0) {
  }

  // Preserve all four SDK message words. Native-width payloads retain actual pointers on a64-bit host.
  constexpr ScMessage(ScMsgCode type, uptr first, uptr second, uptr third)
      : type(type), data1(first), data2(second), data3(third) {
  }
#endif
  ScMsgCode type;
  uptr data1, data2, data3;
} ScMessage;

struct ScPoint {
  i32 x, y;
};

// The original cRZSize used by the snapshot size vector holds two signed dimensions.
struct ScSize {
  i32 width, height;
};

// Terrain water paths expose two unsigned DWORD coordinates at an eight-byte stride.
struct ScPointXZ {
  u32 x, z;
};

// Known xyz fields of sIGZPoint3DFloat32, read at offsets0/4/8 by the terrain vector methods.
// This native value does not assert the original type's complete size or alignment.
struct ScPoint3F {
  f32 x, y, z;
};

// Actual cRZ3DPointFloat32/cRZTriple<float,float,float> value: three fields and no virtual state.
// Tornado producer constructor places this twelve-byte member at228 before speed240; default construction is empty.
struct ScVec3F {
  f32 x, y, z;
};

// Actual cRZ3DPointSint32 value used by earthquake, Smite and whirlpool initialization.
// Signed X/Y/Z words occupy twelve bytes; the middle coordinate is altitude in fixed-position consumers.
struct ScVec3I {
  i32 x, y, z;
};

// Actual cRZ3DPointUint32 value used by crop targets and locust swarm distribution.
// The middle coordinate is altitude; the established vector spelling remains x/y/z.
struct ScVec3U {
  u32 x, y, z;
};

static_assert(sizeof(ScVec3F) == 12);
static_assert(sizeof(ScVec3I) == 12);
static_assert(sizeof(ScVec3U) == 12);

class ScStringView;

// Original IID81C0CB7B query-string facet; methods12/16 follow the genuine Unknown prefix.
class ScQueryInfoString : public ScUnknown {
public:
  virtual u32 GetInfoStringCount(u32 kind) = 0;
  virtual u8 GetInfoStrings(
      u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *firstText, ScStringView *secondText) = 0;
};

class ScMessageQueue;

// Retail cIGZMessageQueueManager order through36. Pointer-valued queue keys use native width on the portable host.
class ScQueueManager : public ScUnknown {
public:
  virtual bool GetQueue(ScQueueKey key, ScMessageQueue **out) = 0;
  virtual bool AddQueue(ScQueueKey key) = 0;
  virtual bool AddQueue(ScQueueKey key, ScMessageQueue *queue) = 0;
  virtual bool RemoveQueue(ScQueueKey key, bool release) = 0;
  virtual bool RemoveQueue(ScMessageQueue *queue, bool release) = 0;
  virtual bool RemoveAllQueues(bool release) = 0;
  virtual u32 GetQueueCount() = 0;
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cIGZUnknownList.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class ScUnknownList : public ScUnknown {

public:
  virtual usize GetSize() = 0;

  virtual bool GetElement(u32 dwOffset, u32 riid, void **ppvObj) = 0;
  virtual bool EnumElements(ScUnknownEnumerator *sEnum, ScUnknown *pUnknown, u32 riid) = 0;

  virtual bool Push_Front(ScUnknown *pElem) = 0;
  virtual bool Push_Front(ScUnknownList const &sList) = 0;

  virtual bool Push_Back(ScUnknown *pElem) = 0;
  virtual bool Push_Back(ScUnknownList const &sList) = 0;

  virtual bool Peek_Front(u32 riid, void **ppvObj) = 0;
  virtual bool Peek_Back(u32 riid, void **ppvObj) = 0;
  virtual bool Pop_Front(u32 riid, void **ppvObj) = 0;
  virtual bool Pop_Back(u32 riid, void **ppvObj) = 0;

  virtual bool Clear() = 0;
  virtual bool CopyFrom(ScUnknownList const &sList) = 0;
  virtual bool CreateClone(ScUnknownList **ppList) = 0;

  virtual bool ConstEnumElements(ScUnknownEnumerator *sEnum, ScUnknown *pUnknown, u32 riid) const = 0;
};

class ScStringView;
struct ScPoint;
struct ScRect;
struct ScSize;

// Original variant tags distinguish scalar storage, retained objects and owned array payloads.
// These are protocol values; keep GetType and constructor arguments at their original u32 width.
enum ScValueTag : u32 {
  kScValueEmpty = 0,
  kScValueUint8 = 1,
  kScValueUint16 = 2,
  kScValueUint32 = 3,
  kScValueUint64 = 4,
  kScValueSint8 = 5,
  kScValueSint16 = 6,
  kScValueSint32 = 7,
  kScValueSint64 = 8,
  kScValueFloat32 = 9,
  kScValueFloat64 = 10,
  kScValueBool = 11,
  kScValueChar = 12,
  kScValueUnknown = 0x7fff,
  kScValueOwnedArray = 0x8000,
  kScValueData = kScValueOwnedArray,
  kScValueUint8Array = kScValueOwnedArray | kScValueUint8,
  kScValueUint16Array = kScValueOwnedArray | kScValueUint16,
  kScValueUint32Array = kScValueOwnedArray | kScValueUint32,
  kScValueUint64Array = kScValueOwnedArray | kScValueUint64,
  kScValueSint8Array = kScValueOwnedArray | kScValueSint8,
  kScValueSint16Array = kScValueOwnedArray | kScValueSint16,
  kScValueSint32Array = kScValueOwnedArray | kScValueSint32,
  kScValueSint64Array = kScValueOwnedArray | kScValueSint64,
  kScValueFloat32Array = kScValueOwnedArray | kScValueFloat32,
  kScValueFloat64Array = kScValueOwnedArray | kScValueFloat64,
  kScValueBoolArray = kScValueOwnedArray | kScValueBool,
  kScValueCharArray = kScValueOwnedArray | kScValueChar,
  kScValueRect = 0x800d,
  kScValuePoint = 0x800e,
  kScValueSize = 0x800f,
};

// Retail cIGZVariant127C, retaining its complete virtual order through slot332.
// As* returns lower original const references to pointer carriers; Ref* getters borrow stored pointers.
// Overloaded Ref groups declare getters first to retain the original MSVC setter/getter slot order.
class ScVariant : public ScUnknown {
public:
  virtual u32 GetType() const = 0;
  virtual u32 GetCount() const = 0;
  virtual bool Erase() = 0;
  virtual bool CopyFrom(const ScVariant &source) = 0;
  virtual bool CreateCopy(ScVariant **out) const = 0;
  virtual bool GetValBool(bool *out) const = 0;
  virtual bool SetValBool(bool input) = 0;
  virtual bool GetValSint8(i8 *out) const = 0;
  virtual bool SetValSint8(i8 input) = 0;
  virtual bool GetValUint8(u8 *out) const = 0;
  virtual bool SetValUint8(u8 input) = 0;
  virtual bool GetValSint16(i16 *out) const = 0;
  virtual bool SetValSint16(i16 input) = 0;
  virtual bool GetValUint16(u16 *out) const = 0;
  virtual bool SetValUint16(u16 input) = 0;
  virtual bool GetValSint32(i32 *out) const = 0;
  virtual bool SetValSint32(i32 input) = 0;
  virtual bool GetValUint32(u32 *out) const = 0;
  virtual bool SetValUint32(u32 input) = 0;
  virtual bool GetValSint64(i64 *out) const = 0;
  virtual bool SetValSint64(i64 input) = 0;
  virtual bool GetValUint64(u64 *out) const = 0;
  virtual bool SetValUint64(u64 input) = 0;
  virtual bool GetValFloat32(f32 *out) const = 0;
  virtual bool SetValFloat32(f32 input) = 0;
  virtual bool GetValFloat64(f64 *out) const = 0;
  virtual bool SetValFloat64(f64 input) = 0;
  virtual bool GetValRZChar(char *out) const = 0;
  virtual bool SetValRZChar(char input) = 0;
  virtual bool CreateValString(ScStringView **out) const = 0;
  virtual bool GetValString(ScStringView *out) const = 0;
  virtual bool SetValString(const ScStringView &text) = 0;
  virtual bool GetValUnknown(u32 iid, void **out) = 0;
  virtual bool SetValUnknown(ScUnknown *object) = 0;
  virtual const u8 *AsUint8() const = 0;
  virtual const u16 *AsUint16() const = 0;
  virtual const u32 *AsUint32() const = 0;
  virtual const u64 *AsUint64() const = 0;
  virtual const i8 *AsSint8() const = 0;
  virtual const i16 *AsSint16() const = 0;
  virtual const i32 *AsSint32() const = 0;
  virtual const i64 *AsSint64() const = 0;
  virtual const f32 *AsFloat32() const = 0;
  virtual const f64 *AsFloat64() const = 0;
  virtual const bool *AsBool() const = 0;
  virtual const char *AsRZChar() const = 0;
  virtual u8 *RefUint8() const = 0;
  virtual void RefUint8(const u8 *data, u32 amount) = 0;
  virtual u16 *RefUint16() const = 0;
  virtual void RefUint16(const u16 *data, u32 amount) = 0;
  virtual u32 *RefUint32() const = 0;
  virtual void RefUint32(const u32 *data, u32 amount) = 0;
  virtual u64 *RefUint64() const = 0;
  virtual void RefUint64(const u64 *data, u32 amount) = 0;
  virtual i8 *RefSint8() const = 0;
  virtual void RefSint8(const i8 *data, u32 amount) = 0;
  virtual i16 *RefSint16() const = 0;
  virtual void RefSint16(const i16 *data, u32 amount) = 0;
  virtual i32 *RefSint32() const = 0;
  virtual void RefSint32(const i32 *data, u32 amount) = 0;
  virtual i64 *RefSint64() const = 0;
  virtual void RefSint64(const i64 *data, u32 amount) = 0;
  virtual f32 *RefFloat32() const = 0;
  virtual void RefFloat32(const f32 *data, u32 amount) = 0;
  virtual f64 *RefFloat64() const = 0;
  virtual void RefFloat64(const f64 *data, u32 amount) = 0;
  virtual bool *RefBool() const = 0;
  virtual void RefBool(const bool *data, u32 amount) = 0;
  virtual char *RefRZChar() const = 0;
  virtual void RefRZChar(const char *data, u32 amount) = 0;
  virtual ScRect *RefRect() const = 0;
  virtual void RefRect(const ScRect *data) = 0;
  virtual ScPoint *RefPoint() const = 0;
  virtual void RefPoint(const ScPoint *data) = 0;
  virtual ScSize *RefSize() const = 0;
  virtual void RefSize(const ScSize *data) = 0;
  virtual void *RefData() const = 0;
  virtual void RefData(void *data, u32 size) = 0;
  virtual ScUnknown *RefUnknown() const = 0;
  virtual bool RefUnknown(u32 iid, void **out) = 0;
  virtual void RefUnknown(ScUnknown *object) = 0;
};

struct HINSTANCE__;
struct HWND__;
class ScFramework;

// Reached Win32 framework identity23EA70A1; GetInstance is original virtual16, with no borrowed-handle substitution.
class ScWin32Framework : public ScUnknown {
public:
  virtual ScFramework *GetFramework() = 0;
  virtual HINSTANCE__ *GetInstance() = 0;
  virtual HWND__ *GetWindowHandle() = 0;
  // Original24 is a void HWND setter, confirmed by authentic cGZFrameWorkW95 CodeView.
  virtual void SetWindowHandle(HWND__ *window) = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3CityCellMap.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
template <class T> class ScCellMap : public ScCellMapBase {

public:
  virtual bool Init(CtCity *, T const *) = 0;
  virtual bool Shutdown() = 0;
  virtual void GetValue(u32, u32, T *) const = 0;
  virtual void SetValue(u32, u32, T const *) = 0;
  virtual void SetAllCells(T const *) = 0;
  virtual void SetValue(u32, u32, u32, u32, T const *) = 0;
};

/*
 *  Scion - an open-source implementation of the Maxis GZCOM/RZCOM framework
 *  Copyright (C) 2022  Nelson Gomez (nsgomez) <nelson@ngomez.me>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation, under
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

// Retail cIGZMessageQueue order through112; overload groups use the original MSVC slot order.
class ScMessageQueue : public ScUnknown {
public:
  virtual void PushBack(const ScMessage &message) = 0;
  virtual void PushFront(const ScMessage &message) = 0;
  virtual bool RemoveFirstFound(const ScMessage &message) = 0;
  virtual bool RemoveFirstFound(ScMsgCode type) = 0;
  virtual bool RemoveLastFound(const ScMessage &message) = 0;
  virtual bool RemoveLastFound(ScMsgCode type) = 0;
  virtual u32 RemoveAllFound(const ScMessage &message) = 0;
  virtual u32 RemoveAllFound(ScMsgCode type) = 0;
  virtual u32 RemoveAll() = 0;
  virtual ScMsgCode PeekFrontType() = 0;
  virtual ScMsgCode PeekBackType() = 0;
  virtual bool PeekAnywhereType(ScMsgCode type) = 0;
  virtual ScMessage *PeekFrontMessage() = 0;
  virtual ScMessage *PeekBackMessage() = 0;
  virtual bool PeekAnywhereMessage(const ScMessage &message) = 0;
  virtual ScMsgCode GetFrontType() = 0;
  virtual ScMsgCode GetBackType() = 0;
  virtual bool GetAnywhereType(ScMsgCode type) = 0;
  virtual bool GetFrontMessage(ScMessage *out) = 0;
  virtual bool GetBackMessage(ScMessage *out) = 0;
  virtual bool GetAnywhereMessage(const ScMessage &message) = 0;
  virtual ScMsgCode GetFrontTypeBlock() = 0;
  virtual ScMsgCode GetBackTypeBlock() = 0;
  virtual bool GetFrontMessageBlock(ScMessage *out) = 0;
  virtual bool GetBackMessageBlock(ScMessage *out) = 0;
  virtual u32 GetMessageCount() = 0;
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original cIGZMessageTarget; the mutable message uses the project's explicit-pointer spelling.
class ScMessageTarget : public ScUnknown {
public:
  virtual bool DoMessage(ScMessage *message) = 0;
};

// Retail cISC3QueryInfo29AF derives from cIGZMessageTarget15D3, then cIGZUnknown11DC at offset zero.
// Preserve that shared Unknown/message/query identity and its installed IID81C0CB7C.
class ScQueryTarget : public ScMessageTarget {
public:
  virtual bool DoQueryInfo(ScMessage *message, ScUnknown *sender) = 0;
};

#endif
