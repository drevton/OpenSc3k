#include <stdlib.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_init_text.h"
#include "city/initialization.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "ui/ui_resources.h"
#include "window/api.h"

static bool CtInitApplicationInitialized;
static void *CtInitApplication;
static ScCommandFactory *CtCachedInitCommandFactory;
static RsManager *CtInitResourceManager;
static RsFileSystem *CtInitFileSystem;
static ScMessageServer *CtInitMessageServer;
static ScRegistry *CtInitRegistry;
static GxGraphics *CtInitGraphics;
static WnWindowManager *CtInitWindowManager;
static WnResources *CtInitWindowResources;
static ScLanguageManager *CtInitLanguageManager;

static void CtInitApplicationExit() {
}

// Borrow the framework directly from the module's process-lifetime director.
ScFramework *CtGetInitFramework() {
  return CtGetInitDirector()->FrameWork();
}

// Publish the lazy flag before querying the application so reentrant lookups observe the same cached output.
// Retain the published interface for process lifetime, including one published alongside a false query result.
ApGameInterface *CtGetInitApplication() {
  if (!CtInitApplicationInitialized) {
    CtInitApplicationInitialized = true;
    CtGetInitFramework()->Application()->QueryInterface(IID_ApGameInterface, &CtInitApplication);
    atexit(CtInitApplicationExit);
  }
  return static_cast<ApGameInterface *>(CtInitApplication);
}

// Borrow the application's current city on each call, returning null when no application was published.
CtCity *CtGetInitCity() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetCity() : nullptr;
}

int CtInitHasCity() {
  return CtGetInitCity() != nullptr;
}

// Cache a published command service before releasing its temporary acquisition reference.
// A null publication retries later, and a reentrant release observes the published cache.
ScCommandFactory *CtGetInitCommandFactory() {
  if (!CtCachedInitCommandFactory) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &value);
    CtCachedInitCommandFactory = static_cast<ScCommandFactory *>(value);
    if (CtCachedInitCommandFactory)
      CtCachedInitCommandFactory->Release();
  }
  return CtCachedInitCommandFactory;
}

// Cache the published message server before releasing the temporary service-acquisition reference.
// A null publication is retried on the next lookup.
ScMessageServer *CtGetInitMessageServer() {
  if (!CtInitMessageServer) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &value);
    CtInitMessageServer = static_cast<ScMessageServer *>(value);
    if (CtInitMessageServer)
      CtInitMessageServer->Release();
  }
  return CtInitMessageServer;
}

// Borrow the current application's agent-type tree, or return null when the application is absent.
CtAgentTree *CtGetInitAgentTree() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Cache the published resource manager before releasing its temporary service-acquisition reference.
// A null publication remains retryable and the returned pointer is borrowed.
RsManager *CtGetInitResourceManager() {
  if (!CtInitResourceManager) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &value);
    CtInitResourceManager = static_cast<RsManager *>(value);
    if (CtInitResourceManager)
      CtInitResourceManager->Release();
  }
  return CtInitResourceManager;
}

// Cache the published file-system service before releasing its temporary acquisition reference.
// A null publication is retried by the next lookup.
RsFileSystem *CtGetInitFileSystem() {
  if (!CtInitFileSystem) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kRsFileSystemServiceId, IID_RsFileSystem, &value);
    CtInitFileSystem = static_cast<RsFileSystem *>(value);
    if (CtInitFileSystem)
      CtInitFileSystem->Release();
  }
  return CtInitFileSystem;
}

// Borrow the current application's occupant-attribute cache, or return null when the application is absent.
OcAttribCache *CtGetInitAttribCache() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Cache the graphics service before releasing its acquired reference; retry a null result on a later call.
GxGraphics *CtGetInitGraphics() {
  if (!CtInitGraphics) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    CtInitGraphics = static_cast<GxGraphics *>(value);
    if (CtInitGraphics)
      CtInitGraphics->Release();
  }
  return CtInitGraphics;
}

// Cache the window manager before releasing its acquired reference; retry a null result on a later call.
WnWindowManager *CtGetInitWindowManager() {
  if (!CtInitWindowManager) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &value);
    CtInitWindowManager = static_cast<WnWindowManager *>(value);
    if (CtInitWindowManager)
      CtInitWindowManager->Release();
  }
  return CtInitWindowManager;
}

// Cache the window-resource service before releasing its acquired reference; retry a null publication later.
WnResources *CtGetInitWindowResources() {
  if (!CtInitWindowResources) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &value);
    CtInitWindowResources = static_cast<WnResources *>(value);
    if (CtInitWindowResources)
      CtInitWindowResources->Release();
  }
  return CtInitWindowResources;
}

// Cache the registry service before releasing the acquired reference; a null publication is retried later.
ScRegistry *CtGetInitRegistry() {
  if (!CtInitRegistry) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
    CtInitRegistry = static_cast<ScRegistry *>(value);
    if (CtInitRegistry)
      CtInitRegistry->Release();
  }
  return CtInitRegistry;
}

// Cache the language manager before releasing its acquired reference; retry a null result on a later call.
ScLanguageManager *CtGetInitLanguageManager() {
  if (!CtInitLanguageManager) {
    void *value = nullptr;
    CtGetInitFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &value);
    CtInitLanguageManager = static_cast<ScLanguageManager *>(value);
    if (CtInitLanguageManager)
      CtInitLanguageManager->Release();
  }
  return CtInitLanguageManager;
}

// Borrow the application's BoxX service, or return null when the application is absent.
ApBoxX *CtGetInitBox() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetBoxX() : nullptr;
}

// Borrow the application's image-resource factory, or return null when the application is absent.
RsImageOptions *CtGetInitImageFactory() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetBufferResourceFactory() : nullptr;
}

// Borrow the application's preferences, or return null when the application is absent.
ApPreferences *CtGetInitPreferences() {
  ApGameInterface *application = CtGetInitApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Query the image-cache service on each call and release the temporary acquisition before returning it.
// The returned pointer is borrowed; a failed lookup returns the framework's published value.
SpImageCache *CtGetInitImageCache() {
  SpImageCache *cache = nullptr;
  CtGetInitFramework()->GetSystemService(kSpImageCacheServiceId, IID_SpImageCache, reinterpret_cast<void **>(&cache));
  if (cache)
    cache->Release();
  return cache;
}

static ScString CtInitMayorToken("%MAYOR%"), CtInitYourNameToken("%YOURNAME%"),
    CtInitCityNameToken("%CITYNAME%"), CtInitYourCityToken("%YOURCITY%"), CtInitPopulationToken("%POPULATION%"),
    CtInitYearToken("%YEAR%"), CtInitParadeToken("%PARADENAME%"), CtInitNeighborToken("%ANYNEIGHBOR%");

CtInitText::CtInitText() : text(nullptr), key(0, 0, 0) {
}

CtInitText::CtInitText(u32 instance, u32 group) : text(nullptr), key(kRsTextResourceType, group, instance) {
  LoadResource();
}

CtInitText::CtInitText(const CtInitText &source) : text(nullptr), key(source.key) {
  LoadResource();
}

CtInitText::~CtInitText() {
  UnloadResource();
}

// Release the previous String and resolve a fresh copy of the source key unless assigning to this same handle.
CtInitText *CtInitText::operator=(const CtInitText &source) {
  if (&source != this) {
    UnloadResource();
    source.GetResKey(&key);
    LoadResource();
  }
  return this;
}

// Compare the resolved String with the supplied interface; an unresolved handle is never equal.
// The matching retail implementation is SC3U.
bool CtInitText::operator==(const ScStringView &other) {
  return text && text->IsEqual(&other);
}

// Copy the resolved String into independent storage, returning an empty String when resolution failed.
CtInitText::operator ScString() {
  if (text)
    return ScString(*text);
  ScString empty;
  return ScString(empty);
}

// Borrow the resolved resource String without retaining it or expanding its tokens.
// The matching retail implementation is SC3U.
const ScStringView *CtInitText::Value() const {
  return text;
}

// A missing resource produces an empty string.
ScString CtInitText::Val() {
  if (text) {
    ScString value(*text);
    ReplaceParameters(&value);
    return ScString(value);
  }
  ScString empty;
  return ScString(empty);
}

// A missing resource leaves the output untouched and returns false.
bool CtInitText::Detokenize(ScStringView *out) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value);
  out->FromChar(value.Data(), value.Strlen());
  return true;
}

bool CtInitText::Detokenize(ScStringView *out, UiTokenData **tokens, u32 count) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value, tokens, count);
  out->FromChar(value.Data(), value.Strlen());
  return true;
}

bool CtInitText::Detokenize(ScStringView *out, CtInitTokenCallback callback, CtInitTokenContext *context) {
  if (!text)
    return false;
  ScString value(*text);
  ReplaceParameters(&value, callback, context);
  out->FromChar(value.Data(), value.Strlen());
  return true;
}

// Replace the first occurrence of a parameter in the resolved concrete resource String.
// The matching retail implementation is SC3U and expects the text factory's String receiver.
bool CtInitText::ReplaceParameter(ScString *token, ScString *replacement) {
  ScString *value = static_cast<ScString *>(text);
  const char *needle = token->ToChar();
  usize length = strlen(needle), count = value->ScString::Strlen(), position = 0;
  while (position < count && (length > count - position || memcmp(value->ScString::Data() + position, needle, length)))
    ++position;
  if (position >= count)
    return false;
  u32 replacementLength = replacement->Strlen();
  const char *replacementData = replacement->Data();
  u32 tokenLength = token->Strlen();
  value->Replace(static_cast<u32>(position), tokenLength, replacementData, replacementLength);
  return true;
}

// Copy the resolved text and replace the first occurrence of the requested parameter in that copy.
// The matching retail implementation is SC3U; an end-position match is not replaced.
bool CtInitText::ReplaceParameter(ScString *token, ScString *replacement, ScString *out) {
  {
    ScString value(*text);
    *out = value;
  }
  const char *needle = token->ToChar();
  usize length = strlen(needle), count = out->ScString::Strlen(), position = 0;
  while (position < count && (length > count - position || memcmp(out->ScString::Data() + position, needle, length)))
    ++position;
  if (position >= count)
    return false;
  u32 replacementLength = replacement->Strlen();
  const char *replacementData = replacement->Data();
  u32 tokenLength = token->Strlen();
  out->Replace(static_cast<u32>(position), tokenLength, replacementData, replacementLength);
  return true;
}

// Continue scanning from the character after the resulting position.
void CtInitText::ReplaceParameters(ScString *value) {
  u32 next = 0;
  while (next < value->Strlen()) {
    const char *match = static_cast<const char *>(memchr(value->Data() + next, '%', value->Strlen() - next));
    if (!match)
      break;
    i32 position = static_cast<i32>(match - value->Data());
    ReplaceCommonParameter(value, &position);
    next = static_cast<u32>(position) + 1;
  }
}

// The matching retail implementation is SC3U and visits every supplied definition in order.
void CtInitText::ReplaceParameters(ScString *value, UiTokenData **tokens, u32 count) {
  u32 next = 0;
  while (next < value->ScString::Strlen()) {
    const char *match = static_cast<const char *>(memchr(value->ScString::Data() + next, '%',
        value->ScString::Strlen() - next));
    if (!match)
      break;
    i32 position = static_cast<i32>(match - value->ScString::Data());
    if (!ReplaceCommonParameter(value, &position) && tokens && count)
      for (u32 i = 0; i < count; ++i)
        ReplaceParameter(value, tokens[i], &position);
    next = static_cast<u32>(position) + 1;
  }
}

void CtInitText::ReplaceParameters(ScString *value, CtInitTokenCallback callback, CtInitTokenContext *context) {
  UiTokenData token;
  u32 next = 0;
  while (next < value->ScString::Strlen()) {
    const char *match = static_cast<const char *>(memchr(value->ScString::Data() + next, '%',
        value->ScString::Strlen() - next));
    if (!match)
      break;
    i32 position = static_cast<i32>(match - value->ScString::Data());
    if (!ReplaceCommonParameter(value, &position) && callback(*value, position, &token, context))
      ReplaceParameter(value, &token, &position);
    next = static_cast<u32>(position) + 1;
  }
}

// Each successful replacement may update the caller's scan position.
bool CtInitText::ReplaceCommonParameter(ScString *value, i32 *position) {
  return ReplaceParameter(value, CtInitMayorToken, position) || ReplaceParameter(value, CtInitYourNameToken, position) ||
      ReplaceParameter(value, CtInitCityNameToken, position) || ReplaceParameter(value, CtInitYourCityToken, position) ||
      ReplaceParameter(value, CtInitPopulationToken, position) || ReplaceParameter(value, CtInitYearToken, position) ||
      ReplaceParameter(value, CtInitNeighborToken, position) || ReplaceParameter(value, CtInitParadeToken, position);
}

bool CtInitText::ReplaceParameter(ScString *value, UiTokenData *token, i32 *position) {
  ScString name(static_cast<const ScStringView &>(token->name));
  ScString found = value->Mid(static_cast<u32>(*position), name.Strlen());
  if (found.Strlen() != name.Strlen() || memcmp(found.Data(), name.Data(), name.Strlen()))
    return false;
  ScString replacement;
  switch (token->kind) {
  case 1: {
    CtInitText resource(token->instance, token->group);
    replacement = static_cast<ScString>(resource);
    break;
  }
  case 2:
  case 3: {
    ScLanguageUtility *utility = CtGetInitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return false;
    if (token->kind == 2)
      utility->MakeNumberString(token->number, &replacement);
    else
      utility->MakeMoneyString(token->number, &replacement);
    break;
  }
  case 4: {
    CtCity *city = CtGetInitCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
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
  value->Replace(static_cast<u32>(*position), name.Strlen(), replacement.Data(), replacement.Strlen());
  *position += static_cast<i32>(replacement.Strlen()) - 1;
  return true;
}

bool CtInitText::ReplaceParameter(ScString *value, const ScString &token, i32 *position) {
  u32 length = token.ScString::Strlen();
  ScString found = value->Mid(static_cast<u32>(*position), length);
  if (found.Strlen() != length || memcmp(found.Data(), token.ScString::Data(), length))
    return false;
  auto isToken = [&token](const ScString &expected) {
    u32 size = token.ScString::Strlen();
    return size == expected.ScString::Strlen() &&
        !memcmp(token.ScString::Data(), expected.ScString::Data(), size);
  };
  if (isToken(CtInitMayorToken) || isToken(CtInitYourNameToken)) {
    CtCity *city = CtGetInitCity();
    if (city) {
      ScStringView *first = city->GetMayorName(), *second = city->GetMayorName();
      u32 size = first->Strlen();
      const char *data = second->Data();
      value->Replace(static_cast<u32>(*position), token.Strlen(), data, size);
      *position += static_cast<i32>(city->GetMayorName()->Strlen()) - 1;
    }
    return true;
  }
  if (isToken(CtInitCityNameToken) || isToken(CtInitYourCityToken)) {
    CtCity *city = CtGetInitCity();
    if (city) {
      ScStringView *first = city->GetCityName(), *second = city->GetCityName();
      u32 size = first->Strlen();
      const char *data = second->Data();
      value->Replace(static_cast<u32>(*position), token.Strlen(), data, size);
      *position += static_cast<i32>(city->GetCityName()->Strlen()) - 1;
    }
    return true;
  }
  if (isToken(CtInitPopulationToken)) {
    ScString fallback("0");
    CtCity *city = CtGetInitCity();
    if (city && city->ResidentialLayer()) {
      i32 population = city->ResidentialLayer()->GetPopulation();
      ScString formatted;
      formatted.Sprintf("%d", population);
      value->Replace(static_cast<u32>(*position), token.Strlen(), formatted.Data(), formatted.Strlen());
      *position += static_cast<i32>(formatted.Strlen()) - 1;
    } else {
      value->Replace(static_cast<u32>(*position), token.Strlen(), fallback.Data(), fallback.Strlen());
      *position += static_cast<i32>(fallback.Strlen()) - 1;
    }
    return true;
  }
  if (isToken(CtInitYearToken)) {
    CtCity *city = CtGetInitCity();
    ScString formatted;
    if (city) {
      u32 month, day, year;
      city->GetDate(&month, &day, &year);
      formatted.Sprintf("%d", static_cast<i32>(year));
    } else {
      formatted.FromChar("1900");
    }
    value->Replace(static_cast<u32>(*position), token.Strlen(), formatted.Data(), formatted.Strlen());
    *position += static_cast<i32>(formatted.Strlen()) - 1;
    return true;
  }
  if (isToken(CtInitNeighborToken)) {
    CtCity *city = CtGetInitCity();
    CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
    if (!neighbors)
      return false;
    ScRandom random(~u32(0));
    ScString name;
    neighbors->GetNeighborName(static_cast<u8>(random.Bounded(4)), &name);
    value->Replace(static_cast<u32>(*position), token.Strlen(), name.Data(), name.Strlen());
    *position += static_cast<i32>(name.Strlen()) - 1;
    return true;
  }
  if (isToken(CtInitParadeToken)) {
    u32 country = static_cast<u32>(CtGetInitPreferences()->GetCountry()), identifier = 0;
    switch (country) {
    case 6:
      identifier = 600;
      break;
    case 17:
      identifier = 601;
      break;
    case 19:
      identifier = 602;
      break;
    case 31:
      identifier = 603;
      break;
    case 32:
      identifier = 604;
      break;
    case 45:
      identifier = 605;
      break;
    case 46:
      identifier = 606;
      break;
    case 49:
      identifier = 607;
      break;
    case 54:
      identifier = 608;
      break;
    }
    ScString name;
    if (identifier) {
      CtInitText resource(identifier, kRsSupplementalGameTextGroup);
      name = static_cast<ScString>(resource);
    }
    value->Replace(static_cast<u32>(*position), token.Strlen(), name.Data(), name.Strlen());
    *position += static_cast<i32>(name.Strlen()) - 1;
  }
  return true;
}

// Borrow the stored resource key, including its original type word.
// The matching retail implementation is SC3U.
const RsKey *CtInitText::GetResKey() const {
  return &key;
}

void CtInitText::GetResKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
}

bool CtInitText::IsStringPresent() {
  return text != nullptr;
}

// Request the String interface through the real resource manager, clearing any pointer published with a false result.
void CtInitText::LoadResource() {
  bool result = CtGetInitResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text));
  if (!result)
    text = nullptr;
}

void CtInitText::UnloadResource() {
  ScRelease(&text);
}

// Fit text to the supplied width, appending the window resource's ellipsis when a shortened prefix can hold it.
// The trailing-space scan stops at the start of the string, excluding the installed all-space underread.
void CtInitText::FormatForEllipsis(ScString *value, i32 width, FtFont *font) {
  if (!font || !value->Strlen())
    return;
  u32 fitted = font->Fit(value->Data(), value->Strlen(), static_cast<u32>(width));
  if (!fitted) {
    value->Erase(0, value->Strlen());
    return;
  }
  if (fitted == value->Strlen())
    return;
  WnResources *resources = CtGetInitWindowResources();
  if (!resources)
    return;
  const ScStringView *tail = resources->GetText(kWnEllipsisTextId);
  ScString ellipsis;
  if (tail) {
    u32 count = tail->Strlen();
    const char *bytes = tail->Data();
    ellipsis.FromChar(bytes, count);
  }
  ScRect area{};
  if (ellipsis.Strlen())
    font->TextArea(ellipsis.Data(), ellipsis.Strlen(), &area);
  i32 ellipsisWidth = area.right - area.left;
  if (ellipsisWidth < width) {
    fitted = font->Fit(value->Data(), value->Strlen(),
        static_cast<u32>(width) + static_cast<u32>(area.left) - static_cast<u32>(area.right));
    value->Erase(fitted, value->Strlen() - fitted);
    value->RTrim();
    value->Append(ellipsis.Data(), ellipsis.Strlen());
  } else {
    value->Erase(0, value->Strlen());
  }
}
