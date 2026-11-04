#ifndef SC3_CITY_OC_SERVICE_OCCUPANT_H
#define SC3_CITY_OC_SERVICE_OCCUPANT_H

#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"

class GeAttrib;
class SpSprite;
class RsStream;
class UiQuery;
class UiQueryWindow;

// SIMSERV independently links the actual20-byte cSC3Occupant base, established by constructor35669 and building subobject+8.
// Independently linked method bodies remain explicit dependencies; private nominal returns remain qualified.
namespace CtService {
class OcOccupant : public ScOccupant {
public:
  OcOccupant();
  virtual ~OcOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  u8 SetDefaultLayerFlag(u32 mask) override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  u8 SubTypeInfo() const override;
  u32 TypeId() const override;
  GeAttrib *GetAttribData() override;
  bool CanSimRemove() const override;
  void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  void SetUserRemove(bool enabled) override;
  void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u8 GetAlt() const override;
  u16 GetAltAnimCells() const override;
  void SetLocation(const CtCoord &value) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool marked) override;
  void SetSubTypeInfo(u8 subtype) override;
  void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  u8 SetRotationFlag(u32 rotation) override;
  u32 GetSoundObjectId(i32 index) override;
  void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;

  bool DoMessage(ScMessage *) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  u32 GetSubTypeData(u32) override;
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32) override;
  bool GetName(ScStringView *out) override;
  bool GetSubSystemData(u32 id, void **out) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpSprite *GetSpriteInst() override;
  SpSprite *GetSpriteInst(u32 layer) override;
  u32 GetDefaultLayerFlag() const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsY() const override;
  void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u32 GetMemUse() const override;
  bool DebugInstanceTag(ScStringView *) override;
  bool DebugClassTag(ScStringView *) override;
  bool DebugTypeTag(ScStringView *) override;

  // Native effects of original288; the public SDK omits this original WORD setter.
  virtual void SetAltAnimCells(u16 altitude);

protected:
  // Actual private query slots292..304, with byte returns and separate original message/query arguments.
  virtual u8 QueryBasic(ScMessage *message, UiQueryWindow *query);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryDetail(ScMessage *message, UiQuery *query);
  virtual u8 QueryResponse(ScMessage *message, UiQueryWindow *query);
  // Original private slot308 returns a Boolean byte; SDK supplies no nominal private declaration.
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  // Full native effects of private slots316/320; original nominal byte/Boolean returns remain unresolved.
  virtual bool ReadState(CtCity *city, RsStream *record);
  virtual bool WriteState(CtCity *city, RsStream *record);
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

} // namespace CtService

#endif
