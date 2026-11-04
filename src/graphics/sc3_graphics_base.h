#ifndef SC3_GRAPHICS_SC3_GRAPHICS_BASE_H
#define SC3_GRAPHICS_SC3_GRAPHICS_BASE_H

#include "graphics/image.h"
#include "graphics/module.h"
#include "graphics/sc3_graphics.h"

// Known fields and methods of the original abstract graphics base; concrete display/module dependencies stay abstract.
class GxGraphicsBase : public GxSystemServiceBase, public GxGraphics, public ScFrameworkHooks {
public:
  GxGraphicsBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool OnTick(u32 argument) override;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool OnInstall() override;
  bool AddHook(GxFrameListener *listener) override;
  bool RemoveHook(GxFrameListener *listener) override;
  u32 PauseDraw() override;
  u32 UnpauseDraw() override;
  bool IsScreenPaletted() override;
  virtual u32 VideoMemory(GxVideoMemoryType kind, bool available) override = 0;
  virtual u32 VideoMemoryFree(GxVideoMemoryType kind) override;
  virtual u32 VideoMemoryTotal(GxVideoMemoryType kind) override;
  virtual void TraceDriverInformation() override;
  virtual i32 GetTotalSurfaceMemory() override;
  virtual i32 GetTotalSystemSurfaceMemory() override;
  virtual i32 GetTotalVideoSurfaceMemory() override;
  virtual void IncrementTotalSystemSurfaceMemory(u32 bytes) override;
  virtual void DecrementTotalSystemSurfaceMemory(u32 bytes) override;
  virtual void IncrementTotalVideoSurfaceMemory(u32 bytes) override;
  virtual void DecrementTotalVideoSurfaceMemory(u32 bytes) override;
  virtual void MakeMemoryUsageString() override;
  GxPaletteView *SystemPalette() override;
  u8 IsForceSystemRAMSurfaces() override;
  void SetForceSystemRAMSurfaces(bool value) override;
  void SetCursorBuffer(GxBuffer *image, i32 hotspot_x, i32 hotspot_y) override;
  // Preserve the signed LONG cursor counter and original Boolean visibility parameter.
  i32 ShowCursor(bool visible) override;
  // Original void startup setters, with source Boolean policies and a const mode input.
  virtual void PreInitSetDesiredGameResolution(const GxMode &mode) override;
  virtual void PreInitSetHardware3DAccellerationUse(bool value) override;
  virtual void PreInitSetWindowedMode(bool value) override;
  virtual void PreInitSetDesiredVRAMForTextures(u32 bytes) override;
  virtual void PreInitSetDesiredVideoDriverIdentifier(u8 *preference) override;
  // Original void color conversions; the byte and float overloads retain their distinct outputs.
  virtual void RGBToHSB(u8 red, u8 green, u8 blue, u8 *hue, u8 *saturation, u8 *value) override;
  virtual void RGBToHSB(u8 red, u8 green, u8 blue, f32 *hue, f32 *saturation, f32 *value) override;
  virtual void HSBToRGB(u8 hue, u8 saturation, u8 value, u8 *red, u8 *green, u8 *blue) override;
  virtual void HSBToRGB(f32 hue, f32 saturation, f32 value, u8 *red, u8 *green, u8 *blue) override;
  virtual void RGBToHLS(u8 red, u8 green, u8 blue, u8 *hue, u8 *lightness, u8 *saturation) override;
  virtual void RGBToHLS(u8 red, u8 green, u8 blue, f32 *hue, f32 *lightness, f32 *saturation) override;
  virtual void HLSToRGB(u8 hue, u8 lightness, u8 saturation, u8 *red, u8 *green, u8 *blue) override;
  virtual void HLSToRGB(f32 hue, f32 lightness, f32 saturation, u8 *red, u8 *green, u8 *blue) override;
  virtual void RGBToCYM(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta) override;
  virtual void CYMToRGB(u8 cyan, u8 yellow, u8 magenta, u8 *red, u8 *green, u8 *blue) override;
  virtual void RGBToCYMK(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta, u8 *black) override;

  virtual void CYMKToRGB(u8 cyan, u8 yellow, u8 magenta, u8 black, u8 *red, u8 *green, u8 *blue) override;

protected:
  // Native effect spellings for private traversal bodies; literal return declarations remain uncovered.
  void HookGSInit(GxBuffer *buffer);
  void HookForceFullSystemRedraw();
  void HookDraw();
  void HookProcessWindowMove();
  void HookCompleteSwitchToFullScreenMode();
  void HookCompleteSwitchToWindowedMode();
  void HookRelease();
  GxFrameList listeners;
  u32 phase = 1;
  u8 no_hardware = 0;
  u32 suppress_overlay = 0;
  GxBuffer *cursor = nullptr;
  u16 cursor_width = 0, cursor_height = 0, hotspot_x = 0, hotspot_y = 0;
  u32 pause = 0;
};

// Native numeric phase of100176E5 retaining the complete DWORD result; private nominal return signedness is unresolved.
u32 GxHlsChannel(f64 low, f64 high, f64 hue);

#endif
