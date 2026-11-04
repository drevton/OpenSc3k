#ifndef SC3_BUILDINGS_ARCHITECT_MANAGER_H
#define SC3_BUILDINGS_ARCHITECT_MANAGER_H

#include <SDL3/SDL_atomic.h>

#include "buildings/architect/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/sc3_string.h"

class BdCatalog;
class BdIxf;
class BdEditor;
class BdExporter;
class BdGrid;
class BdImageLoader;
class BdWorkspace;
class BdDocument;
class GxBuffer;

// Canonical reference base, independent of service properties, with an actual final-release virtual.
class BdAtomicObject : public ScUnknown {
public:
  BdAtomicObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 FinalRelease();
  virtual ~BdAtomicObject();
  SDL_AtomicInt references;
};

// Original service-property base; the manager separately inherits its canonical atomic base.
class BdServiceBase : public ScSystemService {
public:
  BdServiceBase(u32 identifier, i32 priority);
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;
  virtual ~BdServiceBase();
  u32 identifier;
  i32 priority;
  bool running;
};

// Architect manager primary interface, ordered by the installed table and the SIMINIT export caller.
class BdManagerView : public ScUnknown {
public:
  virtual u8 LoadPreviewDocument(ScStringView *path, GxBuffer **image, BdDocument **document) = 0;
  virtual u8 ExportBuildings(ScStringView *path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) = 0;
  virtual u8 LoadPreview(const char *path, GxBuffer **image, BdDocument *document) = 0;
  virtual u8 ExportBuildingsAlternate(
      ScStringView *path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) = 0;
  virtual BdWorkspace *GetWorkspace() = 0;
  virtual BdGrid *GetGrid() = 0;
  virtual BdEditor *GetEditor() = 0;
  virtual i32 EnsureWorkspace() = 0;
  virtual BdCatalog *GetCatalog() = 0;
  virtual i32 SetGrid(BdGrid *grid, u8 clear_path) = 0;
  virtual u8 LoadGrid(const char *path, BdGrid **out) = 0;
  virtual u8 SaveGrid(BdGrid *value, const char *path) = 0;
  virtual u8 SavePreview(u32 unused_first, u32 unused_second, const char *path) = 0;
  virtual u8 LoadDocument(const char *path, BdDocument *document) = 0;
  virtual bool GetPath(ScString *out) = 0;
  virtual u8 SetOperation18(const ScString *value) = 0;
  virtual u8 SetOperation19(const ScString *value) = 0;
};

class BdManager : public BdManagerView, public BdServiceBase, public BdAtomicObject {
public:
  BdManager();
  ~BdManager() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 FinalRelease() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  BdCatalog *GetCatalog() override;
  BdWorkspace *GetWorkspace() override;
  BdGrid *GetGrid() override;
  BdEditor *GetEditor() override;
  i32 SetGrid(BdGrid *grid, u8 clear_path) override;
  bool GetPath(ScString *out) override;
  // Original primary slot 13, loading a grid and its archive metadata.
  u8 LoadGrid(const char *path, BdGrid **out) override;
  // Original primary slot 14, writing a grid and its archive metadata.
  u8 SaveGrid(BdGrid *value, const char *path) override;
  // Original slot 15 ignores its first two DWORD arguments; their source types remain unresolved.
  u8 SavePreview(u32 unused_first, u32 unused_second, const char *path) override;
  u8 LoadPreviewDocument(ScStringView *path, GxBuffer **image, BdDocument **document) override;
  u8 ExportBuildings(ScStringView *path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) override;
  u8 ExportBuildingsAlternate(
      ScStringView *path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) override;
  // Original primary slot 5, loading an archive-backed thumbnail and document.
  u8 LoadPreview(const char *path, GxBuffer **image, BdDocument *document) override;
  u8 LoadDocument(const char *path, BdDocument *document) override;
  // Original primary slots 18 and 19 share an initialization-only body; their private names remain unknown.
  u8 SetOperation18(const ScString *value) override;
  u8 SetOperation19(const ScString *value) override;
  // Original primary slot 10; preserve the observed zero word while its private return spelling is unresolved.
  i32 EnsureWorkspace() override;
  bool CreateGridGuid(BdGrid *value, u32 *out);
  u8 ExportGrid(BdIxf *archive, ScStringView *filename);
  u8 ExportBuildingsInternal(
      ScStringView *path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys, u8 alternate);
  // Installed callers discard the progress commands' path-dependent incidental results; word signedness is unresolved.
  void BeginExportProgress(u32 maximum, const ScStringView *text);
  void UpdateExportProgress(u32 value);
  void EndExportProgress();
  // Installed callers discard the path-dependent incidental return.
  void UpdateThumbnail();
  // Private source bool/byte spelling is unresolved; preserve its observed one-byte result.
  u8 Initialize();
  void BuildDecorations();
  // Native effect spelling; the private clear return is unresolved.
  void ClearCatalog();

  BdCatalog *catalog;
  u32 field_020, field_024;
  BdGrid *grid;
  BdWorkspace *workspace;
  BdImageLoader *image_loader;
  ScString path;
  u32 field_048;
  bool initialized, flag_04d;
  // The export path assigns this temporary pointer before use; the original constructor leaves it untouched.
  BdExporter *exporter;
  ScRandom random;
  u8 field_070;
};

BdManager *BdGetManager();
BdGrid *BdGetCurrentGrid();
void BdLoadConfig();

class BdEditor;
class BdGrid;
class BdCatalog;
class GxBuffer;

// The caller's four retained export images and their corresponding rectangles.
class BdExportView {
public:
  BdExportView();
  ~BdExportView();
  GxBuffer *images[5];
  BdRect bounds[5];
};

// The writer's borrowed four-orientation, four-scale images and their corresponding rectangles.
class BdExportFrames {
public:
  BdExportFrames();
  GxBuffer *images[16];
  BdRect bounds[16];
};

// Original export receiver retains the active grid, editor and temporary render buffer.
class BdExporter {
public:
  BdExporter();
  u8 Init();
  BdCatalog *SetCatalog(BdCatalog *value);
  // Installed callers discard the original path-dependent incidental return.
  void SetEditor(BdEditor *value);
  bool SetGrid(BdGrid *value);
  bool PrepareBuffer(i32 mode);
  bool RenderView(i32 orientation, BdExportView *out);
  bool Shutdown();

  BdCatalog *catalog;
  BdGrid *grid;
  BdEditor *editor;
  i32 current_mode;
  GxBuffer *buffer;
  u32 field_014;
  ScRandom random;
};

#endif
