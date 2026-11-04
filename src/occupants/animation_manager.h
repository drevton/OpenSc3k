#ifndef SC3_OCCUPANTS_ANIMATION_MANAGER_H
#define SC3_OCCUPANTS_ANIMATION_MANAGER_H

#include <list>
#include <vector>

#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/ge_types.h"
#include "occupants/api.h"
#include "sprites/sp_tick_manager.h"

class SpManagerView;

// Retail OccupantInfo type47C3 stores an occupant identity and its independent visibility, flags and sort words.
struct OcAnimationInfo {
  ScOccupant *occupant;
  mutable u8 visibility, flags;
  mutable u16 sort_id;
};

// Occupancy nodes use the original pointer hash, per-bucket links and traversal order.
struct OcAnimationNode {
  OcAnimationNode *next;
  OcAnimationInfo info;
};

// This table is specific to animated occupants. Its bucket traversal and growth follow the executable.
class OcAnimationTable {
public:
  explicit OcAnimationTable(u32 minimum_buckets);
  ~OcAnimationTable();
  OcAnimationNode *Find(ScOccupant *occupant) const;
  OcAnimationNode *Insert(const OcAnimationInfo &entry);
  void Erase(OcAnimationNode *node);
  void Erase(ScOccupant *occupant);
  void Clear();
  OcAnimationNode *First() const;
  OcAnimationNode *Next(const OcAnimationNode *node) const;

private:
  void Grow(u32 minimum_buckets);
  // Original empty hash, equality and key-extractor objects occupy three bytes before the bucket vector.
  u8 hash_state, equal_state, key_state;
  std::vector<OcAnimationNode *> buckets;
  u32 count;
};

// Retail cSC3OccupantManagerAnim::TickEntry type49F8 owns no reference by itself.
struct OcAnimationTickEntry {
  SpTickable *object;
  u8 priority, flags;
};

// Retail type4ACC proves these four interface bases and the original named state.
// The original _hash_set, vector and list use is established by types47F2, 4840, 49BD and 4A36.
// Occupancy traversal uses the recovered bucket order; remaining manager operations are still explicit dependencies.
class OcAnimationManager : public OcAnimManager, public SpTickable, public ScMessageTarget, public SpTickManager {
public:
  OcAnimationManager();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city, u32 id) override;
  bool Shutdown() override;
  u32 GetCellsInX() override;
  u32 GetAnimCellsInX() override;
  f32 GetWorldUnitsInX() override;
  u32 GetCellsInZ() override;
  u32 GetAnimCellsInZ() override;
  f32 GetWorldUnitsInZ() override;
  u32 GetCellsInY() override;
  u32 GetAnimCellsInY() override;
  f32 GetWorldUnitsInY() override;
  u32 GetSenderId() const override;
  bool DoMessage(ScMessage *message) override;
  bool RegisterChangeReceiver(CtChangeReceiver *receiver, u32 *sender, bool post) override;
  bool UnregisterChangeReceiver(CtChangeReceiver *receiver) override;
  bool UnregisterAllChangeReceivers() override;
  using OcAnimManager::InsertOccupant;
  using OcAnimManager::RemoveOccupant;
  using OcAnimManager::ShowOccupant;
  bool InsertOccupant(ScOccupant *occupant, i32 layer, u32 flags, bool visible) override;
  bool InsertOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y, i32 layer, u32 flags, bool visible) override;
  void RemoveOccupant(ScOccupant *occupant) override;
  void RemoveOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y) override;
  bool MoveOccupant(ScOccupant *occupant, CtCoord *position) override;
  bool MoveOccupant(ScOccupant *occupant, i32 old_x, i32 old_z, i32 old_y, i32 x, i32 z, i32 y) override;
  bool RefreshOccupant(ScOccupant *occupant, i32 flags, void *payload) override;
  bool RefreshOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y, i32 flags, void *payload) override;
  bool ShowOccupant(ScOccupant *occupant, bool visible) override;
  bool ShowOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y, bool visible) override;
  bool SetTest(OcIteratorTest *test, bool update) override;
  bool IsOccupantVisible(ScOccupant *occupant) override;
  bool IsOccupantVisible(ScOccupant *occupant, bool check_occlusion) override;
  void SetShouldTick(ScOccupant *occupant, bool enabled) override;
  bool GetShouldTick(ScOccupant *occupant) override;
  bool RemoveOccupants(const GeBounds *bounds, OcIteratorTest *test) override;
  void RemoveAllOccupants() override;
  void ShowOrHideOccupants(OcIteratorTest *test) override;
  bool GetIterator(OcIterator **out) override;
  bool GetOccupants(OcIterator **out, const GeBounds *bounds, OcIteratorTest *test) override;
  bool IsInBoundsCell(u32 x, u32 z) override;
  bool IsInBoundsAnimCell(u32 x, u32 z) override;
  bool IsInBoundsWorld(f32 x, f32 z) override;
  bool IsInBounds(i32 x, i32 z, i32 y) override;
  bool IsInBounds(const CtCoord &position) override;
  bool GetOccupantsInVolume(const GeBounds &bounds, ScOccupant **out, u32 *count) override;
  u8 GetAnimationSystemLoad() const override;
  bool AddTickableObject(SpTickable *object) override;
  bool RemoveTickableObject(SpTickable *object) override;
  void DoAnimationTick(u32 elapsed) override;
  void DoAnimationTick() override;
  u32 PauseAnimationSystem(bool pause) override;
  bool IsAnimationSystemPaused() const override;
  void StepAnimationSystem() override;
  u32 GetCurrentTickTime() const override;
  void SetCriticalExecutionPriority(bool critical) override;
  void SetNewPriority(SpTickable *object, u8 priority, i32 interval) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  virtual bool PostOccupantAll(CtChangeReceiver *receiver);
  virtual bool PostOccupantAll(ScMessageTarget *target) const;

protected:
  virtual void DoDeferredAdditionsAndRemovals();
  u8 GetActualAnimationSystemLoad();

public:
  virtual ~OcAnimationManager();

private:
  friend class OcAnimationIterator;
  u8 initialized;
  u32 references, cell_size, max_x, max_z, max_y;
  OcIteratorTest *visibility_test, *default_visibility_test;
  OcAnimationTable occupants;
  std::vector<OcAnimationInfo> deferred_additions, deferred_removals;
  i8 iterating;
  SpManagerView *sprite_manager;
  SpCellMap *sprite_cell_map;
  ScLock lock;
  u32 sender_id;
  std::list<CtChangeReceiver *> receivers;
  std::list<OcAnimationTickEntry> tick_high, tick_most_first, tick_most_second, tick_few, tick_disabled;
  u32 last_high, last_most_first, last_most_second, last_few, frame_count;
  i32 pause_count;
  f32 fps;
  ScStopwatch tick_timer;
  u8 step_next_frame, current_load, execution_regular[16], execution_critical[16];
  u8 *execution_table;
  u32 addition_count;
  i32 critical_execution_count;
};

// Retail type4D46 stores the manager, optional test, bounds and current hash iterator.
// Iteration follows the executable's bucket chain and applies filters only while advancing.
class OcAnimationIterator : public OcIterator {
public:
  explicit OcAnimationIterator(OcAnimationManager *manager);
  virtual ~OcAnimationIterator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(const GeBounds *bounds, OcIteratorTest *test) override;
  bool Shutdown() override;
  ScOccupant *Reset() override;
  ScOccupant *Current() override;
  ScOccupant *Next() override;

private:
  u32 references;
  OcAnimationManager *manager;
  OcIteratorTest *test;
  GeBounds bounds;
  OcAnimationNode *current;
  OcAnimationTable *current_table;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScOccupant;

// Original occupant-manager iterator filter, IID A1C085DB; its Test method occupies source slot twelve.
class OcIteratorTest : public ScUnknown {
public:
  virtual bool Test(ScOccupant *occupant) = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

struct CtCoord;
class CtCity;
class CtAgent;
class BdBuilding;
struct GeCellRect;
struct GeBounds;
class CtCity;
class OcPersistInfo;
class OcIterator;
class OcIteratorTest;
class ScMessageTarget;
class CtChangeReceiver;
class GeAttrib;
class SpSprite;

// Recovered occupant operations group the installed same-primary message/query prefix without adding an interface view.
// Public order follows SDK and installed SIMGEOM1002B058; historical C++ inheritance spelling remains unproved.
// Preserve installed byte dimensions/classifiers, WORD animation altitude and void shutdown despite SDK disagreements.
class ScOccupant : public ScQueryTarget {
public:
  bool DoMessage(ScMessage *) override = 0;
  bool DoQueryInfo(ScMessage *, ScUnknown *) override = 0;
  virtual bool Init(GeAttrib *) = 0;
  virtual bool Init(RsKey *) = 0;
  virtual bool Init(CtCity *, OcPersistInfo *) = 0;
  virtual void Shutdown() = 0;
  virtual bool Save(CtCity *city, OcPersistInfo *info) = 0;
  virtual bool IsInCity() const = 0;
  virtual bool TestIsInCityFlag(u32 mask) const = 0;
  virtual u32 GetIsInCityFlag() const = 0;
  virtual void SetIsInCityFlag(u32 mask) = 0;
  virtual void ClearIsInCityFlag(u32 mask) = 0;
  virtual bool IsGeomFlag(u32 mask) = 0;
  virtual u8 SetGeomFlag(u32) = 0;
  virtual u8 ClearGeomFlag(u32) = 0;
  virtual u32 GetGeomFlag() = 0;
  virtual u32 GetOccLayerFlag() = 0;
  virtual void SetOccLayerFlag(u32) = 0;
  virtual u8 MajorType() const = 0;
  virtual u8 SubTypeInfo() const = 0;
  virtual u32 GetSubTypeData(u32 mask) = 0;
  virtual u8 AnimType() const = 0;
  virtual void AnimStart() = 0;
  virtual void AnimStop() = 0;
  virtual bool CanSimRemove() const = 0;
  virtual void SetSimRemove(bool) = 0;
  virtual bool CanUserRemove() const = 0;
  virtual void SetUserRemove(bool) = 0;
  virtual bool GetName(ScStringView *) = 0;
  virtual GeAttrib *GetAttribData() = 0;
  virtual bool GetSubSystemData(u32, void **) = 0;
  virtual u32 TypeId() const = 0;
  virtual CtAgentType *CityAgentType() = 0;
  virtual bool CreateQueryDialog(UiQueryWindow **) = 0;
  virtual u32 GetSoundObjectId(i32) = 0;
  virtual SpDefinition *GetSpriteAttrib(u32) = 0;
  virtual SpDefinition *GetSpriteAttrib() = 0;
  virtual SpSprite *GetSpriteInst(u32) = 0;
  virtual SpSprite *GetSpriteInst() = 0;
  virtual u32 GetDefaultLayerFlag() const = 0;
  // Retail CodeView reports T_UCHAR and installed producers return AL; nominal bool/BYTE spelling remains qualified.
  virtual u8 SetDefaultLayerFlag(u32) = 0;
  virtual void GetOrientation(u16 *, u16 *, u16 *) const = 0;
  virtual bool GetRotationFlag(u32 *rotation) const = 0;
  virtual void GetLocation(CtCoord *out) const = 0;
  virtual void GetLocationInCellCoords(u32 *x, u32 *z) const = 0;
  virtual u8 GetAlt() const = 0;
  virtual void GetLocationInAnimCellCoords(u32 *, u32 *) const = 0;
  virtual u16 GetAltAnimCells() const = 0;
  virtual void GetBounds(GeBounds *) const = 0;
  virtual void GetBoundingRectInCellCoords(GeCellRect *out) const = 0;
  virtual u8 SizeInCellsX() const = 0;
  virtual u8 SizeInCellsZ() const = 0;
  virtual u8 SizeInCellsY() const = 0;
  virtual u32 SizeInAnimCellsZ() const = 0;
  virtual u32 SizeInAnimCellsX() const = 0;
  virtual u32 SizeInAnimCellsY() const = 0;
  virtual void SetLocation(const CtCoord &position) = 0;
  virtual void SetLocationInCellCoords(u32 x, u32 z) = 0;
  virtual void SetAlt(u8 altitude) = 0;
  virtual bool GetMarkBit() const = 0;
  virtual void SetMarkBit(bool) = 0;
  // Retail LONG at slot256 and the installed effect/static-animation bodies retain the original DWORD result.
  virtual i32 Tick(u32 elapsed) = 0;
  virtual void SetSubTypeInfo(u8) = 0;
  virtual void SetOrientation(u16, u16, u16) = 0;
  virtual u8 SetRotationFlag(u32) = 0;
  virtual u32 GetMemUse() const = 0;
  virtual bool DebugInstanceTag(ScStringView *) = 0;
  virtual bool DebugClassTag(ScStringView *) = 0;
  virtual bool DebugTypeTag(ScStringView *) = 0;
};

// Actual cISC3MiscOcc IID8312515B adds three borrowed conversions after its Unknown prefix.
// The installed SIMMISC receiver supplies this view at+44; no conversion acquires another hold.
class OcMiscOccupant : public ScUnknown {
public:
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual BdBuilding *AsISC3Building() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
};

// Public virtual order follows the SDK and installed SIMGEOM table10029E24 through slot136.
// Keep the installed Boolean insertion result; the public SDK void spelling disagrees with original caller use.
class ScOccupantManager : public ScUnknown {
public:
  virtual bool Init(CtCity *city, u32 first, u32 second, u32 third, u32 fourth, u32 mask) = 0;
  virtual bool Shutdown() = 0;
  virtual u32 GetCellsInX() = 0;
  virtual u32 GetCellsInZ() = 0;
  virtual u32 GetCellsInY() = 0;
  virtual u32 GetSenderId() const = 0;
  virtual u32 GetOccLayer() const = 0;
  virtual bool RegisterChangeReceiver(CtChangeReceiver *, u32 *, bool) = 0;
  virtual bool UnregisterChangeReceiver(CtChangeReceiver *receiver) = 0;
  virtual bool UnregisterAllChangeReceivers() = 0;
  virtual bool PostOccupantAll(CtChangeReceiver *) = 0;
  virtual bool PostOccupantAll(ScMessageTarget *target) = 0;
  virtual bool InsertOccupant(ScOccupant *occupant) = 0;
  virtual bool InsertOccupantAtCell(u32, u32, ScOccupant *) = 0;
  virtual void RemoveOccupant(ScOccupant *occupant) = 0;
  virtual void RemoveOccupant(CtCoord const &) = 0;
  virtual void RemoveOccupantAtCell(u32, u32) = 0;
  virtual void RemoveAllOccupants() = 0;
  virtual bool OccupantChanged(ScOccupant *occupant, u32 change) = 0;
  virtual void ClearAnimatedOccupantList(bool) = 0;
  virtual bool IsEmpty(GeBounds const &) = 0;
  virtual bool DoesOccupantExistAtLocation(u32 x, u32 z) = 0;
  virtual bool IsInBounds(const CtCoord &point) = 0;
  virtual bool IsInBounds(GeBounds const &) = 0;
  virtual bool IsInBoundsCell(u32, u32) = 0;
  virtual bool GetIterator(OcIterator **) = 0;
  virtual bool GetOccupants(OcIterator **out, const GeBounds *bounds, OcIteratorTest *test) = 0;
  virtual bool GetOccupant(const CtCoord &point, ScOccupant **out) = 0;
  virtual bool GetOccupantAtCell(u32 x, u32 z, ScOccupant **out) = 0;
  virtual bool GetNeighbors(OcIterator **, ScOccupant *, OcIteratorTest *) = 0;
  virtual bool GetOccupantsInCell(GeBounds const &, u32, ScOccupant **, u32 *, bool (*)(ScOccupant *)) = 0;
  virtual bool GetNeighborsOfOccupant(ScOccupant *, u32, ScOccupant **, u32 *, bool (*)(ScOccupant *)) = 0;
};

#endif
