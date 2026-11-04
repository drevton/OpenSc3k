#include "common/sc3_registry.h"

#include <cassert>
#include <cstring>

// The service constructor supplies this original startup ordering priority.
enum ScRegistryServiceOrder : i32 {
  kScRegistryServicePriority = 0x98968a,
};

// Compare the complete stored byte ranges using the original string ordering.
bool ScRegistryStringLess::operator()(const ScString &first, const ScString &second) const {
  u32 first_length = first.ScString::Strlen(), second_length = second.ScString::Strlen();
  u32 length = first_length < second_length ? first_length : second_length;
  int order = std::memcmp(first.begin, second.begin, length);
  return order < 0 || (order == 0 && first_length < second_length);
}

// Find a separator within a concrete string, returning the original all-ones missing position.
static u32 ScRegistryFind(const ScString &text, const char *separator, u32 length) {
  u32 size = text.ScString::Strlen();
  if (length > size)
    return UINT32_MAX;
  for (u32 i = 0; i <= size - length; ++i) {
    if (!std::memcmp(text.begin + i, separator, length))
      return i;
  }
  return UINT32_MAX;
}

// Copy the original substring range, stopping on its unrecoverable out-of-range request.
static ScString ScRegistrySubstring(const ScString &text, u32 start, u32 count) {
  u32 size = text.ScString::Strlen();
  if (start > size)
    ScFatal(SC_ERROR_INVALID, "Registry string substring is out of range");
  if (count > size - start)
    count = size - start;
  return ScString(text.begin + start, count);
}

ScRegistryService::ScRegistryService() : ScServiceBase(kScRegistryServiceId, kScRegistryServicePriority) {
}

ScRegistryService::~ScRegistryService() {
}

bool ScRegistryService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScRegistry, ScRegistry);
  return ScServiceBase::QueryInterface(iid, out);
}

u32 ScRegistryService::AddRef() {
  return ScServiceBase::AddRef();
}

u32 ScRegistryService::Release() {
  return ScServiceBase::Release();
}

// Parse the category, key and value components and insert the resulting registry entry.
bool ScRegistryService::Register(const ScStringView &name) {
  ScString remaining(name);
  u32 position = ScRegistryFind(remaining, "://", 3);
  ScString category = ScRegistrySubstring(remaining, 0, position);
  remaining = ScRegistrySubstring(remaining, position + 3, UINT32_MAX);
  position = ScRegistryFind(remaining, "/", 1);
  ScString key_range = ScRegistrySubstring(remaining, 0, position);
  ScString key(key_range.ToChar());
  remaining = ScRegistrySubstring(remaining, position + 1, UINT32_MAX);
  position = ScRegistryFind(remaining, "/", 1);
  ScString value_range = ScRegistrySubstring(remaining, 0, position);
  ScString value(value_range.ToChar());
  entries.emplace(category, std::make_pair(key, value));
  return true;
}

// Remove the first entry with an empty category; the installed method does not inspect its argument.
// The original erase requires an existing entry and does not handle an end iterator.
bool ScRegistryService::Unregister(const ScStringView &) {
  ScString empty;
  auto entry = entries.find(empty);
  assert(entry != entries.end());
  entries.erase(entry);
  return true;
}

// Find the first matching category, then search forward for the requested key.
bool ScRegistryService::Lookup(const char *category, const ScStringView &key, ScStringView *result) {
  if (!category)
    return false;
  ScString name(category);
  auto entry = entries.lower_bound(name);
  while (entry != entries.end()) {
    if (entry->second.first.IsEqual(&key)) {
      const ScString &value = entry->second.second;
      u32 length = value.Strlen();
      const char *data = value.Data();
      result->FromChar(data, length);
      return true;
    }
    ++entry;
  }
  return false;
}

bool ScRegistryService::MakeRegistryName(
    const char *category, const ScStringView &key, const ScStringView &value, ScStringView *result) {
  if (!category)
    return false;
  ScString name(category);
  name.Append("://");
  name.Append(key.ToChar());
  name.Append("/");
  name.Append(value.ToChar());
  result->FromChar(name.ToChar());
  return true;
}

// Enumerate entries whose category matches the requested name, passing their stored key and value.
bool ScRegistryService::EnumCategory(const char *category, ScRegistryCallback callback, void *context) {
  if (!callback || !context)
    return false;
  ScString name(category);
  // MSVC's multimap find returns the first equal key; lower_bound gives that on every standard library.
  for (auto entry = entries.lower_bound(name); entry != entries.end(); ++entry) {
    if (entry->first.IsEqual(&name))
      callback(entry->second.first, entry->second.second, context);
  }
  return true;
}
