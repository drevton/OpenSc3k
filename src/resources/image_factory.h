#ifndef SC3_RESOURCES_IMAGE_FACTORY_H
#define SC3_RESOURCES_IMAGE_FACTORY_H

#include <deque>

#include "resources/api.h"
#include "resources/bitmap_record.h"
#include "resources/factory.h"

// Original image factory with distinct options and module-reference identities and its real encoding deque.
class RsImageFactory : public RsFactory, public RsImageOptions {
public:
  RsImageFactory();
  ~RsImageFactory() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
  bool LoadFile(ScString *name, GxBuffer **out, u8 compressed, const GxColorType *format, u32 unused) override;
  u8 SaveFile(const ScStringView *name, GxBuffer *source) override;
  u8 LoadModuleBitmap(HINSTANCE__ *module, u16 identifier, GxBuffer **out) override;
  u8 LoadExecutableBitmap(
      u16 identifier, GxBuffer **out, u8 compressed, const GxColorType *format, u32 unused) override;
  u8 LoadStream(GxBuffer **out, RsStream *stream, u32 unused) override;
  u8 SaveStream(GxBuffer *source, RsStream *stream) override;
  void SetMode(GxVramRequest mode) override;
  void SetFormat(const GxColorType &format) override;
  bool PushEncoding(u32 encoding) override;
  bool PopEncoding() override;
  u32 GetEncoding() override;

private:
  GxVramRequest mode;
  GxColorType format;
  std::deque<u32> encodings;
};

#endif
