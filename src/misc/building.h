#ifndef SC3_MISC_BUILDING_H
#define SC3_MISC_BUILDING_H

#include "buildings/api.h"
#include "city/ct_date.h"
#include "common/sc3_string.h"
#include "misc/oc_building.h"
#include "misc/oc_static_anim_building.h"

// Original56-byte business-deal receiver adds a genuine deal identity at44 and an8-byte date at48.
// The canonical deal view exposes the three borrowed conversions without adding a separate owned object.
class OcDealAnimBuilding : public OcStaticAnimBuilding, public BdDealView {
public:
  OcDealAnimBuilding();
  ~OcDealAnimBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  bool IsOperational() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  u32 GetGeomFlag() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original56-byte business-deal receiver adds a genuine deal identity at44 and an8-byte date at48.
// The canonical deal view exposes the three borrowed conversions without adding a separate owned object.
class OcDealBuilding : public OcBuilding, public BdDealView {
public:
  OcDealBuilding();
  ~OcDealBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  bool IsOperational() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  u32 GetGeomFlag() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Actual52-byte cSC3MiscOccAnim adds its conversion interface at44 and one owned optional String pointer at48.
// Private query handlers retain the original separate message/query inputs with qualified native return phases.
class OcMiscAnimBuilding : public OcStaticAnimBuilding, public OcMiscOccupant {
public:
  OcMiscAnimBuilding();
  ~OcMiscAnimBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  using OcStaticAnimBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool GetName(ScStringView *out) override;
  bool SetName(const ScStringView &value) override;
  u32 GetGeomFlag() override;
  void SetOccupancyState(u32 value) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryResponse(ScMessage *message, UiQueryWindow *query) override;
  ScString *custom_name;
};

// Actual52-byte cSC3MiscOcc adds its conversion interface at44 and one owned optional String pointer at48.
// Private query handlers retain the original separate message/query inputs with qualified native return phases.
class OcMiscBuilding : public OcBuilding, public OcMiscOccupant {
public:
  OcMiscBuilding();
  ~OcMiscBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  using OcBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool GetName(ScStringView *out) override;
  bool SetName(const ScStringView &value) override;
  u32 GetGeomFlag() override;
  void SetOccupancyState(u32 value) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryResponse(ScMessage *message, UiQueryWindow *query) override;
  ScString *custom_name;
};

#endif
