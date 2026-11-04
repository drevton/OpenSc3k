#ifndef SC3_GRAPHICS_PRIMARY_BUFFER_H
#define SC3_GRAPHICS_PRIMARY_BUFFER_H


#include "common/api.h"
#include "common/sc3_array.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_general_buffer.h"
#include "graphics/sc3_graphics.h"

#ifdef RGB
#undef RGB
#endif

// The legacy wheel message starts unregistered and retains the same sentinel when native registration fails.
enum GxWheelMessageIdentity : u32 {
  kGxUnregisteredWheelMessage = 0xffffffff,
};

// Recovered DirectDraw surface core at188DD/F7D4, distinct from the native CPU GxSurface boundary.
// Derived allocations use the actual driver classes; restoration dispatches to the owning buffer's recovered callback path.
class GxDrawSurface {
public:
  GxDrawSurface();
  virtual ~GxDrawSurface();
  virtual u8 Close();
  virtual u8 Restore();
  virtual u8 Lock();
  virtual u8 Unlock();
  virtual u8 SetColor(const GxBltFlags *color);
  virtual u8 FillRect(u32 pixel, ScRect *rectangle);
  // MSVC x86 reverses each overload group, placing the point entries before the coordinate entries in the table.
  virtual u32 ReadPixel(i32 x, i32 y);
  virtual u32 ReadPixel(const ScPoint *point);
  // Reached WORD stores only; private Windows return, coordinate signedness and pixel argument width remain unresolved.
  // Linux SDL counterparts take unsigned DWORD arguments but use a different virtual order; they do not prove PC types.
  virtual void WritePixel(i32 x, i32 y, u16 pixel);
  virtual void WritePixel(const ScPoint *point, u16 pixel);
  virtual u8 Blit(GxDrawSurface *source, const ScRect *source_rect, const ScRect *destination) = 0;
  virtual u8 AddAttached(GxDrawSurface *child);
  virtual u8 RemoveAttached(GxDrawSurface *child);
  virtual u8 Flip() = 0;
  virtual u8 FlipTo(GxDrawSurface *target) = 0;

public:
  // DirectDraw owns these records only in the original Windows backend.
  // SDL surfaces use their actual dimensions and drawing policy, without native COM records.
  u32 surface_width = 0, surface_height = 0;
  u32 mirror_flags = 0, source_color_key = 0, depth_operation = 0;
  u32 blit_flags = 0;
  bool ready = false, restoring = false, video_memory = false;
  u32 locks = 0;
  GxBuffer *owner = nullptr;
  u32 allocation_policy = 0;
  u8 *data = nullptr;
  u32 pitch = 0, bits = 0;
};

class GxGraphics;

// Original twelve-byte ring record: ordinal, color surface and optional depth surface.
struct GxSurfaceFrame {
  u32 index;
  GxDrawDeviceSurface *buffer;
  GxDrawDepthSurface *depth;
};

// Recovered primary-buffer state, owned surface ring and original virtual driver creation/release methods.
class GxPrimaryBuffer : public GxGeneralBuffer, public GxPrimary {
  friend class GxDisplay;

public:
  GxPrimaryBuffer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  GxBuffer *AsIGZBuffer() override;
  GxBuffer *ActiveDrawBuffer() override;
  bool IsReady() override;
  u8 PaletteChangedNotification() override;
  void SetSurfaceRestoreType(GxRestoreType type) override;
  u8 LockFrontSurface(GxLockFlags requested_flags) override;
  u8 UnlockFrontSurface(GxLockFlags requested_flags) override;
  u32 Width() const override;
  u32 Height() const override;
  u8 Swap() override;
  GxDrawSurface *GetDisplaySurface() override;
  GxDrawSurface *GetDrawSurface() override;
  using GxGeneralBuffer::Init;
  bool Init(u32 width, u32 height, bool flipping, GxColorType color_type, GxPaletteView *palette,
      GxDepthType depth_type, u32 color_frames, u32 depth_frames, u32 reserve_bytes) override;
  bool Shutdown() override;
  u8 Restore() override;
  bool Uninitialize() override;
  bool IsInitialized() override;
  void RefreshVRAMPreferenceSetting() override;
  // Windows slot492 applies the reached effects; its nominal return remains unresolved.
  virtual void SetGraphicSystem(GxGraphics *graphics);
  void GetAppWindow(ScRect *out) override;
  bool ModifyAppWindow(ScRect *rectangle) override;
  u8 IsPageFlipping() override;
  i32 DisableFlipping(i32 delta) override;
  i32 DisableComposing(i32 delta) override;
  void AdjustClientRect(ScRect *rectangle) override;
  void AdjustClientPoint(ScPoint *point) override;
  u32 GetCurrentBackBufferIndex() override;
  bool SetActiveBackBufferSurface(u32 index) override;
  u8 FlipToGDISurface() override;
  void EnableBackgroundPicturePreview() override;
  void DisableBackgroundPicturePreview() override;
  void DumpBufferInformation() override;

protected:
  virtual u8 CreateScreenSurfaces();
  virtual u8 CreateComplexSurface();
  virtual bool InitializeComplexSurface();
  virtual u8 CreateDepthSurface();
  virtual bool InitializePalette();
  virtual bool CreateSimpleSurfaces();
  virtual u8 InitializeSimpleSurfaces();
  virtual u8 DestroyScreenSurfaces();
  // Linux uses a mutable color-type reference, VRAM enum and bool; Windows raw argument spelling remains provisional.
  virtual u8 InitSimpleBackBufferColorSurface(
      GxDrawDeviceSurface *driver, u32 width, u32 height, i32 *requested_format, u32 allocation_mode, u8 flag);
  ScRect viewport;
  bool active = false;
  u8 flipping = 0;
  u32 flip_depth = 0, present_depth = 0;
  bool hardware_allowed = true;
  u8 requested_flipping = 0;
  u32 option11 = 0, option9 = 0, option10 = 0, word156 = UINT32_MAX;
  GxDrawSurface *front = nullptr;
  GxGraphics *graphics = nullptr;
  void *manager = nullptr;
  GxDrawSurface *draw_target = nullptr;
  ScUnknown *window = nullptr;
  u32 frame_index = 0;
  ScArray<GxSurfaceFrame> frames;
};

struct ScMessage;
struct SDL_Window;

// Actual secondary Windows-canvas identity E46397DB; the borrowed HWND is original virtual12.
class GxWindowHandle : public ScUnknown {
public:
  virtual SDL_Window *GetWindowHandle() = 0;
};

// Recovered GZGraphic canvas fields and methods. Windows handles are actual platform objects, never foreign tokens.
class GxWindow : public GxCanvas, public GxWindowHandle {
public:
  GxWindow();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(const ScRect &area) override;
  bool Shutdown() override;
  bool Uninitialize() override;
  bool Restore() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  ScRect GetArea() const override;
  void ShowWindow() override;
  void HideWindow() override;
  void SetCaption(const ScStringView &caption) override;
  void ProcessWindowMove() override;
  void ForceFullSystemRedraw() override;
  bool CompleteSwitchToWindowedMode() override;
  bool CompleteSwitchToFullScreenMode() override;
  void PauseDrawing() override;
  void ResumeDrawing() override;
  void SetMainBufferSurface(GxBuffer *buffer) override;
  GxBuffer *GetMainBufferSurface() override;
  void SetWindowManager(WnWindowManager *manager) override;
  void SetFrameworkIsShuttingDown(bool stopping) override;
  i32 LastCursorPositionX() const override;
  i32 LastCursorPositionY() const override;
  SDL_Window *GetWindowHandle() override;
  virtual bool Plot();
  virtual void Place(u32 flags);
  virtual i32 GetW();
  virtual i32 GetH();
  virtual i32 GetL();
  virtual i32 GetT();
  virtual i32 GetR();
  virtual i32 GetB();
  virtual ~GxWindow();
  bool CreateNativeWindow(u8 create);

protected:
  bool initialized = false;
  u32 style_flags = 0;
  char *caption = nullptr;
  bool paused = false;
  bool stopping = false;
  GxBuffer *buffer = nullptr;
  WnWindowManager *manager = nullptr;
  ScRect area = {0, 0, 0, 0};
  u32 references = 0;
};

extern GxWindow *GxWindowCurrent;

// Original F104 secondary driver owns depth-buffer storage and borrows its attachment target at source252.
// The shared restore operation is implemented; callback-backed owner restoration remain an explicit dependency.
class GxDrawDepthSurface : public GxDrawSurface {
public:
  u8 FillRect(u32 pixel, ScRect *rectangle) override = 0;
  virtual u8 Init(u32 width, u32 height, i32 *depth, u32 mode) = 0;
  virtual u8 Detach() = 0;
  virtual GxDrawSurface *AttachTo(GxDrawSurface *target) = 0;
};

// Original F0AC device-surface class adds a clipper after the recovered DirectDraw core at source252.
// Palette attachment still requires an unproven concrete Windows palette producer.
class GxDrawDeviceSurface : public GxDrawSurface {
public:
  GxDrawDeviceSurface();
  ~GxDrawDeviceSurface() override;
  u8 Close() override;
  u8 SetColor(const GxBltFlags *color) override;
  u8 FillRect(u32 pixel, ScRect *rectangle) override = 0;
  virtual u8 Init(u32 width, u32 height, i32 *format, u32 mode, u8 flag) = 0;
  virtual u8 SetPalette(GxPaletteView *palette) = 0;
  virtual u8 ClearPalette() = 0;
  virtual u8 SetColorKey(u32 pixel);
  virtual u8 GetFormat(i32 *format) = 0;

protected:
};

// Native platform selection; ordinary game buffers use DirectDraw unless CPU startup explicitly selects owned memory.
void GxUseCpuSurfaces(bool enabled);
bool GxUsesCpuSurfaces();
GxDrawDeviceSurface *GxCreateSurface();
extern u32 GxOutputFormat;

#endif
