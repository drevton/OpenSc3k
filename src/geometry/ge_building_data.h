#ifndef SC3_GEOMETRY_GE_BUILDING_DATA_H
#define SC3_GEOMETRY_GE_BUILDING_DATA_H

#include "geometry/building.h"

// Specialized 332-byte building attributes add IID21183B00 at+316. The original constructor defines the occupant
// factory class; property loading defines the additional raw power byte and placement flags.
class GeBuildingData : public GeBuildingAttrib, public GeBuildingType {
public:
  GeBuildingData();
  ~GeBuildingData() override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *Key() const override;
  u8 MajorType() const override;
  bool GetName(ScStringView *out) override;
  u8 PowerRequired() override;
  bool GetOccupantGZCLSID(u32 *out) const override;
  bool SetOccupantGZCLSID(u32 value) override;
  bool SetPowerRequired(u8 value) override;
  GeAttrib *AsOccupantAttrib() override;
  u32 Flags() override;

  // Forward the original generic prefix through this additional specialized interface.
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 GetOccLayerFlag() override;
  void SetOccLayerFlag(u8 mask) override;
  u8 SubTypeInfo() const override;
  u8 AnimType() const override;
  u32 SizeInCellsX() override;
  u32 SizeInCellsZ() override;
  u32 SizeInCellsY() override;
  CtAgentType *CityAgentType() override;
  u32 TypeId() const override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetOriginalSpriteAttrib() override;
  u32 GetDefaultLayerFlag() const override;
  bool SetDefaultLayerFlag(u32 layer) override;
  iptr GetPeopleProducerAttrib() override;
  iptr GetVehicleProducerAttrib() override;
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

protected:
  u32 classId;
  u8 powerRequired;
  u32 flags;
};

// Original extended building interface IID22002D06 preserves the building prefix and adds two borrowed/value getters.
class GePortType : public GeBuildingType {
public:
  virtual GeBuildingType *AsBuildingAttrib() = 0;
  virtual u32 GetPortType() = 0;
};

// Actual340-byte port attributes retain the separate building316 and extended port332 interface subobjects.
// Property loading defines the extra full-word port type; its constructor deliberately leaves that word unwritten.
class GePortData : public GeBuildingData, public GePortType {
public:
  GePortData();
  ~GePortData() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *Key() const override;
  bool GetName(ScStringView *out) override;
  u8 PowerRequired() override;
  bool GetOccupantGZCLSID(u32 *out) const override;
  bool SetOccupantGZCLSID(u32 value) override;
  GeAttrib *AsOccupantAttrib() override;
  u32 Flags() override;
  GeBuildingType *AsBuildingAttrib() override;
  u32 GetPortType() override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;

  // Forward the original generic prefix through this additional specialized interface.
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 GetOccLayerFlag() override;
  void SetOccLayerFlag(u8 mask) override;
  u8 MajorType() const override;
  u8 SubTypeInfo() const override;
  u8 AnimType() const override;
  u32 SizeInCellsX() override;
  u32 SizeInCellsZ() override;
  u32 SizeInCellsY() override;
  CtAgentType *CityAgentType() override;
  u32 TypeId() const override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetOriginalSpriteAttrib() override;
  u32 GetDefaultLayerFlag() const override;
  bool SetDefaultLayerFlag(u32 layer) override;
  iptr GetPeopleProducerAttrib() override;
  iptr GetVehicleProducerAttrib() override;
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
  bool SetPowerRequired(u8 value) override;

protected:
  u32 portType;
};

u32 SC3_STDCALL GeGetPortType(ScOccupant *occupant);

#endif
