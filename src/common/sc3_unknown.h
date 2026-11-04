#ifndef SC3_COMMON_SC3_UNKNOWN_H
#define SC3_COMMON_SC3_UNKNOWN_H

#include <cassert>

#include "common/ids.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScUnknown {
public:
  virtual bool QueryInterface(u32 iid, void **out) = 0;
  virtual u32 AddRef() = 0;
  virtual u32 Release() = 0;
};

// Acquire one reference and return the same typed pointer. The caller retains ownership of that reference.
template <typename T> inline T *ScRetain(T *object) {
  object->AddRef();
  return object;
}

// Require a nonnull borrowed interface and acquire one reference. Return the same typed pointer to the caller.
template <typename T> inline T *ScAddRefNotNull(T *object) {
  assert(object != nullptr);
  return ScRetain(object);
}

// Take the stored pointer and clear its caller-owned slot. The returned pointer keeps its original type.
template <typename T> inline T *ExchNull(T **object) {
  T *previous = *object;
  *object = nullptr;
  return previous;
}

// Release a nonnull interface after clearing its caller-owned slot. Reentrant callbacks observe the cleared pointer.
template <typename T> inline void ScRelease(T **object) {
  if (*object)
    ExchNull(object)->Release();
}

// Delete an owned object after clearing its caller-owned slot. A null pointer requires no special handling.
template <typename T> inline void ScDelete(T **object) {
  delete ExchNull(object);
}

// Decrement a positive local reference count and delete the object at zero. Return the saved count without reading freed storage.
template <typename T> inline u32 ScReleaseImpl(T *object, u32 *references) {
  if (*references)
    --*references;
  u32 result = *references;
  if (!result)
    delete object;
  return result;
}

// Reject the interface and clear a supplied output. The surrounding QueryInterface returns immediately.
#define RETURN_QUERY_INTERFACE_FAILURE()                                                                               \
  do {                                                                                                                 \
    if (out)                                                                                                           \
      *out = nullptr;                                                                                                  \
    return false;                                                                                                      \
  } while (false)

// Publish the adjusted interface before its virtual retain, then finish a successful QueryInterface.
// Use inside methods whose parameters are named iid/out; unsupported IDs continue to the next handler or fallback.
#define HANDLE_QUERY_INTERFACE(interface_id, Interface)                                                                \
  do {                                                                                                                 \
    if (iid == (interface_id)) {                                                                                       \
      *out = static_cast<Interface *>(this);                                                                           \
      ScRetain(this);                                                                                                  \
      return true;                                                                                                     \
    }                                                                                                                  \
  } while (false)

// Separate deleting identity used by resource objects. It is not an Unknown interface and has no retain methods.
class ScObject {
public:
  // Destroy the complete resource through its actual secondary deleting identity. The base owns no extra state.
  virtual ~ScObject() = default;
};

#endif
