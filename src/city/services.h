#ifndef SC3_CITY_SERVICES_H
#define SC3_CITY_SERVICES_H

#include "common/sc3.h"
#include "common/sc3_string.h"
#include "resources/resource.h"

// Original single-byte cursor scope borrows the live window and cursor services independently at each end.
class CtCursorScope {
public:
  explicit CtCursorScope(u32 identifier);
  ~CtCursorScope();
  bool pushed;
};

class ScStringView;

// Original stack payload carried by synchronous message82684130; its first word is the receiver's mutable result.
struct CtDialogRequest {
  u32 identifier, group, instance;
  ScStringView *text;
  u32 reserved[4];
};

class ScStringView;

// The city load/save scope owns only two unsigned counters; application services remain genuine external interfaces.
class CtProgress {
public:
  CtProgress(u32 count, ScStringView *text);
  ~CtProgress();
  void Step();
  u32 total, current;
};

// Original five-word localized text handle; the retained String interface and actual resource key are separate fields.
class CtTextResource {
public:
  CtTextResource(u32 instance, u32 group);
  virtual ~CtTextResource();
  ScString GetText();
  bool CopyTo(ScStringView *out);
  ScStringView *text;
  RsKey key;
};

u32 SC3_STDCALL CtExpandTokens(ScString *text);
bool SC3_STDCALL CtSubstituteToken(ScString *text, u32 *position);
// Original leaf1000F3F8 resolves values through the real city, preferences, neighbor and localized resource contracts.
bool SC3_STDCALL CtTryToken(ScString *text, const ScString &token, u32 *position);

class ScString;

// Original four-word executable version receiver, retaining the shared cRZVersion method contracts.
// Version bytes are owned; the translation entry borrows storage from that allocation.
class CtVersionInfo {
public:
  // Original cRZVersion::SystemType values; the later Linux implementation adds value six.
  enum SystemType : i32 {
    kSystemTypeUnknown = 0,
    kSystemTypeDos = 1,
    kSystemTypeWindows16 = 2,
    kSystemTypeWindows95 = 3,
    kSystemTypeWindowsNt = 4,
    kSystemTypeMacintosh = 5,
    kSystemTypeLinux = 6,
  };
  CtVersionInfo();
  virtual ~CtVersionInfo();
  bool GetModuleActualFileName(ScString *out);
  bool GetModuleName(ScString *out);
  bool GetModuleVersion(ScString *out);
  i32 GetModuleVersion(u32 index);
  bool GetModuleCopyright(ScString *out);
  bool GetModuleDebugVersion(ScString *out);
  i32 GetSystemVersion(u32 index);
  bool GetSystemVersion(ScString *out);
  bool GetSystemMemory(u32 *total, u32 *available);
  SystemType GetSystemType();
  bool UserName(ScString *out, const ScString *fallback);

protected:
  void LoadVersionInfo();
  uptr module;
  u8 *data; // Owned even when the platform read fails; released by the destructor or a failed translation query.
  void *translation; // Borrowed from data after a successful translation query; never independently released.
};

#endif
