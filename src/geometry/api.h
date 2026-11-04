#ifndef SC3_GEOMETRY_API_H
#define SC3_GEOMETRY_API_H

#include "city/ct_layer.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScStringView;
class CtAgentType;
class SpDefinition;
struct RsKey;

// Occupancy bits identified by the query status captions and historical-building option.
enum GeBuildingStateFlag : u32 {
  kGeBuildingOccupied = 1,
  kGeBuildingAbandoned = 2,
  kGeBuildingUnderConstruction = 4,
  kGeBuildingCoveredInGarbage = 0x100,
  kGeBuildingHistorical = 0x200,
};

// Original occupant-attribute virtual order, retaining independently verified Unlimited widths and constness.
// MSVC reverses the sprite overload group, placing the no-argument getter at84 and indexed getter at88.
class GeAttrib : public ScUnknown {
public:
  virtual bool GetOccupantGZCLSID(u32 *) const = 0;
  virtual bool SetOccupantGZCLSID(u32) = 0;
  virtual RsKey *Key() const = 0; // Borrowed resource key at original slot20.
  virtual bool IsGeomFlag(u32) = 0;
  // Geometry mutation replies preserve raw AL; old T_UCHAR records do not establish literal bool/BYTE spelling.
  virtual u8 SetGeomFlag(u32 mask) = 0;
  virtual u8 ClearGeomFlag(u32 mask) = 0;
  virtual u32 GetGeomFlag() = 0;
  virtual u8 GetOccLayerFlag() = 0;
  virtual void SetOccLayerFlag(u8 mask) = 0;
  virtual u8 MajorType() const = 0;
  virtual u8 SubTypeInfo() const = 0;
  virtual u8 AnimType() const = 0;
  virtual bool GetName(ScStringView *out) = 0;
  virtual u32 SizeInCellsX() = 0;
  virtual u32 SizeInCellsZ() = 0;
  virtual u32 SizeInCellsY() = 0;
  virtual CtAgentType *CityAgentType() = 0;
  virtual u32 TypeId() const = 0;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) = 0;
  virtual SpDefinition *GetSpriteAttrib() = 0;
  virtual SpDefinition *GetOriginalSpriteAttrib() = 0;
  virtual u32 GetDefaultLayerFlag() const = 0;
  virtual bool SetDefaultLayerFlag(u32) = 0;
  virtual iptr GetPeopleProducerAttrib() = 0;
  virtual iptr GetVehicleProducerAttrib() = 0;
  virtual u8 Flammability() = 0;
  virtual u32 GetSoundObjectId(i32 *index) = 0;
  virtual void GetSoundKeyA(RsKey *) = 0;
  virtual void GetSoundKeyB(RsKey *) = 0;
  virtual void GetPlopCursorKey(RsKey *) = 0;
  virtual void GetQueryDescKey(RsKey *) = 0;
  virtual void GetQueryFormatFlags(u32 *) = 0;
  virtual u32 GetQueryDialogClass() = 0;
  virtual bool SetQueryDialogClass(u32) = 0;
  virtual bool DebugInstanceTag(ScStringView *) = 0;
  virtual bool DebugClassTag(ScStringView *) = 0;
  virtual bool DebugTypeTag(ScStringView *) = 0;
  virtual bool GetNameKey(RsKey *) = 0;
  virtual void SetOverRideAttrib(OcOverride *) = 0;
  virtual OcOverride *GetOverRideAttrib() = 0;
  virtual bool GetArchitectName(ScStringView *) = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class BdBuilding;
class GeAttrib;
class SpDefinition;

// Public cISC3BuildingLayer order matches installed table100292C0 through slot112.
// Retain Unlimited signed tracking counts and borrowed typed default-data pointers where the SDK differs.
class GeBuildingLayer : public ScUnknown {
public:
  typedef u32 GeOccupancyState;
  virtual void GetCityCenter(u32 *, u32 *) = 0;
  virtual u32 GetTotalAreaOfBuildings() const = 0;
  virtual bool IsBuildable(GeCellRect const &, bool) const = 0;
  virtual bool CreateBuilding(RsKey const &, u32, void **) = 0;
  virtual bool CreateBuilding(GeAttrib *, u32, void **) = 0;
  virtual bool CreateBuilding(u32, u32, u32, void **) = 0;
  virtual bool CanPlaceBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *) = 0;
  virtual bool CanPlaceShorelineBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *) = 0;
  virtual bool CanPlacePlumbingBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *) = 0;
  virtual bool CanPlaceSubwayBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *) = 0;
  virtual bool CanPlaceMultiBuilding(
      CtCoord const &, RsKey const &, RsKey const &, RsKey const &, RsKey const &, GeBounds *, u32 *) = 0;
  virtual bool PlaceBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *, bool) = 0;
  virtual bool PlaceShorlineBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *, bool) = 0;
  virtual bool PlacePlumbingBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *, bool) = 0;
  virtual bool PlaceSubwayBuilding(CtCoord const &, RsKey const &, GeBounds *, u32 *, bool) = 0;
  virtual bool PlaceMultiBuilding(
      CtCoord const &, RsKey const &, RsKey const &, RsKey const &, RsKey const &, GeBounds *, u32 *, bool) = 0;
  virtual bool DoPlaceBuildingTool(RsKey const &, bool, bool) = 0;
  virtual bool DoPlaceBuildingTool(GeAttrib *attrib, bool first, bool second) = 0;
  virtual bool DoPlaceBuildingTool(u32, u32, bool, bool) = 0;
  virtual bool InsertBuilding(BdBuilding *) = 0;
  virtual bool RemoveBuilding(BdBuilding *) = 0;
  virtual i32 CheckOccupantTracking(u32) = 0;
  virtual GeAttrib *GetDefaultAttribData(BdBuilding *building) = 0;
  virtual SpDefinition *GetDefaultSpriteData(BdBuilding *building) = 0;
  virtual bool StartTrackingOccupantCount(u32) = 0;
  virtual bool CreatePlacementTool(RsKey const &, bool, bool, u32, void **) = 0;
};

struct GeBounds;

class ScOccupantManager;
class OcIteratorTest;
class SpSelection;

// Reached native view for SIMGEOM IID231C8E63, not a foreign vtable overlay or historical class-name claim.
// QI14676 exposes this, Layer206C6E7C, message58D and query81C0CB7C at the same primary address.
// CtLayer retains the observed same-primary prefix. Original100 and later operations remain omitted.
class GeDemolitionView : public CtLayer {
public:
  // Original72, SIMGEOM12E92, RET1Ch. Cost, affected bounds and retained selection are genuine separate outputs.
  virtual u8 CanDemolishOccupants(ScOccupantManager *manager, const GeBounds *bounds, OcIteratorTest *test, u32 mask,
      u32 *cost, GeBounds *affected, SpSelection **selection) = 0;
  // MSVC reverses this overload group: the four-argument form occupies76, the six-argument form80.
  // Original80, SIMGEOM1303B, RET18h. The final byte selects terrain-changed cells.
  virtual u8 CanDemolishFromDirtChange(
      const GeBounds *bounds, u32 mode, u32 *cost, GeBounds *affected, SpSelection **selection, u8 changed_only) = 0;
  // Original76, SIMGEOM131D7, RET10h. Source reads bounds and publishes the computed cost even after refusal.
  virtual u8 CanDemolishFromDirtChange(u32 layers, const GeBounds *bounds, u32 mode, u32 *cost) = 0;
  // Original84, SIMGEOM1334D, RET18h. Its concrete path reaches an undefined null-output dereference.
  virtual u8 CanDemolishForBuildingSite(
      u32 layers, const GeBounds *bounds, u32 mode, u32 *cost, GeBounds *affected, SpSelection **selection) = 0;
  // Original88, SIMGEOM134FB, RET1Ch. Operates on the supplied manager and publishes cost and affected bounds.
  virtual u8 DemolishOccupants(ScOccupantManager *manager, const GeBounds *bounds, OcIteratorTest *test, u32 mask,
      bool free_of_charge, u32 *cost, GeBounds *affected) = 0;
  // MSVC reverses this overload group: the five-argument form is slot92, the six-argument form slot96.
  // Linux identifies the six inputs as bounds, privilege, free flag, cost, affected bounds and changed-only flag.
  virtual u8 DemolishFromDirtChange(const GeBounds *bounds, u32 privilege, bool free_of_charge, u32 *cost,
      GeBounds *affected, bool changed_only) = 0;
  // Original92, SIMGEOM1392A, RET14h. Preserve actual payment AL rather than inventing a normalized result.
  virtual u8 DemolishFromDirtChange(u32 layers, const GeBounds *bounds, u32 privilege, bool free_of_charge, u32 *cost) = 0;
};

// Actual no-argument constructors registered by this DLL. Missing definitions remain explicit production dependencies.
ScUnknown *GeCreateLayer();
ScUnknown *GeCreateBuildingAttrib();
ScUnknown *GeCreateOverride();
ScUnknown *GeCreateOverrideList();
ScUnknown *GeCreateFloraController();
ScUnknown *GeCreateFloraAttrib();
ScUnknown *GeCreatePortLayer();
ScUnknown *GeCreatePortData();
ScUnknown *GeCreateBuildingController();
ScUnknown *GeCreateBuildingData();
ScUnknown *GeCreateBuilding();
ScUnknown *GeCreateAnimatedBuilding();
ScUnknown *GeCreateBuildingGroupFactory();
ScUnknown *GeCreateDemolition();

class ScDirector;
class ScFramework;
class ScMessageServer;
class ScLanguageManager;
class RsManager;
class RsFileSystem;
class ApGameInterface;
class CtCity;
class OcAttribCache;
class CtAgentTree;
class ScCom;
class ScCommandFactory;
class ApPreferences;
class SpCityWindowView;
class SpViewInterface;
class SpTickManager;
class WnWindowManager;
class WnResources;

// Original SIMGEOM module-director accessor; returns the actual lazy GeDirector singleton.
ScDirector *GeGetDirector();

// Original framework and cached message-service accessors use the concrete module director.
ScFramework *GeGetFramework();
ScMessageServer *GeGetMessageServer();
ScLanguageManager *GeGetLanguageManager();
RsManager *GeGetResourceManager();
RsFileSystem *GeGetFileSystem();
WnWindowManager *GeGetWindowManager();
WnResources *GeGetWindowResources();
ApGameInterface *GeGetApplication();
CtCity *GeGetCity();
SpCityWindowView *GeGetCityWindow();
SpViewInterface *GeGetView();
SpTickManager *GeGetTickManager();
OcAttribCache *GeGetOccupantAttribCache();
CtAgentTree *GeGetAgentTypeTree();
ScCom *GeGetCom();
ScCommandFactory *GeGetClassFactory();
ApPreferences *GeGetPreferences();
iptr GeGetSystemPerformanceInfo();

#endif
