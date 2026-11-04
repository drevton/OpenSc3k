#ifndef SC3_RESOURCES_PROPERTY_SET_H
#define SC3_RESOURCES_PROPERTY_SET_H

#include <map>

#include "resources/api.h"

class RsStream;
class RsPropertyValue;
class RsPropertyReader;

// Original property-data producer (constructor3936/table1C62C), separate from the enclosing resource object.
// Its retained stream, format flags and ordered property map are real state, not a foreign object overlay.
class RsProperties : public RsPropertySet {
public:
  RsProperties();
  virtual ~RsProperties();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown();
  u32 ReadCookieFromSerialRecord(RsStream *stream);
  bool WriteCookieToSerialRecord(RsStream *stream, u32 cookie);
  bool Read(RsStream *stream) override;
  bool ReadWithFormat(RsStream *stream, u32 format) override;
  bool InitText(RsStream *stream) override;
  bool InitBinary(RsStream *stream) override;
  bool CopyKey(RsKey *out) const override;
  u32 GetFormat() const override;
  bool IsText() const override;
  bool HasProperty(const RsProperty &property) override;
  bool GetByte(const RsProperty &property, u8 *out, u32 index) override;
  bool GetWord(const RsProperty &property, u32 *out, u32 index) override;
  bool GetKey(const RsProperty &property, RsKey *out, u32 index) override;
  bool SetByte(const RsProperty &property, const u8 &value, u32 index) override;
  bool SetWord(const RsProperty &property, const u32 &value, u32 index) override;
  bool SetText(const RsProperty &property, const ScStringView &value, u32 index) override;
  bool SetKey(const RsProperty &property, const RsKey &value, u32 index) override;

  bool GetU16(const RsProperty &property, u16 *out, u32 index) override;
  bool SetU16(const RsProperty &property, const u16 &value, u32 index) override;
  bool GetI32(const RsProperty &property, i32 *out, u32 index) override;
  bool SetI32(const RsProperty &property, const i32 &value, u32 index) override;
  bool GetI64(const RsProperty &property, i64 *out, u32 index) override;
  bool SetI64(const RsProperty &property, const i64 &value, u32 index) override;
  bool GetF32(const RsProperty &property, f32 *out, u32 index) override;
  bool SetF32(const RsProperty &property, const f32 &value, u32 index) override;
  bool GetText(const RsProperty &property, ScStringView *out, u32 index) override;
  bool SetName(const RsProperty &property, ScStringView *name) override;

private:
  bool Find(const RsProperty &property, RsPropertyValue **out);
  bool Create(const RsProperty &property, RsPropertyValue **out);
  bool ReadFormat(RsStream *stream);
  bool ReadProperties(RsPropertyReader *reader);
  bool WriteProperties(RsPropertyReader *writer);
  u32 references;
  RsStream *source;
  u32 format;
  u8 initialized, text, writable;
  std::map<u32, RsPropertyValue *> entries;
};

#endif
