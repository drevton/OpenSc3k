#include "city/services.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/api.h"
#if defined(_WIN32)
#include <windows.h>
#include <winver.h>
#else
#include <errno.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#endif

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "city/module.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "window/api.h"

// Native memory reporting saturates at the largest supported four-MiB multiple.
enum CtHostMemoryLimit : u32 {
  kCtHostMemoryMaximumMiB = 0xfffffffc,
};

// Return the borrowed COM factory interface from this module's actual director on every request.
ScCom *CtGetCom() {
  return CtGetDirector()->GZCOM();
}

static ScCommandFactory *CtCommandCatalogCache;

// Return the cached command class-factory service, retrying a missing service on the next request.
// This is the full acquisition phase; publish before releasing the temporary lookup reference.
ScCommandFactory *CtGetCommandCatalog() {
  if (!CtCommandCatalogCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &out);
    CtCommandCatalogCache = static_cast<ScCommandFactory *>(out);
    if (CtCommandCatalogCache)
      CtCommandCatalogCache->Release();
  }
  return CtCommandCatalogCache;
}

// Push the requested automatic cursor through the available window manager's live cursor service.
// Remember only a successful push; absence of either service leaves this scope inactive.
CtCursorScope::CtCursorScope(u32 identifier) : pushed(false) {
  WnWindowManager *windows = CtGetWindowManager();
  if (windows) {
    WnCursorManager *cursors = windows->GetCursorManager();
    if (cursors)
      pushed = cursors->PushAutoCursor(identifier);
  }
}

// Retire a successful push through freshly looked-up services rather than retaining its original cursor manager.
// Missing services at destruction leave the original stack untouched.
CtCursorScope::~CtCursorScope() {
  if (pushed) {
    WnWindowManager *windows = CtGetWindowManager();
    if (windows) {
      WnCursorManager *cursors = windows->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
}

static_assert(sizeof(CtCursorScope) == 1);

// Resolve and expand the dialog text, retain an independent heap String during synchronous message delivery, then release it.
// Return the actual identifier written into the stack payload; the message server's predicate is intentionally ignored.
u32 SC3_STDCALL CtRequestDialog(u32 textID) {
  CtTextResource resource(textID, kRsNewsTextGroup);
  ScString expanded;
  resource.CopyTo(&expanded);
  ScString *text = new ScString(expanded);
  text->AddRef();
  CtDialogRequest request{0, kRsNewsTextGroup, textID, text, {0, 0, 0, 0}};
  ScMessage message{kApMsgCreateNotification, reinterpret_cast<uptr>(&request), 0, 0};
  CtGetMessageServer()->MessageSend(message);
  text->Release();
  return request.identifier;
}

static GxGraphics *CtGraphicsCache;

// Publish the graphics service before releasing its temporary lookup hold, retaining the original borrowed cache.
// A missing output remains uncached so the next request repeats the framework lookup.
GxGraphics *CtGetGraphics() {
  if (!CtGraphicsCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &out);
    CtGraphicsCache = static_cast<GxGraphics *>(out);
    if (CtGraphicsCache)
      CtGraphicsCache->Release();
  }
  return CtGraphicsCache;
}

static ScLanguageManager *CtLanguageCache;

// Cache the actual language-manager interface before releasing its temporary framework lookup reference.
ScLanguageManager *CtGetLanguageManager() {
  if (!CtLanguageCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &out);
    CtLanguageCache = static_cast<ScLanguageManager *>(out);
    if (CtLanguageCache)
      CtLanguageCache->Release();
  }
  return CtLanguageCache;
}

// Acquire the common layer interface, then optionally query the separate primary interface directly into its output.
// A failed query releases and clears only the common output; factory failure preserves its actual output effects.
bool SC3_STDCALL CtAcquireLayer(u32 classID, CtLayer **common, u32 iid, void **primary) {
  if (!CtGetCom()->GetClassObject(classID, IID_CtLayer, reinterpret_cast<void **>(common)))
    return false;
  if (iid && !(*common)->QueryInterface(iid, primary)) {
    (*common)->Release();
    *common = nullptr;
    return false;
  }
  return true;
}

CtProgress::CtProgress(u32 count, ScStringView *text) : total(count + 1), current(0) {
  CtGetGameApp()->ShowProgressBar(total, text);
  Step();
}

// Complete and hide the actual progress display before emitting the original BoxX completion event.
CtProgress::~CtProgress() {
  CtGetGameApp()->UpdateProgressBar(total);
  CtGetGameApp()->HideProgressBar();
  CtGetBoxX()->Event(kAuEventCityReady, 0, 0, 0, 0);
}

// Increment the unsigned counter and clamp only the displayed position to the scope's original total.
void CtProgress::Step() {
  ++current;
  u32 *position = total >= current ? &current : &total;
  ApGameInterface *application = CtGetGameApp();
  application->UpdateProgressBar(*position);
}

static RsManager *CtResourceManagerCache;

// Publish the borrowed resource manager before releasing the temporary service lookup hold, retrying a null cache later.
RsManager *CtGetResourceManager() {
  if (!CtResourceManagerCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &out);
    CtResourceManagerCache = static_cast<RsManager *>(out);
    if (CtResourceManagerCache)
      CtResourceManagerCache->Release();
  }
  return CtResourceManagerCache;
}

static ScMessageServer *CtMessageServerCache;
static ScQueueManager *CtQueueManagerCache;

// Obtain the live framework directly from this module's real COM director.
ScFramework *CtGetFramework() {
  return CtGetDirector()->FrameWork();
}

// Acquire the worker's framework once, retaining the original borrowed pointer even when acquisition returns null.
ScFramework *CtGetThreadFramework() {
  static ScFramework *framework = CtGetFramework();
  return framework;
}

// Cache the acquired message-server interface without retaining the temporary service lookup reference.
ScMessageServer *CtGetMessageServer() {
  if (!CtMessageServerCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &out);
    CtMessageServerCache = static_cast<ScMessageServer *>(out);
    if (CtMessageServerCache)
      CtMessageServerCache->Release();
  }
  return CtMessageServerCache;
}

// Cache the actual queue-manager interface while releasing the temporary system-service lookup reference.
ScQueueManager *CtGetQueueManager() {
  if (!CtQueueManagerCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kScQueueManagerServiceId, IID_ScQueueManager, &out);
    CtQueueManagerCache = static_cast<ScQueueManager *>(out);
    if (CtQueueManagerCache)
      CtQueueManagerCache->Release();
  }
  return CtQueueManagerCache;
}

CtTextResource::CtTextResource(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  if (!CtGetResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

CtTextResource::~CtTextResource() {
  ScRelease(&text);
}

// Copy the retained text through its genuine String interface, or construct an empty owned String when lookup failed.
ScString CtTextResource::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Missing resources leave the output untouched; 1000F0EF returns only the resource-presence predicate.
bool CtTextResource::CopyTo(ScStringView *out) {
  if (!text)
    return false;
  ScString expanded(*text);
  CtExpandTokens(&expanded);
  out->FromChar(expanded.Data(), expanded.Strlen());
  return true;
}

static bool CtTokenEquals(const ScString &token, const char *name, u32 length) {
  return (u32)(token.end - token.begin) == length && !memcmp(token.begin, name, length);
}

bool SC3_STDCALL CtTryToken(ScString *text, const ScString &token, u32 *position) {
  u32 tokenLength = token.Strlen(), start = *position, length = (u32)(text->end - text->begin);
  u32 count = start < length ? length - start : 0;
  if (count > tokenLength)
    count = tokenLength;
  ScString candidate = start < length ? ScString(text->begin + start, count) : ScString();
  if ((u32)(candidate.end - candidate.begin) != (u32)(token.end - token.begin) ||
      memcmp(candidate.begin, token.begin, (usize)(candidate.end - candidate.begin)))
    return false;
  if (CtTokenEquals(token, "%MAYOR%", 7) || CtTokenEquals(token, "%YOURNAME%", 10)) {
    CtCity *city = CtGetCity();
    if (city) {
      ScStringView *sizeName = city->GetMayorName(), *dataName = city->GetMayorName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetMayorName()->Strlen() - 1;
    }
    return true;
  }
  if (CtTokenEquals(token, "%CITYNAME%", 10) || CtTokenEquals(token, "%YOURCITY%", 10)) {
    CtCity *city = CtGetCity();
    if (city) {
      ScStringView *sizeName = city->GetCityName(), *dataName = city->GetCityName();
      u32 size = sizeName->Strlen();
      const char *data = dataName->Data();
      u32 remove = token.Strlen();
      text->Replace(*position, remove, data, size);
      *position += city->GetCityName()->Strlen() - 1;
    }
    return true;
  }
  if (CtTokenEquals(token, "%POPULATION%", 12)) {
    ScString fallback("0");
    CtCity *city = CtGetCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", population);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, (u32)(number.end - number.begin));
      *position += (u32)(number.end - number.begin) - 1;
    } else {
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position += (u32)(fallback.end - fallback.begin) - 1;
    }
    return true;
  }
  if (CtTokenEquals(token, "%YEAR%", 6)) {
    CtCity *city = CtGetCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      ScString number;
      char buffer[12];
      int size = snprintf(buffer, sizeof(buffer), "%d", (i32)year);
      number.FromChar(buffer, (u32)size);
      u32 remove = token.Strlen();
      text->Replace(*position, remove, number.begin, (u32)(number.end - number.begin));
      *position += (u32)(number.end - number.begin) - 1;
    } else {
      ScString fallback("1900");
      u32 remove = token.Strlen();
      text->Replace(*position, remove, fallback.begin, (u32)(fallback.end - fallback.begin));
      *position += (u32)(fallback.end - fallback.begin) - 1;
    }
    return true;
  }
  if (CtTokenEquals(token, "%ANYNEIGHBOR%", 13)) {
    CtCity *city = CtGetCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return false;
    ScRandom random(kScRandomSeedFromClock);
    ScString name;
    neighbors->GetNeighborName((u8)random.Bounded(4), &name);
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, (u32)(name.end - name.begin));
    *position += (u32)(name.end - name.begin) - 1;
    return true;
  }
  if (CtTokenEquals(token, "%PARADENAME%", 12)) {
    ScString name;
    i32 country = CtGetPreferences()->GetCountry();
    u32 instance = 0;
    switch (country) {
    case 6:
      instance = 0x258;
      break;
    case 17:
      instance = 0x259;
      break;
    case 19:
      instance = 0x25a;
      break;
    case 31:
      instance = 0x25b;
      break;
    case 32:
      instance = 0x25c;
      break;
    case 45:
      instance = 0x25d;
      break;
    case 46:
      instance = 0x25e;
      break;
    case 49:
      instance = 0x25f;
      break;
    case 54:
      instance = 0x260;
      break;
    }
    if (instance) {
      CtTextResource resource(instance, kRsSupplementalGameTextGroup);
      name = resource.GetText();
    }
    u32 remove = token.Strlen();
    text->Replace(*position, remove, name.begin, (u32)(name.end - name.begin));
    *position += (u32)(name.end - name.begin) - 1;
  }
  return true;
}

static const ScString CtTokenNames[8] = {ScString("%MAYOR%", 7), ScString("%YOURNAME%", 10), ScString("%CITYNAME%", 10),
    ScString("%YOURCITY%", 10), ScString("%POPULATION%", 12), ScString("%YEAR%", 6), ScString("%PARADENAME%", 12),
    ScString("%ANYNEIGHBOR%", 13)};

static u32 CtFindPercent(const ScString &text, u32 start) {
  u32 length = text.Strlen();
  const char *bytes = text.Data();
  for (u32 i = start; i < length; ++i)
    if (bytes[i] == '%')
      return i;
  return UINT32_MAX;
}

u32 SC3_STDCALL CtExpandTokens(ScString *text) {
  u32 position = CtFindPercent(*text, 0);
  while (position < text->Strlen()) {
    CtSubstituteToken(text, &position);
    position = CtFindPercent(*text, position + 1);
  }
  return position;
}

// Try the eight original names in their short-circuit order, with neighbor preceding parade despite static init order.
// The real leaf updates the shared text and position before the next live scan.
bool SC3_STDCALL CtSubstituteToken(ScString *text, u32 *position) {
  return CtTryToken(text, CtTokenNames[0], position) || CtTryToken(text, CtTokenNames[1], position) ||
      CtTryToken(text, CtTokenNames[2], position) || CtTryToken(text, CtTokenNames[3], position) ||
      CtTryToken(text, CtTokenNames[4], position) || CtTryToken(text, CtTokenNames[5], position) ||
      CtTryToken(text, CtTokenNames[7], position) || CtTryToken(text, CtTokenNames[6], position);
}

#ifdef _MSC_VER
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#if defined(_WIN32)
// Require a terminated module path before another platform API consumes it. A terminated truncation remains valid.
static void CtReadModulePath(HMODULE module, char *filename) {
  DWORD length = GetModuleFileNameA(module, filename, 260);
  if (!length || (length >= 260 && !memchr(filename, 0, 260))) {
    assert(false && "The original module path would consume undefined storage");
    abort();
  }
}

// Require the borrowed translation before a getter consumes the loaded version resource.
// A failed data read retains owned storage in stock but supplies no resource that can be queried safely.
static const u16 *CtRequireVersionTranslation(const void *translation) {
  if (!translation) {
    assert(false && "The original version getter would consume unavailable resource data");
    abort();
  }
  return static_cast<const u16 *>(translation);
}
#else
// Read a real host OS name and release; on macOS prefer the product version over the Darwin kernel release.
static bool CtHostSystemVersion(char *name, usize name_size, char *release, usize release_size) {
  struct utsname value;
  if (uname(&value) != 0)
    return false;
#if defined(__APPLE__)
  usize size = release_size;
  if (sysctlbyname("kern.osproductversion", release, &size, nullptr, 0) == 0 && size && size <= release_size) {
    snprintf(name, name_size, "%s", "macOS");
    return true;
  }
#endif
  snprintf(name, name_size, "%s", value.sysname);
  snprintf(release, release_size, "%s", value.release);
  return true;
}

// Round a native byte count up to the original four-MiB units without overflowing the output word.
static u32 CtHostMemoryMiB(u64 bytes) {
  u64 megabytes = bytes >> 20;
  if (!megabytes)
    return 0;
  megabytes = ((megabytes - 1) | 3u) + 1;
  return megabytes > kCtHostMemoryMaximumMiB ? kCtHostMemoryMaximumMiB : static_cast<u32>(megabytes);
}
#endif

CtVersionInfo::CtVersionInfo() : module(0), data(nullptr), translation(nullptr) {
}

// Release owned version bytes while leaving the borrowed translation pointer unused during destruction.
CtVersionInfo::~CtVersionInfo() {
  if (data) {
    delete[] data;
    data = nullptr;
  }
}

// Load the executable version data lazily; retail LoadVersionInfo is void with no explicit arguments.
// A failed data read retains its allocation and skips the translation query; only a failed query frees the bytes.
void CtVersionInfo::LoadVersionInfo() {
  if (data)
    return;
#if defined(_WIN32)
  module = (uptr)GetModuleHandleA(nullptr);
  char filename[264];
  CtReadModulePath(GetModuleHandleA(nullptr), filename);
  DWORD handle;
  UINT length;
  u32 size = GetFileVersionInfoSizeA(filename, &handle);
  if (size) {
    data = new u8[size];
    if (GetFileVersionInfoA(filename, handle, size, data) &&
        !VerQueryValueA(data, "\\VarFileInfo\\Translation", &translation, &length)) {
      delete[] data;
      data = nullptr;
    }
  }
#endif
}

// Copy the executable module's returned filename bytes, preserving the caller's String on API failure.
bool CtVersionInfo::GetModuleActualFileName(ScString *out) {
  ScString path = RsApplicationFilePath();
  if (path.begin == path.end)
    return false;
  *out = path;
  return true;
}

#if defined(_WIN32)
// Query the localized file description after lazy version loading; clear the output only when the query fails.
bool CtVersionInfo::GetModuleName(ScString *out) {
  if (!data) {
    LoadVersionInfo();
    if (!data)
      return false;
  }
  const u16 *language = CtRequireVersionTranslation(translation);
  char query[256];
  wsprintfA(query, "\\StringFileInfo\\%04lx%04lx\\%s", static_cast<unsigned long>(language[0]),
      static_cast<unsigned long>(language[1]), "FileDescription");
  void *value;
  UINT length;
  bool found = VerQueryValueA(data, query, &value, &length) != 0;
  if (found) {
    ScString text(static_cast<const char *>(value));
    *out = text;
  } else {
    out->Erase(0, kScStringToEnd);
  }
  return found;
}

// Query the localized file-version String, retaining the original empty-output behavior on query failure.
bool CtVersionInfo::GetModuleVersion(ScString *out) {
  if (!data) {
    LoadVersionInfo();
    if (!data)
      return false;
  }
  const u16 *language = CtRequireVersionTranslation(translation);
  char query[256];
  wsprintfA(query, "\\StringFileInfo\\%04lx%04lx\\%s", static_cast<unsigned long>(language[0]),
      static_cast<unsigned long>(language[1]), "FileVersion");
  void *value;
  UINT length;
  bool found = VerQueryValueA(data, query, &value, &length) != 0;
  if (found) {
    ScString text(static_cast<const char *>(value));
    *out = text;
  } else {
    out->Erase(0, kScStringToEnd);
  }
  return found;
}

// Query the localized copyright String after version loading, clearing it if the value is unavailable.
bool CtVersionInfo::GetModuleCopyright(ScString *out) {
  if (!data) {
    LoadVersionInfo();
    if (!data)
      return false;
  }
  const u16 *language = CtRequireVersionTranslation(translation);
  char query[256];
  wsprintfA(query, "\\StringFileInfo\\%04lx%04lx\\%s", static_cast<unsigned long>(language[0]),
      static_cast<unsigned long>(language[1]), "LegalCopyright");
  void *value;
  UINT length;
  bool found = VerQueryValueA(data, query, &value, &length) != 0;
  if (found) {
    ScString text(static_cast<const char *>(value));
    *out = text;
  } else {
    out->Erase(0, kScStringToEnd);
  }
  return found;
}

// Query the original "Debug Version" field after lazy loading, clearing the output on query failure.
bool CtVersionInfo::GetModuleDebugVersion(ScString *out) {
  if (!data) {
    LoadVersionInfo();
    if (!data)
      return false;
  }
  const u16 *language = CtRequireVersionTranslation(translation);
  char query[256];
  wsprintfA(query, "\\StringFileInfo\\%04lx%04lx\\%s", static_cast<unsigned long>(language[0]),
      static_cast<unsigned long>(language[1]), "Debug Version");
  void *value;
  UINT length;
  bool found = VerQueryValueA(data, query, &value, &length) != 0;
  if (found) {
    ScString text(static_cast<const char *>(value));
    *out = text;
  } else {
    out->Erase(0, kScStringToEnd);
  }
  return found;
}
#else
bool CtVersionInfo::GetModuleName(ScString *) {
  return false;
}

bool CtVersionInfo::GetModuleVersion(ScString *) {
  return false;
}

bool CtVersionInfo::GetModuleCopyright(ScString *) {
  return false;
}

bool CtVersionInfo::GetModuleDebugVersion(ScString *) {
  return false;
}
#endif

// Report physical memory in MiB with the source's upward rounding to a multiple of four.
bool CtVersionInfo::GetSystemMemory(u32 *total, u32 *available) {
#if defined(_WIN32)
  MEMORYSTATUS status;
  status.dwLength = sizeof(status);
  GlobalMemoryStatus(&status);
  *total = (((static_cast<u32>(status.dwTotalPhys) >> 20) - 1) | 3u) + 1;
  *available = (((static_cast<u32>(status.dwAvailPhys) >> 20) - 1) | 3u) + 1;
  return true;
#elif defined(__APPLE__)
  u64 total_bytes;
  usize size = sizeof(total_bytes);
  if (sysctlbyname("hw.memsize", &total_bytes, &size, nullptr, 0) != 0 || size != sizeof(total_bytes))
    return false;
  mach_port_t host = mach_host_self();
  vm_size_t page_size;
  vm_statistics64_data_t statistics;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  bool found = host_page_size(host, &page_size) == KERN_SUCCESS &&
      host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) == KERN_SUCCESS;
  mach_port_deallocate(mach_task_self(), host);
  if (!found)
    return false;
  u64 available_bytes = (static_cast<u64>(statistics.free_count) + statistics.inactive_count) * page_size;
  *total = CtHostMemoryMiB(total_bytes);
  *available = CtHostMemoryMiB(available_bytes);
  return true;
#elif defined(__linux__)
  FILE *file = fopen("/proc/meminfo", "r");
  if (!file)
    return false;
  char line[256];
  unsigned long long total_kib = 0, available_kib = 0;
  bool got_total = false, got_available = false;
  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, "MemTotal: %llu kB", &total_kib) == 1)
      got_total = true;
    else if (sscanf(line, "MemAvailable: %llu kB", &available_kib) == 1)
      got_available = true;
    if (got_total && got_available)
      break;
  }
  fclose(file);
  if (!got_total || !got_available)
    return false;
  *total = CtHostMemoryMiB(static_cast<u64>(total_kib) * 1024);
  *available = CtHostMemoryMiB(static_cast<u64>(available_kib) * 1024);
  return true;
#else
  return false;
#endif
}

#if defined(_WIN32)
// Lazily acquire version data, query the fixed file record each time, and extract one original 16-bit component.
i32 CtVersionInfo::GetModuleVersion(u32 index) {
  if (!data)
    LoadVersionInfo();
  void *root;
  UINT length;
  if (!data)
    return -1;
  CtRequireVersionTranslation(translation);
  if (!VerQueryValueA(data, "\\", &root, &length))
    return -1;
  const VS_FIXEDFILEINFO *fixed = static_cast<const VS_FIXEDFILEINFO *>(root);
  switch (index) {
  case kScVersionMajor:
    return fixed->dwFileVersionMS >> 16;
  case kScVersionMinor:
    return fixed->dwFileVersionMS & 0xffff;
  case kScVersionBuild:
    return fixed->dwFileVersionLS >> 16;
  case kScVersionRevision:
    return fixed->dwFileVersionLS & 0xffff;
  default:
    return -1;
  }
}
#else
i32 CtVersionInfo::GetModuleVersion(u32) {
  return -1;
}
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

// Format the original Windows family and major/minor bytes from the Win32 version value.
bool CtVersionInfo::GetSystemVersion(ScString *out) {
#if defined(_WIN32)
  u32 version = GetVersion();
  out->Sprintf(version < kScLegacyWindowsVersionFlag ? "Windows NT %u.%u" : "Windows %u.%u", version & 0xff,
      (version >> 8) & 0xff);
  return true;
#else
  char name[256], release[256];
  if (!CtHostSystemVersion(name, sizeof(name), release, sizeof(release)))
    return false;
  out->Sprintf("%s %s", name, release);
  return true;
#endif
}

// Return the major or minor Windows version byte, or minus one for any other selector.
i32 CtVersionInfo::GetSystemVersion(u32 index) {
#if defined(_WIN32)
  u32 version = GetVersion();
  if (index == 0)
    return version & 0xff;
  if (index == 1)
    return (version >> 8) & 0xff;
  return -1;
#else
  if (index > 1)
    return -1;
  char name[256], release[256];
  if (!CtHostSystemVersion(name, sizeof(name), release, sizeof(release)))
    return -1;
  char *end;
  unsigned long major = strtoul(release, &end, 10);
  if (end == release || major > INT32_MAX)
    return -1;
  if (!index)
    return static_cast<i32>(major);
  if (*end != '.')
    return -1;
  char *minor_start = end + 1;
  unsigned long minor = strtoul(minor_start, &end, 10);
  return end == minor_start || minor > INT32_MAX ? -1 : static_cast<i32>(minor);
#endif
}

// Return the original platform-family enum stored in city-save metadata.
// Retail CodeView type 0x2FDA defines Macintosh as five; Linux GetSystemType returns six.
// Other native host families use the enum's existing Unknown value.
CtVersionInfo::SystemType CtVersionInfo::GetSystemType() {
#if defined(_WIN32)
  return GetVersion() < kScLegacyWindowsVersionFlag ? kSystemTypeWindowsNt : kSystemTypeWindows95;
#elif defined(__APPLE__)
  return kSystemTypeMacintosh;
#elif defined(__linux__)
  return kSystemTypeLinux;
#else
  return kSystemTypeUnknown;
#endif
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Assign the native user name on success, otherwise the supplied owned String or the original question mark.
// Preserve the two separate pointer arguments, the fallback self-assignment case and the Boolean presence result.
bool CtVersionInfo::UserName(ScString *out, const ScString *fallback) {
#if defined(_WIN32)
  wchar_t wide[512];
  DWORD count = 512;
  if (GetUserNameW(wide, &count)) {
    char utf8[2048];
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (length) {
      out->FromChar(utf8, static_cast<u32>(length - 1));
      return true;
    }
  }
#elif defined(_WIN32)
  char buffer[512];
  DWORD count = 512;
  if (GetUserNameA(buffer, &count)) {
    ScString name(buffer);
    *out = name;
    return true;
  }
#else
  long recommended = sysconf(_SC_GETPW_R_SIZE_MAX);
  usize capacity = recommended > 0 ? static_cast<usize>(recommended) : 16384;
  for (;;) {
    char *buffer = new char[capacity];
    struct passwd info, *found = nullptr;
    int result = getpwuid_r(geteuid(), &info, buffer, capacity, &found);
    if (result == 0 && found && found->pw_name) {
      out->FromChar(found->pw_name);
      delete[] buffer;
      return true;
    }
    delete[] buffer;
    if (result != ERANGE || capacity > UINT32_MAX / 2)
      break;
    capacity *= 2;
  }
#endif
  if (fallback)
    *out = *fallback;
  else {
    ScString missing("?");
    *out = missing;
  }
  return false;
}

static WnWindowManager *CtWindowManagerCache;

// Retain the original borrowed window-manager cache after retiring its temporary lookup hold.
// Publish before Release and retry a missing output on the next request.
WnWindowManager *CtGetWindowManager() {
  if (!CtWindowManagerCache) {
    void *out = nullptr;
    CtGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &out);
    CtWindowManagerCache = static_cast<WnWindowManager *>(out);
    if (CtWindowManagerCache)
      CtWindowManagerCache->Release();
  }
  return CtWindowManagerCache;
}
