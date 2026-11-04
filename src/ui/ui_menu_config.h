#ifndef SC3_UI_UI_MENU_CONFIG_H
#define SC3_UI_UI_MENU_CONFIG_H

#include "common/sc3_string.h"
#include "resources/resource.h"

// Native fixed storage replaces the temporary map whose only members are the nine original menu IDs.
struct UiMenuKeys {
  void Reset();
  void Overlay(ScString *value);
  RsKey keys[9];
};

// Original scoped registry reference; the acquired service is released at the end of the lookup scope.
class UiRegistryReference {
public:
  UiRegistryReference();
  ~UiRegistryReference();
  ScRegistry *registry;
};

bool UiReadRegistryString(const ScString &path, ScString *out);
bool UiReadRegistryNumber(const ScString &path, i32 *out);
bool UiLoadMenuKeys(UiMenuKeys *keys);
u8 UiReadSplashKey(const char *name, RsKey *key);
bool UiMenuSplit(ScString *input, u8 delimiter, ScString *out);
bool UiMenuRemoveSpaces(ScString *text);

#endif
