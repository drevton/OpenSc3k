#ifndef SC3_CITY_CT_SERVICE_TEXT_H
#define SC3_CITY_CT_SERVICE_TEXT_H

#include "common/sc3_string.h"
#include "resources/resource.h"

namespace CtService {
// SIMSERV's independently linked localized text handle retains the actual resource identity and three-word key.
class ScTextResource {
public:
  ScTextResource();
  ScTextResource(const ScTextResource &source);
  ScTextResource(u32 instance, u32 group);
  virtual ~ScTextResource();
  ScTextResource *operator=(const ScTextResource &source);
  bool operator==(const ScStringView &other);
  const ScStringView *Value() const;
  // Convert the retained text resource to its original owned string value.
  operator ScString();
  // The original borrowed const-reference result uses explicit pointer spelling.
  const RsKey *GetResKey() const;
  void GetResKey(RsKey *out) const;
  bool IsStringPresent();
  bool ReplaceParameter(ScString *pattern, ScString *replacement);
  bool ReplaceParameter(ScString *pattern, ScString *replacement, ScString *out);

private:
  // Original protected resource lifetime methods.
  void LoadResource();
  void UnloadResource();
  void *text_identity; // Published directly by the original external GetResource void** ABI for IID105.
  RsKey key;
};
} // namespace CtService

#endif
