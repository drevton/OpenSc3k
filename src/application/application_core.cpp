#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "application/application_core.h"

#include <assert.h>
#include <ctype.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#include <string.h>

// The installed theme image is a mottled gray texture; its original control association remains unresolved.
enum ApGrayTextureArtwork : u32 {
  kApThemeGrayTextureArtworkId = 0x42b99d48,
};

// The installed horizontal slider atlas contains its track and round thumb side by side.
enum ApHorizontalSliderArtwork : u32 {
  kApThemeHorizontalSliderArtworkId = 0xc2b99e09,
};

// Theme registration and the consuming controls identify these slider, scrollbar, spinner, combo, popup and checkbox resources.
enum ApThemeControlArtwork : u32 {
  kApThemeSliderArtworkId = 0x2b99f6f,
  kApThemeScrollbarArtworkId = 0xe2b99e01,
  kApThemeSpinnerArtworkId = 0x82b99d9d,
  kApThemeComboArrowArtworkId = 0x62b99d31,
  kApThemePopupBackgroundArtworkId = 0xe2b99df9,
  kApThemeCheckBoxArtworkId = 0xc2d6e93a,
};

// The quit command advertises one integer mode using this original descriptor word on Windows and Linux.
// The individual flag bits remain uninterpreted; Execute consumes mode values independently.
enum ApQuitModeParameter : u32 {
  kApQuitModeParameterFlags = 0x810d2e8a,
};

// The sprite-filter command factory constructs the grid-toggle command for this identity.
enum ApGridCommandIdentity : u32 {
  kApGridToggleCommandId = 0x2229205c,
};

// Base applications register in the original application-initialization priority band.
enum ApBaseServiceOrdering : i32 {
  kApBaseServicePriority = -2000000,
};

// OEM string decoding reads the same original string-resource entry from either module.
enum ScOemResourceIdentity : u32 {
  kScOemStringResourceId = 0x186a0,
};

// The original graphics summary divides both video-memory categories by this retained report scale.
enum ApMemoryReportScale : u32 {
  kApVideoMemoryReportDivisor = 0xfa000,
};

// The CMOS-synchronized cycle sample retains its million-fold scale and separate rounding divisor.
enum ScCpuClockScale : u32 {
  kScCpuClockSampleScale = 0xf4240,
  kScCpuClockRoundingDivisor = 0x186a0,
};

// Counter-based CPU measurements retain their duration scale after the frequency divisor is reduced by ten.
enum ScCpuTimerScale : u32 {
  kScCpuTimerDurationScale = 0x186a0,
};

// The native frequency result rounds hertz to the nearest whole megahertz.
enum ScCpuFrequencyUnit : i32 {
  kScCpuHertzPerMegahertz = 0xf4240,
  kScCpuMegahertzRoundingBias = 0x7a120,
};

#if defined(_WIN32)
#include <process.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>
#else
#include <pwd.h>
#include <spawn.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
extern char **environ;
#endif
#endif

#include <SDL3/SDL.h>

#include "graphics/sdl_platform.h"
void WnDispatchSdlEvent(const SDL_Event *event);

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

#include "application/api.h"
#include "application/game_command_factory.h"
#include "application/local_album.h"
#include "application/main_dialogs.h"
#include "application/newscast_ui.h"
#include "application/window_controls.h"
#include "audio/api.h"
#include "audio/local_radio.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "city/services.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/locale.h"
#include "common/path.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_debug_stream.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_registry.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "fonts/api.h"
#include "graphics/api.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/indexed_file.h"
#include "resources/manager_object.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/module.h"
#include "terrain/api.h"
#include "terrain/tr_dirt_bag.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_file_cursor.h"
#include "ui/ui_main_window.h"
#include "ui/ui_menu_config.h"
#include "ui/ui_resource_handles.h"
#include "ui/ui_window.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// The MMX feature bit is read from CPU flags; toggling the ID flag probes CPUID availability.
enum ApCpuFeatureFlag : u32 {
  kApCpuMmxFeatureBit = 0x800000,
  kApCpuIdToggleFlag = 0x200000,
  kApCpuAlignmentCheckFlag = 0x40000,
  kApCpuLegacyHighFlags = 0xf000,
};

// The system-information dialog uses separate window, completion button and background identities.
enum ApSystemInfoControlId : u32 {
  kApSystemInfoWindowId = 0x22f10b8a,
  kApSystemInfoCloseButtonId = 0x22f10b8c,
  kApSystemInfoBackgroundId = 0x22f10b8e,
};

// Preference record keys are named for their actual serialized settings; cheat data uses separate framed records.
enum ApPreferenceRecordKey : u32 {
  kApPreferencesRecordGroup = 0x230e18c3,
  kApGeneralPreferencesRecordType = 0xa352f1b5,
  kApGeneralPreferencesRecordInstance = 0x39ef6ff9,
  kApMusicSelectionPreferencesRecordType = 0x6314c631,
  kApMusicSelectionPreferencesRecordInstance = 0x6313bf98,
  kApNewCityPreferencesRecordType = 0x4324f379,
  kApNewCityPreferencesRecordInstance = 0x39ef6f00,
  kApLoadCityPreferencesRecordType = 0x324f37e,
  kApLoadCityPreferencesRecordInstance = 0x4324f45e,
  kApMapsPreferencesRecordType = 0xa324f381,
  kApMapsPreferencesRecordInstance = 0xa324f46a,
  kApQueryPreferencesRecordType = 0x324f385,
  kApQueryPreferencesRecordInstance = 0xc324f475,
  kApOrdinancesPreferencesRecordType = 0xc324f388,
  kApOrdinancesPreferencesRecordInstance = 0xc324f47f,
  kApNeighborsPreferencesRecordType = 0x2324f38b,
  kApNeighborsPreferencesRecordInstance = 0x2324f488,
  kApBudgetPreferencesRecordType = 0xa324f390,
  kApBudgetPreferencesRecordInstance = 0x324f492,
  kApAdvisorsPreferencesRecordType = 0xc324f393,
  kApAdvisorsPreferencesRecordInstance = 0xe324f49a,
  kApCatalogPreferencesRecordType = 0x633350da,
  kApCatalogPreferencesRecordInstance = 0x233350df,
  kApChartsPreferencesRecordType = 0x3357d61,
  kApChartsPreferencesRecordInstance = 0x43357d6c,
  kApGraphsPreferencesRecordType = 0xa334f3f4,
  kApGraphsPreferencesRecordInstance = 0xe334f3fe,
  kApReplacementPreferencesRecordType = 0x1a35002a,
  kApReplacementPreferencesRecordInstance = 0x5a35004a,
  kApEditBuildingsPreferencesRecordType = 0xba3512e9,
  kApEditBuildingsPreferencesRecordInstance = 0xba3512fd,
  kApCheatCodeDataRecordType = 0x230e2ee6,
  kApCheatCodeDataRecordGroup = 0x330e2ee6,
  kApCheatOccurrenceRecordType = 0x430e2ee6,
  kApCheatOccurrenceRecordGroup = 0x530e2ee6,
  kApActiveCheatCodesRecordType = 0x630e2ee6,
  kApActiveCheatCodesRecordGroup = 0x730e2ee6,
};

#if defined(_M_IX86)
static_assert(sizeof(ApDirector) == 68);
static_assert(sizeof(ApPlatformDirector) == 68);
#endif

// Actual registry specialization of the module-local service pointer, including constructor-failure ownership.
struct ScRegistryRef {
  // Acquire the real registry into the original null-initialized field. A throwing acquisition never completes this owner.
  ScRegistryRef() : object(nullptr) {
    ApGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&object));
  }

  // Release the acquired field after registration, including exception cleanup once construction has completed.
  ~ScRegistryRef() {
    if (object)
      object->Release();
  }

  ScRegistry *object;
};

static bool ApRegisterDirector(ScDllDirector *director) {
  ScRegistryRef registry;
  return director->AppRegister(registry.object);
}

// Borrow the live framework's COM owner when the common director has none, then attach the actual platform child.
// The child receives the existing COM and initially empty path through the original common AddDirector operation.
ApDirector::ApDirector() {
  if (!GZCOM())
    com = ApGetFramework()->GetCOMObject();
  AddDirector(ApGetPlatformDirector());
}

ApDirector::~ApDirector() {
}

u32 ApDirector::GetDirectorID() const {
  return kApDirectorId;
}

bool ApDirector::OnStart(ScCom *) {
  return ApRegisterDirector(this);
}

bool ApDirector::InitializeFrameWork() {
  framework = ApGetFramework();
  return framework != nullptr;
}

ApPlatformDirector::ApPlatformDirector() {
}

ApPlatformDirector::~ApPlatformDirector() {
}

u32 ApPlatformDirector::GetDirectorID() const {
  return kApPlatformDirectorId;
}

bool ApPlatformDirector::OnStart(ScCom *) {
  return ApRegisterDirector(this);
}

// Construct the platform child once and borrow its common director identity without acquiring a reference.
// Its construction during root initialization gives the original root-before-child destruction order.
ScDllDirector *ApGetPlatformDirector() {
  static ApPlatformDirector director;
  return &director;
}

// Construct the original executable director on first access and borrow its common director identity.
// The function-local owner retains the source's normal exit destruction and adds no object reference.
ScDllDirector *ApGetDirector() {
  static ApDirector director;
  return &director;
}

ApComDirector::~ApComDirector() {
}

u32 ApComDirector::GetDirectorID() const {
  return kApComDirectorId;
}

bool ApComDirector::OnStart(ScCom *) {
  return ApRegisterDirector(this);
}

// Construct the actual main window and publish its secondary main-window Unknown identity without retaining it.
ScUnknown *ApComDirector::CreateWinSC3() {
  return static_cast<UiMainWindow *>(new ApMainWindow);
}

// Construct the real system-information window and return its original primary Unknown identity.
ScUnknown *ApComDirector::CreateSysInfoWindow() {
  return new ApSysInfoWindow;
}

// Construct the original COM director on first access and borrow its identity without taking an extra reference.
ScDllDirector *ApGetCOMDirector() {
  static ApComDirector director;
  return &director;
}

// Construct the executable command factory and return its unretained primary Unknown identity.
ScUnknown *ApComDirector::CreateCommandFactory() {
  return new ApCommandFactory;
}

// Construct the executable animated-occupant manager and return its primary interface without retaining it.
ScUnknown *ApComDirector::CreateAnimationManager() {
  return static_cast<OcAnimManager *>(new OcAnimationManager);
}

// Construct the original sprite-filter command factory and return its unretained primary identity.
ScUnknown *ApComDirector::CreateSpriteFilterCommandFactory() {
  return new ApSpriteFilterCommandFactory;
}

// Construct the original intro window and return its secondary interface without adding a reference.
ScUnknown *ApComDirector::CreateDancingBear() {
  return static_cast<ApDancingBearView *>(new ApDancingBear);
}

// Register the executable's ten original COM class producers in their source order.
ApComDirector::ApComDirector() {
  AddClass(kApCommandFactoryClassId, CreateCommandFactory, 0);
  AddClass(kOcAnimManagerClassId, CreateAnimationManager, 0);
  AddClass(kApSpriteFilterCommandFactoryClassId, CreateSpriteFilterCommandFactory, 0);
  AddClass(kUiMainWindowClassId, CreateWinSC3, 0);
  AddClass(kApDancingBearClassId, CreateDancingBear, 0);
  AddClass(kWnAppScrollbarClassId, ApScrollBar::CreateNewObject, 0);
  AddClass(kApListViewControlClassId, ApListView::CreateNewObject, 0);
  AddClass(kApSysInfoWindowClassId, CreateSysInfoWindow, 0);
  AddClass(kApCheatDialogClassId, CreateCheatCodeEntryDialog, 0);
  AddClass(kApNewscastControlClassId, ApNewscast::CreateNewObject, 0);
}

// Construct the executable cheat-entry dialog and return its unretained primary window identity.
ScUnknown *ApComDirector::CreateCheatCodeEntryDialog() {
  return static_cast<WnWindow *>(new ApCheatDialog);
}

// Look for the current language's text resources on the configured CD data path.
// Repeat after the localized prompt unless its cancel result asks the framework to quit.
bool ApApplication::DoCDCheck() {
  RsFileSystem *files = ApGetFileSystem();
  for (;;) {
    bool found = false;
    {
      CtVersionInfo version;
      ScString first, second, path, language;
      ApGetLanguageManager()->GetLanguageDirectoryName(&language, 0);
      if (files->CDDataDirectory()) {
        path = ScString(files->CDDataDirectory());
        path.Append("Res\\Text\\");
        path.Append(language.Data(), language.Strlen());
        path.Append("\\*.*");
        found = RsDirectoryListing::DoesAnyEntryExistThatMatchesPattern(path);
      }
    }
    if (found)
      return true;
    ScString title = UiTextHandle(kRsMessageNoCDText, kRsMessageTextGroup);
    ScString text = UiTextHandle(kRsMessageInsertGameDiscText, kRsMessageTextGroup);
    if (ApGetDialogManager()->GZMsgBox(text, title, 2, true, 0) == kWnCancelButtonId) {
      ApGetFramework()->Quit(0);
      return false;
    }
  }
}

// Find the first command-line argument matching a localized city-file suffix and a recognized city file type.
// If a suffix matched but no usable city file was found, show the original localized error message.
void ApApplication::CheckCommandLineForInputCityFile() {
  input_filename.Erase(0, kScStringToEnd);
  CtDefinition *definition = nullptr;
  if (!ApGetCom()->GetClassObject(kCtDefinitionClassId, IID_CtDefinition, reinterpret_cast<void **>(&definition)) ||
      !definition)
    return;
  bool matched = false;
  {
    ScCommandLine arguments(*ApGetFramework()->CommandLine());
    for (i32 index = 0; index < arguments.argc() && input_filename.begin == input_filename.end; ++index) {
      const u32 prefixes[] = {142, 140, 144};
      for (u32 id : prefixes) {
        ScString prefix = UiTextHandle(id, kRsCommonTextGroup);
        ScString *argument = arguments.GetArgument(static_cast<u32>(index));
        ScString ending = argument->Right(prefix.Strlen());
        if (ScCompareNoCase(ending.ToChar(), prefix.ToChar(), prefix.Strlen()) == 0) {
          matched = true;
          u32 type = definition->GetFileType(*arguments.GetArgument(static_cast<u32>(index)));
          if (type && type <= 6) {
            input_filename = *arguments.GetArgument(static_cast<u32>(index));
            break;
          }
        }
      }
    }
    definition->Release();
  }
  if (matched && input_filename.begin == input_filename.end) {
    ScString title = UiTextHandle(kRsCommonSimCity3000Text, kRsCommonTextGroup);
    ScString text = UiTextHandle(kRsCommonTheFileSpecifiedOnTheCommandLineText, kRsCommonTextGroup);
    ApGetGraphics()->MessageBoxA(text, title);
  }
}

// Configure the platform process error mode before application initialization.
static void ApSetStartupErrorMode() {
#if defined(_WIN32)
  SetErrorMode(0x8003);
#endif
}

// Bring the original existing application window to the foreground when the platform exposes it.
static void ApRestoreRunningInstance() {
#if defined(_WIN32)
  HWND window = FindWindowExA(nullptr, nullptr, "Gonzo", "SimCity 3000");
  if (window) {
    SetForegroundWindow(window);
    ShowWindow(window, SW_RESTORE);
  }
#endif
}

// Establish executable paths and the single-instance mutex before choosing graphics and preferences.
bool ApApplication::PreFrameWorkInit() {
  CtVersionInfo version;
  ScString executable;
  ApSetStartupErrorMode();
  version.GetModuleActualFileName(&executable);
  ScPath path(executable);
  ScString directory(path.Directory()), drive(*path.Drive());
  drive.Append(directory.Data(), directory.Strlen());
  app_directory = drive;
  FindDataDirectory(&data_directory);
  ScString mutex_name("SimCity 3000 Mutex");
  app_mutex = new ScMutex(mutex_name);
  if (!app_mutex->TryLock()) {
    ScString restart("restart"), value;
    if (!ApGetFramework()->CommandLine()->IsSwitchPresent(restart, &value, true)) {
      ApRestoreRunningInstance();
      return false;
    }
    app_mutex->Lock();
  }
  GxMode mode = {};
  if (performance.processor_power < 4) {
    mode.width = 640;
    mode.height = 480;
  } else {
    mode.width = 800;
    mode.height = 600;
  }
  mode.depth = 16;
  ApGetGraphics()->PreInitSetDesiredGameResolution(mode);
  ScString cd_path("Apps\\SC3U.exe");
  ApGetFileSystem()->SetExpectedCDAppPath(cd_path);
  FindAndRegisterAllAvailableLanguages();
  ApPreferencesObject *new_preferences = new ApPreferencesObject;
  new_preferences->QueryInterface(IID_ApPreferences, reinterpret_cast<void **>(&preferences));
  ReadPreferences();
  ReadCommandLine();
  ScCmdLine *command_line = ApGetFramework()->CommandLine();
  AuSoundDevice *sound = AuGetAppSoundDevice();
  ScString option("sound"), value;
  if (command_line->IsSwitchPresent(option, &value, true) && value.BeginsWith("off", 2))
    sound->SetEnabledFlag(0);
  return true;
}

// Dispatch cheat input and save or restore the three city records owned by the application cheat manager.
// Initialize read scratch so that a short record never causes an undefined native read.
bool ApApplication::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    DoCheatCode(static_cast<u32>(message->data1), *reinterpret_cast<ScStringView *>(message->data2));
    return true;
  }
  if (message->type == kApMsgPreCityChange) {
    if (!cheats_disabled_on_startup) {
      cheats.SetCheatCodePassword(nullptr, 0);
      cheats.occurrences = 0;
    }
  } else if (message->type == kCtMsg_SaveToSegment) {
    auto *segment = reinterpret_cast<RsDatabaseSegment *>(message->data1);
    u8 password[4096] = {};
    u32 identifiers[1024] = {}, count = 0;
    cheats.GetCheatCodePassword(nullptr, &count);
    if (count && count <= 1024) {
      TrRecordFrame frame(city, segment, RsKey(kApCheatCodeDataRecordType, kApCheatCodeDataRecordGroup, 0), 1, false);
      if (frame.IsValid()) {
        cheats.GetCheatCodePassword(password, &count);
        RsStream *stream = frame.GetDBSerialRecordPointer();
        stream->WriteU32(count);
        stream->WriteBlock(password, count);
      }
    }
    if (cheats.GetCheatCodeOccurrenceCount()) {
      TrRecordFrame frame(
          city, segment, RsKey(kApCheatOccurrenceRecordType, kApCheatOccurrenceRecordGroup, 0), 1, false);
      if (frame.IsValid())
        frame.GetDBSerialRecordPointer()->WriteU32(cheats.GetCheatCodeOccurrenceCount());
    }
    count = 1024;
    if (cheats.GetSetCheatCodes(identifiers, &count)) {
      TrRecordFrame frame(
          city, segment, RsKey(kApActiveCheatCodesRecordType, kApActiveCheatCodesRecordGroup, 0), 1, false);
      if (frame.IsValid()) {
        RsStream *stream = frame.GetDBSerialRecordPointer();
        if (count > 1024)
          count = 1024;
        stream->WriteU32(count);
        stream->WriteU32Array(identifiers, count);
      }
    }
  } else if (message->type == kCtMsg_LoadFromSegment) {
    auto *segment = reinterpret_cast<RsDatabaseSegment *>(message->data1);
    u8 password[4096] = {};
    u32 identifiers[1024] = {}, count = 0;
    {
      TrRecordFrame frame(city, segment, RsKey(kApCheatCodeDataRecordType, kApCheatCodeDataRecordGroup, 0));
      if (frame.IsValid()) {
        RsStream *stream = frame.GetDBSerialRecordPointer();
        if (stream->ReadU32(&count) && count <= 1024 && stream->ReadBlock(password, count))
          cheats.SetCheatCodePassword(password, count);
      }
    }
    {
      TrRecordFrame frame(city, segment, RsKey(kApCheatOccurrenceRecordType, kApCheatOccurrenceRecordGroup, 0));
      if (frame.IsValid()) {
        count = 0;
        if (frame.GetDBSerialRecordPointer()->ReadU32(&count))
          cheats.occurrences = count;
      }
    }
    {
      TrRecordFrame frame(city, segment, RsKey(kApActiveCheatCodesRecordType, kApActiveCheatCodesRecordGroup, 0));
      count = 1024;
      if (frame.IsValid()) {
        RsStream *stream = frame.GetDBSerialRecordPointer();
        if (stream->ReadU32(&count)) {
          if (count > 1024)
            count = 1024;
          stream->ReadU32Array(identifiers, count);
          for (u32 index = 0; index < count; ++index)
            cheats.SetCheatHasBeenSet(identifiers[index], true);
        }
      }
    }
  } else {
    return true;
  }
  city_started = true;
  return true;
}

// Complete city startup by assigning its file path, rebuilding audio information and sending the original notifications.
// A file-backed city also initializes the current city view from the temporary indexed segment.
bool ApApplication::PostCityInit(i32 first, i32 second, ScStringView *name) {
  ScString path;
  if (name)
    path = ScString(*name);
  if (!city)
    return false;
  city->SetFilePath(path);
  ScRelease(&audio_info);
  if (ApGetCom()->GetClassObject(kAuInfoClassId, IID_AuInfo, reinterpret_cast<void **>(&audio_info)) && audio_info)
    audio_info->Init(city);
  ScMessageServer *messages = ApGetMessageServer();
  ScMessage ready(kApMsgCityReady);
  messages->MessageSend(ready);
  ApGameInterface *game = nullptr;
  if (ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&game))) {
    ApBoxX *box = game->GetBoxX();
    game->Release();
    box->Event(kAuEventCityReady, 0, 0, 0, 0);
  }
  ScMessage loaded(kApMsgPostCityChange);
  messages->MessageSend(loaded);
  if (second == 3) {
    RsIndexedFile segment(nullptr);
    segment.Init(0, &path, true);
    RsDatabaseSegment *identity = nullptr;
    if (segment.QueryInterface(IID_RsDatabaseSegment, reinterpret_cast<void **>(&identity))) {
      SpCityWindowView *view = ApGetCityWindow();
      if (view)
        view->Init(ApGetCity(), identity);
    }
  }
  ScMessage transition(first == 2 ? kWnMsg_EnterTerrainEditor : kWnMsg_EnterCity);
  messages->MessagePost(transition, false);
  if (GetBoxX()) {
    ApBoxX *box = GetBoxX();
    u32 last_z = city->CellCountZ() - 1;
    u32 last_x = city->CellCountX() - 1;
    box->Event(kAuEventAttachCityInfo, reinterpret_cast<uptr>(audio_info), last_x, last_z, 0);
    GetBoxX()->Event(1041, 0, 0, 0, 0);
  }
  return true;
}

void ApApplication::DoCheatCode(u32 code, const ScStringView &) {
  if (code == kScCheat_CheatsEnabled)
    cheats_disabled_on_startup = !city_started;
}

ApBaseApp::ApBaseApp(const char *name) : ScServiceBase(102, kApBaseServicePriority), module_name(name) {
}

ApBaseApp::ApBaseApp(u32 id, const char *name) : ScServiceBase(id, kApBaseServicePriority), module_name(name) {
}

ApBaseApp::~ApBaseApp() {
}

bool ApBaseApp::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ApApp, ApApp);
  return ScServiceBase::QueryInterface(iid, out);
}

u32 ApBaseApp::AddRef() {
  return ScServiceBase::AddRef();
}

u32 ApBaseApp::Release() {
  return RemoveRef();
}

bool ApBaseApp::Init() {
  return true;
}

bool ApBaseApp::Shutdown() {
  return true;
}

// Borrow the actual primary service identity from the secondary application view.
// The original conversion acquires no reference and performs no query.
ScSystemService *ApBaseApp::AsIGZSystemService() {
  return static_cast<ScSystemService *>(this);
}

void ApBaseApp::AddApplicationService(ScSystemService *service) {
  FrameWork()->AddSystemService(service);
}

// Return the borrowed module-name storage directly. Do not append a terminator or acquire ownership here.
const char *ApBaseApp::ModuleName() {
  return module_name.Data();
}

bool ApBaseApp::PreFrameWorkInit() {
  return true;
}

bool ApBaseApp::PostFrameWorkInit() {
  return true;
}

bool ApBaseApp::Run() {
  return false;
}

bool ApBaseApp::PreFrameWorkShutdown() {
  return true;
}

// Borrow the executable's current framework through the original forwarding entry.
// This operation acquires no framework reference and maintains no separate cache.
ScFramework *ApBaseApp::FrameWork() {
  return ApGetFramework();
}

void ApBaseApp::AddDynamicLibrariesHere() {
}

void ApBaseApp::AddCOMDirectorsHere() {
}

void ApBaseApp::AddApplicationServicesHere() {
}

// Construct the platform's existing default debug output stream.
static ScOutputStream *ApCreateDefaultDebugStream() {
  return static_cast<ScOutputStream *>(new ScConsoleDebugStream);
}

// Register the executable's filesystem, registry and audio services, then install debug output and the idle detector.
void ApApplication::AddApplicationServicesHere() {
  RsFileSystemObject *files = new RsFileSystemObject;
  ScSystemService *service = nullptr;
  files->QueryInterface(IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    ApGetFramework()->AddSystemService(service);
    service->Release();
  }
  ScRegistryService *registry = new ScRegistryService;
  registry->QueryInterface(IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    ApGetFramework()->AddSystemService(service);
    LoadRegistry();
    service->Release();
  }
  AddApplicationService(radio ? static_cast<ScSystemService *>(radio) : nullptr);
  AddApplicationService(static_cast<ScSystemService *>(&sound_renderer));
  ScFrameworkRef<ScOutputStream> output(nullptr);
  if (!ApGetCom()->GetClassObject(
          kScOutputStreamClassId, IID_ScOutputStream, reinterpret_cast<void **>(&output.object))) {
    ScFrameworkRef<ScOutputStream> candidate(ApCreateDefaultDebugStream());
    candidate.object->AddRef();
    if (output.object)
      output.object->Release();
    output.object = candidate.object;
  }
  ApGetFramework()->SetStream(ScFramework::kStreamStdOut, output.object);
  ApGetFramework()->SetStream(ScFramework::kStreamStdErr, output.object);
  ScFrameworkRef<ScDebugStream> debug(nullptr);
  output.object->QueryInterface(IID_ScDebugStream, reinterpret_cast<void **>(&debug.object));
  ApGetFramework()->SetDebugStream(debug.object);
  ScFrameworkRef<ScSystemService> debug_service(nullptr);
  output.object->QueryInterface(IID_ScSystemService, reinterpret_cast<void **>(&debug_service.object));
  idle_detector = ScAddRefNotNull(new ApIdleDetector);
  ApGetFramework()->AddToOnIdle(idle_detector);
}

bool ApBaseApp::LoadRegistry() {
  return true;
}

static void ApInstallProcessExceptionHandler() {
#if defined(_WIN32)
  ApInitializeExceptionHandler();
#endif
}

static void ApRestoreProcessExceptionHandler() {
#if defined(_WIN32)
  ApShutdownExceptionHandler();
#endif
}

ApApplication::ApApplication(const char *name)
    : ApBaseApp(name), cheats_disabled_on_startup(false), city_started(false), restart_on_exit(false) {
  instance = this;
  ApInstallProcessExceptionHandler();
  radio = ScAddRefNotNull(new AuRadioPlayer);
}

// Identify the original executable's last-component ICD extension without rejecting long path components.
[[maybe_unused]] static bool ApRestartHasIcdExtension(const char *path) {
  const char *base = path, *extension = nullptr;
  for (const char *byte = path; *byte; ++byte) {
    if (*byte == '/' || *byte == '\\')
      base = byte + 1;
    else if (*byte == '.')
      extension = byte;
  }
  return extension && extension >= base && !ScCompareNoCase(extension, ".icd");
}

// Restart through the original process executable, rewriting an ICD extension and copying the actual argument pointers.
static void ApSpawnRestart(ScCommandLine *command_line) {
#if defined(_WIN32)
  char executable[MAX_PATH + 1];
  DWORD length = GetModuleFileNameA(GetModuleHandleA(nullptr), executable, MAX_PATH);
  executable[length] = '\0';
  if (ApRestartHasIcdExtension(executable)) {
    length -= 3;
    memcpy(executable + length, "exe", 4);
  }
#endif
  const char *arguments[32];
  u32 count = static_cast<u32>(command_line->argc());
  assert(count < 32);
  for (u32 i = 0; i < count; ++i)
    arguments[i] = command_line->GetArgument(i)->begin;
  arguments[count] = nullptr;
#if defined(_WIN32)
  _spawnv(_P_NOWAIT, executable, arguments);
#else
  ScVersion version;
  ScString executable;
  version.GetModuleActualFileName(&executable);
  pid_t child;
#if defined(__APPLE__)
  char **environment = *_NSGetEnviron();
#else
  char **environment = environ;
#endif
  posix_spawn(&child, executable.Data(), nullptr, nullptr, const_cast<char *const *>(arguments), environment);
#endif
}

// Release preferences, unlock/delete both concrete mutexes and release the retained radio before an optional restart.
// Restore the process exception filter before ordinary reverse destruction of all owned application subobjects.
ApApplication::~ApApplication() {
  ScRelease(&preferences);
  if (app_mutex) {
    app_mutex->Unlock();
    if (app_mutex)
      delete app_mutex;
    app_mutex = nullptr;
  }
  if (update_mutex) {
    update_mutex->Unlock();
    if (update_mutex)
      delete update_mutex;
    update_mutex = nullptr;
  }
  ScRelease(&radio);
  if (restart_on_exit)
    ApSpawnRestart(&restart_command_line);
  ApRestoreProcessExceptionHandler();
}

// Shut down the local transfer owner, application services and windows in callback order.
bool ApApplication::Shutdown() {
  if (scheme_manager) {
    scheme_manager->Shutdown();
    scheme_manager->Release();
    scheme_manager = nullptr;
  }
  if (internet) {
    internet->Shutdown();
    internet->Release();
    internet = nullptr;
  }
  if (resource_manager)
    resource_manager->SetGarbageCollectionActive(false);
  if (city)
    city->EndCitySimulation();
  if (box_x)
    box_x->Shutdown();
  WindowShutdown();
  if (tooltip_manager) {
    tooltip_manager->Shutdown();
    tooltip_manager->Release();
    tooltip_manager = nullptr;
  }
  ShutdownUiTheme();
  ShutdownCity();
  if (advisor_dialog_manager) {
    advisor_dialog_manager->Shutdown();
    advisor_dialog_manager->Release();
    advisor_dialog_manager = nullptr;
  }
  if (city_definition) {
    city_definition->Shutdown();
    city_definition->Release();
    city_definition = nullptr;
  }
  RsManager *manager = GetResourceManager();
  if (!manager)
    return false;
  manager->ForceGarbageCollection();
  ApServices::IM()->RealShutdown();
  manager->ForceGarbageCollection();
  ScRelease(&box_x);
  ScRelease(&buffer_factory);
  ScRelease(&resource_manager);
  return ApBaseApp::Shutdown();
}

void ApApplication::AddSpriteSegmentsToCSIM(const ScStringView &key, const ScStringView &value, void *) {
  SpImageCache *images = ApServices::IM();
  ScString expected("DefaultEnabled"), name(key), flag(value);
  images->AddDBSegmentSpriteToSearchList(name, flag.IsEqual(&expected));
}

void ApApplication::AddSpriteImageInfoToCSIM(const ScStringView &key, const ScStringView &value, void *) {
  SpImageCache *images = ApServices::IM();
  ScString expected("DefaultEnabled"), name(key), flag(value);
  images->AddSpriteInfoFileToSearchList(name, flag.IsEqual(&expected));
}

bool ApApplication::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ApGameInterface, ApGameInterface);
  return ApBaseApp::QueryInterface(iid, out);
}

u32 ApApplication::AddRef() {
  return 1;
}

u32 ApApplication::Release() {
  return 1;
}

// Borrow the live BoxX member through the real game-application method.
// The original secondary receiver is root+48 and reads the member at complete offset228.
ApBoxX *ApApplication::GetBoxX() {
  return box_x;
}

// Acquire the root and canvas, assign their independently loaded titles, then create and attach the real main window.
// Keep the original live member reloads, failure ownership and notification-target order without extra queries.
bool ApApplication::WindowSetup() {
  root_window = ApGetDialogManager()->GetMainWindow();
  if (!root_window)
    return false;
  GxCanvas *canvas = ApGetDialogManager()->GetCanvas();
  {
    UiTextHandle title(kRsCommonSimCity3000Text, kRsCommonTextGroup);
    ScStringView *text = title.GetText();
    ScString source = text ? ScString(*text) : ScString();
    ScString first(source), second(first);
    root_window->SetCaption(second);
  }
  {
    UiTextHandle title(kRsCommonSimCity3000Text, kRsCommonTextGroup);
    ScStringView *text = title.GetText();
    ScString source = text ? ScString(*text) : ScString();
    ScString first(source), second(first);
    canvas->SetCaption(second);
  }
  canvas->ShowWindow();
  if (!ApGetCom()->GetClassObject(kUiMainWindowClassId, IID_UiMainWindow, reinterpret_cast<void **>(&main_window)))
    return false;
  main_window->PreInit(static_cast<ApGameInterface *>(this));
  WnWindow *window = main_window->AsIGZWin();
  if (!root_window->ChildAdd(window)) {
    main_window->Release();
    main_window = nullptr;
    return false;
  }
  window->SetNotificationTarget(nullptr);
  root_window->SetNotificationTarget(window);
  root_window->ShowWindow();
  return true;
}

// Release the current controller before clearing it, then hide the live root and remove its children.
// Reload the root after the hide callback, preserve ordinary ignored replies and clear the member last.
bool ApApplication::WindowShutdown() {
  ScRelease(&main_window);
  if (root_window) {
    root_window->HideWindow();
    root_window->ChildDeleteAll();
    root_window = nullptr;
  }
  return true;
}

// Borrow the current city directly from the application. No retain or fallback service lookup occurs.
CtCity *ApApplication::GetCity() {
  return city;
}

// Prefer the updater settings beside the application, then honor the original UpdaterApp command-line override.
// An override returns its sibling settings path without a second existence check.
bool ApApplication::GetPathToUpdaterIniFile(ScString *out) {
  *out = app_directory;
  out->Append("Updater");
  out->Append("\\");
  out->Append("UpdateSettings.ini");
  if (RsFile::FileExists(*out))
    return true;
  ScCommandLine arguments(*ApGetFramework()->CommandLine());
  ScString value;
  bool found;
  {
    ScString option("UpdaterApp");
    found = arguments.IsSwitchPresent(option, &value, true);
  }
  if (found) {
    RsFile::GetPathDirectory(value, out);
    out->Append("UpdateSettings.ini");
  }
  return found;
}

// Resolve Updater.exe beside the application or beside the original command-line override executable.
bool ApApplication::GetPathToUpdaterExecutable(ScString *out) {
  *out = app_directory;
  out->Append("Updater");
  out->Append("\\");
  out->Append("Updater.exe");
  if (RsFile::FileExists(*out))
    return true;
  ScCommandLine arguments(*ApGetFramework()->CommandLine());
  ScString value;
  bool found;
  {
    ScString option("UpdaterApp");
    found = arguments.IsSwitchPresent(option, &value, true);
  }
  if (found) {
    RsFile::GetPathDirectory(value, out);
    out->Append("Updater.exe");
  }
  return found;
}

bool ApApplication::LaunchManualUpdaterExecutable() {
  return false;
}

// Read the configured update name, then its signed numeric check result from the original settings file.
// Missing paths, entries or decimal values return false through the actual INI contracts.
bool ApApplication::HasUpdateBeenFound() {
  ScString path;
  ApIniFile settings;
  if (!GetPathToUpdaterIniFile(&path))
    return false;
  i32 value = 0;
  ScString update_name;
  settings.SetPath(path);
  bool found;
  {
    ScString key("UpdateName"), section("Settings");
    found = settings.ReadEntry(section, key, &update_name);
  }
  if (found) {
    ScString section("Check Results");
    found = settings.ReadNumber(section, update_name, &value);
    if (found && value)
      return true;
  }
  return false;
}

// Assign a resource text through the two original concrete String copies before retiring its scoped resource handle.
// This native extraction shares only the repeated assignment sequence, with no replacement game service or callback.
static void ApAssignExitText(ScString *target, u32 group, u32 instance) {
  UiTextHandle resource(instance, group);
  ScStringView *text = resource.GetText();
  ScString source = text ? ScString(*text) : ScString();
  ScString copy(source);
  *target = copy;
}

// Pause the live city, perform the exit/save dialogs and execute the genuine save command when requested.
// Quit only after acceptance, then unpause the currently installed city even if callbacks replaced the original one.
bool ApApplication::Quit(bool ask_exit, bool ask_save) {
  bool allowed = true, result = false;
  WnWindowManager *dialogs = ApGetDialogManager();
  ApBoxX *commands = ApServices::BoxX();
  if (city)
    city->SimHiddenPause();
  if (!city)
    ask_save = false;
  if (ask_exit) {
    ScString title, message;
    if (dialogs) {
      ApAssignExitText(&title, kRsGuiTextGroup, 85);
      ApAssignExitText(&message, kRsCommonTextGroup, 163);
      allowed = dialogs->GZMsgBox(message, title, 0x10001, true, 0) == kWnOkButtonId;
      if (commands)
        commands->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    }
    if (allowed && ask_save && dialogs) {
      ApAssignExitText(&title, kRsGuiTextGroup, 85);
      ApAssignExitText(&message, kRsCommonTextGroup, 295);
      u32 reply = dialogs->GZMsgBox(message, title, 0x10004, true, 0);
      if (commands)
        commands->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      if (reply == kWnNoButtonId)
        ask_save = false;
      else if (reply == kWnYesButtonId)
        ask_save = true;
      else
        allowed = false;
    }
  }
  if (allowed && ask_save) {
    ScCommandFactory *factory = ApServices::GCF();
    UiCommandInterface *command = nullptr;
    if (factory &&
        factory->GetCommand(kCtSaveCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      ScString filename(*city->GetFilePath());
      ScValue *input = new ScValue;
      input->AddRef();
      input->RefUint8(reinterpret_cast<const u8 *>(filename.Data()), filename.Strlen());
      command->PushBackInput(input);
      input->Release();
      bool saved = command->Execute();
      if (saved) {
        if (!command->OutputQueueSize())
          saved = false;
        if (saved) {
          ScVariant *value;
          command->PopFrontOutput(&value);
          if (!*value->AsBool())
            saved = false;
          value->Release();
        }
      }
      command->Release();
      if (!saved)
        allowed = false;
    }
  }
  if (allowed) {
    ApGetFramework()->Quit(0);
    result = true;
  }
  if (city)
    city->SimHiddenUnpause();
  return result;
}

void ApApplication::SetInputFileName(const ScStringView &name) {
  ScString copy(name);
  input_filename = copy;
}

// Borrow the stored input filename through its const interface without acquiring a reference.
const ScStringView *ApApplication::GetInputFileName() {
  return &input_filename;
}

// Publish the restart flag first, then copy the framework command line and remove every resolution switch.
// Append the original restart argument immediately before the final argument; clearing the flag retains prior arguments.
void ApApplication::SetRestartOnExit(bool restart) {
  restart_on_exit = restart;
  if (!restart)
    return;
  {
    ScCommandLine copy(*ApGetFramework()->CommandLine());
    restart_command_line = copy;
  }
  for (;;) {
    i32 index = restart_command_line.GetIndexOfSwitch('r', 0);
    if (index < 0)
      break;
    restart_command_line.EraseArgument(index);
  }
  ScString argument(" -restart:on");
  restart_command_line.InsertArgument(argument, restart_command_line.argc() - 1);
}

// Borrow the application's actual embedded cheat manager rather than acquiring a replacement service.
ScCheatCodeManager *ApApplication::GetCheatCodeManager() {
  return &cheats;
}

// Return the radio reference retained by application construction without acquiring another reference.
AuRadio *ApApplication::GetRadio() {
  return radio;
}

// Borrow the actual embedded render service's primary sound-render interface.
AuRenderServer *ApApplication::GetSoundRenderServer() {
  return &sound_renderer;
}

// Borrow the real embedded application-state controller through its game interface.
ApStateManager *ApApplication::GetAppStateManager() {
  return &state_manager;
}

// Ask the stored city definition for its borrowed agent tree through the original virtual method.
CtAgentTree *ApApplication::GetAppAgentTypeTree() {
  return city_definition->GetAgentTypeTree();
}

// Ask the same city definition for its borrowed occupant attribute cache without retaining it.
OcAttribCache *ApApplication::GetAppOccupantAttribCache() {
  return city_definition->GetOccupantAttribCache();
}

// Return the application's stored city-definition interface directly.
CtDefinition *ApApplication::GetAppCityDefinition() {
  return city_definition;
}

// Build the INI filename from the executable filesystem's application name and the literal extension.
// Copy through the caller's null-terminated String operation after the owned temporary is complete.
void ApApplication::GetAppIniFileName(ScStringView *out) {
  ScString filename(ApGetFileSystem()->AppName());
  filename.Append(".ini");
  out->FromChar(filename.Data());
}

void ApApplication::GetAppDataDirectory(ScStringView *out) {
  out->FromChar(data_directory.begin, static_cast<u32>(data_directory.end - data_directory.begin));
}

void ApApplication::GetAppDirectory(ScStringView *out) {
  out->FromChar(app_directory.begin, static_cast<u32>(app_directory.end - app_directory.begin));
}

// Borrow the application's registered image-buffer resource factory without acquiring ownership.
RsImageOptions *ApApplication::GetBufferResourceFactory() {
  return buffer_factory;
}

// Borrow the preference interface stored during application initialization.
ApPreferences *ApApplication::GetAppPreferences() {
  return preferences;
}

// Borrow the actual six-byte performance record constructed with the application's other owned members.
ApPerformanceInfo *ApApplication::GetSystemPerformanceInfo() {
  return &performance;
}

// Borrow the stored main-window controller without querying or retaining a new window identity.
UiMainWindow *ApApplication::GetSC3MainWindow() {
  return main_window;
}

// Return the current borrowed city-window interface directly.
SpCityWindowView *ApApplication::GetISC3WinCityView() {
  return city_window;
}

void ApApplication::SetWinCityView(SpCityWindowView *window) {
  city_window = window;
}

// Borrow the application's stored Internet interface without changing its lifetime.
NwInternet *ApApplication::GetSC3Internet() {
  return internet;
}

// Borrow the city scheme manager stored during application initialization.
CtSchemeManager *ApApplication::CitySchemeManager() {
  return scheme_manager;
}

// Acquire the actual command factory and send progress operation zero with its reserved zero, total and text arguments.
// Execute only after lookup succeeds, then retire command and factory references in their original order.
void ApApplication::ShowProgressBar(u32 total, ScStringView *text) {
  ScCommandFactory *factory = nullptr;
  ApGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  UiCommandInterface *command = nullptr;
  if (factory->GetCommand(kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(0u, kScValueUint32));
    command->PushBackInput(new ScValue(0u, kScValueUint32));
    command->PushBackInput(new ScValue(total, kScValueUint32));
    command->PushBackInput(new ScValue(*text));
    command->Execute();
    command->Release();
  }
  if (factory)
    factory->Release();
}

// Send progress operation one followed by the new position through a freshly acquired command factory.
void ApApplication::UpdateProgressBar(u32 position) {
  ScCommandFactory *factory = nullptr;
  ApGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  UiCommandInterface *command = nullptr;
  if (factory->GetCommand(kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(1u, kScValueUint32));
    command->PushBackInput(new ScValue(position, kScValueUint32));
    command->Execute();
    command->Release();
  }
  if (factory)
    factory->Release();
}

// Send progress operation two with no position or text arguments, then release the acquired command and factory.
void ApApplication::HideProgressBar() {
  ScCommandFactory *factory = nullptr;
  ApGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  UiCommandInterface *command = nullptr;
  if (factory->GetCommand(kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(2u, kScValueUint32));
    command->Execute();
    command->Release();
  }
  if (factory)
    factory->Release();
}

void ApApplication::DisplayStartupInformation() {
}

// Actual cRZAutoRef<registry> ownership acquires one typed service reference for the callback's scope.
// This small concrete scope preserves original exception cleanup without recreating generic smart-pointer machinery.
class ApRegistryReference {
public:
  // Acquire the same registry SID/IID directly from the executable framework, initializing its actual output to null.
  ApRegistryReference() : value(nullptr) {
    ApGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&value));
  }

  // Drop the acquired registry reference after later local Strings have been destroyed.
  ~ApRegistryReference() {
    if (value)
      value->Release();
  }

  ScRegistry *value;
};

// Actual cRZAutoRef<command factory> ownership is separate from the borrowed SL command-factory cache.
class ApCommandFactoryReference {
public:
  // Acquire a scoped command factory through the original service query and null-initialized output.
  ApCommandFactoryReference() : value(nullptr) {
    ApGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&value));
  }

  // Release the service reference when the original scope ends, including an exception from a command callback.
  ~ApCommandFactoryReference() {
    if (value)
      value->Release();
  }

  ScCommandFactory *value;
};

// Acquire local startup services and initialize the theme, tooltip, local transfer owner, window and sound setup.
bool ApApplication::Init() {
  ScFramework *framework = ApGetFramework();
  ApRegistryReference registry;
  ScCommandLine command_line(*framework->CommandLine());
  resource_manager = nullptr;
  framework->GetSystemService(kRsManagerServiceId, IID_RsManager, reinterpret_cast<void **>(&resource_manager));
  ApGetCom()->GetClassObject(kRsImageFactoryClassId, IID_RsImageOptions, reinterpret_cast<void **>(&buffer_factory));
  CreateRegisteredObjects();
  state_manager.Init(ApGetMessageServer());
  app_directory = ScString(ApGetFileSystem()->AppDirectory());
  data_directory = ScString(ApGetFileSystem()->DataDirectory());
  InitUiTheme();
  if (!tooltip_manager) {
    WnResources *source = ApGetUiResources(), *destination = ApGetUiResources();
    destination->SetFont(static_cast<i32>(kWnFontTooltip), source->GetFont(kWnFontDefault));
    tooltip_manager = ScAddRefNotNull(new UiToolTipController);
    tooltip_manager->Init(ApGetUiResources()->GetFont(static_cast<i32>(kWnFontTooltip)), 3, 1);
  }
  if (!internet) {
    internet = ScAddRefNotNull(new NwInternetObject);
    internet->Init();
    internet->ReadPreferences();
  }
  bool ready = WindowSetup() && DoCDCheck();
  if (ready) {
    if (ApGetCom()->GetClassObject(kApBoxXClassId, IID_ApBoxX, reinterpret_cast<void **>(&box_x))) {
      ScString value, sound("sound");
      if (!command_line.IsSwitchPresent(sound, &value, true) || !value.BeginsWith("off", 2)) {
        ScString directory(ApGetFileSystem()->DataDirectory());
        box_x->Init(&directory);
        ScString music("music");
        if (command_line.IsSwitchPresent(music, &value, true) && value.BeginsWith("off", 2))
          box_x->Event(kAuEventSetMusicEnabled, 0, 0, 0, 0);
      }
    }
    CheckCommandLineForInputCityFile();
    if (input_filename.begin == input_filename.end) {
      ScString intro("intro"), value;
      if (!ApGetFramework()->CommandLine()->IsSwitchPresent(intro, &value, true) || !value.BeginsWith("off", 2))
        main_window->DoDancingBear();
    }
  }
  assert(main_window);
  main_window->DoCopyRightScreen(true);
  ScAutoCursor loading_cursor(14);
  if (ready) {
    SetupRadio();
    registry.value->EnumCategory("sc3dbssprite", AddSpriteSegmentsToCSIM, this);
    registry.value->EnumCategory("sc3imageinfo", AddSpriteImageInfoToCSIM, this);
    if (ApGetCom()->GetClassObject(
            kCtSchemeManagerClassId, IID_CtSchemeManager, reinterpret_cast<void **>(&scheme_manager)) &&
        scheme_manager)
      scheme_manager->Init();
    if (ApGetCom()->GetClassObject(
            kCtDefinitionClassId, IID_CtDefinition, reinterpret_cast<void **>(&city_definition)) &&
        city_definition)
      city_definition->Init();
    if (scheme_manager)
      scheme_manager->SetDefaultCityScheme();
    if (ApGetCom()->GetClassObject(
            kUiAdvisorManagerClassId, IID_UiAdvisorManager, reinterpret_cast<void **>(&advisor_dialog_manager)) &&
        advisor_dialog_manager)
      advisor_dialog_manager->Init();
    GxGraphics *graphics = ApGetGraphics();
    GxBlendService *alpha = ApServices::AB();
    GxColorType color = graphics->GetScreenColorType();
    alpha->InitAlphaSystem(color);
    graphics = ApGetGraphics();
    GxSpanBlt *span = ApServices::SB();
    color = graphics->GetScreenColorType();
    span->InitSpanBufferAlphaSystem(color);
  }
  if (resource_manager)
    resource_manager->SetGarbageCollectionActive(true);
  initialized = ready;
  return ready;
}

void ApApplication::CreateResourceFactoryCallback(const ScStringView &key, const ScStringView &value, void *) {
  key.ToChar();
  value.ToChar();
  u32 type = ScConvertToUint32(key.ToChar()), class_id = ScConvertToUint32(value.ToChar());
  instance->GetResourceManager()->RegisterObjectFactory(class_id, type);
}

void ApApplication::CreateDBSegmentCallback(const ScStringView &key, const ScStringView &value, void *) {
  key.ToChar();
  value.ToChar();
  u32 identifier = ScConvertToUint32(key.ToChar());
  ScString path(value), class_text = path.SplitToken(";");
  class_text.Append("", 1);
  u32 class_id = ScConvertToUint32(class_text.begin);
  RsDatabaseSegment *segment;
  if (ApGetCom()->GetClassObject(class_id, IID_RsDatabaseSegment, reinterpret_cast<void **>(&segment))) {
    if (segment->Init(identifier, &path, false)) {
      segment->OpenDBSegment(true, false);
      instance->GetResourceManager()->RegisterDBSegment(segment);
    }
    segment->Release();
  }
}

// Parse the original key String after its discarded getter call, construct the service and register it with the framework.
// Registration's result is ignored before releasing the acquired service; neither value nor context is consumed.
void ApApplication::CreateServiceCallback(const ScStringView &key, const ScStringView &, void *) {
  key.ToChar();
  u32 class_id = ScConvertToUint32(key.ToChar());
  ScSystemService *service;
  if (ApGetCom()->GetClassObject(class_id, IID_ScSystemService, reinterpret_cast<void **>(&service))) {
    ApGetFramework()->AddSystemService(service);
    service->Release();
  }
}

// Construct the command factory named by the original key and append it through a freshly acquired factory service.
// Release the new factory before retiring the scoped service reference.
void ApApplication::CreateCommandFactoryCallback(const ScStringView &key, const ScStringView &, void *) {
  key.ToChar();
  u32 class_id = ScConvertToUint32(key.ToChar());
  ScCommandFactory *factory = nullptr;
  if (ApGetCom()->GetClassObject(class_id, IID_ScCommandFactory, reinterpret_cast<void **>(&factory))) {
    ApCommandFactoryReference commands;
    commands.value->AddCommandFactory(factory);
    factory->Release();
  }
}

// Preserve the discarded concrete String getters, then format and register the original cores entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterResourceFactoryCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("cores", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original dbsegment entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterDBSegmentCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("dbsegment", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original dbdfseg entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterDBSegmentDirectoryFilesCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("dbdfseg", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original sysser entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterServiceCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("sysser", key, value, &name))
    registry.value->Register(name);
}

// Register the original command-factory entry after the two discarded String getter calls.
// The formatted name is destroyed before the scoped registry reference is released.
void ApApplication::RegisterCommandFactoryCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("cogamecmd", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original sc3occattrib entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterOccupantCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("sc3occattrib", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original sc3agenttype entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterAgentTypeCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("sc3agenttype", key, value, &name))
    registry.value->Register(name);
}

// Preserve the discarded concrete String getters, then format and register the original sc3typehierarchy entry.
// The local registry name is destroyed before the acquired registry service reference is released.
void ApApplication::RegisterAgentRelationCallback(const ScString &key, const ScString &value, ApIniContext *) {
  key.ToChar();
  value.ToChar();
  ApRegistryReference registry;
  ScString name;
  if (registry.value->MakeRegistryName("sc3typehierarchy", key, value, &name))
    registry.value->Register(name);
}

// Discover plugin database files under the specified plugin subdirectory and register each as addon-enabled.
// Stop the enumeration at the first failed registry name or registration operation.
void ApApplication::RegisterSpriteSegments(const ScString &key, const ScString &, ApIniContext *) {
  ApRegistryReference registry;
  ScString path(ApGetFileSystem()->PlugInDirectory());
  path.Append("PlugIn\\");
  path.Append(key.Data(), key.Strlen());
  ScString filename, value, name;
  ScDirectory directory(path, false, false, false);
  std::list<ScString> files;
  directory.SetNewDirectoryEntryFilter(ScString("*.DAT"), true);
  directory.ReadDirectoryFileEntriesIntoStringList(&files);
  for (const ScString &file : files) {
    filename = directory.GetPath();
    filename.Append(file.Data(), file.Strlen());
    value = ScString("addon-enabled");
    name = ScString("");
    if (!registry.value->MakeRegistryName("sc3dbssprite", filename, value, &name) || !registry.value->Register(name))
      break;
  }
}

// Discover plugin image-info files under the specified plugin subdirectory and register each as addon-enabled.
// Stop the enumeration at the first failed registry name or registration operation.
void ApApplication::RegisterImageInfo(const ScString &key, const ScString &, ApIniContext *) {
  ApRegistryReference registry;
  ScString path(ApGetFileSystem()->PlugInDirectory());
  path.Append("PlugIn\\");
  path.Append(key.Data(), key.Strlen());
  ScString filename, value, name;
  ScDirectory directory(path, false, false, false);
  std::list<ScString> files;
  directory.SetNewDirectoryEntryFilter(ScString("*.SII"), true);
  directory.ReadDirectoryFileEntriesIntoStringList(&files);
  for (const ScString &file : files) {
    filename = directory.GetPath();
    filename.Append(file.Data(), file.Strlen());
    value = ScString("addon-enabled");
    name = ScString("");
    if (!registry.value->MakeRegistryName("sc3imageinfo", filename, value, &name) || !registry.value->Register(name))
      break;
  }
}

static void ApRegisterGenericEntry(const ScString &key, const ScString &value, ApIniContext *context) {
  ApRegistryReference registry;
  if (registry.value) {
    ScString name;
    if (registry.value->MakeRegistryName(reinterpret_cast<const char *>(context), key, value, &name))
      registry.value->Register(name);
  }
}

// Load the original packaged or loose registry sections, then register discovered sprite resources and menus.
bool ApApplication::LoadRegistry() {
  ApRegistryReference registry;
  ScString data(ApGetFileSystem()->DataDirectory());
  ScString default_path(data), package_path(data), sprite_directory(data);
  default_path.Append("Sys\\Default.ini");
  package_path.Append("Sys\\SYS.PAK");
  sprite_directory.Append("Res\\Sprites");
  ApCompressedIni ini;
  ScDirectory sprites(sprite_directory, false, false, false);
  std::list<ScString> images, databases;
  ScString key, value, name;
  ini.SetPath(default_path);
  ini.SetCompressedPath(package_path);
  ApIniContext *context = reinterpret_cast<ApIniContext *>(this);
  ini.EnumEntries(ScString("ResourceFactories"), RegisterResourceFactoryCallback, context);
  ini.EnumEntries(ScString("DBSegments"), RegisterDBSegmentCallback, context);
  ini.EnumEntries(ScString("DBSegmentDirectoryFiles"), RegisterDBSegmentDirectoryFilesCallback, context);
  ini.EnumEntries(ScString("AppServices"), RegisterServiceCallback, context);
  ini.EnumEntries(ScString("GameCommandFactories"), RegisterCommandFactoryCallback, context);
  static const char *const bitmaps[] = {"SPLASHBMP", "WINSC3BMP", "MAINMENUBMP"};
  for (const char *category : bitmaps)
    ini.EnumEntries(ScString(category), ApRegisterGenericEntry,
        reinterpret_cast<ApIniContext *>(const_cast<char *>(category)));
  ScString city_path(data);
  city_path.Append("Sys\\CitySim.ini");
  ini.SetPath(city_path);
  ini.SetCompressedPath(package_path);
  ini.EnumEntries(ScString("AgentTypes"), RegisterAgentTypeCallback, context);
  ini.EnumEntries(ScString("AgentTypeHierarchy"), RegisterAgentRelationCallback, context);
  sprites.SetNewDirectoryEntryFilter(ScString("*.SII"), true);
  sprites.ReadDirectoryFileEntriesIntoStringList(&images);
  for (const ScString &file : images) {
    key = sprites.GetPath();
    key.Append(file.Data(), file.Strlen());
    value = ScString("default-enabled");
    name = ScString("");
    if (!registry.value->MakeRegistryName("sc3imageinfo", key, value, &name) || !registry.value->Register(name))
      return false;
  }
  sprites.SetNewDirectoryEntryFilter(ScString("*.DAT"), true);
  sprites.ReadDirectoryFileEntriesIntoStringList(&databases);
  for (const ScString &file : databases) {
    key = sprites.GetPath();
    key.Append(file.Data(), file.Strlen());
    value = ScString("default-enabled");
    name = ScString("");
    if (!registry.value->MakeRegistryName("sc3dbssprite", key, value, &name) || !registry.value->Register(name))
      return false;
  }
  ScString menu_path(data);
  menu_path.Append("Sys\\MenuItem.ini");
  ini.SetPath(menu_path);
  ini.SetCompressedPath(package_path);
  struct ApMenuSection { const char *section, *category; };
  static const ApMenuSection menu_sections[] = {{"SC3MenuItemInfo", "SC3MII"}, {"SC3MenuSets", "SC3MSET"},
      {"SC3MenuDescs", "SC3MDESC"}, {"SC3MenuBtnDefs", "SC3MBTNDEFS"}};
  for (const ApMenuSection &item : menu_sections)
    ini.EnumEntries(ScString(item.section), ApRegisterGenericEntry,
        reinterpret_cast<ApIniContext *>(const_cast<char *>(item.category)));
  return true;
}

// Create core resource factories, database segments and services in the original short-circuit category order.
void ApApplication::CreateRegisteredObjects() {
  ApRegistryReference registry;
  if (registry.value->EnumCategory("cores", CreateResourceFactoryCallback, this) &&
      registry.value->EnumCategory("dbsegment", CreateDBSegmentCallback, this) &&
      registry.value->EnumCategory("sysser", CreateServiceCallback, this))
    registry.value->EnumCategory("cogamecmd", CreateCommandFactoryCallback, this);
}

// Read the original Directories/Data value from the application INI under the stored executable directory.
// Preserve the caller's nonempty value on lookup failure; only an empty resulting String falls back to the executable path.
void ApApplication::FindDataDirectory(ScString *out) {
  ScString ini_name;
  GetAppIniFileName(&ini_name);
  ScString path(app_directory);
  path.Append(ini_name.Data(), ini_name.Strlen());
  ApIniFile ini(path);
  ScString key("Data"), section("Directories");
  ini.ReadEntry(section, key, out);
  if (out->begin == out->end)
    *out = app_directory;
}

// Subscribe the real application message identity before binding its wildcard cheat notifications and command words.
// The crash cheat is registered last under its original identifier and spelling.
void ApApplication::RegisterCheatCodeStrings() {
  ScMessageTarget *target = static_cast<ScMessageTarget *>(this);
  ApGetMessageServer()->AddNotification(target, kApMsgPreCityChange);
  ApGetMessageServer()->AddNotification(target, kCtMsg_SaveToSegment);
  ApGetMessageServer()->AddNotification(target, kCtMsg_LoadFromSegment);
  cheats.AddNotification(target, 0);
  {
    ScString disable("Disable"), enable("Enable");
    cheats.SetEnableDisableStrings(enable, disable);
  }
  ScString crash("crash");
  cheats.RegisterCheatCode(kApCrashCheatId, crash);
}

void ApApplication::RemoveCheatCodeStrings() {
  ScMessageTarget *target = static_cast<ScMessageTarget *>(this);
  ApGetMessageServer()->RemoveNotification(target, kApMsgPreCityChange);
  ApGetMessageServer()->RemoveNotification(target, kCtMsg_SaveToSegment);
  ApGetMessageServer()->RemoveNotification(target, kCtMsg_LoadFromSegment);
  cheats.RemoveNotification(target, 0);
}

// Notify the current city of idle processing when present and return the original false result to the framework.
bool ApApplication::OnIdle(u32) {
  if (city)
    city->OnIdle();
  return false;
}

bool ApApplication::LaunchBackgroundUpdaterExecutable() {
  return false;
}

void ApApplication::AddDynamicLibrariesHere() {
  ScRegisterBuiltInDirectors(static_cast<ScComCore *>(ApGetCom()));
}

void ApApplication::AddCOMDirectorsHere() {
  ApGetDirector()->AddDirector(ApGetCOMDirector());
}

// Refuse a second city, otherwise publish a successfully created nonnull city while preserving the COM lookup result.
// Notify the borrowed global application's BoxX after its temporary query reference has been released.
bool ApApplication::CreateCity() {
  if (city)
    return false;
  CtCity *created = nullptr;
  bool result = ApGetCom()->GetClassObject(kCtCityClassId, IID_CtCity, reinterpret_cast<void **>(&created));
  if (result && created)
    city = created;
  ApGameInterface *application = nullptr;
  if (ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventCityCreation, 0, 0, 0, 0);
  }
  return result;
}

// Pump pending notifications, pause the city and notify BoxX before retiring the actual audio adapter and simulation.
bool ApApplication::ShutdownCity() {
  if (city) {
    ApGetMessageServer()->OnTick(0);
    city->SimHiddenPause();
    if (GetBoxX())
      GetBoxX()->Event(kAuEventDetachCityInfo, 0, 0, 0, 0);
    if (audio_info) {
      audio_info->Shutdown();
      audio_info->Release();
      audio_info = nullptr;
    }
    city->EndCitySimulation();
    city->Shutdown();
    city->Release();
  }
  city = nullptr;
  cheats.ClearAllSetCheatCodes();
  return true;
}

// Stop the current simulation, notify BoxX and dispatch the original pre-load message before hiding the city window.
// Ask the borrowed global application to shut down its city last; both the message and final true result are preserved.
bool ApApplication::PreCityLoad() {
  if (city)
    city->EndCitySimulation();
  if (GetBoxX())
    GetBoxX()->Event(kAuEventDetachCityInfo, 0, 0, 0, 0);
  ApGetMessageServer()->OnTick(0);
  ScMessage message(kApMsgPreCityChange);
  ApGetMessageServer()->MessageSend(message);
  SpCityWindowView *view = ApGetCityWindow();
  if (view)
    view->AsIGZWin()->HideWindow();
  ApGameInterface *application = ApGetGameApp();
  if (application)
    application->ShutdownCity();
  return true;
}

// Read the live startup modifier state through the selected input backend.
static bool ApPreferencesOverrideHeld() {
  SDL_PumpEvents();
  SDL_Keymod modifiers = SDL_GetModState();
  return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) != 0;
}

// Load preferences unless Control or Shift is held, then apply registry and INI locale selection in their original order.
// Apply the stored startup video/sound settings after locale processing; explicit zero masks exclude unwritten source words.
void ApApplication::ReadPreferences() {
  if (ApPreferencesOverrideHeld())
    return;
  ApPreferences *saved = ApGetPreferences();
  ScLanguageManager *languages = ApGetLanguageManager();
  ApGetFramework();
  saved->Load();
  ScString language_name;
  i32 registry_language;
  if (UiReadRegistryNumber(
          ScString(R"(HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited\Language)"),
          &registry_language)) {
    if (static_cast<u32>(registry_language) >= 44)
      saved->SetLanguage(
          static_cast<i32>(languages->ConvertLanguageIDToLanguageType(static_cast<u32>(registry_language))));
    else
      saved->SetLanguage(registry_language);
  } else if (UiReadRegistryString(
                 ScString(R"(HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited\Language)"),
                 &language_name)) {
    u32 selected;
    if (languages->GetISO639LanguageIDFromLanguageCode(&language_name, &selected))
      saved->SetLanguage(static_cast<i32>(selected));
  }
  ScString country_name;
  i32 registry_country;
  if (UiReadRegistryNumber(
          ScString(R"(HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited\Country)"),
          &registry_country)) {
    if (static_cast<u32>(registry_country) < 57)
      saved->SetCountry(registry_country);
  } else if (UiReadRegistryString(
                 ScString(R"(HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited\Country)"),
                 &country_name)) {
    u32 selected;
    if (languages->GetISO3166CountryIDFromCountryCode(&country_name, &selected))
      saved->SetCountry(static_cast<i32>(selected));
  }
  if (saved->GetLanguage() == -1) {
    ScString ini_name, path, requested;
    ApIniFile ini;
    path = app_directory;
    GetAppIniFileName(&ini_name);
    path.Append(ini_name.begin, static_cast<u32>(ini_name.end - ini_name.begin));
    ini.SetPath(path);
    ScString key("Language"), section("Default Locale");
    u32 selected;
    if (ini.ReadEntry(section, key, &requested) &&
        (languages->GetLanguageIDFromLanguageEnglishName(&requested, &selected) ||
            languages->GetLanguageIDFromLanguageLocalName(&requested, &selected)) &&
        languages->CanSwitchToLanguage(selected)) {
      languages->SetCurrentLanguage(selected);
      saved->SetLanguage(static_cast<i32>(selected));
    } else {
      u32 current = languages->GetCurrentLanguage();
      if (languages->CanSwitchToLanguage(current)) {
        languages->SetCurrentLanguage(current);
        saved->SetLanguage(static_cast<i32>(current));
      }
    }
  } else if (languages->CanSwitchToLanguage(static_cast<u32>(saved->GetLanguage()))) {
    languages->SetCurrentLanguage(static_cast<u32>(saved->GetLanguage()));
  }
  if (saved->GetCountry() == -1) {
    saved->SetCountry(static_cast<i32>(languages->GetDefaultCountryForLanguage(0)));
    languages->SetCurrentCountry(static_cast<u32>(saved->GetCountry()));
  } else if (languages->CanSwitchToCountry(saved->GetCountry())) {
    languages->SetCurrentCountry(static_cast<u32>(saved->GetCountry()));
  }
  ApVideoSettings *video = saved->VideoPrefs();
  if (video->resolution_x && video->resolution_y) {
    GxGraphics *graphics = ApGetGraphics();
    FixStartupResolutionValuesIfNeeded(&video->resolution_x, &video->resolution_y);
    GxMode mode = {video->resolution_x, video->resolution_y, 16, 0, 0, 0, 0};
    graphics->PreInitSetDesiredGameResolution(mode);
    graphics->PreInitSetHardware3DAccellerationUse(video->use_hardware_acceleration != 0);
    graphics->PreInitSetWindowedMode(video->windowed != 0);
    graphics->PreInitSetDesiredVideoDriverIdentifier(video->driver_identifier);
  }
  ApSoundSettings *sound = saved->SoundPrefs();
  AuGetAppSoundDevice()->SetUseDriverSupport(sound->enable_3d_sound);
  AuRadio *station_radio = GetRadio();
  if (sound->music_volume <= 128) {
    station_radio->TurnOff();
  } else {
    if (!station_radio->IsOn())
      station_radio->TurnOn();
    station_radio->SetVolume(sound->music_volume);
  }
}

// Parse the actual external resolution switch before game code consumes both words; partial scans leave original bytes undefined.
static void ApReadStartupResolution(const ScString &text, u32 *width, u32 *height) {
#pragma warning(suppress : 4996)
  [[maybe_unused]] int converted = sscanf(text.begin, "%ux%u", width, height);
  assert(converted == 2);
}

// Apply the original driver String edits to its already-initialized native32-byte span.
// ReadCommandLine reserves that span before extraction, retaining prior defined contents while excluding unwritten tail reads.
static void ApPrepareStartupDriver(ScString *driver) {
  u32 length = driver->ScString::Strlen();
  if (length < 32)
    driver->Append("", 1);
  else
    driver->begin[31] = 0;
}

// Apply startup switches in order, with restart suppressing only resolution and driver overrides.
// The single executable defaults to a window before the original fullscreen and windowed switches are applied.
void ApApplication::ReadCommandLine() {
  ScFramework *framework = ApGetFramework();
  ScLanguageManager *languages = ApGetLanguageManager();
  ScCmdLine *command_line = framework->CommandLine();
  GxGraphics *graphics = ApGetGraphics();
  ScString locale, value, restart("restart");
  // The actual graphics consumer copies32 bytes; define the native spare span before any command-line writes reuse it.
  u8 driver_span[32] = {};
  value.Append(driver_span, sizeof(driver_span));
  value.Erase(0, sizeof(driver_span));
  if (!command_line->IsSwitchPresent(restart, &value, true)) {
    if (command_line->IsSwitchPresent('r', &value, true)) {
      u32 width, height;
      ApReadStartupResolution(value, &width, &height);
      FixStartupResolutionValuesIfNeeded(&width, &height);
      GxMode mode = {width, height, 16, 0, 0, 0, 0};
      graphics->PreInitSetDesiredGameResolution(mode);
    }
    if (command_line->IsSwitchPresent('d', &value, true)) {
      ApPrepareStartupDriver(&value);
      graphics->PreInitSetDesiredVideoDriverIdentifier(reinterpret_cast<u8 *>(value.begin));
    }
  }
#if defined(SC3_MONOLITHIC)
  // The rebuilt desktop application opens in a draggable native window unless fullscreen is explicitly selected.
  graphics->PreInitSetWindowedMode(true);
#endif
  if (command_line->IsSwitchPresent('f', &value, true))
    graphics->PreInitSetWindowedMode(false);
  if (command_line->IsSwitchPresent('w', &value, true))
    graphics->PreInitSetWindowedMode(true);
  u32 selected;
  if (command_line->IsSwitchPresent('l', &locale, true) &&
      (languages->GetLanguageIDFromLanguageEnglishName(&locale, &selected) ||
          languages->GetLanguageIDFromLanguageLocalName(&locale, &selected)) &&
      languages->CanSwitchToLanguage(selected)) {
    languages->SetCurrentLanguage(selected);
    ApGetPreferences()->SetLanguage(static_cast<i32>(selected));
  }
  if (command_line->IsSwitchPresent('c', &locale, true) &&
      (languages->GetCountryIDFromCountryEnglishName(&locale, &selected) ||
          languages->GetCountryIDFromCountryLocalName(&locale, &selected)) &&
      languages->CanSwitchToCountry(static_cast<i32>(selected))) {
    ApGetPreferences()->SetCountry(static_cast<i32>(selected));
    languages->SetCurrentCountry(selected);
  }
  command_line->IsSwitchPresent('x', &locale, true);
  value = ScString("cheats");
  if (command_line->IsSwitchPresent(value, &locale, true)) {
    cheats.EnableCheatCodes(false, locale);
    cheats_disabled_on_startup = true;
  }
  if (command_line->IsSwitchPresent('p', &locale, true)) {
    u32 mhz = 200, memory = 128, processor_power = 6, memory_power = 5;
#pragma warning(suppress : 4996)
    sscanf(locale.begin, "%u %u %u %u", &mhz, &memory, &processor_power, &memory_power);
    if (mhz < 100)
      mhz = 100;
    if (mhz > 1000)
      mhz = 1000;
    if (memory < 16)
      memory = 16;
    if (processor_power < 5)
      processor_power = 5;
    if (processor_power > 10)
      processor_power = 10;
    if (memory_power < 4)
      memory_power = 4;
    performance.processor_mhz = static_cast<i16>(mhz);
    performance.memory_mb = static_cast<i16>(memory);
    performance.processor_power = static_cast<i8>(processor_power);
    performance.memory_power = static_cast<i8>(memory_power);
  }
}

// Bind real language/sound services, initialize disk stations and transfer any COM station reference into the radio.
void ApApplication::SetupRadio() {
  ScString directory, option;
  ScCmdLine *command_line = ScFrameworkCore::FrameWork()->CommandLine();
  directory = ScString(ApGetFileSystem()->DataDirectory());
  directory.Append("res\\sound\\radio\\");
  radio->SetLanguageManager(ApGetLanguageManager());
  radio->SetSoundSystem(AuGetAppSoundDevice());
  radio->SetRadioDirectory(directory);
  radio->Init();
  AuRadioStation *station = nullptr;
  if (ApGetCom()->GetClassObject(kAuRadioStationClassId, IID_AuRadioStation, reinterpret_cast<void **>(&station)) &&
      station)
    radio->AddStation(station);
  {
    ScString saved(*ApGetPreferences()->GetRadioCallSign());
    if (saved.begin == saved.end) {
      u32 count = radio->GetStationCount();
      if (count) {
        ScRandom random(kScRandomSeedFromClock);
        radio->SetStation(random.Bounded(count));
      }
    } else {
      radio->SetStation(saved.begin);
    }
    option = ScString("station");
    if (command_line->IsSwitchPresent(option, &directory, true) && directory.ScString::Strlen() >= 4)
      radio->SetStation(directory.begin);
    option = ScString("radio");
    if (command_line->IsSwitchPresent(option, &directory, true)) {
      if (directory.ScString::Strlen() >= 3 && !memcmp(directory.begin, "off", 3))
        radio->TurnOff();
      else if (directory.ScString::Strlen() >= 2 && !memcmp(directory.begin, "on", 2))
        radio->TurnOn();
    }
  }
}

// Register cheat notifications and registered dialogs after successful initialization, then load the requested city or
// main menu.
bool ApApplication::PostFrameWorkInit() {
  if (initialized) {
    RegisterCheatCodeStrings();
    WnWindowManager *dialogs = ApGetDialogManager();
    if (dialogs)
      dialogs->CreateRegWins();
    if (input_filename.begin == input_filename.end || !main_window->DoLoadCity(&input_filename)) {
      if (main_window)
        main_window->DoMainMenu();
    }
  } else {
    Quit(false, false);
  }
  return true;
}

static ScString ApReadRadioCallSign(AuRadio *radio) {
  char name[4];
  [[maybe_unused]] bool has_station = radio->GetCurrentStation(name);
  assert(has_station);
  u32 length = 0;
  while (length < sizeof(name) && name[length])
    ++length;
  return ScString(name, length);
}

// Save the radio call sign and local application preferences before retiring the idle detector.
// Remove cheat subscriptions last, after the framework's idle registration and the application's detector reference are released.
bool ApApplication::PreFrameWorkShutdown() {
  {
    ScString name = ApReadRadioCallSign(radio);
    ApGetPreferences()->SetRadioCallSign(name);
  }
  ApGetPreferences()->Save();
  if (idle_detector) {
    ApGetFramework()->RemoveFromOnIdle(idle_detector);
    idle_detector->Release();
    idle_detector = nullptr;
  }
  RemoveCheatCodeStrings();
  return true;
}

bool ApApplication::IsStartupResolutionOK(u32 width, u32 height) {
  return (width == 640 && height == 480) || (width == 800 && height == 600) || (width == 1024 && height == 768) ||
      (width == 1152 && height == 864) || (width == 1280 && height == 1024);
}

void ApApplication::FixStartupResolutionValuesIfNeeded(u32 *, u32 *) {
}

HINSTANCE__ *ApFramework::instance;
ScUnknown *ApFrameworkReleaseTarget;

// Construct the actual platform framework and return its primary identity with the constructor's zero reference count.
// Allocation failure follows the installed global new handler, without inventing a game status result.
ScFramework *ScFrameworkCore::CreateFrameWork() {
  return new ApFramework;
}

// The quit result stays unwritten until Quit requests a console-mode exit.
ApFramework::ApFramework() : window(nullptr), references(0), quit_requested(false) {
#if defined(_WIN32)
  instance = GetModuleHandleA(nullptr);
#else
  instance = nullptr;
#endif
}

ApFramework::~ApFramework() {
}

// Publish the primary framework or secondary Win32 identity, then retain through the primary virtual method. Rejected
// queries clear the output pointer.
bool ApFramework::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScFramework);
  HANDLE_QUERY_INTERFACE(IID_ScFramework, ScFramework);
  HANDLE_QUERY_INTERFACE(IID_ScWin32Framework, ScWin32Framework);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ApFramework::AddRef() {
  return ++references;
}

u32 ApFramework::Release() {
  --references;
  return references ? references : FinalRelease();
}

u32 ApFramework::FinalRelease() {
  ScRelease(&ApFrameworkReleaseTarget);
  return 0;
}

// Borrow the primary framework from its separate Win32 identity. No additional reference is acquired.
ScFramework *ApFramework::GetFramework() {
  return this;
}

// Borrow the process instance captured after construction. Every framework instance reads the same original static handle.
HINSTANCE__ *ApFramework::GetInstance() {
  return instance;
}

// Return the borrowed main-window handle. Reading the handle does not create or retain a window.
HWND__ *ApFramework::GetWindowHandle() {
  return window;
}

void ApFramework::SetWindowHandle(HWND__ *value) {
  window = value;
}

bool ApFramework::PreAppInit() {
  return ScFrameworkCore::PreAppInit();
}

bool ApFramework::PostAppShutdown() {
  ScFrameworkCore::PostAppShutdown();
  return true;
}

// Borrow the embedded COM primary identity. Its lifetime is owned by this framework rather than a new allocation.
ScCom *ApFramework::GetCOMObject() {
  return &com;
}

void ApFramework::ShutdownGZCOM() {
  com.RealShutdown();
}

// Drain at most501 queued messages before each enabled idle notification. WM_QUIT publishes its actual exit code.
// Console mode also exits after its live quit flag is set; Run itself has the original void return type.
void ApFramework::Run() {
  while (!quit_requested) {
    SDL_Event event;
    u32 processed = 0;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        Quit(0);
        break;
      }
      WnDispatchSdlEvent(&event);
      if (++processed > 500)
        break;
    }
    if (!quit_requested && running)
      OnIdle();
    GxSdlDrainFrame();
  }
  ScApplicationExitCode = quit_code;
}

// Request the original console-mode exit by setting its flag before the code. Ordinary GUI mode posts WM_QUIT instead.
void ApFramework::Quit(i32 reason) {
  quit_requested = true;
  quit_code = reason;
}

// Deliver abortive-quit hooks between the original debugger messages, then terminate without normal runtime cleanup.
// This explicit application operation is separate from recoverable load or service failures.
void ApFramework::AbortiveQuit(i32 reason) {
  SDL_Log("cGZFrameWorkW95::AbortiveQuit()");
  HookAbortiveQuit();
  SDL_Log("cGZFrameWorkW95::AbortiveQuit(): Calling _exit().");
  _exit(reason);
}

// Suspend idle notifications while the exception dialog is displayed, then unconditionally enable them again.
void ApFramework::ReportException(const char *text) {
  if (!text)
    text = "??? Unknown exception ???";
  running = false;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Exception encountered", text, GxSdlWindow());
  running = true;
}

static ScMessageServer *ApMessages;
static RsManager *ApResources;
static GxGraphics *ApGraphics;
static FtFontSystem *ApFonts;
static ScLanguageManager *ApLanguages;
static WnWindowManager *ApWindows;
static WnResources *ApControls;
static ApGameInterface *ApGameApp;
static bool ApGameAppInitialized;
static ScCommandFactory *ApCommands;
static AuSoundDevice *ApSounds;
static ScVariableManager *ApVariables;

// Acquire the alpha-blending service and release its temporary reference before returning the borrowed interface.
GxBlendService *ApServices::AB() {
  GxBlendService *service = nullptr;
  ApGetFramework()->GetSystemService(kGxBlendServiceId, IID_GxBlendService, reinterpret_cast<void **>(&service));
  if (service)
    service->Release();
  return service;
}

// Acquire the span-blitting service and release the temporary reference before returning its borrowed identity.
GxSpanBlt *ApServices::SB() {
  ScUnknown *service = nullptr;
  ApGetFramework()->GetSystemService(
      kGxColorFunctionsServiceId, IID_GxColorFunctions, reinterpret_cast<void **>(&service));
  GxSpanBlt *result = reinterpret_cast<GxSpanBlt *>(service);
  if (service)
    service->Release();
  return result;
}

// Cache the variable-manager service, releasing the temporary framework acquisition reference.
ScVariableManager *ApServices::VM() {
  if (!ApVariables) {
    ScVariableManager *manager = nullptr;
    ApGetFramework()->GetSystemService(
        kScVariableManagerServiceId, IID_ScVariableManager, reinterpret_cast<void **>(&manager));
    ApVariables = manager;
    if (manager)
      manager->Release();
  }
  return ApVariables;
}

// Borrow the application's buffer-resource factory when the game application is available.
RsImageOptions *ApServices::BMP() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetBufferResourceFactory() : nullptr;
}

// Borrow the application's performance information when the game application is available.
ApPerformanceInfo *ApServices::SPI() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetSystemPerformanceInfo() : nullptr;
}

bool ApServices::IsCityActive() {
  return ApGetCity() != nullptr;
}

bool ApServices::IsWCVActive() {
  return ApGetCityWindow() != nullptr;
}

// Copy the file system's application directory when both the service and its returned path exist.
void ApServices::ADir(ScString *out) {
  RsFileSystem *files = ApGetFileSystem();
  if (files) {
    const char *path = files->AppDirectory();
    if (path)
      *out = ScString(path);
  }
}

void ApServices::RDir(ScString *out) {
  DDir(out);
  out->Append("Res\\");
}

void ApServices::PDir(ScString *out) {
  DDir(out);
  out->Append("PlugIn\\");
}

// Borrow the current application's city definition, or return null when no game application exists.
CtDefinition *ApServices::CDef() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetAppCityDefinition() : nullptr;
}

// Borrow the current application's agent-type tree without changing its reference count.
CtAgentTree *ApServices::ATT() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetAppAgentTypeTree() : nullptr;
}

// Borrow the current application's occupant-attribute cache when an application is present.
OcAttribCache *ApServices::OC() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetAppOccupantAttribCache() : nullptr;
}

// Copy the current file system's data directory when it is available, leaving the output unchanged otherwise.
void ApServices::DDir(ScString *out) {
  RsFileSystem *files = ApGetFileSystem();
  if (files && files->DataDirectory())
    *out = ScString(files->DataDirectory());
}

void ApServices::SDir(ScString *out) {
  DDir(out);
  out->Append("Sys\\");
}

// Borrow the executable's current framework through the original free-function forwarding entry.
ScFramework *ApGetFramework() {
  return ScFrameworkCore::FrameWork();
}

// Acquire the executable's retained application once, publishing the guard before the actual query callback.
// A failed first query stays cached as null; the original registered exit callback has no effects.
ApGameInterface *ApGetGameApp() {
  if (!ApGameAppInitialized) {
    ApGameAppInitialized = true;
    ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, (void **)&ApGameApp);
  }
  return ApGameApp;
}

// Implement original SL::BX by borrowing the live BoxX member from the lazily acquired application on every call.
// A missing cached application produces the original null result without constructing a replacement service.
ApBoxX *ApServices::BoxX() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetBoxX() : nullptr;
}

// Acquire the sprite image service and drop the temporary reference before returning the borrowed interface.
SpImageCache *ApServices::IM() {
  SpImageCache *images = nullptr;
  ApGetFramework()->GetSystemService(kSpImageCacheServiceId, IID_SpImageCache, reinterpret_cast<void **>(&images));
  if (images)
    images->Release();
  return images;
}

// Publish the command-factory service in the original borrowed cache before dropping its temporary reference.
// A null lookup remains retryable on the next call; no extra application or COM query is introduced.
ScCommandFactory *ApServices::GCF() {
  if (!ApCommands) {
    ScCommandFactory *commands = nullptr;
    ApGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&commands));
    ApCommands = commands;
    if (commands)
      commands->Release();
  }
  return ApCommands;
}

// Return the current borrowed city from the executable's cached application. An unavailable application yields null.
CtCity *ApGetCity() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetCity() : nullptr;
}

// Query the application's live city-window identity on every call, preserving changes between repeated requests.
SpCityWindowView *ApGetCityWindow() {
  ApGameInterface *game = ApGetGameApp();
  return game ? game->GetISC3WinCityView() : nullptr;
}

// Borrow the active city view from the current city window, returning null when there is no window.
SpViewInterface *ApGetCurrentCityView() {
  SpCityWindowView *window = ApGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the sprite manager exposed by the current isometric view.
SpManagerView *ApGetCitySpriteManager() {
  SpViewInterface *view = ApGetCurrentCityView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteManager() : nullptr;
}

// Borrow the sprite-cell map exposed by the current isometric view.
SpCellMap *ApGetCitySpriteCellMap() {
  SpViewInterface *view = ApGetCurrentCityView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteCellMap() : nullptr;
}

// Borrow the current view's animation tick manager without retaining it.
SpTickManager *ApGetAnimationTickManager() {
  SpViewInterface *view = ApGetCurrentCityView();
  return view ? view->GetAnimTickManager() : nullptr;
}

// Ask the executable's actual module director for its borrowed COM identity on every call.
ScCom *ApGetCom() {
  return ApGetDirector()->GZCOM();
}

// Publish the acquired message server in the module cache before releasing its temporary reference.
// Failed lookups leave the cache empty so the next call retries the original service request.
ScMessageServer *ApGetMessageServer() {
  if (!ApMessages) {
    ScMessageServer *messages = nullptr;
    ApGetFramework()->GetSystemService(
        kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&messages));
    ApMessages = messages;
    if (messages)
      messages->Release();
  }
  return ApMessages;
}

// Resolve the actual resource manager, not the old inferred string-manager identity, and cache it before release.
RsManager *ApGetResourceManager() {
  if (!ApResources) {
    RsManager *resources = nullptr;
    ApGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, reinterpret_cast<void **>(&resources));
    ApResources = resources;
    if (resources)
      resources->Release();
  }
  return ApResources;
}

// Cache the borrowed graphics system before dropping the service lookup's acquired reference.
GxGraphics *ApGetGraphics() {
  if (!ApGraphics) {
    GxGraphics *graphics = nullptr;
    ApGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&graphics));
    ApGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return ApGraphics;
}

// Prefer the original font service and try the legacy service only when that acquisition produced no pointer.
// Both paths publish the selected borrowed cache before releasing their temporary acquired identities.
FtFontSystem *ApGetFontManager() {
  if (!ApFonts) {
    FtFontSystem *preferred = nullptr;
    ApGetFramework()->GetSystemService(kFtAppFontServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&preferred));
    if (preferred)
      ApFonts = preferred;
    else {
      FtFontSystem *fallback = nullptr;
      ApGetFramework()->GetSystemService(
          kFtFontSystemServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&fallback));
      ApFonts = fallback;
      if (fallback)
        fallback->Release();
    }
    if (preferred)
      preferred->Release();
  }
  return ApFonts;
}

// Borrow the installed language service through its real service ID and interface identity.
ScLanguageManager *ApGetLanguageManager() {
  if (!ApLanguages) {
    ScLanguageManager *languages = nullptr;
    ApGetFramework()->GetSystemService(
        kScLanguageManagerServiceId, IID_ScLanguageManager, reinterpret_cast<void **>(&languages));
    ApLanguages = languages;
    if (languages)
      languages->Release();
  }
  return ApLanguages;
}

// Publish the original window-manager cache before releasing its temporary lookup reference.
WnWindowManager *ApGetDialogManager() {
  if (!ApWindows) {
    WnWindowManager *windows = nullptr;
    ApGetFramework()->GetSystemService(
        kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&windows));
    ApWindows = windows;
    if (windows)
      windows->Release();
  }
  return ApWindows;
}

// Resolve the genuine control-resource manager and retain only the original borrowed cached pointer.
WnResources *ApGetUiResources() {
  if (!ApControls) {
    WnResources *controls = nullptr;
    ApGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&controls));
    ApControls = controls;
    if (controls)
      controls->Release();
  }
  return ApControls;
}

// Cache the executable's borrowed filesystem service before releasing the temporary acquired reference.
// A missing service remains retryable without borrowing another DLL's framework or cache.
RsFileSystem *ApGetFileSystem() {
  static RsFileSystem *file_system;
  if (!file_system) {
    RsFileSystem *value = nullptr;
    ApGetFramework()->GetSystemService(RsFileSystem::ServiceId, RsFileSystem::Iid, reinterpret_cast<void **>(&value));
    file_system = value;
    if (value)
      value->Release();
  }
  return file_system;
}

// Borrow the live application's preference interface on every call; a missing application yields the original null result.
ApPreferences *ApGetPreferences() {
  ApGameInterface *application = ApGetGameApp();
  return application ? application->GetAppPreferences() : nullptr;
}
