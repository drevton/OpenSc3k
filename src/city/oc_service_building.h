#ifndef SC3_CITY_OC_SERVICE_BUILDING_H
#define SC3_CITY_OC_SERVICE_BUILDING_H

#include "city/api.h"
#include "city/oc_service_occupant.h"
#include "economy/api.h"

class SpDefinition;

// SIMSERV independently links the original44-byte building: primary building, city agent+4 and occupant+8 precede four fields.
// This receiver uses SIMSERV's own occupant and module lifetime, independently of the SIMMISC implementation.
namespace CtService {
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
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  ScOccupant *AsISC3Occupant() override;
  CtAgent *AsISC3CityAgent() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  u32 TypeId() const override;
  u32 GetOccupancyState() override;
  void SetOccupancyState(u32 value) override;
  void SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  bool GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  u32 GetGeomFlag() override;
  u32 GetMemUse() const override;
  bool GetName(ScStringView *out) override;
  GeAttrib *GetAttribData() override;
  GeAttrib *GetBaseAttribData() override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  u8 PowerRequired() override;
  bool SetName(const ScStringView &value) override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 QueryResponse(ScMessage *message, UiQueryWindow *query) override;
  // Native effect projection of original virtual100; its unused private return remains unresolved.
  virtual void OnOccupancyStateChanged();
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;
  u32 occupancy_flags;
  CtAgentType *agent_type;
  GeAttrib *cached_attributes;
  SpDefinition *cached_sprite;
};

// The service dispatch marker is the original static-animation building, with the same storage as its building base.
class OcStaticAnimBuilding : public OcBuilding {
public:
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
};

} // namespace CtService

#endif
