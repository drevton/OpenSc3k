#include "common/api.h"
#include "common/value.h"
#include "window/wn_resource_provider.h"

static GxGraphics *WnGraphics;
static FtFontSystem *WnFonts;
static AuSoundDevice *WnSounds;
static WnResources *WnUiResources;
static WnWindowManager *WnWindows;
static ScMessageServer *WnMessages;
static ScLanguageManager *WnLanguage;

// Cache the actual borrowed graphics interface before releasing its temporary acquisition reference.
GxGraphics *WnGetGraphics() {
  if (!WnGraphics) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &slot);
    GxGraphics *graphics = static_cast<GxGraphics *>(slot);
    WnGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return WnGraphics;
}

// Prefer the application font service, then fall back to the framework font service. Publish before temporary releases.
FtFontSystem *WnGetFontManager() {
  if (!WnFonts) {
    void *first = nullptr;
    WnGetFramework()->GetSystemService(kFtAppFontServiceId, IID_FtFontSystem, &first);
    FtFontSystem *manager = static_cast<FtFontSystem *>(first);
    if (manager)
      WnFonts = manager;
    else {
      void *second = nullptr;
      WnGetFramework()->GetSystemService(kFtFontSystemServiceId, IID_FtFontSystem, &second);
      FtFontSystem *fallback = static_cast<FtFontSystem *>(second);
      WnFonts = fallback;
      if (fallback)
        fallback->Release();
    }
    if (manager)
      manager->Release();
  }
  return WnFonts;
}

// Cache the actual borrowed sound-device interface; a missing service remains null and is retried on the next call.
AuSoundDevice *WnGetSoundDevice() {
  if (!WnSounds) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kAuSoundDeviceServiceId, IID_AuSoundDevice, &slot);
    AuSoundDevice *device = static_cast<AuSoundDevice *>(slot);
    WnSounds = device;
    if (device)
      device->Release();
  }
  return WnSounds;
}

// Publish the borrowed UI resource interface before releasing its temporary framework lookup reference.
WnResources *WnGetUiResources() {
  if (!WnUiResources) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &slot);
    WnResources *resources = static_cast<WnResources *>(slot);
    WnUiResources = resources;
    if (resources)
      resources->Release();
  }
  return WnUiResources;
}

// Cache the actual window-manager interface without keeping an additional reference beyond the framework's ownership.
WnWindowManager *WnGetWindowManager() {
  if (!WnWindows) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &slot);
    WnWindowManager *windows = static_cast<WnWindowManager *>(slot);
    WnWindows = windows;
    if (windows)
      windows->Release();
  }
  return WnWindows;
}

// Resolve the genuine cIGZMessageServer used by hints and modal broadcasts; a missing result is retried later.
ScMessageServer *WnGetMessageServer() {
  if (!WnMessages) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &slot);
    ScMessageServer *messages = static_cast<ScMessageServer *>(slot);
    WnMessages = messages;
    if (messages)
      messages->Release();
  }
  return WnMessages;
}

// Cache the borrowed language service before releasing its temporary framework acquisition reference; misses are retried.
ScLanguageManager *WnGetLanguageManager() {
  if (!WnLanguage) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &slot);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(slot);
    WnLanguage = manager;
    if (manager)
      manager->Release();
  }
  return WnLanguage;
}
