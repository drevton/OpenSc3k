#include "common/value.h"

#include <string.h>

#include <cassert>

#include "application/application_core.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "economy/ec_module.h"


#if !defined(SC3_MONOLITHIC)
// Expose the module's lazily constructed director through the original DLL loader entry.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return ScGetVariableDirector();
}
#endif

// The original factories and startup hook remain explicit dependencies until their complete contracts are recovered.
ScUnknown *ScCreateVariableStore();
ScUnknown *ScCreateVariableCommand();

// The installed module director registers the variable service and the game-command factory.
class ScVariableDirector : public ScDllDirector {
public:
  ScVariableDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

ScVariableDirector::ScVariableDirector() {
  AddClass(kScVariableStoreClassId, ScCreateVariableStore, 0);
  AddClass(kScVariableCommandFactoryClassId, ScCreateVariableCommand, 0);
}

u32 ScVariableDirector::GetDirectorID() const {
  return kScVariableDirectorId;
}

// Install the module's system service, then register its command factory through the existing framework service.
// Release each temporary interface in the original order after its corresponding registration call.
bool ScVariableDirector::OnStart(ScCom *) {
  ScSystemService *service = nullptr;
  GZCOM()->GetClassObject(kScVariableStoreClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    ScGetVariableDirector()->FrameWork()->AddSystemService(service);
    service->Release();
  }
  ScVariableCommandFactory *factory = new ScVariableCommandFactory;
  ScCommandFactory *child = nullptr;
  if (factory->QueryInterface(IID_ScCommandFactory, reinterpret_cast<void **>(&child))) {
    ScCommandFactory *manager = nullptr;
    ScGetVariableDirector()->FrameWork()->GetSystemService(
        kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&manager));
    manager->AddCommandFactory(child);
    if (manager)
      manager->Release();
  }
  factory->Release();
  return true;
}

// Register the original service and command-factory aliases, attempting each independently.
// A rejected name or registration does not prevent the second alias or change the success result.
bool ScVariableDirector::AppRegister(ScRegistry *registry) {
  ScString name, key, value;
  key.FromChar("0xa4232f1e");
  value.FromChar("1");
  if (registry->MakeRegistryName("sysser", key, value, &name))
    registry->Register(name);
  key.FromChar("0x24242600");
  value.FromChar("1");
  if (registry->MakeRegistryName("cogamecmd", key, value, &name))
    registry->Register(name);
  return true;
}

// Return the module's lazily constructed director used by its reference-count identities.
ScDirector *ScGetVariableDirector() {
  static ScVariableDirector director;
  return &director;
}

u32 ScParseVariableNumber(const char *text) {
  return ScConvertToUint32(text);
}

// Parse the value before the key, construct the original DWORD variant, then pass it to the store insertion operation.
void ScReadVariableTuning(const ScString &key, const ScString &value, ScVariableStore *store) {
  u32 number = ScParseVariableNumber(value.ToChar());
  ScValue parsed(number, 3);
  u32 identifier = ScParseVariableNumber(key.ToChar());
  store->StoreVariable(identifier, parsed);
}

static void ScVariableIniCallback(const ScString &key, const ScString &value, void *context) {
  ScReadVariableTuning(key, value, static_cast<ScVariableStore *>(context));
}

// Cache the application identity once, publishing the initialization guard before querying the framework application.
static ApGameInterface *ScGetVariableApplication() {
  static bool initialized = false;
  static ApGameInterface *application = nullptr;
  if (!initialized) {
    initialized = true;
    ScGetVariableDirector()->FrameWork()->Application()->QueryInterface(
        IID_ApGameInterface, reinterpret_cast<void **>(&application));
  }
  return application;
}

// Borrow the message service from the framework, retrying a missing service on a later call.
static ScMessageServer *ScGetVariableMessages() {
  static ScMessageServer *messages = nullptr;
  if (!messages) {
    ScMessageServer *acquired = nullptr;
    ScGetVariableDirector()->FrameWork()->GetSystemService(
        kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&acquired));
    messages = acquired;
    if (acquired)
      acquired->Release();
  }
  return messages;
}

ScVariableReference::ScVariableReference() : references(0) {
}

bool ScVariableReference::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScVariableReference::AddRef() {
  ScGetVariableDirector()->AddRef();
  return ++references;
}

// Select destruction from the entry count, then release the module. Other counts decrement the live value with wraparound.
u32 ScVariableReference::Release() {
  if (references == 1) {
    ScGetVariableDirector()->RemoveRef();
    delete this;
    return 0;
  }
  ScGetVariableDirector()->RemoveRef();
  return --references;
}

ScVariableReference::~ScVariableReference() {
}

// Remove a module hold and decrement only when the current count is nonzero.
u32 ScVariableReference::RemoveRef() {
  if (references) {
    ScGetVariableDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScVariableReference::RefCount() {
  return references;
}

ScVariableService::ScVariableService(u32 service_id, i32 service_priority)
    : identifier(service_id), priority(service_priority), running(false) {
}

ScVariableService::~ScVariableService() {
}

bool ScVariableService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return ScVariableReference::QueryInterface(iid, out);
}

u32 ScVariableService::AddRef() {
  return ScVariableReference::AddRef();
}

u32 ScVariableService::Release() {
  return ScVariableReference::Release();
}

u32 ScVariableService::GetServiceID() const {
  return identifier;
}

void ScVariableService::SetServiceID(u32 service_id) {
  identifier = service_id;
}

i32 ScVariableService::GetServicePriority() const {
  return priority;
}

bool ScVariableService::IsServiceRunning() const {
  return running;
}

void ScVariableService::SetServiceRunning(bool state) {
  running = state;
}

bool ScVariableService::Init() {
  return true;
}

bool ScVariableService::Shutdown() {
  return true;
}

bool ScVariableService::OnTick(u32) {
  return true;
}

bool ScVariableService::OnIdle(u32) {
  return true;
}

bool ScVariableService::OnInstall() {
  return true;
}

ScVariableStore::ScVariableStore() : ScVariableService(kScVariableManagerServiceId, 0) {
}

ScVariableStore::~ScVariableStore() {
}

ScVariableEntry::ScVariableEntry(const ScVariableEntry &source) : key(source.key), value(source.value) {
}

// Construct the variable store and expose its original primary manager identity without an extra retain.
ScUnknown *ScCreateVariableStore() {
  return static_cast<ScVariableManager *>(new ScVariableStore);
}

// Register the tuneup cheat and subscribe the store, then pass the real SimTune.INI path to its loading operation.
// An unavailable application prevents registration and reports failure before any other callback.
bool ScVariableStore::Init() {
  if (!ScGetVariableApplication())
    return false;
  {
    ScString name("tuneup");
    ScGetVariableApplication()->GetCheatCodeManager()->RegisterCheatCode(kScCheat_TuneUp, name);
  }
  ScGetVariableApplication()->GetCheatCodeManager()->AddNotification(
      static_cast<ScMessageTarget *>(this), kScCheat_TuneUp);
  ScString path;
  ScGetVariableApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SimTune.INI");
  LoadTuning(path);
  return true;
}

bool ScVariableStore::Shutdown() {
  ScGetVariableApplication()->GetCheatCodeManager()->RemoveNotification(
      static_cast<ScMessageTarget *>(this), kScCheat_TuneUp);
  ScGetVariableApplication()->GetCheatCodeManager()->UnregisterCheatCode(kScCheat_TuneUp);
  return true;
}

// Copy the source path, select it in the packaged INI reader, and select the original SYS.PAK under the data directory.
u8 ScVariableStore::LoadTuning(const ScStringView &path) {
  {
    ScString copy(path);
    source_path = copy;
  }
  ScString package_path;
  EcIni ini;
  ini.SelectFile(&source_path);
  ScGetVariableApplication()->GetAppDataDirectory(&package_path);
  package_path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(package_path);
  ScString section("Tunables");
  ini.EnumerateSection(&section, ScVariableIniCallback, this);
  return 1;
}

// Execute the original tuneup command for the matching cheat notification and release both acquired interfaces.
// Other cheat notifications are declined; unrelated messages are accepted without further action.
bool ScVariableStore::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CheatCommand)
    return true;
  if (u32(message->data1) != kScCheat_TuneUp)
    return false;
  ScCommandFactory *manager = nullptr;
  ScGetVariableDirector()->FrameWork()->GetSystemService(
      kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&manager));
  UiCommandInterface *command = nullptr;
  if (manager->GetCommand(kScTuneUpCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    command->Execute();
    command->Release();
  }
  manager->Release();
  return true;
}

// Publish the variable-manager identity when requested, and otherwise delegate to the actual service identity.
bool ScVariableStore::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScVariableManager)
    return ScVariableService::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScVariableManager *>(this);
  AddRef();
  return true;
}

u32 ScVariableStore::AddRef() {
  return ScVariableService::AddRef();
}

u32 ScVariableStore::Release() {
  return ScVariableService::Release();
}

// Borrow the embedded system-service view without acquiring another reference.
ScSystemService *ScVariableStore::AsSystemService() {
  return static_cast<ScVariableService *>(this);
}

// Search entries in their original order and report whether the requested key exists.
u8 ScVariableStore::HasVariable(u32 key) {
  for (const ScVariableEntry &entry : entries)
    if (entry.key == key)
      return 1;
  return 0;
}

// Copy the first matching value through the concrete variant's virtual constructor; a miss leaves the output untouched.
u8 ScVariableStore::GetVariable(u32 key, ScValue *out) {
  for (const ScVariableEntry &entry : entries) {
    if (entry.key == key) {
      *out = entry.value;
      return 1;
    }
  }
  return 0;
}

// Borrow the retained source string without copying it or acquiring a reference.
ScStringView *ScVariableStore::GetSourcePath() {
  return &source_path;
}

// Replace the first matching value, or append and notify listeners before the original temporary retires.
// The shallow array copy violates ownership when the temporary retires; diagnose that after the original notification.
u8 ScVariableStore::StoreVariable(u32 key, const ScVariant &source) {
  for (ScVariableEntry &entry : entries) {
    if (entry.key == key) {
      entry.value = &source;
      return 1;
    }
  }
  ScVariableEntry entry;
  entry.key = key;
  entry.value = &source;
  if (entries.size() == entries.capacity())
    entries.reserve(entries.empty() ? 1 : entries.size() * 2);
  entries.push_back(entry);
  ScMessageServer *messages = ScGetVariableMessages();
  if (messages)
    messages->MessagePost(ScMessage(kScMsg_VariableAdded, key, 0, 0), false);
  assert(!(entry.value.type & kScValueOwnedArray));
  return 1;
}

ScVariableFactoryBase::ScVariableFactoryBase() : references(0) {
}

ScVariableFactoryBase::~ScVariableFactoryBase() {
  auto entry = children.begin(), end = children.end();
  while (entry != end && *entry) {
    (*entry)->Release();
    *entry++ = nullptr;
  }
}

bool ScVariableFactoryBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the module, then return the live count after that callback.
u32 ScVariableFactoryBase::AddRef() {
  ++references;
  ScGetVariableDirector()->AddRef();
  return references;
}

// Decrement first, release the module, and destroy the factory if its live count has reached zero.
u32 ScVariableFactoryBase::Release() {
  --references;
  ScGetVariableDirector()->RemoveRef();
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Ask retained child factories in order, falling back to the concrete factory only when all decline.
bool ScVariableFactoryBase::GetCommand(u32 class_id, u32 iid, void **out) {
  for (usize index = 0; index < children.size(); ++index)
    if (children[index]->GetCommand(class_id, iid, out))
      return true;
  return CreateCommand(class_id, iid, out);
}

// Retain a non-null child before appending it to the factory's original ordered vector.
bool ScVariableFactoryBase::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  children.push_back(factory);
  return true;
}

ScVariableCommandFactory::~ScVariableCommandFactory() {
}

bool ScVariableCommandFactory::CreateCommand(u32, u32, void **out) {
  *out = nullptr;
  return false;
}

// Construct the module's original command factory without acquiring an extra reference.
ScUnknown *ScCreateVariableCommand() {
  return static_cast<ScCommandFactory *>(new ScVariableCommandFactory);
}

// Preserve finite float representations even when the caller enables SSE denormal flushing.
static f32 ScVariantTransport32(f32 data) {
  u32 bits;
  memcpy(&bits, &data, sizeof(bits));
  if ((bits & kScFloatExponentQuietMask) != kScFloatInfinityMagnitude || !(bits & kScFloatNaNPayloadMask))
    return data;
  volatile f32 identity = 1.0f;
  return data * identity;
}

// Transfer finite doubles directly so SSE denormal controls cannot change the stored representation.
// Materialize signaling NaNs through native arithmetic, retaining their sign and payload while quieting them.
static f64 ScVariantTransport64(f64 data) {
  u64 bits;
  memcpy(&bits, &data, sizeof(bits));
  if ((bits & 0x7ff8000000000000ull) != 0x7ff0000000000000ull || !(bits & 0x0007ffffffffffffull))
    return data;
  volatile f64 identity = 1.0;
  return data * identity;
}

// Start an empty variant with zero payload, array count and references.
ScValue::ScValue() {
  type = kScValueEmpty;
  value.bits = 0;
  count = 0;
  references = 0;
}

// Copy the original concrete variant data and local references without retaining or cloning a payload.
// Copy typed fields separately so native structure padding is neither consumed nor turned into game state.
ScValue::ScValue(const ScValue &source) : references(source.references) {
  type = source.type;
  value = source.value;
  count = source.count;
}

// Store the original DWORD and full type tag. Pointer-bearing tags widen that original address input only.
ScValue::ScValue(u32 input, u32 kind) {
  references = 0;
  count = 0;
  type = kind;
  if ((kind & 0x8000) || kind == 0x7fff)
    value.pointer = reinterpret_cast<void *>(uptr(input));
  else
    value.word = input;
}

ScValue::ScValue(bool input) {
  references = 0;
  count = 0;
  type = kScValueBool;
  value.boolean = input;
}

// Capture String length before data and copy exactly those bytes, without allocating a terminator.
ScValue::ScValue(const ScStringView &text) {
  references = 0;
  type = kScValueEmpty;
  count = 0;
  value.word = 0;
  u32 size = text.Strlen();
  const char *data = text.Data();
  ScValue::RefRZChar(data, size);
}

// Begin empty and retain the supplied object through the original object assignment operation.
ScValue::ScValue(ScUnknown *object) {
  references = 0;
  type = kScValueEmpty;
  count = 0;
  SetValueUnknown(object);
}

ScValue::ScValue(const ScVariant &source) : ScValue() {
  ScValue::Ctor(&source);
}

ScValue::ScValue(const ScVariant *source) : ScValue() {
  ScValue::Ctor(source);
}

// Publish the supported Unknown or Variant view before its virtual retain; other identifiers leave output alone.
bool ScValue::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScVariant);
  HANDLE_QUERY_INTERFACE(IID_ScVariant, ScVariant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScValue::AddRef() {
  return ++references;
}

// Delete only at exactly one reference. Releasing zero wraps the unsigned count.
u32 ScValue::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

u32 ScValue::GetType() const {
  return type;
}

u32 ScValue::GetCount() const {
  return count;
}

// Dispatch live cleanup before zeroing all eight payload bytes and reporting success.
bool ScValue::Erase() {
  Dtor();
  value.bits = 0;
  return true;
}

bool ScValue::CopyFrom(const ScVariant &source) {
  Ctor(&source);
  return true;
}

// Create a retained copy and publish it only after its reference increment.
bool ScValue::CreateCopy(ScVariant **out) const {
  ScValue *copy = new ScValue(static_cast<const ScVariant *>(this));
  copy->AddRef();
  *out = copy;
  return true;
}

bool ScValue::GetValUint8(u8 *out) const {
  return GetValueUint8(out);
}

bool ScValue::SetValUint8(u8 input) {
  return SetValueUint8(input);
}

bool ScValue::GetValUint16(u16 *out) const {
  return GetValueUint16(out);
}

bool ScValue::SetValUint16(u16 input) {
  return SetValueUint16(input);
}

bool ScValue::GetValUint32(u32 *out) const {
  return GetValueUint32(out);
}

bool ScValue::SetValUint32(u32 input) {
  return SetValueUint32(input);
}

bool ScValue::GetValUint64(u64 *out) const {
  return GetValueUint64(out);
}

bool ScValue::SetValUint64(u64 input) {
  return SetValueUint64(input);
}

bool ScValue::GetValSint8(i8 *out) const {
  return GetValueSint8(out);
}

bool ScValue::SetValSint8(i8 input) {
  return SetValueSint8(input);
}

bool ScValue::GetValSint16(i16 *out) const {
  return GetValueSint16(out);
}

bool ScValue::SetValSint16(i16 input) {
  return SetValueSint16(input);
}

bool ScValue::GetValSint32(i32 *out) const {
  return GetValueSint32(out);
}

bool ScValue::SetValSint32(i32 input) {
  return SetValueSint32(input);
}

bool ScValue::GetValSint64(i64 *out) const {
  return GetValueSint64(out);
}

bool ScValue::SetValSint64(i64 input) {
  return SetValueSint64(input);
}

bool ScValue::GetValFloat32(f32 *out) const {
  return GetValueFloat32(out);
}

bool ScValue::SetValFloat32(f32 input) {
  return SetValueFloat32(ScVariantTransport32(input));
}

bool ScValue::GetValFloat64(f64 *out) const {
  return GetValueFloat64(out);
}

bool ScValue::SetValFloat64(f64 input) {
  return SetValueFloat64(ScVariantTransport64(input));
}

bool ScValue::GetValBool(bool *out) const {
  return GetValueBool(out);
}

bool ScValue::SetValBool(bool input) {
  return SetValueBool(input);
}

bool ScValue::GetValRZChar(char *out) const {
  return GetValueRZChar(out);
}

bool ScValue::SetValRZChar(char input) {
  return SetValueRZChar(input);
}

// Allocate a String and copy the character array through the concrete getter.
// Retain before publication and release afterward.
bool ScValue::CreateValString(ScStringView **out) const {
  if (!out)
    return false;
  ScString *text = new ScString;
  text->AddRef();
  bool result = GetValueString(text);
  if (result) {
    text->AddRef();
    *out = text;
  }
  text->Release();
  return result;
}

bool ScValue::GetValString(ScStringView *out) const {
  return GetValueString(out);
}

bool ScValue::SetValString(const ScStringView &text) {
  return SetValueString(text);
}

bool ScValue::GetValUnknown(u32 iid, void **out) {
  return GetValueUnknown(iid, out);
}

bool ScValue::SetValUnknown(ScUnknown *object) {
  return SetValueUnknown(object);
}

// Borrow the stored Uint8 scalar reference without a tag check, conversion or copy.
const u8 *ScValue::AsUint8() const {
  return &value.unsigned8;
}

// Borrow the stored Uint16 scalar reference without a tag check, conversion or copy.
const u16 *ScValue::AsUint16() const {
  return &value.unsigned16;
}

// Borrow the stored Uint32 scalar reference without a tag check, conversion or copy.
const u32 *ScValue::AsUint32() const {
  return &value.word;
}

// Borrow the stored Uint64 scalar reference without a tag check, conversion or copy.
const u64 *ScValue::AsUint64() const {
  return &value.bits;
}

// Borrow the stored Sint8 scalar reference without a tag check, conversion or copy.
const i8 *ScValue::AsSint8() const {
  return &value.signed8;
}

// Borrow the stored Sint16 scalar reference without a tag check, conversion or copy.
const i16 *ScValue::AsSint16() const {
  return &value.signed16;
}

// Borrow the stored Sint32 scalar reference without a tag check, conversion or copy.
const i32 *ScValue::AsSint32() const {
  return &value.integer;
}

// Borrow the stored Sint64 scalar reference without a tag check, conversion or copy.
const i64 *ScValue::AsSint64() const {
  return &value.signed64;
}

// Borrow the stored Float32 scalar reference without a tag check, conversion or copy.
const f32 *ScValue::AsFloat32() const {
  return &value.real32;
}

// Borrow the stored Float64 scalar reference without a tag check, conversion or copy.
const f64 *ScValue::AsFloat64() const {
  return &value.real64;
}

// Borrow the stored Bool scalar reference without a tag check, conversion or copy.
const bool *ScValue::AsBool() const {
  return &value.boolean;
}

// Borrow the stored RZChar scalar reference without a tag check, conversion or copy.
const char *ScValue::AsRZChar() const {
  return &value.character;
}

// Replace storage with an owned Uint8 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefUint8(const u8 *data, u32 amount) {
  Dtor();
  u32 bytes = amount;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueUint8Array;
}

// Borrow the stored Uint8 array pointer without retaining or checking its tag.
u8 *ScValue::RefUint8() const {
  return static_cast<u8 *>(value.pointer);
}

// Replace storage with an owned Uint16 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefUint16(const u16 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 2;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueUint16Array;
}

// Borrow the stored Uint16 array pointer without retaining or checking its tag.
u16 *ScValue::RefUint16() const {
  return static_cast<u16 *>(value.pointer);
}

// Replace storage with an owned Uint32 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefUint32(const u32 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 4;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueUint32Array;
}

// Borrow the stored Uint32 array pointer without retaining or checking its tag.
u32 *ScValue::RefUint32() const {
  return static_cast<u32 *>(value.pointer);
}

// Replace storage with an owned Uint64 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefUint64(const u64 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 8;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueUint64Array;
}

// Borrow the stored Uint64 array pointer without retaining or checking its tag.
u64 *ScValue::RefUint64() const {
  return static_cast<u64 *>(value.pointer);
}

// Replace storage with an owned Sint8 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefSint8(const i8 *data, u32 amount) {
  Dtor();
  u32 bytes = amount;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueSint8Array;
}

// Borrow the stored Sint8 array pointer without retaining or checking its tag.
i8 *ScValue::RefSint8() const {
  return static_cast<i8 *>(value.pointer);
}

// Replace storage with an owned Sint16 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefSint16(const i16 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 2;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueSint16Array;
}

// Borrow the stored Sint16 array pointer without retaining or checking its tag.
i16 *ScValue::RefSint16() const {
  return static_cast<i16 *>(value.pointer);
}

// Replace storage with an owned Sint32 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefSint32(const i32 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 4;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueSint32Array;
}

// Borrow the stored Sint32 array pointer without retaining or checking its tag.
i32 *ScValue::RefSint32() const {
  return static_cast<i32 *>(value.pointer);
}

// Replace storage with an owned Sint64 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefSint64(const i64 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 8;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueSint64Array;
}

// Borrow the stored Sint64 array pointer without retaining or checking its tag.
i64 *ScValue::RefSint64() const {
  return static_cast<i64 *>(value.pointer);
}

// Replace storage with an owned Float32 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefFloat32(const f32 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 4;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueFloat32Array;
}

// Borrow the stored Float32 array pointer without retaining or checking its tag.
f32 *ScValue::RefFloat32() const {
  return static_cast<f32 *>(value.pointer);
}

// Replace storage with an owned Float64 array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefFloat64(const f64 *data, u32 amount) {
  Dtor();
  u32 bytes = amount * 8;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueFloat64Array;
}

// Borrow the stored Float64 array pointer without retaining or checking its tag.
f64 *ScValue::RefFloat64() const {
  return static_cast<f64 *>(value.pointer);
}

// Replace storage with an owned Bool array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefBool(const bool *data, u32 amount) {
  Dtor();
  u32 bytes = amount;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueBoolArray;
}

// Borrow the stored Bool array pointer without retaining or checking its tag.
bool *ScValue::RefBool() const {
  return static_cast<bool *>(value.pointer);
}

// Replace storage with an owned RZChar array. Byte-size arithmetic retains its original unsigned 32-bit width.
void ScValue::RefRZChar(const char *data, u32 amount) {
  Dtor();
  u32 bytes = amount;
  value.pointer = new u8[bytes];
  memcpy(value.pointer, data, bytes);
  count = amount;
  type = kScValueCharArray;
}

// Borrow the stored RZChar array pointer without retaining or checking its tag.
char *ScValue::RefRZChar() const {
  return static_cast<char *>(value.pointer);
}

// Copy the original 16-byte Rect record into owned storage. The original does not assign an element count.
void ScValue::RefRect(const ScRect *data) {
  Dtor();
  value.pointer = new u8[16];
  memcpy(value.pointer, data, 16);
  type = kScValueRect;
}

// Borrow the stored Rect pointer without a tag check or ownership change.
ScRect *ScValue::RefRect() const {
  return static_cast<ScRect *>(value.pointer);
}

// Copy the original 8-byte Point record into owned storage. The original does not assign an element count.
void ScValue::RefPoint(const ScPoint *data) {
  Dtor();
  value.pointer = new u8[8];
  memcpy(value.pointer, data, 8);
  type = kScValuePoint;
}

// Borrow the stored Point pointer without a tag check or ownership change.
ScPoint *ScValue::RefPoint() const {
  return static_cast<ScPoint *>(value.pointer);
}

// Copy the original 8-byte Size record into owned storage. The original does not assign an element count.
void ScValue::RefSize(const ScSize *data) {
  Dtor();
  value.pointer = new u8[8];
  memcpy(value.pointer, data, 8);
  type = kScValueSize;
}

// Borrow the stored Size pointer without a tag check or ownership change.
ScSize *ScValue::RefSize() const {
  return static_cast<ScSize *>(value.pointer);
}

// Replace storage with an owned byte copy and publish its byte count and untyped array tag.
void ScValue::RefData(void *data, u32 size) {
  Dtor();
  value.pointer = new u8[size];
  memcpy(value.pointer, data, size);
  count = size;
  type = kScValueData;
}

// Borrow the raw stored pointer without inspecting the pointed-to bytes.
void *ScValue::RefData() const {
  return value.pointer;
}

void ScValue::RefUnknown(ScUnknown *object) {
  SetValueUnknown(object);
}

bool ScValue::RefUnknown(u32 iid, void **out) {
  return GetValueUnknown(iid, out);
}

// Borrow the stored object pointer without a retain or type check.
ScUnknown *ScValue::RefUnknown() const {
  return static_cast<ScUnknown *>(value.pointer);
}

ScValue::~ScValue() {
  ScValue::Dtor();
}

// Copy through the original source interface, preserving direct scalar assignment and virtual array callbacks.
// Unsupported tags preserve the destination; count is read before each source array pointer.
void ScValue::Ctor(const ScVariant *source) {
  if (!source) {
    Dtor();
    return;
  }
  u32 kind = source->GetType();
  switch (kind) {
  case kScValueEmpty:
    Dtor();
    break;
  case kScValueUint8: {
    u8 input = *source->AsUint8();
    SetValueUint8(input);
    break;
  }
  case kScValueUint16: {
    u16 input = *source->AsUint16();
    SetValueUint16(input);
    break;
  }
  case kScValueUint32: {
    u32 input = *source->AsUint32();
    SetValueUint32(input);
    break;
  }
  case kScValueUint64: {
    u64 input = *source->AsUint64();
    SetValueUint64(input);
    break;
  }
  case kScValueSint8: {
    i8 input = *source->AsSint8();
    SetValueSint8(input);
    break;
  }
  case kScValueSint16: {
    i16 input = *source->AsSint16();
    SetValueSint16(input);
    break;
  }
  case kScValueSint32: {
    i32 input = *source->AsSint32();
    SetValueSint32(input);
    break;
  }
  case kScValueSint64: {
    i64 input = *source->AsSint64();
    SetValueSint64(input);
    break;
  }
  case kScValueFloat32: {
    f32 input = *source->AsFloat32();
    SetValueFloat32(ScVariantTransport32(input));
    break;
  }
  case kScValueFloat64: {
    f64 input = *source->AsFloat64();
    SetValueFloat64(ScVariantTransport64(input));
    break;
  }
  case kScValueBool: {
    bool input = *source->AsBool();
    SetValueBool(input);
    break;
  }
  case kScValueChar: {
    char input = *source->AsRZChar();
    SetValueRZChar(input);
    break;
  }
  case kScValueUnknown:
    SetValueUnknown(source->RefUnknown());
    break;
  case kScValueData: {
    u32 size = source->GetCount();
    void *data = source->RefData();
    RefData(data, size);
    break;
  }
  case kScValueUint8Array: {
    u32 size = source->GetCount();
    u8 *data = source->RefUint8();
    RefUint8(data, size);
    break;
  }
  case kScValueUint16Array: {
    u32 size = source->GetCount();
    u16 *data = source->RefUint16();
    RefUint16(data, size);
    break;
  }
  case kScValueUint32Array: {
    u32 size = source->GetCount();
    u32 *data = source->RefUint32();
    RefUint32(data, size);
    break;
  }
  case kScValueUint64Array: {
    u32 size = source->GetCount();
    u64 *data = source->RefUint64();
    RefUint64(data, size);
    break;
  }
  case kScValueSint8Array: {
    u32 size = source->GetCount();
    i8 *data = source->RefSint8();
    RefSint8(data, size);
    break;
  }
  case kScValueSint16Array: {
    u32 size = source->GetCount();
    i16 *data = source->RefSint16();
    RefSint16(data, size);
    break;
  }
  case kScValueSint32Array: {
    u32 size = source->GetCount();
    i32 *data = source->RefSint32();
    RefSint32(data, size);
    break;
  }
  case kScValueSint64Array: {
    u32 size = source->GetCount();
    i64 *data = source->RefSint64();
    RefSint64(data, size);
    break;
  }
  case kScValueFloat32Array: {
    u32 size = source->GetCount();
    f32 *data = source->RefFloat32();
    RefFloat32(data, size);
    break;
  }
  case kScValueFloat64Array: {
    u32 size = source->GetCount();
    f64 *data = source->RefFloat64();
    RefFloat64(data, size);
    break;
  }
  case kScValueBoolArray: {
    u32 size = source->GetCount();
    bool *data = source->RefBool();
    RefBool(data, size);
    break;
  }
  case kScValueCharArray: {
    u32 size = source->GetCount();
    char *data = source->RefRZChar();
    RefRZChar(data, size);
    break;
  }
  case kScValueRect:
    RefRect(source->RefRect());
    break;
  case kScValuePoint:
    RefPoint(source->RefPoint());
    break;
  case kScValueSize:
    RefSize(source->RefSize());
    break;
  }
}

// Retire owned arrays or release the still-published object, then clear the tag. Scalar payload bytes remain untouched.
void ScValue::Dtor() {
  if (type & kScValueOwnedArray) {
    delete[] static_cast<u8 *>(value.pointer);
    value.pointer = nullptr;
    count = 0;
  } else if (type == kScValueUnknown) {
    ScUnknown *object = static_cast<ScUnknown *>(value.pointer);
    if (object)
      object->Release();
    value.pointer = nullptr;
  }
  type = kScValueEmpty;
}

// Copy the Uint8 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueUint8(u8 *out) const {
  if (type != kScValueUint8)
    return false;
  *out = value.unsigned8;
  return true;
}

// Retire a live value before publishing the Uint8 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueUint8(u8 input) {
  if (type)
    Dtor();
  value.unsigned8 = input;
  type = kScValueUint8;
  return true;
}

// Copy the Uint16 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueUint16(u16 *out) const {
  if (type != kScValueUint16)
    return false;
  *out = value.unsigned16;
  return true;
}

// Retire a live value before publishing the Uint16 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueUint16(u16 input) {
  if (type)
    Dtor();
  type = kScValueUint16;
  value.unsigned16 = input;
  return true;
}

// Copy the Uint32 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueUint32(u32 *out) const {
  if (type != kScValueUint32)
    return false;
  *out = value.word;
  return true;
}

// Retire a live value before publishing the Uint32 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueUint32(u32 input) {
  if (type)
    Dtor();
  type = kScValueUint32;
  value.word = input;
  return true;
}

// Copy the Uint64 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueUint64(u64 *out) const {
  if (type != kScValueUint64)
    return false;
  *out = value.bits;
  return true;
}

// Retire a live value before publishing the Uint64 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueUint64(u64 input) {
  if (type)
    Dtor();
  type = kScValueUint64;
  value.bits = input;
  return true;
}

// Copy the Sint8 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueSint8(i8 *out) const {
  if (type != kScValueSint8)
    return false;
  *out = value.signed8;
  return true;
}

// Retire a live value before publishing the Sint8 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueSint8(i8 input) {
  if (type)
    Dtor();
  type = kScValueSint8;
  value.signed8 = input;
  return true;
}

// Copy the Sint16 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueSint16(i16 *out) const {
  if (type != kScValueSint16)
    return false;
  *out = value.signed16;
  return true;
}

// Retire a live value before publishing the Sint16 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueSint16(i16 input) {
  if (type)
    Dtor();
  type = kScValueSint16;
  value.signed16 = input;
  return true;
}

// Copy the Sint32 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueSint32(i32 *out) const {
  if (type != kScValueSint32)
    return false;
  *out = value.integer;
  return true;
}

// Retire a live value before publishing the Sint32 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueSint32(i32 input) {
  if (type)
    Dtor();
  type = kScValueSint32;
  value.integer = input;
  return true;
}

// Copy the Sint64 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueSint64(i64 *out) const {
  if (type != kScValueSint64)
    return false;
  *out = value.signed64;
  return true;
}

// Retire a live value before publishing the Sint64 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueSint64(i64 input) {
  if (type)
    Dtor();
  type = kScValueSint64;
  value.signed64 = input;
  return true;
}

// Copy the Float32 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueFloat32(f32 *out) const {
  if (type != kScValueFloat32)
    return false;
  f32 data;
  memcpy(&data, &value, sizeof(data));
  *out = ScVariantTransport32(data);
  return true;
}

// Retire a live value before publishing the Float32 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueFloat32(f32 input) {
  if (type)
    Dtor();
  type = kScValueFloat32;
  value.real32 = input;
  return true;
}

// Copy the Float64 payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueFloat64(f64 *out) const {
  if (type != kScValueFloat64)
    return false;
  f64 data;
  memcpy(&data, &value, sizeof(data));
  *out = ScVariantTransport64(data);
  return true;
}

// Retire a live value before publishing the Float64 scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueFloat64(f64 input) {
  if (type)
    Dtor();
  type = kScValueFloat64;
  value.real64 = ScVariantTransport64(input);
  return true;
}

// Copy the Bool payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueBool(bool *out) const {
  if (type != kScValueBool)
    return false;
  *out = value.boolean;
  return true;
}

// Retire a live value before publishing the Bool scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueBool(bool input) {
  if (type)
    Dtor();
  type = kScValueBool;
  value.boolean = input;
  return true;
}

// Copy the RZChar payload only for its exact scalar tag. A refused conversion leaves its output untouched.
bool ScValue::GetValueRZChar(char *out) const {
  if (type != kScValueChar)
    return false;
  *out = value.character;
  return true;
}

// Retire a live value before publishing the RZChar scalar and its original tag.
// Preserve unrelated count and payload bytes.
bool ScValue::SetValueRZChar(char input) {
  if (type)
    Dtor();
  type = kScValueChar;
  value.character = input;
  return true;
}

// Copy an owned character range into the supplied String only for tag 800C.
bool ScValue::GetValueString(ScStringView *out) const {
  if (type != kScValueCharArray)
    return false;
  out->FromChar(static_cast<const char *>(value.pointer), count);
  return true;
}

// Read length then data and dispatch the live character-array setter.
bool ScValue::SetValueString(const ScStringView &text) {
  u32 size = text.Strlen();
  const char *data = text.Data();
  RefRZChar(data, size);
  return true;
}

// Query the stored nonnull object only for the Unknown tag.
bool ScValue::GetValueUnknown(u32 iid, void **out) {
  if (type != kScValueUnknown || !value.pointer)
    return false;
  return static_cast<ScUnknown *>(value.pointer)->QueryInterface(iid, out);
}

// Retain the incoming object before cleanup and publish it under the Unknown tag.
bool ScValue::SetValueUnknown(ScUnknown *object) {
  if (object)
    object->AddRef();
  if (type)
    Dtor();
  value.pointer = object;
  type = kScValueUnknown;
  return true;
}

// Construct the original u8 payload with its caller-supplied type. Only the represented payload bytes are written.
ScValue::ScValue(u8 input, u32 kind) {
  references = 0;
  count = 0;
  type = kind;
  value.unsigned8 = input;
}

// Construct the original u16 payload with its caller-supplied type. Only the represented payload bytes are written.
ScValue::ScValue(u16 input, u32 kind) {
  references = 0;
  count = 0;
  type = kind;
  value.unsigned16 = input;
}

// Construct the original u64 payload with its caller-supplied type. Only the represented payload bytes are written.
ScValue::ScValue(u64 input, u32 kind) {
  references = 0;
  count = 0;
  type = kind;
  value.bits = input;
}

// Construct scalar type 9 by copying the original single-precision bits without an extra arithmetic conversion.
ScValue::ScValue(f32 input) {
  references = 0;
  count = 0;
  type = kScValueFloat32;
  memcpy(&value.real32, &input, sizeof(input));
}

// Construct scalar type 10 with the original x87 double load/store behavior.
ScValue::ScValue(f64 input) {
  references = 0;
  count = 0;
  type = kScValueFloat64;
  value.real64 = ScVariantTransport64(input);
}

// Copy the null-terminated input up to its terminator into owned character-array storage.
ScValue::ScValue(const char *text) {
  references = 0;
  type = kScValueEmpty;
  count = 0;
  value.word = 0;
  u32 size = static_cast<u32>(strlen(text));
  ScValue::RefRZChar(text, size);
}

ScValue::ScValue(void *data, u32 size, u32 kind) : ScValue() {
  ScValue::RefData(data, size);
  type = kind | kScValueOwnedArray;
}

// Dispatch original copy initialization through the live virtual operation. Return the const-reference carrier.
const ScValue *ScValue::operator=(const ScValue &source) {
  Ctor(&source);
  return this;
}

// Dispatch the nullable source through live copy initialization and return this as a const-reference carrier.
const ScValue *ScValue::operator=(const ScVariant *source) {
  Ctor(source);
  return this;
}

// Copy the input characters through the virtual array setter and return this as a const-reference carrier.
const ScValue *ScValue::operator=(const char *text) {
  u32 size = static_cast<u32>(strlen(text));
  RefRZChar(text, size);
  return this;
}

// Read String length before data, then dispatch the virtual character setter and return this.
const ScValue *ScValue::operator=(const ScStringView &text) {
  u32 size = text.Strlen();
  const char *data = text.Data();
  RefRZChar(data, size);
  return this;
}

// Assign the original Uint8 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(u8 input) {
  SetValueUint8(input);
  return this;
}

// Assign the original Uint16 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(u16 input) {
  SetValueUint16(input);
  return this;
}

// Assign the original Uint32 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(u32 input) {
  SetValueUint32(input);
  return this;
}

// Assign the original Uint64 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(u64 input) {
  SetValueUint64(input);
  return this;
}

// Assign the original Float32 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(f32 input) {
  SetValueFloat32(ScVariantTransport32(input));
  return this;
}

// Assign the original Float64 scalar through its direct private conversion and return this.
const ScValue *ScValue::operator=(f64 input) {
  SetValueFloat64(ScVariantTransport64(input));
  return this;
}

// Assign the object through the direct retain-before-cleanup conversion and return this.
const ScValue *ScValue::operator=(ScUnknown *object) {
  SetValueUnknown(object);
  return this;
}

// Compare the stored type tags of two concrete variants. Payloads are not inspected.
bool ScValue::operator==(const ScValue &source) {
  return type == source.type;
}

// Compare this stored type tag with the type reported by the other variant's virtual interface.
bool ScValue::operator==(const ScVariant *source) {
  return type == source->GetType();
}

// Compare character data only for a character-array tag, using the original virtual borrowed-data accessor.
bool ScValue::operator==(const char *text) {
  return type == kScValueCharArray && strcmp(RefRZChar(), text) == 0;
}

// Compare the low byte of the payload without checking its type tag.
bool ScValue::operator==(u8 input) {
  u8 data;
  memcpy(&data, &value, sizeof(data));
  return data == input;
}

// Compare the low word of the payload without checking its type tag.
bool ScValue::operator==(u16 input) {
  u16 data;
  memcpy(&data, &value, sizeof(data));
  return data == input;
}

// Compare the low double word of the payload without checking its type tag.
bool ScValue::operator==(u32 input) {
  u32 data;
  memcpy(&data, &value, sizeof(data));
  return data == input;
}

// Compare both words of the scalar payload without checking its type tag.
bool ScValue::operator==(u64 input) {
  u64 data;
  memcpy(&data, &value, sizeof(data));
  return data == input;
}

// Match the original x87 equality branch, which accepts unordered NaNs and treats signed zeros as equal.
bool ScValue::operator==(f32 input) {
  u32 data, other;
  memcpy(&data, &value, sizeof(data));
  memcpy(&other, &input, sizeof(other));
  u32 a = data & kScFloatMagnitudeMask, b = other & kScFloatMagnitudeMask;
  return a > kScFloatInfinityMagnitude || b > kScFloatInfinityMagnitude || (!a && !b) || data == other;
}

// Match the original x87 double comparison, including unordered NaNs and opposite-signed zeros.
bool ScValue::operator==(f64 input) {
  u64 data, other;
  memcpy(&data, &value, sizeof(data));
  memcpy(&other, &input, sizeof(other));
  u64 a = data & kScDoubleMagnitudeMask, b = other & kScDoubleMagnitudeMask;
  return a > kScDoubleInfinityMagnitude || b > kScDoubleInfinityMagnitude || (!a && !b) || data == other;
}

// Compare the stored object pointer with the supplied interface pointer without querying either object.
bool ScValue::operator==(ScUnknown *object) {
  return value.pointer == object;
}
