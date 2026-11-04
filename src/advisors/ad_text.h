#ifndef SC3_ADVISORS_AD_TEXT_H
#define SC3_ADVISORS_AD_TEXT_H

#include "advisors/api.h"
#include "common/sc3_string.h"
#include "resources/resource.h"

// Original seven-slot record interface, including inherited Unknown operations; it has no virtual destructor.
class AdHeaderRecord : public ScUnknown {
public:
  virtual bool GetText(ScStringView **out) = 0;
  virtual bool CopyText(ScStringView *out) = 0;
  virtual ScStringView *Text() = 0;
  virtual u32 GetID() = 0;
};

class AdHeader : public AdHeaderRecord {
public:
  AdHeader();
  virtual ~AdHeader();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetText(ScStringView **out) override;
  bool CopyText(ScStringView *out) override;
  ScStringView *Text() override;
  u32 GetID() override;
  bool SetText(ScStringView *source);
  void SetID(u32 value);
  ScString text;
  u32 id, references;
};

// Recovered twelve-slot advice message interface. Owned copies and borrowed Strings use distinct original slots.
class AdMessageRecord : public ScUnknown {
public:
  virtual u32 GetID() = 0;
  virtual u32 GetMood() = 0;
  virtual RsKey *GetPicture() = 0;
  virtual ScStringView *Title() = 0;
  virtual bool GetTitle(ScStringView **out) = 0;
  virtual bool CopyTitle(ScStringView *out) = 0;
  virtual ScStringView *Message() = 0;
  virtual bool GetMessageText(ScStringView **out) = 0;
  virtual bool CopyMessage(ScStringView *out) = 0;
};

class AdMessage : public AdMessageRecord {
public:
  AdMessage();
  AdMessage(u32 id, u8 mood, const RsKey &picture, const ScString &title, const ScString &message);
  virtual ~AdMessage();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetID() override;
  u32 GetMood() override;
  RsKey *GetPicture() override;
  ScStringView *Title() override;
  bool GetTitle(ScStringView **out) override;
  bool CopyTitle(ScStringView *out) override;
  ScStringView *Message() override;
  bool GetMessageText(ScStringView **out) override;
  bool CopyMessage(ScStringView *out) override;
  ScString *SetTitle(const ScString &source);
  ScString *SetMessage(const ScString &source);
  u32 id;
  ScString title, message;
  u8 mood;
  RsKey picture;
  u32 references;
};

// Original INI-backed topic picture lookup; implementation remains explicit until its resource/file services are recovered.
bool AdLoadTopicPicture(u32 id, RsKey *out);

class AdToken;

// Actual eight module-owned builtin token descriptors, shared by scanning and live city replacement.
extern const ScString AdTokenNames[8];

u32 AD_STDCALL AdExpandTokens(ScString *text);
u32 AD_STDCALL AdExpandTokens(ScString *text, const AdToken *const *tokens, u32 count);
bool AD_STDCALL AdTryParameterToken(ScString *text, const AdToken *token, u32 *position);
bool AD_STDCALL AdSubstituteToken(ScString *text, u32 *position);
// Builtin replacement resolves the original live city and preferences interfaces directly.
bool AD_STDCALL AdTryToken(ScString *text, const ScString &token, u32 *position);

// The scoped SIMADV text-key value retains its resolved String interface and complete key.
class AdTextHandle {
public:
  AdTextHandle();
  AdTextHandle(u32 instance, u32 group);
  virtual ~AdTextHandle();
  bool Resolve();
  bool IsValid() const;
  RsKey *GetKey(RsKey *out) const;
  AdTextHandle *Assign(const AdTextHandle &source);
  bool CopyTo(ScStringView *out);
  bool CopyTo(ScStringView *out, const AdToken *const *tokens, u32 count);
  ScString ToString() const;
  ScString ToExpandedString() const;
  bool CopyWithReplacement(const ScStringView *needle, const ScStringView *replacement, ScString *out);
  ScStringView *text;
  RsKey key;
};

#endif
