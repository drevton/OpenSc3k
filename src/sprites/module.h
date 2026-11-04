#ifndef SC3_SPRITES_MODULE_H
#define SC3_SPRITES_MODULE_H

#include "city/api.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/sprite.h"

// Primary identity of the DAT provider constructed at 1001D8F7. The file and codec identities are separate bases.
class SpImageSource : public ScUnknown {
public:
  virtual bool Init(u32 identifier, const ScStringView *name, bool resolved) = 0; // Original12.
  virtual bool Shutdown() = 0;                                                    // Original16.
  // Original20 decodes into an existing image; its separate stream contract remains unported.
  virtual u8 ReadImage(GxBuffer **out, const RsKey *key) = 0;  // Original24.
  virtual u8 ReadMetadata(SpImage *out, const RsKey *key) = 0; // Original28.
};

struct SpImageEntry {
  u32 group, resource;
  SpImage *image;
  u32 next;
};

// One SII file's path and map; records are stable allocations shared by frame definitions.
// Project arrays replace only generic hash storage, retaining resource-based buckets and first-inserted key ownership.
struct SpImageMap {
  SpImageMap *next, *previous;
  ScString path;
  ScArray<SpImageEntry> entries;
  ScArray<u32> buckets;
  SpImageMap();
  ~SpImageMap();
  SpImage *Find(u32 resource, u32 group);
  void Rehash(u32 count);
  u32 First();
  u32 Next(u32 index);
  void Insert(u32 resource, u32 group, SpImage *image);
};

struct SpImageProvider {
  SpImageProvider *next, *previous;
  SpImageSource *source;
};

// The module has two original circular list sentinels and a display color key, shared by every service receiver.
extern SpImageMap *SpImageMaps;
extern SpImageProvider *SpImageProviders;
extern u32 SpImageColorKey;

// Recovered secondary reference identity compiled into SIMSPR. Counts retain the owning sprite module directly.
class SpModuleObject : public ScUnknown {
public:
  SpModuleObject();
  virtual ~SpModuleObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Actual system-service base and separate module identity, preceding the cache's own initialization state.
class SpService : public ScSystemService, public SpModuleObject {
public:
  SpService(u32 identifier, i32 priority);
  ~SpService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool running;
};

// Cache, framework-service and module identities share one original count; lookup records remain module globals.
class SpImageStore : public SpImageCache, public SpService {
public:
  SpImageStore();
  ~SpImageStore() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  u8 RealShutdown() override;
  u8 IsInitialized() const override;
  u8 AddSpriteInfoFileToSearchList(const ScStringView &path, u8 append) override;
  u8 RemoveSpriteInfoFileToSearchList(const ScStringView &path) override;
  u8 AddDBSegmentSpriteToSearchList(const ScStringView &path, u32 append) override;
  u8 AddDBSegmentSpriteToSearchList(SpImageSource *source, u32 append) override;
  u8 RemoveDBSegmentSpriteToSearchList(const ScStringView &path) override;
  u8 RemoveDBSegmentSpriteToSearchList(SpImageSource *source, u8 destroy) override;
  // Original10012E2B discovery uses real directory enumeration and the direct registration methods below.
  void DiscoverSources();
  u8 AddIndex(const ScStringView *path, u8 append);
  u8 AddDatPath(const ScStringView *path, u32 append);
  u8 RemoveIndex(const ScStringView *path);
  u8 RemoveDatPath(const ScStringView *path);
  SpImage *GetImageInfo(u32 resource, u32 group) override;
  u8 PrepareImage(u32 resource, u32 group, SpImage **out) override;
  u32 UnprepareImage(u32 resource, u32 group) override;
  u8 initialized;
  ScLock lock;
};

SpImageStore *SpGetImageStore();
GxBuffer *SpLoadImage(u32 resource, u32 group);
int SpLoadImageMaps();
u8 SpReadImageMap(const ScStringView *path, SpImageMap *map, u32 argument);
u8 SpImportImageMap(const char *source, SpImageMap *map, u32 argument);
u8 SpReadImageLine(const char **source, ScStringView *out);

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3CityViewIso.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class SpManagerView;

class SpIsoView : public SpViewInterface {

public:
  virtual SpManagerView *GetCitySpriteManager() = 0;
  virtual SpCellMap *GetCitySpriteCellMap() = 0;
  virtual bool DoScreenShake(i32, i32, i32, i32, bool, i32) = 0;
  virtual bool IsScreenShaking() = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// SPDX-License-Identifier: LGPL-2.1-or-later
// Declarations adapted from cISC3CitySpriteManager.h, copyright2026 Nicholas Hayes.

class SpSprite;
class OcIteratorTest;
class UiCityCursor;
class ScDirtBag;
class CtZoneLayer;
class CtAuraLayer;
class CtCrimeLayer;
class CtLandValueLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class EnHazardMap;
class EcLayer;
class EcValveLayer;
class NwTrafficLayer;
class NwManager;
class SpEmptyTest;
class SpSurfaceTest;
class SpSubwayTest;
class SpPipeTest;
class SpFlammabilityTest;
class SpCrimeTest;
class SpTrafficTest;
class SpPowerTest;
class OcAnimManager;
class CtPoliceLayer;
class CtFireLayer;
class SpNeighborDisplay;
class SpEffects;
class SpPowerDisplay;
class SpRadiationDisplay;
class SpWeatherView;

// Complete SDK city-sprite-manager interface, retaining the installed four-argument removal correction.
// Mutable outputs use explicit pointers; the concrete producer has separate message and change-receiver identities.
class SpManagerView : public ScUnknown {
public:
  virtual bool Init(CtCity *, SpCellMap *) = 0;
  virtual bool Shutdown() = 0;
  virtual void GetViewID(int *) const = 0;
  virtual void SetViewID(int, bool) = 0;
  virtual void SetCityViewFlag(u32, bool, bool) = 0;
  virtual bool GetCityViewFlag(u32) const = 0;
  virtual void SetCityViewFlags(u32, bool) = 0;
  virtual u32 GetCityViewFlags() const = 0;
  virtual bool TestCityViewFlags(u32) const = 0;
  virtual void ClearCityViewFlags() = 0;
  virtual bool OccupantInserted(u32, ScOccupant *) = 0;
  virtual bool OccupantRemoved(u32, ScOccupant *) = 0;
  virtual bool OccupantMoved(u32, ScOccupant *, CtCoord const &) = 0;
  virtual bool OccupantChanged(u32, ScOccupant *, int) = 0;
  virtual bool OccupantChanged(u32, ScOccupant *, int, void *) = 0;
  virtual bool GetUnalignedOccupantsInVolume(GeBounds const &, ScOccupant **, u32 *) = 0;
  virtual bool InsertCellAlignedSprite(ScOccupant *, bool) const = 0;
  virtual bool RemoveCellAlignedSprite(ScOccupant *) const = 0;
  virtual bool RemoveAndInsertCellAlignedSprites(ScOccupant *pSpriteToRemove, ScOccupant *pSpriteToInsert) const = 0;
  virtual bool ChangeCellAlignedSprite(ScOccupant *, u32, void *, u32) const = 0;
  virtual bool ChangeCellAlignedSpriteInst(const CtCoord &position, u32 flags, void *payload) const = 0;
  virtual bool InsertUnalignedSprite(ScOccupant *, int, u32) const = 0;
  virtual bool InsertUnalignedSprite(ScOccupant *, int, int, int, int, u32) const = 0;
  virtual bool RemoveUnalignedSprite(ScOccupant *) const = 0;
  virtual bool RemoveUnalignedSprite(ScOccupant *, int, int, int) const = 0;
  virtual bool MoveUnalignedSprite(ScOccupant *, CtCoord const &) const = 0;
  virtual bool MoveUnalignedSprite(ScOccupant *, int, int, int, int, int, int) const = 0;
  virtual bool ChangeUnalignedSprite(ScOccupant *, u32, void *) const = 0;
  virtual bool ChangeUnalignedSprite(ScOccupant *, CtCoord const &, u32, void *) const = 0;
  virtual bool ChangeUnalignedSprite(ScOccupant *, int, int, u32, void *) const = 0;
  virtual bool InsertUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, i32 priority, u32 flags) const = 0;
  virtual bool RemoveUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position) const = 0;
  virtual bool MoveUnalignedSpriteInst(SpSprite *, CtCoord const &, CtCoord const &) const = 0;
  virtual bool ChangeUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, u32 flags, void *payload) const = 0;
  virtual ScOccupantManager *GetCurrentOccupantManager() = 0;
};

// Recovered SIMSPR manager receiver with its three original interface identities and direct lifecycle/message calls.
// Effect construction and the plumbing change-mask operation remain explicit dependencies; fields are native storage.
class SpManager : public SpManagerView, public ScMessageTarget, public CtChangeReceiver {
public:
  SpManager();
  virtual ~SpManager();
  // Original private extension156 changes only the grid bit, with the source's no-redraw flag update.
  virtual void SetGrid(bool enabled);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  virtual bool CellChanged(u32 sender, const u32 *coordinates, u32 count);
  bool Init(CtCity *city, SpCellMap *map) override;
  bool Shutdown() override;
  void GetViewID(int *) const override;
  void SetViewID(int, bool) override;
  void SetCityViewFlag(u32, bool, bool) override;
  bool GetCityViewFlag(u32) const override;
  void SetCityViewFlags(u32, bool) override;
  u32 GetCityViewFlags() const override;
  bool TestCityViewFlags(u32) const override;
  void ClearCityViewFlags() override;
  bool OccupantInserted(u32, ScOccupant *) override;
  bool OccupantRemoved(u32, ScOccupant *) override;
  bool OccupantMoved(u32, ScOccupant *, CtCoord const &) override;
  bool OccupantChanged(u32, ScOccupant *, int) override;
  bool OccupantChanged(u32, ScOccupant *, int, void *) override;
  bool GetUnalignedOccupantsInVolume(GeBounds const &, ScOccupant **, u32 *) override;
  bool InsertCellAlignedSprite(ScOccupant *, bool) const override;
  bool RemoveCellAlignedSprite(ScOccupant *) const override;
  bool RemoveAndInsertCellAlignedSprites(ScOccupant *pSpriteToRemove, ScOccupant *pSpriteToInsert) const override;
  bool ChangeCellAlignedSprite(ScOccupant *, u32, void *, u32) const override;
  bool ChangeCellAlignedSpriteInst(const CtCoord &position, u32 flags, void *payload) const override;
  bool InsertUnalignedSprite(ScOccupant *, int, u32) const override;
  bool InsertUnalignedSprite(ScOccupant *, int, int, int, int, u32) const override;
  bool RemoveUnalignedSprite(ScOccupant *) const override;
  bool RemoveUnalignedSprite(ScOccupant *, int, int, int) const override;
  bool MoveUnalignedSprite(ScOccupant *, CtCoord const &) const override;
  bool MoveUnalignedSprite(ScOccupant *, int, int, int, int, int, int) const override;
  bool ChangeUnalignedSprite(ScOccupant *, u32, void *) const override;
  bool ChangeUnalignedSprite(ScOccupant *, CtCoord const &, u32, void *) const override;
  bool ChangeUnalignedSprite(ScOccupant *, int, int, u32, void *) const override;
  bool InsertUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, i32 priority, u32 flags) const override;
  bool RemoveUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position) const override;
  bool MoveUnalignedSpriteInst(SpSprite *, CtCoord const &, CtCoord const &) const override;
  bool ChangeUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, u32 flags, void *payload) const override;
  ScOccupantManager *GetCurrentOccupantManager() override;

  u8 OccupantIsDirtClod(SpSprite *sprite, bool require_all) const;
  u8 FindHighlightColor(u32 min_x, u32 min_z, u32 max_x, u32 max_z, u32 *color) const;
  u8 CreateTerrainSprite(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainCursor(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainZone(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainAura(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainCrime(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainLandValue(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainPollution(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainFlammability(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainPopulation(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainTraffic(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainPower(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainPipe(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  u8 CreateTerrainSubway(u32 x, u32 z, u8 *altitude, SpSprite **out) const;
  void UpdateTerrainQuad(u32 x, u32 z, bool update) const;
  void UpdateTerrainMesh(u32 min_x, u32 min_z, u32 max_x, u32 max_z, bool update) const;
  // Recovered private effects; the ignored nominal return declarations remain unavailable.
  void AcquireOccTests();
  void SetupViewFilters();
  // Installed private entries define their AL result; literal bool/BYTE declarations remain unavailable.
  u8 InitEdges();
  u8 ShutdownEdges();
  u8 SetEdgeCompass(i32 compass);
  u8 GetEdgeElementCoords(bool first, u32 index, u32 *x, u32 *z);
  u8 IsEdge(u32 x, u32 z, bool *first, u32 *index);
  u8 AddEdgeElement(bool first, u32 index, u32 x, u32 z);
  u8 RemoveEdgeElement(bool first, u32 index, u32 x, u32 z);
  u8 UpdateEdgeElement(bool first, u32 index, u32 x, u32 z);

  // Private control helpers preserve their reached effects; ignored nominal return declarations remain unresolved.
  void SetOccupantView(ScOccupantManager *manager, OcIteratorTest *terrain_filter, OcIteratorTest *animation_filter,
      bool zone_update, bool force);
  void ShowOrHideOccupants(ScOccupantManager *manager, OcIteratorTest *filter, bool zone_update);
  void ShowAllOccupants(ScOccupantManager *manager, bool show);
  void DoDataViewChange();
  // Actual private operations; construction and plumbing's unrecovered change-mask interface remain explicit dependencies.
  void InitCityViewEffects();
  void ShutdownCityViewEffects();
  void RefreshPipeCells();
  // These private entries define their raw AL result; original bool/BYTE spelling remains unresolved.
  u8 CellNormallyHasZoneColor(u32 x, u32 z);
  u8 Register();
  u8 RegisterTerrain();
  u8 Unregister();

  u32 references = 0;
  CtCity *city;
  SpCellMap *cell_map;
  ScOccupantManager *current_occupants;
  OcIteratorTest *occupant_filter;
  UiCityCursor *cursor;
  ScDirtBag *dirt;
  CtZoneLayer *zone;
  CtAuraLayer *aura;
  CtCrimeLayer *crime;
  CtLandValueLayer *land_value;
  CtPowerLayer *power;
  CtPlumbingLayer *plumbing;
  EnHazardMap *flammability;
  EcLayer *pollution;
  EcValveLayer *valves;
  NwTrafficLayer *traffic;
  NwManager *networks;
  ScOccupantManager *surface_occupants, *pipe_occupants, *subway_occupants;
  OcAnimManager *animated_occupants;
  CtPoliceLayer *police;
  CtFireLayer *fire;
  u32 *cell_bits;
  u32 city_width, city_height;
  u32 surface_sender, pipe_sender, subway_sender, animation_sender, zone_sender, cursor_sender, change_sender;
  SpNeighborDisplay *surface_effects, *pipe_effects, *subway_effects;
  SpEffects *effects;
  SpPowerDisplay *power_display;
  SpRadiationDisplay *radiation_display;
  SpWeatherView *weather_sim;
  u8 flag16, flag216, flag117, flag218, animations_visible, animations_applied;
  SpEmptyTest *empty_test, *spare_test;
  SpSurfaceTest *surface_test;
  SpSubwayTest *subway_test;
  SpPipeTest *pipe_test;
  SpFlammabilityTest *flammability_test;
  SpCrimeTest *crime_test;
  SpTrafficTest *traffic_test;
  SpPowerTest *power_test;
  OcIteratorTest *road_test, *rail_test, *network_test, *view_filter;
  u32 pollution_maximum;
  f32 pollution_scale, pollution_threshold;
  i32 view_id;
  u32 view_flags, sprite_layer;
  u8 invalidate_changes;
  // Original +117 is independent of the dry-only switch at +217 used by terrain calls.
  u8 cursor_visible, terrain_kind, dry_only;
  u16 good_cursor_color, bad_cursor_color;
  i32 edge_compass = -1;
  u32 edge_first_count = 0, edge_second_count = 0;
  SpSprite **edge_sprites = nullptr;
};

i32 SpLayerPriority(i32 layer, u32 flags);

#endif
