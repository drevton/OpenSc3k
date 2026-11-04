#ifndef SC3_ENVIRONMENT_API_H
#define SC3_ENVIRONMENT_API_H

#include "city/ct_layer.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Reached IID2144802B from the SIMSERV hazard layer; its getter returns a defined byte, unlike ScByteMap's void getter.
// The provider exposes this separate identity at complete+16. Unreached suffix methods remain unresolved.
class EnHazardMap : public ScUnknown {
public:
  virtual u8 GetValue(u32 x, u32 z, u8 *out) = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3WeatherLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// Native grouping of the verified common controlling identity and CityLayer prefix; original SDK inheritance is unproved.
// SIMMISC1001B47A publishes the same pointer for58D/message/query/weather; existing declaration and overload order remain.
class EnWeatherLayer : public CtLayer {

public:
  enum class EnHemisphere : i32 { Northern = 0, Southern = 1 };

  typedef i32 EnSeason;
  typedef i32 EnWeatherType;

  virtual bool DoMessage(ScMessage *) = 0;
  virtual bool DoQueryInfo(ScMessage *, ScUnknown *) = 0;
  virtual bool StaticInit(CtDefinition *) = 0;
  virtual bool StaticShutdown() = 0;
  virtual bool Init(CtCity *) = 0;
  virtual bool Init(CtCity *, ScImporter *) = 0;
  virtual bool Init(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool Save(CtCity *, RsDatabaseSegment *) = 0;
  virtual bool SimulationBegin() = 0;
  virtual bool SimulationEnd() = 0;
  virtual bool Shutdown() = 0;
  virtual bool GetManipulator(CtAgentType const &, CtLayerManipulator **, i32 *) = 0;
  virtual u32 GetLayerType() = 0;
  virtual bool DebugClassTag(ScStringView *) = 0;
  virtual bool DebugTypeTag(ScStringView *) = 0;

  virtual void GetPosition(i32 *longitude, i32 *latitude) = 0;
  virtual void SetPosition(i32 longitude, i32 latitude) = 0;
  virtual EnHemisphere GetHemisphere() = 0;
  virtual EnSeason GetCurrentSeason() = 0;
  virtual i32 GetCurrentSeasonMonth() = 0;
  virtual void GetWeather(EnWeatherType *type, i32 *unknown2, CtDate *unknown3) = 0;
  virtual void SetWeather(EnWeatherType type, i32 unknown2) = 0;

  virtual f32 FarenheitToCelsius(f32 farenheit) = 0;
  virtual f32 CelsiusToFarenheit(f32 celsius) = 0;
  virtual f32 CelsiusToKelvin(f32 celsius) = 0;
  virtual f32 KelvinToCelsius(f32 kelvin) = 0;

  virtual f32 GetAverageTemperature(bool farenheit, CtDate *unknown2) = 0;
  virtual f32 GetAverageHighTemperature(bool farenheit, CtDate *unknown2) = 0;
  virtual f32 GetAverageLowTemperature(bool farenheit, CtDate *unknown2) = 0;
  virtual void GetAverageTemperaturePerSeasonPerLocation(f32 *unknown1, f32 *unknown2, f32 *unknown3, f32 *unknown4,
      f32 *unknown5, f32 *unknown6, f32 *unknown7, f32 *unknown8, f32 *unknown9, EnSeason unknown10, i32 unknown11,
      i32 unknown12, bool unknown13) = 0;

  virtual f32 GetHumidity(CtDate *unknown1) = 0;
  virtual f32 GetWindyness(CtDate *unknown1) = 0;
};

#endif
