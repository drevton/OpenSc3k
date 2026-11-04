#ifndef SC3_UI_TERRAIN_EDITOR_H
#define SC3_UI_TERRAIN_EDITOR_H

#include "ui/controls.h"
#include "ui/generic_dialogs.h"

// Terrain-editor controls follow their actual acceptance, generation, camera and tool callbacks.
enum UiTerrainControlId : u32 {
  kUiTerrainEditorWindowId = 0x72e85997,
  kUiTerrainAcceptButtonId = 0x72e867a1,
  kUiTerrainRegenerateButtonId = 0x72e867a2,
  kUiTerrainZoomInButtonId = 0x72e867a3,
  kUiTerrainZoomOutButtonId = 0x72e867a4,
  kUiTerrainRotateClockwiseButtonId = 0x72e867a5,
  kUiTerrainRotateCounterclockwiseButtonId = 0x72e867a6,
  kUiTerrainLowerToolButtonId = 0x72e867a7,
  kUiTerrainRaiseToolButtonId = 0x72e867a8,
  kUiTerrainLevelToolButtonId = 0x72e867a9,
  kUiTerrainTreeToolButtonId = 0x72e867aa,
  kUiTerrainWaterToolButtonId = 0x72e867ab,
  kUiTerrainDemolishToolButtonId = 0x72e867ac,
  kUiTerrainHillsSliderId = 0x72e867ad,
  kUiTerrainWaterSliderId = 0x72e867ae,
  kUiTerrainTreesSliderId = 0x72e867af,
  kUiTerrainConfigurationId = 0x72e867b0,
  kUiTerrainFeaturesLabelId = 0x72e867b1,
  kUiTerrainParametersLabelId = 0x72e867b2,
};

// Coast quadrants match the configuration view; the center hit selects its independently stored shape.
enum UiTerrainConfigurationFlag : i32 {
  kUiTerrainCoastUpperLeft = 1,
  kUiTerrainCoastUpperRight = 2,
  kUiTerrainCoastLowerLeft = 4,
  kUiTerrainCoastLowerRight = 8,
  kUiTerrainAllCoasts = 15,
  kUiTerrainCenterHit = 16,
};

// Center selections correspond to the low center, two river axes, raised center and no special feature.
enum UiTerrainCenter : i32 {
  kUiTerrainCenterNone = 0,
  kUiTerrainCenterLake = 1,
  kUiTerrainCenterRiverZ = 2,
  kUiTerrainCenterRiverX = 3,
  kUiTerrainCenterPeak = 4,
  kUiTerrainCenterCount = 5,
};

// SIMUI's terrain-configuration view is the secondary interface published by IID82E968EF.
// Private byte/Boolean return spellings remain qualified by the installed calling sites.
class UiTerrainConfigInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 SetWinTerrConfigFlag(i32 mask, bool enabled) = 0;
  virtual i32 GetWinTerrConfigFlag(i32 mask) const = 0;
  virtual u8 SetCenterType(u32 value) = 0;
  virtual u8 GetCenterType(u32 *out) const = 0;
  virtual void SetRotation(i32 degrees) = 0;
};

// Actual cSC3WinTerrConfig uses the SIMUI window base and owns its terrain images and color mask.
// Painting and resource handling are candidates awaiting connected verification.
class UiTerrainConfig : public UiBaseWindow, public UiTerrainConfigInterface {
public:
  UiTerrainConfig();
  ~UiTerrainConfig() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u8 SetWinTerrConfigFlag(i32 mask, bool enabled) override;
  i32 GetWinTerrConfigFlag(i32 mask) const override;
  u8 SetCenterType(u32 value) override;
  u8 GetCenterType(u32 *out) const override;
  void SetRotation(i32 degrees) override;
  bool Init() override;
  bool Shutdown() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;

protected:
  u32 center_type;
  u16 config_flags;
  i32 rotation;
  u8 hit, hovering;
  // Successfully loaded images are retained until Shutdown releases the corresponding fields.
  GxBuffer *overlay[4], *highlight[4], *background, *centers[5], *mask;
  u32 colors[5];
  u8 GetBuffer(u32 instance, GxBuffer **out);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiTerrainConfig) == 264);

class WnBitmapControl;
class UiTerrainConfigInterface;

// Original72E85998 extends the window callback12 with terrain-window creation16 and settings20..44.
// Private nominal byte/Boolean replies remain qualified; no separate provider/show interface is present.
class UiTerrainEditorInterface : public UiWindowCallback {
public:
  virtual u8 CreateTerrEditWin(WnWindow **out) = 0;
  virtual u32 GetCenterType() = 0;
  virtual u16 GetCoastFlags() = 0;
  virtual u8 GetHills() = 0;
  virtual u8 GetWater() = 0;
  virtual u8 GetTrees() = 0;
  virtual u8 GetSmoothing() = 0;
  virtual void ResetToDefault() = 0;
};

// SIMUI's terrain-edit procedure has a primary window callback and a message-target view at offset4 on x86.
// The editor receives generic-window callbacks and a separate message-target interface.
class UiTerrainEditor : public UiTerrainEditorInterface, public ScMessageTarget {
public:
  UiTerrainEditor();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 CreateTerrEditWin(WnWindow **out) override;
  u32 GetCenterType() override;
  u16 GetCoastFlags() override;
  u8 GetHills() override;
  u8 GetWater() override;
  u8 GetTrees() override;
  u8 GetSmoothing() override;
  void ResetToDefault() override;
  virtual ~UiTerrainEditor();
  bool DoMessage(ScMessage *message) override;

protected:
  u32 references, center_type;
  u16 coast_flags;
  u8 hills, water, trees, smoothing;
  WnBitmapControl *preview;
  UiTerrainConfigInterface *configuration;
  i64 saved_funds;

  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButtonClicked(WnWindow *window, u32 identifier);
  u8 OnKeyDown(WnWindow *window, u32 key, u32 modifiers);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiTerrainEditor) == 40);

#endif
