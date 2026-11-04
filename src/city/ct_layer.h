#ifndef SC3_CITY_CT_LAYER_H
#define SC3_CITY_CT_LAYER_H

#include "common/api.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.
/*
 * sc3k-gzcom-dll - an open-source DLL Plugin SDK for SimCity 3000
 *
 * cISC3CityLayer.h
 *
 * Copyright (C) 2025 Nicholas Hayes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, under
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see evidence/SIMCITY/sdk-license.txt.
 */

struct ScMessage;
class ScStringView;
class ScImporter;
class RsDatabaseSegment;
class CtCity;
class CtDefinition;
class CtAgentType;
class CtLayerManipulator;

// Native grouping of the installed same-pointer layer/message/query prefix; it adds no receiver subobject.
// The SDK lists ScUnknown directly, so the original C++ source hierarchy remains unproved.
class CtLayer : public ScQueryTarget {
public:
  bool DoMessage(ScMessage *message) override = 0;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override = 0;
  virtual bool StaticInit(CtDefinition *definition) = 0;
  virtual bool StaticShutdown() = 0;
  virtual bool Init(CtCity *city) = 0;
  virtual bool Init(CtCity *city, ScImporter *importer) = 0;
  virtual bool Init(CtCity *city, RsDatabaseSegment *segment) = 0;
  virtual bool Save(CtCity *city, RsDatabaseSegment *segment) = 0;
  virtual bool SimulationBegin() = 0;
  virtual bool SimulationEnd() = 0;
  virtual bool Shutdown() = 0;
  virtual bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) = 0;
  virtual u32 GetLayerType() = 0;
  virtual bool DebugClassTag(ScStringView *out) = 0;
  virtual bool DebugTypeTag(ScStringView *out) = 0;
};

#endif
