#ifndef SC3_RESOURCES_ACCELERATOR_RESOURCE_H
#define SC3_RESOURCES_ACCELERATOR_RESOURCE_H

#include <map>

#include "resources/factory.h"
#include "resources/resource.h"
#include "window/api.h"

class RsStream;

// Keyboard bindings retain their packed modifier/key ordering and actual command messages.
class RsAcceleratorResources : public WnAcceleratorResources, public RsResource {
public:
  RsAcceleratorResources();
  ~RsAcceleratorResources() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool RegisterResources(WnAccelerator *accelerator) override;
  virtual u8 Read(RsStream *stream);
  virtual u8 Write(RsStream *stream);

private:
  std::map<u32, ScMessage> bindings;
};

// This factory produces only the installed text keyboard-binding resource type.
class RsAcceleratorFactory : public RsFactory {
public:
  RsAcceleratorFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

#endif
