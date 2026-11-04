#ifndef SC3_COMMON_SC3_STRING_H
#define SC3_COMMON_SC3_STRING_H

#include "common/sc3.h"
#include "common/sc3_error.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// String range operations clamp this whole-tail count to the characters remaining after the starting index.
inline constexpr u32 kScStringToEnd = UINT32_MAX;

// A failed character/token lookup has no index in the string's owned byte range.
inline constexpr u32 kScStringNotFound = UINT32_MAX;

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original cIGZString operations preserve the installed virtual order through the null-terminated append at slot40.
// Native storage and inheritance do not overlay a foreign object layout.
class ScStringView : public ScUnknown {
public:
  virtual void FromChar(const char *data) = 0;
  virtual void FromChar(const char *data, u32 length) = 0;
  virtual const char *ToChar() const = 0;
  virtual const char *Data() const = 0;
  virtual u32 Strlen() const = 0;
  virtual bool IsEqual(const ScStringView *source) const = 0;
  virtual void Copy(const ScStringView &source) = 0;
  virtual ScStringView &VStrcat(const char *data) = 0;
};

// The concrete string owns the original begin/end/storage-end pointers and reference count.
class ScString : public ScStringView {
public:
  ScString();
  ScString(const char *data);
  ScString(const ScString &source);
  ScString(const ScStringView &source);
  ScString(const char *data, u32 length);
  ~ScString();
  ScString &operator=(const ScString &source);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void FromChar(const char *data) override;
  void FromChar(const char *data, u32 length) override;
  void Append(const void *data, u32 length);
  ScString &Append(const char *data);
  void Erase(u32 start, u32 count);
  // Native replacement of the original standard-string storage operation, not a recovered interface method.
  void Replace(u32 start, u32 count, const char *data, u32 length);
  const char *ToChar() const override;
  const char *Data() const override;
  u32 Strlen() const override;
  u32 Capacity() const;
  bool IsEqual(const ScStringView *source) const override;
  void Copy(const ScStringView &source) override;
  ScStringView &VStrcat(const char *data) override;
  ScString SplitToken(const char *delimiter);
  ScString Left(u32 count) const;
  ScString Right(u32 count) const;
  ScString Mid(u32 start, u32 count) const;
  bool BeginsWith(const char *text, u32 length) const;
  bool EndsWith(const char *text, u32 length) const;
  void MakeLower();
  void MakeUpper();
  void LTrim();
  void RTrim();
  void Trim();
  ScString &SC3_CDECL Sprintf(const char *format, ...);
  char *begin, *end, *storage_end;
  u32 references;
};

u32 ScConvertToUint32(const char *text);
int ScCompareNoCase(const char *first, const char *second);
int ScCompareNoCase(const char *first, const char *second, usize length);
void ScLowerCase(char *text);
void ScUpperCase(char *text);
// Assemble one original byte callback into a complete UTF-8 scalar in a pending buffer, excluding invalid sequences.
// The caller consumes/clears completed input; the scalar output may be null and is untouched until completion.
bool ScAppendUtf8Byte(ScString *pending, u8 byte, u32 *scalar);

#endif
