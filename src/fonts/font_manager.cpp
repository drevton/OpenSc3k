#include <cstdlib>
#include <cstring>
#include <new>

#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/services_module.h"
#include "fonts/api.h"
#include "fonts/multibyte_font.h"
#include "fonts/sc3_font_manager.h"
#include "graphics/sc3_graphics.h"

// The service returns its original fixed framework scheduling priority.
enum FtSystemServiceOrder : i32 {
  kFtSystemServicePriority = 0xdbba0,
};

// Borrow the module's cached graphics service, publishing a successful query before releasing its temporary reference.
GxGraphics *FtGetGraphics() {
  static GxGraphics *graphics;
  if (!graphics) {
    GxGraphics *value = nullptr;
    ScServicesFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&value));
    graphics = value;
    if (value)
      value->Release();
  }
  return graphics;
}

// Borrow the module's cached font system, trying the preferred service before the original fallback service.
FtFontSystem *FtGetFontSystem() {
  static FtFontSystem *fonts;
  if (!fonts) {
    FtFontSystem *preferred = nullptr;
    ScServicesFramework()->GetSystemService(
        kFtAppFontServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&preferred));
    if (preferred)
      fonts = preferred;
    else {
      FtFontSystem *fallback = nullptr;
      ScServicesFramework()->GetSystemService(
          kFtFontSystemServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&fallback));
      fonts = fallback;
      if (fallback)
        fallback->Release();
    }
    if (preferred)
      preferred->Release();
  }
  return fonts;
}

// Borrow the module's cached language manager, publishing a successful query before releasing its temporary reference.
ScLanguageManager *FtGetLanguageManager() {
  static ScLanguageManager *manager;
  if (!manager) {
    ScLanguageManager *value = nullptr;
    ScServicesFramework()->GetSystemService(
        kScLanguageManagerServiceId, IID_ScLanguageManager, reinterpret_cast<void **>(&value));
    manager = value;
    if (value)
      value->Release();
  }
  return manager;
}

u32 FtLocale = 1;

FtManager::FtManager() : count(0), initialized(false) {
  head.next = head.previous = &head;
  head.font = nullptr;
}

// Delete only list storage; original Close is the operation that invokes font release methods.
FtManager::~FtManager() {
  while (head.next != &head) {
    FtNode *node = head.next;
    head.next = node->next;
    delete node;
  }
}

// Insert a borrowed font at the front of the actual cache list, without adding a second ownership reference.
void FtManager::Insert(FtFont *font) {
  FtNode *node = new FtNode;
  node->font = font;
  node->previous = &head;
  node->next = head.next;
  node->next->previous = node;
  head.next = node;
  ++count;
}

// Apply the source locale and exact case-specific filename substring tests for the alternate font class.
static int FtAlternate(u32 locale, const ScStringView &path) {
  if (locale != 15 && locale != 17 && locale != 18 && locale != 19 && locale != 20)
    return 0;
  const char *bytes = path.ToChar();
  u32 size = path.Strlen();
  for (u32 index = 0; size >= 4 && index <= size - 4; ++index)
    if (!memcmp(bytes + index, ".fon", 4) || !memcmp(bytes + index, ".FON", 4))
      return 1;
  return 0;
}

// Return a retained matching font or initialize and insert a new locale-specific file font.
// Failed initialization uses raw deallocation, as in the original font-system acquisition path.
FtFont *FtManager::Load(ScStringView *path) {
  ScString temporary;
  for (FtNode *node = head.next; node != &head; node = node->next) {
    FtFont *font = node->font;
    if (font->GetFontFilePath(&temporary) && !ScCompareNoCase(temporary.ToChar(), path->ToChar())) {
      font->AddRef();
      return font;
    }
  }
  u32 locale = FtLocale;
  ScString copy(*path);
  FtFont *font;
  if (FtAlternate(locale, copy))
    font = new FtMultiFont(copy, true);
  else
    font = new FtBitmapFont(&copy, 1);
  if (font->Initialize()) {
    Insert(font);
    font->AddRef();
    path->ToChar();
  } else {
    ::operator delete(font);
    font = nullptr;
  }
  return font;
}

// Release matching entries until one release reaches zero, then erase all entries with that source pointer.
u32 FtManager::Remove(FtFont *font) {
  for (FtNode *node = head.next; node != &head; node = node->next) {
    if (node->font == font && !font->Release()) {
      for (FtNode *item = head.next; item != &head;) {
        FtNode *next = item->next;
        if (item->font == font) {
          item->previous->next = item->next;
          item->next->previous = item->previous;
          delete item;
          --count;
        }
        item = next;
      }
      return 0;
    }
  }
  return 0;
}

// Release each live list entry before clearing storage; new entries and mutations follow the actual list traversal.
bool FtManager::Close() {
  for (FtNode *node = head.next; node != &head; node = node->next)
    node->font->Release();
  while (head.next != &head) {
    FtNode *node = head.next;
    head.next = node->next;
    delete node;
  }
  head.previous = &head;
  count = 0;
  initialized = false;
  return 1;
}

// Match the source's case-specific.fon searches within the complete owned filename byte range.
static bool FtIsAlternateFile(u32 locale, const ScString &filename) {
  if (locale != 15 && locale != 17 && locale != 18 && locale != 19 && locale != 20)
    return false;
  u32 length = filename.Strlen();
  for (u32 index = 0; length >= 4 && index <= length - 4; ++index)
    if (!memcmp(filename.begin + index, ".fon", 4) || !memcmp(filename.begin + index, ".FON", 4))
      return true;
  return false;
}

FtSystemList::FtSystemList() : head(new FtNode) {
  head->next = head->previous = head;
  head->font = nullptr;
}

FtSystemList::~FtSystemList() {
  delete head;
}

FtSystem::FtSystem() : service_id(kFtFontSystemServiceId), initialized(false), references(0) {
}

FtSystem::~FtSystem() {
  FtSystem::Shutdown();
}

// Publish the requested font or system-service identity, then retain the complete object through its primary facet.
bool FtSystem::QueryInterface(u32 iid, void **out) {
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, FtFontSystem);
  HANDLE_QUERY_INTERFACE(IID_FtFontSystem, FtFontSystem);
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local reference first, retain the module, then return the live count after that callback.
u32 FtSystem::AddRef() {
  ++references;
  ScGetServicesDirector()->AddRef();
  return references;
}

// Remove the module reference before deleting the final object; otherwise decrement before that callback.
u32 FtSystem::Release() {
  if (references == 1) {
    ScGetServicesDirector()->RemoveRef();
    delete this;
    return 0;
  }
  --references;
  ScGetServicesDirector()->RemoveRef();
  return references;
}

// Install the five original family names through the live font-system interface once per initialization.
bool FtSystem::Init() {
  if (!initialized) {
    const char *values[] = {"Times New Roman", "Arial", "Courier New", "Lucida Console", "Wingdings"};
    for (u32 index = 0; index < 5; ++index) {
      ScString name(values[index]);
      FontSetName((FtType)index, name);
    }
    initialized = true;
  }
  return 1;
}

// Release each cached font in list order, erase its nodes and clear the initialization byte.
bool FtSystem::Shutdown() {
  FtNode *head = fonts.head;
  for (FtNode *node = head->next; node != head; node = node->next)
    node->font->Release();
  while (head->next != head) {
    FtNode *node = head->next;
    head->next = node->next;
    delete node;
  }
  head->previous = head;
  initialized = false;
  return 1;
}

// Retain an attribute-matched cached font, or construct the original locale-specific parameter font.
FtFont *FtSystem::FontAcquire(FtType type, u32 size, u32 flags, u32 color) {
  FtNode *head = fonts.head;
  for (FtNode *node = head->next; node != head; node = node->next) {
    FtFont *font = node->font;
    if (font->GetFontType() == type && font->GetFontSize() == size && font->GetFontFlags() == flags &&
        font->Color() == color) {
      font->AddRef();
      return font;
    }
  }
  ScLanguageManager *manager = FtGetLanguageManager();
  u32 locale = manager ? manager->GetCurrentLanguage() : 1;
  FtFont *font;
  if (locale == 15 || locale == 17 || locale == 18 || locale == 19 || locale == 20)
    font = new FtMultiFont(type, size, flags, color);
  else
    font = new FtBitmapFont((u32)type, size, flags, color);
  if (!font->Initialize()) {
    ::operator delete(font);
    return nullptr;
  }
  FtNode *node = new FtNode;
  node->font = font;
  node->previous = head;
  node->next = head->next;
  node->next->previous = node;
  head->next = node;
  font->AddRef();
  return font;
}

// Acquire a retained cached filename match or initialize the original file-font class for the active locale.
FtFont *FtSystem::FontAcquire(const ScStringView &filename) {
  ScString path;
  FtNode *head = fonts.head;
  for (FtNode *node = head->next; node != head; node = node->next) {
    FtFont *font = node->font;
    if (font->GetFontFilePath(&path) && !ScCompareNoCase(path.ToChar(), filename.ToChar())) {
      font->AddRef();
      return font;
    }
  }
  ScLanguageManager *manager = FtGetLanguageManager();
  u32 locale = manager ? manager->GetCurrentLanguage() : 1;
  ScString copy(filename);
  FtFont *font;
  if (FtIsAlternateFile(locale, copy))
    font = new FtMultiFont(copy, true);
  else
    font = new FtBitmapFont(&copy, 1);
  if (!font->Initialize()) {
    ::operator delete(font);
    return nullptr;
  }
  FtNode *node = new FtNode;
  node->font = font;
  node->previous = head;
  node->next = head->next;
  node->next->previous = node;
  head->next = node;
  font->AddRef();
  filename.ToChar();
  return font;
}

// Release matching cache entries until one reaches zero, then remove all nodes with that source pointer.
u32 FtSystem::FontRelease(FtFont *font) {
  FtNode *head = fonts.head;
  for (FtNode *node = head->next; node != head; node = node->next) {
    if (node->font == font && !font->Release()) {
      for (FtNode *item = head->next; item != head;) {
        FtNode *next = item->next;
        if (item->font == font) {
          item->previous->next = item->next;
          item->next->previous = item->previous;
          delete item;
        }
        item = next;
      }
      return 0;
    }
  }
  return 0;
}

// Assign a valid family name after reading its length and data; invalid types only call the source ToChar method.
u8 FtSystem::FontSetName(FtType type, const ScStringView &name) {
  u32 key = (u32)type;
  if (key > 5) {
    name.ToChar();
    return 0;
  }
  u32 length = name.Strlen();
  const char *data = name.Data();
  names[key].FromChar(data, length);
  return 1;
}

// Copy an existing family name through the output interface; a missing key leaves that output unchanged.
u8 FtSystem::FontGetName(FtType type, ScStringView *name) {
  auto entry = names.find((u32)type);
  if (entry == names.end()) {
    ScString fallback("Arial");
    return 0;
  }
  name->Copy(entry->second);
  return 1;
}

u32 FtSystem::GetServiceID() const {
  return service_id;
}

void FtSystem::SetServiceID(u32 value) {
  service_id = value;
}

i32 FtSystem::GetServicePriority() const {
  return kFtSystemServicePriority;
}

bool FtSystem::IsServiceRunning() const {
  return true;
}

void FtSystem::SetServiceRunning(bool) {
}

bool FtSystem::OnIdle(u32) {
  return false;
}

bool FtSystem::OnTick(u32) {
  return true;
}

bool FtSystem::OnInstall() {
  return true;
}
