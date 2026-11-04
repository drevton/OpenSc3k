#include <cstdio>
#include <cstring>

#include "common/sc3_stream.h"

ScTextOutputStream::ScTextOutputStream() : references(0) {
}

ScTextOutputStream::~ScTextOutputStream() {
}

bool ScTextOutputStream::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScTextOutputStream);
  HANDLE_QUERY_INTERFACE(IID_ScOutputStream, ScTextOutputStream);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScTextOutputStream::AddRef() {
  return ++references;
}

// Delete the stream when its last reference is released.
u32 ScTextOutputStream::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetSint8(i8 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetUint8(u8 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetSint16(i16 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetUint16(u16 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetSint32(i32 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetUint32(u32 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
  SetRZCharStr(buffer);
}

// Format the low 32-bit word as a signed decimal value and send it to the character transport.
void ScTextOutputStream::SetSint64(i64 value) {
  u32 word = static_cast<u32>(value);
  i32 signed_word;
  std::memcpy(&signed_word, &word, sizeof(signed_word));
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(signed_word));
  SetRZCharStr(buffer);
}

// Format the low 32-bit word as an unsigned decimal value and send it to the character transport.
void ScTextOutputStream::SetUint64(u64 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(static_cast<u32>(value)));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetFloat32(f32 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value));
  SetRZCharStr(buffer);
}

// Format the value as text and pass it to the derived character transport.
void ScTextOutputStream::SetFloat64(f64 value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%f", value);
  SetRZCharStr(buffer);
}

void ScTextOutputStream::SetGZStr(const ScStringView &value) {
  SetRZCharStr(value.ToChar());
}

void ScTextOutputStream::SetGZSerializable(const ScSerializable &value) {
  value.Write(this);
}

// Write a labeled hexadecimal dump, inserting a newline after every 25 bytes.
void ScTextOutputStream::SetVoid(const void *data, u32 length) {
  const i8 *bytes = static_cast<const i8 *>(data);
  char buffer[256];
  SetRZCharStr("\nHEXDUMP:\n");
  for (u32 i = 0; i < length; ++i) {
    std::snprintf(buffer, sizeof(buffer), "%02x ", static_cast<unsigned int>(static_cast<int>(bytes[i])));
    SetRZCharStr(buffer);
    if ((i + 1) % 25 == 0)
      SetRZCharStr("\n");
  }
  SetRZCharStr("\n");
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(const ScSerializable &value) {
  SetGZSerializable(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(const ScStringView &value) {
  SetGZStr(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(const char *value) {
  SetRZCharStr(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(f64 value) {
  SetFloat64(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(f32 value) {
  SetFloat32(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(u64 value) {
  SetUint64(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(i64 value) {
  SetSint64(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(u32 value) {
  SetUint32(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(i32 value) {
  SetSint32(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(u16 value) {
  SetUint16(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(i16 value) {
  SetSint16(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(u8 value) {
  SetUint8(value);
  return this;
}

// Forward the value to its corresponding text writer and return the stream.
ScOutputStream *ScTextOutputStream::operator<<(i8 value) {
  SetSint8(value);
  return this;
}

static u16 SC3_STDCALL ScStreamNative16(u16 value) {
  return value;
}

static u16 SC3_STDCALL ScStreamSwap16(u16 value) {
  return static_cast<u16>((value << 8) | (value >> 8));
}

static u32 SC3_STDCALL ScStreamNative32(u32 value) {
  return value;
}

static u32 SC3_STDCALL ScStreamSwap32(u32 value) {
  return ((value & 0xffu) << 24) | ((value & 0xff00u) << 8) | ((value >> 8) & 0xff00u) | (value >> 24);
}

static u64 SC3_STDCALL ScStreamNative64(u64 value) {
  return value;
}

static u64 SC3_STDCALL ScStreamSwap64(u64 value) {
  return (static_cast<u64>(ScStreamSwap32(static_cast<u32>(value))) << 32) |
      ScStreamSwap32(static_cast<u32>(value >> 32));
}

ScOutputStreamBase::ScOutputStreamBase() : references(0), convert16(nullptr), convert32(nullptr), convert64(nullptr) {
}

ScOutputStreamBase::ScOutputStreamBase(bool native_order) : references(0) {
  Init(native_order);
}

ScOutputStreamBase::~ScOutputStreamBase() {
}

// Select native or reversed integer encoding. Floating-point values retain their original raw byte encoding.
bool ScOutputStreamBase::Init(bool native_order) {
  convert16 = native_order ? ScStreamNative16 : ScStreamSwap16;
  convert32 = native_order ? ScStreamNative32 : ScStreamSwap32;
  convert64 = native_order ? ScStreamNative64 : ScStreamSwap64;
  return true;
}

// Clear the converters when stream processing shuts down.
bool ScOutputStreamBase::Shutdown() {
  convert16 = nullptr;
  convert32 = nullptr;
  convert64 = nullptr;
  return true;
}

// Publish the supported primary identity and retain it. Rejected queries clear the output.
bool ScOutputStreamBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOutputStreamBase);
  HANDLE_QUERY_INTERFACE(IID_ScOutputStream, ScOutputStreamBase);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScOutputStreamBase::AddRef() {
  return ++references;
}

// Retire the stream when its old count is at most one; otherwise decrement the live count.
u32 ScOutputStreamBase::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

void ScOutputStreamBase::SetSint8(i8 value) {
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetUint8(u8 value) {
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetSint16(i16 value) {
  value = static_cast<i16>(convert16(static_cast<u16>(value)));
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetUint16(u16 value) {
  value = convert16(value);
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetSint32(i32 value) {
  value = static_cast<i32>(convert32(static_cast<u32>(value)));
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetUint32(u32 value) {
  value = convert32(value);
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetSint64(i64 value) {
  value = static_cast<i64>(convert64(static_cast<u64>(value)));
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetUint64(u64 value) {
  value = convert64(value);
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetFloat32(f32 value) {
  SetVoid(&value, sizeof(value));
}

void ScOutputStreamBase::SetFloat64(f64 value) {
  SetVoid(&value, sizeof(value));
}

// Store the low byte of the C-string length, followed by exactly that many source bytes.
void ScOutputStreamBase::SetRZCharStr(const char *value) {
  u8 length = static_cast<u8>(strlen(value));
  SetUint8(length);
  if (length)
    SetVoid(value, length);
}

// Write the variable-length string size in seven-bit groups, followed by the original string bytes.
void ScOutputStreamBase::SetGZStr(const ScStringView &value) {
  u32 length = value.Strlen(), remaining = length;
  do {
    u8 part = static_cast<u8>(remaining & 0x7f);
    remaining >>= 7;
    if (remaining)
      part |= 0x80;
    *this << part;
  } while (remaining);
  if (length)
    SetVoid(value.ToChar(), length);
}

void ScOutputStreamBase::SetGZSerializable(const ScSerializable &value) {
  value.Write(this);
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(i8 value) {
  SetSint8(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(u8 value) {
  SetUint8(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(i16 value) {
  SetSint16(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(u16 value) {
  SetUint16(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(i32 value) {
  SetSint32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(u32 value) {
  SetUint32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(i64 value) {
  SetSint64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(u64 value) {
  SetUint64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(f32 value) {
  SetFloat32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(f64 value) {
  SetFloat64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(const char *value) {
  SetRZCharStr(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(const ScStringView &value) {
  SetGZStr(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScOutputStream *ScOutputStreamBase::operator<<(const ScSerializable &value) {
  SetGZSerializable(value);
  return this;
}

ScNullOutputStream::ScNullOutputStream() : ScOutputStreamBase(false) {
}

ScNullOutputStream::~ScNullOutputStream() {
}

void ScNullOutputStream::Flush() {
}

void ScNullOutputStream::SetVoid(const void *, u32) {
}

ScInputStreamBase::ScInputStreamBase() : references(0), convert16(nullptr), convert32(nullptr), convert64(nullptr) {
}

ScInputStreamBase::ScInputStreamBase(bool native_order) : references(0) {
  Init(native_order);
}

ScInputStreamBase::~ScInputStreamBase() {
}

// Select native or reversed integer encoding. Floating-point values retain their original raw byte encoding.
bool ScInputStreamBase::Init(bool native_order) {
  convert16 = native_order ? ScStreamNative16 : ScStreamSwap16;
  convert32 = native_order ? ScStreamNative32 : ScStreamSwap32;
  convert64 = native_order ? ScStreamNative64 : ScStreamSwap64;
  return true;
}

// Clear the converters when stream processing shuts down.
bool ScInputStreamBase::Shutdown() {
  convert16 = nullptr;
  convert32 = nullptr;
  convert64 = nullptr;
  return true;
}

// Publish the supported primary identity and retain it. Rejected queries clear the output.
bool ScInputStreamBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScInputStreamBase);
  HANDLE_QUERY_INTERFACE(IID_ScInputStream, ScInputStreamBase);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScInputStreamBase::AddRef() {
  return ++references;
}

// Retire the stream when its old count is at most one; otherwise decrement the live count.
u32 ScInputStreamBase::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

void ScInputStreamBase::GetSint8(i8 *value) {
  GetVoid(value, sizeof(*value));
}

void ScInputStreamBase::GetUint8(u8 *value) {
  GetVoid(value, sizeof(*value));
}

void ScInputStreamBase::GetSint16(i16 *value) {
  GetVoid(value, sizeof(*value));
  *value = static_cast<i16>(convert16(static_cast<u16>(*value)));
}

void ScInputStreamBase::GetUint16(u16 *value) {
  GetVoid(value, sizeof(*value));
  *value = convert16((*value));
}

void ScInputStreamBase::GetSint32(i32 *value) {
  GetVoid(value, sizeof(*value));
  *value = static_cast<i32>(convert32(static_cast<u32>(*value)));
}

void ScInputStreamBase::GetUint32(u32 *value) {
  GetVoid(value, sizeof(*value));
  *value = convert32((*value));
}

void ScInputStreamBase::GetSint64(i64 *value) {
  GetVoid(value, sizeof(*value));
  *value = static_cast<i64>(convert64(static_cast<u64>(*value)));
}

void ScInputStreamBase::GetUint64(u64 *value) {
  GetVoid(value, sizeof(*value));
  *value = convert64((*value));
}

void ScInputStreamBase::GetFloat32(f32 *value) {
  GetVoid(value, sizeof(*value));
}

void ScInputStreamBase::GetFloat64(f64 *value) {
  GetVoid(value, sizeof(*value));
}

// Read the byte-sized length and bounded payload, then terminate at the encoded length as in the original.
// Initialize the local length to exclude an undefined stack read when a null transport supplies no byte.
void ScInputStreamBase::GetRZCharStr(char *value, u32 capacity) {
  u8 length = 0;
  GetUint8(&length);
  if (!GetError() && length)
    GetVoid(value, length < capacity ? length : capacity);
  value[length] = 0;
}

// Decode the seven-bit length and append text in chunks of at most255 bytes.
void ScInputStreamBase::GetGZStr(ScStringView *value) {
  u8 parts[8] = {};
  u32 index = 0, length = 0;
  *this >> &parts[0];
  while (parts[index] & 0x80) {
    parts[index] &= 0x7f;
    if (++index == sizeof(parts))
      ScFatal(SC_ERROR_INVALID, "Invalid stream string length");
    *this >> &parts[index];
  }
  do {
    length = (length << 7) | parts[index];
  } while (index--);
  while (!GetError() && length) {
    char buffer[256] = {};
    u32 count = length < 256 ? length : 255;
    GetVoid(buffer, count);
    buffer[count] = 0;
    value->VStrcat(buffer);
    length -= count;
  }
}

void ScInputStreamBase::GetGZSerializable(ScSerializable *value) {
  value->Read(this);
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(i8 *value) {
  GetSint8(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(u8 *value) {
  GetUint8(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(i16 *value) {
  GetSint16(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(u16 *value) {
  GetUint16(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(i32 *value) {
  GetSint32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(u32 *value) {
  GetUint32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(i64 *value) {
  GetSint64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(u64 *value) {
  GetUint64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(f32 *value) {
  GetFloat32(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(f64 *value) {
  GetFloat64(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(ScStringView *value) {
  GetGZStr(value);
  return this;
}

// Forward to the corresponding virtual scalar or object operation and return the same stream identity.
ScInputStream *ScInputStreamBase::operator>>(ScSerializable *value) {
  GetGZSerializable(value);
  return this;
}

ScNullInputStream::ScNullInputStream() : ScInputStreamBase(false), error(false) {
}

ScNullInputStream::~ScNullInputStream() {
}

void ScNullInputStream::Skip(u32) {
  error = true;
}

void ScNullInputStream::GetVoid(void *, u32) {
  error = true;
}

bool ScNullInputStream::GetError() const {
  return error;
}
