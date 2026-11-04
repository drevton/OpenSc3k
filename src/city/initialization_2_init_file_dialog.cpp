#include "city/initialization.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <set>
#include "advisors/ad_ini.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_date.h"
#include "city/ct_init_text.h"
#include "city/ct_scheme_manager_impl.h"
#include "city/module.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/manager_object.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/city_scheme.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// Dispatch the five registered command classes through their original QueryInterface methods.
// An unknown class clears the output and returns the original recoverable failure.
bool CtInitCommandFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  switch (classId) {
  case kCtShutdownCityCommandId: {
    CtInitShutdownCommand *command = new CtInitShutdownCommand;
    if (command->QueryInterface(iid, out))
      return true;
    delete static_cast<UiCommandOwner *>(command);
    return false;
  }
  case kCtLoadCityCommandId: {
    CtInitLoadCommand *command = new CtInitLoadCommand;
    if (command->QueryInterface(iid, out))
      return true;
    delete static_cast<UiCommandOwner *>(command);
    return false;
  }
  case kCtNewCityCommandId: {
    CtInitNewCityCommand *command = new CtInitNewCityCommand;
    if (command->QueryInterface(iid, out))
      return true;
    delete static_cast<UiCommandOwner *>(command);
    return false;
  }
  case kCtFileDialogCommandId: {
    CtInitDialogCommand *command = new CtInitDialogCommand;
    if (command->QueryInterface(iid, out))
      return true;
    delete static_cast<UiCommandOwner *>(command);
    return false;
  }
  case kCtSaveCityCommandId: {
    CtInitSaveCommand *command = new CtInitSaveCommand;
    if (command->QueryInterface(iid, out))
      return true;
    delete static_cast<UiCommandOwner *>(command);
    return false;
  }
  default:
    *out = nullptr;
    return false;
  }
}

// Read one big-endian chunk word, retaining the installed file read and failure contract.
static bool CtReadDefinitionWord(RsFile *file, u32 *out) {
  u8 bytes[4];
  if (!file->Read(bytes, 4))
    return false;
  *out = (u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3];
  return true;
}

CtDefinitionImpl::CtDefinitionImpl()
    : initialized(false), references(0), agentTree(nullptr), attributes(nullptr), city(nullptr) {
}

CtDefinitionImpl::~CtDefinitionImpl() {
  if (initialized)
    CtDefinitionImpl::Shutdown();
}

// Clear the output and publish the city-definition identity for either supported IID.
bool CtDefinitionImpl::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtDefinition);
  HANDLE_QUERY_INTERFACE(IID_CtDefinition, CtDefinition);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the receiver count before retaining the owning SIMINIT director.
u32 CtDefinitionImpl::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Release the director only for a positive count and delete the receiver whenever the resulting count is zero.
u32 CtDefinitionImpl::Release() {
  if (references) {
    CtGetInitDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Create the agent tree, attribute cache and city in order, initializing each before proceeding.
// A failed factory call returns immediately with any earlier child still retained, matching the installed receiver.
bool CtDefinitionImpl::Init() {
  if (initialized)
    Shutdown();
  ScCom *com = CtGetInitDirector()->GZCOM();
  if (!com->GetClassObject(kCtAgentTreeClassId, IID_CtAgentTree, reinterpret_cast<void **>(&agentTree)))
    return false;
  agentTree->Init();
  com = CtGetInitDirector()->GZCOM();
  if (!com->GetClassObject(kOcAttribCacheClassId, IID_OcAttribCache, reinterpret_cast<void **>(&attributes)))
    return false;
  attributes->Init(this);
  com = CtGetInitDirector()->GZCOM();
  if (!com->GetClassObject(kCtCityClassId, IID_CtCity, reinterpret_cast<void **>(&city)))
    return false;
  city->StaticInit(this);
  initialized = true;
  return true;
}

// Shut down and release the city, attribute cache and agent tree in their original reverse initialization order.
bool CtDefinitionImpl::Shutdown() {
  if (!initialized)
    return false;
  initialized = false;
  if (city) {
    city->StaticShutdown();
    city->Release();
    city = nullptr;
  }
  if (attributes) {
    attributes->Shutdown();
    attributes->Release();
    attributes = nullptr;
  }
  if (agentTree) {
    agentTree->Shutdown();
    agentTree->Release();
    agentTree = nullptr;
  }
  return true;
}

// Borrow the retained agent tree without adding a reference.
CtAgentTree *CtDefinitionImpl::GetAgentTypeTree() {
  return agentTree;
}

// Borrow the retained occupant-attribute cache without adding a reference.
OcAttribCache *CtDefinitionImpl::GetOccupantAttribCache() {
  return attributes;
}

bool CtDefinitionImpl::RegisterLayer(u32, u32, u32) {
  return false;
}

// Classify a file by testing the installed validators in their original priority order.
u32 CtDefinitionImpl::GetFileType(const ScStringView &path) {
  if (ValidateSC2File(path))
    return 1;
  if (ValidateSCXFile(path))
    return 2;
  if (ValidateSCTFile(path))
    return 4;
  if (ValidateST3File(path))
    return 5;
  if (ValidateSC3File(path))
    return 3;
  if (ValidateSNRFile(path))
    return 6;
  return 0;
}

// Verify an SC2 FORM/SCDH header and walk complete chunk headers within the opened file's recorded size.
bool CtDefinitionImpl::ValidateSC2File(const ScStringView &path) {
  RsFile file(path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u32 fileSize = file.Size(), word, formSize;
  bool result = false;
  if (CtReadDefinitionWord(&file, &word) && word == kCtLegacyFormTag && CtReadDefinitionWord(&file, &formSize) &&
      CtReadDefinitionWord(&file, &word) && word == kCtLegacyScdhFormType) {
    u32 consumed = 4, chunkSize;
    while (CtReadDefinitionWord(&file, &word) && CtReadDefinitionWord(&file, &chunkSize)) {
      u32 position = file.Position();
      if (chunkSize + position > fileSize || file.Seek(static_cast<i32>(chunkSize), kRsSeekCurrent) == 0)
        break;
      consumed += chunkSize + 8;
      if (consumed >= formSize) {
        result = true;
        break;
      }
    }
  }
  file.Close();
  return result;
}

// Probe the first four file bytes when opening succeeds, but preserve the installed unconditional rejection.
bool CtDefinitionImpl::ValidateSCXFile(const ScStringView &path) {
  RsFile file(path);
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    u8 bytes[4];
    file.Read(bytes, 4);
    file.Close();
  }
  return false;
}

// Accept a readable four-byte file only when the supplied nonempty name ends in the character '3'.
bool CtDefinitionImpl::ValidateSC3File(const ScStringView &path) {
  RsFile file(path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u8 bytes[4];
  bool result = file.Read(bytes, 4);
  if (result) {
    ScString name(path);
    if (name.Strlen())
      result = name.Data()[name.Strlen() - 1] == '3';
  }
  file.Close();
  return result;
}

static void CtFoldDefinitionExtension(ScString *extension, bool upper) {
  if (upper)
    extension->MakeUpper();
  else
    extension->MakeLower();
}

// Identify a scenario by its case-insensitive final extension without opening the path.
bool CtDefinitionImpl::ValidateSNRFile(const ScStringView &path) {
  ScString extension;
  if (!RsFile::GetPathExtension(path, &extension))
    return false;
  CtFoldDefinitionExtension(&extension, false);
  return !strcmp(extension.Data(), ".snr");
}

static bool CtValidateInitExtension(const ScStringView &path, const char *expected) {
  RsFile file(path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u8 bytes[4];
  bool result = file.Read(bytes, 4);
  if (result) {
    ScString full(path);
    ScPath parsed(full);
    ScString extension(parsed.Extension());
    CtFoldDefinitionExtension(&extension, true);
    result = !strcmp(extension.Data(), expected);
  }
  file.Close();
  return result;
}

bool CtDefinitionImpl::ValidateSCTFile(const ScStringView &path) {
  return CtValidateInitExtension(path, ".SCT");
}

bool CtDefinitionImpl::ValidateST3File(const ScStringView &path) {
  return CtValidateInitExtension(path, ".ST3");
}

// Construct the registered game-command factory with its original zero reference count.
static ScUnknown *CtCreateInitCommand() {
  return new CtInitCommandFactory;
}

// Construct the registered legacy city importer with its original zero reference count.
static ScUnknown *CtCreateLegacyImporter() {
  return new CtLegacyImporter;
}

// Construct the registered city-definition receiver with its original empty child-service state.
static ScUnknown *CtCreateInit42FBF1D7() {
  return new CtDefinitionImpl;
}

// Construct the registered agent-tree receiver with its original empty maps and zero reference count.
static ScUnknown *CtCreateInitC306B854() {
  return new CtAgentTreeImpl;
}

// Construct the registered occupant-attribute cache with its original initial reference count.
static ScUnknown *OcCreateInitAttribCache() {
  return new OcInitAttribCache;
}

// Construct the registered city-scheme manager with its original empty catalogs and service references.
static ScUnknown *CtCreateSchemeManager() {
  return new CtSchemeManagerImpl;
}

// Construct the registered plugin-building manager with its original initial reference count.
static ScUnknown *CtCreateInit852BEAA1() {
  return new BdPluginManagerImpl;
}

// Register the seven installed classes in their original order without constructing their receivers.
CtInitDirector::CtInitDirector() {
  AddClass(kCtInitCommandFactoryClassId, CtCreateInitCommand, 0);
  AddClass(kScImporterClassId, CtCreateLegacyImporter, 0);
  AddClass(kCtDefinitionClassId, CtCreateInit42FBF1D7, 0);
  AddClass(kCtAgentTreeClassId, CtCreateInitC306B854, 0);
  AddClass(kOcAttribCacheClassId, OcCreateInitAttribCache, 0);
  AddClass(kCtSchemeManagerClassId, CtCreateSchemeManager, 0);
  AddClass(kBdPluginManagerClassId, CtCreateInit852BEAA1, 0);
}

u32 CtInitDirector::GetDirectorID() const {
  return kCtInitDirectorId;
}

// The shared base's SIMINIT body uses this module's framework for registry acquisition.
// Run registration and release any published registry even if the acquisition returned false.
bool CtInitDirector::OnStart(ScCom *) {
  ScRegistry *registry = nullptr;
  CtGetInitFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

// Form the original game-command registry name and register it only when name construction succeeds.
bool CtInitDirector::AppRegister(ScRegistry *registry) {
  ScString name, key("0x2380fbf2"), value("0");
  if (!registry->MakeRegistryName("cogamecmd", key, value, &name))
    return false;
  return registry->Register(name);
}

alignas(CtInitDirector) static u8 CtInitDirectorStorage[sizeof(CtInitDirector)];
static bool CtInitDirectorInitialized;

static void CtDestroyInitDirector() {
  reinterpret_cast<CtInitDirector *>(CtInitDirectorStorage)->~CtInitDirector();
}

// Set the original lazy flag before construction and register exit cleanup after construction completes.
// Reentrant access observes the same static storage and never constructs a second receiver.
ScDirector *CtGetInitDirector() {
  if (!CtInitDirectorInitialized) {
    CtInitDirectorInitialized = true;
    new (CtInitDirectorStorage) CtInitDirector();
    atexit(CtDestroyInitDirector);
  }
  return reinterpret_cast<CtInitDirector *>(CtInitDirectorStorage);
}

void SC3_STDCALL CtInitUpdateCityStats(WnWindow *window, i32 population, i64 funds, const CtDate *date);

// Accept an empty filename or one made entirely of ordinary spaces.
bool SC3_CDECL CtInitIsBlankFileName(const char *text) {
  while (*text == ' ')
    ++text;
  return *text == 0;
}

// Classify a lead byte according to the initially selected language, caching that language on first use.
// Languages 17 and 18 compare the full incoming word; language 15 compares its low byte.
int SC3_CDECL CtInitIsFilenameLeadByte(u32 value) {
  static i32 language = -1;
  if (language < kScLanguageCurrent)
    language = static_cast<i32>(CtGetInitLanguageManager()->GetCurrentLanguage());
  if (language == kScLanguageJapanese) {
    u8 byte = static_cast<u8>(value);
    return (byte >= 0x81 && byte <= 0x9f) || (byte >= 0xe0 && byte <= 0xfc);
  }
  if (language == kScLanguageSimplifiedChinese)
    return value > 0xff || (value >= 0xa1 && value <= 0xfc);
  if (language == kScLanguageTraditionalChinese)
    return value > 0xff || (value >= 0x81 && value <= 0xfe);
  return 0;
}

// Search a filename for forbidden characters while skipping the byte after each recognized multibyte lead.
const char *SC3_CDECL CtInitFindInvalidFilename(const char *text, const char *characters) {
  u32 pointerWord = static_cast<u32>(reinterpret_cast<uptr>(text)) & kCtFilenameLeadPointerHighBytesMask;
  bool skip = false;
  for (const char *cursor = text; *cursor; ++cursor) {
    u8 byte = static_cast<u8>(*cursor);
    if (skip) {
      skip = false;
    } else if (CtInitIsFilenameLeadByte(pointerWord | byte)) {
      skip = true;
    } else if (strchr(characters, byte)) {
      return cursor;
    }
  }
  return nullptr;
}

u32 SC3_STDCALL CtInitSetChildEnabled(WnWindow *parent, u32 identifier, u32 enabled) {
  WnWindow *child = parent->GetChildWindowFromID(identifier);
  return child ? child->SetFlag(kWnFlagEnabled, static_cast<u8>(enabled) != 0) : 0;
}

// Create and configure a label, then attach its window to the supplied parent.
// A failed attachment shuts down the new window before the local label reference is released.
bool SC3_STDCALL CtInitAddLabel(
    WnWindow *parent, u32 identifier, i32 x, i32 y, const ScStringView &caption, u32 alignment) {
  WnResources *resources = CtGetInitWindowResources();
  if (!resources)
    return false;
  WnLabelControl *label = resources->CreateLabel(identifier, caption);
  if (!label)
    return false;
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  label->SetLabelFlag(kWnLabelUseFontColors, false);
  label->SetTextColor(resources->GetColor(kWnThemeMutedTextColorId));
  label->SetAlignment(alignment);
  label->GetWindow()->GZWinMoveTo(x, y);
  bool added = parent->ChildAdd(label->GetWindow());
  if (!added)
    label->GetWindow()->Shutdown();
  label->Release();
  return added;
}

// Create a fitted right-aligned label whose right edge lands at the supplied x coordinate.
// Release the local label reference after attaching or shutting down its window.
bool SC3_STDCALL CtInitAddRightLabel(
    WnWindow *parent, u32 identifier, i32 x, i32 y, const ScStringView &caption, u32 alignment) {
  WnResources *resources = CtGetInitWindowResources();
  if (!resources)
    return false;
  WnLabelControl *label = resources->CreateLabel(identifier, caption);
  if (!label)
    return false;
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  label->SetLabelFlag(kWnLabelUseFontColors, false);
  label->SetTextColor(resources->GetColor(kWnThemeMutedTextColorId));
  label->Fit();
  label->SetAlignment(alignment);
  WnWindow *window = label->GetWindow();
  label->GetWindow()->GZWinMoveTo(x - window->GetW(), y);
  bool added = parent->ChildAdd(label->GetWindow());
  if (!added)
    label->GetWindow()->Shutdown();
  label->Release();
  return added;
}

// Attach the two nested flat rectangles that form the recessed input border.
// Each temporary reference is released after its window is attached or destroyed.
bool SC3_STDCALL CtInitAddInputBorder(WnWindow *parent, i32 x, i32 y, i32 width, i32 height) {
  WnFlatRect *outer = new WnFlatRect();
  outer->AddRef();
  WnWindow *window = outer->GetWindow();
  window->SetArea(x, y, x + width, y + height);
  window->SetFlag(kWnFlagReorderable, false);
  outer->SetOutlineFlags(15);
  WnResources *first = CtGetInitWindowResources(), *second = CtGetInitWindowResources();
  WnResources *third = CtGetInitWindowResources(), *fourth = CtGetInitWindowResources();
  u32 bottom = first->GetColor(kWnThemeOutlineLightColorId), right = second->GetColor(kWnThemeOutlineLightColorId);
  u32 top = third->GetColor(kWnThemeOutlineMediumColorId), left = fourth->GetColor(kWnThemeOutlineMediumColorId);
  outer->SetOutlineColors(left, top, right, bottom);
  if (!parent->ChildAdd(outer)) {
    CtGetInitWindowManager()->DestroyWindow(outer);
    window->Release();
    return false;
  }
  window->Release();
  WnFlatRect *inner = new WnFlatRect();
  inner->AddRef();
  window = inner->GetWindow();
  window->SetArea(x + 1, y + 1, x + width - 1, y + height - 1);
  window->SetFlag(kWnFlagReorderable, false);
  inner->SetOutlineFlags(15);
  first = CtGetInitWindowResources();
  second = CtGetInitWindowResources();
  third = CtGetInitWindowResources();
  fourth = CtGetInitWindowResources();
  bottom = first->GetColor(kWnThemeSecondaryMutedColorId);
  right = second->GetColor(kWnThemeSecondaryMutedColorId);
  top = third->GetColor(kWnThemeOutlineDarkColorId);
  left = fourth->GetColor(kWnThemeOutlineDarkColorId);
  inner->SetOutlineColors(left, top, right, bottom);
  bool added = parent->ChildAdd(inner);
  if (!added)
    CtGetInitWindowManager()->DestroyWindow(inner);
  window->Release();
  return added;
}

// Add a bordered single-line filename editor and initialize its font, text and character limit.
// If attachment fails, destroy the editor before releasing the temporary reference.
bool SC3_STDCALL CtInitAddFileEditor(
    WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height, const ScStringView *text) {
  CtInitAddInputBorder(parent, x, y, width, height);
  WnTextEdit *editor = new WnTextEdit(1);
  editor->AddRef();
  editor->SetFont(CtGetInitWindowResources()->GetFont(kWnFontUiSmall));
  editor->SetArea(x + 2, y + 2, x + width - 2, y + height - 2);
  editor->SetID(identifier);
  editor->SetText(text);
  editor->SetOutline(0);
  editor->SetCharacterLimit(50);
  bool added = parent->ChildAdd(editor);
  if (added)
    editor->SetFillColor(CtGetInitWindowResources()->GetColor(kWnColorWhite));
  else
    CtGetInitWindowManager()->DestroyWindow(editor);
  editor->Release();
  return added;
}

CtInitImageResource::CtInitImageResource() : buffer(nullptr) {
}

CtInitImageResource::CtInitImageResource(const CtInitImageResource &source) : buffer(source.buffer) {
  if (buffer)
    buffer->AddRef();
}

// Select the image factory's mode and format, then resolve the requested image while the options are active.
// Release the queried options and factory in the installed order.
CtInitImageResource::CtInitImageResource(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format)
    : buffer(nullptr) {
  RsManager *manager = CtGetInitResourceManager();
  RsFactoryInterface *factory = nullptr;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options = nullptr;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      LoadResource(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the held image and clear its stored pointer.
CtInitImageResource::~CtInitImageResource() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

// Retain the source buffer before releasing the previous one, preserving self-assignment.
// The matching retail implementation is SC3U.
CtInitImageResource *CtInitImageResource::operator=(const CtInitImageResource &source) {
  if (&source != this) {
    if (source.buffer)
      source.buffer->AddRef();
    if (buffer)
      buffer->Release();
    buffer = source.buffer;
  }
  return this;
}

void CtInitImageResource::LoadResource(RsManager *manager, u32 group, u32 instance) {
  RsKey key(kRsImageResourceType, group, instance);
  manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Borrow the image buffer retained by this resource scope without acquiring another reference.
GxBuffer *CtInitImageResource::Value() const {
  return buffer;
}

bool CtInitImageResource::IsBufferPresent() const {
  return buffer != nullptr;
}

CtInitFileDialog::CtInitFileDialog(u32 value) : references(0) {
  mode = value;
}

CtInitFileDialog::~CtInitFileDialog() {
}

bool CtInitFileDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtInitFileDialog::AddRef() {
  return references++;
}

// Delete the callback for counts zero or one; otherwise decrement and return the previous count.
u32 CtInitFileDialog::Release() {
  u32 previous = references;
  if (previous > 1) {
    --references;
    return previous;
  }
  delete this;
  return 0;
}

// Route generic-window and control messages to the original dialog actions and update the selected filename.
// List entries retain their leading type bytes; the directory and confirmation operations remain separate calls.
bool CtInitFileDialog::DoWinMsg(WnWindow *window, u32 message, uptr firstWord, uptr secondWord, uptr) {
  if (message == kWnMsg_Init)
    return BuildContent(window) != 0;
  if (message == kWnMsg_Shutdown)
    return Close(window);
  if (message == kWnMsg_ClientConstraints)
    return GetLimits(window, reinterpret_cast<ScRect *>(firstWord));
  if (message == kWnMsg_KeyDown) {
    if (firstWord == 13) {
      if (output.Strlen())
        return Commit(window, kCtFileAcceptButton) != 0;
      return true;
    }
    if (firstWord == 27 || (firstWord == 115 && (secondWord & 4))) {
      Finish(window, kCtFileCancelButton);
      return true;
    }
    return false;
  }
  if (message != kWnMsg_Command)
    return false;
  u32 identifier = static_cast<u32>(secondWord);
  if (firstWord == kWnCommandButtonActivated || firstWord == kWnCommandButtonActivatedByWindow)
    return Commit(window, identifier) != 0;
  if (firstWord == kWnCommandComboSelectionChanged) {
    UpdateDirectoryFromCombo(window);
    return false;
  }
  if (firstWord == kWnCommandListSelectionChanged || firstWord == kWnCommandListDoubleClickSelection) {
    WnWindow *child = window->GetChildWindowFromID(kCtFileList);
    WnListControl *list = nullptr;
    if (!child || !child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list)))
      return false;
    ScString selected;
    currentDirectory = directories.begin();
    for (i32 index = 0; index <= list->GetSelection(); ++index) {
      selected = *currentDirectory;
      ++currentDirectory;
    }
    list->Release();
    if (!selected.Strlen())
      return false;
    i8 kind = static_cast<i8>(selected.begin[0]);
    if (firstWord == kWnCommandListSelectionChanged) {
      if (kind == 7 || kind == 16 || kind == 18 || kind == 19 || kind == 30)
        Populate(window);
      return false;
    }
    if (kind == 7 || kind == 16 || kind == 18 || kind == 19 || kind == 30)
      return Commit(window, kCtFileAcceptButton) != 0;
    if (kind == 6 || kind == 12 || kind == 14 || kind == 15 || kind == 17)
      Populate(window);
    return false;
  }
  if (firstWord == kWnCommandTextAccepted) {
    if (identifier == kCtFileNameEdit) {
      if (eighth.Strlen())
        Commit(window, kCtFileAcceptButton);
    } else if (identifier != kCtFileMayorNameEdit && identifier != kCtFileCityNameEdit) {
      return false;
    }
    UpdateDirectoryFromCombo(window);
    return false;
  }
  if (firstWord != kWnCommandTextChanged ||
      (identifier != kCtFileNameEdit && identifier != kCtFileMayorNameEdit && identifier != kCtFileCityNameEdit))
    return false;
  WnWindow *child = window->GetChildWindowFromID(identifier);
  WnTextControl *control = nullptr;
  if (!child || !child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&control)))
    return false;
  ScString text;
  if (control->GetText(&text) <= 0) {
    CtInitSetChildEnabled(window, kCtFileAcceptButton, false);
    control->Release();
    return true;
  }
  if (identifier == kCtFileMayorNameEdit || identifier == kCtFileCityNameEdit) {
    if (identifier == kCtFileMayorNameEdit)
      sixth = text;
    else
      seventh = text;
    CtInitSetChildEnabled(window, kCtFileAcceptButton, true);
    control->Release();
    return true;
  }
  eighth = text;
  output = fourth;
  output.Append(eighth.Data(), eighth.Strlen());
  bool enable = true;
  if (mode == kCtFileOpenCity || mode == kCtFileOpenTerrain || mode == kCtFileOpenStarterTown ||
      mode == kCtFileOpenScenario || mode == kCtFileImportReplacementSet) {
    enable = false;
    WnWindow *listWindow = window->GetChildWindowFromID(kCtFileList);
    WnListControl *list = nullptr;
    if (listWindow && listWindow->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list))) {
      for (i32 index = 0; index < list->GetItemCount(); ++index) {
        currentDirectory = directories.begin();
        ScString entry;
        for (i32 current = 0; current <= index; ++current) {
          entry = *currentDirectory;
          ++currentDirectory;
        }
        u32 length = entry.Strlen() > 2 ? entry.Strlen() - 2 : 0;
        if (length == text.Strlen()) {
          int comparison = 0;
          if (length)
            comparison = ScCompareNoCase(entry.begin + 2, text.begin, length);
          if (!comparison) {
            enable = true;
            break;
          }
        }
      }
      list->Release();
    }
  }
  WnWindow *button = window->GetChildWindowFromID(kCtFileAcceptButton);
  if (button)
    button->SetFlag(kWnFlagEnabled, enable);
  i32 kind = mode == kCtFileOpenScenario ? 19
      : mode == kCtFileOpenTerrain       ? 16
      : mode == kCtFileOpenStarterTown   ? 7
                                         : 18;
  ScString extension;
  if (FindName(kind, &extension)) {
    output.Append(".");
    output.Append(extension.Data(), extension.Strlen());
  }
  control->Release();
  return true;
}

// Create the generic city-file window, configure its styles and buffer, and install this callback.
// The generic interface's retained reference accompanies the returned primary window view.
bool CtInitFileDialog::BuildWindow(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic = nullptr;
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, true);
  generic->SetStyle(kUiGenericDraggable, CtGetInitCity() != nullptr);
  generic->SetStyle(kUiGenericResizable, false);
  generic->SetStyle(kUiGenericScroller, false);
  generic->SetStyle(kUiGenericBackButton, false);
  generic->SetStyle(kUiGenericSecondButton, false);
  generic->GetWindow()->PrivateBuffer(true);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtFileDialogWindow);
  return true;
}

// Construct the installed file-dialog controls, preview preference, file types and initial directory selection.
u8 CtInitFileDialog::BuildContent(WnWindow *window) {
  u32 title = 0, group = kRsGameTextGroup;
  switch (mode) {
  case kCtFileOpenCity:
    title = 544;
    break;
  case kCtFileOpenScenario:
    title = 2;
    group = kRsSupplementalGameTextGroup;
    break;
  case kCtFileImportReplacementSet:
    title = 320;
    group = kRsSupplementalGameTextGroup;
    break;
  case kCtFileSaveCity:
    title = 550;
    break;
  case kCtFileOpenTerrain:
    title = 588;
    break;
  case kCtFileOpenStarterTown:
    title = 719;
    break;
  default:
    break;
  }
  if (title) {
    CtInitText resource(title, group);
    ScString caption = static_cast<ScString>(resource);
    window->SetCaption(caption);
  }
  ApLoadPrefs *preferences = CtGetInitPreferences()->LoadPrefs();
  flags[2] = preferences->ready ? CtGetInitPreferences()->LoadPrefs()->show_city_stats : 1;
  flags[1] = 0;
  flags[0] = 0;
  CtInitFilePair pair;
  const char *extensions[] = {"sc3", "sc2", "snr", "st3", "sct"};
  const char kinds[] = {18, 30, 19, 7, 16};
  for (u32 i = 0; i < 5; ++i) {
    pair.first.FromChar(extensions[i]);
    pair.second.FromChar(&kinds[i], 1);
    selections.push_back(pair);
  }
  FtFont *font = CtGetInitWindowResources()->GetFont(kWnFontUiFileDialog);
  AddImageButton(window, kCtFileAcceptButton, 425, 310, kRsStandardButtonImageId, 48);
  CtInitSetChildEnabled(window, kCtFileAcceptButton, 0);
  {
    CtInitText resource(kRsGameLookInText, kRsGameTextGroup);
    ScString caption = static_cast<ScString>(resource);
    CtInitAddRightLabel(window, kCtFileDirectoryLabel, 242, 40, caption, 7);
  }
  {
    CtInitText resource(kRsGameFileNameText, kRsGameTextGroup);
    ScString caption = static_cast<ScString>(resource);
    CtInitAddRightLabel(window, kCtFileNameLabel, 242, 260, caption, 7);
  }
  CtInitAddFileEditor(window, kCtFileNameEdit, 252, 260, 214, 19, &seventh);
  WnWindow *filenameEditor = window->GetChildWindowFromID(kCtFileNameEdit);
  WnWindow *filenameLabel = window->GetChildWindowFromID(kCtFileNameLabel);
  if (filenameEditor && filenameLabel) {
    i32 labelHeight = filenameLabel->GetH(), editorHeight = filenameEditor->GetH();
    i32 y = filenameEditor->GetT() + (editorHeight - labelHeight) / 2;
    i32 labelWidth = filenameLabel->GetW(), x = filenameEditor->GetL() - 2 - labelWidth;
    filenameLabel->GZWinMoveTo(x, y);
  }
  WnList *list = new WnList(false);
  list->AddRef();
  list->SetFillColor(CtGetInitGraphics()->ConvertRGBValueToNative16Bit(255, 255, 255));
  list->SetHighlightFillAlpha(1.0f);
  list->SetHighlightColor(CtGetInitGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
  list->SetArea(252, 70, 466, 245);
  list->SetFont(font);
  list->SetID(kCtFileList);
  list->SetMinimumLineHeight(16);
  if (!window->ChildAdd(list)) {
    CtGetInitWindowManager()->DestroyWindow(list);
    return 0;
  }
  BuildDriveCombo(window, font);
  WnWindow *combo = window->GetChildWindowFromID(kCtFileDirectoryCombo);
  filenameLabel = window->GetChildWindowFromID(kCtFileDirectoryLabel);
  if (combo && filenameLabel) {
    i32 labelHeight = filenameLabel->GetH(), comboHeight = combo->GetH();
    i32 y = combo->GetT() + (comboHeight - labelHeight) / 2;
    i32 labelWidth = filenameLabel->GetW(), x = combo->GetL() - 2 - labelWidth;
    filenameLabel->GZWinMoveTo(x, y);
  }
  ScString empty;
  if (mode == kCtFileOpenCity || mode == kCtFileSaveCity || mode == kCtFileImportReplacementSet) {
    CtInitAddLabel(window, kCtFilePopulationLabel, 20, 245, empty, 1);
    CtInitAddLabel(window, kCtFileFundsLabel, 20, 259, empty, 1);
    CtInitAddLabel(window, kCtFileDateLabel, 20, 273, empty, 1);
  }
  if (mode == kCtFileOpenCity || mode == kCtFileImportReplacementSet) {
    CtInitAddLabel(window, kCtFileMayorLabel, 20, 287, empty, 1);
    CtInitAddLabel(window, kCtFileCityLabel, 20, 301, empty, 1);
    WnLabelControl *control = nullptr;
    if (window->GetChildAs(kCtFileMayorLabel, IID_WnLabelControl, reinterpret_cast<void **>(&control))) {
      control->GetWindow()->SetW(360);
      control->Release();
    }
    if (window->GetChildAs(kCtFileMayorLabel, IID_WnLabelControl, reinterpret_cast<void **>(&control))) {
      control->GetWindow()->SetW(360);
      control->Release();
    }
  }
  if (mode == kCtFileSaveCity) {
    {
      CtInitText resource(kRsGameMayorNameText, kRsGameTextGroup);
      ScString caption = static_cast<ScString>(resource);
      CtInitAddLabel(window, kCtFileMayorLabel, 20, 292, caption, 1);
    }
    {
      CtInitText resource(kRsGameCityNameText, kRsGameTextGroup);
      ScString caption = static_cast<ScString>(resource);
      CtInitAddLabel(window, kCtFileCityLabel, 20, 315, caption, 1);
    }
    WnLabelControl *control = nullptr;
    u32 firstWidth = 0, secondWidth = 0;
    bool haveFirst = window->GetChildAs(kCtFileMayorLabel, IID_WnLabelControl, reinterpret_cast<void **>(&control));
    if (haveFirst) {
      control->Fit();
      firstWidth = control->GetWindow()->GetW();
      control->Release();
    }
    bool haveSecond = window->GetChildAs(kCtFileCityLabel, IID_WnLabelControl, reinterpret_cast<void **>(&control));
    if (haveSecond) {
      control->Fit();
      secondWidth = control->GetWindow()->GetW();
      control->Release();
    }
    // Stock consumes an uninitialized width if either lookup fails; only its defined path reaches the editors.
    if (haveFirst && haveSecond) {
      u32 left = (firstWidth >= secondWidth ? firstWidth : secondWidth) + 25;
      CtCity *city = CtGetInitCity();
      if (city) {
        {
          ScString mayor(*city->GetMayorName());
          sixth = mayor;
        }
        {
          ScString name(*city->GetCityName());
          seventh = name;
        }
      }
      CtInitAddFileEditor(window, kCtFileMayorNameEdit, left, 292, 168, 19, &sixth);
      CtInitAddFileEditor(window, kCtFileCityNameEdit, left, 315, 168, 19, &seventh);
    }
  }
  if (mode == kCtFileOpenCity) {
    WnButton *button;
    {
      CtInitText resource(kRsGameShowCityStatsText, kRsGameTextGroup);
      ScString caption = static_cast<ScString>(resource);
      button = CtGetInitWindowResources()->CreateCheckBox(kCtFileShowStatistics, caption, nullptr, nullptr, 0);
    }
    button->SetChecked(flags[2] != 0);
    button->GetWindow()->SetW(200);
    button->SetFont(CtGetInitWindowResources()->GetFont(kWnFontUiSmall));
    button->SetFontColor(kWnButtonTextEnabled, CtGetInitWindowResources()->GetColor(kWnThemeMutedTextColorId));
    button->SetFontColor(kWnButtonTextHovered, CtGetInitWindowResources()->GetColor(kWnThemeMutedTextColorId));
    button->SetFontColor(kWnButtonTextDisabled, CtGetInitWindowResources()->GetColor(kWnThemeMutedTextColorId));
    button->GetWindow()->GZWinMoveTo(22, 325);
    if (!window->ChildAdd(button->GetWindow())) {
      CtGetInitWindowManager()->DestroyWindow(button->GetWindow());
      button->Release();
      return 0;
    }
    button->Release();
  }
  if (mode == kCtFileOpenCity || mode == kCtFileSaveCity || mode == kCtFileOpenTerrain ||
      mode == kCtFileImportReplacementSet) {
    UiMapControl *preview = nullptr;
    if (!CtGetInitDirector()->GZCOM()->GetClassObject(
            kUiMapControlClassId, IID_UiMapControl, reinterpret_cast<void **>(&preview)))
      return 0;
    preview->AsIGZWin()->SetArea(17, 42, 211, 236);
    preview->AsIGZWin()->SetID(kCtFileMapPreview);
    preview->SetDrawWindowOutline(false);
    preview->SetDrawImageOutline(true);
    if (mode == kCtFileSaveCity) {
      CtCity *city = CtGetInitCity();
      if (city) {
        ScDirtBag *dirt = city->DirtBag();
        if (dirt) {
          SpMapSpriteFactory *factory = nullptr;
          if (dirt->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
            GxRasterSource *sprite = nullptr;
            factory->CreateMapSprite(10001, IID_GxRasterSource, reinterpret_cast<void **>(&sprite));
            if (sprite) {
              preview->AddFilter(sprite);
              sprite->Release();
            }
            factory->Release();
          }
        }
        CtZoneLayer *zone = city->ZoneLayer();
        if (zone) {
          SpMapSpriteFactory *factory = nullptr;
          if (zone->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
            GxRasterSource *sprite = nullptr;
            if (factory->CreateMapSprite(10011, IID_GxRasterSource, reinterpret_cast<void **>(&sprite))) {
              preview->AddFilter(sprite);
              sprite->Release();
            }
            factory->Release();
          }
        }
      }
    } else {
      GxColorType format(GxColorType::Color::RGB16);
      CtInitImageResource resource(
          kCtFileCityPreviewPlaceholderImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, format);
      GxBuffer *image = resource.Value();
      if (image) {
        image->AddRef();
        image->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
        u32 width = image->Width(), height = image->Height();
        WnBitmap *bitmap = new WnBitmap;
        bitmap->AddRef();
        bitmap->SetID(kCtFilePreviewPlaceholder);
        bitmap->SetArea(0, 0, width, height);
        bitmap->SetImage(image);
        if (!window->ChildAdd(bitmap->GetWindow())) {
          CtGetInitWindowManager()->DestroyWindow(bitmap->GetWindow());
          bitmap->Release();
          return 0;
        }
        bitmap->GetWindow()->GZWinMoveTo(17, 43);
        bitmap->Release();
        image->Release();
      }
    }
    if (mode != kCtFileSaveCity)
      preview->AsIGZWin()->HideWindow();
    if (!window->ChildAdd(preview->AsIGZWin())) {
      CtGetInitWindowManager()->DestroyWindow(preview->AsIGZWin());
      preview->Release();
      return 0;
    }
    preview->Release();
    for (i32 index = 0; index < 2; ++index) {
      WnOutlineControl *outline = nullptr;
      if (CtGetInitDirector()->GZCOM()->GetClassObject(
              kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)) &&
          outline) {
        outline->SetFlags(kWnOutlineInset | kWnOutlinePolygon);
        ScRect area{0, 0, 194, 194};
        outline->GetWindow()->SetArea(area);
        ScPoint points[4] = {{97, index ? -1 : 0}, {index ? -1 : 0, 97},
            {97, index ? 195 : 194}, {index ? 195 : 194, 97}};
        outline->SetShapePoly(4, points);
        outline->GetWindow()->GZWinMoveTo(17, 42);
        outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
        outline->GetWindow()->SetID(kCtFilePreviewOutlineIdBase + index);
        window->ChildAdd(outline->GetWindow());
        outline->Release();
      }
    }
  }
  if (mode == kCtFileOpenStarterTown) {
    GxColorType format(GxColorType::Color::RGB16);
    CtInitImageResource resource(
        kCtFileStarterTownPreviewPlaceholderImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *image = resource.Value();
    if (image) {
      image->AddRef();
      image->Width();
      image->Height();
      WnOutlineControl *outline = nullptr;
      if (CtGetInitDirector()->GZCOM()->GetClassObject(
              kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)) &&
          outline) {
        outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
        ScRect area{0, 0, 164, 154};
        outline->GetWindow()->SetArea(area);
        outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
        outline->GetWindow()->GZWinMoveTo(14, 69);
        window->ChildAdd(outline->GetWindow());
        outline->Release();
      }
      WnBitmap *bitmap = new WnBitmap;
      bitmap->AddRef();
      bitmap->SetID(kCtFilePreviewBitmap);
      bitmap->SetArea(0, 0, 162, 152);
      bitmap->SetImage(image);
      if (!window->ChildAdd(bitmap->GetWindow())) {
        CtGetInitWindowManager()->DestroyWindow(bitmap->GetWindow());
        bitmap->Release();
        return 0;
      }
      bitmap->GetWindow()->GZWinMoveTo(15, 70);
      bitmap->Release();
    }
    CtInitText text(kRsGameStarterTownDescriptionText, kRsGameTextGroup);
    ScString caption;
    if (text.IsStringPresent())
      text.Detokenize(&caption);
    WnLabelControl *label = CtGetInitWindowResources()->CreateLabel(kCtFilePreviewDescriptionLabel, caption);
    if (label) {
      label->SetFont(CtGetInitWindowResources()->GetFont(kWnFontUiMain));
      label->SetLabelFlag(kWnLabelUseFontColors, true);
      label->SetLabelFlag(kWnLabelWrapText, true);
      label->GetWindow()->SetSize(155, 140);
      label->SetAlignment(kWnLabelLeftTop);
      label->GetWindow()->GZWinMoveTo(15, 232);
      if (!window->ChildAdd(label->GetWindow()))
        label->GetWindow()->Shutdown();
      label->Release();
    }
    if (image)
      image->Release();
  }
  if (mode == kCtFileOpenScenario) {
    GxColorType format(GxColorType::Color::RGB16);
    CtInitImageResource resource(
        kCtFileStarterTownPreviewPlaceholderImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *image = resource.Value();
    i32 labelRight = 252;
    {
      CtInitText text(kRsSupplementalGameAuthorText, kRsSupplementalGameTextGroup);
      ScString caption = static_cast<ScString>(text);
      CtInitAddLabel(window, kCtFileAuthorLabel, 252, 318, caption, 1);
    }
    WnLabelControl *label = nullptr;
    if (window->GetChildAs(kCtFileAuthorLabel, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
      label->Fit();
      labelRight = label->GetWindow()->GetR();
      label->GetWindow()->HideWindow();
      label->Release();
    }
    {
      ScString emptyDetail;
      CtInitAddLabel(window, kCtFileAuthorValue, labelRight + 2, 318, emptyDetail, 1);
    }
    WnWindow *detail = window->GetChildWindowFromID(kCtFileAuthorValue);
    if (detail) {
      i32 height = detail->GetH(), left = detail->GetL();
      detail->SetArea(left, 318, 416, height + 318);
    }
    if (image) {
      image->AddRef();
      image->Width();
      image->Height();
      WnOutlineControl *outline = nullptr;
      if (CtGetInitDirector()->GZCOM()->GetClassObject(
              kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)) &&
          outline) {
        outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
        ScRect area{0, 0, 164, 154};
        outline->GetWindow()->SetArea(area);
        outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
        outline->GetWindow()->GZWinMoveTo(11, 69);
        window->ChildAdd(outline->GetWindow());
        outline->Release();
      }
      WnBitmap *bitmap = new WnBitmap;
      bitmap->AddRef();
      bitmap->SetID(kCtFilePreviewBitmap);
      bitmap->SetArea(0, 0, 162, 152);
      bitmap->SetImage(image);
      if (!window->ChildAdd(bitmap->GetWindow())) {
        CtGetInitWindowManager()->DestroyWindow(bitmap->GetWindow());
        bitmap->Release();
        return 0;
      }
      bitmap->GetWindow()->GZWinMoveTo(12, 70);
      bitmap->Release();
    }
    WnFlatControl *background = nullptr;
    if (CtGetInitDirector()->GZCOM()->GetClassObject(
            kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&background))) {
      WnWindow *backgroundWindow = background->GetWindow();
      backgroundWindow->SetID(445);
      backgroundWindow->SetFillColor(CtGetInitWindowResources()->GetColor(kWnColorUiBackground));
      background->SetOutlineFlags(32);
      ScRect area{0, 0, 164, 110};
      backgroundWindow->SetArea(area);
      if (window->ChildAdd(backgroundWindow)) {
        backgroundWindow->GZWinMoveTo(11, 231);
        backgroundWindow->SendToBack();
        backgroundWindow->SetFlag(kWnFlagReorderable, false);
      } else {
        CtGetInitWindowManager()->DestroyWindow(backgroundWindow);
      }
      background->Release();
    }
    WnEditorControl *editor = nullptr;
    if (CtGetInitDirector()->GZCOM()->GetClassObject(
            kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
      WnWindow *editorWindow = editor->AsIGZWin();
      editorWindow->SetID(444);
      editor->SetFont(CtGetInitWindowResources()->GetFont(kWnFontUiMain));
      editor->SetFontColor(CtGetInitWindowResources()->GetColor(kWnThemeTextColorId));
      editor->SetTextGutterX(5);
      editor->SetTextGutterY(5);
      editorWindow->SetSize(162, 108);
      editor->SetFlag(2);
      editor->SetFlag(8);
      editor->ClearFlag(16);
      editor->ClearFlag(1);
      editor->ClearFlag(32);
      if (!window->ChildAdd(editorWindow))
        editorWindow->Shutdown();
      editorWindow->GZWinMoveTo(12, 232);
      editor->Release();
    }
    if (image)
      image->Release();
  }
  if ((mode == kCtFileOpenCity || mode == kCtFileOpenStarterTown || mode == kCtFileOpenTerrain ||
          mode == kCtFileOpenScenario || mode == kCtFileImportReplacementSet) &&
      flags[2] == 1) {
    fifth.Erase(0, fifth.Strlen());
    Populate(window);
  }
  if (mode == kCtFileSaveCity) {
    CtCity *city = CtGetInitCity();
    if (city) {
      i32 population = 0;
      CtResidentialLayer *residential = city->ResidentialLayer();
      if (residential)
        population = residential->GetPopulation();
      i64 funds = 0;
      EcBudgetLayer *budget = city->BudgetLayer();
      if (budget)
        funds = budget->GetTotalFunds();
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      CtDate date(month, day, year);
      CtInitUpdateCityStats(window, population, funds, &date);
    }
    WnWindow *fileWindow = window->GetChildWindowFromID(kCtFileNameEdit);
    WnTextControl *fileControl = nullptr;
    if (fileWindow && fileWindow->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&fileControl))) {
      if (city) {
        ScString filename(*city->GetFilePath());
        if (filename.Strlen()) {
          ScPath path(filename);
          filename = path.FileName();
        } else {
          filename = seventh;
        }
        fileControl->SetText(&filename);
      }
      fileControl->Release();
    }
  }
  window->SetSize(470, 310);
  WnWindow *heightParent = window->GetMainWindow(), *widthParent = window->GetMainWindow();
  i32 parentHeight = heightParent->GetH(), height = window->GetH();
  i32 parentWidth = widthParent->GetW(), width = window->GetW();
  window->GZWinMoveTo((parentWidth - width) / 2, (parentHeight - height) / 2);
  list->SetFocus();
  list->Release();
  if (mode != kCtFileSaveCity)
    CtGetInitBox()->Event(kAuEventBeginFileSelection, 0, 0, 0, 0);
  return 1;
}

// Return a path without its drive prefix: "C:\\dir" loses "C:", while a POSIX root loses only its leading separator.
static ScString CtPathAfterDrive(const ScString &path) {
#if !defined(_WIN32)
  return path.Mid(1, kScStringToEnd);
#else
  return path.Mid(2, kScStringToEnd);
#endif
}

// Rebuild a full path from a drive row and the remainder: "C" + ":" + "\\dir" on Windows, "/" + "dir" on POSIX roots.
static ScString CtJoinDrive(const ScString &drive, const ScString &rest) {
  ScString joined(drive);
#if defined(_WIN32)
  joined.Append(":");
#endif
  joined.Append(rest.ToChar());
  return joined;
}

// Select the remembered city directory, populate its drive selector, and refresh the resulting directory.
// If the remembered drive is unavailable, try the resource directory before falling back to the first drive.
u8 CtInitFileDialog::BuildDriveCombo(WnWindow *window, FtFont *font) {
  ScString remembered(*CtGetInitPreferences()->GetLoadSavePath());
  if ((mode == kCtFileOpenCity || mode == kCtFileSaveCity) && remembered.Strlen()) {
    fourth = remembered;
  } else {
    ScString directory(CtGetInitFileSystem()->AppDirectory());
    u32 previous = 0, last = 0;
    for (u32 i = 0; i < directory.Strlen(); ++i) {
      if (directory.Data()[i] == '\\') {
        previous = last;
        last = i;
      }
    }
    if (previous)
      directory = directory.Left(previous + 1);
    directory.Append("Cities\\");
    if (mode == kCtFileOpenStarterTown)
      directory.Append("StarterTowns\\");
    else if (mode == kCtFileOpenTerrain)
      directory.Append("Terrains\\");
    else if (mode == kCtFileOpenScenario)
      directory.Append("Scenarios\\");
    fourth = directory;
  }
  ScPath path(fourth);
  if (!path.Exists())
    fourth.FromChar(CtGetInitFileSystem()->AppDirectory());
  ScString drive = fourth.Left(1);
  first = CtPathAfterDrive(fourth);
  WnCombo *combo = new WnCombo(0);
  combo->AddRef();
  combo->SetFont(font);
  combo->SetID(kCtFileDirectoryCombo);
  combo->SetFillColor(CtGetInitGraphics()->ConvertRGBValueToNative16Bit(255, 255, 255));
  combo->SetHighlightColor(CtGetInitGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
  combo->SetArea(252, 40, 466, 59);
  std::list<ScString> names;
  std::list<RsDirectoryListing::DriveType> types;
  RsDirectoryListing::ReadAvailableDrivesIntoStringList(&names, &types);
  auto type = types.begin();
  i32 index = 0;
  for (const ScString &name : names) {
    char icon = 14;
    if (*type == RsDirectoryListing::DriveType::Removable)
      icon = 15;
    else if (*type == RsDirectoryListing::DriveType::Cdrom)
      icon = 17;
    ScString label(&icon, 1);
    label.Append(" ");
    ScString root = name.Left(2);
    label.Append(root.ToChar());
    combo->InsertString(label, index++);
    ++type;
  }
  if (!window->ChildAdd(combo->GetWindow())) {
    CtGetInitWindowManager()->DestroyWindow(combo->GetWindow());
    combo->Release();
    return 0;
  }
  bool found = false;
  ScString candidate, selected;
  for (u32 attempt = 0; attempt < 2 && !found; ++attempt) {
    if (!attempt) {
      candidate = drive;
    } else {
      const char *appDirectory = CtGetInitFileSystem()->AppDirectory();
      if (appDirectory)
        candidate.FromChar(appDirectory);
      candidate.Append("Res\\");
    }
    index = 0;
    while (combo->GetStringFromIndex(index, &selected)) {
      ScString selectedDrive = selected.Mid(1, kScStringToEnd);
      selectedDrive.LTrim();
      selectedDrive = selectedDrive.Left(1);
      int differs = ScCompareNoCase(candidate.ToChar(), selectedDrive.ToChar(), 1);
      if (!differs) {
        combo->SetSelection(index, true);
        fourth = CtJoinDrive(candidate, first);
        found = true;
        break;
      }
      ++index;
    }
  }
  if (!found) {
    combo->SetSelection(0, true);
    combo->GetStringFromIndex(0, &selected);
    selected = selected.Mid(1, kScStringToEnd);
    selected.LTrim();
    fourth = selected;
  }
  combo->Release();
  RefreshDirectory(window);
  return 1;
}

// Format the preview's population, funds and date with the current language utility and update their labels.
void SC3_STDCALL CtInitUpdateCityStats(WnWindow *window, i32 population, i64 funds, const CtDate *date) {
  ScString format, formatted, local;
  ScLanguageUtility *language = CtGetInitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return;
  WnWindow *child = window->GetChildWindowFromID(kCtFilePopulationLabel);
  WnLabelControl *label = nullptr;
  if (child && child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    format = CtInitText(kRsGamePopulationFormatText, kRsGameTextGroup);
    language->MakeNumberString(static_cast<i64>(population), &local);
    formatted.Sprintf(format.ToChar(), local.ToChar());
    label->SetCaption(formatted);
    label->Fit();
    label->Release();
    label = nullptr;
  }
  child = window->GetChildWindowFromID(kCtFileFundsLabel);
  if (child && child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    format = CtInitText(kRsGameFundsFormatText, kRsGameTextGroup);
    language->MakeMoneyString(funds, &local);
    formatted.Sprintf(format.ToChar(), local.ToChar());
    label->SetCaption(formatted);
    label->Fit();
    label->Release();
    label = nullptr;
  }
  child = window->GetChildWindowFromID(kCtFileDateLabel);
  if (child && child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    format = CtInitText(kRsGameDateFormatText, kRsGameTextGroup);
    u32 year = date->Year(), day = date->DayOfMonth(), month = date->Month();
    language->MakeDateString(month, day, year, &local, ScLanguageUtility::DateFormat::NumericMonthDayYear);
    formatted.Sprintf(format.ToChar(), local.ToChar());
    label->SetCaption(formatted);
    label->Fit();
    label->Release();
  }
}

// Populate the mayor and city labels from localized prefixes and the supplied preview metadata.
void CtInitFileDialog::UpdateCityLabels(WnWindow *window, const ScString *mayor, const ScString *city) {
  WnWindow *child = window->GetChildWindowFromID(kCtFileMayorLabel);
  WnLabelControl *label = nullptr;
  if (child && child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    ScString format = CtInitText(kRsGameStringFormatText, kRsGameTextGroup);
    ScString name;
    name.Sprintf(format.ToChar(), mayor->Data());
    ScString caption = CtInitText(kRsGameMayorNameText, kRsGameTextGroup);
    caption.Append(" ");
    caption.Append(name.Data(), name.Strlen());
    if (mode == kCtFileOpenCity)
      CtInitText::FormatForEllipsis(&caption, 360, CtGetInitWindowResources()->GetFont(kWnFontUiSmall));
    label->SetCaption(caption);
    label->Fit();
    label->Release();
    label = nullptr;
  }
  child = window->GetChildWindowFromID(kCtFileCityLabel);
  if (child && child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    ScString caption = CtInitText(kRsGameCityNameText, kRsGameTextGroup);
    caption.Append(" ");
    caption.Append(city->Data(), city->Strlen());
    if (mode == kCtFileOpenCity)
      CtInitText::FormatForEllipsis(&caption, 360, CtGetInitWindowResources()->GetFont(kWnFontUiSmall));
    label->SetCaption(caption);
    label->Fit();
    label->Release();
  }
}

static bool SC3_STDCALL CtInitReadScenarioInfo(
    const ScStringView *path, ScString *description, ScString *title, GxBuffer **preview) {
  if (!path->Strlen() || !RsFile::FileExists(*path))
    return false;
  CtCityInfo info(*path, 3, 162, 152);
  info.GetPreview(preview);
  *description = info.field84;
  *title = info.field104;
  return true;
}

// Read the selected city, terrain or starter-town metadata through its original descriptor or SC2 importer.
// Fields unavailable in an SC2 file are left as supplied by the caller.
void CtInitFileDialog::ReadCityInfo(const ScString &path, i32 *population, i64 *funds, CtDate *date,
    ScString *mayor, ScString *city, GxBuffer **preview) {
  ScPath parsed(path);
  if (mode == kCtFileOpenCity || mode == kCtFileImportReplacementSet) {
    const ScString &extension = parsed.Extension();
    int cityFile = ScCompareNoCase(extension.ToChar(), ".sc3"), legacyFile = ScCompareNoCase(extension.ToChar(), ".sc2");
    if (!cityFile) {
      CtCityInfo info(path, 0, 162, 152);
      *population = static_cast<i32>(info.population);
      *funds = info.funds;
      *date = info.date;
      *mayor = info.mayorName;
      *city = info.cityName;
      info.GetPreview(preview);
    } else if (!legacyFile) {
      ScImporter *importer = nullptr;
      if (CtGetInitDirector()->GZCOM()->GetClassObject(
              kScImporterClassId, IID_ScImporter, reinterpret_cast<void **>(&importer)) &&
          importer->Init(path)) {
        char name[32];
        importer->GetCityName(name);
        u32 length = 0;
        while (length < sizeof(name) && name[length])
          ++length;
        city->FromChar(name, length);
        i32 year, elapsed, value, firstPopulation, secondPopulation;
        if (importer->GetMiscValue(3, &year) && importer->GetMiscValue(4, &elapsed))
          date->Set(static_cast<u32>(elapsed / 25 % 12 + 1), static_cast<u32>(elapsed % 25 + 1),
              static_cast<u32>(year + elapsed / 300));
        if (importer->GetMiscValue(5, &value))
          *funds = value;
        if (importer->GetMiscValue(1032, &firstPopulation) && importer->GetMiscValue(1035, &secondPopulation))
          *population = static_cast<i32>(static_cast<u32>(firstPopulation) + static_cast<u32>(secondPopulation));
      }
      if (importer)
        importer->Release();
    }
  } else if (mode == kCtFileOpenTerrain || mode == kCtFileOpenStarterTown) {
    CtCityInfo info(path, mode == kCtFileOpenTerrain ? 2 : 1, 162, 152);
    info.GetPreview(preview);
  }
}

// Leave an already displayed selection untouched, avoiding duplicate city metadata and preview work.
// Display the selected city or scenario metadata and update the appropriate preview control.
void CtInitFileDialog::UpdatePreview(WnWindow *window) {
  u32 length = output.Strlen();
  if (length == fifth.Strlen() && memcmp(output.Data(), fifth.Data(), length) == 0)
    return;
  CtInitCursorScope cursor(14);
  CtDate date;
  ScString mayor, city;
  ScString description, name;
  GxBuffer *preview = nullptr;
  if (mode == kCtFileOpenScenario) {
    CtInitReadScenarioInfo(&output, &description, &name, &preview);
    if (preview) {
      WnBitmapControl *bitmap = nullptr;
      if (window->GetChildAs(kCtFilePreviewBitmap, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)) && bitmap) {
        bitmap->SetImage(preview);
        bitmap->Release();
      }
    }
    WnBitmapControl *bitmap = nullptr;
    if (window->GetChildAs(kCtFilePreviewBitmap, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)) && bitmap) {
      if (preview) {
        bitmap->SetImage(preview);
        bitmap->Release();
      } else {
        GxColorType format(GxColorType::Color::RGB16);
        CtInitImageResource resource(
            kCtFileStarterTownPreviewPlaceholderImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, format);
        GxBuffer *image = resource.Value();
        if (image) {
          image->AddRef();
          bitmap->SetImage(image);
          bitmap->Release();
        }
      }
    }
    WnEditorControl *editor = nullptr;
    if (window->GetChildAs(444, IID_WnEditorControl, reinterpret_cast<void **>(&editor)) && editor) {
      editor->SetText(description);
      editor->ScrollToStart();
      editor->Release();
    }
    WnWindow *detail = window->GetChildWindowFromID(kCtFileAuthorValue);
    WnWindow *title = window->GetChildWindowFromID(kCtFileAuthorLabel);
    if (name.Strlen()) {
      if (detail) {
        detail->SetCaption(name);
        detail->ShowWindow();
      }
      if (title)
        title->ShowWindow();
    } else {
      if (detail)
        detail->HideWindow();
      if (title)
        title->HideWindow();
    }
    if (preview)
      preview->Release();
    fifth = output;
    return;
  }
  i32 population = 0;
  i64 funds = 0;
  ReadCityInfo(output, &population, &funds, &date, &mayor, &city, &preview);
  if (mode != kCtFileOpenTerrain) {
    CtInitUpdateCityStats(window, population, funds, &date);
    UpdateCityLabels(window, &mayor, &city);
  }
  if (mode == kCtFileOpenStarterTown || preview) {
    WnBitmapControl *bitmap = nullptr;
    if (window->GetChildAs(kCtFilePreviewBitmap, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)) && bitmap) {
      bitmap->SetImage(preview);
      bitmap->Release();
    }
  }
  if (mode == kCtFileOpenCity || mode == kCtFileOpenTerrain || mode == kCtFileImportReplacementSet) {
    WnWindow *mapWindow = window->GetChildWindowFromID(kCtFileMapPreview);
    UiMapControl *map = nullptr;
    if (mapWindow && mapWindow->QueryInterface(IID_UiMapControl, reinterpret_cast<void **>(&map))) {
      WnWindow *placeholder = window->GetChildWindowFromID(kCtFilePreviewPlaceholder);
      if (preview) {
        map->ReleaseFilter(kCtFileMapPreviewFilterId);
        map->AddFilter(kCtFileMapPreviewFilterId, preview);
        mapWindow->ShowWindow();
        placeholder->HideWindow();
      } else {
        mapWindow->HideWindow();
        placeholder->ShowWindow();
      }
      map->Release();
    }
  }
  if (preview)
    preview->Release();
  fifth = output;
}

// Rebuild the drive hierarchy and sorted directory/file rows using the selected mode's original filters. The original
// leaves the reentry flag set when either initial interface query fails.
u8 CtInitFileDialog::RefreshDirectory(WnWindow *window) {
  if (flags[0])
    return 0;
  flags[0] = 1;
  WnWindow *child = window->GetChildWindowFromID(kCtFileDirectoryCombo);
  WnComboControl *combo = nullptr;
  if (!child->QueryInterface(IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  combo->Release();
  child = window->GetChildWindowFromID(kCtFileList);
  WnListControl *list = nullptr;
  if (!child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  list->Release();
  FtFont *font = CtGetInitWindowResources()->GetFont(kWnFontUiFileDialog);
  second.Erase(0, second.Strlen());
  directoryListing = new RsDirectoryListing(fourth, true, false, false);
  if (mode != kCtFileOpenTerrain && mode != kCtFileOpenStarterTown && mode != kCtFileOpenScenario)
    CtGetInitPreferences()->SetLoadSavePath(&fourth);
  child = window->GetChildWindowFromID(kCtFileDirectoryCombo);
  combo = nullptr;
  if (child && child->QueryInterface(IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
    ScString text;
    i32 index = 0;
    while (combo->GetStringFromIndex(index, &text) == 1) {
      if (text.Strlen() && text.Data()[0] == ' ')
        combo->RemoveString(index);
      else
        ++index;
    }
    index = 0;
    while (combo->GetStringFromIndex(index, &text) == 1) {
      ScString drive = text.Mid(1, kScStringToEnd);
      drive.LTrim();
      ScString selectedDrive = fourth.Left(1);
      int differs = ScCompareNoCase(drive.Data(), selectedDrive.Data(), 1);
      if (!differs) {
        ScPath path(fourth);
        ScString directory(path.Directory());
        u32 depth = 0;
        while (directory.Strlen()) {
          u32 length = static_cast<u32>(strcspn(directory.Data(), "\\/"));
          ScString component = directory.Left(length), label;
          directory.Erase(0, length + (length < directory.Strlen()));
          // Leading or doubled separators (POSIX roots) yield empty components that must not become hierarchy rows.
          if (!length)
            continue;
          ++depth;
          for (u32 i = 0; i < depth; ++i)
            label.Append("     ");
          const char icon = 6;
          label.Append(&icon, 1);
          label.Append(" ");
          label.Append(component.Data(), component.Strlen());
          ++index;
          combo->InsertString(label, index);
        }
        combo->SetSelection(index, true);
        break;
      }
      ++index;
    }
    combo->Release();
  }
  list->RemoveAllStrings();
  directories.clear();
  ScString filter("*.*");
  directoryListing->SetNewDirectoryEntryFilter(filter, true);
  std::list<ScString> names;
  directoryListing->ReadDirectoryDirectoryEntriesIntoStringList(&names, 0);
  auto less = [](const ScString &left, const ScString &right) {
    u32 leftLength = left.Strlen(), rightLength = right.Strlen();
    int order = memcmp(left.Data(), right.Data(), leftLength < rightLength ? leftLength : rightLength);
    return order < 0 || (!order && leftLength < rightLength);
  };
  names.sort(less);
  ScPath current(fourth);
  i32 index = 0, firstFile = 0;
  if (current.Directory().Strlen() > 1) {
    const char icon = 12;
    ScString row(&icon, 1);
    row.Append(" ");
    row.Append(third.Data(), third.Strlen());
    directories.push_back(row);
    CtInitText::FormatForEllipsis(&row, 188, font);
    list->InsertString(row, index++);
  }
  for (const ScString &name : names) {
    const char icon = 6;
    ScString row(&icon, 1);
    row.Append(" ");
    row.Append(name.ToChar());
    directories.push_back(row);
    CtInitText::FormatForEllipsis(&row, 188, font);
    list->InsertString(row, index++);
  }
  std::list<ScString> files;
  if (mode == kCtFileOpenCity || mode == kCtFileSaveCity || mode == kCtFileImportReplacementSet) {
    filter = CtInitText(127, kRsCommonTextGroup);
    directoryListing->SetNewDirectoryEntryFilter(filter, true);
    if (directoryListing->Size())
      directoryListing->ReadDirectoryFileEntriesIntoStringList(&files);
    filter = CtInitText(125, kRsCommonTextGroup);
    directoryListing->SetNewDirectoryEntryFilter(filter, true);
    if (directoryListing->Size())
      directoryListing->ReadDirectoryFileEntriesIntoStringList(&files);
  } else {
    const char *pattern = nullptr;
    if (mode == kCtFileOpenScenario)
      pattern = "*.SNR";
    else if (mode == kCtFileOpenTerrain)
      pattern = "*.SCT";
    else if (mode == kCtFileOpenStarterTown)
      pattern = "*.ST3";
    if (pattern) {
      filter.FromChar(pattern);
      directoryListing->SetNewDirectoryEntryFilter(filter, true);
      if (directoryListing->Size())
        directoryListing->ReadDirectoryFileEntriesIntoStringList(&files);
    }
  }
  files.sort(less);
  for (const ScString &name : files) {
    ScPath file(name);
    ScString extension(file.Extension());
    if (extension.Strlen() && extension.Data()[0] == '.')
      extension.Erase(0, 1);
    i32 kind = FindKind(&extension);
    ScString row;
    if (kind) {
      char icon = static_cast<char>(kind);
      row.Append(&icon, 1);
      row.Append(" ");
    }
    ScString &stem = file.FileName();
    row.Append(stem.Data(), stem.Strlen());
    directories.push_back(row);
    CtInitText::FormatForEllipsis(&row, 188, font);
    list->InsertString(row, index);
    if (!firstFile)
      firstFile = index;
    ++index;
  }
  list->SetSelection(firstFile, true);
  delete directoryListing;
  directoryListing = nullptr;
  flags[0] = 0;
  return 1;
}

// Create a fixed image button, move its window and retain the button in the dialog's entry list.
// On child attachment failure, ask the parent window manager to destroy the new window.
bool CtInitFileDialog::AddImageButton(WnWindow *parent, u32 identifier, i32 x, i32 y, u32 image, i8 key) {
  GxBuffer *buffer;
  {
    GxColorType format(GxColorType::Color::RGB16);
    CtInitImageResource resource(image, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    buffer = resource.Value();
  }
  if (!buffer)
    return false;
  WnButton *button;
  {
    ScString caption;
    WnResources *resources = CtGetInitWindowResources();
    const ScRect *area = buffer->GetBufferArea();
    button = resources->CreateFixedButton(identifier, caption, buffer, area, static_cast<u32>(static_cast<i32>(key)));
  }
  if (!button)
    return false;
  WnWindow *window = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  window->GZWinMoveTo(x, y);
  if (!parent->ChildAdd(window)) {
    parent->GetWindowManager()->DestroyWindow(window);
    return false;
  }
  entries.push_back(button);
  return true;
}

bool CtInitFileDialog::Close(WnWindow *) {
  for (ScUnknown *&entry : entries) {
    ScRelease(&entry);
  }
  entries.clear();
  return true;
}

// Set the city's file-dialog minimum and maximum dimensions when an output is supplied.
bool CtInitFileDialog::GetLimits(WnWindow *, ScRect *limits) {
  if (!limits)
    return false;
  *limits = {470, 310, 470, 310};
  return true;
}

// Hide and repaint the completed dialog before posting its modal result to the window manager.
// Cancellation reports the original BoxX event except in the designated save mode.
u8 CtInitFileDialog::Finish(WnWindow *window, u32 result) {
  if (result == kCtFileCancelButton && mode != kCtFileSaveCity)
    CtGetInitBox()->Event(kAuEventCancelFileSelection, 0, 0, 0, 0);
  window->HideWindow();
  CtGetInitWindowManager()->Plot();
  CtGetInitGraphics()->Draw();
  return CtGetInitWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
}

// Handle confirmation, the city-preview checkbox and the original cancel command.
// Filename validation uses the installed language-specific search and displays its original error text on failure.
u8 CtInitFileDialog::Commit(WnWindow *window, u32 identifier) {
  if (identifier == kUiFrameCloseButtonId) {
    CtGetInitBox()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    Finish(window, kCtFileCancelButton);
    return 1;
  }
  if (identifier == kCtFileShowStatistics) {
    WnWindow *child = window->GetChildWindowFromID(kCtFileShowStatistics);
    WnButton *button = nullptr;
    if (child && child->QueryInterface(IID_WnButton, reinterpret_cast<void **>(&button))) {
      flags[2] = button->IsChecked();
      button->Release();
      if (flags[2] == 1)
        UpdatePreview(window);
      for (u32 id = kCtFilePopulationLabel; id <= kCtFileCityLabel; ++id) {
        child = window->GetChildWindowFromID(id);
        if (child)
          child->SetFlag(kWnFlagVisible, flags[2] != 0);
      }
      child = window->GetChildWindowFromID(kCtFileMapPreview);
      if (child) {
        if (flags[2] == 1)
          child->ShowWindow();
        else
          child->HideWindow();
      }
      child = window->GetChildWindowFromID(kCtFilePreviewPlaceholder);
      if (child) {
        if (flags[2] == 1)
          child->HideWindow();
        else
          child->ShowWindow();
      }
    }
    return 1;
  }
  if (identifier != kCtFileAcceptButton)
    return 0;
  CtGetInitBox()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  bool valid = true;
  const char *sets[2] = {"<>:\"/\\|", "*?"};
  for (u32 i = 0; i < 2; ++i) {
    u32 language = CtGetInitLanguageManager()->GetCurrentLanguage();
    bool multibyte = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
        language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
    const char *invalid = multibyte ? CtInitFindInvalidFilename(eighth.begin, sets[i]) : strpbrk(eighth.begin, sets[i]);
    if (invalid)
      valid = false;
  }
  if (CtInitIsBlankFileName(eighth.begin))
    valid = false;
  if (!valid) {
    ScString title = CtInitText(kRsMessageErrorMessageText, kRsMessageTextGroup);
    ScString format = CtInitText(kRsMessageInvalidFileNameFormatText, kRsMessageTextGroup);
    ScString message;
    message.Sprintf(format.ToChar(), eighth.begin);
    CtGetInitWindowManager()->GZMsgBox(message, title, 0x40000, true, 0);
    return 1;
  }
  if (mode == kCtFileSaveCity) {
    CtCity *city = CtGetInitCity();
    if (city) {
      city->SetMayorName(sixth);
      city->SetCityName(seventh);
    }
  }
  WnButton *button = nullptr;
  if (window->GetChildAs(kCtFileShowStatistics, IID_WnButton, reinterpret_cast<void **>(&button))) {
    ApPreferences *preferences = CtGetInitPreferences();
    u8 checked = button->IsChecked();
    reinterpret_cast<u8 *>(preferences->LoadPrefs())[1] = checked;
    button->Release();
    preferences = CtGetInitPreferences();
    reinterpret_cast<u8 *>(preferences->LoadPrefs())[0] = 1;
  }
  Finish(window, kCtFileAcceptButton);
  return 1;
}

// Rebuild the selected directory from combo entries, stripping their type byte and leading spaces.
// The reentrancy flag remains set when the combo cannot be acquired, matching the installed early return.
u8 CtInitFileDialog::UpdateDirectoryFromCombo(WnWindow *window) {
  if (flags[1])
    return 0;
  flags[1] = 1;
  WnWindow *child = window->GetChildWindowFromID(kCtFileDirectoryCombo);
  WnComboControl *combo = nullptr;
  if (!child || !child->QueryInterface(IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  ScString selected, path;
  i32 index = combo->GetSelection();
  i8 kind;
  do {
    combo->GetStringFromIndex(index, &selected);
    u32 length = selected.Strlen(), position = 0;
    while (position < length && selected.begin[position] == ' ')
      ++position;
    kind = position < length ? static_cast<i8>(selected.begin[position]) : 0;
    u32 start = position < length ? position + 1 : length;
    while (start < length && selected.begin[start] == ' ')
      ++start;
    ScString part;
    if (start < length)
      part.FromChar(selected.begin + start, length - start);
    if (path.Strlen()) {
      part.Append("\\");
      part.Append(path.Data(), path.Strlen());
      path = part;
    } else {
      path.Append(part.Data(), part.Strlen());
    }
    --index;
  } while (kind == 6);
  if (path.Strlen() && path.begin[path.Strlen() - 1] != '\\')
    path.Append("\\");
  fourth = path;
  RefreshDirectory(window);
  flags[1] = 0;
  combo->Release();
  return 1;
}

// Navigate the selected directory entry or copy a selected filename into the input control.
// Rebuild the output path and preview when appropriate, then return focus to the file list.
u8 CtInitFileDialog::Populate(WnWindow *window) {
  ScString selected, filename;
  if (flags[1])
    return 0;
  flags[1] = 1;
  WnWindow *child = window->GetChildWindowFromID(kCtFileList);
  WnListControl *list = nullptr;
  if (!child || !child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  currentDirectory = directories.begin();
  for (i32 index = 0; index <= list->GetSelection(); ++index) {
    selected = *currentDirectory;
    ++currentDirectory;
  }
  list->Release();
  child = window->GetChildWindowFromID(kCtFileNameEdit);
  WnTextControl *edit = nullptr;
  if (!child || !child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&edit)))
    return 0;
  edit->GetText(&filename);
  edit->Release();
  i8 kind = static_cast<i8>(selected.begin[0]);
  if (kind == 6 || kind == 12) {
    if (kind == 6) {
      u32 length = selected.Strlen();
      second.FromChar(length > 1 ? selected.begin + 1 : "", length > 1 ? length - 1 : 0);
      u32 spaces = 0;
      while (spaces < second.Strlen() && second.begin[spaces] == ' ')
        ++spaces;
      if (spaces)
        second.Erase(0, spaces);
      second.Append("\\");
      first.Append("\\");
      first.Append(second.ToChar());
      fourth.Append(second.ToChar());
    } else if (fourth.Strlen() > 1) {
      fourth.Erase(fourth.Strlen() - 1, 1);
      while (fourth.Strlen() && fourth.begin[fourth.Strlen() - 1] != '\\')
        fourth.Erase(fourth.Strlen() - 1, 1);
    }
    RefreshDirectory(window);
    if (mode != kCtFileSaveCity) {
      child = window->GetChildWindowFromID(kCtFileList);
      list = nullptr;
      if (child && child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list))) {
        i32 selectedIndex = list->GetSelection();
        list->GetStringFromIndex(selectedIndex, &selected);
        list->Release();
        kind = static_cast<i8>(selected.begin[0]);
        if (kind == 7 || kind == 16 || kind == 18 || kind == 19 || kind == 30) {
          u32 length = selected.Strlen();
          filename.FromChar(length > 1 ? selected.begin + 1 : "", length > 1 ? length - 1 : 0);
          u32 spaces = 0;
          while (spaces < filename.Strlen() && filename.begin[spaces] == ' ')
            ++spaces;
          if (spaces)
            filename.Erase(0, spaces);
        } else {
          filename.Erase(0, filename.Strlen());
        }
      }
    }
  } else if (kind == 7 || kind == 16 || kind == 18 || kind == 19 || kind == 30) {
    u32 length = selected.Strlen();
    filename.FromChar(length > 1 ? selected.begin + 1 : "", length > 1 ? length - 1 : 0);
    u32 spaces = 0;
    while (spaces < filename.Strlen() && filename.begin[spaces] == ' ')
      ++spaces;
    if (spaces)
      filename.Erase(0, spaces);
  } else {
    filename.Erase(0, filename.Strlen());
  }
  edit->SetText(&filename);
  CtInitSetChildEnabled(window, kCtFileAcceptButton, filename.Strlen() != 0);
  if (filename.Strlen()) {
    output = fourth;
    output.Append(filename.Data(), filename.Strlen());
    ScString extension;
    if (FindName(kind, &extension)) {
      output.Append(".");
      output.Append(extension.Data(), extension.Strlen());
    }
    if (mode == kCtFileOpenTerrain || mode == kCtFileOpenStarterTown || mode == kCtFileOpenScenario ||
        mode == kCtFileImportReplacementSet || (mode == kCtFileOpenCity && flags[2] == 1))
      UpdatePreview(window);
  }
  child = window->GetChildWindowFromID(kCtFileList);
  if (child)
    CtGetInitWindowManager()->SetFocus(child);
  flags[1] = 0;
  return 1;
}

// Lowercase the supplied concrete name, then return the matching selection's signed kind character or zero.
i32 CtInitFileDialog::FindKind(ScString *name) {
  name->MakeLower();
  u32 length = name->ScString::Strlen();
  for (const CtInitFilePair &selection : selections) {
    ScString candidate(selection.first);
    if (length == candidate.ScString::Strlen() && !memcmp(name->begin, candidate.begin, length))
      return static_cast<i8>(selection.second.begin[0]);
  }
  return 0;
}

// Copy the first selection whose kind character equals the supplied signed integer.
bool CtInitFileDialog::FindName(i32 kind, ScString *out) {
  for (const CtInitFilePair &selection : selections) {
    if (kind == static_cast<i8>(selection.second.begin[0])) {
      *out = selection.first;
      return true;
    }
  }
  return false;
}
