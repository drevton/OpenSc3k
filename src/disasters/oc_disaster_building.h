#ifndef SC3_DISASTERS_OC_DISASTER_BUILDING_H
#define SC3_DISASTERS_OC_DISASTER_BUILDING_H

#include "city/api.h"
#include "disasters/oc_disaster_occupant.h"
#include "economy/api.h"

// SIMDSTR links an independent building with its city-agent and occupant views at offsets four and eight.
// Missing original virtual operations remain abstract until their contracts are recovered.
namespace DsDisaster {
class OcBuilding : public BdBuilding, public CtAgent, public OcOccupant {
public:
  OcBuilding();
  ~OcBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  ScOccupant *AsISC3Occupant() override;
  CtAgent *AsISC3CityAgent() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  void SetOccupancyState(u32 state) override;
  u32 GetOccupancyState() override;
  void SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  bool GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  u32 GetGeomFlag() override;
  u32 GetMemUse() const override;
  GeAttrib *GetBaseAttribData() override;
  GeAttrib *GetAttribData() override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  u8 PowerRequired() override;
  bool SetName(const ScStringView &value) override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) override;
  // Native effect of original slot100; its ignored nominal return declaration remains unresolved.
  virtual void OnOccupancyStateChanged();
  u32 occupancy_flags;
  CtAgentType *agent_type;
  GeAttrib *cached_attributes;
  SpDefinition *cached_sprite;
};

} // namespace DsDisaster

#endif
