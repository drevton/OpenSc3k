#ifndef SC3_SPRITES_SP_DEFINITION_FACTORY_H
#define SC3_SPRITES_SP_DEFINITION_FACTORY_H

#include "resources/factory.h"
#include "sprites/module.h"

// SIMSPR's own copy of the resource-factory base keeps a separate sprite-module reference identity.
// The original factory server-lock operation occupies slot28.
class RsSpriteFactory : public RsFactoryInterface, public SpModuleObject {
public:
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Load(RsRecord *record, u32 iid, void **out) override;
  bool ReadRecord(RsResource *resource, RsRecord *record) override;
  bool WriteRecord(const RsResource *resource, RsRecord *record) override;
  // The private nominal Boolean/BYTE spellings remain unresolved; these preserve the installed AL carriers.
  u8 LockServer(u32 locks) override;
  virtual u8 SerialRead(RsResource *resource, RsStream *stream) = 0;
  virtual u8 SerialWrite(RsResource *resource, RsStream *stream) = 0;
  ~RsSpriteFactory() override;
};

// The registered sprite-attribute factory forwards construction and dispatches the definition's stream methods.
class SpDefinitionFactory : public RsSpriteFactory {
public:
  SpDefinitionFactory();
  ~SpDefinitionFactory() override;
  bool New(u32 type, u32 iid, void **out) override;
  u8 SerialRead(RsResource *resource, RsStream *stream) override;
  u8 SerialWrite(RsResource *resource, RsStream *stream) override;
};

ScUnknown *SpCreateDefinitionFactory();
ScUnknown *SpCreateDefinition();
ScUnknown *SpCreateScriptDefinition();
ScUnknown *SpCreateScriptSprite();
ScUnknown *SpCreateInstance();
ScUnknown *SpCreateLayerSprite();

#endif
