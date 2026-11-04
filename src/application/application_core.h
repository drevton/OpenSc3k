#ifndef SC3_APPLICATION_APPLICATION_CORE_H
#define SC3_APPLICATION_APPLICATION_CORE_H

#include <map>
#include <unordered_map>
#include <unordered_set>

#include <atomic>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

// Use the explicit Win32 API spelling so its alias cannot rename the game's menu interface methods.
#ifdef GetMenuItemInfo
#undef GetMenuItemInfo
#endif

#include <vector>
#endif

#include "application/api.h"
#include "application/reference_count.h"
#include "application/window_controls.h"
#include "audio/local_radio.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/cheat_manager.h"
#include "common/com.h"
#include "common/sc3_array.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_director.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "resources/file.h"
#include "resources/indexed_file.h"
#include "ui/generic_dialogs.h"

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original SystemPerformanceInfo from retail CodeView24BA: two signed WORD measurements and two char ranks.
// Its actual hardware-probing constructor is separate from consumers borrowing this six-byte record.
struct ApPerformanceInfo {
  ApPerformanceInfo();
  i16 processor_mhz, memory_mb;
  i8 processor_power, memory_power;
};

static_assert(sizeof(ApPerformanceInfo) == 6);

// Original cIGZApp from retail CodeView2613 derives directly from Unknown and occupies slots12..56.
// AddApplicationService is void and slot32 is Run; newer SDK spellings do not replace these recovered contracts.
class ApApp : public ScUnknown {
public:
  virtual ScSystemService *AsIGZSystemService() = 0;
  virtual void AddApplicationService(ScSystemService *service) = 0;
  virtual char const *ModuleName() = 0;
  virtual bool PreFrameWorkInit() = 0;
  virtual bool PostFrameWorkInit() = 0;
  virtual bool Run() = 0;
  virtual bool PreFrameWorkShutdown() = 0;
  virtual ScFramework *FrameWork() = 0;
  virtual void AddDynamicLibrariesHere() = 0;
  virtual void AddCOMDirectorsHere() = 0;
  virtual void AddApplicationServicesHere() = 0;
  virtual bool LoadRegistry() = 0;
};

// Original cGZApp service base and secondary application identity, with its owned module-name String.
// Release removes a reference without deleting this process-owned object; the registered factory destructor owns deletion.
class ApBaseApp : public ScServiceBase, public ApApp {
public:
  explicit ApBaseApp(const char *name);
  ApBaseApp(u32 identifier, const char *name);
  ~ApBaseApp() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  ScSystemService *AsIGZSystemService() override;
  void AddApplicationService(ScSystemService *service) override;
  const char *ModuleName() override;
  bool PreFrameWorkInit() override;
  bool PostFrameWorkInit() override;
  bool Run() override;
  bool PreFrameWorkShutdown() override;
  ScFramework *FrameWork() override;
  void AddDynamicLibrariesHere() override;
  void AddCOMDirectorsHere() override;
  void AddApplicationServicesHere() override;
  bool LoadRegistry() override;

protected:
  ScString module_name;
};

// Original process hooks4FAB68/4FAB6C. Application registration supplies the concrete factory and optional destructor.
extern ApBaseApp *(*ApApplicationFactory)();
extern void (*ApApplicationDestroy)(ApBaseApp *application);

// Original cSC3AppStateManager. Retail CodeView3601 fields359F/35D8 prove hash_map and hash_set use.
// The native standard hash containers replace those original STL facilities; targets and server remain borrowed.
class ApStateController : public ApStateManager {
public:
  ApStateController();
  virtual ~ApStateController();
  bool Init(ScMessageServer *server);
  bool Shutdown();
  u32 GetCurrentUIState() override;
  bool SetCurrentUIState(u32 state) override;
  ScMessageTarget *GetCurrentStateTarget() override;
  bool RegisterUIState(u32 state, ScMessageTarget *target) override;
  bool UnregisterUIState(u32 state) override;
  bool UnregisterAllUIStates() override;
  bool EnumerateUIStates(ScMessageTarget *target) override;
  bool GetFlag(u32 flag) override;
  void SetFlag(u32 flag) override;
  void ClearAllFlags() override;

private:
  u32 current_state;
  ScMessageTarget *current_target;
  ScMessageServer *message_server;
  std::unordered_map<u32, ScMessageTarget *> states;
  std::unordered_set<u32> flags;
};

// Original cSC3IdleStateDetector adds no fields to the real24-byte cRZSystemService base.
class ApIdleDetector : public ScServiceBase {
public:
  ApIdleDetector();
  ~ApIdleDetector() override;
  bool OnIdle(u32 elapsed) override;
};

class FtFont;

// Retail cISC3AdvDlgMgr type 3627/field list A94C follows the message-target prefix with these two operations.
class AdDialogManager : public ScMessageTarget {
public:
  virtual u8 Init() = 0;
  virtual u8 Shutdown() = 0;
};

// Retail cIGZWinToolTipMgr type 362E/field list A968 has the same prefix and retains the original font/char inputs.
class UiToolTipManager : public ScMessageTarget {
public:
  virtual bool Init(FtFont *font, i8 first, i8 second) = 0;
  virtual bool Shutdown() = 0;
};

// Original executable director adds framework initialization to the common director without additional fields.
class ApDirector : public ScDllDirector {
public:
  ApDirector();
  ~ApDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  virtual bool InitializeFrameWork();
};

// The actual Windows platform child has its own static lifetime and otherwise uses the common director contracts.
class ApPlatformDirector : public ScDllDirector {
public:
  ApPlatformDirector();
  ~ApPlatformDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

// Executable COM director owns the real class registration table and uses the common framework hook identity.
class ApComDirector : public ScDllDirector {
public:
  ApComDirector();
  ~ApComDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  static ScUnknown *CreateCommandFactory();
  static ScUnknown *CreateAnimationManager();
  static ScUnknown *CreateCheatCodeEntryDialog();
  static ScUnknown *CreateSpriteFilterCommandFactory();
  static ScUnknown *CreateWinSC3();
  static ScUnknown *CreateDancingBear();
  static ScUnknown *CreateSysInfoWindow();
};

ScDllDirector *ApGetPlatformDirector();

struct ApIniSection {
  ScString *name;
  u32 position;
};

// The original generic INI enumeration ABI transports a borrowed caller context without assuming an interface.
struct ApIniContext;
using ApIniCallback = void(SC3_CDECL *)(const ScString &key, const ScString &value, ApIniContext *context);

// Native storage for the original String-to-offset index; lookup retains full byte ranges and the first duplicate.
// Generic tree allocation is replaced by the existing project array, without changing parser or file operations.
struct ApIniIndex {
  ~ApIniIndex();
  void Clear();
  ApIniSection *Find(const ScString *name);
  void Insert(const ScString *name, u32 position);
  ScArray<ApIniSection> entries;
};

// Original cRZIniFile owns its filename, section index and file.
class ApIniFile {
public:
  ApIniFile();
  explicit ApIniFile(const ScString &source);
  virtual ~ApIniFile();
  virtual const ScString *GetPath() const;
  virtual void SetPath(const ScString &source);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool ReadEntry(const ScString &section, const ScString &key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size);
  virtual bool EnumEntries(const ScString &section, ApIniCallback callback, ApIniContext *context);
  virtual bool EnumSections(ApIniCallback callback, ApIniContext *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool leave_open);
  ScString name;
  bool keep_open, indexed;
  ApIniIndex sections;
  RsFile file;

protected:
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original VideoSettings is44 bytes; its flags are raw bytes and its driver identifier occupies32 bytes at offset10.
struct ApVideoSettings {
  u32 resolution_x, resolution_y;
  u8 use_hardware_acceleration, windowed, driver_identifier[32];
};

static_assert(sizeof(ApVideoSettings) == 44);
static_assert(offsetof(ApVideoSettings, resolution_x) == 0);
static_assert(offsetof(ApVideoSettings, resolution_y) == 4);
static_assert(offsetof(ApVideoSettings, use_hardware_acceleration) == 8);
static_assert(offsetof(ApVideoSettings, windowed) == 9);
static_assert(offsetof(ApVideoSettings, driver_identifier) == 10);

// Original SoundSettings retains byte flags and signed volumes; intervening padding is not preference data.
struct ApSoundSettings {
  u8 enable_3d_sound, music;
  i32 music_volume;
  u8 sound_effects;
  i32 sound_effects_volume;
  u8 ambience;
  i32 ambience_volume;
};

static_assert(sizeof(ApSoundSettings) == 24);
static_assert(offsetof(ApSoundSettings, enable_3d_sound) == 0);
static_assert(offsetof(ApSoundSettings, music) == 1);
static_assert(offsetof(ApSoundSettings, music_volume) == 4);
static_assert(offsetof(ApSoundSettings, sound_effects) == 8);
static_assert(offsetof(ApSoundSettings, sound_effects_volume) == 12);
static_assert(offsetof(ApSoundSettings, ambience) == 16);
static_assert(offsetof(ApSoundSettings, ambience_volume) == 20);

// Original simulation preferences retain three raw byte flags and three independent resource-set identifiers.
struct ApSimSettings {
  u8 auto_goto, auto_budget, enable_disasters;
  u32 landscape_id, flora_set_id, building_set_id;
};

static_assert(sizeof(ApSimSettings) == 16);
static_assert(offsetof(ApSimSettings, auto_goto) == 0);
static_assert(offsetof(ApSimSettings, auto_budget) == 1);
static_assert(offsetof(ApSimSettings, enable_disasters) == 2);
static_assert(offsetof(ApSimSettings, landscape_id) == 4);
static_assert(offsetof(ApSimSettings, flora_set_id) == 8);
static_assert(offsetof(ApSimSettings, building_set_id) == 12);

// Original visibility preferences store signed zoom-level counts for traffic, people and other animation.
struct ApVisibilitySettings {
  i32 traffic_visibility, sims_visibility, other_animations_visibility;
};

static_assert(sizeof(ApVisibilitySettings) == 12);
static_assert(offsetof(ApVisibilitySettings, traffic_visibility) == 0);
static_assert(offsetof(ApVisibilitySettings, sims_visibility) == 4);
static_assert(offsetof(ApVisibilitySettings, other_animations_visibility) == 8);

// SC3U408FC2 borrows this embedded sixteen-byte query preference record at owner+2384.
// Retail118C identifies the unsigned combo-box value and coordinate words; readiness remains a raw byte.
struct ApQueryPrefs {
  u8 enabled;
  u32 word4, x, y;
};

static_assert(sizeof(ApQueryPrefs) == 16);
static_assert(offsetof(ApQueryPrefs, x) == 8);

// Retail11A0 proves readiness followed by unsigned position and five category-specific selection words.
struct ApCatalogPrefs {
  u8 enabled;
  u32 x, y, selections[5];
};

static_assert(sizeof(ApCatalogPrefs) == 32);
static_assert(offsetof(ApCatalogPrefs, selections) == 12);
static_assert(offsetof(ApQueryPrefs, y) == 12);

// Original EditBATBldgListSettings owns its directory string; the ready byte is independent of its contents.
// Retail CodeView11B3/11B4 proves the PC24-byte record, with the string beginning at offset4.
struct ApEditBldgPrefs {
  u8 ready;
  ScString directory_path;
};

// Retail records preserve their actual unsigned selections/positions and raw readiness bytes.
struct ApMiscWindowPrefs {
  i32 values[16][32];
};

struct ApNewCityPrefs {
  ApNewCityPrefs();
  u8 ready;
  u32 difficulty, city_size, start_date;
  char mayor_name[128];
};

struct ApLoadPrefs {
  u8 ready, show_city_stats;
};

struct ApMapsPrefs {
  u8 ready;
  u32 selected_view;
  u8 show_in_city;
  u32 x, y;
};

struct ApOrdinancesPrefs {
  u8 ready;
  u32 x, y;
};

struct ApNeighborsPrefs {
  u8 ready;
  u32 selected_neighbor, x, y;
};

struct ApBudgetPrefs {
  u8 ready;
  u32 x, y;
};

struct ApAdvisorPrefs {
  u8 ready;
  u32 meeting_x, meeting_y, main_x, main_y, main_page, topic_x, topic_y, petition_x, petition_y, ordinance_x,
      ordinance_y;
};

struct ApChartsPrefs {
  u8 ready;
  u32 selection;
};

static_assert(sizeof(ApMiscWindowPrefs) == 2048);
static_assert(sizeof(ApNewCityPrefs) == 144);
static_assert(sizeof(ApLoadPrefs) == 2);
static_assert(sizeof(ApMapsPrefs) == 20);
static_assert(sizeof(ApOrdinancesPrefs) == 12);
static_assert(sizeof(ApNeighborsPrefs) == 16);
static_assert(sizeof(ApBudgetPrefs) == 12);
static_assert(sizeof(ApAdvisorPrefs) == 48);
static_assert(sizeof(ApChartsPrefs) == 8);

// Original cISC3GraphsSettings has no Unknown prefix or virtual destructor; indices enumerate ascending checkbox IDs.
class ApGraphsPrefs {
public:
  virtual u32 CheckboxCount() = 0;
  virtual bool FindCheckbox(u32 identifier) = 0;
  virtual bool GetCheckboxState(u32 identifier) = 0;
  virtual bool AddCheckbox(u32 identifier, bool state) = 0;
  virtual bool GetCheckboxByIndex(u32 index, u32 *identifier, bool *state) = 0;
  virtual void SetReady(bool ready) = 0;
  virtual bool IsReady() = 0;
  virtual void SetRadioSelection(u32 selection) = 0;
  virtual u32 GetRadioSelection() = 0;
};

struct ApReplacementPrefs;

// Retail cISC3AppPreferences24B8 order through144, including signed language/country and borrowed settings.
class ApPreferences : public ScUnknown {
public:
  virtual bool Load() = 0;
  virtual bool Save() = 0;
  virtual bool IsWindowVisible(i32, i32, i32, i32) = 0;
  virtual ScStringView *GetRadioCallSign() = 0;
  virtual void SetRadioCallSign(ScStringView const &) = 0;
  virtual ScStringView *GetLoadSavePath() = 0;
  virtual void SetLoadSavePath(ScStringView *) = 0;
  virtual ApVideoSettings *VideoPrefs() = 0;
  virtual ApSoundSettings *SoundPrefs() = 0;
  virtual ApSimSettings *SimPrefs() = 0;
  virtual ApVisibilitySettings *VisibilityPrefs() = 0;
  virtual ApTipPrefs *TODPrefs() = 0; // SC3U408F9F returns borrowed embedded storage; SDK intptr_t was opaque.
  virtual ApMiscWindowPrefs *MiscWindowPrefs() = 0;
  virtual ApNewCityPrefs *NewCityPrefs() = 0;
  virtual ApLoadPrefs *LoadPrefs() = 0;
  virtual ApMapsPrefs *MapsWindowPrefs() = 0;
  virtual ApQueryPrefs *QueryDialogPrefs() = 0;
  virtual ApOrdinancesPrefs *OrdinancesWindowPrefs() = 0;
  virtual ApNeighborsPrefs *NeighborsDialogPrefs() = 0;
  virtual ApBudgetPrefs *BudgetDialogPrefs() = 0;
  virtual ApAdvisorPrefs *AdvMgrPrefs() = 0;
  virtual ApCatalogPrefs *SelectABuildingPrefs() = 0;
  virtual ApChartsPrefs *ChartsPrefs() = 0;
  virtual ApGraphsPrefs *GraphsPrefs() = 0;
  virtual ApReplacementPrefs *BldgReplacementPrefs() = 0; // SC3U408FFA returns original mutable settings reference.
  virtual ApEditBldgPrefs *EditBATBldgListPrefs() = 0;    // SC3U409001 borrows this embedded record.
  virtual i32 GetLanguage() = 0;
  virtual void SetLanguage(i32) = 0;
  virtual i32 GetCountry() = 0;
  virtual void SetCountry(i32) = 0;
  virtual bool GetIntelliScroll() = 0;
  virtual void SetIntelliScroll(bool) = 0;
  virtual bool IsSongEnabled(u32) = 0;
  virtual void EnableDisableSong(u32, bool) = 0;
};

class FtFont;
class WnAppInfoList;

// Original cISC3WinSysInfo extends the window procedure with its single creation method.
class ApSysInfo : public UiWindowCallback {
public:
  virtual bool CreateSysInfoWindow(WnWindow **out) = 0;
};

// Original cSC3SysInfoWindow owns two borrowed font views and its director-backed reference count.
class ApSysInfoWindow : public ApSysInfo {
public:
  ApSysInfoWindow();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool CreateSysInfoWindow(WnWindow **out) override;
  virtual ~ApSysInfoWindow();

protected:
  bool EndDialog(WnWindow *window, u32 result);
  bool OnInit(WnWindow *window);
  bool OnShutdown(WnWindow *window);
  bool OnGetMinMaxInfo(WnWindow *window, ScRect *limits);
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  bool AddImageButton(WnWindow *window, u32 identifier, u32 x, u32 y, u32 image, char shortcut);
  void PutInfoIntoTheInfoListBoxWindow(WnAppInfoList *list);
  FtFont *right_font, *left_font;
  u32 references;
};

static_assert(sizeof(uptr) != 4 || sizeof(ApSysInfoWindow) == 16);

using ScTimerDispatch = u32 (*)(ScCallbackTimer *timer);

// A scheduled callback stores its original delay, absolute deadline and repetition flags.
struct ScCallbackJob {
  u32 identifier;
  ScTimerDispatch callback;
  ScCallbackTimer *context;
  u8 once;
  u32 delay, deadline;
  u8 fixed;
};

struct ScCallbackNode {
  ScCallbackNode *volatile next, *volatile previous;
  ScCallbackJob job;
};

// The executable owns a circular list of callback jobs, including a sentinel with unused payload.
struct ScCallbackList {
  ScCallbackList();
  ~ScCallbackList();
  void Append(const ScCallbackJob &job);
  void Erase(ScCallbackNode *node);
  void Clear();
  ScCallbackNode *head;
};

// Executable cRZCallbackThread holds the actual job list, stopwatch, worker and recursive lock.
class ScCallbackThread : public ScThreadTarget {
public:
  ScCallbackThread();
  ~ScCallbackThread();
  i32 AddEvent(ScTimerDispatch callback, ScCallbackTimer *context, u32 delay, bool once, bool fixed);
  bool RemoveEvent(i32 identifier);
  u32 StartFunction(void *argument) override;
  ScCallbackList list;
  ScStopwatch elapsed;
  ScThread worker;
  u32 next_id;
  ScLock lock;
  std::atomic<u8> stop, running;
};

extern ScCallbackThread ScCallbacks;

#if defined(_WIN32)
// DbgHelp requires Windows types before its declarations; preserve this dependency order.
// clang-format off
// clang-format on

// Retail AD0D stores two concrete Strings and the image address/size. Native image addresses follow platform pointer width.
struct ScExceptionModule {
  ScString image_name, module_name;
  uptr base;
  u32 size;
};

// Original MSJExceptionHandler has no instance fields: the process filter, filename, file and image-helper imports are static.
class ScExceptionHandler {
public:
  ScExceptionHandler();
  ~ScExceptionHandler();
  void SetLogFileName(char *name);

private:
  static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS *exception);
  static void GenerateExceptionReport(EXCEPTION_POINTERS *exception);
  static char *GetExceptionString(u32 code);
  static i32 GetLogicalAddress(void *address, char *filename, u32 capacity, u32 *section, u32 *offset);
  static void IntelStackWalk(CONTEXT *context);
  static void ImagehlpStackWalk(CONTEXT *context);
  static i32 SC3_CDECL Print(const char *format, ...);
  static i32 InitImagehlpFunctions();
  static void EnumAndLoadModuleSymbols(HANDLE process, u32 process_id);
  static bool FillModuleList(std::vector<ScExceptionModule> *modules, u32 process_id, HANDLE process);
  static bool FillModuleListTH32(std::vector<ScExceptionModule> *modules, u32 process_id);
  static bool FillModuleListPSAPI(std::vector<ScExceptionModule> *modules, u32 process_id, HANDLE process);
  static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
  static char log_filename[MAX_PATH], exception_buffer[512];
  static HANDLE report_file;
  static decltype(&::SymInitialize) sym_initialize;
  static decltype(&::SymCleanup) sym_cleanup;
#if defined(_WIN64)
  static decltype(&::StackWalk64) stack_walk;
  static decltype(&::SymFunctionTableAccess64) sym_function_table;
  static decltype(&::SymGetModuleBase64) sym_module_base;
  static decltype(&::SymGetSymFromAddr64) sym_get_symbol;
  static decltype(&::SymLoadModule64) sym_load_module;
#else
  static decltype(&::StackWalk) stack_walk;
  static decltype(&::SymFunctionTableAccess) sym_function_table;
  static decltype(&::SymGetModuleBase) sym_module_base;
  static decltype(&::SymGetSymFromAddr) sym_get_symbol;
  static decltype(&::SymLoadModule) sym_load_module;
#endif
};
void ApInitializeExceptionHandler();
void ApShutdownExceptionHandler();
#endif

// Concrete Windows framework, with its separate Win32 identity and embedded COM service.
// Unrecovered standard-stream operations stop explicitly in the common base; construction itself is available.
class ApFramework : public ScFrameworkCore, public ScWin32Framework {
public:
  ApFramework();
  ~ApFramework() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScFramework *GetFramework() override;
  HINSTANCE__ *GetInstance() override;
  HWND__ *GetWindowHandle() override;
  void SetWindowHandle(HWND__ *value) override;
  bool PreAppInit() override;
  bool PostAppShutdown() override;
  ScCom *GetCOMObject() override;
  void Run() override;
  void Quit(i32 reason) override;
  [[noreturn]] void AbortiveQuit(i32 reason) override;
  void ReportException(const char *text) override;
  virtual u32 FinalRelease();

protected:
  void ShutdownGZCOM() override;

private:
  HWND__ *window;
  u32 references;
  ScComCore com;
  volatile bool quit_requested;
  i32 quit_code;
  static HINSTANCE__ *instance;
};

// Original optional4FABD8 release target. Its producer and nominal interface remain unresolved.
extern ScUnknown *ApFrameworkReleaseTarget;

// Executable cRZVersion owns the loaded version resource and its borrowed translation entry.
class ScVersion {
public:
  enum class SystemType : i32;
  ScVersion();
  virtual ~ScVersion();
  bool GetModuleActualFileName(ScString *out);
  bool GetModuleName(ScString *out);
  bool GetModuleVersion(ScString *out);
  bool GetModuleCopyright(ScString *out);
  bool GetModuleDebugVersion(ScString *out);
  i32 GetModuleVersion(u32 index);
  bool GetDecryptedOEMString(ScString *out);
  bool GetDecryptedOEMStringFromFile(ScString *file, ScString *out);
  void EncryptOEMString(const char *source, char *destination);
  void DecryptOEMString(const char *source, char *destination);
  bool GetSystemVersion(ScString *out);
  i32 GetSystemVersion(u32 index);
  SystemType GetSystemType();
  i32 GetCPUVersion();
  bool GetCPUVersion(ScString *out);
  u32 GetCPUSpeed();
  bool GetCPUSpeed(ScString *out);
  bool GetCPUDescription(ScString *out);
  bool GetCPUName(ScString *out);
  u32 GetCPUFlags();
  bool GetCPUFlags(ScString *out);
  bool IsCPUClone();
  bool GetSystemMemory(u32 *total, u32 *available);
  bool GetUserNameA(ScString *out, ScString *fallback);

protected:
  void LoadVersionInfo();
  bool ReadVersionField(const char *name, ScString *out);
#if defined(_WIN32)
  HMODULE module;
#else
  void *module;
#endif
  u8 *version_data;
  const u16 *translation;
};

class ScDirector;
struct ApIniContext;
class ScDllDirector;
class ApGameInterface;
class CtCity;
class SpCityWindowView;
class SpImageCache;
class SpManagerView;
class ScFramework;
class ScCom;
class ScMessageServer;
class ScLanguageManager;
class RsManager;
class GxGraphics;
class GxBlendService;
class GxSpanBlt;
class ScVariableManager;
class FtFontSystem;
class WnWindowManager;
class WnResources;
class UiMainWindow;
class ApBoxX;
class AuRadio;
class AuRadioPlayer;
class AuSoundDevice;
class AuInfo;
class AdDialogManager;
class UiToolTipManager;
class ApIdleDetector;
class ScMutex;
class NwInternet;
class ScCommandFactory;
class RsFileSystem;
ScDllDirector *ApGetDirector();
ScFramework *ApGetFramework();
ApGameInterface *ApGetGameApp();
CtCity *ApGetCity();
SpCityWindowView *ApGetCityWindow();
SpViewInterface *ApGetCurrentCityView();
SpManagerView *ApGetCitySpriteManager();
SpCellMap *ApGetCitySpriteCellMap();
SpTickManager *ApGetAnimationTickManager();
ScCom *ApGetCom();
ApPreferences *ApGetPreferences();
// Original411B09 process COM-director producer remains an explicit implementation dependency.
ScDllDirector *ApGetCOMDirector();
RsFileSystem *ApGetFileSystem();
ScMessageServer *ApGetMessageServer();
RsManager *ApGetResourceManager();
GxGraphics *ApGetGraphics();
FtFontSystem *ApGetFontManager();
ScLanguageManager *ApGetLanguageManager();
WnWindowManager *ApGetDialogManager();
WnResources *ApGetUiResources();

// Original SL static accessors have no receiver or separate service ownership.
class ApServices {
public:
  static GxBlendService *AB();
  static GxSpanBlt *SB();
  static ScVariableManager *VM();
  static RsImageOptions *BMP();
  static ApPerformanceInfo *SPI();
  static bool IsCityActive();
  static bool IsWCVActive();
  static ApBoxX *BoxX();
  static ScCommandFactory *GCF();
  static SpImageCache *IM();
  static AuSoundDevice *SS();
  static void DDir(ScString *out);
  static void ADir(ScString *out);
  static void RDir(ScString *out);
  static void PDir(ScString *out);
  static void SDir(ScString *out);
  static CtDefinition *CDef();
  static CtAgentTree *ATT();
  static OcAttribCache *OC();
};

// Original cSC3App bases are cGZApp0, cISC3App48 and cIGZMessageTarget52 on x86.
// Native containers do not overlay the foreign432-byte object; owned subobjects and field order follow retail3635.
class ApApplication : public ApBaseApp, public ApGameInterface, public ScMessageTarget {
public:
  explicit ApApplication(const char *name);
  ~ApApplication() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool PreFrameWorkInit() override;
  void AddApplicationServicesHere() override;
  bool LoadRegistry() override;
  ApBoxX *GetBoxX() override;
  CtCity *GetCity() override;
  bool Quit(bool ask_exit, bool ask_save) override;
  void SetInputFileName(const ScStringView &name) override;
  const ScStringView *GetInputFileName() override;
  void SetRestartOnExit(bool restart) override;
  ScCheatCodeManager *GetCheatCodeManager() override;
  AuRadio *GetRadio() override;
  AuRenderServer *GetSoundRenderServer() override;
  ApStateManager *GetAppStateManager() override;
  CtAgentTree *GetAppAgentTypeTree() override;
  OcAttribCache *GetAppOccupantAttribCache() override;
  CtDefinition *GetAppCityDefinition() override;
  void GetAppIniFileName(ScStringView *out) override;
  void GetAppDataDirectory(ScStringView *out) override;
  void GetAppDirectory(ScStringView *out) override;
  RsImageOptions *GetBufferResourceFactory() override;
  ApPreferences *GetAppPreferences() override;
  ApPerformanceInfo *GetSystemPerformanceInfo() override;
  UiMainWindow *GetSC3MainWindow() override;
  SpCityWindowView *GetISC3WinCityView() override;
  void SetWinCityView(SpCityWindowView *window) override;
  NwInternet *GetSC3Internet() override;
  CtSchemeManager *CitySchemeManager() override;
  void ShowProgressBar(u32 total, ScStringView *text) override;
  void UpdateProgressBar(u32 position) override;
  void HideProgressBar() override;
  void AddDynamicLibrariesHere() override;
  void AddCOMDirectorsHere() override;
  bool PostFrameWorkInit() override;
  bool PreFrameWorkShutdown() override;
  bool OnIdle(u32 elapsed) override;
  bool CreateCity() override;
  bool PostCityInit(i32 first, i32 second, ScStringView *name) override;
  bool ShutdownCity() override;
  void FixStartupResolutionValuesIfNeeded(u32 *width, u32 *height) override;
  bool IsStartupResolutionOK(u32 width, u32 height) override;
  bool PreCityLoad() override;
  virtual void ReadPreferences();
  virtual void ReadCommandLine();
  bool InitUiTheme();
  bool ShutdownUiTheme();
  virtual void FindAndRegisterAllAvailableLanguages();
  virtual void SetupRadio();
  RsManager *GetResourceManager() override;

protected:
  bool initialized;
  WnWindow *root_window = nullptr;
  SpCityWindowView *city_window = nullptr;
  UiMainWindow *main_window = nullptr;
  CtCity *city = nullptr;
  AuRadioPlayer *radio = nullptr;
  AuRenderService sound_renderer;

public:
  ScString app_directory, data_directory, input_filename;

protected:
  static void AddSpriteSegmentsToCSIM(const ScStringView &key, const ScStringView &value, void *context);
  static void AddSpriteImageInfoToCSIM(const ScStringView &key, const ScStringView &value, void *context);
  static void CreateResourceFactoryCallback(const ScStringView &key, const ScStringView &value, void *context);
  static void CreateDBSegmentCallback(const ScStringView &key, const ScStringView &value, void *context);
  static void CreateServiceCallback(const ScStringView &key, const ScStringView &value, void *context);
  static void CreateCommandFactoryCallback(const ScStringView &key, const ScStringView &value, void *context);
  static void RegisterResourceFactoryCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterDBSegmentCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterDBSegmentDirectoryFilesCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterServiceCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterCommandFactoryCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterOccupantCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterAgentTypeCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterAgentRelationCallback(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterSpriteSegments(const ScString &key, const ScString &value, ApIniContext *context);
  static void RegisterImageInfo(const ScString &key, const ScString &value, ApIniContext *context);
  static ApApplication *instance;
  ApStateController state_manager;
  CtDefinition *city_definition = nullptr;
  RsManager *resource_manager = nullptr;
  ApBoxX *box_x = nullptr;
  AuInfo *audio_info = nullptr;
  AdDialogManager *advisor_dialog_manager = nullptr;
  RsImageOptions *buffer_factory = nullptr;
  UiToolTipManager *tooltip_manager = nullptr;
  ApPreferences *preferences = nullptr;
  ApPerformanceInfo performance;
  NwInternet *internet = nullptr;
  CtSchemeManager *scheme_manager = nullptr;
  ApIdleDetector *idle_detector = nullptr;
  ScCheatManager cheats;
  bool cheats_disabled_on_startup, city_started, restart_on_exit;
  ScCommandLine restart_command_line;
  ScMutex *app_mutex = nullptr, *update_mutex = nullptr;
  void DoCheatCode(u32 identifier, const ScStringView &text);
  void CheckCommandLineForInputCityFile();
  void DisplayStartupInformation();
  void CreateRegisteredObjects();
  void RegisterCheatCodeStrings();
  void RemoveCheatCodeStrings();

private:
  void FindDataDirectory(ScString *out);
  bool LaunchBackgroundUpdaterExecutable();
  bool WindowSetup();
  bool WindowShutdown();
  bool GetPathToUpdaterIniFile(ScString *out);
  bool GetPathToUpdaterExecutable(ScString *out);
  bool DoCDCheck();
  bool LaunchManualUpdaterExecutable();
  bool HasUpdateBeenFound();
};

// The executable's cSC3CompressibleINIFile extends the original loose INI reader with a package stream.
class ApCompressedIni : public ApIniFile {
public:
  ApCompressedIni();
  ~ApCompressedIni() override;
  bool WriteEntry(const ScString &section, const ScString &key, const ScString &value) override;
  bool ReadEntry(const ScString &section, const ScString &key, ScString *out) override;
  bool WriteNumber(const ScString &section, const ScString &key, i32 value) override;
  bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size) override;
  bool ReadNumber(const ScString &section, const ScString &key, i32 *out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size) override;
  bool EnumEntries(const ScString &section, ApIniCallback callback, ApIniContext *context) override;
  bool SectionExists(const ScStringView &section) override;
  virtual void SetCompressedPath(const ScString &path);
  virtual const ScString *GetCompressedPath() const;
  virtual bool EnumEntriesOld(const ScString &section, ApIniCallback callback, ApIniContext *context);

protected:
  void InitFileType();
  void ReadString(ScString *out);
  ScString package_name;
  RsFile package_file;
  u32 mode;
  char *buffer;
  u32 buffer_capacity, package_offset;
};

// Retail889B proves map<unsigned long,bool> storage; the original has no separate constructor body.
class ApGraphSettings : public ApGraphsPrefs {
public:
  u32 CheckboxCount() override;
  bool FindCheckbox(u32 identifier) override;
  bool GetCheckboxState(u32 identifier) override;
  bool AddCheckbox(u32 identifier, bool state) override;
  bool GetCheckboxByIndex(u32 index, u32 *identifier, bool *state) override;
  void SetReady(bool ready) override;
  bool IsReady() override;
  void SetRadioSelection(u32 selection) override;
  u32 GetRadioSelection() override;
  std::map<u32, bool> checkboxes;
  bool ready;
  u32 selection;
};

// Actual cSC3AppPreferences owner, with ordinary String/database/map members instead of a foreign byte overlay.
// The actual record reader and writer preserve all fifteen original resource-key schemas.
class ApPreferencesObject : public ApPreferences, public ApRuntime::ScRefCount {
public:
  ApPreferencesObject();
  ~ApPreferencesObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Load() override;
  bool Save() override;
  bool IsWindowVisible(i32 x, i32 y, i32 width, i32 height) override;
  ScStringView *GetRadioCallSign() override;
  void SetRadioCallSign(const ScStringView &value) override;
  ScStringView *GetLoadSavePath() override;
  void SetLoadSavePath(ScStringView *value) override;
  ApVideoSettings *VideoPrefs() override;
  ApSoundSettings *SoundPrefs() override;
  ApSimSettings *SimPrefs() override;
  ApVisibilitySettings *VisibilityPrefs() override;
  ApTipPrefs *TODPrefs() override;
  ApMiscWindowPrefs *MiscWindowPrefs() override;
  ApNewCityPrefs *NewCityPrefs() override;
  ApLoadPrefs *LoadPrefs() override;
  ApMapsPrefs *MapsWindowPrefs() override;
  ApQueryPrefs *QueryDialogPrefs() override;
  ApOrdinancesPrefs *OrdinancesWindowPrefs() override;
  ApNeighborsPrefs *NeighborsDialogPrefs() override;
  ApBudgetPrefs *BudgetDialogPrefs() override;
  ApAdvisorPrefs *AdvMgrPrefs() override;
  ApCatalogPrefs *SelectABuildingPrefs() override;
  ApChartsPrefs *ChartsPrefs() override;
  ApGraphsPrefs *GraphsPrefs() override;
  ApReplacementPrefs *BldgReplacementPrefs() override;
  ApEditBldgPrefs *EditBATBldgListPrefs() override;
  i32 GetLanguage() override;
  void SetLanguage(i32 value) override;
  i32 GetCountry() override;
  void SetCountry(i32 value) override;
  bool GetIntelliScroll() override;
  void SetIntelliScroll(bool value) override;
  bool IsSongEnabled(u32 identifier) override;
  void EnableDisableSong(u32 identifier, bool enabled) override;

protected:
  bool TryToReadRecord(u32 type, u32 instance);
  bool TryToWriteRecord(u32 type, u32 instance);
  ScString radio_call_sign, load_save_path;
  i32 language, country;
  bool intelli_scroll;
  ApVideoSettings video;
  ApSoundSettings sound;
  ApSimSettings simulation;
  ApVisibilitySettings visibility;
  ApTipPrefs tips;
  ApMiscWindowPrefs miscellaneous;
  ApNewCityPrefs new_city;
  ApLoadPrefs load;
  ApMapsPrefs maps;
  ApQueryPrefs query;
  ApOrdinancesPrefs ordinances;
  ApNeighborsPrefs neighbors;
  ApBudgetPrefs budget;
  ApAdvisorPrefs advisors;
  ApCatalogPrefs catalog;
  ApChartsPrefs charts;
  ApGraphSettings graphs;
  ApReplacementPrefs replacement;
  ApEditBldgPrefs edit_buildings;
  bool ready;
  RsIndexedFile database;
  ScString path;
  // RetailA6FF member mSongBoolsMap proves the original ordered map, including its default-enabled missing key rule.
  std::map<u32, bool> songs;
};

#endif
