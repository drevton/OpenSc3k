#ifndef SC3_SPRITES_VIEW_CAMERA_H
#define SC3_SPRITES_VIEW_CAMERA_H

#include "city/api.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "geometry/ge_types.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"

class SpViewData;
class SpViewInfo;
class SpManagerView;

// Recovered cSC3CityViewIso exposes the real isometric-view prefix and a separate message-target identity.
// Construction, release, lifecycle, drawing and unported private timer work remain explicit dependencies.
class SpView : public SpIsoView, public ScMessageTarget {
public:
  SpView();
  bool Init(ApGameInterface *application, CtCity *source_city) override;
  bool Init(CtCity *source_city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *source_city, RsDatabaseSegment *segment) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown() override;
  void Plot() override;
  bool DoMessage(ScMessage *message) override;
  u32 EnablePlot(bool enabled) override;
  u32 GetViewID() const override;
  void SetCityViewID(i32 id, bool update) override;
  i32 GetCityViewID() const override;
  bool GetCityViewFlag(u32 mask) const override;
  void SetCityViewFlag(u32 mask, bool enabled, bool update) override;
  u32 GetCityViewFlags() const override;
  void SetCityViewFlags(u32 flags, bool update) override;
  bool TestCityViewFlags(u32 mask) const override;
  void ClearCityViewFlags() override;
  bool GetScreenClipRect(ScRect *out) const override;
  bool SetScreenClipRect(const ScRect &clip) override;
  void CameraGoHome(bool update) override;
  void FocusCameraOn(u32 x, u32 z, bool update) override;
  void GetFocusCell(u32 *x, u32 *z) override;
  void GetCameraHint(ScRect *first, ScRect *second) override;
  void ZoomIn(bool update) override;
  void ZoomOut(bool update) override;
  void GetZoomAndRotation(i32 *zoom, i32 *rotation) override;
  void SetZoomAndRotation(i32 zoom, i32 rotation, bool update) override;
  void RotateCameraCW(bool update) override;
  void RotateCameraCCW(bool update) override;
  void Translate(f32 x, f32 y, bool update) override;
  void SetAnimFrequency(const f32 &frequency) override;
  void GetAnimFrequency(f32 *frequency) override;
  SpTickManager *GetAnimTickManager() override;
  ScOccupantManager *CurrentOccupantManager() override;
  bool PerformPick(i32 x, i32 y, CtCoord *out, bool artwork) override;
  bool GetOccupantsInVolume(const GeBounds &bounds, ScOccupant **out, u32 *count, bool, bool unaligned) override;
  SpManagerView *GetCitySpriteManager() override;
  SpCellMap *GetCitySpriteCellMap() override;
  bool DoScreenShake(i32 mode, i32 duration, i32 first, i32 second, bool update, i32 interval) override;
  bool IsScreenShaking() override;
  // Original private extension order164..196; unused setter/hook nominal returns remain qualified.
  virtual void SetBackgroundBitmapForZoom(i32 zoom);
  virtual void DoNextShakeFrame();
  virtual u8 SetSmoothNavigation(bool enabled);
  virtual u8 GetSmoothNavigation();
  virtual u8 SetNotify(bool enabled);
  virtual void NotifyClients(u32 message, u32 first, u32 second);
  virtual u8 GetViewInfo(SpViewInfo **out);
  virtual SpManagerView *GetViewManager();
  virtual SpCellMap *GetSpriteCellMap();
  virtual ~SpView();

  CtCity *city;
  SpCellMap *cell_map;
  i32 focus_x, focus_z;
  u32 multiplier;
  i32 width, height;
  u8 events_enabled;
  SpViewData *data;
  SpManagerView *state;
  SpTickManager *tick_manager;
  u32 references, plot_disabled;
  i32 screen_width, screen_height;
  f32 animation_frequency;
  u8 animation_enabled, smooth_navigation, shaking, shake_update;
  u8 shake_mode;
  i8 shake_first, shake_second, shake_direction;
  i32 shake_duration, shake_interval;
  i32 shake_elapsed, shake_last;
  ApGameInterface *application;
  SpManagerView *controller;
  GxBuffer *canvas_buffer;
  ScRect screen_rectangle, shake_source, shake_destination;
  ScStopwatch frame_clock, animation_clock, shake_clock;
  u32 frame_total;
  f32 frame_rate;
  u8 initialized, flag41, flag200;
};

// Original cISC3CityViewInfo prefix shared by the isometric snapshot. Mutable reference outputs spell pointers.
class SpViewInfo : public ScUnknown {
public:
  virtual u32 GetViewId() const = 0;
  virtual bool GetInfo(u32 *out) const = 0;
  virtual bool SetInfo(i32 value) = 0;
  virtual bool GetType(i32 *out) const = 0;
  virtual bool SetType(i32 value) = 0;
  virtual bool GetFlags(u32 *out) const = 0;
  virtual bool SetFlags(u32 value) = 0;
};

// Original cISC3CityViewInfoIso extends the general snapshot with geometry, zoom, compass and tiling data.
class SpIsoViewInfo : public SpViewInfo {
public:
  // MSVC reverses each new overload group: bounds precedes coordinate getters/setters at40/44 and48/52.
  virtual bool GetInfo(CtCoord *out) const = 0;
  virtual bool SetInfo(const CtCoord &position) = 0;
  virtual bool GetInfo(GeBounds *out) const = 0;
  virtual bool SetInfo(const GeBounds &bounds) = 0;
  virtual bool GetZoom(i32 *out) const = 0;
  virtual bool SetZoom(const i32 &value) = 0;
  virtual bool GetCompass(i32 *out) const = 0;
  virtual bool SetCompass(const i32 &value) = 0;
  // Original72 writes both coordinates; its ignored nominal return remains unresolved.
  virtual void GetTilingOriginWindowPixel(i32 *x, i32 *y) const = 0;
};

// Concrete cSC3CityViewInfoIso snapshot. Only fields initialized by the original constructor are seeded.
class SpViewData : public SpIsoViewInfo {
public:
  SpViewData();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetViewId() const override;
  bool GetInfo(u32 *out) const override;
  bool SetInfo(i32 value) override;
  bool GetType(i32 *out) const override;
  bool SetType(i32 value) override;
  bool GetFlags(u32 *out) const override;
  bool SetFlags(u32 value) override;
  bool GetInfo(CtCoord *out) const override;
  bool SetInfo(const CtCoord &position) override;
  bool GetInfo(GeBounds *out) const override;
  bool SetInfo(const GeBounds &bounds) override;
  bool GetZoom(i32 *out) const override;
  bool SetZoom(const i32 &value) override;
  bool GetCompass(i32 *out) const override;
  bool SetCompass(const i32 &value) override;
  void GetTilingOriginWindowPixel(i32 *x, i32 *y) const override;
  virtual ~SpViewData();
  u32 references;
  i32 view_id;
  u32 flags;
  CtCoord focus;
  GeBounds bounds;
  i32 zoom, rotation, tiling_x, tiling_y;
};

#endif
