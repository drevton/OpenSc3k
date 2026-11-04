#ifndef SC3_DISASTERS_OC_DISASTER_OCCUPANT_H
#define SC3_DISASTERS_OC_DISASTER_OCCUPANT_H

#include "occupants/animation_manager.h"

class GeAttrib;
class SpSprite;
class UiQuery;
class RsStream;

// SIMDSTR links its own occupant base, private query/sprite methods and director lifetime.
namespace DsDisaster {
class OcOccupant : public ScOccupant {
public:
  OcOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  u8 SubTypeInfo() const override;
  bool CanSimRemove() const override;
  void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  void SetUserRemove(bool enabled) override;
  void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u8 GetAlt() const override;
  void SetLocation(const CtCoord &value) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool marked) override;
  void SetSubTypeInfo(u8 subtype) override;
  void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  GeAttrib *GetAttribData() override;
  SpSprite *GetSpriteInst() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  u32 TypeId() const override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  u8 AnimType() const override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpSprite *GetSpriteInst(u32 layer) override;
  u32 GetDefaultLayerFlag() const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsY() const override;
  void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u8 SetRotationFlag(u32 rotation) override;
  u32 GetMemUse() const override;
  u32 GetSubTypeData(u32 mask) override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
  bool GetName(ScStringView *out) override;
  bool GetSubSystemData(u32 id, void **out) override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  u32 GetSoundObjectId(i32 index) override;
  u8 SetDefaultLayerFlag(u32 layer) override;
  u16 GetAltAnimCells() const override;
  void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  // Original private input widths are established; ignored nominal return spellings remain qualified.
  virtual void SetAltAnimCells(u16 altitude);
  // Query hooks transport the original AL result; private bool/BYTE source spelling remains unresolved.
  virtual u8 DoQueryViews(ScMessage *message, UiQueryWindow *window);
  virtual u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  virtual u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  virtual u8 DoExitQueryWindow(ScMessage *message, UiQueryWindow *window);
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  virtual u8 SerialRead(CtCity *city, RsStream *record);
  virtual u8 SerialWrite(CtCity *city, RsStream *record);

public:
  virtual ~OcOccupant();

protected:
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

} // namespace DsDisaster

#endif
