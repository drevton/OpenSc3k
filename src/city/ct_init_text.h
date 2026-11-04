#ifndef SC3_CITY_CT_INIT_TEXT_H
#define SC3_CITY_CT_INIT_TEXT_H

#include "common/sc3_string.h"
#include "resources/resource.h"

class FtFont;
class UiTokenData;
struct CtInitTokenContext;
using CtInitTokenCallback = bool(SC3_CDECL *)(const ScString &, i32, UiTokenData *, CtInitTokenContext *);

// SIMINIT's scoped resource-text handle owns a resolved String interface and its original resource key.
class CtInitText {
public:
  CtInitText();
  CtInitText(u32 instance, u32 group);
  // Retail copy construction resolves a new resource from the source handle's key.
  CtInitText(const CtInitText &source);
  virtual ~CtInitText();
  CtInitText *operator=(const CtInitText &source);
  bool operator==(const ScStringView &other);
  operator ScString();
  const ScStringView *Value() const;
  ScString Val();
  bool Detokenize(ScStringView *out);
  bool Detokenize(ScStringView *out, UiTokenData **tokens, u32 count);
  bool Detokenize(ScStringView *out, CtInitTokenCallback callback, CtInitTokenContext *context);
  bool ReplaceParameter(ScString *token, ScString *replacement);
  bool ReplaceParameter(ScString *token, ScString *replacement, ScString *out);
  const RsKey *GetResKey() const;
  void GetResKey(RsKey *out) const;
  bool IsStringPresent();
  static void FormatForEllipsis(ScString *value, i32 width, FtFont *font);

private:
  void ReplaceParameters(ScString *value);
  void ReplaceParameters(ScString *value, UiTokenData **tokens, u32 count);
  void ReplaceParameters(ScString *value, CtInitTokenCallback callback, CtInitTokenContext *context);
  bool ReplaceCommonParameter(ScString *value, i32 *position);
  bool ReplaceParameter(ScString *value, const ScString &token, i32 *position);
  bool ReplaceParameter(ScString *value, UiTokenData *token, i32 *position);
  void LoadResource();
  void UnloadResource();
  ScStringView *text;
  RsKey key;
};

#endif
