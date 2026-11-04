#ifndef SC3_ADVISORS_AD_REF_COUNT_H
#define SC3_ADVISORS_AD_REF_COUNT_H

#include "common/sc3.h"

// SIMADV's actual eight-byte reference base, used as the secondary base of its city manipulators.
// This is recovered object storage/ownership, separate from the SDK's unrelated convenience template.
class ScRefCount {
public:
  ScRefCount();
  virtual ~ScRefCount();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 GetReferenceCount() const;
  u32 references;
};

#endif
