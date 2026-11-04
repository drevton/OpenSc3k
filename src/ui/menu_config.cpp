#include "application/application_core.h"
#include "common/api.h"
#include "resources/resource.h"
#include "ui/ui_menu_config.h"
#define WIN32_LEAN_AND_MEAN
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

// Default menu image keys follow the consumer rows; the quit button retains a separate alternate image.
enum UiDefaultMenuArtwork : u32 {
  kUiDefaultQuitMenuImageId = 0x22729930,
  kUiDefaultQuitAlternateMenuImageId = 0x22729931,
  kUiDefaultCreditsMenuImageId = 0x22729933,
  kUiDefaultNewCityMenuImageId = 0x22729936,
  kUiDefaultLoadCityMenuImageId = 0x22729932,
  kUiDefaultTerrainMenuImageId = 0x22729935,
  kUiDefaultStarterTownMenuImageId = 0x22729937,
  kUiDefaultPreferencesMenuImageId = 0x22729934,
  kUiDefaultScenarioMenuImageId = 0x22729938,
};

// Native storage phase of 43A31E: discard prior entries and install the nine original resource triples in ID order.
void UiMenuKeys::Reset() {
  static const u32 kUiDefaultMenuImages[9] = {kUiDefaultQuitMenuImageId, kUiDefaultQuitAlternateMenuImageId,
      kUiDefaultCreditsMenuImageId, kUiDefaultNewCityMenuImageId, kUiDefaultLoadCityMenuImageId,
      kUiDefaultTerrainMenuImageId, kUiDefaultStarterTownMenuImageId, kUiDefaultPreferencesMenuImageId,
      kUiDefaultScenarioMenuImageId};
  for (u32 i = 0; i < 9; ++i)
    keys[i] = {0, kRsMainImageGroup, kUiDefaultMenuImages[i]};
}

// Copy the prefix into a separate String and erase it plus the delimiter, or consume the complete remaining input.
bool UiMenuSplit(ScString *input, u8 delimiter, ScString *out) {
  u32 index = 0, length = input->ScString::Strlen();
  const char *bytes = input->ScString::Data();
  while (index < length && (u8)bytes[index] != delimiter)
    ++index;
  out->ScString::FromChar(bytes, index);
  input->Erase(0, index < length ? index + 1 : length);
  return true;
}

bool UiMenuRemoveSpaces(ScString *text) {
  u32 index = 0;
  while (index < text->ScString::Strlen())
    if (text->ScString::Data()[index] == ' ')
      text->Erase(index, 1);
    else
      ++index;
  return text->ScString::Strlen() != 0;
}

// Native parsing phase of 43A31E: update matching default IDs in order and stop at the first absent ID or empty record.
void UiMenuKeys::Overlay(ScString *value) {
  ScString record, token;
  UiMenuRemoveSpaces(value);
  while (value->ScString::Strlen()) {
    UiMenuSplit(value, '<', &record);
    UiMenuSplit(value, '>', &record);
    if (!record.Strlen())
      break;
    UiMenuSplit(&record, '=', &token);
    u32 id = ScConvertToUint32(token.Data());
    UiMenuSplit(&record, ',', &token);
    u32 group = ScConvertToUint32(token.Data());
    UiMenuSplit(&record, ',', &token);
    u32 instance = ScConvertToUint32(token.Data());
    if (!id || id > 9)
      break;
    keys[id - 1] = {0, group, instance};
  }
}

#if defined(_WIN32)
// Split the registry path and read REG_SZ or REG_DWORD, preserving the returned string's byte count.
// Match complete hive names with their original case; ordinary lookup failures leave the destination unchanged.
static bool UiPlatformReadRegistryString(const ScString &path, ScString *out) {
  ScString root, subkey, value_name;
  subkey = path;
  u32 first = 0;
  while (first < subkey.Strlen() && subkey.Data()[first] != '\\')
    ++first;
  if (first == subkey.Strlen())
    return false;
  root.FromChar(subkey.Data(), first);
  subkey.Erase(0, first + 1);
  u32 last = subkey.Strlen();
  while (last && subkey.Data()[last - 1] != '\\')
    --last;
  if (!last)
    return false;
  value_name.FromChar(subkey.Data() + last, subkey.Strlen() - last);
  subkey.Erase(last - 1, subkey.Strlen() - last + 1);
  const struct {
    const char *name;
    HKEY handle;
  } roots[] = {{"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT}, {"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
      {"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE}, {"HKEY_USERS", HKEY_USERS},
      {"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG}};
  HKEY hive = nullptr;
  for (const auto &candidate : roots) {
    usize length = strlen(candidate.name);
    if (root.Strlen() == length && !memcmp(root.Data(), candidate.name, length)) {
      hive = candidate.handle;
      break;
    }
  }
  if (!hive)
    return false;
  HKEY handle = nullptr;
  REGSAM access = KEY_QUERY_VALUE;
#ifdef _WIN64
  access |= KEY_WOW64_32KEY;
#endif
  if (RegOpenKeyExA(hive, subkey.Data(), 0, access, &handle) != ERROR_SUCCESS)
    return false;
  bool result = false;
  DWORD type;
  if (RegQueryValueExA(handle, value_name.Data(), nullptr, &type, nullptr, nullptr) == ERROR_SUCCESS) {
    if (type == REG_SZ) {
      char bytes[1028];
      DWORD length = 1024;
      if (RegQueryValueExA(handle, value_name.Data(), nullptr, &type, reinterpret_cast<BYTE *>(bytes), &length) ==
          ERROR_SUCCESS) {
        out->ScString::FromChar(bytes, length);
        result = true;
      }
    } else if (type == REG_DWORD) {
      DWORD value, length = 4;
      if (RegQueryValueExA(handle, value_name.Data(), nullptr, &type, reinterpret_cast<BYTE *>(&value), &length) ==
          ERROR_SUCCESS) {
        // Reject malformed external data before formatting could read an unwritten local byte.
        if (length >= 4) {
          char bytes[16];
          int size = snprintf(bytes, sizeof(bytes), "%u", (u32)value);
          out->ScString::FromChar(bytes, (u32)size);
          result = true;
        }
      }
    }
  }
  RegCloseKey(handle);
  return result;
}
#else
static bool UiPlatformReadRegistryString(const ScString &, ScString *) {
  return false;
}
#endif

bool UiReadRegistryString(const ScString &path, ScString *out) {
  return UiPlatformReadRegistryString(path, out);
}

// Read the named registry value, require every logical byte to be a decimal digit, then write its signed number.
bool UiReadRegistryNumber(const ScString &path, i32 *out) {
  ScString value;
  if (!UiReadRegistryString(path, &value))
    return false;
  for (const char *byte = value.begin; byte != value.end; ++byte)
    if (*byte < '0' || *byte > '9')
      return false;
  *out = (i32)strtoll(value.begin, nullptr, 10);
  return true;
}

UiRegistryReference::UiRegistryReference() : registry(nullptr) {
  ApGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
}

UiRegistryReference::~UiRegistryReference() {
  if (registry)
    registry->Release();
}

// Install all defaults before registry acquisition; a successful MAINMENUBMP lookup determines success even on bad records.
bool UiLoadMenuKeys(UiMenuKeys *keys) {
  keys->Reset();
  ScString sku;
  if (!UiReadRegistryString("HKEY_LOCAL_MACHINE\\Software\\Electronic Arts\\Maxis\\SimCity 3000 Unlimited\\SKU", &sku) ||
      !sku.Strlen())
    return false;
  for (char *p = sku.begin; *p; ++p)
    *p = (char)toupper((u8)*p);
  UiRegistryReference source;
  if (!source.registry)
    return false;
  ScString key, value;
  key.FromChar(sku.Data());
  bool result = source.registry->Lookup("MAINMENUBMP", key, &value);
  if (result)
    keys->Overlay(&value);
  return result;
}

// Resolve the named bitmap mapping under the installed SKU, then decode its comma-separated group and instance.
u8 UiReadSplashKey(const char *name, RsKey *key) {
  ScString sku;
  if (!UiReadRegistryString("HKEY_LOCAL_MACHINE\\Software\\Electronic Arts\\Maxis\\SimCity 3000 Unlimited\\SKU", &sku) ||
      !sku.Strlen())
    return 0;
  for (char *p = sku.begin; *p; ++p)
    *p = (char)toupper((u8)*p);
  UiRegistryReference source;
  if (!source.registry)
    return 0;
  ScString lookup(sku.Data()), value;
  if (!source.registry->Lookup(name, lookup, &value))
    return 0;
  ScString group;
  u32 comma = 0, length = value.Strlen();
  while (comma < length && value.Data()[comma] != ',')
    ++comma;
  if (comma < length) {
    group.FromChar(value.Data(), comma);
    value.Erase(0, comma + 1);
  }
  key->group = ScConvertToUint32(group.Data());
  key->instance = ScConvertToUint32(value.Data());
  return 1;
}
