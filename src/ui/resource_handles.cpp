#include <cstring>

#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "resources/resource.h"
#include "ui/ui_menu_label.h"
#include "ui/ui_resource_handles.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "common/ids.h"

UiImageHandle::UiImageHandle() : buffer(nullptr) {
}

UiImageHandle::UiImageHandle(const UiImageHandle &source) : buffer(source.buffer) {
  if (buffer)
    buffer->AddRef();
}

UiImageHandle::UiImageHandle(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = ApGetResourceManager();
  RsFactoryInterface *factory;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      Load(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the scoped buffer reference and clear its field; no native observation or paint descriptor is retained.
UiImageHandle::~UiImageHandle() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

// Borrow the buffer without taking an additional reference.
GxBuffer *UiImageHandle::GetBuffer() const {
  return buffer;
}

// Retain the replacement before releasing the old buffer, preserving self-assignment without callbacks.
UiImageHandle &UiImageHandle::operator=(const UiImageHandle &source) {
  if (&source != this) {
    if (source.buffer)
      source.buffer->AddRef();
    if (buffer)
      buffer->Release();
    buffer = source.buffer;
  }
  return *this;
}

bool UiImageHandle::IsBufferPresent() const {
  return buffer != nullptr;
}

void UiImageHandle::Load(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Retain the completed buffer before the scoped handle releases it; optional keys use the converted low WORD.
bool UiLoadMenuImage(u32 group, u32 instance, u32 color, GxBuffer **out) {
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageHandle handle(instance, group, GxVramRequest::SystemMemory, format);
  GxBuffer *buffer = handle.GetBuffer();
  if (!buffer)
    return false;
  if (color) {
    u16 key = buffer->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
    buffer->SetTransparency(key);
  }
  buffer->AddRef();
  *out = buffer;
  return true;
}

UiTextHandle::UiTextHandle() : text(nullptr), key{} {
}

// Release the previous resource before copying the source key and resolving it independently.
UiTextHandle &UiTextHandle::operator=(const UiTextHandle &source) {
  if (&source != this) {
    UnloadResource();
    key = source.key;
    Resolve();
  }
  return *this;
}

UiTextHandle::UiTextHandle(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

UiTextHandle::~UiTextHandle() {
  UnloadResource();
}

void UiTextHandle::UnloadResource() {
  ScRelease(&text);
}

// Borrow the resolved String interface without querying or retaining it.
ScStringView *UiTextHandle::GetText() {
  return text;
}

void UiTextHandle::Resolve() {
  if (!ApGetResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

// Fit the original encoded text to the available width and append the localized ellipsis when it fits.
void UiTextHandle::FormatForEllipsis(ScString *text, i32 width, FtFont *font) {
  if (!font || text->begin == text->end)
    return;
  u32 length = static_cast<u32>(text->end - text->begin);
  u32 fitted = font->Fit(text->begin, length, static_cast<u32>(width));
  if (!fitted) {
    text->Erase(0, length);
    return;
  }
  if (fitted == length)
    return;
  WnResources *resources = ApGetUiResources();
  if (!resources)
    return;
  const ScStringView *source = resources->GetText(kWnEllipsisTextId);
  ScString ellipsis;
  if (source) {
    u32 size = source->Strlen();
    const char *bytes = source->Data();
    ellipsis.FromChar(bytes, size);
  }
  ScRect bounds{};
  if (ellipsis.begin != ellipsis.end)
    font->TextArea(ellipsis.begin, static_cast<u32>(ellipsis.end - ellipsis.begin), &bounds);
  if (bounds.right - bounds.left < width) {
    fitted = font->Fit(
        text->begin, static_cast<u32>(text->end - text->begin), static_cast<u32>(width + bounds.left - bounds.right));
    text->Erase(fitted, static_cast<u32>(text->end - text->begin) - fitted);
    text->RTrim();
    text->Append(ellipsis.begin, static_cast<u32>(ellipsis.end - ellipsis.begin));
  } else {
    text->Erase(0, static_cast<u32>(text->end - text->begin));
  }
}

UiTextHandle::UiTextHandle(const UiTextHandle &source) : text(nullptr), key(source.key) {
  Resolve();
}

// Compare a resolved string with the supplied interface; an unresolved resource compares false.
bool UiTextHandle::operator==(const ScStringView &other) {
  return text && text->IsEqual(&other);
}

UiTextHandle::operator ScString() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Return the borrowed original resource-key field without changing its owner.
const RsKey *UiTextHandle::GetResKey() const {
  return &key;
}

void UiTextHandle::GetResKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
}

bool UiTextHandle::IsStringPresent() {
  return text != nullptr;
}

static u32 UiFindText(const ScString &text, const char *needle, u32 length, u32 start) {
  u32 size = static_cast<u32>(text.end - text.begin);
  if (start > size || length > size - start)
    return UINT32_MAX;
  for (u32 offset = start; offset <= size - length; ++offset)
    if (!length || memcmp(text.begin + offset, needle, length) == 0)
      return offset;
  return UINT32_MAX;
}

static bool UiTextEquals(const ScString &text, const char *literal) {
  usize length = strlen(literal);
  return text.Strlen() == length && memcmp(text.Data(), literal, length) == 0;
}

// Scan the value for parameter markers, applying the original common-parameter replacement at each occurrence.
void UiTextHandle::ReplaceParameters(ScString *value) {
  u32 position = UiFindText(*value, "%", 1, 0);
  while (position < static_cast<u32>(value->end - value->begin)) {
    i32 current = static_cast<i32>(position);
    ReplaceCommonParameter(value, &current);
    position = UiFindText(*value, "%", 1, static_cast<u32>(current) + 1);
  }
}

void UiTextHandle::ReplaceParameters(ScString *value, UiTokenData **tokens, u32 count) {
  u32 position = UiFindText(*value, "%", 1, 0);
  while (position < static_cast<u32>(value->end - value->begin)) {
    i32 current = static_cast<i32>(position);
    if (!ReplaceCommonParameter(value, &current) && tokens && count)
      for (u32 index = 0; index < count; ++index)
        ReplaceParameter(value, tokens[index], &current);
    position = UiFindText(*value, "%", 1, static_cast<u32>(current) + 1);
  }
}

void UiTextHandle::ReplaceParameters(ScString *value, UiTokenCallback callback, UiTokenCallbackContext *context) {
  UiTokenData token;
  u32 position = UiFindText(*value, "%", 1, 0);
  while (position < static_cast<u32>(value->end - value->begin)) {
    i32 current = static_cast<i32>(position);
    if (!ReplaceCommonParameter(value, &current) && callback(*value, current, &token, context))
      ReplaceParameter(value, &token, &current);
    position = UiFindText(*value, "%", 1, static_cast<u32>(current) + 1);
  }
}

// Return a concrete copy of the resolved string after applying its original common-parameter substitutions.
ScString UiTextHandle::Val() {
  if (!text)
    return ScString();
  ScString value(*text);
  ReplaceParameters(&value);
  return value;
}

// Copy and expand a resolved string into the caller's output interface.
bool UiTextHandle::Detokenize(ScStringView *out) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value);
  out->FromChar(value.begin, static_cast<u32>(value.end - value.begin));
  return true;
}

bool UiTextHandle::Detokenize(ScStringView *out, UiTokenData **tokens, u32 count) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value, tokens, count);
  out->FromChar(value.begin, static_cast<u32>(value.end - value.begin));
  return true;
}

bool UiTextHandle::Detokenize(ScStringView *out, UiTokenCallback callback, UiTokenCallbackContext *context) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value, callback, context);
  out->FromChar(value.begin, static_cast<u32>(value.end - value.begin));
  return true;
}

// Replace the first named parameter in the handle's resolved concrete string.
bool UiTextHandle::ReplaceParameter(ScString *name, ScString *replacement) {
  ScString *value = static_cast<ScString *>(text);
  const char *needle = name->ToChar();
  u32 position = UiFindText(*value, needle, static_cast<u32>(strlen(needle)), 0);
  if (position >= static_cast<u32>(value->end - value->begin))
    return false;
  u32 count = replacement->Strlen();
  const char *bytes = replacement->Data();
  u32 old_length = name->Strlen();
  value->Replace(position, old_length, bytes, count);
  return true;
}

// Copy the resolved string and replace the first named parameter in the caller's concrete output.
bool UiTextHandle::ReplaceParameter(ScString *name, ScString *replacement, ScString *out) {
  ScString value(*text);
  *out = value;
  const char *needle = name->ToChar();
  u32 position = UiFindText(*out, needle, static_cast<u32>(strlen(needle)), 0);
  if (position >= static_cast<u32>(out->end - out->begin))
    return false;
  u32 count = replacement->Strlen();
  const char *bytes = replacement->Data();
  u32 old_length = name->Strlen();
  out->Replace(position, old_length, bytes, count);
  return true;
}

// Try the executable's eight common keywords in their original short-circuit order.
bool UiTextHandle::ReplaceCommonParameter(ScString *value, i32 *position) {
  static const ScString names[] = {ScString("%MAYOR%"), ScString("%YOURNAME%"), ScString("%CITYNAME%"),
      ScString("%YOURCITY%"), ScString("%POPULATION%"), ScString("%YEAR%"), ScString("%ANYNEIGHBOR%"),
      ScString("%PARADENAME%")};
  for (const ScString &name : names)
    if (ReplaceParameter(value, name, position))
      return true;
  return false;
}

bool UiTextHandle::ReplaceParameter(ScString *value, UiTokenData *token, i32 *position) {
  ScString name(token->name);
  ScString part = value->Mid(static_cast<u32>(*position), name.Strlen());
  u32 length = name.Strlen();
  if (part.Strlen() != length || memcmp(part.Data(), name.Data(), length))
    return false;
  ScString replacement;
  switch (token->kind) {
  case 1: {
    UiTextHandle resource(token->instance, token->group);
    ScString copy = static_cast<ScString>(resource);
    replacement = copy;
    break;
  }
  case 2:
  case 3: {
    ScLanguageUtility *utility = ApGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return false;
    if (token->kind == 2)
      utility->MakeNumberString(token->number, &replacement);
    else
      utility->MakeMoneyString(token->number, &replacement);
    break;
  }
  case 4: {
    CtCity *city = ApGetCity();
    if (!city)
      return false;
    CtNeighborsLayer *neighbors = city->NeighborsLayer();
    if (!neighbors)
      return false;
    neighbors->GetNeighborName(static_cast<u8>(token->instance), &replacement);
    break;
  }
  case 5:
    replacement.Sprintf("%d", token->number);
    break;
  default:
    return false;
  }
  value->Replace(static_cast<u32>(*position), length, replacement.Data(), replacement.Strlen());
  *position = static_cast<i32>(static_cast<u32>(*position) + replacement.Strlen() - 1);
  return true;
}

// Replace a matching common keyword with its live city, population, date, neighbor or localized parade value.
bool UiTextHandle::ReplaceParameter(ScString *value, const ScString &name, i32 *position) {
  u32 start = static_cast<u32>(*position), length = name.Strlen();
  ScString part = value->Mid(start, length);
  if (part.Strlen() != length || memcmp(part.Data(), name.Data(), length))
    return false;
  if (UiTextEquals(name, "%MAYOR%") || UiTextEquals(name, "%YOURNAME%") || UiTextEquals(name, "%CITYNAME%") ||
      UiTextEquals(name, "%YOURCITY%")) {
    CtCity *city = ApGetCity();
    if (city) {
      bool mayor = UiTextEquals(name, "%MAYOR%") || UiTextEquals(name, "%YOURNAME%");
      ScStringView *first = mayor ? city->GetMayorName() : city->GetCityName();
      ScStringView *second = mayor ? city->GetMayorName() : city->GetCityName();
      u32 count = first->Strlen();
      const char *bytes = second->Data();
      value->Replace(start, length, bytes, count);
      ScStringView *third = mayor ? city->GetMayorName() : city->GetCityName();
      *position = static_cast<i32>(start + third->Strlen() - 1);
    }
    return true;
  }
  ScString replacement;
  if (UiTextEquals(name, "%POPULATION%")) {
    replacement.FromChar("0");
    CtCity *city = ApGetCity();
    if (city && city->ResidentialLayer())
      replacement.Sprintf("%d", city->ResidentialLayer()->GetPopulation());
  } else if (UiTextEquals(name, "%YEAR%")) {
    CtCity *city = ApGetCity();
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      replacement.Sprintf("%d", static_cast<i32>(year));
    } else {
      replacement.FromChar("1900");
    }
  } else if (UiTextEquals(name, "%ANYNEIGHBOR%")) {
    CtCity *city = ApGetCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return false;
    ScRandom random(kScRandomSeedFromClock);
    neighbors->GetNeighborName(static_cast<u8>(random.Bounded(4)), &replacement);
  } else if (UiTextEquals(name, "%PARADENAME%")) {
    i32 country = ApGetPreferences()->GetCountry();
    u32 instance = 0;
    switch (country) {
    case 6:
      instance = 0x258;
      break;
    case 17:
      instance = 0x259;
      break;
    case 19:
      instance = 0x25a;
      break;
    case 31:
      instance = 0x25b;
      break;
    case 32:
      instance = 0x25c;
      break;
    case 45:
      instance = 0x25d;
      break;
    case 46:
      instance = 0x25e;
      break;
    case 49:
      instance = 0x25f;
      break;
    case 54:
      instance = 0x260;
      break;
    }
    if (instance) {
      UiTextHandle resource(instance, kRsSupplementalGameTextGroup);
      ScString copy = static_cast<ScString>(resource);
      replacement = copy;
    }
  } else {
    return true;
  }
  value->Replace(start, length, replacement.Data(), replacement.Strlen());
  *position = static_cast<i32>(start + replacement.Strlen() - 1);
  return true;
}

// Publish a retained label only when its scoped text lookup succeeds; failed lookup leaves the caller output untouched.
bool UiCreateMenuLabel(u32 identifier, u32 group, u32 instance, UiMenuLabel **out) {
  UiMenuLabel *label = new UiMenuLabel();
  label->AddRef();
  UiTextHandle caption(instance, group);
  bool result = false;
  if (caption.GetText()) {
    label->SetID(identifier);
    label->SetCaption(*caption.GetText());
    label->SetFlag(kWnFlagReorderable, false);
    label->AddRef();
    result = true;
    *out = label;
  }
  label->Release();
  return result;
}
