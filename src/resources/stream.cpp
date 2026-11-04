#include "resources/stream.h"

#include <string.h>

#include "common/api.h"
#include "common/locale.h"
#include "common/rect.h"
#include "common/value.h"

// Record whether the stream published a payload even if its read reported failure.
class RsLegacyTextBuffer : public ScString {
public:
  // Retain a null-terminated assignment for decoding after the stream returns.
  void FromChar(const char *data) override {
    assigned = true;
    ScString::FromChar(data);
  }

  // Retain the complete assigned byte span, including embedded zero bytes.
  void FromChar(const char *data, u32 length) override {
    assigned = true;
    ScString::FromChar(data, length);
  }

  bool assigned = false;
};

// Decode only a published payload, distinguishing conversion failure from the original stream result.
// A failed payload read still publishes decoded text; an absent assignment leaves both text and conversion valid.
bool RsReadLegacyText(RsStream *stream, ScStringView *text, [[maybe_unused]] u32 page, bool *conversion_valid) {
  *conversion_valid = true;
  RsLegacyTextBuffer raw;
  bool result = stream->ReadString(&raw);
  if (raw.assigned) {
    ScString decoded;
    *conversion_valid = ScDecodeLegacyText(page, raw, &decoded);
    if (!*conversion_valid)
      return false;
    text->FromChar(decoded.Data(), decoded.Strlen());
  }
  return result;
}

bool RsReadLegacyText(RsStream *stream, ScStringView *text, u32 page) {
  bool conversion_valid;
  return RsReadLegacyText(stream, text, page, &conversion_valid);
}

// Encode the complete text before writing and preserve the actual stream result.
bool RsWriteLegacyText(RsStream *stream, const ScStringView *text, [[maybe_unused]] u32 page) {
  ScString encoded;
  return ScEncodeLegacyText(page, *text, &encoded) && stream->WriteString(&encoded);
}

bool RsStream::ReadBytes(void *out, u32 length) {
  return ReadBlock(out, length);
}

// A completely written payload is assigned even when the payload read fails.
bool RsStream::ReadString(ScStringView *text) {
  u32 length;
  if (!ReadU32(&length))
    return 0;
  char stack[1024]{}, *heap = length > sizeof(stack) ? new char[length]{} : nullptr, *buffer = heap ? heap : stack;
  bool result = ReadBlock(buffer, length);
  text->FromChar(buffer, length);
  delete[] heap;
  return result;
}

bool RsStream::WriteBytes(const void *data, u32 length) {
  return WriteBlock(data, length);
}

bool RsStream::WriteU32(u32 value) {
  return WriteBlock(&value, 4);
}

bool RsStream::WriteI32(i32 value) {
  return WriteBlock(&value, 4);
}

bool RsStream::WriteF32(f32 value) {
  return WriteBlock(&value, 4);
}

bool RsStream::ReadU16Array(u16 *out, u32 count) {
  return ReadBlock(out, count * 2);
}

bool RsStream::ReadU32Array(u32 *out, u32 count) {
  return ReadBlock(out, count * 4);
}

bool RsStream::WriteU16Array(const u16 *source, u32 count) {
  return WriteBlock(source, count * 2);
}

bool RsStream::WriteU32Array(const u32 *source, u32 count) {
  return WriteBlock(source, count * 4);
}

bool RsStream::ReadU8(u8 *out) {
  return ReadBlock(out, 1);
}

bool RsStream::ReadI8(i8 *out) {
  return ReadBlock(out, 1);
}

bool RsStream::ReadU16(u16 *out) {
  return ReadBlock(out, 2);
}

bool RsStream::ReadI16(i16 *out) {
  return ReadBlock(out, 2);
}

bool RsStream::ReadU32(u32 *out) {
  return ReadBlock(out, 4);
}

bool RsStream::ReadF32(f32 *out) {
  return ReadBlock(out, 4);
}

bool RsStream::ReadI32(i32 *out) {
  return ReadBlock(out, 4);
}

bool RsStream::WriteU8(u8 value) {
  return WriteBlock(&value, 1);
}

bool RsStream::WriteU16(u16 value) {
  return WriteBlock(&value, 2);
}

bool RsStream::WriteI8(i8 value) {
  return WriteBlock(&value, 1);
}

// Write the initial length before re-querying the live string for its payload length and bytes. A failed length
// write stops immediately; the payload uses raw writer172 and preserves its Boolean result.
bool RsStream::WriteString(const ScStringView *text) {
  bool result = WriteU32(text->Strlen());
  if (!result)
    return result;
  u32 length = text->Strlen();
  const char *data = text->Data();
  return WriteBlock(data, length);
}

bool RsStream::ReadI64(i64 *out) {
  return ReadBlock(out, 8);
}

bool RsStream::WriteI64(i64 value) {
  return WriteBlock(&value, 8);
}

// Read the key words through their live scalar virtual in type/group/instance order. Stop on zero and preserve the
// last Boolean result, including partial output writes made by a failing scalar read.
bool RsStream::ReadKey(RsKey *out) {
  bool result = ReadU32(&out->type);
  if (result) {
    result = ReadU32(&out->group);
    if (result)
      result = ReadU32(&out->instance);
  }
  return result;
}

// Write each key word through the live scalar virtual in order, rereading later words after preceding callbacks.
// Stop when a scalar writer fails, preserving all preceding writes and callback effects.
bool RsStream::WriteKey(const RsKey *key) {
  bool result = WriteU32(key->type);
  if (result) {
    result = WriteU32(key->group);
    if (result)
      result = WriteU32(key->instance);
  }
  return result;
}

// Decode through the actual shared concrete variant and original scalar/array/shape virtual transports.
// Publish a retained output only on success; array setters still run after a failed payload transfer before cleanup.
bool RsStream::ReadVariant(ScVariant **out) {
  u32 type;
  bool result = ReadU32(&type);
  if (!result)
    return result;
  ScValue *value = new ScValue;
  value->AddRef();
  result = 0;
  switch (type) {
  case kScValueUint8: {
    u8 data;
    result = ReadU8(&data);
    if (result)
      result = value->SetValUint8(data);
    break;
  }
  case kScValueUint16: {
    u16 data;
    result = ReadU16(&data);
    if (result)
      result = value->SetValUint16(data);
    break;
  }
  case kScValueUint32: {
    u32 data;
    result = ReadU32(&data);
    if (result)
      result = value->SetValUint32(data);
    break;
  }
  case kScValueUint64: {
    u64 data;
    result = ReadBlock(&data, 8);
    if (result)
      result = value->SetValUint64(data);
    break;
  }
  case kScValueSint8: {
    i8 data;
    result = ReadI8(&data);
    if (result)
      result = value->SetValSint8(data);
    break;
  }
  case kScValueSint16: {
    i16 data;
    result = ReadI16(&data);
    if (result)
      result = value->SetValSint16(data);
    break;
  }
  case kScValueSint32: {
    i32 data;
    result = ReadI32(&data);
    if (result)
      result = value->SetValSint32(data);
    break;
  }
  case kScValueSint64: {
    i64 data;
    result = ReadI64(&data);
    if (result)
      result = value->SetValSint64(data);
    break;
  }
  case kScValueFloat32: {
    f32 data;
    result = ReadF32(&data);
    if (result)
      result = value->SetValFloat32(data);
    break;
  }
  case kScValueFloat64: {
    f64 data;
    result = ReadBlock(&data, 8);
    if (result)
      result = value->SetValFloat64(data);
    break;
  }
  case kScValueBool: {
    u8 data;
    result = ReadBlock(&data, 1);
    if (result)
      result = value->SetValBool(data);
    break;
  }
  case kScValueChar: {
    u8 data;
    result = ReadU8(&data);
    if (result)
      result = value->SetValRZChar(static_cast<char>(data));
    break;
  }
  case kScValueData:
  case kScValueUint8Array:
  case kScValueUint16Array:
  case kScValueUint32Array:
  case kScValueUint64Array:
  case kScValueSint8Array:
  case kScValueSint16Array:
  case kScValueSint32Array:
  case kScValueSint64Array:
  case kScValueFloat32Array:
  case kScValueFloat64Array:
  case kScValueBoolArray: {
    u32 count = 0;
    result = ReadU32(&count);
    if (!result)
      break;
    u32 width = 1;
    if (type == kScValueUint16Array || type == kScValueSint16Array)
      width = 2;
    else if (type == kScValueUint32Array || type == kScValueSint32Array || type == kScValueFloat32Array)
      width = 4;
    else if (type == kScValueUint64Array || type == kScValueSint64Array || type == kScValueFloat64Array)
      width = 8;
    u32 bytes = count * width;
    u8 *data = new u8[bytes]{};
    switch (type) {
    case kScValueData:
      result = ReadBlock(data, bytes);
      value->RefData(data, count);
      break;
    case kScValueUint8Array:
      result = ReadBytes(data, count);
      value->RefUint8(data, count);
      break;
    case kScValueUint16Array:
      result = ReadU16Array(reinterpret_cast<u16 *>(data), count);
      value->RefUint16(reinterpret_cast<const u16 *>(data), count);
      break;
    case kScValueUint32Array:
      result = ReadU32Array(reinterpret_cast<u32 *>(data), count);
      value->RefUint32(reinterpret_cast<const u32 *>(data), count);
      break;
    case kScValueUint64Array:
      result = ReadBlock(data, bytes);
      value->RefUint64(reinterpret_cast<const u64 *>(data), count);
      break;
    case kScValueSint8Array:
      result = ReadBytes(data, count);
      value->RefSint8(reinterpret_cast<const i8 *>(data), count);
      break;
    case kScValueSint16Array:
      result = ReadU16Array(reinterpret_cast<u16 *>(data), count);
      value->RefSint16(reinterpret_cast<const i16 *>(data), count);
      break;
    case kScValueSint32Array:
      result = ReadU32Array(reinterpret_cast<u32 *>(data), count);
      value->RefSint32(reinterpret_cast<const i32 *>(data), count);
      break;
    case kScValueSint64Array:
      result = ReadBlock(data, bytes);
      value->RefSint64(reinterpret_cast<const i64 *>(data), count);
      break;
    case kScValueFloat32Array:
      result = ReadF32Array(reinterpret_cast<f32 *>(data), count);
      value->RefFloat32(reinterpret_cast<const f32 *>(data), count);
      break;
    case kScValueFloat64Array:
      result = ReadBlock(data, bytes);
      value->RefFloat64(reinterpret_cast<const f64 *>(data), count);
      break;
    case kScValueBoolArray:
      result = ReadBlock(data, bytes);
      value->RefBool(reinterpret_cast<const bool *>(data), count);
      break;
    }
    delete[] data;
    break;
  }
  case kScValueCharArray: {
    ScString text;
    result = ReadString(&text);
    value->RefRZChar(text.Data(), text.Strlen());
    break;
  }
  case kScValueRect: {
    ScRect rectangle{};
    result = ReadU32(reinterpret_cast<u32 *>(&rectangle.left));
    result = result && ReadU32(reinterpret_cast<u32 *>(&rectangle.top)) &&
        ReadU32(reinterpret_cast<u32 *>(&rectangle.right));
    if (result) {
      result = ReadU32(reinterpret_cast<u32 *>(&rectangle.bottom));
      value->RefRect(&rectangle);
    }
    break;
  }
  case kScValuePoint: {
    ScPoint point{};
    result = ReadU32(reinterpret_cast<u32 *>(&point.x));
    if (result) {
      result = ReadU32(reinterpret_cast<u32 *>(&point.y));
      value->RefPoint(&point);
    }
    break;
  }
  case kScValueSize: {
    alignas(u64) u32 size[2]{};
    result = ReadU32(&size[0]);
    if (result) {
      result = ReadU32(&size[1]);
      value->RefSize(reinterpret_cast<const ScSize *>(size));
    }
    break;
  }
  default:
    break;
  }
  if (result) {
    value->AddRef();
    *out = value;
  }
  value->Release();
  return result;
}

bool RsStream::ReadF32Array(f32 *out, u32 count) {
  return ReadBlock(out, count * 4);
}

bool RsStream::WriteF32Array(const f32 *source, u32 count) {
  return WriteBlock(source, count * 4);
}

// Array and raw eight-byte cases bypass this scalar floating transport entirely.
static f32 RsVariantFloat32(f32 value) {
  u32 bits;
  memcpy(&bits, &value, 4);
  if ((bits & kScFloatExponentQuietMask) != kScFloatInfinityMagnitude || !(bits & kScFloatNaNPayloadMask))
    return value;
  volatile f32 identity = 1.0f;
  return value * identity;
}

// Write the live variant tag before its scalar, array, String or shape payload, preserving each virtual transport.
// Arrays query count again after writing it; unsupported tags still leave their tag word written and return zero.
bool RsStream::WriteVariant(const ScVariant *value) {
  u32 type = value->GetType();
  bool result = WriteU32(type);
  if (!result)
    return result;
  switch (type) {
  case kScValueUint8:
    return WriteU8(*value->AsUint8());
  case kScValueUint16:
    return WriteU16(*value->AsUint16());
  case kScValueUint32:
    return WriteU32(*value->AsUint32());
  case kScValueUint64:
    return WriteBlock(value->AsUint64(), 8);
  case kScValueSint8:
    return WriteU8(static_cast<u8>(*value->AsSint8()));
  case kScValueSint16:
    return WriteU16(static_cast<u16>(*value->AsSint16()));
  case kScValueSint32:
    return WriteU32(static_cast<u32>(*value->AsSint32()));
  case kScValueSint64:
    return WriteBlock(value->AsSint64(), 8);
  case kScValueFloat32:
    return WriteF32(RsVariantFloat32(*value->AsFloat32()));
  case kScValueFloat64:
    return WriteBlock(value->AsFloat64(), 8);
  case kScValueBool:
    return WriteBlock(value->AsBool(), 1);
  case kScValueChar:
    return WriteU8(static_cast<u8>(*value->AsRZChar()));
  case kScValueData:
  case kScValueUint8Array:
  case kScValueUint16Array:
  case kScValueUint32Array:
  case kScValueUint64Array:
  case kScValueSint8Array:
  case kScValueSint16Array:
  case kScValueSint32Array:
  case kScValueSint64Array:
  case kScValueFloat32Array:
  case kScValueFloat64Array:
  case kScValueBoolArray: {
    u32 count = value->GetCount();
    result = WriteU32(count);
    if (!result)
      return result;
    count = value->GetCount();
    switch (type) {
    case kScValueData:
      return WriteBlock(value->RefData(), count);
    case kScValueUint8Array:
      return WriteBytes(value->RefUint8(), count);
    case kScValueUint16Array:
      return WriteU16Array(value->RefUint16(), count);
    case kScValueUint32Array:
      return WriteU32Array(value->RefUint32(), count);
    case kScValueUint64Array:
      return WriteBlock(value->RefUint64(), count * 8);
    case kScValueSint8Array:
      return WriteBytes(value->RefSint8(), count);
    case kScValueSint16Array:
      return WriteU16Array(reinterpret_cast<const u16 *>(value->RefSint16()), count);
    case kScValueSint32Array:
      return WriteU32Array(reinterpret_cast<const u32 *>(value->RefSint32()), count);
    case kScValueSint64Array:
      return WriteBlock(value->RefSint64(), count * 8);
    case kScValueFloat32Array:
      return WriteF32Array(value->RefFloat32(), count);
    case kScValueFloat64Array:
      return WriteBlock(value->RefFloat64(), count * 8);
    case kScValueBoolArray:
      return WriteBlock(value->RefBool(), count);
    }
    return 0;
  }
  case kScValueCharArray: {
    u32 count = value->GetCount();
    char *data = value->RefRZChar();
    ScString text(data, count);
    return WriteString(&text);
  }
  case kScValueRect: {
    ScRect *rectangle = value->RefRect();
    result = WriteU32(static_cast<u32>(rectangle->left));
    result = result && WriteU32(static_cast<u32>(rectangle->top)) && WriteU32(static_cast<u32>(rectangle->right)) &&
        WriteU32(static_cast<u32>(rectangle->bottom));
    return result;
  }
  case kScValuePoint: {
    ScPoint *point = value->RefPoint();
    result = WriteU32(static_cast<u32>(point->x));
    if (result)
      result = WriteU32(static_cast<u32>(point->y));
    return result;
  }
  case kScValueSize: {
    ScSize *size = value->RefSize();
    u32 word;
    memcpy(&word, size, 4);
    result = WriteU32(word);
    if (result) {
      memcpy(&word, reinterpret_cast<const u8 *>(size) + 4, 4);
      result = WriteU32(word);
    }
    return result;
  }
  default:
    return 0;
  }
}

bool RsStream::ReadI8Array(i8 *out, u32 count) {
  return ReadBlock(out, count);
}

bool RsStream::ReadI16Array(i16 *out, u32 count) {
  return ReadBlock(out, count * 2);
}

bool RsStream::ReadI32Array(i32 *out, u32 count) {
  return ReadBlock(out, count * 4);
}

bool RsStream::ReadI64Array(i64 *out, u32 count) {
  return ReadBlock(out, count * 8);
}

bool RsStream::WriteI8Array(const i8 *source, u32 count) {
  return WriteBlock(source, count);
}

bool RsStream::WriteI16Array(const i16 *source, u32 count) {
  return WriteBlock(source, count * 2);
}

bool RsStream::WriteI16(i16 value) {
  return WriteBlock(&value, 2);
}

bool RsStream::WriteI32Array(const i32 *source, u32 count) {
  return WriteBlock(source, count * 4);
}

bool RsStream::WriteI64Array(const i64 *source, u32 count) {
  return WriteBlock(source, count * 8);
}
