#ifndef SC3_SPRITES_API_H
#define SC3_SPRITES_API_H

#include <list>

#include "common/api.h"
#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "sprites/sp_effect.h"

// Layer sprites apply per-view anchor corrections when this geometry flag is set.
enum SpLayerGeometryFlag : u32 {
  kSpLayerAnchorAdjustmentGeometry = 0x20000000,
};

// Sprite drawing interprets the low byte as opacity when this packed effect bit is set.
// Other high-word effects remain distinct from low-word highlight colors.
enum SpDrawEffect : u32 {
  kSpDrawOpacity = 0x10000,
};

// The all-ones view-flags request preserves current flags while the receiver enables its default visibility mask.
enum SpCityViewFlagRequest : u32 {
  kSpCityViewDefaultFlagsRequest = 0xffffffff,
};

// Water-impact callers select matched color/mask bases for the original nine- and ten-frame splash families.
enum SpSplashArtwork : i32 {
  kSpNineFrameSplashColorBase = 0x2d490000,
  kSpNineFrameSplashMaskBase = 0x2d490005,
  kSpTenFrameSplashColorBase = 0x48640000,
  kSpTenFrameSplashMaskBase = 0x48640005,
};

// Cell-cursor change callbacks retain the same sender identity published by the cursor map.
enum SpCursorChangeSender : u32 {
  kSpCellCursorChangeSenderId = 0xa17c8fc9,
};

// City-view bits independently select the terrain grid and the six above-ground layer categories.
enum SpCityViewFlag : u32 {
  kSpCityViewTerrainLayerMask = 6,
  kSpCityViewOccupantLayerMask = 0x7c,
  kSpCityViewTerrainGrid = 1,
  kSpCityViewZones = 2,
  kSpCityViewTransport = 4,
  kSpCityViewPowerLines = 8,
  kSpCityViewFlora = 16,
  kSpCityViewZonedBuildings = 32,
  kSpCityViewOtherBuildings = 64,
};

// View selections map to the layers dialog's surface, underground and named data-overlay options.
enum SpCityViewId : u32 {
  kSpCityViewSurface = 0,
  kSpCityViewSubway = 1,
  kSpCityViewWaterPipes = 2,
  kSpCityViewAura = 10,
  kSpCityViewCrime = 11,
  kSpCityViewPower = 12,
  kSpCityViewFlammability = 13,
  kSpCityViewLandValue = 14,
  kSpCityViewPollution = 15,
  kSpCityViewTraffic = 16,
  kSpCityViewDensity = 17,
};

// The city window consumes view ID, view flags and redraw selection in the message's three payload words.
enum SpCityViewMsg : u32 {
  kWnMsg_CityViewportChanged = 0x624a8241,
  kWnMsg_InitCityViewLayers = 0x62f6b8a3,
  kWnMsg_UpdateCityViewLayers = 0x22f6b8aa,
  kWnMsg_FocusCityCamera = 0xa35ee145,
  kWnMsg_SetCityCameraZoomRotation = 0xe35ee21f,
  kWnMsg_SetCityViewParams = 0x635ee3c3,
};

// Installed city cursor identities retain the filenames in the city-window registration table.
// The second rewards identity aliases the same installed cursor bitmap.
enum SpCursorId : u32 {
  kSpCenterCursorId = 0xc16c4e1b,
  kSpBlankCursorId = 0x43401c71,
  kSpQueryCursorId = 0x816d7f74,
  kSpRewardsCursorId = 0x417ee36b,
  kSpDemolishClearCursorId = 0xe1855adc,
  kSpWandOfDestructionCursorId = 0x22069e9b,
  kSpSnapshotCursorId = 0x3a3bdaef,
  kSpPanUpArrowCursorId = 0x62a67606,
  kSpPanUpLeftArrowCursorId = 0x2a67691,
  kSpPanLeftArrowCursorId = 0x62a67694,
  kSpPanDownLeftArrowCursorId = 0xa2a67698,
  kSpPanDownArrowCursorId = 0x62a6769b,
  kSpPanDownRightArrowCursorId = 0x22a676a0,
  kSpPanRightArrowCursorId = 0xc2a676a4,
  kSpPanUpRightArrowCursorId = 0x82a676a8,
  kSpEarlyMoverCursorId = 0xc2a676ac,
  kSpLevelTerrainCursorId = 0x82e434c0,
  kSpLowerTerrainCursorId = 0xc2e434c9,
  kSpManyTreesCursorId = 0xa2e4422f,
  kSpPaintWaterCursorId = 0xc2e434d0,
  kSpPlantTreeCursorId = 0x42e434d6,
  kSpRaiseLowerTerrainCursorId = 0x62e434dc,
  kSpRaiseTerrainCursorId = 0x62e434e9,
  kSpRiverCursorId = 0xc2e434f1,
  kSpDezoneCursorId = 0x42e434f7,
  kSpZoneAirportsCursorId = 0x42e434ff,
  kSpZoneCommercialHighCursorId = 0xc2e43505,
  kSpZoneCommercialLowCursorId = 0x82e4350c,
  kSpZoneCommercialMediumCursorId = 0xa16c4e2a,
  kSpZoneIndustrialHighCursorId = 0x22e43512,
  kSpZoneIndustrialLowCursorId = 0x42e43517,
  kSpZoneIndustrialMediumCursorId = 0xc16c4e30,
  kSpZoneLandfillCursorId = 0x2e4351e,
  kSpZoneResidentialHighCursorId = 0x2e43524,
  kSpZoneResidentialLowCursorId = 0x62e4352b,
  kSpZoneResidentialMediumCursorId = 0x16c4e26,
  kSpZoneSeaportsCursorId = 0x62e43531,
  kSpPowerLinesCursorId = 0xa16f1463,
  kSpWaterPipesCursorId = 0x82c2ff49,
  kSpRoadCursorId = 0xe17bb266,
  kSpAvenueCursorId = 0x2bfbd34,
  kSpRoadTunnelCursorId = 0x817bb2aa,
  kSpRailroadCursorId = 0x217bb308,
  kSpSubwayTubeCursorId = 0xc17bb329,
  kSpOnrampCursorId = 0xa3629cae,
  kSpDispatchFirefightersCursorId = 0x2e43537,
  kSpDispatchPoliceCursorId = 0xc2e4353d,
  kSpDispatchPlaneCursorId = 0x45357694,
  kSpEmergencyCursorId = 0x82e43543,
  kSpSignpostCursorId = 0xe2e43554,
  kSpBigParkCursorId = 0xc2e43562,
  kSpCollegeCursorId = 0xa2e43568,
  kSpFireCursorId = 0x62e4356d,
  kSpHospitalCursorId = 0xe2e43577,
  kSpJailCursorId = 0xc2e43572,
  kSpLandmarksCursorId = 0x22e4357e,
  kSpLibraryCursorId = 0x42e43583,
  kSpMarinaCursorId = 0x22e4358e,
  kSpMuseumCursorId = 0xe2e43594,
  kSpPoliceCursorId = 0xa2e4359c,
  kSpRewardsCursorAliasId = 0x42e435a2,
  kSpSchoolCursorId = 0x22e43589,
  kSpSmallParkCursorId = 0x2e435a8,
  kSpStadiumCursorId = 0xc2e435ae,
  kSpZooCursorId = 0xa2e435b3,
  kSpBusStopCursorId = 0x22e435b8,
  kSpSubwayRailConnectCursorId = 0x62e435c6,
  kSpSubwayStationCursorId = 0xe2e435cf,
  kSpTrainStationCursorId = 0xe2e435d5,
  kSpDesalinizationCursorId = 0x42e435db,
  kSpIncineratorCursorId = 0x62e435e3,
  kSpPowerPlantCursorId = 0x82e435e8,
  kSpRecyclingCenterCursorId = 0xe2e435ef,
  kSpWasteToEnergyCursorId = 0xc2e435f6,
  kSpWaterPumpingStationCursorId = 0xc2e435fd,
  kSpWaterTowerPumpCursorId = 0x22e43603,
  kSpWaterTreatmentCursorId = 0x22e43608,
  kSpBallparkCursorId = 0x436555fc,
  kSpPondCursorId = 0xa3655601,
  kSpPlaygroundCursorId = 0xc3655605,
};

// Effect constructors use a common geometry word; whirlpool pieces and abducted occupants retain distinct words.
enum SpEffectGeometry : u32 {
  kSpEffectBaseGeometry = 0x8000000,
  kSpWhirlpoolPieceGeometry = 0x8001000,
  kSpAbductedOccupantGeometry = 0x8000002,
};

// Installed sprite interface7101 is a distinct secondary Unknown receiver; its PC nominal class name is unavailable.
class SpAnimation : public ScUnknown {
public:
  virtual u8 Start(i32 x, i32 z, i32 altitude, u32 argument) = 0;
  virtual u8 Stop() = 0;
  virtual void SetPosition(i32 x, i32 z, i32 altitude, u32 argument) = 0;
};

struct SpImage;
class SpImageSource;
class ScStringView;

// Original image-cache service64111139/IID24111122 through its last image-release slot60.
// Overload declarations account for MSVC reversal; byte transports retain qualified private bool/BYTE spelling.
class SpImageCache : public ScUnknown {
public:
  virtual u8 IsInitialized() const = 0;
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual u8 RealShutdown() = 0;
  virtual u8 AddSpriteInfoFileToSearchList(const ScStringView &path, u8 append) = 0;
  virtual u8 RemoveSpriteInfoFileToSearchList(const ScStringView &path) = 0;
  virtual u8 AddDBSegmentSpriteToSearchList(const ScStringView &path, u32 append) = 0;
  virtual u8 AddDBSegmentSpriteToSearchList(SpImageSource *source, u32 append) = 0;
  virtual u8 RemoveDBSegmentSpriteToSearchList(const ScStringView &path) = 0;
  virtual u8 RemoveDBSegmentSpriteToSearchList(SpImageSource *source, u8 destroy) = 0;
  virtual SpImage *GetImageInfo(u32 resource, u32 group) = 0;          // Original +52 loads metadata when needed.
  virtual u8 PrepareImage(u32 resource, u32 group, SpImage **out) = 0; // Original +56, raw AL result.
  virtual u32 UnprepareImage(u32 resource, u32 group) = 0;             // Original +60, remaining reference count.
};

class GxBuffer;
class SpDefinition;

// Original sprite-instance slot order through96. Uncovered private nominal return spellings remain qualified below;
// the two native signed extent carriers retain the original DWORD bits, with source signedness still unresolved.
class SpSprite : public ScUnknown {
public:
  virtual i32 GetW(i32 zoom, i32 rotation) const = 0;
  virtual i32 GetH(i32 zoom, i32 rotation) const = 0;
  virtual u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const = 0;
  virtual u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const = 0;
  // Slot28 writes exactly one output byte; its discarded nominal return remains unresolved.
  virtual void GetRotationOffset(u8 *out) const = 0;
  virtual u32 GetLayerFlags() const = 0;
  // Slot36 consumes the original rotation byte; its discarded nominal result remains unresolved.
  virtual void SetRotationOffset(u8 rotation) = 0;
  virtual u8 SetEffect(u32 effect) = 0;
  virtual void SetLayer(u8 layer) = 0;
  virtual bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const = 0;
  // Linux Prepare(i,i,b) proves the third input. Preserve the existing raw AL return carrier.
  virtual u8 DemandArt(i32 zoom, i32 rotation, bool argument) = 0;
  virtual i32 ReleaseArt(i32 zoom, i32 rotation) = 0;
  // MSVC reverses newly introduced overloads: rectangle drawing occupies60, point drawing64.
  virtual u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const = 0;
  virtual u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const = 0;
  // The reached script returns a defined zero DWORD; its nominal source declaration remains uncovered.
  virtual i32 Tick(u32 delta) = 0;
  virtual bool SetDefinition(SpDefinition *value) = 0;
  virtual u8 HasGeomFlag(u32 mask) = 0;
  virtual u8 SetGeomFlag(u32 mask) = 0;
  virtual u8 ClearGeomFlag(u32 mask) = 0;
  virtual u32 GetGeomFlag() = 0;
  // Slot92's empty-to-empty path has no defined EAX. Callers consume only the ownership effect.
  virtual void SetNext(SpSprite *next) = 0;
  virtual SpSprite *GetNext() = 0;
};

// Original cISC3MapSpriteFactory, IID000005E4, exposed by the city layers that advertise navigation-map sprites.
// Linux Aura symbols establish the recovered names; installed producers retain the existing argument and result contracts.
class SpMapSpriteFactory : public ScUnknown {
public:
  virtual bool CreateMapSprite(u32 kind, u32 iid, void **out) = 0; // Original12.
  virtual u32 GetMapSpriteCount() = 0;                             // Original16.
  virtual bool GetSpriteIdFromIndex(u32 index, u32 *out) = 0;      // Original20.
};

class ScDirector;
class ScFramework;
class ScMessageServer;
class CtCity;
class SpManagerView;
class SpCityWindowView;
class SpViewInterface;
class SpTickManager;
class SpCellMap;
class RsManager;
class ScCom;
class ScLanguageManager;
class RsFileSystem;
class ApGameInterface;
class ApPreferences;
class SpImageCache;
class GxGraphics;
class GxBlendService;
class WnResources;
struct ApPerformanceInfo;

ScDirector *SpGetDirector();
ScFramework *SpGetFramework();
ScMessageServer *SpGetMessageServer();
CtCity *SpGetCity();
bool SpHasCity();
SpManagerView *SpGetSpriteManager();
SpCityWindowView *SpGetCityWindowView();
SpViewInterface *SpGetCurrentView();
SpCellMap *SpGetCellMap();
SpTickManager *SpGetTickManager();
RsManager *SpGetResourceManager();
ScCom *SpGetCom();
ScLanguageManager *SpGetLanguageManager();
RsFileSystem *SpGetFileSystem();
ApGameInterface *SpGetApplication();
ApPreferences *SpGetPreferences();
SpImageCache *SpGetImageCache();
GxGraphics *SpGetGraphics();
GxBlendService *SpGetBlendService();
WnResources *SpGetWindowResources();
ApPerformanceInfo *SpGetPerformanceInfo();

class GxBuffer;

// Four-stack-argument decoder entries; no receiver or incoming EBX/EDI parameter is consumed.
// The byte result preserves Lock's AL on failure; private bool/BYTE and unused-size signedness remain unknown.
u8 SC3_STDCALL SpDecodeRaw(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
u8 SC3_STDCALL SpDecodeRowRuns(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
u8 SC3_STDCALL SpDecodeColumnRuns(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);

// Reached primary IID22B66D3D, shared with the same receiver's message-target identity.
// Authentic retail selection-handler overrides establish bool results; reset remains void and the view predicate is const.
class SpViewHandler : public ScMessageTarget {
public:
  virtual bool Init() = 0; // Original16, called by SIMSPR SwitchHandler10048A2D.
  virtual bool Shutdown() = 0;
  virtual bool OnKeyDown(u32 key, u32 flags) = 0;
  virtual bool OnKeyUp(u32 key, u32 flags) = 0;
  virtual bool OnMouseDownL(i32 x, i32 y, u32 flags) = 0;
  virtual bool OnMouseDownR(i32 x, i32 y, u32 flags) = 0;
  virtual bool OnMouseUpL(i32 x, i32 y, u32 flags) = 0;
  virtual bool OnMouseUpR(i32 x, i32 y, u32 flags) = 0;
  virtual bool OnMouseMove(i32 x, i32 y, u32 flags) = 0;
  virtual bool GetCursor(u32 *out) = 0; // Original52.
  virtual void OnResetState() = 0;      // Original56.
  virtual bool IsValidHandlerByView(
      i32 view_kind, u32 flags) const = 0; // Original60; destructor follows in concrete tools.
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class SpTickManager;

// Complete SDK cISC3CityView order through144. Installed GetZoomAndRotation104 and Translate120 corroborate
// the placement of camera hint92 and animation manager132; the concrete view's linkage remains unresolved.
class SpViewInterface : public ScUnknown {
public:
  // MSVC reverses this overload group: original saved-city Init is12, application/city Init is16.
  virtual bool Init(ApGameInterface *, CtCity *) = 0;
  virtual bool Init(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool Save(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool Shutdown() = 0;
  virtual u32 EnablePlot(bool) = 0;
  virtual void Plot() = 0;
  virtual u32 GetViewID() const = 0;
  virtual void SetCityViewID(i32, bool) = 0;
  virtual i32 GetCityViewID() const = 0;
  virtual bool GetCityViewFlag(u32) const = 0;
  virtual void SetCityViewFlag(u32, bool, bool) = 0;
  virtual u32 GetCityViewFlags() const = 0;
  virtual void SetCityViewFlags(u32, bool) = 0;
  virtual bool TestCityViewFlags(u32) const = 0;
  virtual void ClearCityViewFlags() = 0;
  virtual bool GetScreenClipRect(ScRect *) const = 0;
  virtual bool SetScreenClipRect(ScRect const &) = 0;
  virtual void CameraGoHome(bool) = 0;
  virtual void FocusCameraOn(u32, u32, bool) = 0;
  virtual void GetFocusCell(u32 *, u32 *) = 0;
  virtual void GetCameraHint(ScRect *first, ScRect *second) = 0; // SDK cISC3CityView.h:60; installed +92.
  virtual void ZoomIn(bool) = 0;
  virtual void ZoomOut(bool) = 0;
  virtual void GetZoomAndRotation(i32 *zoom, i32 *rotation) = 0;
  virtual void SetZoomAndRotation(i32, i32, bool) = 0;
  virtual void RotateCameraCW(bool) = 0;
  virtual void RotateCameraCCW(bool) = 0;
  virtual void Translate(f32, f32, bool) = 0;
  virtual void SetAnimFrequency(f32 const &) = 0;
  virtual void GetAnimFrequency(f32 *) = 0;
  virtual SpTickManager *GetAnimTickManager() = 0; // SDK cISC3CityView.h:71; installed +132.
  virtual ScOccupantManager *CurrentOccupantManager() = 0;
  virtual bool PerformPick(i32, i32, CtCoord *, bool) = 0;
  virtual bool GetOccupantsInVolume(GeBounds const &, ScOccupant **, u32 *, bool, bool unaligned) = 0;
};

// Recovered implementation base: primary vptr, packed flags/reference count, and owned definition.
// Its constructor/vtable are distinct from the script instance's secondary animation interface.
class SpInstance : public SpSprite {
public:
  SpInstance();
  virtual ~SpInstance();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool SetDefinition(SpDefinition *value) override;
  virtual i32 GetW(i32 zoom, i32 rotation) const override;
  virtual i32 GetH(i32 zoom, i32 rotation) const override;
  virtual u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  virtual bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 delta) override;
  void GetRotationOffset(u8 *out) const override;
  void SetRotationOffset(u8 rotation) override;
  u32 GetLayerFlags() const override;
  u8 SetEffect(u32 effect) override;
  void SetLayer(u8 layer) override;
  void SetNext(SpSprite *next) override;
  virtual u8 HasGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  virtual u8 ClearGeomFlag(u32 mask) override;
  virtual u32 GetGeomFlag() override;
  SpSprite *GetNext() override;
  virtual u32 GetFrameIndex(i32 zoom, u8 rotation) const;
  u32 flags;
  SpDefinition *definition;
};

// Real class7312 extends the base instance with a retained chain link, byte frame offset and anchor-adjustment flag.
class SpLayerSprite : public SpInstance {
public:
  SpLayerSprite();
  ~SpLayerSprite() override;
  void SetRotationOffset(u8 rotation) override;
  u8 SetEffect(u32 effect) override;
  void SetLayer(u8 layer) override;
  void SetNext(SpSprite *value) override;
  SpSprite *GetNext() override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u32 GetFrameIndex(i32 zoom, u8 rotation) const override;
  SpSprite *next;
  u8 frame_offset, adjusted;
};

// Original cSC3CitySpriteInstGenericBuilding supplies the cell map's unloaded-art fallback at five zoom levels.
// Its per-zoom demand counts own cache references; the buffer pointers themselves are borrowed.
class SpGenericBuilding : public SpInstance {
public:
  // Construct only the original instance base. Init establishes the footprint and both per-zoom arrays afterward.
  SpGenericBuilding() = default;

  // Destruction invokes only the original instance base. Outstanding cache demands are not silently released here.
  ~SpGenericBuilding() override = default;
  u8 Init(i32 footprint, i32 unused);
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u32 GetLayerFlags() const override;

private:
  static void StaticInit();
  static u32 GetSpriteKey(i32 zoom, u32 footprint, u32 base);
  u8 footprint;
  u32 demands[5];
  GxBuffer *buffers[5];
};

// Original cSC3CitySpriteInstUseCSMTerrain retains its terrain sprite and a separate optional chain link.
// Geometry, drawing and animation forward to terrain; artwork residency and effects also visit the link.
class SpTerrainSprite : public SpSprite {
public:
  SpTerrainSprite(SpSprite *terrain);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  void GetRotationOffset(u8 *out) const override;
  u32 GetLayerFlags() const override;
  void SetRotationOffset(u8 rotation) override;
  u8 SetEffect(u32 effect) override;
  void SetLayer(u8 layer) override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  bool SetDefinition(SpDefinition *definition) override;
  u8 HasGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  void SetNext(SpSprite *value) override;
  SpSprite *GetNext() override;
  virtual ~SpTerrainSprite();
  u32 references;
  SpSprite *terrain, *next;
};

// Original cSC3CitySpriteInstBasic combines the sprite identity with an effects occupant.
// Each zoom borrows color/mask buffers; the constructor deliberately leaves the effect parameter unwritten.
class SpBasicSprite : public SpSprite, public SpEffectOccupant {
public:
  SpBasicSprite();
  ~SpBasicSprite() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  void GetRotationOffset(u8 *out) const override;
  u32 GetLayerFlags() const override;
  void SetRotationOffset(u8 rotation) override;
  u8 SetEffect(u32 effect) override;
  void SetLayer(u8 layer) override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  bool SetDefinition(SpDefinition *definition) override;
  u8 HasGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  void SetNext(SpSprite *next) override;
  SpSprite *GetNext() override;
  SpSprite *GetSpriteInst() override;
  using SpEffectOccupant::GetSpriteInst;

  u8 mode, shifts[5];
  GxBuffer *color[5], *mask[5];
  ScPoint origins[5];
  ScRect registration[5];
  u32 effect_value;
};

// Original LightParticle owns list nodes for borrowed trail particles. Its packed scalar state precedes that list.
// Source spellings for the unused scalar fields remain unresolved; their widths and initialization are recovered.
class SpLightParticle {
public:
  SpLightParticle();
  // Copy the actual scalar state and borrowed trail pointers into a separate list lifetime.
  // This replaces the original default-construction/assignment sequence without recreating its allocator.
  SpLightParticle(const SpLightParticle &) = default;
  ~SpLightParticle();
  u8 IsCompletelyDead();
  // Private tick/generation helpers preserve their complete effects; discarded nominal returns remain unresolved.
  void GenerateTrailingParticle();
  void RemoveDeadParticles();
  void DoTick(i32 elapsed);
  void CalculateNextPosition(i32 elapsed);
  i32 GetColorForAge(i32 age);
  static i32 SC3_STDCALL GetGroundTypeForPosition(const ScVec3F &point, i32 *altitude);
  static i32 PositionIsInWorldBounds(const ScVec3F &point);
  u8 alive, trailing;
  u16 lifetime, trail_interval, trail_lifetime;
  i16 kind;
  i16 motion;
  i16 color_index;
  u16 word14;
  ScPoint pixel;
  ScVec3F position, velocity;
  u16 age, color, last_trail_age, word54;
  f32 float56, float60, float64;
  u16 word68, word70;
  std::list<SpLightParticle *> trails;
};

// Original ParticleBlastGenerator subobject starts at simulator+8 and contains its reusable particle prototype.
// Base-versus-member source spelling is unresolved; this represents the verified subobject without inventing inheritance.
class SpParticleBlast {
public:
  void Generate(ScArray<SpLightParticle *> *out, i32 count, i32 shape, i16 color);
  void DoGeneration(ScArray<SpLightParticle *> *out);
  i32 shape;
  ScVec3F origin;
  u32 count;
  SpLightParticle prototype;
};

// Actual cSC3ParticleSimulator owns its generator subobject and a vector of borrowed generated particle pointers.
// Sprites adopt the generated particles; clearing the vector does not delete those adopted objects.
class SpParticleSimulator final : public SpParticleEffect {
public:
  SpParticleSimulator();
  ~SpParticleSimulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Init() override;
  u32 DoParticleGeneration(i32 x, i32 z, i32 y, i32 mode, i32 count) override;
  u32 references;
  SpParticleBlast generator;
  ScArray<SpLightParticle *> generated;
};

ScUnknown *SpCreateParticleSimulator();

// Original LightParticleSprite owns one lead particle, borrowed per-zoom artwork and five source rectangles.
// Allocation uses ordinary new; the original fixed-block free-list machinery is not reconstructed.
class SpParticleSprite : public SpBasicSprite {
public:
  SpParticleSprite();
  ~SpParticleSprite() override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  // These private operations preserve reached effects; discarded nominal return declarations remain unresolved.
  void Init(SpLightParticle *particle);
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void CalculateCurrentOutlineAndPosition(i32 zoom);
  void SetInitialSourceRectForSprite(i32 zoom);
  void DrawParticle(SpLightParticle *particle, GxBuffer *target, i32 zoom, i32 x, i32 y, ScRect *clip) const;
  static void SC3_STDCALL MicroActualGridToMicroDrawGrid(i32 rotation, i32 x, i32 z, i32 *out_x, i32 *out_z);
  static void SC3_STDCALL ConvertPositionToCityPixel(const ScVec3F &point, i32 *x, i32 *y);
  static u32 GetSpriteKey(i32 zoom, i32 kind);
  u8 registered;
  u16 age_frame;
  i16 frame;
  u32 frame_started;
  SpLightParticle *particle;
  u32 elapsed;
  ScRect area;
  i32 kind;
  GxBuffer *particle_color[5], *particle_mask[5], *color_table;
  ScRect source[5];
};

// Original cSC3CloudSprite owns cache demands for its low-zoom color/mask pair and optional borrowed shadow.
// Motion and shadow lifetime belong to the weather simulator, not the sprite destructor.
class SpCloud : public SpBasicSprite {
public:
  explicit SpCloud(i32 variant);
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  // Private SetSpanRects has an unproved discarded nominal result; preserve its actual field updates only.
  void SetSpanRects();
  static u32 GetSpriteKey(i32 zoom, i32 variant, i32 base);
  i32 variant;
  u32 elapsed, step_time;
  ScVec3F velocity;
  SpBasicSprite *shadow;
};

// Original cSC3PrecipitationSprite uses the same zoom restriction with separately selected precipitation artwork.
// Its two byte fields and elapsed/step words are updated by the weather simulator.
class SpPrecipitation : public SpBasicSprite {
public:
  SpPrecipitation();
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  // As with clouds, this private helper's ignored nominal return remains unresolved.
  void SetSpanRects();
  static u32 GetSpriteKey(i32 zoom, i32 variant, i32 base);
  i8 kind;
  u8 phase;
  u32 elapsed, step_time;
};

// Original cSC3LightningSprite parks at altitude4194304 while hidden, retaining its animation-layer membership.
// Its artwork is a mask borrowed from the cache; the simulator controls activation and visible placement.
class SpLightning : public SpBasicSprite {
public:
  SpLightning();
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  // These private operations preserve their reached effects; original discarded nominal results remain unresolved.
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void HideFromView();
  void MoveToPosition(const ScVec3F &point);
  bool IsInVisiblePosition();
  u8 registered;
  u32 word204;
};

// Original cSC3RadiationSprite adds only its resource-demand methods to the real basic sprite.
class SpRadIcon : public SpBasicSprite {
public:
  // The original inlined constructor initializes only the basic sprite. No extra fields or image demands are added.
  SpRadIcon() = default;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
};

// Original cSC3PowerOuttageDisplaySprite blinks one of three horizontal frames over an occupant or zoned cell.
// A pointer value of one denotes a bare zoned cell; it is compared, never dereferenced as an occupant.
class SpPowerIcon : public SpBasicSprite {
public:
  SpPowerIcon();
  ~SpPowerIcon() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 delta) override;
  u8 AddToAnimationSystem();
  u8 RemoveFromAnimationSystem();
  // These private methods have unresolved discarded nominal results; preserve their actual reached effects.
  void SetSpanRects();
  void MoveToNewPosition(i32 x, i32 z, i32 y, u8 insert, u8 reinsert);
  void UpdatePosition(i32 zoom, i32 rotation);
  void MoveToNewOccupant(ScOccupant *source, i32 zoom, i32 rotation, i32 x, i32 z);
  void GetPositionForOccupant(ScOccupant *source, i32 zoom, i32 rotation, i32 *x, i32 *z, i32 *y);
  ScOccupant *occupant;
  u8 registered;
  i8 frame, frame_step;
  u32 elapsed, next_blink;
  i32 cell_x, cell_z;
  static u32 blink_time;
};

// Original cSC3NeighborTileSprite stores one uninitialized rotation word after the basic sprite.
// GetIconPositionFromOccupant supplies that word before a visible icon acquires its art.
class SpNeighborIcon : public SpBasicSprite {
public:
  enum class Rotation : i32 {};
  // Construct the original basic fields and leave rotation unwritten until the owning display places the icon.
  SpNeighborIcon() = default;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u32 GetSpriteKey(i32 zoom, i32 camera_rotation);
  Rotation rotation;
};

// Recovered cSC3NeighborTileDisplay has primary/message identities and retains icons in signed z-then-x order.
// Init establishes the borrowed manager, layer, message identifiers and geometry mask before any subscriptions.
class SpNeighbors final : public SpNeighborDisplay, public ScMessageTarget {
public:
  SpNeighbors();
  ~SpNeighbors();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(ScOccupantManager *manager, i32 layer, u8 post) override;
  void Shutdown() override;
  void ShowOrHide(u8 enabled) override;
  bool DoMessage(ScMessage *message) override;
  void UpdateAll();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  void GetIconPositionFromOccupant(ScOccupant *occupant, SpNeighborIcon::Rotation *rotation, u32 *x, u32 *z, u32 *y);
  u8 DoesOccupantConnectToNeighbor(ScOccupant *occupant, u32 *x, u32 *z, i32 layer);
  ScOccupant *GetOccupantForCell(u32 x, u32 z);
  u8 OccupantInserted(ScOccupant *occupant, u8 post);
  u8 OccupantRemoved(ScOccupant *occupant, u8 post);
  u8 OccupantChanged(ScOccupant *occupant, u8 post);
  u32 references;
  u8 visible, registered;
  i32 layer;
  ScOccupantManager *manager;
  u32 inserted_message, removed_message, changed_message, geom_mask;
  ScArray<SpCellIcon> icons;
};

// Original cSC3WaterSplashSimulator owns its controller primary, basic sprite at4 and occupant at8.
// Resource selection and horizontal frame animation use separate original byte fields.
class SpSplash : public SpWaterSplash, public SpBasicSprite {
public:
  SpSplash();
  ~SpSplash() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, i32 first, i32 last, i8 frames) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(i32 variant, i32 zoom, i32 base);
  u32 color_base, mask_base;
  i8 variant;
  u8 registered, frame, frame_count;
  u32 elapsed, frame_elapsed;
};

// Original cSC3TrailingSmoke retains the sprite primary and exposes its smoke interface at200.
// Animation uses the original fixed tick threshold despite the separately stored public interval word.
class SpSmoke : public SpBasicSprite, public SpSmokeView {
public:
  SpSmoke();
  ~SpSmoke() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 SetTimeBetweenFrames(u32 duration) override;
  u8 Init(i32 x, i32 z, i32 altitude, i32 variant);
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(i32 zoom, i32 variant, u32 base);
  i32 variant;
  u32 elapsed, frame_elapsed, word216;
  u8 registered, frame, frame_count;
  u32 frame_interval;
};

// Original cSC3WaterDebris has a sprite/occupant prefix and its own control identity at200 on PC.
// Variant and initial fade strength require the real initialization path; its PC mapping remains unresolved.
class SpWaterDebris : public SpBasicSprite, public SpWaterDebrisView {
public:
  SpWaterDebris();
  ~SpWaterDebris() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 SetTimeBetweenFrames(u32 duration) override;
  u32 SetTimeToLive(u32 duration) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 delta) override;
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(i32 zoom, i32 variant, u32 base);
  i32 variant;
  u32 elapsed, frame_elapsed, word216;
  u8 registered, frame, frame_count;
  u32 frame_interval, fade_elapsed, time_to_live;
};

// Original cSC3ExplosionDebrisSimulator owns the controller primary, basic sprite at4 and occupant at8.
// Flight state and its optional basic shadow occupy the original tail through byte251.
class SpDebris : public SpDebrisView, public SpBasicSprite {
public:
  SpDebris();
  ~SpDebris() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, const ScVec3F &velocity, i32 variant, i8 mode) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(i32 mode, i32 zoom, i32 variant);
  u8 byte204;
  i16 variant;
  ScVec3F velocity;
  u32 elapsed, frame_elapsed;
  f32 gravity;
  u8 hit_building;
  u32 settled_at;
  u8 registered, frame, frame_count, create_shadow;
  SpBasicSprite *shadow;
  u32 frame_interval;
};

// Original cSC3ExplosionSimulator exposes its controller primary, sprite at4 and effects occupant at8.
// The separately stored center is used for animation membership without changing the basic occupant position.
class SpExplosion : public SpExplosionView, public SpBasicSprite {
public:
  SpExplosion();
  ~SpExplosion() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, u8 severity, i8 mode, u8 start) override;
  i32 SetBlastRadius(i32 first, i32 second, i32 third) override;
  u8 GetOccupantTrajectories(ScVec3F *positions, ScVec3F *out, u32 count) override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  void DoBlastWave(i32 radius);
  void SetSpanRects();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  u32 GetSpriteKey(i32 severity, i32 zoom, i32 rotation, i32 variant, u32 base);
  u8 registered, rotation_art;
  i32 variant;
  u32 color_base, mask_base, stage, word224, stage_times[5], word248, frame_times[15];
  u8 blast_enabled;
  i32 radii[3];
  u32 elapsed, word332, word336;
  u8 frame, frame_count, severity;
  i8 effect_mode;
  ScVec3F center;
  SpBasicSprite *overlay;
};

// Actual nuclear controller, sprite and occupant identities occupy0/4/8 on PC.
// Frame-pointer rows have ten entries while registration rows retain the original32-entry stride.
class SpNuclear : public SpNuclearView, public SpBasicSprite {
public:
  SpNuclear();
  ~SpNuclear() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, i8 first, i8 second) override;
  u8 GetCoordinatesForPowerPlantExplosion(i32 cell_x, i32 cell_z, i32 *x, i32 *z, i32 *altitude) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 delta) override;
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  static u32 GetSpriteKey(u32 base, i32 zoom, i32 frame);
  i32 first_type, second_type;
  u32 debris_time, elapsed, last_frame, frame_interval;
  u8 debris_spawned;
  i32 frame_count, current_frame;
  u8 registered;
  GxBuffer *frame_color[5][10], *frame_mask[5][10];
  ScRect frame_registration[5][32];
};

// Original cSC3TrajectoryObject combines the controller primary with a basic sprite at4 and occupant at8.
// Shadow/layer membership and the global trajectory set retain their original separate lifetimes.
class SpTrajectory : public SpTrajectoryView, public SpBasicSprite {
public:
  SpTrajectory();
  ~SpTrajectory() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(i32 x, i32 z, i32 altitude, const ScVec3F &velocity, i32 kind) override;
  void SetTrajectoryType(Motion mode, void *data) override;
  u8 EnableTrailingSmoke(u8 enabled) override;
  i32 GetType() override;
  u32 GetSourceSignature() override;
  u32 SetSourceSignature(u32 signature) override;
  u32 GetCountOfTrajectoryObjects(u32 signature) override;
  u8 EnableCreationOutsideWorldBoundaries(u8 enabled);
  u8 AddShadow(u32 base, u32 index);
  u8 RemoveShadow();
  void SetSpanRects();
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  i32 Tick(u32 elapsed) override;
  void DoImpact();
  void AddToAnimationSystem();
  void RemoveFromAnimationSystem();
  u32 GetSpriteKey(i32 zoom, i32 rotation, i32 variant, u32 base);
  static void AddTrajectoryObjectToStaticList(SpTrajectory *object, u8 insert);
  static u32 GetCountOfTrajectoryObjectsStatic(u32 signature);

  u8 byte204;
  i32 kind;
  u8 allow_outside;
  u32 signature;
  i32 sprite_index;
  u32 color_base, mask_base;
  Motion motion;
  ScVec3F target;
  f32 target_speed;
  u8 trailing_smoke, registered;
  i32 impact_kind;
  u8 flag260;
  ScVec3F velocity, midpoint;
  u32 elapsed, frame_elapsed;
  f32 distance296, distance300;
  u8 frame, frame_count, frame_step, rotation_art;
  u32 frame_interval;
  SpBasicSprite *shadow;
  u32 shadow_base, shadow_index;
};

extern u32 SpFallbackColor;

#endif
