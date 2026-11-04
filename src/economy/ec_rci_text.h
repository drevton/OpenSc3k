#ifndef SC3_ECONOMY_EC_RCI_TEXT_H
#define SC3_ECONOMY_EC_RCI_TEXT_H

#include "common/sc3_string.h"
#include "resources/resource.h"

namespace EcRci {
// SIMRCI's local text handle retains the actual resource identity and its three-word key.
class ScTextResource {
public:
  ScTextResource(u32 instance, u32 group);
  virtual ~ScTextResource();
  // The original aggregate-return effect is known; its private source name remains unresolved.
  ScString GetText();
  // Original folded field getter; its private source name remains unresolved.
  ScStringView *GetResource();

private:
  void *text_identity;
  RsKey key;
};
} // namespace EcRci

#endif
