#ifndef SC3_UI_UI_MAP_VIEW_H
#define SC3_UI_UI_MAP_VIEW_H

#include "ui/map_navigation.h"

class ApGameInterface;
class SpCityWindowView;
class SpViewInterface;
class UiMapControl;
class WnBitmapControl;
class WnLabelControl;

// Original IID5E6 adds a separate Unknown view at offset100 on x86.
class UiMapViewIdentity : public ScUnknown {};

// Common map-view owner. The overlay operation retains its observed effects and unresolved nominal-return qualification.
class UiMapView : public UiMapsView, public UiMapViewIdentity {
public:
  static UiMapsViewInterface *CreateMapAura();
  static UiMapsViewInterface *CreateMapZone();
  static UiMapsViewInterface *CreateMapCrime();
  static UiMapsViewInterface *CreateMapPopulation();
  static UiMapsViewInterface *CreateMapPower();
  static UiMapsViewInterface *CreateMapFlammability();
  static UiMapsViewInterface *CreateMapLandValue();
  static UiMapsViewInterface *CreateMapPollution();
  static UiMapsViewInterface *CreateMapTraffic();
  static UiMapsViewInterface *CreateMapWaterSupply();
  UiMapView();
  ~UiMapView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateView(UiMapsWindow *owner, ScRect *area, ScPoint *location) override;
  u8 PrepareHideView() override;
  u8 PrepareShowView() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool DoMessage(ScMessage *message) override;
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  virtual UiMapControl *GetMapControl();
  // Original46130 effects; checked callers discard the register result, and the nominal return stays unresolved.
  virtual void SetShowInCity();
  virtual u8 DoShowInCity();
  virtual u8 DoCityViewRotation(i32 rotation);
  virtual u8 DoCityViewZoom(i32 zoom);

protected:
  ApGameInterface *application;
  UiMapControl *map;
  i32 rotation;
  WnBitmapControl *legend;
  WnLabelControl *compass[4];
  GxBuffer *palette_image;

  i32 HasCityWindow();
  SpCityWindowView *GetCityWindow();
  SpViewInterface *GetCurrentView();
  u8 GetZoomAndRotation(i32 *zoom, i32 *rotation_out);
  u8 UpdateView();
  u8 CreateMapOutlines(ScRect *area, u32 count);
  u8 CreateMapControl(ScRect *area);
  void CalculateMapControlRectEffects(const ScRect &area, ScRect *out);
  u8 CreateCompassWindows();
  u8 CreateShowInCityWindow(ScRect *area);
  u8 CreateLegendOutline(ScRect *area, bool upper);
  u8 CreatePalette(UiMapsWindow *owner, u32 identifier);
  u8 CreateLegendCaption(ScRect *area, const ScStringView &text, bool upper, u32 index, u32 count);
  u8 CreateLegendBitmap(UiMapsWindow *owner, ScRect *area, u32 identifier, bool upper);
  u8 CreateCircleAndOutline(ScRect *area, const ScStringView &text);
  void CreateLegendCaptionResource(ScRect *area, u32 instance, bool upper, u32 index, u32 count);
  u8 CreateCircleAndOutlineResource(ScRect *area, u32 instance);
};

// Concrete map kinds have distinct original class identities and override CreateView in the installed DLL.
// Their recovered resource-building overrides retain explicit missing dependencies.
class UiAuraMapView : public UiMapView {
public:
  UiAuraMapView();
  ~UiAuraMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiZoneMapView : public UiMapView {
public:
  UiZoneMapView();
  ~UiZoneMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiCrimeMapView : public UiMapView {
public:
  UiCrimeMapView();
  ~UiCrimeMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiPopulationMapView : public UiMapView {
public:
  UiPopulationMapView();
  ~UiPopulationMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiPowerMapView : public UiMapView {
public:
  UiPowerMapView();
  ~UiPowerMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiFlammabilityMapView : public UiMapView {
public:
  UiFlammabilityMapView();
  ~UiFlammabilityMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiLandValueMapView : public UiMapView {
public:
  UiLandValueMapView();
  ~UiLandValueMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiPollutionMapView : public UiMapView {
public:
  UiPollutionMapView();
  ~UiPollutionMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiTrafficMapView : public UiMapView {
public:
  UiTrafficMapView();
  ~UiTrafficMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

class UiWaterMapView : public UiMapView {
public:
  UiWaterMapView();
  ~UiWaterMapView() override;
  u8 CreateView(UiMapsWindow *, ScRect *, ScPoint *) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapView) == 140);

#endif
