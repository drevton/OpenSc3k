#ifndef SC3_NETWORK_NW_OCCUPANT_H
#define SC3_NETWORK_NW_OCCUPANT_H

#include "city/api.h"
#include "common/sc3_string.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "network/api.h"
#include "network/street.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"

struct NwCachedArt {
  void *resource; // Actual escaped COM publication slot for a retained sprite definition.
  RsKey key;
};

// Constructor248F6 establishes the resource, occupant-attribute and property subobjects and these retained fields.
// Property persistence uses the actual resource descriptor methods and preserves their required/optional distinction.
class NwAttrib : public RsResource, public GeAttrib, public GePropertyObject {
public:
  NwAttrib();
  ~NwAttrib() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *Key() const override;
  bool GetOccupantGZCLSID(u32 *out) const override;
  bool SetOccupantGZCLSID(u32 classId) override;
  bool SetDefaultLayerFlag(u32 mask) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 GetOccLayerFlag() override;
  void SetOccLayerFlag(u8 mask) override;
  u8 MajorType() const override;
  u8 SubTypeInfo() const override;
  u8 AnimType() const override;
  CtAgentType *CityAgentType() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetOriginalSpriteAttrib() override;
  iptr GetPeopleProducerAttrib() override;
  iptr GetVehicleProducerAttrib() override;
  u32 SizeInCellsX() override;
  u32 SizeInCellsZ() override;
  u32 SizeInCellsY() override;
  u32 TypeId() const override;
  u32 GetDefaultLayerFlag() const override;
  virtual u8 Flammability() override;
  bool GetName(ScStringView *out) override;
  virtual bool GetArchitectName(ScStringView *out) override;
  virtual bool GetNameKey(RsKey *out) override;
  u32 GetSoundObjectId(i32 *index) override;
  virtual void GetSoundKeyA(RsKey *out) override;
  virtual void GetSoundKeyB(RsKey *out) override;
  virtual void GetPlopCursorKey(RsKey *out) override;
  virtual void GetQueryDescKey(RsKey *out) override;
  virtual void GetQueryFormatFlags(u32 *out) override;
  virtual u32 GetQueryDialogClass() override;
  virtual bool SetQueryDialogClass(u32 clsid) override;
  virtual void SetOverRideAttrib(OcOverride *value) override;
  virtual OcOverride *GetOverRideAttrib() override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;
  u8 SetPropertyFlag(u8 value) override;
  u8 GetPropertyFlag() override;

protected:
  // Private loader byte results are observed; their nominal source declarations remain unresolved.
  u8 LoadPeople(const RsKey &resourceKey);
  u8 LoadVehicle(const RsKey &resourceKey);

  u32 occupantClass;
  ScString name;
  u32 geometryFlags, sizeX, sizeZ, sizeY;
  CtAgentType *agentType;
  NwCachedArt primary;
  void *peopleAttrib, *vehicleAttrib; // Actual escaped resource-manager publication slots.
  RsKey peopleKey, vehicleKey;
  u8 flammability;
  RsKey soundA, soundB;
  u8 cachedLayers;
  NwCachedArt variants[7];
  RsKey nameKey, plopKey, queryKey;
  u32 queryFlags;
  u8 propertyFlag;
  OcOverride *overrideAttrib;
  u32 queryDialogClass;
};

// ConstructorDAEC installs the connection interface before the actual generic attribute base.
// Its property interface extends the generic reader/writer with the four original connection words.
class NwPieceAttrib : public NwConnectionData, public NwAttrib {
public:
  NwPieceAttrib();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetOccupantGZCLSID(u32 *out) const override;
  bool SetOccupantGZCLSID(u32 classId) override;
  RsKey *Key() const override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 GetOccLayerFlag() override;
  void SetOccLayerFlag(u8 mask) override;
  u8 SubTypeInfo() const override;
  u8 AnimType() const override;
  bool GetName(ScStringView *out) override;
  u32 SizeInCellsX() override;
  u32 SizeInCellsZ() override;
  u32 SizeInCellsY() override;
  CtAgentType *CityAgentType() override;
  u32 TypeId() const override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetOriginalSpriteAttrib() override;
  u32 GetDefaultLayerFlag() const override;
  bool SetDefaultLayerFlag(u32 mask) override;
  u8 Flammability() override;
  u32 GetSoundObjectId(i32 *index) override;
  void GetSoundKeyA(RsKey *out) override;
  void GetSoundKeyB(RsKey *out) override;
  void GetPlopCursorKey(RsKey *out) override;
  void GetQueryDescKey(RsKey *out) override;
  void GetQueryFormatFlags(u32 *out) override;
  u32 GetQueryDialogClass() override;
  bool SetQueryDialogClass(u32 clsid) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool GetNameKey(RsKey *out) override;
  void SetOverRideAttrib(OcOverride *value) override;
  OcOverride *GetOverRideAttrib() override;
  bool GetArchitectName(ScStringView *out) override;
  u8 MajorType() const override;
  iptr GetPeopleProducerAttrib() override;
  iptr GetVehicleProducerAttrib() override;
  void GetConnectionSamples(
      i32 kind, u8 plane, i32 side, i32 rotation, u8 *first, u8 *second, u8 *third, u8 *fourth) const override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;
  u8 ConnectionFlag0() const override;
  u8 ConnectionFlag1() const override;
  // These private setters retain source effects; their unused nominal return declarations remain unresolved.
  void SetPrimaryMask(u32 value) override;
  void SetConnectionFlag0(u8 value) override;
  void SetConnectionFlag1(u8 value) override;

protected:
  u32 connectionMasks[3], connectionFlags;
};

struct RsKey;
class GeAttrib;
class SpSprite;
class SpDefinition;
class ScStringView;
class CtAgentType;
class ScLock;
class UiQuery;

// SIMNTWRK generic occupant base established by constructor calls and secondary-interface adjustors.
// Nominal identity with SIMGEOM's class is unproven; lifetime and binding dependencies remain explicit.
class NwOccupant : public ScOccupant {
public:
  NwOccupant();
  virtual ~NwOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  void AnimStart() override;
  u8 SetDefaultLayerFlag(u32 layer) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual bool Init(GeAttrib *value) override;
  bool Init(RsKey *key) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  virtual bool IsInCity() const override;
  virtual bool TestIsInCityFlag(u32 mask) const override;
  virtual u32 GetIsInCityFlag() const override;
  virtual void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  bool IsGeomFlag(u32 mask) override;
  virtual u8 SetGeomFlag(u32 mask) override;
  virtual u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetSubTypeData(u32 mask) override;
  virtual u32 GetOccLayerFlag() override;
  virtual void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  virtual u8 AnimType() const override;
  u32 TypeId() const override;
  virtual GeAttrib *GetAttribData() override;
  virtual bool GetSubSystemData(u32 id, void **out) override;
  virtual u32 GetDefaultLayerFlag() const override;
  virtual bool GetName(ScStringView *out) override;
  virtual CtAgentType *CityAgentType() override;
  virtual u32 GetSoundObjectId(i32 index) override;
  virtual SpDefinition *GetSpriteAttrib() override;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) override;
  virtual SpSprite *GetSpriteInst() override;
  virtual SpSprite *GetSpriteInst(u32 layer) override;
  virtual u32 GetMemUse() const override;
  u8 SubTypeInfo() const override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
  virtual bool CanSimRemove() const override;
  virtual void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  virtual void SetUserRemove(bool enabled) override;
  virtual void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  virtual void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  virtual u8 GetAlt() const override;
  virtual void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u16 GetAltAnimCells() const override;
  virtual void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  virtual u8 SizeInCellsX() const override;
  virtual u8 SizeInCellsZ() const override;
  virtual u8 SizeInCellsY() const override;
  virtual u32 SizeInAnimCellsZ() const override;
  virtual u32 SizeInAnimCellsX() const override;
  virtual u32 SizeInAnimCellsY() const override;
  void SetLocation(const CtCoord &position) override;
  virtual void SetLocationInCellCoords(u32 x, u32 z) override;
  virtual void SetAlt(u8 altitude) override;
  virtual bool GetMarkBit() const override;
  virtual void SetMarkBit(bool marked) override;
  virtual void SetSubTypeInfo(u8 subtype) override;
  virtual void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  virtual u8 SetRotationFlag(u32 rotation) override;

protected:
  // Private query hooks retain their observed byte results; original nominal return declarations remain unresolved.
  virtual u8 QueryWindow(ScMessage *message, UiQueryWindow *window);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryText(ScMessage *message, UiQuery *query);
  virtual u8 QueryWindowEnd(ScMessage *message, UiQueryWindow *window);

  // Private virtual308 effect phase; native bool carrier, original bool/BYTE nominal type unresolved.
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  // Private virtual312 effect phase; nominal void/unused-return declaration remains unresolved.
  virtual void DestroySprites(SpSprite **out);

  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

extern ScLock NwOccupantLock; // Actual module-wide recursive reference lock, original global325F8.

struct NwPositionNode {
  NwPositionNode *next, *previous;
  CtCoord position;
};

// Game-coordinate storage retains the original one-pointer sentinel and stable linked nodes.
// Core allocation replaces the original container/allocator machinery; there is no cached size field.
struct NwPositionList {
  NwPositionList();
  NwPositionList(const NwPositionList &source);
  ~NwPositionList();
  void Append(const CtCoord &position);
  void Clear();
  void RemoveXZ(const CtCoord &position);
  void PopFront();

  // Implicit shallow assignment would transfer neither nodes nor sentinel ownership.
  NwPositionList &operator=(const NwPositionList &) = delete;
  NwPositionNode *head;
};

// Scoped localized String resource used by network query text. The acquired interface remains published until release.
class NwTextRef {
public:
  NwTextRef(u32 instance, u32 group);
  virtual ~NwTextRef();
  ScString CopyString() const;

protected:
  void *stringInterface;
  RsKey key;
  void Resolve();
};

// Constructor D0FC and interface adjustors establish these two actual base subobjects.
// Identifier and descriptor binding retain the real generic occupant base and its ordered teardown.
class NwPiece : public NwConnection, public NwOccupant {
public:
  NwPiece();
  ScOccupant *AsOccupant() override;
  using NwOccupant::Init;
  bool Init(RsKey *key) override;
  bool Init(u32 piece, u32 rotation) override;   // Original primary16, both arguments are full unsigned DWORDs.
  RsKey AsResourceKey(u32 piece) const override; // Original primary96; aggregate result has a hidden PC return buffer.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetSubTypeData(u32 mask) override;
  u32 TileId() const override;
  void Repair() override;
  void SetIsDiagonal(u8 value) override;
  void SetCollapse(u8 value) override;
  // Value-storing setter returns remain unresolved; these declarations retain only their original effects.
  void SetDamage(u8 value) override;
  void SetTileId(u32 piece) override;
  void SetRotationFlags(u32 rotation) override;
  u32 NeedsRepair() const override;
  u32 GetRotationFlags() override;
  int IsTeleport() const override;
  void SetTeleport(i32 direction, i32 length) override;
  int GetTeleport(i32 *direction, i32 *length) override;
  u8 GetTrafficKey(RsKey *out) override;
  NwTrafficBlock *GetTrafficBlock() override;
  u8 ClearTrafficBlock() override;
  u8 MajorType() const override = 0; // Original secondary +84 is pure in the abstract piece base.
  u8 ConnectionFlag0() const override;
  u8 ConnectionFlag1() const override;
  void GetConnectionSamples(i32 kind, u8 plane, i32 side, u8 *first, u8 *second, u8 *third, u8 *fourth) const override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryText(ScMessage *message, UiQuery *query) override;

  u32 pieceData; // Original primary +24: ID16, decoration1, extra-present1, extra2+8, unused high nibble.
};

// OriginalD2DA extends the abstract28-byte piece with one retained traffic-block interface at primary+28.
class NwTrafficPiece : public NwPiece {
public:
  NwTrafficPiece();
  ~NwTrafficPiece() override;
  u8 GetTrafficKey(RsKey *out) override;
  NwTrafficBlock *GetTrafficBlock() override;
  u8 ClearTrafficBlock() override;

protected:
  NwTrafficBlock *trafficBlock;
};

// Distinct factory variants preserve the original base selections and constant virtual methods.
// Names describe proven memberships or literal masks; original private class names remain unavailable.
// Each class retains the actual piece teardown and is selected by the original low-byte kind dispatch.

// Factory kind1 uses NwTrafficPiece and geometry mask0xc804.
class NwRoadPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind2 uses NwTrafficPiece and geometry mask0x14804.
class NwHighwayPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind3 uses NwTrafficPiece and geometry mask0x24804.
class NwRailPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind4 uses NwTrafficPiece and geometry mask0x44804.
class NwSubwayPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind5 uses NwTrafficPiece and geometry mask0x2c804.
class NwRoadRailPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind6 uses NwTrafficPiece and geometry mask0x1c804.
class NwRoadHighwayPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind7 uses NwTrafficPiece and geometry mask0x34804.
class NwRailHighwayPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind8 uses NwTrafficPiece and geometry mask0x64804.
class NwRailSubwayPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind9 uses NwPiece and geometry mask.
class NwPipePiece : public NwPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind10 uses NwPiece and geometry mask.
class NwPowerPiece : public NwPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind11 uses NwTrafficPiece and geometry mask.
class NwRoadPowerPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind12 uses NwTrafficPiece and geometry mask.
class NwRailPowerPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind13 uses NwTrafficPiece and geometry mask.
class NwHighwayPowerPiece : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind14 uses NwTrafficPiece and geometry mask.
class NwMask21C804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind15 uses NwTrafficPiece and geometry mask.
class NwMask214804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind16 uses NwTrafficPiece and geometry mask0x8c804.
class NwMask8C804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind17 uses NwTrafficPiece and geometry mask0x94804.
class NwMask94804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind18 uses NwTrafficPiece and geometry mask0xa4804.
class NwMaskA4804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind19 uses NwPiece and geometry mask.
class NwMask480804 : public NwPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind20 uses NwTrafficPiece and geometry mask.
class NwMask10C804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind21 uses NwTrafficPiece and geometry mask.
class NwMask124804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Factory kind22 uses NwTrafficPiece and geometry mask.
class NwMask114804 : public NwTrafficPiece {
public:
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  u8 MajorType() const override;
  u32 GetDefaultLayerFlag() const override;
  bool DebugClassTag(ScStringView *out) override;
};

// Original BDCD cdecl creator; private nominal bool/BYTE return spelling remains unresolved.
u8 NwMakePiece(u32 kind, u32 iid, void **out);

#endif
