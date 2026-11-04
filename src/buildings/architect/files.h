#ifndef SC3_BUILDINGS_ARCHITECT_FILES_H
#define SC3_BUILDINGS_ARCHITECT_FILES_H

#include "buildings/architect/bd_geometry.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "ui/generic_dialogs.h"

// Architect building-file modes reuse the stock picker transport, with distinct building operations.
enum BdFileDialogMode : u32 {
  kBdFileOpenBuilding = 0x83172ad7,
  kBdFileOpenBuildingSet = 0x83172ad8,
  kBdFileSaveBuildingSet = 0xc3172aeb,
  kBdFileSelectSetForRendering = 0xc414cc57,
  kBdFileRenderSetDirectory = 0xc414cc58,
  kBdFileSaveBuilding = 0xc3172aea,
};

class BdDirectorySearch;

// Probe the installed directory wildcard; the private source bool/byte spelling remains unresolved.
u8 BdDirectoryExists(const ScString *path);

// The original directory result records retain their search owner, name and file/directory kind.
struct BdDirectoryEntry {
  BdDirectorySearch *owner;
  ScString name;
  u32 kind;
};

struct BdDirectoryNode {
  BdDirectoryNode *next, *previous;
  BdDirectoryEntry entry;
};

// The search owns a circular list; its sentinel is never exposed as a result.
struct BdDirectoryEntries {
  BdDirectoryEntries();
  ~BdDirectoryEntries();
  void Clear();
  void Insert(BdDirectoryNode *before, BdDirectorySearch *owner, const char *name, u32 kind);
  bool Contains(const char *name) const;
  BdDirectoryNode *head;
};

// The original name-list output owns each selected directory-entry string.
struct BdDirectoryNames {
  ~BdDirectoryNames();
  void Clear();
  void Append(const char *name);
  void Prepend(const char *name);
  void Sort();
  ScArray<ScString *> names;
};

// Enumerate matching drive names and kinds into two independent outputs.
// The private source bool/byte return spelling remains unresolved.
u8 BdGetLogicalDrives(BdDirectoryNames *names, ScArray<u32> *kinds);

// Directory path, wildcard, results and the two original special-directory inclusion flags.
class BdDirectorySearch {
public:
  BdDirectorySearch(const ScString *path, u8 scan, u8 include_parent, u8 include_current);
  ~BdDirectorySearch();
  u8 Scan();
  // Installed callers discard the private setter's path-dependent return.
  void SetPattern(const ScString *value, u8 scan);
  u8 GetNames(BdDirectoryNames *out, u8 flags);

  BdDirectoryEntries entries;
  ScString directory, pattern;
  u8 include_parent, include_current;
};

class BdDirectorySearch;
class GxBuffer;

// These effect-only declarations retain the original mutable String argument; private incidental returns are unresolved.
void BdTrimTrailingSpaces(ScString *text);
void BdTruncateEncodedText(ScString *text, i32 width, FtFont *font);
u8 BdFitDialogText(ScString *text, i32 width, FtFont *font);

u8 BD_STDCALL BdCreateDialogFrame(WnWindow *parent, i32 x, i32 y, i32 width, i32 height);
// Preserve the observed flag-set result; the private nominal return declaration is unresolved.
u32 BD_STDCALL BdSetDialogChildEnabled(WnWindow *parent, u32 identifier, u8 enabled);
u8 BD_STDCALL BdCreateDialogButton(WnWindow *parent, u32 identifier, i32 x, i32 y, u32 image_instance, i8 key);
u8 BD_STDCALL BdCreateDialogLabel(
    WnWindow *parent, u32 identifier, i32 x, i32 y, const ScStringView *caption, u32 alignment);
u8 BD_STDCALL BdCreateDialogInput(
    WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height, ScStringView *text, u8 editable);
u8 BD_STDCALL BdCreateDialogEditor(
    WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height, ScStringView *text, u8 editable);
u8 BD_STDCALL BdCreateStyledLabel(WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height,
    const ScStringView *caption, u32 alignment, u32 font, u8 theme_color);

// Each original 40-byte file-type entry owns two strings; their private property names are unresolved.
struct BdFileFilter {
  ScString first, second;
};

// The file list retains its entries in the original circular order.
struct BdFileEntry {
  BdFileEntry *next, *previous;
  ScString value;
};

// The architect file dialog owns its strings, filters, available types and file-list entries.
// Its content construction and individual file actions are separate, still-unrecovered methods.
class BdFileDialog : public UiWindowCallback {
public:
  BdFileDialog();
  virtual ~BdFileDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool BuildWindow(WnWindow **out);
  u8 BuildContent(WnWindow *window, u32 mode);
  // The private source bool/byte spelling remains unresolved; preserve the observed byte result.
  u8 ResolvePath(WnWindow *window, FtFont *font);
  u8 Close(WnWindow *window);
  u8 GetLimits(WnWindow *window, ScRect *limits);
  u8 Commit(WnWindow *window, u32 identifier);
  u8 Finish(WnWindow *window, u32 identifier);
  u8 SelectEntry(WnWindow *window);
  bool EditEntry(WnWindow *window, u32 identifier);
  u8 ChangeEntry(WnWindow *window, u32 identifier);
  u8 Populate(WnWindow *window);
  u8 ActivateEntry(WnWindow *window, u32 identifier);
  u8 ImportEntry(WnWindow *window, u8 open);
  bool FindExtension(i32 kind, ScString *out);
  i32 FindKind(ScString *extension);
  // The private source bool/byte spelling remains unresolved; preserve the observed byte result.
  u8 RefreshFileList(WnWindow *window);
  // Installed callers discard the private preview update's incidental result.
  void UpdatePreview(WnWindow *window);
  void ReadPreview(const ScString *path, ScString *text_first, ScString *text_second, ScString *text_third, u32 *first,
      u32 *second, GxBuffer **image, u32 *size);

  u32 references, mode;
  ScString field_00c, field_020, field_034, field_048;
  BdDirectorySearch *search;
  ScString field_060, field_074, field_088, field_09c, field_0b0;
  u32 field_0c4;
  ScArray<u32> available_types;
  u8 field_0d4, field_0d5, field_0d6, field_0d7;
  BdFileFilter *filters;
  u32 filter_count, filter_capacity;
  BdFileEntry *entries, *entry_cursor;
};

// Original process-wide last-used directories, updated by the dialog's completion action.
extern ScString BdLastBuildingDirectory, BdLastSetDirectory, BdLastSceneDirectory;

struct BdIniSection {
  ScString name;
  u32 position;
};

struct BdIniEnumContext;
using BdIniEnumCallback = void (*)(const ScString &name, const ScString &value, BdIniEnumContext *context);

// Native storage replacing only the private section tree, with destruction ordered after the embedded file.
struct BdIniIndex {
  ScArray<BdIniSection *> entries;
  ~BdIniIndex();
  void Clear();
};

// Recovered INI operations in original virtual order. Native section-index storage is not a foreign object overlay.
class BdIniFile {
public:
  BdIniFile();
  virtual ~BdIniFile();
  virtual const ScString *GetPath() const;
  virtual void SetPath(const ScString &path);
  virtual bool Close();
  virtual bool WriteEntry(const ScString &section, const ScString &key, const ScString &value);
  virtual bool ReadEntry(const ScString &section, const ScString &key, ScString *out);
  virtual bool WriteNumber(const ScString &section, const ScString &key, i32 value);
  virtual bool WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 size);
  virtual bool ReadNumber(const ScString &section, const ScString &key, i32 *out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out);
  virtual u32 ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity);
  virtual bool EnumEntries(const ScString &section, BdIniEnumCallback callback, BdIniEnumContext *context);
  virtual bool EnumSections(BdIniEnumCallback callback, BdIniEnumContext *context);
  virtual bool SectionExists(const ScStringView &section);
  virtual bool SetLeaveFileOpenBetweenOperations(bool value);
  virtual bool LoadSectionNames();
  virtual bool GetFileLine(ScString *out);
  virtual bool Open(bool write);

  ScString name;
  u8 keep_open, indexed;
  BdIniIndex sections;
  RsFile file;
};

// Native effect spelling for the original five-argument splitter; its private register return is unresolved.
void BdSplitPath(const char *path, char *drive, char *directory, char *name, char *extension);

// The original path extends String and lazily keeps its full spelling and four components in sync.
class BdPath : public ScString {
public:
  explicit BdPath(const ScString *value);
  ~BdPath();
  ScString *GetFull();
  ScString *GetDrive();
  ScString *GetDirectory();
  ScString *GetName();
  ScString *GetExtension();
  // Preserve the observed byte; the private source bool/byte spelling remains unresolved.
  u8 Exists();
  // Installed callers discard these methods' private nominal return values.
  void MakeAbsolute();
  void AssignAbsolute(const ScString *value);
  // The private parser and builder's nominal return declarations remain unresolved.
  void Split();
  void Rebuild();

  u8 full_valid;
  ScString drive, directory, name, extension;
  u8 parts_valid;
};

// The architect module's original localized String handle, with an external ABI output slot and a real key.
class BdTextResource {
public:
  BdTextResource(u32 instance, u32 group);
  virtual ~BdTextResource();
  ScString GetRawText();
  void Resolve();

  // The resource manager writes this field directly through its genuine void** SDK output.
  void *text;
  RsKey key;
};

#endif
