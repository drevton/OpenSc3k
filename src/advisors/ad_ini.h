#ifndef SC3_ADVISORS_AD_INI_H
#define SC3_ADVISORS_AD_INI_H

#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "resources/file.h"

u32 AdParseIniNumber(char *text);

// Retail cRZIniFile passes two const String references and the caller's opaque callback context.
using AdIniCallback = void (*)(const ScString &key, const ScString &value, void *context);

// One owned section name and its original physical file offset, retained in unsigned byte-name order.
struct AdIniSection {
  ScString name;
  u32 offset;
};

// Private INI reader embedded in SIMADV. The original map, file member and two flags are recovered;
// section indexing and key lookup preserve the original persistent file/cache behavior.
class AdIniFile {
public:
  AdIniFile();
  virtual ~AdIniFile();
  virtual const ScString *GetPath() const;
  virtual void SetPath(const ScString &input);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool ReadEntry(const ScString &section, const ScString &key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual bool EnumEntries(const ScString &section, AdIniCallback callback, void *context);
  virtual bool EnumSections(AdIniCallback callback, void *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool leave_open);

protected:
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);

public:
  // Native section-cache operations preserve the original first-entry rule without generic tree machinery.
  void ClearSections();
  void AddSection(const ScString &name, u32 offset);
  AdIniSection *FindSection(const ScString &name);
  ScString filename;
  u8 keep_open, loaded;
  ScArray<AdIniSection *> sections;
  RsFile file;
};

// Packaged INI reader extends the loose-file cache with the actual SYS.PAK path, stream and lazy selection state.
class AdIni : public AdIniFile {
public:
  AdIni();
  ~AdIni() override;
  bool WriteEntry(const ScString &section, const ScString &key, const ScString &value) override;
  bool ReadEntry(const ScString &section, const ScString &key, ScString *out) override;
  bool WriteNumber(const ScString &section, const ScString &key, i32 value) override;
  bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size) override;
  bool ReadNumber(const ScString &section, const ScString &key, i32 *out) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size) override;
  u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) override;
  bool EnumEntries(const ScString &section, AdIniCallback callback, void *context) override;
  bool SectionExists(const ScStringView &section) override;
  virtual void SetCompressedPath(const ScString &input);
  virtual const ScString *GetCompressedPath() const;
  virtual bool EnumEntriesOld(const ScString &section, AdIniCallback callback, void *context);

protected:
  void InitFileType();
  void ReadString(ScString *out);

public:
  ScString package_name;
  RsFile package_file;
  u32 mode;
  char *buffer;
  u32 buffer_capacity, package_offset;
};

#endif
