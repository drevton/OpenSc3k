#ifndef SC3_COMMON_SC3_STREAM_H
#define SC3_COMMON_SC3_STREAM_H

#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

class ScOutputStream;
class ScInputStream;

// Retail cIGZSerializable virtual order, verified by CodeView50A6 and stream callers478B4E/478F29.
class ScSerializable : public ScUnknown {
public:
  virtual bool Write(ScOutputStream *stream) const = 0;
  virtual bool Read(ScInputStream *stream) = 0;
  virtual u32 GetGZCLSID() const = 0;
};

// Original cIGZOStream primary identity; mutable references use explicit pointers.
class ScOutputStream : public ScUnknown {
public:
  virtual void Flush() = 0;
  virtual void SetSint8(i8 value) = 0;
  virtual void SetUint8(u8 value) = 0;
  virtual void SetSint16(i16 value) = 0;
  virtual void SetUint16(u16 value) = 0;
  virtual void SetSint32(i32 value) = 0;
  virtual void SetUint32(u32 value) = 0;
  virtual void SetSint64(i64 value) = 0;
  virtual void SetUint64(u64 value) = 0;
  virtual void SetFloat32(f32 value) = 0;
  virtual void SetFloat64(f64 value) = 0;
  virtual void SetRZCharStr(const char *value) = 0;
  virtual void SetGZStr(const ScStringView &value) = 0;
  virtual void SetGZSerializable(const ScSerializable &value) = 0;
  virtual void SetVoid(const void *data, u32 length) = 0;
  virtual ScOutputStream *operator<<(const ScSerializable &value) = 0;
  virtual ScOutputStream *operator<<(const ScStringView &value) = 0;
  virtual ScOutputStream *operator<<(const char *value) = 0;
  virtual ScOutputStream *operator<<(f64 value) = 0;
  virtual ScOutputStream *operator<<(f32 value) = 0;
  virtual ScOutputStream *operator<<(u64 value) = 0;
  virtual ScOutputStream *operator<<(i64 value) = 0;
  virtual ScOutputStream *operator<<(u32 value) = 0;
  virtual ScOutputStream *operator<<(i32 value) = 0;
  virtual ScOutputStream *operator<<(u16 value) = 0;
  virtual ScOutputStream *operator<<(i16 value) = 0;
  virtual ScOutputStream *operator<<(u8 value) = 0;
  virtual ScOutputStream *operator<<(i8 value) = 0;
};

// Original cRZTextOStream formats values for a derived text transport.
// Its 64-bit formatters pass the low word to the 32-bit decimal conversion in the installed x86 binary.
class ScTextOutputStream : public ScOutputStream {
public:
  ScTextOutputStream();
  virtual ~ScTextOutputStream();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetSint8(i8 value) override;
  void SetUint8(u8 value) override;
  void SetSint16(i16 value) override;
  void SetUint16(u16 value) override;
  void SetSint32(i32 value) override;
  void SetUint32(u32 value) override;
  void SetSint64(i64 value) override;
  void SetUint64(u64 value) override;
  void SetFloat32(f32 value) override;
  void SetFloat64(f64 value) override;
  void SetGZStr(const ScStringView &value) override;
  void SetGZSerializable(const ScSerializable &value) override;
  void SetVoid(const void *data, u32 length) override;
  ScOutputStream *operator<<(const ScSerializable &value) override;
  ScOutputStream *operator<<(const ScStringView &value) override;
  ScOutputStream *operator<<(const char *value) override;
  ScOutputStream *operator<<(f64 value) override;
  ScOutputStream *operator<<(f32 value) override;
  ScOutputStream *operator<<(u64 value) override;
  ScOutputStream *operator<<(i64 value) override;
  ScOutputStream *operator<<(u32 value) override;
  ScOutputStream *operator<<(i32 value) override;
  ScOutputStream *operator<<(u16 value) override;
  ScOutputStream *operator<<(i16 value) override;
  ScOutputStream *operator<<(u8 value) override;
  ScOutputStream *operator<<(i8 value) override;

protected:
  u32 references;
};

// Original cRZOStream owns its reference count and three byte-order conversion functions.
class ScOutputStreamBase : public ScOutputStream {
public:
  ScOutputStreamBase();
  explicit ScOutputStreamBase(bool native_order);
  virtual ~ScOutputStreamBase();
  virtual bool Init(bool native_order);
  virtual bool Shutdown();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetSint8(i8 value) override;
  void SetUint8(u8 value) override;
  void SetSint16(i16 value) override;
  void SetUint16(u16 value) override;
  void SetSint32(i32 value) override;
  void SetUint32(u32 value) override;
  void SetSint64(i64 value) override;
  void SetUint64(u64 value) override;
  void SetFloat32(f32 value) override;
  void SetFloat64(f64 value) override;
  void SetRZCharStr(const char *value) override;
  void SetGZStr(const ScStringView &value) override;
  void SetGZSerializable(const ScSerializable &value) override;
  ScOutputStream *operator<<(const ScSerializable &value) override;
  ScOutputStream *operator<<(const ScStringView &value) override;
  ScOutputStream *operator<<(const char *value) override;
  ScOutputStream *operator<<(f64 value) override;
  ScOutputStream *operator<<(f32 value) override;
  ScOutputStream *operator<<(u64 value) override;
  ScOutputStream *operator<<(i64 value) override;
  ScOutputStream *operator<<(u32 value) override;
  ScOutputStream *operator<<(i32 value) override;
  ScOutputStream *operator<<(u16 value) override;
  ScOutputStream *operator<<(i16 value) override;
  ScOutputStream *operator<<(u8 value) override;
  ScOutputStream *operator<<(i8 value) override;

protected:
  u32 references;
  u16(SC3_STDCALL *convert16)(u16);
  u32(SC3_STDCALL *convert32)(u32);
  u64(SC3_STDCALL *convert64)(u64);
};

// Original cRZNullOStream discards all writes.
class ScNullOutputStream : public ScOutputStreamBase {
public:
  ScNullOutputStream();
  ~ScNullOutputStream() override;
  void Flush() override;
  void SetVoid(const void *data, u32 length) override;
};

// Original cIGZIStream primary identity; mutable references use explicit pointers.
class ScInputStream : public ScUnknown {
public:
  virtual void Skip(u32 length) = 0;
  virtual void GetSint8(i8 *value) = 0;
  virtual void GetUint8(u8 *value) = 0;
  virtual void GetSint16(i16 *value) = 0;
  virtual void GetUint16(u16 *value) = 0;
  virtual void GetSint32(i32 *value) = 0;
  virtual void GetUint32(u32 *value) = 0;
  virtual void GetSint64(i64 *value) = 0;
  virtual void GetUint64(u64 *value) = 0;
  virtual void GetFloat32(f32 *value) = 0;
  virtual void GetFloat64(f64 *value) = 0;
  virtual void GetRZCharStr(char *value, u32 capacity) = 0;
  virtual void GetGZStr(ScStringView *value) = 0;
  virtual void GetGZSerializable(ScSerializable *value) = 0;
  virtual void GetVoid(void *data, u32 length) = 0;
  virtual bool GetError() const = 0;
  virtual ScInputStream *operator>>(ScSerializable *value) = 0;
  virtual ScInputStream *operator>>(ScStringView *value) = 0;
  virtual ScInputStream *operator>>(f64 *value) = 0;
  virtual ScInputStream *operator>>(f32 *value) = 0;
  virtual ScInputStream *operator>>(u64 *value) = 0;
  virtual ScInputStream *operator>>(i64 *value) = 0;
  virtual ScInputStream *operator>>(u32 *value) = 0;
  virtual ScInputStream *operator>>(i32 *value) = 0;
  virtual ScInputStream *operator>>(u16 *value) = 0;
  virtual ScInputStream *operator>>(i16 *value) = 0;
  virtual ScInputStream *operator>>(u8 *value) = 0;
  virtual ScInputStream *operator>>(i8 *value) = 0;
};

// Original cRZIStream owns its reference count and three byte-order conversion functions.
class ScInputStreamBase : public ScInputStream {
public:
  ScInputStreamBase();
  explicit ScInputStreamBase(bool native_order);
  virtual ~ScInputStreamBase();
  virtual bool Init(bool native_order);
  virtual bool Shutdown();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void GetSint8(i8 *value) override;
  void GetUint8(u8 *value) override;
  void GetSint16(i16 *value) override;
  void GetUint16(u16 *value) override;
  void GetSint32(i32 *value) override;
  void GetUint32(u32 *value) override;
  void GetSint64(i64 *value) override;
  void GetUint64(u64 *value) override;
  void GetFloat32(f32 *value) override;
  void GetFloat64(f64 *value) override;
  void GetRZCharStr(char *value, u32 capacity) override;
  void GetGZStr(ScStringView *value) override;
  void GetGZSerializable(ScSerializable *value) override;
  ScInputStream *operator>>(ScSerializable *value) override;
  ScInputStream *operator>>(ScStringView *value) override;
  ScInputStream *operator>>(f64 *value) override;
  ScInputStream *operator>>(f32 *value) override;
  ScInputStream *operator>>(u64 *value) override;
  ScInputStream *operator>>(i64 *value) override;
  ScInputStream *operator>>(u32 *value) override;
  ScInputStream *operator>>(i32 *value) override;
  ScInputStream *operator>>(u16 *value) override;
  ScInputStream *operator>>(i16 *value) override;
  ScInputStream *operator>>(u8 *value) override;
  ScInputStream *operator>>(i8 *value) override;

protected:
  u32 references;
  u16(SC3_STDCALL *convert16)(u16);
  u32(SC3_STDCALL *convert32)(u32);
  u64(SC3_STDCALL *convert64)(u64);
};

// Original cRZNullIStream records a failed read without changing the destination.
class ScNullInputStream : public ScInputStreamBase {
public:
  ScNullInputStream();
  ~ScNullInputStream() override;
  void Skip(u32 length) override;
  void GetVoid(void *data, u32 length) override;
  bool GetError() const override;

private:
  bool error;
};

#endif
