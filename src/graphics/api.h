#ifndef SC3_GRAPHICS_API_H
#define SC3_GRAPHICS_API_H

#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Packed ARGB pixels retain independent alpha and RGB channel masks for codecs, palettes and presentation.
enum GxArgbField : u32 {
  kGxArgbAlphaMask = 0xff000000,
  kGxArgbRedMask = 0xff0000,
  kGxArgbGreenMask = 0xff00,
  kGxArgbBlueMask = 255,
  kGxArgbRgbMask = 0xffffff,
};

class GxBuffer;
class GxPaletteView;
class ScStringView;
struct ScRect;

// Shared raster identity IID5DD. The installed Windows table keeps these operations in slots12 through68.
// The post-filter uses the Windows byte carrier and the coordinate setter exposes its reached effects only. The Windows
// source declarations remain unresolved; the Linux reference spells the filter input bool.
class GxRasterSource : public ScUnknown {
public:
  virtual bool Init(u32 id, u32 width, u32 height) = 0;
  virtual u32 GetId() const = 0;
  virtual bool SetId(u32 id) = 0;
  virtual bool SetPalette(GxPaletteView *source) = 0;
  virtual GxPaletteView *GetPalette() = 0;
  virtual i32 GetBaseWidth() const = 0;
  virtual i32 GetBaseHeight() const = 0;
  virtual bool SetBaseSize(u32 width, u32 height) = 0;
  virtual u8 SetPostFilter(u8 enabled) = 0;
  virtual void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) = 0;
  virtual u8 IsPostFilter() const = 0;
  virtual bool Draw(GxBuffer *target, const ScRect &rectangle, ScRect *clip) = 0;
  // Windows returns AL for these tags; the nominal Boolean/byte declarations remain unresolved.
  virtual u8 DebugInstanceTag(ScStringView *out) = 0;
  virtual u8 DebugClassTag(ScStringView *out) = 0;
  virtual u8 DebugTypeTag(ScStringView *out) = 0;
};

// Copyright (C) 2025, 2026 Nicholas Hayes

class GxBuffer;
class ScStringView;
class WnWindowManager;

// Declaration reference: sc3k-gzcom-dll/cIGZCanvas.h, Nicholas Hayes2025-2026 (LGPL-2.1-or-later).
// Actual cIGZCanvas interface. Retail CodeView proves GetArea returns ScRect by value.
class GxCanvas : public ScUnknown {
public:
  virtual bool Init(const ScRect &area) = 0;
  virtual bool Shutdown() = 0;
  virtual bool Uninitialize() = 0;
  virtual bool Restore() = 0;
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual ScRect GetArea() const = 0;
  virtual void ShowWindow() = 0;
  virtual void HideWindow() = 0;
  virtual void SetCaption(const ScStringView &caption) = 0;
  virtual void ProcessWindowMove() = 0;
  virtual void ForceFullSystemRedraw() = 0;
  virtual bool CompleteSwitchToWindowedMode() = 0;
  virtual bool CompleteSwitchToFullScreenMode() = 0;
  virtual void PauseDrawing() = 0;
  virtual void ResumeDrawing() = 0;
  virtual void SetMainBufferSurface(GxBuffer *buffer) = 0;
  virtual GxBuffer *GetMainBufferSurface() = 0;
  virtual void SetWindowManager(WnWindowManager *manager) = 0;
  virtual void SetFrameworkIsShuttingDown(bool stopping) = 0;
  virtual i32 LastCursorPositionX() const = 0;
  virtual i32 LastCursorPositionY() const = 0;
};

// Defined effects of GZGraphicD10012BD6. Its two DWORD outputs are genuine; callers discard private return residue.
void GxProbeCapabilities(u32 *level, u32 *platform);

// Original cIGZBuffer::eVRAMRequestType; the signed default value is distinct from every memory policy.
enum class GxVramRequest : i32 {
  Default = -1,
  SystemMemory = 0,
  VideoMemoryLowPriority = 1,
  VideoMemoryHighPriority = 2,
  VideoMemoryAbsolutePriority = 3,
  Local = 4
};

// Original eight-byte cGZBufferColorType value. Its user-provided constructor also preserves aggregate-return ABI.
class GxColorType {
public:
  enum class Color : i32 {
    Invalid = 0,
    Native = 1,
    Pal4 = 2,
    Pal8 = 3,
    RGB16 = 4,
    RGB555 = 5,
    RGB655 = 6,
    RGB565 = 7,
    RGB556 = 8,
    RGBA5551 = 9,
    ARGB1555 = 10,
    RGBA4444 = 11,
    ARGB4444 = 12,
    RGB24 = 13,
    ARGB32 = 14
  };
  GxColorType(Color value);
  u32 GetDepthFromColorType(Color value) const;
  Color color;
  u32 depth;
  // Retail SetColorType is declared, but its independent body remains unresolved.
};

// Store the original color code before deriving its depth. Both final color categories use32bits in this build.
inline GxColorType::GxColorType(Color value) : color(value) {
  depth = GetDepthFromColorType(value);
}

// Return the original depth mapping for the supplied category without changing the current color value.
// Retail SC3U0042932D and Linux84FD784 both retain the same two palette and two high-color cases.
inline u32 GxColorType::GetDepthFromColorType(Color value) const {
  i32 code = static_cast<i32>(value);
  return code == 2 ? 4 : code == 3 ? 8 : code > 3 && code <= 12 ? 16 : code > 12 && code <= 14 ? 32 : 0;
}

// Original four-byte cGZBufferZDepthType value; the constructor stores its signed enum without validation.
class GxDepthType {
public:
  enum class Depth : i32 { None = 0, Depth16 = 16, Depth32 = 32 };
  GxDepthType(Depth value);
  Depth depth;
};

// Store the source depth category directly. Retail SC3U0042935D supplies the out-of-line constructor body.
inline GxDepthType::GxDepthType(Depth value) : depth(value) {
}

class GxGraphics;
class GxBuffer;

// Original retail cIGZGraphicSystemHook interface, with raw byte results where the source primitive cannot
// distinguish Boolean from BYTE. Both shutdown and full-redraw notifications have genuine void returns.
class GxFrameListener : public ScUnknown {
public:
  virtual u8 GSInit(GxGraphics *graphics, GxBuffer *buffer) = 0;
  virtual void GSShutdown(GxGraphics *graphics) = 0;
  virtual void ForceFullSystemRedraw() = 0;
  virtual u8 Draw() = 0;
  virtual u8 ProcessWindowMove() = 0;
  virtual u8 CompleteSwitchToFullScreenMode() = 0;
  virtual u8 CompleteSwitchToWindowedMode() = 0;
};

class ScFramework;
class GxGraphics;
class WnWindowManager;
class ScMessageServer;

// Actual module framework getter1000104A, resolved through the lazily constructed DLL director.
ScFramework *GxFrameWork();
GxGraphics *GxGetGraphics();
WnWindowManager *GxGetWindowManager();
ScMessageServer *GxGetMessageServer();

// The installed application and Linux cGZSpanBlt interface establish the first span service operation.
class GxSpanBlt : public ScUnknown {
public:
  virtual u8 InitSpanBufferAlphaSystem(const GxColorType &format) = 0;
};

class GxDrawSurface;
enum class GxLockFlags : i32;

// The third original buffer identity starts at x86 offset108 and shares the general buffer's reference word.
// All thirty own virtual entries follow retail order through128; raw byte nominal returns remain qualified.
class GxPrimary : public ScUnknown {
public:
  virtual GxBuffer *AsIGZBuffer() = 0;
  virtual bool Init(u32 width, u32 height, bool flipping, GxColorType color_type, GxPaletteView *palette,
      GxDepthType depth_type, u32 color_frames, u32 depth_frames, u32 reserve_bytes) = 0;
  virtual bool Shutdown() = 0;
  virtual u8 Restore() = 0;
  virtual bool Uninitialize() = 0;
  virtual bool IsInitialized() = 0;
  virtual void GetAppWindow(ScRect *out) = 0;
  virtual bool ModifyAppWindow(ScRect *rectangle) = 0;
  // Retain raw byte results; the retail primitive leaves bool/BYTE nominal spelling unresolved for these entries.
  virtual u8 IsPageFlipping() = 0;
  virtual u8 Swap() = 0;
  virtual i32 DisableFlipping(i32 delta) = 0;
  virtual i32 DisableComposing(i32 delta) = 0;
  virtual GxBuffer *ActiveDrawBuffer() = 0;
  virtual u32 GetCurrentBackBufferIndex() = 0;
  virtual bool SetActiveBackBufferSurface(u32 index) = 0;
  virtual u32 Width() const = 0;
  virtual u32 Height() const = 0;
  virtual void RefreshVRAMPreferenceSetting() = 0;
  virtual bool IsReady() = 0;
  virtual u8 PaletteChangedNotification() = 0;
  virtual u8 LockFrontSurface(GxLockFlags requested_flags) = 0;
  virtual u8 UnlockFrontSurface(GxLockFlags requested_flags) = 0;
  virtual void AdjustClientRect(ScRect *rectangle) = 0;
  virtual void AdjustClientPoint(ScPoint *point) = 0;
  virtual void EnableBackgroundPicturePreview() = 0;
  virtual void DisableBackgroundPicturePreview() = 0;
  virtual void DumpBufferInformation() = 0;
  virtual GxDrawSurface *GetDisplaySurface() = 0;
  virtual GxDrawSurface *GetDrawSurface() = 0;
  virtual u8 FlipToGDISurface() = 0;
};

#endif
