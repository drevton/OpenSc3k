#ifndef SC3_MISC_LANDMARK_H
#define SC3_MISC_LANDMARK_H

#include "city/ct_date.h"
#include "misc/building.h"

// Original fieldless52-byte special building supplies its own query and permits custom names.
class OcBallPark : public OcMiscBuilding {
public:
  OcBallPark();
  ~OcBallPark() override;
  bool IsNameChangeable() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// Actual fieldless56-byte business-deal leaf supplies its original private query behavior.
// The actual base supplies the canonical borrowed conversions and operational business-deal predicate.
class OcCasino : public OcDealAnimBuilding {
public:
  OcCasino();
  ~OcCasino() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// Original fieldless52-byte special-building leaf supplies its actual private query behavior.
class OcCastle : public OcMiscBuilding {
public:
  OcCastle();
  ~OcCastle() override;
  bool IsNameChangeable() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcCountryClub : public OcMiscBuilding {
public:
  OcCountryClub();
  ~OcCountryClub() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcGeyserPark : public OcMiscAnimBuilding {
public:
  OcGeyserPark();
  ~OcGeyserPark() override;
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
class OcGigamall : public OcDealBuilding {
public:
  OcGigamall();
  ~OcGigamall() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// Original fieldless52-byte special-building leaf supplies its actual private query behavior.
class OcLandmark : public OcMiscBuilding {
public:
  OcLandmark();
  ~OcLandmark() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcLighthouse : public OcMiscAnimBuilding {
public:
  OcLighthouse();
  ~OcLighthouse() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original fieldless52-byte special building supplies its own query and permits custom names.
class OcMarina : public OcMiscBuilding {
public:
  OcMarina();
  ~OcMarina() override;
  bool IsNameChangeable() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// The original mayor's house adds one8-byte date value after the52-byte special-building receiver.
class OcMayorsHouse : public OcMiscBuilding {
public:
  OcMayorsHouse();
  ~OcMayorsHouse() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original cSC3Park adds its park query and nameability to the52-byte special-building receiver without more fields.
class OcPark : public OcMiscBuilding {
public:
  OcPark();
  ~OcPark() override;
  bool IsNameChangeable() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcPerformingArtsCenter : public OcMiscBuilding {
public:
  OcPerformingArtsCenter();
  ~OcPerformingArtsCenter() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original80-byte stadium adds its actual date and team-name String after the animated special-building receiver.
// The original team editor and descriptor persistence use their canonical shared interfaces directly.
class OcStadium : public OcMiscAnimBuilding {
public:
  OcStadium();
  ~OcStadium() override;
  using OcMiscAnimBuilding::Init;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryResponse(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
  ScString team_name;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcStatue : public OcMiscBuilding {
public:
  OcStatue();
  ~OcStatue() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// This original dated special building adds one8-byte date value after the52-byte special-building receiver.
class OcStockExchange : public OcMiscBuilding {
public:
  OcStockExchange();
  ~OcStockExchange() override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  CtDate birthdate;
};

// Original animated64-byte campus adds a date at52 and a reentrancy byte at60 after the52-byte base.
class OcThemePark : public OcMiscAnimBuilding {
public:
  OcThemePark();
  ~OcThemePark() override;
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

// Original fieldless52-byte special building supplies its own query and permits custom names.
class OcZoo : public OcMiscBuilding {
public:
  OcZoo();
  ~OcZoo() override;
  bool IsNameChangeable() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

#endif
