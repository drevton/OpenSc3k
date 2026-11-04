#include "common/sc3_string.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/sc3_error.h"
#include "common/utf8.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Bind formatting to the Windows CRT used by the game, retaining its historical format rules.
class ScStringCrt {
public:
  // Retain the actual imported runtime so its historical formatting rules remain available.
  ScStringCrt() : module(LoadLibraryA("msvcrt.dll")) {
    if (!module)
      ScFatal(SC_ERROR_INVALID, "String operations require the Windows MSVCRT runtime");
    Format =
        reinterpret_cast<i32(__cdecl *)(char *, usize, const char *, va_list)>(GetProcAddress(module, "_vsnprintf"));
    Measure = reinterpret_cast<i32(__cdecl *)(const char *, va_list)>(GetProcAddress(module, "_vscprintf"));
    if (!Format || !Measure)
      ScFatal(SC_ERROR_INVALID, "String operations require the legacy MSVCRT exports");
  }

  // Release the native module reference after its last process-level use.
  ~ScStringCrt() {
    FreeLibrary(module);
  }

  i32(__cdecl *Format)(char *, usize, const char *, va_list);
  i32(__cdecl *Measure)(const char *, va_list);

private:
  HMODULE module;
};
#else
class ScStringCrt {
public:
  // Report the required length through the host formatter. Truncated output follows the old CRT failure convention.
  static i32 Format(char *buffer, usize capacity, const char *format, va_list arguments) {
    i32 length = vsnprintf(buffer, capacity, format, arguments);
    return length < 0 || static_cast<usize>(length) >= capacity ? -1 : length;
  }

  // Measure without consuming the caller's argument list. Sprintf owns and retires the copied va_list.
  static i32 Measure(const char *format, va_list arguments) {
    return vsnprintf(nullptr, 0, format, arguments);
  }
};
#endif

// Share one retained runtime binding across String formatting calls.
static ScStringCrt *ScGetStringCrt() {
  static ScStringCrt runtime;
  return &runtime;
}

// Decode one bounded C-string scalar without reading past its terminator or byte limit.
// Incomplete or non-text bytes compare literally, preserving callers that use strings for binary identifiers.
static u32 ScStringScalar(const char *text, usize length, u32 *scalar) {
  if (static_cast<u8>(*text) < 0x80) {
    *scalar = static_cast<u8>(*text);
    return 1;
  }
  u32 available = 0;
  while (available < length && available < 4 && text[available])
    ++available;
  u32 count = ScUtf8Decode(text, available, scalar);
  if (count)
    return count;
  *scalar = static_cast<u8>(*text);
  return 1;
}

int ScCompareNoCase(const char *first, const char *second) {
  return ScCompareNoCase(first, second, SIZE_MAX);
}

// Compare each input within its own byte limit, stopping at an earlier NUL as the original bounded API does.
// A limit inside a multibyte scalar compares that incomplete suffix literally rather than reading beyond it.
int ScCompareNoCase(const char *first, const char *second, usize length) {
  usize firstLeft = length, secondLeft = length;
  while (firstLeft && secondLeft) {
    u32 a, b, firstCount = ScStringScalar(first, firstLeft, &a);
    u32 secondCount = ScStringScalar(second, secondLeft, &b);
    a = ScUnicodeLower(a);
    b = ScUnicodeLower(b);
    if (a != b)
      return static_cast<int>(a) - static_cast<int>(b);
    if (!a)
      return 0;
    first += firstCount;
    second += secondCount;
    firstLeft -= firstCount;
    secondLeft -= secondCount;
  }
  return 0;
}

// Lowercase ASCII in a fixed-size C buffer without corrupting UTF-8 continuation bytes.
// Owned strings use MakeLower when a Unicode mapping can change the encoded byte length.
void ScLowerCase(char *text) {
  for (char *byte = text; *byte; ++byte)
    if (*byte >= 'A' && *byte <= 'Z')
      *byte += 'a' - 'A';
}

// Uppercase ASCII in a fixed-size C buffer, preserving all multibyte text and the allocation's extent.
// Owned strings use MakeUpper for Unicode mappings that require more or fewer bytes.
void ScUpperCase(char *text) {
  for (char *byte = text; *byte; ++byte)
    if (*byte >= 'a' && *byte <= 'z')
      *byte -= 'a' - 'A';
}

// Assemble the original byte callback into one complete UTF-8 scalar before exposing text to game observers.
// Invalid or interrupted sequences are discarded without inventing replacement text or a legacy code page.
bool ScAppendUtf8Byte(ScString *pending, u8 byte, u32 *scalar) {
  if ((byte & 0xc0) != 0x80)
    pending->Erase(0, kScStringToEnd);
  if (!pending->ScString::Strlen() && byte >= 0x80 && (byte < 0xc2 || byte > 0xf4))
    return false;
  pending->Append(&byte, 1);
  u8 first = static_cast<u8>(pending->begin[0]);
  u32 expected = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
  if (pending->ScString::Strlen() < expected)
    return false;
  if (ScUtf8Decode(pending->begin, pending->ScString::Strlen(), scalar) == expected)
    return true;
  pending->Erase(0, kScStringToEnd);
  return false;
}

// Format with the original CRT rules using a small stack buffer, allocating exact storage only for larger results.
// Keep the receiver's old storage alive until formatting completes so its contents may supply the format or arguments.
ScString &SC3_CDECL ScString::Sprintf(const char *format, ...) {
  ScStringCrt *crt = ScGetStringCrt();
  char local[4096];
  va_list arguments, formatting;
  va_start(arguments, format);
  va_copy(formatting, arguments);
  i32 length = crt->Format(local, sizeof(local), format, formatting);
  va_end(formatting);
  if (length >= 0 && usize(length) <= sizeof(local)) {
    va_end(arguments);
    ScString::FromChar(local, u32(length));
    return *this;
  }
  if (length < 0) {
    va_copy(formatting, arguments);
    length = crt->Measure(format, formatting);
    va_end(formatting);
  }
  if (length < 0) {
    va_end(arguments);
    ScFatal(SC_ERROR_INVALID, "String formatting failed");
  }
  char *buffer = new char[usize(length) + 1];
  i32 written = crt->Format(buffer, usize(length) + 1, format, arguments);
  va_end(arguments);
  if (written < 0 || written > length) {
    delete[] buffer;
    ScFatal(SC_ERROR_INVALID, "String formatting failed");
  }
  ScString::FromChar(buffer, u32(written));
  delete[] buffer;
  return *this;
}

// Start an empty string with the original eight-byte allocation and zero references. Spare capacity is left unwritten.
ScString::ScString() : references(0) {
  begin = new char[8];
  end = begin;
  storage_end = begin + 8;
  *end = 0;
}

// Copy a null-terminated source into independent exact-size storage with no external references.
// The installed constructor reads through strlen before allocating and does not accept a null input.
ScString::ScString(const char *data) : references(0) {
  usize length = strlen(data);
  if (length >= UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  begin = new char[length + 1];
  if (length)
    memcpy(begin, data, length);
  end = begin + length;
  storage_end = end + 1;
  *end = 0;
}

// Copy a byte range into exactly length+1 owned bytes.
ScString::ScString(const char *data, u32 length) : references(0) {
  if (length == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  begin = new char[(usize)length + 1];
  if (length)
    memcpy(begin, data, length);
  end = begin + length;
  storage_end = end + 1;
  *end = 0;
}

ScString::ScString(const ScString &source) : ScString(source.begin, source.ScString::Strlen()) {
}

// Copy an arbitrary original String interface after reading Strlen and then Data in the installed call order.
// The owned bytes are separate from the input implementation's allocation.
ScString::ScString(const ScStringView &source) : references(0) {
  u32 length = source.Strlen();
  const char *data = source.Data();
  if (length == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  begin = new char[(usize)length + 1];
  if (length)
    memcpy(begin, data, length);
  end = begin + length;
  storage_end = end + 1;
  *end = 0;
}

ScString::~ScString() {
  delete[] begin;
}

// Assign the concrete backing range with the source operator's self check and receiver-reference return.
// Concrete assignment does not dispatch interface getters or an overridden receiver FromChar method.
ScString &ScString::operator=(const ScString &source) {
  if (this != &source)
    ScString::FromChar(source.begin, source.ScString::Strlen());
  return *this;
}

// Publish the concrete supported interface before retaining it. Unsupported identifiers clear the output.
bool ScString::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScStringView)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = this;
  AddRef();
  return 1;
}

u32 ScString::AddRef() {
  return ++references;
}

// A zero or final reference deletes the object. Larger counts are decremented and returned directly.
u32 ScString::Release() {
  if (references <= 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Assign bytes with the original copy-then-append/erase order. The original interface has no return value.
void ScString::FromChar(const char *data, u32 length) {
  u32 size = ScString::Strlen(), copied = length < size ? length : size;
  if (copied)
    memcpy(begin, data, copied);
  if (length > size)
    Append(data + copied, length - copied);
  else
    Erase(length, size - length);
}

// Copy through a temporary for alias safety, then retire that temporary before the void operation returns.
void ScString::FromChar(const char *data) {
  usize length = strlen(data);
  if (length >= UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  ScString temporary(data, (u32)length);
  *this = temporary;
}

// Append a byte range using the original capacity growth and ordered in-place writes.
// The first byte is consumed after the tail move and terminator write, preserving defined own-buffer alias cases.
void ScString::Append(const void *data, u32 length) {
  if (!length)
    return;
  u32 size = ScString::Strlen();
  if (length > UINT32_MAX - 1 - size)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  const char *source = (const char *)data;
  if (size + length < Capacity()) {
    if (length > 1)
      memmove(end + 1, source + 1, length - 1);
    end[length] = 0;
    *end = source[0];
    end += length;
    return;
  }
  u64 capacity = (u64)size + (size > length ? size : length) + 1;
  if (capacity > UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String capacity exceeds its storage range");
  char *copy = new char[(usize)capacity];
  if (size)
    memcpy(copy, begin, size);
  memmove(copy + size, source, length);
  copy[size + length] = 0;
  delete[] begin;
  begin = copy;
  end = copy + size + length;
  storage_end = copy + capacity;
}

// Append through the concrete storage convenience while retaining its native concrete receiver reference.
ScString &ScString::Append(const char *data) {
  ScString::VStrcat(data);
  return *this;
}

// Append the null-terminated source and return the original String interface of this same receiver.
ScStringView &ScString::VStrcat(const char *data) {
  usize length = strlen(data);
  if (length >= UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  Append(data, (u32)length);
  return *this;
}

ScString ScString::SplitToken(const char *delimiter) {
  u32 size = ScString::Strlen();
  if (!delimiter || !*delimiter)
    return ScString(begin, size);
  usize length = strlen(delimiter);
  if (length >= UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "String token exceeds its storage range");
  u32 tokenLength = (u32)length, position = UINT32_MAX;
  if (tokenLength <= size)
    for (u32 index = 0; index <= size - tokenLength; ++index)
      if (!memcmp(begin + index, delimiter, tokenLength)) {
        position = index;
        break;
      }
  if (position == UINT32_MAX) {
    ScString empty;
    return ScString(empty);
  }
  ScString prefix(static_cast<const ScString &>(Left(position)));
  {
    ScString suffix = Right(ScString::Strlen() - position - tokenLength);
    *this = suffix;
  }
  return ScString(prefix);
}

// Copy a concrete prefix through the original middle-range operation, retaining its separate temporary lifetime.
// Counts larger than the source length copy the entire concrete backing range without virtual getter calls.
ScString ScString::Left(u32 count) const {
  u32 size = ScString::Strlen();
  if (count <= size) {
    ScString prefix = Mid(0, count);
    return ScString(prefix);
  }
  return ScString(begin, size);
}

// Copy a concrete suffix through Mid while retaining the intermediate String and final independent return storage.
// Counts beyond the source length copy the full backing range without virtual getters.
ScString ScString::Right(u32 count) const {
  u32 size = ScString::Strlen();
  if (count <= size) {
    ScString suffix = Mid(size - count, count);
    return ScString(suffix);
  }
  return ScString(begin, size);
}

// Copy a clamped concrete range through a separate String temporary; starts at or beyond the end return empty text.
// The empty branch constructs the original default String before copying its range into the returned value.
ScString ScString::Mid(u32 start, u32 count) const {
  u32 size = ScString::Strlen();
  if (start < size) {
    if (count > size - start)
      count = size - start;
    ScString range(begin + start, count);
    return ScString(range);
  }
  ScString empty;
  return ScString(empty);
}

// Compare the requested byte range with the beginning of the stored string.
bool ScString::BeginsWith(const char *text, u32 length) const {
  if (length > ScString::Strlen())
    return false;
  for (u32 i = 0; i < length; ++i)
    if (begin[i] != text[i])
      return false;
  return true;
}

// Compare the requested byte range backwards with the end of the stored string.
bool ScString::EndsWith(const char *text, u32 length) const {
  if (length > ScString::Strlen())
    return false;
  for (u32 i = 0; i < length; ++i)
    if (end[-1 - static_cast<isize>(i)] != text[length - 1 - i])
      return false;
  return true;
}

// Convert the C-string prefix while allowing Unicode mappings to change its byte length.
static void ScStringCase(ScString *text, bool upper) {
  ScString converted;
  u32 length = text->ScString::Strlen(), offset = 0;
  while (offset < length && text->begin[offset]) {
    u32 scalar, count = ScUtf8Decode(text->begin + offset, length - offset, &scalar);
    if (count) {
      char bytes[4];
      u32 written = ScUtf8Encode(upper ? ScUnicodeUpper(scalar) : ScUnicodeLower(scalar), bytes);
      converted.Append(bytes, written);
    } else {
      count = 1;
      converted.Append(text->begin + offset, count);
    }
    offset += count;
  }
  converted.Append(text->begin + offset, length - offset);
  text->ScString::FromChar(converted.begin, converted.ScString::Strlen());
}

void ScString::MakeLower() {
  ScStringCase(this, false);
}

void ScString::MakeUpper() {
  ScStringCase(this, true);
}

// Remove only leading literal spaces from concrete backing storage, retaining allocation and interface identity.
void ScString::LTrim() {
  u32 size = ScString::Strlen(), count = 0;
  while (count < size && begin[count] == ' ')
    ++count;
  if (count)
    Erase(0, count);
}

// Remove only trailing literal spaces without virtual getter calls.
void ScString::RTrim() {
  u32 size = ScString::Strlen(), finish = size;
  while (finish && begin[finish - 1] == ' ')
    --finish;
  if (finish < size)
    Erase(finish, size - finish);
}

void ScString::Trim() {
  LTrim();
  RTrim();
}

// Clamp the requested count to the remaining suffix, including the original npos whole-tail case.
void ScString::Erase(u32 start, u32 count) {
  u32 size = ScString::Strlen();
  if (start > size)
    ScFatal(SC_ERROR_INVALID, "String erasure starts outside its text");
  if (count > size - start)
    count = size - start;
  if (count) {
    memmove(begin + start, begin + start + count, size - start - count + 1);
    end -= count;
  }
}

// Replace a bounded byte span using native storage plumbing for the original standard-string operation.
// Keep spare capacity when possible, clamp erasure to the suffix, and snapshot an aliased source before moving bytes.
void ScString::Replace(u32 start, u32 count, const char *data, u32 length) {
  u32 size = ScString::Strlen();
  if (start > size)
    ScFatal(SC_ERROR_INVALID, "String replacement starts outside its text");
  if (count > size - start)
    count = size - start;
  if (length > UINT32_MAX - 1 - (size - count))
    ScFatal(SC_ERROR_INVALID, "String length exceeds its storage range");
  u32 total = size - count + length;
  char *snapshot = nullptr;
  if (length && (uptr)data >= (uptr)begin && (uptr)data < (uptr)storage_end) {
    snapshot = new char[length];
    memcpy(snapshot, data, length);
    data = snapshot;
  }
  if (total >= Capacity()) {
    u32 extra = length - count;
    u64 capacity = (u64)size + (size > extra ? size : extra) + 1;
    if (capacity > UINT32_MAX)
      ScFatal(SC_ERROR_INVALID, "String capacity exceeds its storage range");
    char *copy = new char[(usize)capacity];
    if (start)
      memcpy(copy, begin, start);
    if (length)
      memcpy(copy + start, data, length);
    memcpy(copy + start + length, begin + start + count, size - start - count + 1);
    delete[] begin;
    begin = copy;
    storage_end = copy + capacity;
  } else {
    memmove(begin + start + length, begin + start + count, size - start - count + 1);
    if (length)
      memcpy(begin + start, data, length);
  }
  end = begin + total;
  delete[] snapshot;
}

// Return the source interface's ToChar pointer without allocation or termination work. It is distinct from Data dispatch.
const char *ScString::ToChar() const {
  return begin;
}

// Borrow the original beginning of the text allocation without copying it.
const char *ScString::Data() const {
  return begin;
}

u32 ScString::Strlen() const {
  return (u32)(end - begin);
}

u32 ScString::Capacity() const {
  return (u32)(storage_end - begin);
}

bool ScString::IsEqual(const ScStringView *source) const {
  return Strlen() == source->Strlen() && !memcmp(begin, source->Data(), Strlen());
}

// Leave self-assignment unchanged; otherwise read the source length before its ToChar pointer.
// Dispatch the destination's virtual byte assignment without making an intermediate owned copy.
void ScString::Copy(const ScStringView &source) {
  if (&source == this)
    return;
  u32 length = source.Strlen();
  const char *data = source.ToChar();
  FromChar(data, length);
}

// Select radix16 for an immediate0x prefix or any hexadecimal letter; otherwise parse unsigned decimal.
// Preserve the bundled32-bit CRT overflow result and post-overflow negation on hosts with wider unsigned long.
u32 ScConvertToUint32(const char *text) {
  u32 radix = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    radix = 16;
  else
    for (const char *p = text; *p; ++p)
      if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        radix = 16;
        break;
      }
  while (isspace((u8)*text))
    ++text;
  bool negative = *text == '-';
  if (*text == '-' || *text == '+')
    ++text;
  if (*text == '-' || *text == '+' || isspace((u8)*text))
    return 0;
  u64 value = strtoull(text, nullptr, (int)radix);
  if (value > UINT32_MAX) {
    value = UINT32_MAX;
    errno = ERANGE;
  }
  return negative ? 0u - (u32)value : (u32)value;
}
