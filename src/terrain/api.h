#ifndef SC3_TERRAIN_API_H
#define SC3_TERRAIN_API_H

#include "city/ct_layer.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/ge_types.h"

// Terrain cheat identities follow the paired name/ID registration arrays.
enum TrCheatId : u32 {
  kTrLoadTerrainCheatId = 0xc31cc15e,
  kTrSaltOnCheatId = 0x831cc164,
  kTrSaltOffCheatId = 0xa31cc169,
  kTrTerrainOneUpCheatId = 0xa31cf2ef,
  kTrTerrainOneDownCheatId = 0xc31cf2f6,
  kTrTerrainTenUpCheatId = 0xc31cf2fa,
  kTrTerrainTenDownCheatId = 0x831cf2ff,
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Installed SIMDIRT QI1000EBE0 returns the same primary receiver for message-target IID58D and the terrain interfaces.
// Group the complete proven CityLayer/message prefix without another subobject; the SDK direct-Unknown hierarchy is unproved.
class ScDirtBag : public CtLayer {
public:
  bool DoMessage(ScMessage *message) override = 0; // DirtBag +12.
  // These declarations override the existing CityLayer prefix and do not introduce additional slots.
  virtual bool DoQueryInfo(ScMessage *, ScUnknown *) override = 0;
  virtual bool StaticInit(CtDefinition *) override = 0;
  virtual bool StaticShutdown() override = 0;
  virtual bool Init(CtCity *) override = 0;
  virtual bool Init(CtCity *, ScImporter *) override = 0;
  virtual bool Init(CtCity *, RsDatabaseSegment *) override = 0;
  virtual bool Save(CtCity *, RsDatabaseSegment *) override = 0;
  virtual bool SimulationBegin() override = 0;
  virtual bool SimulationEnd() override = 0;
  virtual bool Shutdown() override = 0;
  virtual bool GetManipulator(CtAgentType const &, CtLayerManipulator **, i32 *) override = 0;
  virtual u32 GetLayerType() override = 0;
  virtual bool DebugClassTag(ScStringView *) override = 0;
  virtual bool DebugTypeTag(ScStringView *) override = 0;
  // The installed primary table has altitude operations at72/76/80, water predicate84, count88 and real-water92.
  virtual void GetVertexAltitude(u32 x, u32 z, u8 *altitude) const = 0;
  virtual void GetVertexAltitudeDirt(u32 x, u32 z, u8 *altitude) const = 0;
  virtual void GetVertexAltitudeWater(u32, u32, u8 *) const = 0;
  virtual bool IsWater(u32 x, u32 z) const = 0;
  virtual u8 CellWaterVertCount(u32, u32) const = 0;
  virtual bool IsRealWater(u32 x, u32 z) const = 0;
  virtual u8 GetGlobalSeaLevel() const = 0;
  virtual void SetGlobalSeaLevel(u8 level) = 0;
  virtual void GetVertexLight(u32, u32, u8 *) const = 0;
  virtual u8 GetDirtClod(u8, u32, u32, bool, SpSprite **, u8 *, u8) const = 0;
  virtual u8 GetDirtClodSingle(u8, u16, u32, u32, bool, SpSprite **, u8 *) const = 0;
  virtual u8 GetDirtClodEdge(u32, u32, bool, bool, SpSprite **, u8 *) const = 0;
  virtual void RegenPalette(u8) = 0;
  virtual const ScStringView *GetQueryString(u32, u32) const = 0;
  // MSVC emits each overload group in reverse declaration order: rectangle128/136 and cell132/140.
  virtual bool SetVertexAltitude(u32 x, u32 z, u8 altitude) = 0;
  virtual bool SetVertexAltitude(u32, u32, u32, u32, u8) = 0;
  virtual bool SetWaterTable(u32 x, u32 z, u8 altitude, bool shore) = 0;
  virtual bool SetWaterTable(u32, u32, u32, u32, u8) = 0;
  virtual bool InitFloraLevel(u32 x, u32 z, u8 flora) = 0;
  virtual bool GetGrid() const = 0;
  virtual void SetGrid(bool) = 0;
  virtual void Tick(i32, i32, ScRect const &, u32) = 0;
  virtual bool GenerateAltFromBMP(ScStringView *) = 0;
  virtual bool RecalcSaltWater() = 0;
  virtual u32 CellCountX() const = 0;
  virtual u32 CellCountZ() const = 0;
  virtual u32 VertexCountX() const = 0;
  virtual u32 VertexCountZ() const = 0;
  // Unlimited6EA0 defines AL only (XOR AL,AL); the SDK DWORD result reads undefined upper bits.
  virtual u8 MinAltitudeAllowed() const = 0;
  // Unlimited6E90 defines AL only (OR AL,FF); preserve the existing byte-returning producer.
  virtual u8 MaxAltitudeAllowed() const = 0;
  // Unlimited6EB0 returns MOV AL,4, with no defined upper-register result.
  virtual u8 MaxAltitudeDeltaAllowed() const = 0;
  // UnlimitedEAB0 preserves only BL across Unlock and returns AL; upper EAX is not the altitude result.
  virtual u8 GetAverageAltitude() const = 0;
  virtual f32 AltitudeScale() const = 0;
  virtual bool InCellBounds(u32, u32) const = 0;
  virtual bool InCellBounds(u32, u32, u32, u32) const = 0;
  virtual bool InVertexBounds(u32, u32) const = 0;
  virtual bool InVertexBounds(u32, u32, u32, u32) const = 0;
  virtual bool IsValidVertexAltitude(u8) const = 0;
  virtual bool IsValidVertexLight(u8) const = 0;
  virtual u8 AltitudeMax() const = 0;
  virtual u8 AltitudeMin() const = 0;
  virtual u8 MaxAltitudeDelta() const = 0;
  virtual bool InstallFunctionFilter(bool (*)(GeBounds const &, u32, i32, void *), void *, bool) = 0;
  virtual bool CanRaiseTerrain(GeBounds const &, GeBounds *, u32 *) const = 0;
  virtual bool CanLowerTerrain(GeBounds const &, GeBounds *, u32 *) const = 0;
  virtual bool CanLevelTerrain(GeBounds const &, u8, GeBounds *, u32 *) const = 0;
  virtual bool GetOptimalLevel(GeBounds const &, u8 *, GeBounds *, u32 *) const = 0;
  virtual bool CanPrepareForRoad(SpSelection *, GeBounds *, u32 *) const = 0;
  virtual bool CanPrepareForHighway(SpSelection *, GeBounds *, u32 *) const = 0;
  virtual bool CanPlaceWater(GeBounds const &, GeBounds *, u32 *) const = 0;
  virtual bool CanPlaceWater(SpSelection *, GeBounds *, u32 *) const = 0;
  virtual bool CanSetupZone(GeBounds const &, u32 *, bool) const = 0;
  virtual bool RaiseTerrain(GeBounds const &, GeBounds *, u32 *, bool) = 0;
  virtual bool LowerTerrain(GeBounds const &, GeBounds *, u32 *, bool) = 0;
  virtual bool LevelTerrain(GeBounds const &, u8, GeBounds *, u32 *, bool) = 0;
  virtual bool PrepareForRoad(SpSelection *, GeBounds *, u32 *, bool) = 0;
  virtual bool PrepareForHighway(SpSelection *, GeBounds *, u32 *, bool) = 0;
  virtual bool PlaceWater(GeBounds const &, GeBounds *, u32 *, bool) = 0;
  virtual bool PlaceWater(SpSelection *, GeBounds *, u32 *, bool) = 0;
  virtual bool SetupZone(GeBounds const &, u32 *, bool, bool) = 0;
  virtual void ResetChanges() const = 0;
  virtual void MarkVertexChanged(u32, u32) const = 0;
  virtual void MarkCellChanged(u32, u32) const = 0;
  virtual bool ChangedByLastOperation(u32, u32) const = 0;
  virtual void LockUpdates(bool) = 0;
  virtual void RecomputeSeaportMap() = 0;
  virtual bool Distance(u32, u32, u32 *) const = 0;
  virtual bool GetWaterPath(u32, u32, u32, ScPointXZ *, u32 *) const = 0;
  virtual u32 GetPathGranularity() const = 0;
  virtual void BlockCell(u32, u32, bool) = 0;
  virtual bool IsCellBlocked(u32, u32) const = 0;
  virtual bool IsDeepWaterPresent() = 0;
  virtual bool IsNeighborPresent(u8) const = 0;
};

class ScMessageServer;
class ScDirector;
class ScCom;
class SpCityWindowView;
class SpViewInterface;
class ScFramework;
class GxGraphics;
class ScLock;
class ApGameInterface;
class CtCity;
class RsFileSystem;
class RsManager;
class ScLanguageManager;
class WnWindowManager;

// Original SIMDIRT module lock and message-server getter.
// The concrete module director owns the actual class registrations and reference lifetime.
extern ScLock TrTerrainLock;
extern f32 TrAltitudeScale;
// Native effects of shared private1001BD62; its discarded lock-pointer return is not a game result.
void TrAdjustReference(u32 *counter, i32 delta);
ScMessageServer *TrGetMessageServer();
ScFramework *TrGetFramework();
ScDirector *TrGetDirector();
GxGraphics *TrGetGraphics();
ApGameInterface *TrGetApplication();
CtCity *TrGetCity();
SpCityWindowView *TrGetCityWindow();
SpViewInterface *TrGetCurrentView();
ScCom *TrGetCom();
RsFileSystem *TrGetFileSystem();
RsManager *TrGetResourceManager();
ScLanguageManager *TrGetLanguageManager();
WnWindowManager *TrGetWindowManager();

// Original IID20685AA3 factory base retains child factories and falls back to its real derived creation method.
class TrFactory : public ScCommandFactory {
public:
  TrFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 class_id, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~TrFactory();
  virtual bool CreateCommand(u32 class_id, u32 iid, void **out) = 0;

protected:
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// Concrete tool factory has no extra fields and creates the four actual terrain commands through their recovered methods.
class TrToolFactory final : public TrFactory {
public:
  bool CreateCommand(u32 class_id, u32 iid, void **out) override;
};

// InstallFunctionFilter is an original public cross-DLL callback ABI; its caller-owned context is genuinely opaque.
using TrFunctionFilter = bool (*)(const GeBounds &bounds, u32 cost, i32 kind, void *context);

struct TrFilterLink {
  TrFilterLink *next, *previous;
  TrFunctionFilter filter;
  void *context;
};

// Native form of the original local module-lock guard; its destructor also runs during genuine C++ unwinding.
class TrLockScope {
public:
  // Acquire the same recursive terrain lock before the guarded method enters its game or external-service work.
  TrLockScope() {
    TrTerrainLock.Lock();
  }

  // Release the terrain lock after all later local objects have been destroyed.
  ~TrLockScope() {
    TrTerrainLock.Unlock();
  }

  TrLockScope(const TrLockScope &) = delete;
  TrLockScope &operator=(const TrLockScope &) = delete;
};

#endif
