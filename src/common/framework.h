#ifndef SC3_COMMON_FRAMEWORK_H
#define SC3_COMMON_FRAMEWORK_H

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScFramework;

// The reached cIGZCOM operation has a genuine interface-output slot and a Boolean result.
class ScCom : public ScUnknown {
public:
  virtual bool GetClassObject(u32 class_id, u32 iid, void **out) = 0;
  virtual ScFramework *FrameWork() = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual bool AddLibrary(const ScStringView &sPath) = 0;
  virtual void FreeUnusedLibraries() = 0;
  virtual bool CreateGuid(u32 *pGuid) = 0;
  virtual bool RealInit() = 0;
  virtual void RealShutdown() = 0;
  // Installed40 forwards the ordinary service setter; it does not produce a service pointer.
  virtual void SetServiceRunning(bool bRunning) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cIGZUnknownEnumerator.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class ScUnknownEnumerator : public ScUnknown {

public:
  virtual void DoEnumObject(ScUnknown *pObject, ScUnknown *pCallbackUnknown, u32 callbackUnknownRIID) = 0;
};

#endif
