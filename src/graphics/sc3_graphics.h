#ifndef SC3_GRAPHICS_SC3_GRAPHICS_H
#define SC3_GRAPHICS_SC3_GRAPHICS_H

#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"


// Original cGZDisplayMetrics record, including all four channel masks. Only its first three words select a mode.
struct GxMode {
  u32 width, height, depth, alpha_mask, red_mask, green_mask, blue_mask;
};

// The mode vector is ordinary native storage; it carries no initialized-byte or foreign-address metadata.
struct GxModes {
  ScArray<GxMode> rows;
  void Append(const GxMode &mode);
  void Assign(const GxModes &source);
  bool Contains(const GxMode &mode) const;
};

// Native representation of the original adapter's two strings, version, GUID, device reference and mode vector.
struct GxAdapter {
  GxAdapter();
  GxAdapter &operator=(const GxAdapter &source);
  ScString name, description;
  u32 version_low = 0, version_high = 0;
  u8 guid[16] = {};
  u32 display_id = 0;
  GxModes modes;
};

struct GxAdapterLink {
  GxAdapterLink *next, *previous;
};

struct GxAdapterNode : GxAdapterLink {
  GxAdapter adapter;
};

// This storage replaces the source list implementation, preserving its live release pass before node destruction.
class GxAdapters {
public:
  GxAdapters();
  ~GxAdapters();
  GxAdapter *Append();
  GxAdapter *Find(const u8 *guid);
  bool Supports(const GxMode *mode, const u8 *guid);
  void Clear();
  bool Discover();
  bool CreateDevices();
  bool EnumerateModes();
  GxAdapterLink head;
};

extern GxMode GxRequestedMode;
extern u8 GxStartWindowed, GxFlipping;
extern u8 GxAdapterPreference[32];
extern u32 GxPrimaryColorFrames, GxPrimaryDepthFrames, GxPrimaryReserveBytes;
void GxResetRequestedMode();
extern u8 GxDefaultAdapter[16];

#ifdef __cplusplus
class GxCanvas;
class GxPrimary;
class ScStringView;

// Original signed cIGZGraphicSystem::eVideoMemoryType values.
enum class GxVideoMemoryType : i32 { Texture = 0, ZBuffer = 1, OffscreenPlain = 2 };

// Original twelve-byte display timing value, with independently writable float outputs.
struct GxTiming {
  f32 swap, window_manager, other;
};

// Retail and installed graphics own-method order through272. Byte nominal types retain their explicit qualifications.
class GxGraphics : public ScUnknown {
public:
  virtual bool AddHook(GxFrameListener *hook) = 0;
  virtual bool RemoveHook(GxFrameListener *hook) = 0;
  virtual bool CreateBuffer(u32 iid, void **out) = 0;
  virtual bool CreateBuffer(GxBuffer **out) = 0;
  virtual u8 IsForceSystemRAMSurfaces() = 0;
  virtual void SetForceSystemRAMSurfaces(bool force) = 0;
  virtual GxPrimary *ScreenBuffer() = 0;
  virtual GxColorType GetScreenColorType() = 0;
  virtual GxMode GetScreenMetrics() = 0;
  virtual GxMode GetGameMetrics() = 0;
  virtual u8 IsFullScreenMode() = 0;
  virtual bool IsScreenPaletted() = 0;
  virtual bool GetCanvas(u32 iid, void **out) = 0;
  virtual GxCanvas *GetCanvas() const = 0;
  virtual ScRect CanvasRect() = 0;
  virtual bool Draw() = 0;
  virtual u32 PauseDraw() = 0;
  virtual u32 UnpauseDraw() = 0;
  virtual u32 MessageBoxA(const ScStringView &text, const ScStringView &caption) = 0;
  virtual u32 VideoMemoryFree(GxVideoMemoryType type) = 0;
  virtual u32 VideoMemoryTotal(GxVideoMemoryType type) = 0;
  virtual u32 VideoMemory(GxVideoMemoryType type, bool available) = 0;
  virtual void TraceDriverInformation() = 0;
  virtual u8 GetVideoDriverVersionInformation(u32 *major, u32 *minor) = 0;
  virtual u32 GetVideoDriverInfoCount() = 0;
  // Installed producers write genuine string interfaces; the retail opaque character-reference spelling is qualified.
  virtual u8 GetVideoDriverInfo(ScStringView *driver, ScStringView *description, u32 index) = 0;
  virtual u8 GetVideoDriverInfoCurrent(ScStringView *driver, ScStringView *description) = 0;
  virtual i32 ShowCursor(bool visible) = 0;
  virtual void SetCursorBuffer(GxBuffer *image, i32 hotspot_x, i32 hotspot_y) = 0;
  virtual bool DoesSystemHaveCursorSupport() = 0;
  virtual u8 ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) = 0;
  virtual u16 ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) = 0;
  virtual u8 Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) = 0;
  virtual u8 Convert16BitValueFromFormatToFormat(
      u16 *pixel, GxColorType::Color source_format, GxColorType::Color target_format) = 0;
  virtual GxPackFunction GetRGBTo16ConverterPtr() const = 0;
  virtual GxUnpackFunction Get16ToRGBConverterPtr() const = 0;
  virtual void RGBToHSB(u8 red, u8 green, u8 blue, u8 *hue, u8 *saturation, u8 *value) = 0;
  virtual void RGBToHSB(u8 red, u8 green, u8 blue, f32 *hue, f32 *saturation, f32 *value) = 0;
  virtual void HSBToRGB(u8 hue, u8 saturation, u8 value, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void HSBToRGB(f32 hue, f32 saturation, f32 value, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void RGBToHLS(u8 red, u8 green, u8 blue, u8 *hue, u8 *lightness, u8 *saturation) = 0;
  virtual void RGBToHLS(u8 red, u8 green, u8 blue, f32 *hue, f32 *lightness, f32 *saturation) = 0;
  virtual void HLSToRGB(u8 hue, u8 lightness, u8 saturation, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void HLSToRGB(f32 hue, f32 lightness, f32 saturation, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void RGBToCYM(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta) = 0;
  virtual void CYMToRGB(u8 cyan, u8 yellow, u8 magenta, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void RGBToCYMK(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta, u8 *black) = 0;
  virtual void CYMKToRGB(u8 cyan, u8 yellow, u8 magenta, u8 black, u8 *red, u8 *green, u8 *blue) = 0;
  virtual void GetScreenTiming(GxTiming *out) = 0;
  virtual void GetRefreshRate(f32 *out) = 0;
  virtual void GetRefreshRateAvg(f32 *out) = 0;
  virtual void GetRefreshRateAvgLong(f32 *out) = 0;
  virtual void PreInitSetDesiredGameResolution(const GxMode &mode) = 0;
  virtual void PreInitSetHardware3DAccellerationUse(bool value) = 0;
  virtual void PreInitSetWindowedMode(bool value) = 0;
  virtual void PreInitSetDesiredVRAMForTextures(u32 bytes) = 0;
  virtual void PreInitSetDesiredVideoDriverIdentifier(u8 *identifier) = 0;
  virtual i32 GetTotalSurfaceMemory() = 0;
  virtual i32 GetTotalSystemSurfaceMemory() = 0;
  virtual i32 GetTotalVideoSurfaceMemory() = 0;
  virtual void IncrementTotalSystemSurfaceMemory(u32 bytes) = 0;
  virtual void DecrementTotalSystemSurfaceMemory(u32 bytes) = 0;
  virtual void IncrementTotalVideoSurfaceMemory(u32 bytes) = 0;
  virtual void DecrementTotalVideoSurfaceMemory(u32 bytes) = 0;
  virtual void MakeMemoryUsageString() = 0;
  virtual GxPaletteView *SystemPalette() = 0;
};

// The module caches a borrowed actual graphics interface; it adds no synthetic ownership.
extern GxGraphics *GxGraphicsCurrent;

// SDL tracks only the capability flags consumed by the game; DirectDraw retains its native record.
struct GxCapabilities {
  u32 capabilities;
};

struct SDL_Renderer;
struct SDL_Window;
class GxAdapters;
struct GxAdapter;

// Actual Windows secondary manager table030980FD, own slots12..68. Platform-only names remain provisional where
// no Windows source declaration survives; effect-only cooperative/redraw returns do not assert a nominal return type.
class GxDeviceManager : public ScUnknown {
public:
  virtual GxGraphics *AsIGZGraphicSystem() = 0;
  virtual bool IsHardware2DAccellerated() = 0;
  virtual bool IsHardware3DAccellerated() = 0;
  virtual bool DoesSystemHaveCursorSupport() = 0;
  virtual SDL_Renderer *GetDevice() = 0;
  virtual const GxCapabilities *GetCapabilities() = 0;
  virtual void SetCooperativeLevel() = 0;
  virtual void SetCooperativeLevel(u8 exclusive) = 0;
  virtual i32 IsDefaultAdapter() = 0;
  virtual u8 IsSoftware() = 0;
  virtual void ForceFullSystemRedraw() = 0;
  virtual SDL_Window *SetWindowHandle(SDL_Window *window) = 0;
  virtual SDL_Window *GetWindowHandle() = 0;
  virtual GxAdapters *GetAdapters() = 0;
  virtual GxAdapter *GetSelectedAdapter() = 0;
};

GxDeviceManager *GxGetDeviceManager();

using GxColorEffect = u32 (*)(u16 *destination, u16 source, f32 alpha);
extern u8 GxDeviceInitialized, GxNoHardware, GxDisableColorEffects, GxNoSyslock, GxForceSoftware;
extern GxColorEffect GxApplyColor;
u32 GxColor555(u16 *destination, u16 source, f32 alpha);
u32 GxColor655(u16 *destination, u16 source, f32 alpha);
u32 GxColor565(u16 *destination, u16 source, f32 alpha);
u32 GxColor556(u16 *destination, u16 source, f32 alpha);

// Native effect spelling for10014078: all reached game callers discard its mixed incidental register result.
// The literal private declaration remains uncovered; this does not introduce a fabricated game return contract.
void GxInitializeDeviceGlobals(GxGraphics *graphics);

// Platform CPU rendering backend. It implements real buffer/graphics interfaces without emulated addresses or validity masks.
GxGraphics *GxCreateSoftwareGraphics(u32 native_format);
#endif

#endif
