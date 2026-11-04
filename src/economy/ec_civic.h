#ifndef SC3_ECONOMY_EC_CIVIC_H
#define SC3_ECONOMY_EC_CIVIC_H

#include "city/api.h"
#include "city/ct_date.h"
#include "economy/api.h"
#include "economy/ec_pollution.h"
#include "economy/ec_residential.h"
#include "economy/ec_sector.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"

class GeAttrib;
class SpSprite;
class UiQuery;
class UiQueryWindow;
class RsStream;

// SIMRCI's actual module-local occupant base, embedded in the civic building receivers.
// Attribute/sprite and descriptor paths are restored; private query/record protocols remain explicit dependencies.
class EcCivicOccupant : public ScOccupant {
public:
  EcCivicOccupant();
  virtual ~EcCivicOccupant();
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

protected:
  // Actual private query slots292..304, with byte returns and separate original message/query arguments.
  virtual u8 QueryBasic(ScMessage *message, UiQueryWindow *query);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryDetail(ScMessage *message, UiQuery *query);
  virtual u8 QueryResponse(ScMessage *message, UiQueryWindow *query);
  // Private slots308/312 preserve reached effects; nominal source return declarations remain unresolved.
  virtual u8 CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  // Private record slots316/320 use the reached two-argument effects; nominal bool/BYTE returns remain unresolved.
  virtual u8 ReadRecord(CtCity *city, RsStream *record);
  virtual u8 WriteRecord(CtCity *city, RsStream *record);
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

// Incinerator owner with its actual separate combustion interface and class-specific state.
// The inherited private stream-state adapters remain an explicit reconstruction gap.
class EcIncinerator : public EcBuilding, public EcCombustion {
public:
  EcIncinerator();
  ~EcIncinerator() override;
  using EcBuilding::Init;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
  bool Simulate(i32 phase) override;
  i32 MinimumExecutionInterval() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  void Retire() override;
  u8 GetRetired() override;
  u32 GetAge() override;
  u32 GetDeclineAge() override;
  bool IsAging() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *GetOccupant() override;
  u32 GetProduction() override;
  u32 GetCapacity() override;
  u32 Consume(u32 amount) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  // Actual private update/replacement operations; unused nominal returns remain unresolved.
  virtual void ReplaceRetired();
  void UpdateEffectiveness();
  u32 capacity;
  CtDate construction_date;
  u32 consumed, previousMonthConsumed;
  u8 retired;
};

// Recycling owner retains an independent interface and date layout; it is not a combustion subtype.
class EcRecycler : public EcBuilding, public EcRecycling {
public:
  EcRecycler();
  ~EcRecycler() override;
  using EcBuilding::Init;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Simulate(i32 phase) override;
  i32 MinimumExecutionInterval() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  void Retire() override;
  u8 GetRetired() override;
  u32 GetAge() override;
  u32 GetDeclineAge() override;
  bool IsAging() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *GetOccupant() override;
  u32 GetEffectiveness() override;
  u32 GetCapacity() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  // Actual private update/replacement operations; unused nominal returns remain unresolved.
  virtual void ReplaceRetired();
  void UpdateEffectiveness();
  u32 effectiveness, population;
  CtDate construction_date;
  u8 retired;
};

extern u32 EcIncineratorOptimal, EcIncineratorLifespan, EcIncineratorDecline;
extern u32 EcRecyclerOptimal, EcRecyclerPopulation, EcRecyclerLifespan, EcRecyclerDecline;

class SpDefinition;

// SIMRCI's actual common building receiver has building, city-agent and occupant views before four fields.
// Unported queries, agent and name/date methods remain explicit dependencies; native views are not foreign overlays.
class EcCivicBuilding : public BdBuilding, public CtAgent, public EcCivicOccupant {
public:
  EcCivicBuilding();
  ~EcCivicBuilding() override;
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
  virtual void RefreshSprite();
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  u32 occupancy_flags;
  CtAgentType *agent_type;
  GeAttrib *cached_attributes;
  SpDefinition *cached_sprite;
};

// Partial native receivers for the actual civic/education/health views. The actual module-local building/occupant base owns packed references.
// The trailing private primary operation and remaining base dependencies stay explicit; these are not foreign overlays.
// Private DWORD/enum signedness, byte versus bool and float versus double declarations remain explicitly unresolved.

class EcSchool : public EcCivicView, public EcEducationService, public BdUpkeepView, public EcCivicBuilding {
public:
  EcSchool();
  virtual ~EcSchool();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  // Actual child city/descriptor overrides preserve their distinct required-field and fallback behavior.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetKind() override;
  u32 GetCapacity() override;
  u32 GetStaffCount() override;
  u8 GetMinimumAge() override;
  u8 GetMaximumAge() override;
  f32 GetEducationEffect() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  static bool tuning_loaded;
  u32 staff;
  f32 education_effect;
  u32 base_upkeep, funded_upkeep; // Complete+56,+60,+64,+68.
  static u32 optimal_capacity, optimal_staff;
  static u8 minimum_age, maximum_age;
  static f32 optimal_effect, maximum_efficiency, mystical_e;
  static u32 optimal_upkeep;
};

class EcCollege : public EcCivicView, public EcEducationService, public BdUpkeepView, public EcCivicBuilding {
public:
  EcCollege();
  virtual ~EcCollege();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  // Actual child city/descriptor overrides preserve their distinct required-field and fallback behavior.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetKind() override;
  u32 GetCapacity() override;
  u32 GetStaffCount() override;
  u8 GetMinimumAge() override;
  u8 GetMaximumAge() override;
  f32 GetEducationEffect() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  static bool tuning_loaded;
  u32 staff;
  f32 education_effect;
  u32 base_upkeep, funded_upkeep; // Complete+56,+60,+64,+68.
  static u32 optimal_capacity, optimal_staff;
  static u8 minimum_age, maximum_age;
  static f32 optimal_effect, maximum_efficiency, mystical_e;
  static u32 optimal_upkeep;
};

class EcLibrary : public EcCivicView, public EcEducationService, public BdUpkeepView, public EcCivicBuilding {
public:
  EcLibrary();
  virtual ~EcLibrary();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  // Actual child city/descriptor overrides preserve their distinct required-field and fallback behavior.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetKind() override;
  u32 GetCapacity() override;
  u32 GetStaffCount() override;
  u8 GetMinimumAge() override;
  u8 GetMaximumAge() override;
  f32 GetEducationEffect() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  static bool tuning_loaded;
  f32 education_effect; // Complete+56.
  u32 funded_upkeep;    // Complete+60.
  static u32 optimal_capacity, optimal_upkeep;
  static u8 minimum_age, maximum_age;
  static f32 optimal_effect;
};

class EcMuseum : public EcCivicView, public EcEducationService, public BdUpkeepView, public EcCivicBuilding {
public:
  EcMuseum();
  virtual ~EcMuseum();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  // Actual child city/descriptor overrides preserve their distinct required-field and fallback behavior.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetKind() override;
  u32 GetCapacity() override;
  u32 GetStaffCount() override;
  u8 GetMinimumAge() override;
  u8 GetMaximumAge() override;
  f32 GetEducationEffect() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  static bool tuning_loaded;
  u32 funded_upkeep; // Complete+56; its education effect is shared tuning, not instance state.
  static u32 optimal_capacity, optimal_upkeep;
  static u8 minimum_age, maximum_age;
  static f32 optimal_effect;
};

class EcHospital : public EcCivicView, public BdUpkeepView, public EcHealthService, public EcCivicBuilding {
public:
  EcHospital();
  virtual ~EcHospital();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  // Actual child city/descriptor overrides preserve their distinct required-field and fallback behavior.
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetBedCount() override;
  u32 GetDoctorCount() override;
  f32 GetHealthEffect() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;
  static bool tuning_loaded;
  u32 beds, doctors;
  f32 health_effect;
  u32 base_upkeep, funded_upkeep; // Complete+56,+60,+64,+68,+72.
  static u32 optimal_beds, optimal_doctors, optimal_upkeep;
  static f32 optimal_effect, maximum_efficiency, mystical_e;
};

#endif
