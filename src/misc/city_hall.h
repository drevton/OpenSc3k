#ifndef SC3_MISC_CITY_HALL_H
#define SC3_MISC_CITY_HALL_H

#include "city/ct_date.h"
#include "misc/building.h"

// Original fieldless52-byte port inherits the special-building name and date predicates unchanged.
class OcAirport : public OcMiscBuilding {
public:
  OcAirport();
  ~OcAirport() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcCityHall : public OcMiscBuilding {
public:
  OcCityHall();
  ~OcCityHall() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcCourthouse : public OcMiscBuilding {
public:
  OcCourthouse();
  ~OcCourthouse() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcDefenseContractor : public OcMiscBuilding {
public:
  OcDefenseContractor();
  ~OcDefenseContractor() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcMedicalResearchCenter : public OcMiscBuilding {
public:
  OcMedicalResearchCenter();
  ~OcMedicalResearchCenter() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original animated64-byte campus adds a date at52 and a reentrancy byte at60 after the52-byte base.
class OcMilitaryBase : public OcMiscAnimBuilding {
public:
  OcMilitaryBase();
  ~OcMilitaryBase() override;
  u32 GetGeomFlag() override;
  void SetOccupancyState(u32 value) override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
  u8 occupancy_updating;
};

// Actual fieldless56-byte business-deal leaf supplies its original private query behavior.
// The actual base supplies the canonical borrowed conversions and operational business-deal predicate.
class OcPrison : public OcDealBuilding {
public:
  OcPrison();
  ~OcPrison() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcScienceCenter : public OcMiscAnimBuilding {
public:
  OcScienceCenter();
  ~OcScienceCenter() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original fieldless52-byte port inherits the special-building name and date predicates unchanged.
class OcSeaport : public OcMiscBuilding {
public:
  OcSeaport();
  ~OcSeaport() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcSpaceport : public OcMiscAnimBuilding {
public:
  OcSpaceport();
  ~OcSpaceport() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Actual fieldless56-byte business-deal leaf supplies its original private query behavior.
// The actual base supplies the canonical borrowed conversions and operational business-deal predicate.
class OcToxicWastePlant : public OcDealAnimBuilding {
public:
  OcToxicWastePlant();
  ~OcToxicWastePlant() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// Original animated64-byte campus adds a date at52 and a reentrancy byte at60 after the52-byte base.
class OcUniversity : public OcMiscAnimBuilding {
public:
  OcUniversity();
  ~OcUniversity() override;
  u32 GetGeomFlag() override;
  void SetOccupancyState(u32 value) override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
  u8 occupancy_updating;
};

#endif
