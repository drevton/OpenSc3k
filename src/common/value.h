#ifndef SC3_COMMON_VALUE_H
#define SC3_COMMON_VALUE_H

#include <vector>

#include "common/api.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// Variant transport and equality distinguish binary32 magnitude, infinity and signaling-NaN payload fields.
enum ScFloatBitField : u32 {
  kScFloatExponentQuietMask = 0x7fc00000,
  kScFloatInfinityMagnitude = 0x7f800000,
  kScFloatNaNPayloadMask = 0x3fffff,
  kScFloatMagnitudeMask = 0x7fffffff,
};

// Double equality uses the same magnitude and infinity distinction with binary64 field widths.
enum ScDoubleBitField : u64 {
  kScDoubleMagnitudeMask = 0x7fffffffffffffffull,
  kScDoubleInfinityMagnitude = 0x7ff0000000000000ull,
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class ScMessageTarget;
class ScStringView;

// Original cIGZCheatCodeManager virtual order through76, verified against retail table4D7900.
class ScCheatCodeManager : public ScUnknown {
public:
  virtual bool RegisterCheatCode(u32 identifier, const ScStringView &name) = 0;
  virtual bool UnregisterCheatCode(u32 identifier) = 0;
  virtual bool AddNotification(ScMessageTarget *target, u32 identifier) = 0;
  virtual bool RemoveNotification(ScMessageTarget *target, u32 identifier) = 0;
  virtual bool GetCheatHasBeenSet(u32 identifier) = 0;
  virtual bool SetCheatHasBeenSet(u32 identifier, bool enabled) = 0;
  virtual u32 GetSetCheatCodes(u32 *out, u32 *count) = 0;
  virtual bool SendCheatNotifications(const ScStringView &text, u32 identifier) = 0;
  virtual i32 DoesCheatCodeMatch(const ScStringView &text, u32 *identifier) = 0;
  virtual bool DoDefaultCheatCodeProcessing(const ScStringView &text, u32 *identifier) = 0;
  virtual bool GetListOfAllCheatStrings(ScStringView **out, u32 *count) = 0;
  virtual u32 GetCheatCodeOccurrenceCount() = 0;
  virtual bool EnableCheatCodes(bool enabled, const ScStringView &password) = 0;
  virtual bool GetCheatCodesEnabled() = 0;
  virtual bool GetCheatCodePassword(u8 *out, u32 *length) = 0;
  virtual bool SetCheatCodePassword(u8 *data, u32 length) = 0;
  virtual bool SetEnableDisableStrings(const ScStringView &enable, const ScStringView &disable) = 0;
};

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2023, 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Retail3592 establishes the complete Unlimited order through52. Targeted post/cancel return void, unlike the newer SDK.
class ScMessageServer : public ScUnknown {
public:
  virtual bool MessageSend(const ScMessage &message) = 0;
  virtual bool MessagePost(const ScMessage &message, bool high_priority) = 0;
  // SDK original slots +20/+24; receivers retain their genuine typed message-target identity.
  virtual bool AddNotification(ScMessageTarget *target, ScMsgCode message_id) = 0;
  virtual bool RemoveNotification(ScMessageTarget *target, ScMsgCode message_id) = 0;
  virtual u32 GetReferenceCountToMessageTarget(ScMessageTarget *) = 0;
  virtual u32 GetReferenceCountToMessageType(ScMsgCode) = 0;
  virtual u32 GetTotalReferenceCount() = 0;
  virtual void GeneralMessagePostToTarget(ScMessage const &sMessage, ScMessageTarget *pTarget) = 0;
  virtual void CancelGeneralMessagePostsToTarget(ScMessageTarget *pTarget) = 0;
  virtual bool OnTick(u32 unknown1) = 0;
  virtual u32 GetMessageQueueSize() = 0;
};

// Original tagRZVariant data base: type, aligned scalar/pointer union and array count.
// The concrete cRZVariant inherits this after its interface and stores its reference count afterward.
struct ScValueData {
  u32 type;

  union {
    u8 unsigned8;
    u16 unsigned16;
    u32 word;
    u64 bits;
    i8 signed8;
    i16 signed16;
    i32 integer;
    i64 signed64;
    f32 real32;
    f64 real64;
    bool boolean;
    char character;
    void *pointer;
  } value;

  u32 count;
};

// Concrete variant owner shared by executable, city and window command producers.
class ScValue : public ScVariant, public ScValueData {
public:
  ScValue();
  // Copy the concrete payload and reference count without cloning or retaining its contents.
  // Original vector records use this shallow copy rather than the variant assignment operation.
  ScValue(const ScValue &source);
  ScValue(u8 input, u32 kind);
  ScValue(u16 input, u32 kind);
  ScValue(u32 input, u32 kind);
  ScValue(u64 input, u32 kind);
  explicit ScValue(f32 input);
  explicit ScValue(f64 input);
  explicit ScValue(const char *text);
  ScValue(void *data, u32 size, u32 kind);
  explicit ScValue(bool input);
  explicit ScValue(const ScStringView &text);
  explicit ScValue(ScUnknown *object);
  explicit ScValue(const ScVariant &source);
  explicit ScValue(const ScVariant *source);
  const ScValue *operator=(const ScValue &source);
  const ScValue *operator=(const ScVariant *source);
  const ScValue *operator=(const char *text);
  const ScValue *operator=(const ScStringView &text);
  const ScValue *operator=(u8 input);
  const ScValue *operator=(u16 input);
  const ScValue *operator=(u32 input);
  const ScValue *operator=(u64 input);
  const ScValue *operator=(f32 input);
  const ScValue *operator=(f64 input);
  const ScValue *operator=(ScUnknown *object);
  bool operator==(const ScValue &source);
  bool operator==(const ScVariant *source);
  bool operator==(const char *text);
  bool operator==(u8 input);
  bool operator==(u16 input);
  bool operator==(u32 input);
  bool operator==(u64 input);
  bool operator==(f32 input);
  bool operator==(f64 input);
  bool operator==(ScUnknown *object);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetType() const override;
  u32 GetCount() const override;
  bool Erase() override;
  bool CopyFrom(const ScVariant &source) override;
  bool CreateCopy(ScVariant **out) const override;
  bool GetValBool(bool *out) const override;
  bool SetValBool(bool input) override;
  bool GetValSint8(i8 *out) const override;
  bool SetValSint8(i8 input) override;
  bool GetValUint8(u8 *out) const override;
  bool SetValUint8(u8 input) override;
  bool GetValSint16(i16 *out) const override;
  bool SetValSint16(i16 input) override;
  bool GetValUint16(u16 *out) const override;
  bool SetValUint16(u16 input) override;
  bool GetValSint32(i32 *out) const override;
  bool SetValSint32(i32 input) override;
  bool GetValUint32(u32 *out) const override;
  bool SetValUint32(u32 input) override;
  bool GetValSint64(i64 *out) const override;
  bool SetValSint64(i64 input) override;
  bool GetValUint64(u64 *out) const override;
  bool SetValUint64(u64 input) override;
  bool GetValFloat32(f32 *out) const override;
  bool SetValFloat32(f32 input) override;
  bool GetValFloat64(f64 *out) const override;
  bool SetValFloat64(f64 input) override;
  bool GetValRZChar(char *out) const override;
  bool SetValRZChar(char input) override;
  bool CreateValString(ScStringView **out) const override;
  bool GetValString(ScStringView *out) const override;
  bool SetValString(const ScStringView &text) override;
  bool GetValUnknown(u32 iid, void **out) override;
  bool SetValUnknown(ScUnknown *object) override;
  const u8 *AsUint8() const override;
  const u16 *AsUint16() const override;
  const u32 *AsUint32() const override;
  const u64 *AsUint64() const override;
  const i8 *AsSint8() const override;
  const i16 *AsSint16() const override;
  const i32 *AsSint32() const override;
  const i64 *AsSint64() const override;
  const f32 *AsFloat32() const override;
  const f64 *AsFloat64() const override;
  const bool *AsBool() const override;
  const char *AsRZChar() const override;
  u8 *RefUint8() const override;
  void RefUint8(const u8 *data, u32 amount) override;
  u16 *RefUint16() const override;
  void RefUint16(const u16 *data, u32 amount) override;
  u32 *RefUint32() const override;
  void RefUint32(const u32 *data, u32 amount) override;
  u64 *RefUint64() const override;
  void RefUint64(const u64 *data, u32 amount) override;
  i8 *RefSint8() const override;
  void RefSint8(const i8 *data, u32 amount) override;
  i16 *RefSint16() const override;
  void RefSint16(const i16 *data, u32 amount) override;
  i32 *RefSint32() const override;
  void RefSint32(const i32 *data, u32 amount) override;
  i64 *RefSint64() const override;
  void RefSint64(const i64 *data, u32 amount) override;
  f32 *RefFloat32() const override;
  void RefFloat32(const f32 *data, u32 amount) override;
  f64 *RefFloat64() const override;
  void RefFloat64(const f64 *data, u32 amount) override;
  bool *RefBool() const override;
  void RefBool(const bool *data, u32 amount) override;
  char *RefRZChar() const override;
  void RefRZChar(const char *data, u32 amount) override;
  ScRect *RefRect() const override;
  void RefRect(const ScRect *data) override;
  ScPoint *RefPoint() const override;
  void RefPoint(const ScPoint *data) override;
  ScSize *RefSize() const override;
  void RefSize(const ScSize *data) override;
  void *RefData() const override;
  void RefData(void *data, u32 size) override;
  ScUnknown *RefUnknown() const override;
  bool RefUnknown(u32 iid, void **out) override;
  void RefUnknown(ScUnknown *object) override;
  virtual ~ScValue();

protected:
  virtual void Ctor(const ScVariant *source);
  virtual void Dtor();
  u32 references;

private:
  bool GetValueUint8(u8 *out) const;
  bool SetValueUint8(u8 input);
  bool GetValueUint16(u16 *out) const;
  bool SetValueUint16(u16 input);
  bool GetValueUint32(u32 *out) const;
  bool SetValueUint32(u32 input);
  bool GetValueUint64(u64 *out) const;
  bool SetValueUint64(u64 input);
  bool GetValueSint8(i8 *out) const;
  bool SetValueSint8(i8 input);
  bool GetValueSint16(i16 *out) const;
  bool SetValueSint16(i16 input);
  bool GetValueSint32(i32 *out) const;
  bool SetValueSint32(i32 input);
  bool GetValueSint64(i64 *out) const;
  bool SetValueSint64(i64 input);
  bool GetValueFloat32(f32 *out) const;
  bool SetValueFloat32(f32 input);
  bool GetValueFloat64(f64 *out) const;
  bool SetValueFloat64(f64 input);
  bool GetValueBool(bool *out) const;
  bool SetValueBool(bool input);
  bool GetValueRZChar(char *out) const;
  bool SetValueRZChar(char input);
  bool GetValueString(ScStringView *out) const;
  bool SetValueString(const ScStringView &text);
  bool GetValueUnknown(u32 iid, void **out);
  bool SetValueUnknown(ScUnknown *object);
};

class ScDirector;

// The SIMVARIABLES director is the actual owner of this module's reference holds.
ScDirector *ScGetVariableDirector();
u32 ScParseVariableNumber(const char *text);

// The installed system-service base embeds this separate reference-count identity.
class ScVariableReference : public ScUnknown {
public:
  ScVariableReference();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual ~ScVariableReference();
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// The module-local service base owns its ID, priority and running flag independently of the variable store.
class ScVariableService : public ScSystemService, public ScVariableReference {
public:
  ScVariableService(u32 identifier, i32 priority);
  ~ScVariableService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
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

protected:
  u32 identifier;
  i32 priority;
  bool running;
};

// The installed variable vector stores a DWORD key followed by a concrete variant at record offset eight.
struct ScVariableEntry {
  // Start the variant in its own empty state; the key is supplied by the insertion path.
  ScVariableEntry() = default;
  // Original vector copying preserves the payload words and local references without another object retain.
  ScVariableEntry(const ScVariableEntry &source);
  ScVariableEntry &operator=(const ScVariableEntry &) = delete;
  u32 key;
  ScValue value;
};

// SIMVARIABLES stores entries in an original std::vector: ctor 100010D2 and scan 1000159C prove its live range and stride.
// Explicit reserve requests retain the installed doubling policy before the standard vector appends.
class ScVariableStore : public ScVariableManager, public ScMessageTarget, public ScVariableService {
public:
  ScVariableStore();
  ~ScVariableStore() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScSystemService *AsSystemService() override;
  u8 HasVariable(u32 key) override;
  u8 GetVariable(u32 key, ScValue *out) override;
  u8 LoadTuning(const ScStringView &path) override;
  ScStringView *GetSourcePath() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;

private:
  friend void ScReadVariableTuning(const ScString &key, const ScString &value, ScVariableStore *store);
  // The installed insertion returns raw AL; its Boolean/BYTE and input const spellings remain unresolved.
  u8 StoreVariable(u32 key, const ScVariant &value);
  std::vector<ScVariableEntry> entries;
  ScString source_path;
};

// The original enumeration callback reads concrete string inputs and a typed variable-store receiver.
void ScReadVariableTuning(const ScString &key, const ScString &value, ScVariableStore *store);

// The installed command-factory base retains its child factories in an original std::vector.
class ScVariableFactoryBase : public ScCommandFactory {
public:
  ScVariableFactoryBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 class_id, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual ~ScVariableFactoryBase();
  // The installed private callback's Boolean/BYTE nominal return remains unresolved.
  virtual bool CreateCommand(u32 class_id, u32 iid, void **out) = 0;

protected:
  u32 references;
  std::vector<ScCommandFactory *> children;
};

// The module's registered factory delegates to children and rejects its own unknown command requests.
class ScVariableCommandFactory : public ScVariableFactoryBase {
public:
  ~ScVariableCommandFactory() override;
  bool CreateCommand(u32 class_id, u32 iid, void **out) override;
};

#endif
