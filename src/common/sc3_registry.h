#ifndef SC3_COMMON_SC3_REGISTRY_H
#define SC3_COMMON_SC3_REGISTRY_H

#include <map>
#include <utility>

#include "common/api.h"
#include "common/sc3_string.h"

// Retail CodeView identifies the original multimap<cRZString, pair<cRZString,cRZString>>.
struct ScRegistryStringLess {
  bool operator()(const ScString &first, const ScString &second) const;
};

// Original cGZRegistry has its registry identity before the service and module identities.
class ScRegistryService : public ScRegistry, public ScServiceBase {
public:
  ScRegistryService();
  ~ScRegistryService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Register(const ScStringView &name) override;
  bool Unregister(const ScStringView &name) override;
  bool Lookup(const char *category, const ScStringView &key, ScStringView *result) override;
  bool MakeRegistryName(
      const char *category, const ScStringView &key, const ScStringView &value, ScStringView *result) override;
  bool EnumCategory(const char *category, ScRegistryCallback callback, void *context) override;

protected:
  std::multimap<ScString, std::pair<ScString, ScString>, ScRegistryStringLess> entries;
};

#endif
