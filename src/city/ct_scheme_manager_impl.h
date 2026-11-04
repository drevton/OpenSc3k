#ifndef SC3_CITY_CT_SCHEME_MANAGER_IMPL_H
#define SC3_CITY_CT_SCHEME_MANAGER_IMPL_H

#include <map>
#include <vector>

#include "city/api.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "resources/resource.h"

class ScString;
class CtSchemeManagerImpl;
class BdManagerView;

void SC3_CDECL CtReadLandscapeScheme(const ScString &key, const ScString &value, void *owner);
void SC3_CDECL CtReadFloraScheme(const ScString &key, const ScString &value, void *owner);
void SC3_CDECL CtReadBuildingScheme(const ScString &key, const ScString &value, void *owner);
void SC3_CDECL CtReadReplaceableScheme(const ScString &key, const ScString &value, void *owner);
void SC3_CDECL CtReadRewardScheme(const ScString &key, const ScString &value, void *owner);

// Installed 40-byte landscape catalog entry, populated by the scheme configuration loader.
struct CtLandscapeScheme {
  u32 id, iconGroup, iconInstance, paletteGroup, paletteInstance, imageGroup, imageInstance;
  u32 navPalette, dataPalette, sound;
};

// Installed 32-byte flora/building entry; both tables use the same key and four image fields.
struct CtContentScheme {
  u32 id, iconGroup, iconInstance, imageGroup, imageInstance, imageGroup2, imageInstance2, sound;
};

// Installed segment record: identifier, retained database segment and owned path String.
struct CtSchemeSegment {
  u32 id;
  RsDatabaseSegment *segment;
  ScString path;
};

// The installed replacement map orders resource keys by group, instance and then type.
struct CtSchemeKeyOrder {
  bool operator()(const RsKey &left, const RsKey &right) const;
};

// SIMINIT scheme receiver; some original load and sprite-cache dependencies remain unresolved.
// Its native arrays preserve catalog behavior but do not claim the installed object's private layout.
class CtSchemeManagerImpl : public CtSchemeManager {
public:
  CtSchemeManagerImpl();
  virtual ~CtSchemeManagerImpl();
  using CtSchemeManager::GetBATBuildings;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool SetCityScheme(u32 landscape, u32 floraSet, u32 buildingSet, bool showProgress) override;
  void SetDefaultCityScheme() override;
  BdPluginManager *GetPluginBuildingMgr() override;
  bool GetCurrentLandScape(u32 *out) const override;
  u32 GetDefaultLandScape() const override;
  u32 GetNumLandScapes() const override;
  void GetLandScapeIds(u32 *ids, u32 capacity, u32 *count) override;
  bool GetLandScapeIconKey(u32 id, RsKey *out) override;
  bool GetLandScapePalette(u32 id, u32 *group, u32 *instance) override;
  bool GetUiImageForLandScape(u32 id, u32 *group, u32 *instance) override;
  bool GetSoundIdForCurrentLandScape(u32 *out) override;
  void GetNavViewTerrainPaletteForCurrentLandScape(u32 *out) override;
  void GetDataMapsTerrainPaletteForCurrentLandScape(u32 *out) override;
  bool GetCurrentFloraSet(u32 *out) const override;
  u32 GetDefaultFloraSet() const override;
  u32 GetNumFloraSets() const override;
  void GetFloraSetIds(u32 *ids, u32 capacity, u32 *count) override;
  bool GetFloraSetIconKey(u32 id, RsKey *out) override;
  bool GetUiImageForFloraSet(u32 id, u32 *group, u32 *instance, u32 *group2, u32 *instance2) override;
  bool GetSoundIdForCurrentFloraSet(u32 *out) override;
  bool GetCurrentBuildingSet(u32 *out) const override;
  u32 GetDefaultBuildingSet() const override;
  u32 GetNumBuildingSets() const override;
  void GetBuildingSetIds(u32 *ids, u32 capacity, u32 *count) override;
  bool GetBuildingSetIconKey(u32 id, RsKey *out) override;
  bool GetUiImageForBuildingSet(u32 id, u32 *group, u32 *instance, u32 *group2, u32 *instance2) override;
  bool GetSoundIdForCurrentBuildingSet(u32 *out) override;
  void LoadBATSegment() override;
  void UnloadBATSegment() override;
  u32 GetNumBATBuildings() override;
  void GetBATBuildingKeys(RsKey *keys, u32 capacity, u32 *count) override;
  void UpdateBATSegment(u32 fileCount, ScStringView **files, u32 removeCount, RsKey *removeKeys) override;
  bool IsReplaceableBuilding(const GeAttrib *attribute) override;
  bool IsRewardBuilding(u32 id) override;
  void GetBATBuildings(OcOverrideContainer *out, u32 lotSize) override;
  iptr GetBATBuildings() override;
  bool MakeBATReplacement(const RsKey &original, const RsKey &replacement) override;
  bool MakeBATReplacement(const RsKey &original, OcOverride *replacement) override;
  bool ClearBATReplacement(const RsKey &original) override;
  bool LoadBATReplacementSet(RsDatabaseSegment *segment, bool clear) override;
  bool SaveBATReplacementSet(RsDatabaseSegment *segment) override;
  bool ImportReplacementSet(ScStringView *path) override;
  void ClearReplacementSet() override;

protected:
  friend void SC3_CDECL CtReadLandscapeScheme(const ScString &, const ScString &, void *);
  friend void SC3_CDECL CtReadFloraScheme(const ScString &, const ScString &, void *);
  friend void SC3_CDECL CtReadBuildingScheme(const ScString &, const ScString &, void *);
  friend void SC3_CDECL CtReadReplaceableScheme(const ScString &, const ScString &, void *);
  friend void SC3_CDECL CtReadRewardScheme(const ScString &, const ScString &, void *);
  // Original configuration-reader return is unconsumed; this declaration exposes only its recovered effects.
  void LoadConfiguration();
  // Original reward-reader return is unconsumed; this declaration exposes its recovered effects.
  void LoadRewardConfiguration();
  bool LoadFloraSet(u32 selection, RsKeyListInterface **keys);
  bool LoadBuildingSet(u32 selection, RsKeyListInterface **keys);
  bool RebuildOccupantAttribs(RsKeyListInterface *keys);
  // Original private progress helper's nominal return is unconsumed by its callers.
  void UpdateProgress(u32 increment);
  // The original private refresh return is unconsumed; its resource and city effects are retained directly.
  void RefreshBuildingAssets();
  // Original private return is unconsumed here; the segment-unload effects remain an explicit dependency.
  void UnloadBuildingSegments(RsKeyListInterface **removed);
  // The flora counterpart has the same unload sequence and an independently owned segment vector.
  void UnloadFloraSegments(RsKeyListInterface **removed);
  void UnloadSegments(std::vector<CtSchemeSegment> *segments, RsKeyListInterface **removed);
  // Forwards the sprite-cache byte result, or returns zero when the cache is absent.
  // Its nominal Boolean/BYTE spelling and the owning cache method remain unresolved.
  CtLandscapeScheme *FindLandscape(u32 id);
  CtContentScheme *FindFlora(u32 id);
  CtContentScheme *FindBuilding(u32 id);
  u32 currentLandscape = 0, defaultLandscape = 0;
  u32 references = 0;
  u32 progressTotal = 0, progressCurrent = 0;
  u32 currentFlora = 0, defaultFlora = 0;
  u32 currentBuilding = 0, defaultBuilding = 0;
  bool floraLoaded = false, buildingLoaded = false, progressVisible = false;
  BdPluginManager *pluginManager = nullptr;
  RsManager *resourceManager = nullptr;
  OcOverrideContainer *batContainer = nullptr;
  BdManagerView *architect = nullptr;
  ScArray<CtLandscapeScheme> landscapes;
  ScArray<CtContentScheme> flora;
  std::vector<CtSchemeSegment> floraSegments;
  ScArray<CtContentScheme> buildings;
  std::vector<CtSchemeSegment> buildingSegments, batSegments;
  ScArray<RsKey> batKeys;
  std::map<RsKey, RsKey, CtSchemeKeyOrder> replacements;
  ScArray<u32> rewards;
  ScArray<RsKey> replaceable;
};

static_assert(sizeof(CtLandscapeScheme) == 40);
static_assert(sizeof(CtContentScheme) == 32);

#endif
