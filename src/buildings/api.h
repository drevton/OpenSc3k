#ifndef SC3_BUILDINGS_API_H
#define SC3_BUILDINGS_API_H

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

class ScOccupant;
class BdBuilding;
class CtAgent;

// Native descriptive name for SIMMISC's queried IID03669E46 view at complete building+44.
// This separate Unknown identity borrows the three actual subobjects at original slots12/16/20.
class BdDealView : public ScUnknown {
public:
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual BdBuilding *AsISC3Building() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual bool IsOperational() = 0; // Original24, known Boolean predicate.
};

class ScOccupant;
class BdBuilding;

// Shared IID43680CA9 view: both facility QIs select the same address as their specialized identity, with the same
// original0..48 prefix. Unused private setter returns are native effect projections; nominal declarations remain open.
// This native interface grouping does not establish the historical C++ inheritance spelling.
class BdMortal : public ScUnknown {
public:
  virtual void SetMaximumAge(u32 value) = 0; // Original12.
  virtual u32 GetMaximumAge() = 0;           // Original16.
  virtual void SetDeclineAge(u32 value) = 0; // Original20.
  virtual u32 GetDeclineAge() = 0;           // Original24.
  virtual u32 GetAge() = 0;                  // Original28.
  virtual bool IsAging() = 0;                // Original32.
  virtual void Retire() = 0;                 // Original36 effects; unused nominal return unresolved.
  virtual u8 GetRetired() = 0;               // Original40, raw byte.
  virtual BdBuilding *AsISC3Building() = 0; // Original44, borrowed building view.
  virtual ScOccupant *GetOccupant() = 0;     // Original48, borrowed occupant view.
};

// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3PluginBuildingMgr.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class BdPluginManager : public ScUnknown {

public:
  virtual bool Init(ScStringView const &) = 0;
  virtual bool Shutdown() = 0;

  virtual void CheckForPluginBuildings(bool) = 0;
};

#endif
