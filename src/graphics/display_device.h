#ifndef SC3_GRAPHICS_DISPLAY_DEVICE_H
#define SC3_GRAPHICS_DISPLAY_DEVICE_H

#include "common/thread.h"
#include "graphics/module.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_graphics_base.h"

#ifdef RGB
#undef RGB
#endif

class ScStringView;

// Single owned canvas member: its destruction releases the interface at this field's original place in member order.
struct GxCanvasRef {
  GxCanvas *value = nullptr;
  ~GxCanvasRef();
};

// Recovered concrete graphics receiver, including its owned adapter list and selected adapter record.
// Its concrete startup, shutdown and drawing paths retain the original ownership and callback ordering.
class GxDisplay : public GxGraphicsBase, public GxDeviceManager {
public:
  GxDisplay();
  ~GxDisplay() override;

  // Avoid native paint through a missing or retired presentation target; paused or suppressed drawing needs no target.
  bool NativePaintAvailable() const {
    if (pause || primary.present_depth)
      return true;
    GxDrawSurface *target = primary.draw_target;
    if (!target || (target != primary.front && target != primary.backend))
      return false;
    return target->ready && primary.backend;
  }

  bool CreateBuffer(GxBuffer **out) override;
  virtual bool CreateBuffer(u32 iid, void **out) override;
  GxColorType GetScreenColorType() override;
  GxCanvas *GetCanvas() const override;
  virtual bool GetCanvas(u32 iid, void **out) override;
  virtual ScRect CanvasRect() override;
  // The installed method returns AL1; its private bool/BYTE spelling remains unresolved.
  virtual u8 GetVideoDriverVersionInformation(u32 *major, u32 *minor) override;
  // Original void output operation; the retail interface preserves the timing aggregate declaration.
  virtual void GetScreenTiming(GxTiming *out) override;
  virtual void GetRefreshRate(f32 *out) override;
  virtual void GetRefreshRateAvg(f32 *out) override;
  virtual void GetRefreshRateAvgLong(f32 *out) override;
  GxPrimary *ScreenBuffer() override;
  u8 IsFullScreenMode() override;
  bool Draw() override;
  u32 PauseDraw() override;
  bool DoesSystemHaveCursorSupport() override;
  u16 ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) override;
  u8 ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) override;
  u8 Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) override;
  virtual GxPackFunction GetRGBTo16ConverterPtr() const override;
  virtual GxUnpackFunction Get16ToRGBConverterPtr() const override;
  u8 Convert16BitValueFromFormatToFormat(u16 *pixel, GxColorType::Color source, GxColorType::Color target) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool AbortiveQuit() override;
  // Reached mode-switch effects; mixed private register returns do not establish a nominal source declaration.
  virtual void SetFullscreen(u8 value);
  const GxCapabilities *GetCapabilities() override;
  u8 IsSoftware() override;
  GxGraphics *AsIGZGraphicSystem() override;
  bool IsHardware2DAccellerated() override;
  bool IsHardware3DAccellerated() override;
  SDL_Renderer *GetDevice() override;
  void ForceFullSystemRedraw() override;
  SDL_Window *SetWindowHandle(SDL_Window *window) override;
  SDL_Window *GetWindowHandle() override;
  bool CreateDevice();
  bool CloseDevice();
  // Reached manager36 effects; callers discard its incidental result, so the private return declaration stays unresolved.
  void SetCooperativeLevel(u8 exclusive) override;
  void SetCooperativeLevel() override;
  u8 RefreshSoftware();
  i32 IsDefaultAdapter() override;
  GxAdapters *GetAdapters() override;
  GxAdapter *GetSelectedAdapter() override;
  virtual u8 GetVideoDriverInfoCurrent(ScStringView *driver, ScStringView *description) override;
  virtual u32 GetVideoDriverInfoCount() override;
  GxAdapter *GetAdapter(u32 index);
  virtual u8 GetVideoDriverInfo(ScStringView *driver, ScStringView *description, u32 index) override;
  bool DiscoverAdapters();
  bool ClearAdapters();
  bool EnumerateModes();
  bool SupportsMode(const GxMode *mode, const u8 *guid);
  GxMode GetScreenMetrics() override;
  u32 VideoMemory(GxVideoMemoryType kind, bool available) override;
  virtual i32 WaitForVerticalBlank();
  virtual u32 MessageBoxA(const ScStringView &text, const ScStringView &caption) override;
  GxMode GetGameMetrics() override;

protected:
  ScFramework *framework;
  u8 fullscreen = 0;
  GxMode selected_mode, desktop_mode;
  GxCanvasRef canvas;
  ScRect canvas_area = {0, 0, (i32)GxRequestedMode.width, (i32)GxRequestedMode.height};
  u32 capability_level = 0, capability_platform = 0;
  u8 hardware_cursor = 0;
  ScLock draw_lock;
  GxAdapters adapters;
  GxAdapter selected_adapter;
  GxPrimaryBuffer primary;
  u8 software = 0;
  GxTimer timer1{GxTimeUnit::Milliseconds, 0}, timer2{GxTimeUnit::Milliseconds, 0};
  GxTiming timing = {};
  ScArray<f32> values10, values100, values1000;
  u8 flag652 = 0, flag653 = 0, flag654 = 0;
  u32 word656 = 0, word660 = 0, word664 = 0;
  SDL_Window *window_handle = nullptr;
};


// CPU presentation driver for the recovered primary object. It owns actual pixels rather than a binding/service descriptor.
class GxSoftwarePrimary final : public GxPrimaryBuffer {
public:
  GxSoftwarePrimary(GxGraphics *graphics, u32 native_format);
  ~GxSoftwarePrimary() override;
  GxDrawSurface *GetDisplaySurface() override;
  u8 Swap() override;

protected:
  u8 CreateScreenSurfaces() override;
  u8 DestroyScreenSurfaces() override;
};

#endif
