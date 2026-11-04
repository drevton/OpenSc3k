#ifndef SC3_APPLICATION_REFERENCE_COUNT_H
#define SC3_APPLICATION_REFERENCE_COUNT_H

#include "common/sc3.h"

// Executable copy of cRZRefCount; its secondary identity retains the executable director, not SIMADV's copy.
namespace ApRuntime {
class ScRefCount {
public:
  ScRefCount();
  virtual ~ScRefCount();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();
  u32 references;
};
} // namespace ApRuntime

#endif
