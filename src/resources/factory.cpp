#include "resources/factory.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include <new>

#include "common/api.h"
#include "common/ids.h"
#include "common/locale.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "geometry/building.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/image_factory.h"
#include "resources/indexed_file.h"
#include "resources/property_set.h"
#include "resources/resource.h"

// Cache the actual filesystem before dropping its temporary service reference, retaining the original borrowed lifetime.
RsFileSystem *RsGetFileSystem() {
  static RsFileSystem *fileSystem;
  if (!fileSystem) {
    RsFileSystem *value = nullptr;
    RsGetFrameWork()->GetSystemService(RsFileSystem::ServiceId, RsFileSystem::Iid, reinterpret_cast<void **>(&value));
    fileSystem = value;
    if (value)
      value->Release();
  }
  return fileSystem;
}

// Publish the language service in the resource DLL's borrowed cache before releasing the query's owned reference.
ScLanguageManager *RsGetLanguageManager() {
  static ScLanguageManager *language;
  if (!language) {
    ScLanguageManager *value = nullptr;
    RsGetFrameWork()->GetSystemService(
        kScLanguageManagerServiceId, IID_ScLanguageManager, reinterpret_cast<void **>(&value));
    language = value;
    if (value)
      value->Release();
  }
  return language;
}

// Construct the genuine indexed-file segment with an empty filename and return its zero-reference controlling view.
ScUnknown *RsCreateArchiveSegment() {
  return static_cast<RsDatabaseSegment *>(new RsIndexedFile(nullptr));
}

RsDllDirector::RsDllDirector() : references(0), com(nullptr), library_path(), framework(nullptr) {
}

RsDllDirector::~RsDllDirector() {
}

// Publish either the director or its distinct framework-hook interface before retaining the shared module count.
bool RsDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsDllDirector::AddRef() {
  return ++references;
}

u32 RsDllDirector::Release() {
  return RemoveRef();
}

// Zero remains zero; positive module references decrement without destroying the director or its children.
u32 RsDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 RsDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured by InitializeCOM, including its initial null value.
ScFramework *RsDllDirector::FrameWork() {
  return framework;
}

// Borrow the COM root captured by InitializeCOM, including its initial null value.
ScCom *RsDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and framework, copy the supplied path, then initialize each live child in stored order.
bool RsDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (usize child = 0; child < children.size(); ++child)
    children[child]->InitializeCOM(input, path);
  return true;
}

void RsDllDirector::AddDirector(RsDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (usize child = 0; child < director->children.size(); ++child)
    AddDirector(director->children[child]);
  children.push_back(director);
}

void RsDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (usize child = 0; child < children.size(); ++child)
    children[child]->EnumClassObjects(callback, context);
  for (auto entry = classes.begin(); entry != classes.end(); ++entry)
    callback(entry->first.first, entry->first.second, context);
}

// Search children before local constructors. A failed QI is followed by AddRef/Release to dispose the new zero-ref object.
bool RsDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (usize child = 0; child < children.size(); ++child)
    if (children[child]->GetClassObject(clsid, iid, out))
      return true;
  for (auto entry = classes.begin(); entry != classes.end(); ++entry) {
    if (entry->first.first != clsid)
      continue;
    ScUnknown *object = entry->second();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// A child can veto unloading before the parent's own reference count is inspected.
bool RsDllDirector::CanUnloadNow() {
  for (usize child = 0; child < children.size(); ++child)
    if (!children[child]->CanUnloadNow())
      return false;
  return references == 0;
}

bool RsDllDirector::OnUnload() {
  return true;
}

bool RsDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child even after a refusal, combining their register results into the final Boolean.
bool RsDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (usize child = 0; child < children.size(); ++child)
    if (!children[child]->AppRegister(registry))
      result = false;
  return result;
}

bool RsDllDirector::OnStart(ScCom *) {
  ScRegistry *registry = nullptr;
  RsGetFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

bool RsDllDirector::PreFrameWorkInit() {
  return true;
}

bool RsDllDirector::PreAppInit() {
  return true;
}

bool RsDllDirector::PostAppInit() {
  return true;
}

bool RsDllDirector::PreAppShutdown() {
  return true;
}

bool RsDllDirector::PostAppShutdown() {
  return true;
}

bool RsDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool RsDllDirector::AbortiveQuit() {
  return true;
}

bool RsDllDirector::OnInstall() {
  return true;
}

RsDirector::RsDirector() {
  AddDirector(RsGetResourceDirector());
  AddDirector(RsGetImageDirector());
}

u32 RsDirector::GetDirectorID() const {
  return kRsDirectorId;
}

bool RsDirector::OnStart(ScCom *input) {
  RsGetResourceDirector()->OnStart(input);
  return true;
}

// Register the original resource class IDs and actual constructor functions without creating class instances.
// Keep unsigned rvalues for the original literal keys so the pair constructor retains its original type deduction.
RsResourceDirector::RsResourceDirector() {
  classes.emplace(RsClassKey{u32(kRsMiffSegmentClassId), 0}, RsCreateClass199925);
  classes.emplace(RsClassKey{kRsIndexedFileClassId, 0}, RsCreateArchiveSegment);
  classes.emplace(RsClassKey{kRsKeyListClassId, 0}, RsCreateKeyList);
  classes.emplace(RsClassKey{u32(kRsDirectorySegmentClassId), 0}, RsCreateClassA079CE1B);
  classes.emplace(RsClassKey{u32(kRsTextFactoryClassId), 0}, RsCreateTextFactory);
  classes.emplace(RsClassKey{kRsLayeredFileClassId, 0}, RsCreateClass835F62CF);
  classes.emplace(RsClassKey{kRsManagerClassId, 0}, RsCreateManager);
  classes.emplace(RsClassKey{u32(kRsQfsCodecClassId), 0}, RsCreateClass35990A3);
  classes.emplace(RsClassKey{u32(kRsLzoCodecClassId), 0}, RsCreateLzoCodec);
  classes.emplace(RsClassKey{u32(kRsPropertyFactoryClassId), 0}, RsCreateClass2AE197F);
  classes.emplace(RsClassKey{u32(kRsPaletteFactoryClassId), 0}, RsCreateClass5E3);
  classes.emplace(RsClassKey{u32(kRsAcceleratorFactoryClassId), 0}, RsCreateClass42E411C2);
  classes.emplace(RsClassKey{u32(kRsSerializableFactoryClassId), 0}, RsCreateClass1A1E8FFC);
}

u32 RsResourceDirector::GetDirectorID() const {
  return kRsResourceDirectorId;
}

bool RsResourceDirector::OnStart(ScCom *input) {
  ScSystemService *service = nullptr;
  input->GetClassObject(kRsManagerClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    RsGetFrameWork()->AddSystemService(service);
    service->Release();
  }
  return true;
}

RsImageDirector::RsImageDirector() {
  classes.emplace(RsClassKey{kRsImageFactoryClassId, 0}, RsCreateImageFactory);
}

u32 RsImageDirector::GetDirectorID() const {
  return kRsImageDirectorId;
}

alignas(RsDirector) static u8 RsDirectorStorage[sizeof(RsDirector)];
alignas(RsResourceDirector) static u8 RsResourceDirectorStorage[sizeof(RsResourceDirector)];
alignas(RsImageDirector) static u8 RsImageDirectorStorage[sizeof(RsImageDirector)];
static bool RsDirectorInitialized, RsResourceDirectorInitialized, RsImageDirectorInitialized;

static void RsDestroyDirector() {
  reinterpret_cast<RsDirector *>(RsDirectorStorage)->~RsDirector();
}

static void RsDestroyResourceDirector() {
  reinterpret_cast<RsResourceDirector *>(RsResourceDirectorStorage)->~RsResourceDirector();
}

static void RsDestroyImageDirector() {
  reinterpret_cast<RsImageDirector *>(RsImageDirectorStorage)->~RsImageDirector();
}

// Set the lazy flag before construction, preserving recursive lookup of the original process-static director.
RsDirector *RsGetDirector() {
  if (!RsDirectorInitialized) {
    RsDirectorInitialized = true;
    new (RsDirectorStorage) RsDirector();
    atexit(RsDestroyDirector);
  }
  return reinterpret_cast<RsDirector *>(RsDirectorStorage);
}

// Construct and register the resource child once, independently of the main and image directors.
RsResourceDirector *RsGetResourceDirector() {
  if (!RsResourceDirectorInitialized) {
    RsResourceDirectorInitialized = true;
    new (RsResourceDirectorStorage) RsResourceDirector();
    atexit(RsDestroyResourceDirector);
  }
  return reinterpret_cast<RsResourceDirector *>(RsResourceDirectorStorage);
}

// Construct and register the image child once, independently of the main and resource directors.
RsImageDirector *RsGetImageDirector() {
  if (!RsImageDirectorInitialized) {
    RsImageDirectorInitialized = true;
    new (RsImageDirectorStorage) RsImageDirector();
    atexit(RsDestroyImageDirector);
  }
  return reinterpret_cast<RsImageDirector *>(RsImageDirectorStorage);
}

// Use the real module director's framework getter rather than a separate injected service root.
ScFramework *RsGetFrameWork() {
  return RsGetDirector()->FrameWork();
}

u32 RsAddModuleRef() {
  return RsGetDirector()->AddRef();
}

u32 RsRemoveModuleRef() {
  return RsGetDirector()->RemoveRef();
}

// Borrow the genuine COM root stored by the module director, without adding a reference or fabricating a service.
ScCom *RsGetCom() {
  return RsGetDirector()->GZCOM();
}

#if !defined(SC3_MONOLITHIC)
// Export the same lazy module director without querying an interface or retaining it.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return RsGetDirector();
}
#endif

RsExtensionMap::RsExtensionMap() : references(0) {
}

RsExtensionMap::~RsExtensionMap() {
}

// Publish the same controlling interface for Unknown or IID80 and retain it. Unsupported queries clear the output.
bool RsExtensionMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsExtensionMap);
  HANDLE_QUERY_INTERFACE(IID_RsExtensionMap, RsExtensionMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsExtensionMap::AddRef() {
  return ++references;
}

// Delete only on the one-to-zero transition. An initially zero counter wraps, as in the original unsigned field.
u32 RsExtensionMap::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

bool RsExtensionMap::Insert(u32 type, const ScStringView *extension) {
  ScString copy(*extension);
  return extensions.insert({type, copy}).first != extensions.end();
}

// Find the first ordered extension matching the caller's entire length, using the original case-insensitive prefix test.
u8 RsExtensionMap::GetType(const ScStringView *extension, u32 *out) {
  u32 length = extension->Strlen();
  for (const auto &entry : extensions)
    if (!ScCompareNoCase(extension->Data(), entry.second.begin, length)) {
      *out = entry.first;
      return 1;
    }
  return 0;
}

// Copy the first equal type's C extension into the supplied String. Missing types leave the output unchanged.
u8 RsExtensionMap::GetExtension(const u32 *type, ScStringView *out) {
  for (const auto &entry : extensions)
    if (entry.first == *type) {
      out->FromChar(entry.second.begin);
      return 1;
    }
  return 0;
}

void RsExtensionMap::Clear() {
  extensions.clear();
}

// Replace the original tree through its virtual clear and insertion methods, preserving first-duplicate behavior.
void RsExtensionMap::CopyFrom(const RsExtensionMap *source) {
  Clear();
  for (const auto &entry : source->extensions)
    Insert(entry.first, &entry.second);
}

u32 RsExtensionMap::Size() {
  return static_cast<u32>(extensions.size());
}

// Pair the source exception-unwind unlock with ordinary scope exit, including nested factory reads.
class RsRegionLock {
public:
  // Acquire the module region when the guarded operation begins.
  RsRegionLock() {
    RsEnterResourceRegion();
  }

  // Release the region on the original normal and exceptional exits.
  ~RsRegionLock() {
    RsLeaveResourceRegion();
  }
};

RsFactory::RsFactory() {
}

RsFactory::~RsFactory() {
}

bool RsFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsFactoryInterface, RsFactoryInterface);
  return RsModuleObject::QueryInterface(iid, out);
}

u32 RsFactory::AddRef() {
  return RsModuleObject::AddRef();
}

u32 RsFactory::Release() {
  return RsModuleObject::Release();
}

u8 RsFactory::LockServer(u32) {
  return 0;
}

// Create the record's resource type, copy the live second key and publish only after successful reading.
bool RsFactory::Load(RsRecord *record, u32 iid, void **out) {
  RsRegionLock region;
  RsResource *resource;
  bool result = false;
  if (New(record->GetKey()->type, IID_RsResource, reinterpret_cast<void **>(&resource))) {
    RsKey *source = record->GetKey(), *destination = resource->GetKey();
    *destination = *source;
    if (ReadRecord(resource, record))
      result = resource->QueryInterface(iid, out);
    resource->Release();
  }
  return result;
}

bool RsFactory::ReadRecord(RsResource *resource, RsRecord *record) {
  RsRegionLock region;
  RsStream *stream;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    result = SerialRead(resource, stream);
    stream->Release();
  }
  return result;
}

// Query the record stream under the recursive region and save the Boolean result before releasing it.
// The original const public input enters the existing mutable serial-writer boundary without changing its dispatch.
bool RsFactory::WriteRecord(const RsResource *resource, RsRecord *record) {
  RsRegionLock region;
  RsStream *stream;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    result = SerialWrite(const_cast<RsResource *>(resource), stream);
    stream->Release();
  }
  return result;
}

RsImageFactory::RsImageFactory() : mode(GxVramRequest::SystemMemory), format(GxColorType::Color::RGB16) {
  encodings.push_back(2);
}

RsImageFactory::~RsImageFactory() {
}

bool RsImageFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsImageOptions, RsImageOptions);
  return RsFactory::QueryInterface(iid, out);
}

u32 RsImageFactory::AddRef() {
  return RsFactory::AddRef();
}

u32 RsImageFactory::Release() {
  return RsFactory::Release();
}

// Create only image resources, retaining a temporary across QI and releasing it even when the query fails.
bool RsImageFactory::New(u32 type, u32 iid, void **out) {
  if (type != kRsImageResourceType)
    return false;
  RsImageResource *image = new RsImageResource();
  image->AddRef();
  bool result = image->QueryInterface(iid, out);
  image->Release();
  return result;
}

// Check the original mutable Type view before querying image data. Keep the format pair live while capturing the
// mode before entering the image reader.
bool RsImageFactory::SerialRead(RsResource *resource, RsStream *stream) {
  if (*resource->Type() != kRsImageResourceType)
    return 0;
  RsImageData *image;
  if (!resource->QueryInterface(IID_RsImageData, reinterpret_cast<void **>(&image)))
    return 0;
  u32 captured = static_cast<u32>(mode);
  u8 result = image->Read(stream, &format, captured);
  image->Release();
  return result;
}

// Check the original mutable Type view, then query image data and fetch the encoding through its live virtual.
// Preserve the writer result across release of the queried image interface.
bool RsImageFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  if (*resource->Type() != kRsImageResourceType)
    return 0;
  RsImageData *image = nullptr;
  if (!resource->QueryInterface(IID_RsImageData, reinterpret_cast<void **>(&image)))
    return 0;
  u32 encoding = GetEncoding();
  u8 result = image->Write(stream, encoding);
  image->Release();
  return result;
}

// Load through the actual loose-file stream and stack image resource, closing before image destruction on either result.
bool RsImageFactory::LoadFile(ScString *name, GxBuffer **out, u8 compressed, const GxColorType *requestedFormat, u32) {
  RsKey key;
  RsLooseRecord record;
  record.AddRef();
  bool result = record.Open(&key, name, 1);
  if (result) {
    RsImageResource image;
    image.AddRef();
    result = image.Read(static_cast<RsStream *>(&record), requestedFormat, compressed ? 2u : 0u) != 0;
    if (result)
      result = image.QueryBuffer(out);
    record.Close();
  }
  return result;
}

// Create a new file through the real record, replacing the stack image's buffer before selecting the live encoding.
u8 RsImageFactory::SaveFile(const ScStringView *name, GxBuffer *source) {
  if (!source)
    return 0;
  RsKey key;
  RsLooseRecord record;
  record.AddRef();
  u8 result = record.Open(&key, name, 3);
  if (result) {
    RsImageResource image;
    image.AddRef();
    image.SetBuffer(source);
    u32 encoding = GetEncoding();
    result = image.Write(static_cast<RsStream *>(&record), encoding);
    record.Close();
  }
  return result;
}

// Decode the selected module's bitmap using the live factory settings; preserve the read byte across buffer querying.
u8 RsImageFactory::LoadModuleBitmap(HINSTANCE__ *module, u16 identifier, GxBuffer **out) {
  RsBitmapRecord record;
  u8 result = 0;
  if (record.Init(module, identifier)) {
    RsImageResource image;
    image.AddRef();
    u32 captured = static_cast<u32>(mode);
    result = image.Read(static_cast<RsStream *>(&record), &format, captured);
    if (result)
      image.QueryBuffer(out);
  }
  return result;
}

// Decode the executable's bitmap using the supplied live format and compression byte, ignoring buffer-query failure.
u8 RsImageFactory::LoadExecutableBitmap(
    u16 identifier, GxBuffer **out, u8 compressed, const GxColorType *requestedFormat, u32) {
  RsBitmapRecord record;
  u8 result = 0;
  HINSTANCE__ *module = nullptr;
  if (record.Init(module, identifier)) {
    RsImageResource image;
    image.AddRef();
    result = image.Read(static_cast<RsStream *>(&record), requestedFormat, compressed ? 2u : 0u);
    if (result)
      image.QueryBuffer(out);
  }
  return result;
}

u8 RsImageFactory::LoadStream(GxBuffer **out, RsStream *stream, u32) {
  RsImageResource image;
  image.AddRef();
  u8 result = image.Read(stream, &format, 0);
  if (result)
    image.QueryBuffer(out);
  return result;
}

// Wrap the supplied buffer in a stack resource, preserving its creation, replacement and direct-destruction ordering.
u8 RsImageFactory::SaveStream(GxBuffer *source, RsStream *stream) {
  if (!source || !stream)
    return 0;
  RsImageResource image;
  image.AddRef();
  image.SetBuffer(source);
  u32 encoding = GetEncoding();
  return image.Write(stream, encoding);
}

void RsImageFactory::SetMode(GxVramRequest value) {
  mode = value;
}

void RsImageFactory::SetFormat(const GxColorType &value) {
  format = value;
}

bool RsImageFactory::PushEncoding(u32 encoding) {
  encodings.push_back(encoding);
  return true;
}

// Retire the latest encoding only when another entry remains beneath it.
bool RsImageFactory::PopEncoding() {
  if (encodings.size() <= 1)
    return false;
  encodings.pop_back();
  return true;
}

u32 RsImageFactory::GetEncoding() {
  return encodings.empty() ? 2 : encodings.back();
}

// Return the zero-reference image factory registered by the module's image director.
ScUnknown *RsCreateImageFactory() {
  return static_cast<RsFactoryInterface *>(new RsImageFactory());
}

// Cache the framework's actual graphics view before releasing the temporary reference, leaving a borrowed global pointer.
static GxGraphics *RsGetPaletteGraphics() {
  static GxGraphics *graphics;
  if (!graphics) {
    GxGraphics *value = nullptr;
    RsGetFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&value));
    graphics = value;
    if (value)
      value->Release();
  }
  return graphics;
}

RsPaletteResource::RsPaletteResource() {
}

RsPaletteResource::~RsPaletteResource() {
}

// Publish the primary palette identity and acquire its resource reference. Other IDs recurse without a base case
// through both installed facets, so exclude that invalid execution instead of inventing a failed query result.
bool RsPaletteResource::QueryInterface(u32 iid, void **out) {
  if (iid != IID_GxPaletteView) {
    assert(false && "The original palette resource recursively queries unsupported interface IDs");
    abort();
  }
  *out = static_cast<GxPaletteView *>(this);
  AddRef();
  return true;
}

u32 RsPaletteResource::AddRef() {
  return RsResource::AddRef();
}

u32 RsPaletteResource::Release() {
  return RsResource::Release();
}

RsPaletteFactory::RsPaletteFactory() {
}

bool RsPaletteFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsPaletteLoader, RsPaletteLoader);
  return RsFactory::QueryInterface(iid, out);
}

u32 RsPaletteFactory::AddRef() {
  return RsFactory::AddRef();
}

u32 RsPaletteFactory::Release() {
  return RsFactory::Release();
}

// Create only palette resources and destroy a newly constructed resource if its requested interface query fails.
bool RsPaletteFactory::New(u32 type, u32 iid, void **out) {
  if (type != 0x5e2u)
    return false;
  RsPaletteResource *resource = new RsPaletteResource();
  if (resource->QueryInterface(iid, out))
    return true;
  delete resource;
  return false;
}

// Read all256 RGB triples before allocating the palette, then convert each entry through the actual graphics service.
// Zero-extend each packed low word into the original DWORD object passed to the live palette setter.
bool RsPaletteFactory::SerialRead(RsResource *resource, RsStream *stream) {
  GxPaletteView *palette = nullptr;
  if (!resource->QueryInterface(IID_GxPaletteView, reinterpret_cast<void **>(&palette)))
    return 0;
  u8 rgb[768] = {};
  u8 result = stream->ReadBytes(rgb, sizeof(rgb));
  if (result) {
    result = palette->MakeGenericPalette(256, 16);
    if (result)
      for (i32 i = 0; i < 256; ++i) {
        u32 value = RsGetPaletteGraphics()->ConvertRGBValueToNative16Bit(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
        palette->SetColor(i, value);
      }
  }
  palette->Release();
  return result;
}

// Write exactly a256-entry sixteen-bit palette through the graphics unpacker and preserve the stream's final byte.
bool RsPaletteFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  GxPaletteView *palette = nullptr;
  if (!resource->QueryInterface(IID_GxPaletteView, reinterpret_cast<void **>(&palette)))
    return 0;
  u8 result = 0;
  i32 count = palette->GetNumberEntries(), bits = palette->GetBitsPerPixel();
  if (count == 256 && bits == 16) {
    const u8 *data = static_cast<const u8 *>(palette->GetPaletteArray());
    u8 rgb[768] = {};
    for (i32 i = 0; i < 256; ++i) {
      u16 value;
      memcpy(&value, data + 2 * i, 2);
      RsGetPaletteGraphics()->Convert16BitNativeValueToRGB(value, rgb + 3 * i, rgb + 3 * i + 1, rgb + 3 * i + 2);
    }
    result = stream->WriteBytes(rgb, sizeof(rgb));
  }
  palette->Release();
  return result;
}

// Read the raw RGB file before allocating colors, then ignore allocation/store replies as the source does.
// Each color input is an actual DWORD containing the graphics converter's zero-extended low word.
static bool SC3_STDCALL RsLoadPaletteFile(GxPaletteView *palette, const ScStringView *name) {
  RsFile file(*name);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u8 rgb[768];
  if (!file.Read(rgb, sizeof(rgb))) {
    file.Close();
    return false;
  }
  file.Close();
  palette->MakeGenericPalette(256, 16);
  for (i32 i = 0; i < 256; ++i) {
    u32 value = RsGetPaletteGraphics()->ConvertRGBValueToNative16Bit(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    palette->SetColor(i, value);
  }
  return true;
}

// Return an independently retained ordinary palette on success, clearing output and destroying it after file failure.
u8 RsPaletteFactory::LoadFile(const ScStringView *name, GxPaletteView **out) {
  ScString copy(*name);
  GxPalette *palette = new GxPalette();
  if (RsLoadPaletteFile(palette, &copy)) {
    *out = palette;
    palette->AddRef();
    return 1;
  }
  delete palette;
  *out = nullptr;
  return 0;
}

// Return the options view of the zero-reference palette factory registered for class5E3.
ScUnknown *RsCreateClass5E3() {
  return static_cast<RsPaletteLoader *>(new RsPaletteFactory());
}

RsPropertyFactory::RsPropertyFactory() {
}

bool RsPropertyFactory::New(u32 type, u32 iid, void **out) {
  return RsGetCom()->GetClassObject(type, iid, out);
}

// Read the real format before constructing property data, then load the resource through its adjusted property view.
// Preserve the successful-query result for a zero format and release both live objects in the original order.
bool RsPropertyFactory::SerialRead(RsResource *resource, RsStream *stream) {
  GePropertyObject *object = nullptr;
  u8 result = resource->QueryInterface(IID_GePropertyObject, reinterpret_cast<void **>(&object));
  if (result) {
    u32 format = ReadCookie(stream);
    if (format) {
      RsProperties *properties = new RsProperties();
      properties->AddRef();
      result = properties->ReadWithFormat(stream, format);
      if (result)
        result = object->LoadProperties(properties);
      properties->Release();
    }
  }
  if (object)
    object->Release();
  return result;
}

// Choose the writable format from the resource's property flag, then populate data through its genuine save method.
// Final release serializes before releasing the queried resource view. Write failures preserve the save result.
bool RsPropertyFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  GePropertyObject *object = nullptr;
  u8 result = resource->QueryInterface(IID_GePropertyObject, reinterpret_cast<void **>(&object));
  if (result) {
    u8 text = object->GetPropertyFlag();
    RsProperties *properties = new RsProperties();
    properties->AddRef();
    result = text ? properties->InitText(stream) : properties->InitBinary(stream);
    if (result)
      result = object->SaveProperties(properties);
    properties->Release();
  }
  if (object)
    object->Release();
  return result;
}

u32 RsPropertyFactory::ReadCookie(RsStream *stream) {
  u32 format = 0;
  return stream->ReadU32(&format) ? format : 0;
}

// Return the original zero-reference primary factory interface registered for class02AE197F.
ScUnknown *RsCreateClass2AE197F() {
  return static_cast<RsFactoryInterface *>(new RsPropertyFactory());
}

RsSerializableFactory::RsSerializableFactory() {
}

bool RsSerializableFactory::New(u32 type, u32 iid, void **out) {
  return RsGetCom()->GetClassObject(type, iid, out);
}

// Read through the queried serialized interface and preserve its Boolean result across Release. A false QI ignores its output.
bool RsSerializableFactory::SerialRead(RsResource *resource, RsStream *stream) {
  RsSerializable *reader = nullptr;
  u8 result = resource->QueryInterface(IID_RsSerializable, reinterpret_cast<void **>(&reader));
  if (result) {
    result = reader->Read(stream);
    reader->Release();
  }
  return result;
}

// Invoke the actual serialized output interface and preserve its Boolean result across Release; failed QI ignores its output.
bool RsSerializableFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  RsSerializable *writer = nullptr;
  u8 result = resource->QueryInterface(IID_RsSerializable, reinterpret_cast<void **>(&writer));
  if (result) {
    result = writer->Write(stream);
    writer->Release();
  }
  return result;
}

// Return the zero-reference primary factory interface registered for class1A1E8FFC.
ScUnknown *RsCreateClass1A1E8FFC() {
  return static_cast<RsFactoryInterface *>(new RsSerializableFactory());
}

RsServiceBase::RsServiceBase(u32 identifier, i32 priority)
    : identifier(identifier), priority(priority), serviceRunning(false) {
}

RsServiceBase::~RsServiceBase() {
}

bool RsServiceBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return RsModuleObject::QueryInterface(iid, out);
}

u32 RsServiceBase::AddRef() {
  return RsModuleObject::AddRef();
}

u32 RsServiceBase::Release() {
  return RsModuleObject::Release();
}

u32 RsServiceBase::GetServiceID() const {
  return identifier;
}

void RsServiceBase::SetServiceID(u32 value) {
  identifier = value;
}

i32 RsServiceBase::GetServicePriority() const {
  return priority;
}

bool RsServiceBase::IsServiceRunning() const {
  return serviceRunning;
}

void RsServiceBase::SetServiceRunning(bool value) {
  serviceRunning = value;
}

bool RsServiceBase::Init() {
  return true;
}

bool RsServiceBase::Shutdown() {
  return true;
}

bool RsServiceBase::OnTick(u32) {
  return true;
}

bool RsServiceBase::OnIdle(u32) {
  return true;
}

bool RsServiceBase::OnInstall() {
  return true;
}

RsTextFactory::RsTextFactory() {
}

static u32 RsTextResourcePage() {
  ScLanguageManager *manager = RsGetLanguageManager();
  return manager ? ScLanguageCrtCodePage(manager->GetCurrentLanguage()) : 0;
}

// Allocate only the supported String resource type. A failed interface query leaves the zero-reference allocation alive.
bool RsTextFactory::New(u32 type, u32 iid, void **out) {
  if (type != kRsTextResourceType)
    return false;
  return (new RsString())->QueryInterface(iid, out);
}

// Check the original mutable Type view before querying String. SDL converts installed text to UTF-8 after reading.
bool RsTextFactory::SerialRead(RsResource *resource, RsStream *stream) {
  if (*resource->Type() != kRsTextResourceType)
    return 0;
  ScStringView *text;
  if (!resource->QueryInterface(IID_ScStringView, reinterpret_cast<void **>(&text)))
    return 0;
  bool result = RsReadLegacyText(stream, text, RsTextResourcePage());
  text->Release();
  return result;
}

// Check the live Type view and release the queried String after output. SDL encodes UTF-8 to the installed page first;
// the original stream result remains ignored.
bool RsTextFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  if (*resource->Type() != kRsTextResourceType)
    return 0;
  ScStringView *text;
  if (!resource->QueryInterface(IID_ScStringView, reinterpret_cast<void **>(&text)))
    return 0;
  ScString encoded;
  if (!ScEncodeLegacyText(RsTextResourcePage(), *text, &encoded)) {
    text->Release();
    return false;
  }
  stream->WriteString(&encoded);
  text->Release();
  return 1;
}

// Allocate the concrete zero-reference factory returned by the resource director's registered constructor.
ScUnknown *RsCreateTextFactory() {
  return static_cast<RsFactoryInterface *>(new RsTextFactory());
}
