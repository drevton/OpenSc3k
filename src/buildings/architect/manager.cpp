#define WIN32_LEAN_AND_MEAN
#include "buildings/architect/manager.h"

#include <string.h>

#include <vector>

#include "buildings/architect/api.h"
#include "buildings/architect/bd_archive.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/bd_director.h"
#include "buildings/architect/bd_editor.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/image.h"
#include "common/framework.h"

#include "common/ids.h"

#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/file.h"
#include "ui/commands.h"

// GUID creation publishes the original all-ones output sentinel before invoking the COM service.
enum BdGridGuidInitialization : u32 {
  kBdGridGuidInitialOutput = 0xffffffff,
};

// Form the original literal path concatenation without inserting a separator or normalizing the filesystem root.
static ScString BdDecorationPath(const ScString &base, const char *suffix) {
  ScString path(base);
  path.Append(suffix);
  return path;
}

// Configuration is the caller's next separate operation; this builder does not mark the manager initialized.
void BdManager::BuildDecorations() {
  if (catalog)
    return;
  catalog = new BdCatalog;
  BdManager::Init();
  const char *directory = BdGetFileSystem()->DataDirectory();
  ScString base(directory, static_cast<u32>(strlen(directory)));
  ScString architecture = BdDecorationPath(base, "Res\\BA\\Props\\Architecture\\");
  ScString decoration = BdDecorationPath(base, "Res\\BA\\Props\\Building Decoration\\");
  ScString street = BdDecorationPath(base, "Res\\BA\\Props\\Plaza and Street\\");
  ScString yard = BdDecorationPath(base, "Res\\BA\\Props\\Yard Objects\\");
  ScString flora = BdDecorationPath(base, "Res\\BA\\Props\\Flora\\");
  ScString industrial = BdDecorationPath(base, "Res\\BA\\Props\\Industrial\\");
  ScString rooftops = BdDecorationPath(base, "Res\\BA\\Props\\Rooftops\\");
  ScString vehicles = BdDecorationPath(base, "Res\\BA\\Props\\Vehicles\\");
  ScString ground = BdDecorationPath(base, "Res\\BA\\Paints\\Ground\\");
  ScString roofs = BdDecorationPath(base, "Res\\BA\\Paints\\Roofs\\");
  ScString walls = BdDecorationPath(base, "Res\\BA\\Paints\\Walls\\");
  ScString colors = BdDecorationPath(base, "Res\\BA\\Paints\\Colors\\");
  ScString contemporary = BdDecorationPath(base, "Res\\BA\\Details\\Contemporary\\");
  ScString traditional = BdDecorationPath(base, "Res\\BA\\Details\\Traditional\\");
  ScString custom_paints = BdDecorationPath(base, "BACustom\\Paints\\");
  ScString custom_details = BdDecorationPath(base, "BACustom\\Details\\");
  ScString custom_props = BdDecorationPath(base, "BACustom\\Props\\");
  catalog->EnumerateFamily(0, &ground);
  catalog->EnumerateFamily(1, &roofs);
  catalog->EnumerateFamily(2, &walls);
  catalog->EnumerateFamily(3, &colors);
  BdCreateColors(catalog);
  catalog->EnumerateFamily(4, &contemporary);
  catalog->EnumerateFamily(5, &traditional);
  catalog->EnumerateFamily(8, &architecture);
  catalog->BuildDefinitionFiles(8);
  catalog->EnumerateFamily(9, &decoration);
  catalog->BuildDefinitionFiles(9);
  catalog->EnumerateFamily(10, &street);
  catalog->BuildDefinitionFiles(10);
  catalog->EnumerateFamily(11, &yard);
  catalog->BuildDefinitionFiles(11);
  catalog->EnumerateFamily(12, &flora);
  catalog->BuildDefinitionFiles(12);
  catalog->EnumerateFamily(13, &industrial);
  catalog->BuildDefinitionFiles(13);
  catalog->EnumerateFamily(14, &rooftops);
  catalog->BuildDefinitionFiles(14);
  catalog->EnumerateFamily(15, &vehicles);
  catalog->BuildDefinitionFiles(15);
  catalog->EnumerateFamily(6, &custom_paints);
  catalog->EnumerateFamily(7, &custom_details);
  catalog->EnumerateFamily(16, &custom_props);
  catalog->BuildDefinitionFiles(16);
}

BdExportView::BdExportView() {
  for (u32 i = 1; i <= 4; ++i)
    images[i] = nullptr;
}

BdExportView::~BdExportView() {
  for (u32 i = 1; i <= 4; ++i)
    if (images[i])
      images[i]->Release();
}

BdExportFrames::BdExportFrames() {
  for (u32 i = 0; i < 16; ++i)
    images[i] = nullptr;
}

// Construct the original random generator before clearing the export receiver's references and mode.
BdExporter::BdExporter() : random(kScRandomSeedFromClock) {
  current_mode = 0;
  buffer = nullptr;
  catalog = nullptr;
  editor = nullptr;
  grid = nullptr;
}

u8 BdExporter::Init() {
  return 1;
}

// Store the borrowed catalog and return the assigned pointer without changing its reference count.
BdCatalog *BdExporter::SetCatalog(BdCatalog *value) {
  if (catalog)
    catalog = nullptr;
  catalog = value;
  return value;
}

// Release the previous editor before retaining and storing the replacement editor.
void BdExporter::SetEditor(BdEditor *value) {
  ScRelease(&editor);
  if (value)
    value->AddRef();
  editor = value;
}

// Replace the temporary render buffer with the original dimensions for the requested grid view mode.
// Preserve the source's early reuse and its buffer-creation and initialization results.
bool BdExporter::PrepareBuffer(i32 mode) {
  if (current_mode == mode)
    return true;
  i32 width = 0, height = 0;
  switch (mode) {
  case 1:
    width = 128;
    height = 512;
    break;
  case 2:
    width = 256;
    height = 1024;
    break;
  case 3:
    width = 384;
    height = 1792;
    break;
  case 4:
    width = 512;
    height = 1792;
    break;
  case 5:
    width = 640;
    height = 1792;
    break;
  }
  ScRelease(&buffer);
  if (!BdGetGraphics()->CreateBuffer(&buffer))
    return false;
  return buffer->Init(static_cast<u32>(width), static_cast<u32>(height),
      GxColorType(static_cast<GxColorType::Color>(14)), nullptr, GxDepthType(GxDepthType::Depth::None),
      GxVramRequest::VideoMemoryLowPriority, false);
}

// Retain the replacement grid, prepare its view-mode buffer and attach both to the live editor.
// The editor attachment still runs when buffer preparation reports failure, as in the source.
bool BdExporter::SetGrid(BdGrid *value) {
  ScRelease(&grid);
  if (value)
    value->AddRef();
  grid = value;
  i32 mode = value->view_mode >= 1 && value->view_mode <= 5 ? value->view_mode : 0;
  bool result = PrepareBuffer(mode);
  editor->SetGridAndBuffer(value, buffer);
  return result;
}

// An unsupported display format leaves the source's temporary rectangles undefined; the native temporary starts clear.
bool BdExporter::RenderView(i32 orientation, BdExportView *out) {
  BdThumbnailViews rendered = {};
  GxBuffer *source = buffer;
  bool result = editor->RenderExportView(orientation, &source, &rendered);
  for (u32 i = 1; i <= 4; ++i) {
    out->images[i] = rendered.images[5 - i];
    out->bounds[i] = rendered.bounds[5 - i];
  }
  return result;
}

// Disable edge drawing and release the export buffer, grid and editor in the original order.
bool BdExporter::Shutdown() {
  if (editor)
    editor->SetDrawEdges(0);
  ScRelease(&buffer);
  ScRelease(&grid);
  ScRelease(&editor);
  return true;
}

// Construct both actual bases, owned string and random generator before zeroing manager fields and querying the loader.
// Decoration/config initialization remains a separate lazy dependency; this query owns its returned loader interface.
BdManager::BdManager() : BdServiceBase(kBdManagerServiceId, 0), random(kScRandomSeedFromClock) {
  catalog = nullptr;
  field_020 = 0;
  field_024 = 0;
  grid = nullptr;
  workspace = nullptr;
  image_loader = nullptr;
  field_048 = 0;
  flag_04d = true;
  initialized = false;
  field_070 = 0;
  BdGetCom()->GetClassObject(kBdImageLoaderClassId, IID_BdImageLoader, reinterpret_cast<void **>(&image_loader));
}

BdManager::~BdManager() {
  ScRelease(&image_loader);
}

u8 BdManager::SetOperation18(const ScString *) {
  Initialize();
  return 0;
}

u8 BdManager::SetOperation19(const ScString *) {
  Initialize();
  return 0;
}

// Initialize the manager and obtain its workspace through the actual virtual getter.
// The installed method returns zero after the workspace lookup, regardless of its result.
i32 BdManager::EnsureWorkspace() {
  Initialize();
  GetWorkspace();
  return 0;
}

// Initialize the document identifier, then request a fresh GUID from the real COM service.
// The source returns success regardless of the service result and leaves its grid argument unused.
bool BdManager::CreateGridGuid(BdGrid *, u32 *out) {
  u32 value = static_cast<u32>(reinterpret_cast<uptr>(out));
  *out = kBdGridGuidInitialOutput;
  BdGetCom()->CreateGuid(&value);
  *out = value;
  return true;
}

// Ask the lazily acquired editor to refresh its building thumbnail when an editor exists.
void BdManager::UpdateThumbnail() {
  BdEditor *current = GetEditor();
  if (current)
    current->RenderThumbnail();
}

// Publish the canonical, service or primary view before retaining through the manager's actual virtual.
// Unknown identifiers clear an available output; null output is rejected before any retain.
bool BdManager::QueryInterface(u32 iid, void **out) {
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (iid == IID_ScUnknown)
    *out = static_cast<BdAtomicObject *>(this);
  else if (iid == IID_ScSystemService)
    *out = static_cast<ScSystemService *>(this);
  else if (iid == IID_BdManagerView)
    *out = static_cast<BdManagerView *>(this);
  else {
    *out = nullptr;
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  return true;
}

u32 BdManager::AddRef() {
  return BdAtomicObject::AddRef();
}

u32 BdManager::Release() {
  return BdAtomicObject::Release();
}

// Release the retained grid before destroying the complete manager through its genuine destructor.
u32 BdManager::FinalRelease() {
  ScRelease(&grid);
  delete this;
  return 0;
}

bool BdManager::Init() {
  return true;
}

bool BdManager::Shutdown() {
  if (workspace) {
    if (workspace->editor) {
      workspace->editor->Release();
      workspace->editor = nullptr;
    }
    workspace->Release();
    workspace = nullptr;
  }
  ClearCatalog();
  initialized = false;
  return true;
}

bool BdManager::OnTick(u32) {
  return true;
}

bool BdManager::OnIdle(u32) {
  return true;
}

// Initialize the manager and return the current editor's borrowed grid, or null if no editor is available.
BdGrid *BdManager::GetGrid() {
  Initialize();
  BdEditor *editor = GetEditor();
  return editor ? editor->GetGrid() : nullptr;
}

// Clear the stored path when requested or when the grid is null, then update the editor and post the grid-change message.
i32 BdManager::SetGrid(BdGrid *value, u8 clear_path) {
  Initialize();
  if (clear_path || !value)
    path = ScString("");
  BdEditor *editor = GetEditor();
  if (editor)
    editor->SetGrid(value);
  ScMessage message(kWnMsg_ArchitectGridChanged);
  BdGetMessageServer()->MessagePost(message, false);
  return value != nullptr;
}

// Run genuine initialization before lazily allocating the actual catalog when its cache remains empty.
BdCatalog *BdManager::GetCatalog() {
  Initialize();
  if (!catalog)
    catalog = new BdCatalog;
  return catalog;
}

// Copy the manager's current path only when the underlying filesystem entry exists.
// A missing entry leaves the caller's output string unchanged.
bool BdManager::GetPath(ScString *out) {
  if (!RsFile::FileExists(path))
    return false;
  *out = path;
  return true;
}

// Borrow the singleton's actual primary manager, keeping its canonical unknown view distinct.
BdManager *BdGetManager() {
  if (!BdManagerPrimaryCurrent)
    return nullptr;
  return static_cast<BdManager *>(static_cast<BdManagerView *>(BdManagerPrimaryCurrent));
}

// Borrow the current grid through the singleton manager's original virtual getter.
// An absent manager returns null without constructing or retaining either object.
BdGrid *BdGetCurrentGrid() {
  BdManager *manager = BdGetManager();
  return manager ? manager->GetGrid() : nullptr;
}

// Construct and retain the singleton once for the root director, then return the separate canonical view.
ScUnknown *BdCreateManager() {
  BdManager *manager = BdGetManager();
  if (!manager) {
    manager = new BdManager;
    BdManagerPrimaryCurrent = static_cast<BdManagerView *>(manager);
    manager->AddRef();
  }
  return static_cast<BdAtomicObject *>(manager);
}

// Borrow the loader owned by the actual manager; an absent singleton returns null without construction or retention.
BdImageLoader *BdGetImageLoader() {
  BdManager *manager = BdGetManager();
  return manager ? manager->image_loader : nullptr;
}

// Borrow the catalog through the manager's primary slot 11, preserving the absent-manager null result.
BdCatalog *BdGetCatalog() {
  BdManager *manager = BdGetManager();
  return manager ? manager->GetCatalog() : nullptr;
}

u8 BdManager::Initialize() {
  if (!initialized) {
    BuildDecorations();
    BdLoadConfig();
    initialized = true;
  }
  return 1;
}

void BdManager::ClearCatalog() {
  if (catalog) {
    catalog->ClearAssets();
    delete catalog;
    catalog = nullptr;
  }
}

BdAtomicObject::BdAtomicObject() : references{0} {
}

BdAtomicObject::~BdAtomicObject() {
}

// Publish the canonical basic identity before retaining it through the actual virtual.
bool BdAtomicObject::QueryInterface(u32 iid, void **out) {
  if (!out || iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = this;
  AddRef();
  return true;
}

// Increment the actual Windows32-bit atomic count without retaining the module director.
u32 BdAtomicObject::AddRef() {
  return static_cast<u32>(SDL_AddAtomicInt(&references, 1)) + 1;
}

// Atomically decrement, then dispatch final release when the live count reaches zero.
u32 BdAtomicObject::Release() {
  SDL_AddAtomicInt(&references, -1);
  u32 remaining = static_cast<u32>(SDL_GetAtomicInt(&references));
  if (!remaining)
    return FinalRelease();
  return remaining;
}

u32 BdAtomicObject::FinalRelease() {
  delete this;
  return 0;
}

BdServiceBase::BdServiceBase(u32 value, i32 order) : identifier(value), priority(order), running(false) {
}

BdServiceBase::~BdServiceBase() {
}

u32 BdServiceBase::GetServiceID() const {
  return identifier;
}

void BdServiceBase::SetServiceID(u32 value) {
  identifier = value;
}

i32 BdServiceBase::GetServicePriority() const {
  return priority;
}

bool BdServiceBase::IsServiceRunning() const {
  return running;
}

void BdServiceBase::SetServiceRunning(bool value) {
  running = value;
}

bool BdServiceBase::Init() {
  return true;
}

bool BdServiceBase::Shutdown() {
  return true;
}

bool BdServiceBase::OnTick(u32) {
  return true;
}

bool BdServiceBase::OnIdle(u32) {
  return true;
}

bool BdServiceBase::OnInstall() {
  return true;
}

// Start the export progress command with its initial value, lower bound, maximum and display text.
// A command lookup failure leaves the progress display untouched.
void BdManager::BeginExportProgress(u32 maximum, const ScStringView *text) {
  UiCommandInterface *command = nullptr;
  if (BdGetCommandFactory()->GetCommand(
          kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(0u, kScValueUint32));
    command->PushBackInput(new ScValue(0u, kScValueUint32));
    command->PushBackInput(new ScValue(maximum, kScValueUint32));
    command->PushBackInput(new ScValue(*text));
    command->Execute();
    command->Release();
  }
}

void BdManager::UpdateExportProgress(u32 value) {
  UiCommandInterface *command = nullptr;
  if (BdGetCommandFactory()->GetCommand(
          kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(1u, kScValueUint32));
    command->PushBackInput(new ScValue(value, kScValueUint32));
    command->Execute();
    command->Release();
  }
}

void BdManager::EndExportProgress() {
  UiCommandInterface *command = nullptr;
  if (BdGetCommandFactory()->GetCommand(
          kUiProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->PushBackInput(new ScValue(2u, kScValueUint32));
    command->Execute();
    command->Release();
  }
}

u8 BdManager::ExportBuildings(ScStringView *input_path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) {
  return ExportBuildingsInternal(input_path, count, files, key_count, keys, 0);
}

// Select the alternate export lifecycle and retain the manager's original rendered-image cache flag.
u8 BdManager::ExportBuildingsAlternate(
    ScStringView *input_path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys) {
  field_070 = 1;
  return ExportBuildingsInternal(input_path, count, files, key_count, keys, 1);
}

// Replace the rendered-building archive, render grids in document-category order, and retain requested records.
// Exclude the installed path's undefined dereference of a missing old archive when keys are requested.
u8 BdManager::ExportBuildingsInternal(
    ScStringView *input_path, u32 count, ScStringView ***files, u32 key_count, RsKey *keys, u8 alternate) {
  ScString filename(input_path->ToChar());
  ScString data_directory(BdGetFileSystem()->DataDirectory());
  ScString target(filename), backup(data_directory);
  backup.Append("Res\\BA\\RenderedBuildingsBackup.IXF");
  if (BdFileExists(&filename) &&
      ((BdHasFileAttributes(&filename, 1) && !BdSetFileAttributes(&filename, 1, 0)) || !BdCanWriteFile(&filename)))
    return 0;
  BdIxf *previous = nullptr, *output = nullptr;
  if (BdFileExists(&filename)) {
    if (BdFileExists(&backup))
      BdDeleteFile(&backup);
    if (BdMoveFile(&filename, &backup)) {
      previous = new BdIxf;
      if (!previous->OpenArchive(&backup)) {
        delete previous;
        if (!BdFileExists(&target) && BdFileExists(&backup))
          BdMoveFile(&backup, &target);
        return 0;
      }
    } else {
      if (!BdFileExists(&target) && BdFileExists(&backup))
        BdMoveFile(&backup, &target);
      return 0;
    }
  }
  if (BdFileExists(&target))
    BdDeleteFile(&target);
  output = new BdIxf;
  if (!output->CreateArchive(&target)) {
    delete previous;
    delete output;
    if (!BdFileExists(&target) && BdFileExists(&backup))
      BdMoveFile(&backup, &target);
    return 0;
  }
  u32 progress = 0;
  if (flag_04d) {
    u32 maximum = 32u * count;
    if (static_cast<i32>(count) > 0)
      maximum += 100;
    if (static_cast<i32>(key_count) > 0)
      maximum += 200;
    ScString caption;
    BeginExportProgress(maximum, &caption);
    UpdateExportProgress(0);
  }
  {
    std::vector<ScStringView *> groups[6];
    if (static_cast<i32>(count) > 0) {
      for (u32 i = 0; i < count; ++i) {
        ScStringView *entry = (*files)[i];
        if (!entry)
          continue;
        entry->AddRef();
        entry = (*files)[i];
        BdDocument document;
        if (BdFileExists(entry) && LoadDocument(entry->ToChar(), &document) && document.byte_004 < 6) {
          entry->AddRef();
          entry->AddRef();
          groups[document.byte_004].push_back(entry);
          entry->Release();
        }
        entry->Release();
      }
    }
    usize selected = 0;
    for (u32 group = 1; group < 6; ++group)
      selected += groups[group].size();
    if (selected) {
      Initialize();
      progress = 100;
      if (flag_04d)
        UpdateExportProgress(progress);
      BdEditor *editor = GetEditor();
      editor->HideWindow();
      if (!alternate)
        editor->Init();
      exporter = new BdExporter;
      exporter->Init();
      exporter->SetCatalog(catalog);
      exporter->SetEditor(GetEditor());
      for (u32 group = 1; group < 6; ++group) {
        for (ScStringView *entry : groups[group])
          ExportGrid(output, entry);
        progress += 16;
        if (flag_04d)
          UpdateExportProgress(progress);
      }
      exporter->Shutdown();
      delete exporter;
      exporter = nullptr;
      if (alternate)
        BdGetGraphics()->Draw();
      else {
        editor->Shutdown();
        Init();
      }
    }
    for (u32 group = 0; group < 6; ++group)
      for (ScStringView *entry : groups[group])
        entry->Release();
  }
  if (static_cast<i32>(key_count) > 0 && previous) {
    std::vector<RsKey> directories, rectangles, images;
    for (u32 i = 0; i < key_count; ++i) {
      RsKey linked(0, 0, 0);
      if (previous->ReadGridInfoKey(&keys[i], &linked))
        directories.push_back(linked);
    }
    progress += 50;
    if (flag_04d)
      UpdateExportProgress(progress);
    for (const RsKey &key : directories)
      previous->ReadDirectoryKeys(&key, &rectangles, &images);
    progress += 50;
    if (flag_04d)
      UpdateExportProgress(progress);
    for (u32 i = 0; i < key_count; ++i)
      BdCopyArchiveRecord(previous, &keys[i], output, &keys[i]);
    for (const RsKey &key : directories)
      BdCopyArchiveRecord(previous, &key, output, &key);
    for (const RsKey &key : rectangles)
      BdCopyArchiveRecord(previous, &key, output, &key);
    for (const RsKey &key : images)
      BdCopyArchiveRecord(previous, &key, output, &key);
    progress += 100;
    if (flag_04d)
      UpdateExportProgress(progress);
  }
  if (previous)
    previous->CloseArchive();
  output->CloseArchive();
  // The installed success path closes both heap archives without destroying their objects.
  if (!key_count && !count && BdFileExists(&target))
    BdDeleteFile(&target);
  if (BdFileExists(&backup))
    BdDeleteFile(&backup);
  EndExportProgress();
  return 1;
}

// The archive serializes each completed view through its original frame writer.
u8 BdManager::ExportGrid(BdIxf *archive, ScStringView *filename) {
  if (!archive || !filename)
    return 0;
  BdGrid *value = nullptr;
  if (!LoadGrid(filename->ToChar(), &value))
    return 0;
  u8 result = 1;
  u32 group = random.Next(), instance = value->field_1a8;
  if (!instance) {
    instance = static_cast<u32>(reinterpret_cast<uptr>(this));
    BdGetCom()->CreateGuid(&instance);
  }
  if (value->rotation == 1)
    value->Rotate(-1);
  else if (value->rotation == 3)
    value->Rotate(1);
  if (exporter->SetGrid(value)) {
    BdExportView views[4];
    for (i32 orientation = 0; orientation < 4; ++orientation)
      exporter->RenderView(orientation, &views[orientation]);
    BdExportFrames frames;
    for (u32 scale = 0; scale < 4; ++scale) {
      for (u32 orientation = 0; orientation < 4; ++orientation) {
        u32 index = scale * 4 + orientation;
        frames.images[index] = views[orientation].images[4 - scale];
        frames.bounds[index] = views[orientation].bounds[4 - scale];
      }
    }
    result = archive->WriteFrames(group, instance, &frames);
    if (result)
      result = archive->WriteGridInfo(group, instance, value);
  }
  BdCatalog *global_catalog = BdGetCatalog();
  if (!field_070 && global_catalog)
    global_catalog->ClearRenderedImages();
  value->Release();
  return result;
}

static void BdReadDocumentRegistration(BdDocument *document) {
  ScString encoded;
  ScString registry_path("HKEY_LOCAL_MACHINE\\Software\\Electronic Arts\\Maxis\\SimCity 3000 Unlimited\\ergc");
  BdReadRegistryValue(&registry_path, &encoded);
  u32 length = encoded.Strlen();
  if (length && memchr(encoded.Data(), 0, length) == encoded.Data() + length - 1) {
    char *decoded = new char[length];
    BdTransformRegistryBytes(encoded.Data(), decoded, nullptr);
    document->text_058.FromChar(decoded, length);
    // The stock path does not release this temporary after copying its bytes.
  }
}

// Load the saved grid and its document metadata, then restore its preview and manager path.
// An unsuccessful grid read exits before the original null-grid dereference.
u8 BdManager::LoadGrid(const char *input_path, BdGrid **out) {
  if (!input_path || !out)
    return 0;
  *out = nullptr;
  Initialize();
  ScString filename(input_path);
  BdIxf archive;
  archive.SetCatalog(catalog);
  if (!archive.OpenArchive(&filename))
    return 0;
  BdGrid *loaded = nullptr;
  RsKey grid_key(kBdGridRecordType, kBdBuildingArchiveGroup, kBdGridRecordInstance);
  u8 result = archive.ReadGrid(&grid_key, &loaded);
  if (!loaded) {
    archive.CloseArchive();
    return 0;
  }
  loaded->changed = 0;
  *out = loaded;
  RsKey document_key(kBdDocumentRecordType, kBdBuildingArchiveGroup, kBdDocumentRecordInstance);
  BdDocument document;
  if (archive.ReadDocument(&document_key, &document)) {
    loaded->localized_466 = document.text_008;
    loaded->localized_465 = document.text_01c;
    loaded->field_038 = document.text_030;
    loaded->field_04c = document.word_044;
    loaded->field_054 = document.word_04c;
    loaded->mode = document.bytes_048[0];
    for (u32 i = 0; i < 3; ++i)
      loaded->mode_bytes[i] = document.bytes_048[i + 1];
    if (!document.text_058.Strlen())
      BdReadDocumentRegistration(&document);
    loaded->field_194 = document.text_058;
    if (!document.word_054)
      CreateGridGuid(loaded, &document.word_054);
    loaded->field_1a8 = document.word_054;
  }
  RsKey preview_key(kBdPreviewRecordType, kBdBuildingArchiveGroup, kBdPreviewRecordInstance);
  archive.ReadPreviewImage(&preview_key, &loaded->buffer);
  this->path = filename;
  loaded->CountUsedMaterials();
  loaded->CountSpecialMaterials();
  loaded->CountCells();
  loaded->GetLimitedHeight();
  archive.CloseArchive();
  return result;
}

// Save the current grid, its document metadata and the refreshed preview into a new architect archive.
u8 BdManager::SaveGrid(BdGrid *value, const char *input_path) {
  if (!value || !input_path)
    return 0;
  Initialize();
  if (!BdCanEncodeDocumentText(value))
    return 0;
  ScString filename(input_path);
  BdIxf archive;
  archive.SetCatalog(catalog);
  u8 result = archive.CreateArchive(&filename);
  if (result) {
    RsKey grid_key(kBdGridRecordType, kBdBuildingArchiveGroup, kBdGridRecordInstance);
    value->InitializeGround();
    value->RemoveHiddenCells();
    value->CompactCells();
    if (!BdCanEncodeGridText(value, catalog)) {
      archive.CloseArchive();
      return 0;
    }
    archive.WriteGrid(&grid_key, value);
    value->changed = 0;
    RsKey document_key(kBdDocumentRecordType, kBdBuildingArchiveGroup, kBdDocumentRecordInstance);
    BdDocument document;
    document.byte_004 = value->view_mode;
    document.text_008 = value->localized_466;
    document.text_01c = value->localized_465;
    document.text_030 = value->field_038;
    document.word_044 = value->field_04c;
    document.word_04c = value->field_054;
    document.bytes_048[0] = value->mode;
    for (u32 i = 0; i < 3; ++i)
      document.bytes_048[i + 1] = value->mode_bytes[i];
    BdReadDocumentRegistration(&document);
    CreateGridGuid(value, &document.word_054);
    archive.WriteDocument(&document_key, &document);
    UpdateThumbnail();
    RsKey preview_key(kBdPreviewRecordType, kBdBuildingArchiveGroup, kBdPreviewRecordInstance);
    result = value->buffer ? archive.WritePreviewImage(&preview_key, value->buffer) : 0;
    path = filename;
    archive.CloseArchive();
  }
  return result;
}

// Create an archive containing the current grid's document metadata and stored preview image.
// Reject unsupported native text separately from the ignored document I/O result; the first two arguments are unused.
u8 BdManager::SavePreview(u32, u32, const char *input_path) {
  ScString filename(input_path);
  BdIxf archive;
  u8 result = archive.CreateArchive(&filename);
  if (result) {
    BdDocument document;
    BdGrid *value = GetGrid();
    if (!BdCanEncodeDocumentText(value)) {
      archive.CloseArchive();
      return 0;
    }
    document.byte_004 = value->view_mode;
    document.text_008 = value->localized_466;
    document.text_01c = value->localized_465;
    document.text_030 = value->field_038;
    document.word_044 = value->field_04c;
    document.word_04c = value->field_054;
    document.bytes_048[0] = value->mode;
    for (u32 i = 0; i < 3; ++i)
      document.bytes_048[i + 1] = value->mode_bytes[i];
    BdReadDocumentRegistration(&document);
    CreateGridGuid(value, &document.word_054);
    RsKey document_key(kBdDocumentRecordType, kBdBuildingArchiveGroup, kBdDocumentRecordInstance);
    archive.WriteDocument(&document_key, &document);
    RsKey preview_key(kBdPreviewRecordType, kBdBuildingArchiveGroup, kBdPreviewRecordInstance);
    result = value->buffer ? archive.WritePreviewImage(&preview_key, value->buffer) : 0;
    archive.CloseArchive();
  }
  return result;
}

// Allocate a retained document and load it with its preview from the supplied string path.
// On failure the source leaves the allocated document unpublished and retains its original ownership behavior.
u8 BdManager::LoadPreviewDocument(ScStringView *input_path, GxBuffer **image, BdDocument **document) {
  if (!input_path || !image || !document)
    return 0;
  *document = nullptr;
  *image = nullptr;
  BdDocument *value = new BdDocument;
  value->AddRef();
  u8 result = LoadPreview(input_path->ToChar(), image, value);
  if (result)
    *document = value;
  return result;
}

// Read a building document and its stored preview image from the selected architect archive.
// The archive-open result is replaced by the image reader's raw byte when opening succeeds.
u8 BdManager::LoadPreview(const char *input_path, GxBuffer **image, BdDocument *document) {
  ScString filename(input_path);
  BdIxf archive;
  u8 result = archive.OpenArchive(&filename);
  if (result) {
    RsKey document_key(kBdDocumentRecordType, kBdBuildingArchiveGroup, kBdDocumentRecordInstance);
    archive.ReadDocument(&document_key, document);
    GxBuffer *preview = nullptr;
    RsKey preview_key(kBdPreviewRecordType, kBdBuildingArchiveGroup, kBdPreviewRecordInstance);
    result = archive.ReadPreviewImage(&preview_key, &preview);
    *image = result ? preview : nullptr;
    archive.CloseArchive();
  }
  return result;
}

// Open an architect archive and load its document metadata without reading the preview or grid.
// Return the document reader's raw byte when the archive opened successfully.
u8 BdManager::LoadDocument(const char *input_path, BdDocument *document) {
  ScString filename(input_path);
  BdIxf archive;
  u8 result = archive.OpenArchive(&filename);
  if (result) {
    RsKey key(kBdDocumentRecordType, kBdBuildingArchiveGroup, kBdDocumentRecordInstance);
    result = archive.ReadDocument(&key, document);
    archive.CloseArchive();
  }
  return result;
}

// Initialize the manager and lazily construct its retained workspace. The returned pointer is borrowed.
BdWorkspace *BdManager::GetWorkspace() {
  Initialize();
  if (!workspace)
    workspace = ScAddRefNotNull(new BdWorkspace);
  return workspace;
}

// Initialize the manager and lazily retain the workspace's editor, returning its borrowed pointer.
BdEditor *BdManager::GetEditor() {
  Initialize();
  BdWorkspace *current = GetWorkspace();
  if (!current)
    return nullptr;
  if (!current->editor) {
    current->editor = new BdEditor;
    current->editor->AddRef();
  }
  return current->editor;
}
