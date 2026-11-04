#define WIN32_LEAN_AND_MEAN
#include "window/input.h"

#include <assert.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "common/sc3_unknown.h"
#include "window/sdl_input.h"

#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "graphics/image_codecs.h"
#include "resources/api.h"
#include "resources/file.h"
#include "window/api.h"
#include "window/cursor_platform.h"

#undef LoadCursor

static constexpr u32 kWnCursorRefHighWordMask = 0xffff0000;

// Construct empty cursor state, then acquire graphics only when the module has a framework.
WnCursorBase::WnCursorBase()
    : hardware(false), graphics(nullptr), filename(), image(nullptr), hotspot_x(0), hotspot_y(0), references(0),
      cursor(nullptr) {
  if (WnGetFramework()) {
    graphics = WnGetGraphics();
    if (graphics)
      hardware = graphics->DoesSystemHaveCursorSupport();
  }
}

// Clear the Windows shared handle or destroy the SDL-owned cursor, then release the software image.
WnCursorBase::~WnCursorBase() {
  if (cursor) {
    WnPlatformDestroyCursor(cursor);
    cursor = nullptr;
  }
  ScRelease(&image);
}

// Clear the genuine output before checking either supported cursor identity, then publish before virtual AddRef.
bool WnCursorBase::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnCursor);
  HANDLE_QUERY_INTERFACE(IID_WnCursor, WnCursor);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnCursorBase::AddRef() {
  return ++references;
}

// Delete only when the entry count was one. An entry zero wraps without deletion.
u32 WnCursorBase::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Prefer the platform handle when permitted; otherwise replace the software cursor image at hotspot zero.
// SDL clears the previous software image before activating a system cursor.
void WnCursorBase::SetCursor() {
  if (cursor) {
    if (WnPlatformSelectsCursorDirectly() || hardware || !graphics || graphics->DoesSystemHaveCursorSupport()) {
      WnPlatformSetCursor(cursor, graphics);
      return;
    }
  } else if (!graphics)
    return;
  WnPlatformClearCursorOverlay();
  graphics->SetCursorBuffer(image, 0, 0);
}

void WnCursorBase::GetHotSpot(i32 *x, i32 *y) {
  *x = hotspot_x;
  *y = hotspot_y;
}

// Recreate the captured Windows IDC_UPARROW as an SDL-owned color cursor without selecting it.
// Captured from Windows Aero at system DPI 96 (32x32, hotspot 6,0); other themes and scales may differ.
static SDL_Cursor *WnCreateUpArrowCursor() {
  static constexpr u32 kColors[] = {
      0x00000000, 0x60000000, 0x80000000, 0xbf000000, 0xdf000000, 0xfb000000,
      0xff000000, 0xff707070, 0xff7f7f7f, 0xffbfbfbf, 0xffdfdfdf, 0xffffffff};
  static constexpr char kPixels[] =
      "00000000000000000000000000000000"
      "00000550000000000000000000000000"
      "00005775000000000000000000000000"
      "00057BB7500000000000000000000000"
      "0057BBBB750000000000000000000000"
      "05788AA8875000000000000000000000"
      "02224994222000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003993000000000000000000000000"
      "00003663000000000000000000000000"
      "00000110000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000"
      "00000000000000000000000000000000";
  u32 pixels[32 * 32];
  for (u32 i = 0; i < 32 * 32; ++i) {
    char index = kPixels[i];
    pixels[i] = kColors[index <= '9' ? index - '0' : index - 'A' + 10];
  }
  SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_ARGB8888, pixels, 32 * sizeof(u32));
  if (!surface)
    return nullptr;
  SDL_Cursor *cursor = SDL_CreateColorCursor(surface, 6, 0);
  SDL_DestroySurface(surface);
  return cursor;
}

// Map the original recognized system-cursor identifiers to the active platform's cursor handles.
static void *WnCreatePlatformSystemCursor(WnStandardCursor identifier) {
  SDL_SystemCursor resource;
  switch (identifier) {
  case WnStandardCursor::Arrow:
    resource = SDL_SYSTEM_CURSOR_DEFAULT;
    break;
  case WnStandardCursor::Cross:
    resource = SDL_SYSTEM_CURSOR_CROSSHAIR;
    break;
  case WnStandardCursor::IBeam:
    resource = SDL_SYSTEM_CURSOR_TEXT;
    break;
  case WnStandardCursor::No:
    resource = SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    break;
  case WnStandardCursor::SizeAll:
    resource = SDL_SYSTEM_CURSOR_MOVE;
    break;
  case WnStandardCursor::SizeNESW:
    resource = SDL_SYSTEM_CURSOR_NESW_RESIZE;
    break;
  case WnStandardCursor::SizeNS:
    resource = SDL_SYSTEM_CURSOR_NS_RESIZE;
    break;
  case WnStandardCursor::SizeNWSE:
    resource = SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    break;
  case WnStandardCursor::SizeWE:
    resource = SDL_SYSTEM_CURSOR_EW_RESIZE;
    break;
  case WnStandardCursor::Wait:
    resource = SDL_SYSTEM_CURSOR_WAIT;
    break;
  case WnStandardCursor::UpArrow:
    return WnCreateUpArrowCursor();
  default:
    return nullptr;
  }
  return SDL_CreateSystemCursor(resource);
}

WnSystemCursor::WnSystemCursor(WnStandardCursor identifier) {
  cursor = WnCreatePlatformSystemCursor(identifier);
}

// Clear the borrowed system cursor handle before the base cursor destructor runs.
WnSystemCursor::~WnSystemCursor() {
  if (cursor)
    WnPlatformDestroyCursor(cursor);
  cursor = nullptr;
}

bool WnSystemCursor::LoadCursor(ScStringView *) {
  return false;
}

void WnSystemCursor::GetHotSpot(i32 *, i32 *) {
}

#undef LoadCursor
#undef SetCursor

// Copy the supplied interface before initializing image/handle ownership, then acquire graphics and load the owned name.
// The original direct loader call may update the hotspot and low reference-count word from software BMP metadata.
WnCursorBase::WnCursorBase(const ScStringView &name)
    : hardware(false), graphics(nullptr), filename(name), image(nullptr), hotspot_x(0), hotspot_y(0), references(0),
      cursor(nullptr) {
  if (WnGetFramework()) {
    graphics = WnGetGraphics();
    if (graphics)
      hardware = graphics->DoesSystemHaveCursorSupport();
  }
  hardware = WnPlatformFileCursorHardware(hardware);
  WnCursorBase::LoadCursor(&filename);
}

// Load CUR through Windows when hardware is selected; otherwise load the replaced BMP name or create its real fallback.
// Preserve the original overlapping hotspot reads as explicit field updates, including their low reference-count overwrite.
bool WnCursorBase::LoadCursor(ScStringView *name) {
  {
    ScString copy(*name);
    filename = copy;
  }
  if (hardware) {
    cursor = WnPlatformLoadCursor(cursor, filename);
    return cursor != nullptr;
  }
  if (cursor) {
    WnPlatformDestroyCursor(cursor);
    cursor = nullptr;
  }
  cursor = WnPlatformLoadCompositedCursor(filename);
  if (cursor)
    return true;
  u32 length = filename.Strlen();
  // External filename boundary: the original extension writes are outside allocated storage for shorter names.
  if (length < 3)
    ScFatal(SC_ERROR_INVALID, "Cursor filename is shorter than its replaced extension");
  memcpy(filename.begin + length - 3, "bmp", 3);
  void *slot = nullptr;
  if (WnGetCom()->GetClassObject(kRsImageFactoryClassId, IID_RsImageOptions, &slot)) {
    const GxColorType format(GxColorType::Color::RGB16);
    static_cast<RsImageOptions *>(slot)->LoadFile(&filename, &image, 1, &format, 0);
    static_cast<RsImageOptions *>(slot)->Release();
    slot = nullptr;
  }
  if (!image) {
    WnGetGraphics()->CreateBuffer(&image);
    image->Init(32, 32, GxColorType(static_cast<GxColorType::Color>(4)), nullptr, GxDepthType(GxDepthType::Depth::None),
        GxVramRequest::VideoMemoryLowPriority, false);
  }
  WnResources *resources = WnGetUiResources();
  u32 key = resources->GetColor(kWnColorUiTransparency);
  image->SetTransparency(key);
  RsFile file(filename);
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    i32 file_length = (i32)file.Size();
    file.SeekBegin();
    u32 argument = (u32)(uptr)name;
    file.Read(reinterpret_cast<u8 *>(&argument) + 2, 2);
    if ((u16)(argument >> 16) == 0x4D42) {
      file.SeekAbsolute(2);
      u32 bitmap_size, read = 4;
      file.Read(&bitmap_size, &read);
      // The original consumes all four size bytes regardless of the read's Boolean result.
      assert(read == 4);
      if (file_length >= (i32)(bitmap_size + 8u)) {
        file.SeekAbsolute((i32)bitmap_size);
        u32 first = (u16)hotspot_x | ((u32)(u16)hotspot_y << 16);
        file.Read(&first, 4);
        hotspot_x = (i16)first;
        hotspot_y = (i16)(first >> 16);
        u32 second = (u16)hotspot_y | (references << 16);
        file.Read(&second, 4);
        hotspot_y = (i16)second;
        references = (references & kWnCursorRefHighWordMask) | (second >> 16);
        i16 x = hotspot_x;
        i32 width = image->Width();
        if (x >= width || x < 0)
          hotspot_x = 0;
        i16 y = hotspot_y;
        i32 height = image->Height();
        if (y >= height || y < 0)
          hotspot_y = 0;
      }
    }
  }
  return image != nullptr;
}

WnCursors::WnCursors() : entries(nullptr), saved(), current(nullptr), references(0) {
}

WnCursors::~WnCursors() {
  WnCursors::Shutdown();
}

// A null output returns false; other outputs are cleared before either supported manager identity is tested.
bool WnCursors::QueryInterface(u32 iid, void **out) {
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = nullptr;
  if (iid != IID_ScUnknown && iid != IID_WnCursorManager)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnCursorManager *>(this);
  AddRef();
  return true;
}

u32 WnCursors::AddRef() {
  return ++references;
}

// Post-decrement the shared manager count, deleting only for an entry count of one.
u32 WnCursors::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Register arrow first and make it current before creating and registering the wait and I-beam cursors.
bool WnCursors::Init() {
  WnCursor *arrow = new WnSystemCursor(WnStandardCursor::Arrow);
  AddCursor(arrow, 0);
  SetCursor(arrow);
  AddCursor(new WnSystemCursor(WnStandardCursor::Wait), 14);
  AddCursor(new WnSystemCursor(WnStandardCursor::IBeam), 2);
  return true;
}

// Release current before the live ordered registry; saved holds deliberately remain in their original stack.
bool WnCursors::Shutdown() {
  SetCurrentCursor(nullptr);
  ReleaseAllCursors();
  return true;
}

// Find a registered cursor in unsigned identifier order, replacing only the general-purpose map machinery.
WnCursors::WnCursorEntry *WnCursors::Find(u32 identifier) {
  WnCursorEntry *entry = entries;
  while (entry && entry->identifier < identifier)
    entry = entry->next;
  return entry && entry->identifier == identifier ? entry : nullptr;
}

// Remove the captured map entry after any original Release callback has completed.
void WnCursors::Erase(WnCursorEntry *entry) {
  WnCursorEntry **link = &entries;
  while (*link != entry)
    link = &(*link)->next;
  *link = entry->next;
  delete entry;
}

// Retain the replacement before lookup, release an old row before erasing it, then insert only if still absent.
bool WnCursors::AddCursor(WnCursor *cursor, u32 identifier) {
  if (cursor)
    cursor->AddRef();
  WnCursorEntry *old = Find(identifier);
  if (old) {
    if (old->cursor)
      old->cursor->Release();
    Erase(old);
  }
  if (cursor) {
    WnCursorEntry **link = &entries;
    while (*link && (*link)->identifier < identifier)
      link = &(*link)->next;
    if (!*link || (*link)->identifier != identifier)
      *link = new WnCursorEntry{*link, identifier, cursor};
  }
  return true;
}

// Return the borrowed registered cursor without acquiring another ownership reference.
WnCursor *WnCursors::GetIGZCursor(u32 identifier) {
  WnCursorEntry *entry = Find(identifier);
  return entry ? entry->cursor : nullptr;
}

// A missing identifier returns false; an existing cursor is released before its registry row is erased.
bool WnCursors::RemoveCursor(u32 identifier) {
  WnCursorEntry *entry = Find(identifier);
  if (!entry)
    return false;
  entry->cursor->Release();
  Erase(entry);
  return true;
}

// Retain the new value before loading and releasing current, then publish the selected pointer.
void WnCursors::SetCurrentCursor(WnCursor *cursor) {
  if (cursor)
    cursor->AddRef();
  if (current)
    current->Release();
  current = cursor;
}

// Show a found cursor before updating current. Missing registry identifiers leave the current cursor unchanged.
bool WnCursors::SetCursor(u32 identifier) {
  WnCursor *cursor = GetIGZCursor(identifier);
  if (!cursor)
    return false;
  cursor->SetCursor();
  SetCurrentCursor(cursor);
  return true;
}

// The pointer overload assigns ownership before calling the selected cursor's live activation method.
bool WnCursors::SetCursor(WnCursor *cursor) {
  SetCurrentCursor(cursor);
  cursor->SetCursor();
  return true;
}

// Hold the current cursor before selection; save that hold only when selecting the requested identifier succeeds.
bool WnCursors::PushAutoCursor(u32 identifier) {
  WnCursor *previous = current;
  previous->AddRef();
  if (SetCursor(identifier)) {
    *saved.Append() = previous;
    return true;
  }
  previous->Release();
  return false;
}

// Pop before restoring the saved selection, then release the saved hold after the live pointer-overload call.
bool WnCursors::PopAutoCursor() {
  if (!saved.size)
    return false;
  WnCursor *cursor = saved.data[--saved.size];
  SetCursor(cursor);
  cursor->Release();
  return true;
}

// Release rows in live ascending order, then discard map storage without another round of cursor releases.
void WnCursors::ReleaseAllCursors() {
  WnCursorEntry *entry = entries;
  while (entry) {
    entry->cursor->Release();
    entry = entry->next;
  }
  while (entries) {
    entry = entries;
    entries = entry->next;
    delete entry;
  }
}

bool WnCursors::AddCursor(const ScStringView &name, u32 identifier) {
  return AddCursor(new WnCursorBase(name), identifier);
}

struct WnAcceleratorAcquisition {
  ScMessageServer *value;

  // Match the source temporary's release on both ordinary return and registration callback unwinding.
  ~WnAcceleratorAcquisition() {
    if (value)
      value->Release();
  }
};

WnKeyAccelerator::WnKeyAccelerator() : initialized(false), references(0), messages(nullptr) {
}

WnKeyAccelerator::~WnKeyAccelerator() {
  if (initialized)
    WnKeyAccelerator::Shutdown();
}

// Publish and retain the accelerator or unknown identity, clearing the output.
bool WnKeyAccelerator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnAccelerator, WnAccelerator);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnAccelerator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnKeyAccelerator::AddRef() {
  return ++references;
}

// Destroy the actual accelerator at its last reference without reading its storage afterward.
u32 WnKeyAccelerator::Release() {
  if (--references)
    return references;
  delete this;
  return 0;
}

// Publish the fresh message service before resource registration.
// Mark initialized before releasing that acquisition, retaining its original scoped lifetime.
bool WnKeyAccelerator::Init(WnAcceleratorResources *resources) {
  if (initialized)
    return false;
  void *slot = nullptr;
  WnGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &slot);
  WnAcceleratorAcquisition acquired{static_cast<ScMessageServer *>(slot)};
  messages = acquired.value;
  resources->RegisterResources(this);
  initialized = true;
  return true;
}

bool WnKeyAccelerator::Shutdown() {
  return true;
}

// Insert the combined key and full message only when absent; duplicate registration leaves the prior message intact.
bool WnKeyAccelerator::RegisterAccelerator(u32 modifiers, u32 key, const ScMessage &message) {
  u32 combined = modifiers | (key << 16);
  for (u32 i = 0; i < entries.size; ++i)
    if (entries.data[i].key == combined)
      return false;
  WnAcceleratorEntry *entry = entries.Append();
  entry->key = combined;
  entry->message = message;
  return true;
}

bool WnKeyAccelerator::RegisterAccelerator(u32 modifiers, u32 key, u32 command) {
  ScMessage message{kWnMsg_AcceleratorCommand, command, 0, 0};
  return RegisterAccelerator(modifiers, key, message);
}

// Remove an exact combined DWORD key, preserving all other registered messages and returning whether it existed.
bool WnKeyAccelerator::UnregisterAccelerator(u32 key) {
  for (u32 i = 0; i < entries.size; ++i) {
    if (entries.data[i].key != key)
      continue;
    memmove(entries.data + i, entries.data + i + 1, (entries.size - i - 1) * sizeof(WnAcceleratorEntry));
    --entries.size;
    return true;
  }
  return false;
}

// Ignore modifier keys and source type4; send a copied matching message, replacing its type for source type6.
bool WnKeyAccelerator::ProcessAccelerator(const ScMessage &input) {
  u32 key = (u32)input.data1;
  if (input.type == kWnMsg_Character || key == kWnKeyShift || key == kWnKeyControl || key == kWnKeyAlt)
    return false;
  u32 combined = (u32)input.data2 | (key << 16);
  for (u32 i = 0; i < entries.size; ++i) {
    if (entries.data[i].key != combined)
      continue;
    ScMessage message = entries.data[i].message;
    if (input.type == kWnMsg_KeyUp)
      message.type = kWnMsg_AcceleratorKeyUp;
    messages->MessageSend(message);
    return true;
  }
  return false;
}

// Construct the registered accelerator at zero references and return its actual leading unknown interface.
ScUnknown *WnConstructKeyAccelerator() {
  return static_cast<WnAccelerator *>(new WnKeyAccelerator);
}

// Allocate the original empty key list before clearing all 256 stored key bytes.
WnKeyboard::WnKeyboard() {
  head = new WnKeyNode;
  head->next = head->previous = head;
  WnKeyboard::ClearKeyboard();
}

WnKeyboard::~WnKeyboard() {
  WnKeyNode *node = head->next;
  while (node != head) {
    WnKeyNode *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

// Clear stored state before removing the ordered list of pressed keys.
void WnKeyboard::ClearKeyboard() {
  memset(keys, 0, sizeof(keys));
  WnKeyNode *node = head->next;
  while (node != head) {
    WnKeyNode *next = node->next;
    delete node;
    node = next;
  }
  head->next = head->previous = head;
}

bool WnKeyboard::IsKeyDown(u32 key) {
  assert(key < 256);
  return keys[key] != 0;
}

// A stored press matches only the exact current modifier mask, not a subset of it.
bool WnKeyboard::IsKeyDown(u32 key, u32 modifiers) {
  bool result = IsKeyDown(key);
  if (result)
    return modifiers == GetCurrentModifierState();
  return result;
}

// Read the live platform key state without consulting the game's stored key list.
static bool WnPlatformIsKeyDown(u32 key) {
  return WnSdlIsKeyDown(key);
}

bool WnKeyboard::IsKeyDownNow(u32 key) {
  return WnPlatformIsKeyDown(key);
}

bool WnKeyboard::CharIsModifier(u32 key) {
  return key == kWnKeyShift || key == kWnKeyControl || key == kWnKeyAlt;
}

// Acquire Shift, Control and Alt through the live stored-key virtual in that order.
u32 WnKeyboard::GetCurrentModifierState() {
  u32 result = IsKeyDown(kWnKeyShift) ? kWnModifierShift : 0;
  if (IsKeyDown(kWnKeyControl))
    result |= kWnModifierControl;
  if (IsKeyDown(kWnKeyAlt))
    result |= kWnModifierAlt;
  return result;
}

// Read the three SDL lock modifiers independently of stored game keys.
// Other virtual-key low bits have no SDL equivalent; report the unavailable query through SDL's error channel.
bool WnIsToggleKeySet(u32 key) {
  SDL_Keymod mod = SDL_GetModState();
  if (key == kWnKeyCapsLock)
    return (mod & SDL_KMOD_CAPS) != 0;
  if (key == kWnKeyNumLock)
    return (mod & SDL_KMOD_NUM) != 0;
  if (key == kWnKeyScrollLock)
    return (mod & SDL_KMOD_SCROLL) != 0;
  return SDL_SetError("Toggle state is unavailable for virtual key %u", key);
}

bool WnKeyboard::IsToggleKeySet(u32 key) {
  return WnIsToggleKeySet(key);
}

// Store a new press before appending its key; repeated key-down messages do not add duplicates.
void WnKeyboard::ProcessKeyDown(u32 key) {
  assert(key < 256);
  if (!keys[key]) {
    keys[key] = 1;
    WnKeyNode *node = new WnKeyNode;
    node->key = key;
    node->next = head;
    node->previous = head->previous;
    node->previous->next = node;
    node->next->previous = node;
  }
}

// Clear the byte first, then remove every matching pressed-list entry.
void WnKeyboard::ProcessKeyUp(u32 key) {
  assert(key < 256);
  if (keys[key]) {
    keys[key] = 0;
    WnKeyNode *node = head->next;
    while (node != head) {
      WnKeyNode *next = node->next;
      if (node->key == key) {
        node->previous->next = node->next;
        node->next->previous = node->previous;
        delete node;
      }
      node = next;
    }
  }
}
