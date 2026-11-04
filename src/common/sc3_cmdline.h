#ifndef SC3_COMMON_SC3_CMDLINE_H
#define SC3_COMMON_SC3_CMDLINE_H

#include <vector>

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// Interface declarations adapted from sc3k-gzcom-dll cIGZCmdLine.h (Copyright (C) 2024 Nicholas Hayes).
// SPDX-License-Identifier: LGPL-2.1-or-later
class ScStringView;

// Retail command-line order begins with argc12/argv16 and the two searches20/24 before switch queries28/32.
// Installed graphics calls string28/char32; MSVC virtual overload grouping must be considered before claiming a PC difference.
class ScCmdLine : public ScUnknown {
public:
  virtual i32 argc() const = 0;
  virtual const char *const argv(i32 index) const = 0;
  virtual i32 Find(ScStringView const &value, bool caseSensitive) const = 0;
  virtual i32 FindSubString(ScStringView const &value, bool caseSensitive) const = 0;
  virtual bool IsSwitchPresent(char argument, ScStringView *value, bool option) const = 0;
  virtual bool IsSwitchPresent(const ScStringView &argument, ScStringView *value, bool option) const = 0;
  virtual i32 GetIndexOfSwitch(char argument, i32 startIndex) const = 0;
  virtual i32 GetIndexOfSwitch(ScStringView const &argument, i32 startIndex) const = 0;
  virtual bool InsertArgument(ScStringView const &arg, i32 index) = 0;
  virtual bool EraseArgument(i32 index) = 0;
  virtual bool GetCommandLineText(ScStringView *text) const = 0;
};

// Actual command-line owner uses a vector of concrete Strings, plus raw text and a stable out-of-range fallback.
// The original default constructor leaves references unwritten; owning constructors establish zero.
class ScCommandLine : public ScCmdLine {
public:
  ScCommandLine();
  explicit ScCommandLine(const char *line);
  ScCommandLine(const ScCommandLine &source);
  explicit ScCommandLine(const ScCmdLine &source);
  virtual ~ScCommandLine();
  ScCommandLine &operator=(const ScCommandLine &source);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  i32 argc() const override;
  const char *const argv(i32 index) const override;
  i32 Find(const ScStringView &value, bool case_sensitive) const override;
  i32 FindSubString(const ScStringView &value, bool case_sensitive) const override;
  bool IsSwitchPresent(char argument, ScStringView *value, bool option) const override;
  bool IsSwitchPresent(const ScStringView &argument, ScStringView *value, bool option) const override;
  i32 GetIndexOfSwitch(char argument, i32 start) const override;
  i32 GetIndexOfSwitch(const ScStringView &argument, i32 start) const override;
  bool InsertArgument(const ScStringView &argument, i32 index) override;
  bool EraseArgument(i32 index) override;
  bool GetCommandLineText(ScStringView *text) const override;
  virtual ScString *GetArgument(u32 index);
  u32 references;
  std::vector<ScString> arguments;
  ScString raw, fallback;

private:
  void GrowArguments();
  void RebuildText();
  void AppendToken(const char *first, u32 length);
};

#endif
