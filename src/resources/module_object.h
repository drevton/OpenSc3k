#ifndef SC3_RESOURCES_MODULE_OBJECT_H
#define SC3_RESOURCES_MODULE_OBJECT_H

#include "common/sc3_unknown.h"

// This original secondary base owns references in both the object and its actual module director.
class RsModuleObject : public ScUnknown {
public:
  RsModuleObject();
  virtual ~RsModuleObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 references;
};

#endif
