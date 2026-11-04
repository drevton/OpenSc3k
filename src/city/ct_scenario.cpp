#define _CRT_SECURE_NO_WARNINGS
#include "city/ct_scenario.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cassert>
#include <string>

#include "advisors/ad_advice.h"
#include "advisors/ad_advisors.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_module.h"
#include "advisors/ad_petitions.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/rect.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_locust.h"
#include "disasters/ds_tornado.h"
#include "disasters/ds_toxic.h"
#include "disasters/ds_track.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "geometry/utilities.h"
#include "graphics/image_codecs.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "ui/advisor_dialogs.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_main_window.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// Scenario choices and the confirmation panel retain their separate artwork resources.
enum CtScenarioArtwork : u32 {
  kCtScenarioChoiceButtonImageId = 0xc5171c41,
  kCtScenarioConfirmBackgroundImageId = 0x42e55fd5,
};

// Scenario headers, portrait resources and executable scripts use independent resource-key namespaces.
enum CtScenarioResourceKey : u32 {
  kCtScenarioHeaderRecordType = 0x4845cbe,
  kCtScenarioHeaderRecordGroup = 0xc3de4d66,
  kCtScenarioPortraitImageGroup = 0x6504691d,
  kCtScenarioScriptResourceType = 0x23dfae5f,
  kCtScenarioScriptResourceGroup = 0x23dfae8c,
};

// Scenario confirmation controls and goal-state images retain their construction and callback identities.
enum CtScenarioConfirmControlId : u32 {
  kCtScenarioPortraitPanelId = 0x244d4a3f,
  kCtScenarioLeftPanelId = 0x244d4a40,
  kCtScenarioFooterPanelId = 0x244d4a41,
  kCtScenarioPortraitControlId = 0x244d4a42,
  kCtScenarioTitleLabelId = 0x244d4a3e,
  kCtScenarioBodyPanelId = 0x244d4a44,
  kCtScenarioTextEditorId = 0x244d4a43,
  kCtScenarioConfirmButtonId = 0x244d4a3d,
  kCtScenarioCloseButtonId = 0x244d4a45,
  kCtScenarioConfirmWindowId = 0x244d4a3c,
  kCtScenarioGoalAchievedImageId = 0xe50ed85a,
  kCtScenarioGoalPendingImageId = 0x252a754f,
};

enum CtScenarioEntryArchiveKey : u32 {
  kCtScenarioDialogEntriesRecordType = 0x44eb6bc8,
  kCtScenarioDialogEntriesRecordGroup = 0x44eb6bc9,
  kCtScenarioTickerEntriesRecordType = 0xa4846144,
  kCtScenarioTickerEntriesRecordGroup = 0xa48461a3,
};

// This module owns the same recursive lock used by script references and its resource factory.
static ScLock CtScenarioResourceLock;
RsManager *CtScenarioResourceManager = nullptr;
CtScenarioHandler *CtScenarioHandlers = nullptr;
i32 CtScenarioLastCommand = -1;
static ScArray<CtScenarioHandler> CtScenarioHandlerStorage;
static std::map<std::string, i32> CtScenarioCommandIds;

struct CtScenarioResourceFilter {
  u32 type, group;
};

static ScArray<CtScenarioResourceFilter> CtScenarioResourceFilters;
static CtScenarioLayerImpl *CtCurrentScenarioLayer = nullptr;

// Construct the original scenario layer and publish its primary Unknown identity to the director factory.
ScUnknown *CtCreateScenarioLayer() {
  return static_cast<CtScenarioLayer *>(new CtScenarioLayerImpl);
}

u8 CtScenarioExpandToken(ScString *text, u32 offset, CtScenarioToken *replacement, CtScenarioLayerImpl *layer) {
  return layer ? layer->ResolveToken(text, offset, replacement) : 0;
}

static u8 CtScenarioMatchToken(ScString *text, u32 offset, ScStringView *marker) {
  u32 length = text->Strlen(), markerLength = marker->Strlen();
  if (offset >= length)
    return markerLength == 0;
  return length - offset >= markerLength && memcmp(text->Data() + offset, marker->Data(), markerLength) == 0;
}

// Extract a percent-delimited scenario-variable marker and its index and format kind.
static u8 CtScenarioParseVariable(ScString *text, u32 offset, i32 *index, ScString *marker, u32 *kind) {
  u32 length = text->Strlen();
  if (offset >= length)
    return 0;
  const char *end = static_cast<const char *>(memchr(text->Data() + offset + 1, '%', length - offset - 1));
  if (!end)
    return 0;
  marker->FromChar(text->Data() + offset, u32(end - (text->Data() + offset)) + 1);
  if (strncmp(marker->ToChar(), "%SCNV", 5) != 0)
    return 0;
  char name[20];
  if (sscanf(marker->ToChar(), "%19s %d", name, index) != 2)
    return 0;
  switch (marker->Data()[5]) {
  case 'M':
    *kind = 3;
    return 1;
  case 'N':
    *kind = 2;
    return 1;
  case 'Y':
    *kind = 5;
    return 1;
  default:
    return 0;
  }
}

// A parsed variable must address the layer's fixed 512-word variable bank.
u8 CtScenarioLayerImpl::ResolveToken(ScString *text, u32 offset, CtScenarioToken *replacement) {
  ScString name;
  static ScString scenarioName("%SCNNAME%");
  if (CtScenarioMatchToken(text, offset, &scenarioName)) {
    if (progressCaption) {
      RsKey key;
      progressCaption->GetResourceKey(&key);
      CtScenarioToken token(1, scenarioName, i32(key.instance), i32(key.group));
      replacement->Assign(token);
      return 1;
    }
  } else {
    i32 index;
    u32 kind;
    if (CtScenarioParseVariable(text, offset, &index, &name, &kind)) {
      assert(u32(index) < 512);
      CtScenarioToken token(i32(kind), name, variables[index]);
      replacement->Assign(token);
      return 1;
    }
  }
  return 0;
}

CtScenarioToken::CtScenarioToken() : kind(0), name("%TOKEN%"), first(0), second(0), value(0) {
}

CtScenarioToken::CtScenarioToken(i32 tokenKind, const ScStringView &tokenName, i32 firstValue, i32 secondValue)
    : kind(tokenKind), first(firstValue), second(secondValue), value(0) {
  ScString copy(tokenName);
  name = copy;
}

CtScenarioToken::CtScenarioToken(i32 tokenKind, const ScStringView &tokenName, i32 number)
    : kind(tokenKind), first(0), second(0), value(number) {
  ScString copy(tokenName);
  name = copy;
}

CtScenarioToken::~CtScenarioToken() = default;

// Copy every token field except when the source and destination are the same object.
// The installed helper returns its destination pointer; its private nominal declaration is unidentified.
CtScenarioToken *CtScenarioToken::Assign(const CtScenarioToken &source) {
  if (this != &source) {
    kind = source.kind;
    name = source.name;
    first = source.first;
    second = source.second;
    value = source.value;
  }
  return this;
}

CtScenarioManipulator::CtScenarioManipulator(CtScenarioLayerImpl *value, i32 tickMode)
    : references(0), owner(value), mode(tickMode) {
}

// Publish the manipulator or Unknown identity and clear the output for an unsupported IID.
bool CtScenarioManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtScenarioManipulator::AddRef() {
  return ++references;
}

u32 CtScenarioManipulator::Release() {
  return ScReleaseImpl(this, &references);
}

bool CtScenarioManipulator::Apply(CtAgent *) {
  owner->TickScenario(mode);
  return true;
}

bool CtScenarioManipulator::IsLockingCity() {
  return false;
}

bool CtScenarioManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtScenarioManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool CtScenarioManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Query and cache the application interface once, including a refused or null publication.
static ApGameInterface *CtGetScenarioApplication() {
  static u8 initialized = 0;
  static ApGameInterface *application = nullptr;
  if (!(initialized & 1)) {
    initialized |= 1;
    CtGetScenarioFramework()->Application()->QueryInterface(
        IID_ApGameInterface, reinterpret_cast<void **>(&application));
  }
  return application;
}

// Borrow the command factory after releasing the framework's temporary service reference.
static ScCommandFactory *CtGetScenarioCommandFactory() {
  static ScCommandFactory *cached = nullptr;
  if (!cached) {
    ScCommandFactory *factory = nullptr;
    CtGetScenarioFramework()->GetSystemService(
        kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
    cached = factory;
    if (factory)
      factory->Release();
  }
  return cached;
}

// Run the original city-load command with its scenario selector, query flag and optional file path.
u8 CtScenarioOpenCity(const ScStringView *path) {
  UiCommandInterface *command = nullptr;
  if (!CtGetScenarioCommandFactory()->GetCommand(
          kCtLoadCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)))
    return 0;
  ScValue *selector = new ScValue(kCtFileOpenScenario, kScValueUint32);
  selector->AddRef();
  command->PushBackInput(selector);
  selector->Release();
  ScValue *query = new ScValue(false);
  query->AddRef();
  command->PushBackInput(query);
  query->Release();
  if (path) {
    ScValue *file = new ScValue(*path);
    file->AddRef();
    command->PushBackInput(file);
    file->Release();
  }
  u8 result = command->Execute();
  if (result && command->OutputQueueSize()) {
    ScVariant *output;
    command->PopFrontOutput(&output);
    result = *output->AsBool();
    output->Release();
  }
  command->Release();
  return result;
}

// Borrow the installed language manager after releasing the framework's temporary service reference.
static ScLanguageManager *CtGetScenarioLanguage() {
  static ScLanguageManager *cached = nullptr;
  if (!cached) {
    ScLanguageManager *manager = nullptr;
    CtGetScenarioFramework()->GetSystemService(
        kScLanguageManagerServiceId, IID_ScLanguageManager, reinterpret_cast<void **>(&manager));
    cached = manager;
    if (manager)
      manager->Release();
  }
  return cached;
}

// Borrow the window resource provider cached from the framework's system service.
// The temporary service reference is released after its address is retained.
WnResources *CtGetScenarioWindowResources() {
  static WnResources *cached = nullptr;
  if (!cached) {
    WnResources *resources = nullptr;
    CtGetScenarioFramework()->GetSystemService(
        kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&resources));
    cached = resources;
    if (resources)
      resources->Release();
  }
  return cached;
}

// Remove trailing ASCII spaces before the dialog's truncation suffix is appended.
void CtScenarioTrimTrailingSpaces(ScString *text) {
  u32 length = text->Strlen(), end = length;
  while (end && text->Data()[end - 1] == ' ')
    --end;
  if (end != length)
    text->Erase(end, length - end);
}

// Fit dialog text to the supplied font width, adding the resource-defined suffix when it also fits.
void CtScenarioTruncateText(ScString *text, i32 width, FtFont *font) {
  if (!font || !text->Strlen())
    return;
  u32 length = text->Strlen(), count = font->Fit(text->Data(), length, u32(width));
  if (!count) {
    text->Erase(0, length);
    return;
  }
  if (count == length)
    return;
  WnResources *resources = CtGetScenarioWindowResources();
  if (!resources)
    return;
  const ScStringView *value = resources->GetText(kWnEllipsisTextId);
  ScString suffix;
  if (value) {
    u32 suffixLength = value->Strlen();
    const char *suffixData = value->Data();
    suffix.FromChar(suffixData, suffixLength);
  }
  ScRect area{};
  if (suffix.Strlen())
    font->TextArea(suffix.Data(), suffix.Strlen(), &area);
  if (area.right - area.left < width) {
    count = font->Fit(text->Data(), text->Strlen(), u32(width + area.left - area.right));
    text->Erase(count, text->Strlen() - count);
    CtScenarioTrimTrailingSpaces(text);
    text->Append(suffix.Data(), suffix.Strlen());
  } else
    text->Erase(0, text->Strlen());
}

// Create and configure a label, attach its window to the dialog, and move it to the requested coordinates. If
// attachment fails, ask the window manager to destroy it.
u32 CtScenarioCreateLabel(WnWindow *parent, u32 id, i32 x, i32 y, const ScStringView &caption, FtFont *font,
    bool flag) {
  WnLabelControl *label = CtGetScenarioWindowResources()->CreateLabel(id, caption);
  if (!label)
    return 0;
  WnWindow *window = label->GetWindow();
  label->SetFont(font);
  label->SetBackground(false);
  label->SetLabelFlag(kWnLabelWrapText, flag);
  label->Fit();
  if (parent->ChildAdd(window))
    window->GZWinMoveTo(x, y);
  else
    parent->GetWindowManager()->DestroyWindow(window);
  return label->Release();
}

u8 CtScenarioApplyToken(ScString *text, CtScenarioToken *token, u32 *offset) {
  ScString tokenName(token->name);
  ScString found = text->Mid(*offset, tokenName.Strlen());
  if (!found.IsEqual(&tokenName))
    return 0;
  ScString replacement;
  switch (token->kind) {
  case 1: {
    CtScenarioText resource(u32(token->first), u32(token->second));
    replacement = resource.GetText();
    break;
  }
  case 2:
  case 3: {
    ScLanguageUtility *utility = CtGetScenarioLanguage()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    if (token->kind == 2)
      utility->MakeNumberString(i64(token->value), &replacement);
    else
      utility->MakeMoneyString(i64(token->value), &replacement);
    break;
  }
  case 4: {
    ApGameInterface *application = CtGetScenarioApplication();
    CtCity *currentCity = application ? application->GetCity() : nullptr;
    if (!currentCity)
      return 0;
    CtNeighborsLayer *neighbors = currentCity->NeighborsLayer();
    if (!neighbors)
      return 0;
    neighbors->GetNeighborName(u8(token->first), &replacement);
    break;
  }
  case 5:
    replacement.Sprintf("%d", token->value);
    break;
  default:
    return 0;
  }
  text->Replace(*offset, tokenName.Strlen(), replacement.Data(), replacement.Strlen());
  *offset += replacement.Strlen() - 1;
  return 1;
}

// Try the eight original built-in markers in their installed short-circuit order.
u8 CtScenarioApplyBuiltinToken(ScString *text, u32 *offset) {
  static ScString mayor("%MAYOR%"), yourName("%YOURNAME%"), cityName("%CITYNAME%"), yourCity("%YOURCITY%");
  static ScString population("%POPULATION%"), year("%YEAR%"), parade("%PARADENAME%"), neighbor("%ANYNEIGHBOR%");
  return CtScenarioApplyBuiltin(text, mayor, offset) || CtScenarioApplyBuiltin(text, yourName, offset) ||
      CtScenarioApplyBuiltin(text, cityName, offset) || CtScenarioApplyBuiltin(text, yourCity, offset) ||
      CtScenarioApplyBuiltin(text, population, offset) || CtScenarioApplyBuiltin(text, year, offset) ||
      CtScenarioApplyBuiltin(text, neighbor, offset) || CtScenarioApplyBuiltin(text, parade, offset);
}

static bool CtScenarioMarkerEquals(const ScStringView &marker, const char *literal) {
  usize length = strlen(literal);
  return marker.Strlen() == length && memcmp(marker.Data(), literal, length) == 0;
}

// Borrow the current city through the application, preserving the original nullable application branch.
static CtCity *CtGetScenarioCity() {
  ApGameInterface *application = CtGetScenarioApplication();
  return application ? application->GetCity() : nullptr;
}

// Select the advisor portrait using the advisor identity, mood and current language, falling back through installed
// keys.
u8 CtGetScenarioAdvisorInfo(u32 id, i32 mood, u8 alternate, RsKey *portrait, u32 *mode,
    ScStringView *personalName, ScStringView *name) {
  CtCity *city = CtGetScenarioCity();
  AdAdvisor *advisor;
  if (!city || !city->GetAdvisor(id, &advisor))
    return 0;
  advisor->GetPersonalName(personalName);
  advisor->GetName(name);
  u32 advisorNumber;
  switch (id) {
  case kAdUtilitiesAdvisorId:
    advisorNumber = 1;
    break;
  case kAdTransportationAdvisorId:
    advisorNumber = 6;
    break;
  case kAdBudgetAdvisorId:
    advisorNumber = 2;
    break;
  case kAdEnvironmentAdvisorId:
    advisorNumber = 4;
    break;
  case kAdPublicSafetyAdvisorId:
    advisorNumber = 5;
    break;
  case kAdCityPlanningAdvisorId:
    advisorNumber = 3;
    break;
  case kAdDemographicsAdvisorId:
    advisorNumber = 7;
    break;
  default:
    return 0;
  }
  if (!mood)
    mood = 3;
  u32 language = CtGetScenarioLanguage()->GetCurrentLanguage(), languageGroup = 0;
  if ((language >= kScLanguageFrench && language <= kScLanguagePortuguese) || language == kScLanguagePolish)
    languageGroup = 1;
  else if (language == kScLanguageJapanese)
    languageGroup = 2;
  u32 alternateBit = alternate ? kAdPortraitSmallFlag : 0;
  portrait->type = kRsImageResourceType;
  portrait->group = kRsAdvisorPortraitGroup;
  portrait->instance = alternateBit + 0x10000 * languageGroup + 256 * advisorNumber + u32(mood);
  RsManager *resources = CtGetScenarioResourceManager();
  if (!resources->TestForKey(portrait)) {
    portrait->instance = alternateBit + 256 * advisorNumber + u32(mood);
    if (!resources->TestForKey(portrait)) {
      portrait->instance = alternateBit + 256 * advisorNumber + 3;
      if (!resources->TestForKey(portrait)) {
        portrait->instance = 0x10303;
        if (!resources->TestForKey(portrait))
          return 0;
        *mode = 5;
        return 1;
      }
    }
  }
  *mode = ((~portrait->instance & kAdPortraitSmallFlag) | kAdPortraitModeBias) >> 22;
  return 1;
}

// Copy a petitioner's portrait and two text identities into the scenario dialog outputs.
// A returned petitioner record must provide both strings consumed by the original callback.
u8 CtGetScenarioPetitionInfo(u32 id, RsKey *portrait, u32 *mode, ScStringView *title, ScStringView *description) {
  CtCity *city = CtGetScenarioCity();
  if (!city)
    return 0;
  AdPetitionerManager *manager = city->GetPetitionerManager();
  if (!manager)
    return 0;
  AdPetitionerRecord record{};
  manager->GetPetitionerStructureFromID(id, &record);
  assert(record.text[0] && record.text[2]);
  title->Copy(*record.text[0]);
  description->Copy(*record.text[2]);
  *portrait = record.portrait;
  *mode = 1;
  return 1;
}

// Replace a matching built-in marker with the current city, population, date or localized parade text.
u8 CtScenarioApplyBuiltin(ScString *text, const ScStringView &marker, u32 *offset) {
  ScString found = text->Mid(*offset, marker.Strlen());
  if (!found.IsEqual(&marker))
    return 0;
  if (CtScenarioMarkerEquals(marker, "%MAYOR%") || CtScenarioMarkerEquals(marker, "%YOURNAME%")) {
    CtCity *currentCity = CtGetScenarioCity();
    if (currentCity) {
      ScStringView *lengthSource = currentCity->GetMayorName();
      ScStringView *dataSource = currentCity->GetMayorName();
      text->Replace(*offset, marker.Strlen(), dataSource->Data(), lengthSource->Strlen());
      *offset += currentCity->GetMayorName()->Strlen() - 1;
    }
    return 1;
  }
  if (CtScenarioMarkerEquals(marker, "%CITYNAME%") || CtScenarioMarkerEquals(marker, "%YOURCITY%")) {
    CtCity *currentCity = CtGetScenarioCity();
    if (currentCity) {
      ScStringView *lengthSource = currentCity->GetCityName();
      ScStringView *dataSource = currentCity->GetCityName();
      text->Replace(*offset, marker.Strlen(), dataSource->Data(), lengthSource->Strlen());
      *offset += currentCity->GetCityName()->Strlen() - 1;
    }
    return 1;
  }
  ScString replacement;
  if (CtScenarioMarkerEquals(marker, "%POPULATION%")) {
    replacement.FromChar("0");
    CtCity *currentCity = CtGetScenarioCity();
    if (currentCity && currentCity->ResidentialLayer())
      replacement.Sprintf("%d", currentCity->ResidentialLayer()->GetPopulation());
  } else if (CtScenarioMarkerEquals(marker, "%YEAR%")) {
    CtCity *currentCity = CtGetScenarioCity();
    if (currentCity) {
      u32 month, day, year;
      currentCity->GetDate(&month, &day, &year);
      replacement.Sprintf("%d", i32(year));
    } else
      replacement.FromChar("1900");
  } else if (CtScenarioMarkerEquals(marker, "%ANYNEIGHBOR%")) {
    CtCity *currentCity = CtGetScenarioCity();
    if (!currentCity)
      return 0;
    CtNeighborsLayer *neighbors = currentCity->NeighborsLayer();
    if (!neighbors)
      return 0;
    ScRandom random(kScRandomSeedFromClock);
    neighbors->GetNeighborName(u8(random.Bounded(4)), &replacement);
  } else if (CtScenarioMarkerEquals(marker, "%PARADENAME%")) {
    ApGameInterface *application = CtGetScenarioApplication();
    ApPreferences *preferences = application ? application->GetAppPreferences() : nullptr;
    u32 instance = 0;
    switch (preferences->GetCountry()) {
    case 6:
      instance = 600;
      break;
    case 17:
      instance = 601;
      break;
    case 19:
      instance = 602;
      break;
    case 31:
      instance = 603;
      break;
    case 32:
      instance = 604;
      break;
    case 45:
      instance = 605;
      break;
    case 46:
      instance = 606;
      break;
    case 49:
      instance = 607;
      break;
    case 54:
      instance = 608;
      break;
    }
    if (instance) {
      CtScenarioText resource(instance, kRsSupplementalGameTextGroup);
      if (resource.text)
        replacement.Copy(*resource.text);
    }
  } else
    return 1;
  text->Replace(*offset, marker.Strlen(), replacement.Data(), replacement.Strlen());
  *offset += replacement.Strlen() - 1;
  return 1;
}

CtScenarioTypeTest::CtScenarioTypeTest(u32 requestedType) : references(0), type(requestedType) {
  ApGameInterface *application = CtGetScenarioApplication();
  tree = application ? application->GetAppAgentTypeTree() : nullptr;
}

// Publish this filter for Unknown or the occupant-iterator-test identity and retain it on success.
bool CtScenarioTypeTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtScenarioTypeTest::AddRef() {
  return ++references;
}

u32 CtScenarioTypeTest::Release() {
  return ScReleaseImpl(this, &references);
}

// Accept occupants whose type is descended from the requested type in the borrowed agent tree.
bool CtScenarioTypeTest::Test(ScOccupant *occupant) {
  if (!tree)
    return false;
  // SCENARIO calls this at slot +36; the separately owned shared tree interface still needs that ordering repair.
  return tree->IsAncestor(type, occupant->TypeId());
}

// Destroy the filter without releasing its borrowed application tree.
CtScenarioTypeTest::~CtScenarioTypeTest() = default;

// Borrow the cached message server after releasing the framework's temporary service identity.
static ScMessageServer *CtGetScenarioMessageServer() {
  static ScMessageServer *cached = nullptr;
  if (!cached) {
    ScMessageServer *server = nullptr;
    CtGetScenarioFramework()->GetSystemService(
        kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&server));
    cached = server;
    if (server)
      server->Release();
  }
  return cached;
}

// Borrow the scenario application's current city window when the application exists.
static SpCityWindowView *CtGetScenarioCityView() {
  ApGameInterface *application = CtGetScenarioApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the current view through the scenario city window when one exists.
static SpViewInterface *CtGetScenarioCurrentView() {
  SpCityWindowView *view = CtGetScenarioCityView();
  return view ? view->GetCurrentView() : nullptr;
}

// Borrow the application's BoxX event service when the application exists.
static ApBoxX *CtGetScenarioBoxX() {
  ApGameInterface *application = CtGetScenarioApplication();
  return application ? application->GetBoxX() : nullptr;
}

static void CtScenarioPreCityLoad() {
  ApGameInterface *application = CtGetScenarioApplication();
  if (application)
    application->PreCityLoad();
}

static u8 CtScenarioMainMenu() {
  CtScenarioPreCityLoad();
  ApBoxX *box = CtGetScenarioBoxX();
  if (box)
    box->Event(kAuEventMainMenu, 0, 0, 0, 0);
  ApGameInterface *application = CtGetScenarioApplication();
  if (application) {
    UiMainWindow *window = application->GetSC3MainWindow();
    if (window) {
      window->DoTitleBackgroundScreen();
      window->AsIGZWin()->ShowWindow();
      window->DoMainMenu();
    }
  }
  return 1;
}

// Borrow the window manager after dropping the temporary framework service reference.
static WnWindowManager *CtGetScenarioWindowManager() {
  static WnWindowManager *cached = nullptr;
  if (!cached) {
    WnWindowManager *manager = nullptr;
    CtGetScenarioFramework()->GetSystemService(
        kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&manager));
    cached = manager;
    if (manager)
      manager->Release();
  }
  return cached;
}

// Add the localized mayor-name label and input field to the scenario progress dialog.
// Position the input beside the fitted label and apply its font, initial text and window color.
void CtScenarioCreateMayorInput(WnWindow *parent, FtFont *inputFont, FtFont *labelFont) {
  CtScenarioText initialText(kRsGameDefactoText, kRsGameTextGroup),
      labelText(kRsSupplementalGameGreetingsMayorText, kRsSupplementalGameTextGroup);
  i32 top = (104 - labelFont->Height(0)) / 2 - 2;
  ScString caption = labelText.GetText();
  CtScenarioCreateLabel(parent, kCtScenarioMayorLabelId, 18, top, caption, labelFont, false);
  WnWindow *label = parent->GetChildWindowFromID(kCtScenarioMayorLabelId);
  i32 left = (label ? label->GetW() + 18 : 18) + 2;
  WnTextControl *input = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnTextEditClassId, IID_WnTextControl, reinterpret_cast<void **>(&input))) {
    i32 height = inputFont->Height(0);
    WnWindow *window = input->AsIGZWin();
    input->SetFont(inputFont);
    window->SetArea(left, (104 - height) / 2, 341, (height + 104) / 2);
    window->SetID(kCtScenarioMayorEditId);
    ScString initial = initialText.GetText();
    input->SetText(initial);
    input->SetOutline(1);
    input->SetCharacterLimit(50);
    if (parent->ChildAdd(window))
      window->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorWhite));
    else
      CtGetScenarioWindowManager()->DestroyWindow(window);
    input->Release();
  }
}

// Load the requested portrait, optionally copy it into a bounded RGB16 buffer, and create an advisor button for it.
u8 CtScenarioCreatePortrait(u32 id, i32 frames, const RsKey &key, UiAdvisorButtonInterface **out) {
  RsKey imageKey(kRsImageResourceType, key.group, key.instance);
  GxBuffer *source;
  if (!CtGetScenarioResourceManager()->GetResource(imageKey, IID_GxBuffer, reinterpret_cast<void **>(&source)))
    return 0;
  GxBuffer *duplicate = nullptr;
  source->Duplicate(&duplicate, GxVramRequest::SystemMemory);
  GxBuffer *image;
  if (frames == 1) {
    if (!CtGetScenarioDirector()->GZCOM()->GetClassObject(
            kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&image)))
      return 0;
    ScRect original = *source->GetBufferArea(), area = original;
    if (area.right > 94)
      area.right = 94;
    if (area.bottom > 117)
      area.bottom = 117;
    image->Init(u32(area.right - area.left), u32(area.bottom - area.top), GxColorType(GxColorType::Color::RGB16),
        nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
    if (image->Lock(GxLockFlags::Blt)) {
      source->BltSrc(image, original, area, &area);
      image->Unlock(GxLockFlags::Blt);
    }
  } else
    image = source;
  *out = nullptr;
  if (!CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kUiAdvisorButtonClassId, IID_UiAdvisorButtonInterface, reinterpret_cast<void **>(out)))
    return 0;
  (*out)->AsIGZWin()->SetID(id);
  (*out)->SetAdvImage(2, image, nullptr, image ? frames : 1);
  return 1;
}

// The original scoped image object owns one graphics-buffer interface acquired from the resource manager.
class CtScenarioImageResource {
public:
  CtScenarioImageResource(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~CtScenarioImageResource();
  void LoadResource(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

// Select the image factory's mode and color format before loading the requested scenario image.
// Release the queried options and factory after the lookup, preserving their original ordering.
CtScenarioImageResource::CtScenarioImageResource(
    u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = CtGetScenarioResourceManager();
  RsFactoryInterface *factory = nullptr;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options = nullptr;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      LoadResource(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the held image interface and clear the stored pointer when this scope ends.
CtScenarioImageResource::~CtScenarioImageResource() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

void CtScenarioImageResource::LoadResource(RsManager *manager, u32 group, u32 instance) {
  RsKey key(kRsImageResourceType, group, instance);
  manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

CtScenarioProgress::CtScenarioProgress() : references(0), windowOpen(0), owner(nullptr), bound(0), persistText(1) {
}

CtScenarioProgress::~CtScenarioProgress() {
  if (bound)
    Unbind();
}

bool CtScenarioProgress::QueryInterface(u32 iid, void **out) {
  if (iid == IID_UiWindowCallback && !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (iid != IID_UiWindowCallback && iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 CtScenarioProgress::AddRef() {
  return ++references;
}

// Drop the progress controller's reference and destroy it when the signed count is no longer positive.
u32 CtScenarioProgress::Release() {
  --references;
  if (i32(references) <= 0) {
    delete this;
    return 0;
  }
  return references;
}

bool CtScenarioProgress::DoMessage(ScMessage *) {
  return true;
}

// Dispatch button, initialization and shutdown window events to their original handlers.
bool CtScenarioProgress::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if (first == 1)
      return OnButton(window, u32(second)) != 0;
  } else if (message == kWnMsg_Init) {
    if (window)
      return OnInit(window) != 0;
  } else if (message == kWnMsg_Shutdown && window)
    return OnShutdown(window) != 0;
  return false;
}

// Bind the controller to the scenario layer and retain that owner before marking the binding live.
u8 CtScenarioProgress::Bind(CtScenarioLayerImpl *layer) {
  owner = layer;
  layer->AddRef();
  bound = 1;
  return 1;
}

// Clear the binding flag before releasing and clearing the retained owner.
u8 CtScenarioProgress::Unbind() {
  if (bound) {
    bound = 0;
    ScRelease(&owner);
  }
  return 1;
}

// Replace the list contents with the scenario's current goal text and achieved or pending icon.
// Acquire each icon only when its first goal is encountered and release the retained images after insertion.
u8 CtScenarioProgress::PopulateGoals(WnIconListControl *list) {
  u32 pendingColor = CtGetScenarioWindowResources()->GetColor(kWnThemeTextColorId);
  u32 achievedColor = CtGetScenarioWindowResources()->GetColor(24);
  GxBuffer *pending = nullptr, *achieved = nullptr;
  list->RemoveAllListItems();
  owner->GetGoalList(&entries);
  for (CtScenarioProgressEntry &entry : entries) {
    GxBuffer **image = entry.achieved ? &achieved : &pending;
    if (!*image) {
      {
        CtScenarioImageResource resource(
            entry.achieved ? kCtScenarioGoalAchievedImageId : kCtScenarioGoalPendingImageId, kRsDialogImageGroup,
            GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
        *image = resource.buffer;
      }
      if (*image)
        (*image)->AddRef();
    }
    list->InsertListItem(*image, entry.text, entry.achieved ? achievedColor : pendingColor, -1);
  }
  if (achieved)
    achieved->Release();
  if (pending)
    pending->Release();
  return 1;
}

// Format the scenario's dialog text and place it in the progress dialog's editor control.
// An absent or invalid resource leaves the initially empty string in the control.
u8 CtScenarioProgress::SetDialogText(WnEditorControl *editor) {
  ScString text;
  owner->GetDialogText(&text);
  editor->SetText(text);
  return 1;
}

// Initialize the progress window's panels, goal list, editor and mayor-name controls from scenario resources.
u8 CtScenarioProgress::OnInit(WnWindow *window) {
  i32 portraitBottom = 0, portraitWidth = 94;
  FtFont *font = CtGetScenarioWindowResources()->GetFont(kWnFontUiSmall);
  FtFont *smallFont = CtGetScenarioWindowResources()->GetFont(kWnFontUiHeading);
  ScString title, caption;
  if (!window || !font || !smallFont)
    return 0;
  window->SetSize(474, 432);
  WnWindow *parent = window->GetMainWindow();
  if (parent) {
    i32 y = (parent->GetH() - window->GetH()) / 2;
    i32 x = (parent->GetW() - window->GetW()) / 2;
    window->GZWinMoveTo(x, y);
  }
  owner->GetProgressCaption(&caption);
  window->SetCaption(caption);
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect first = {349, 34, 470, 212}, second = {4, 394, 470, 427}, third = {4, 34, 14, 395};
    generic->AddPanel(kCtScenarioProgressPortraitPanelId, 0, 13, &first);
    generic->AddPanel(kCtScenarioProgressFooterPanelId, 0, 15, &second);
    generic->AddPanel(kCtScenarioProgressLeftBorderId, 0, 5, &third);
    generic->Release();
  }
  i32 advisor, group, mode;
  owner->GetScenarioAdvisor(&advisor, &group, &mode);
  RsKey key;
  u32 portraitMode = 5;
  {
    ScString description;
    u8 valid = 0;
    if (mode == 0)
      valid = CtGetScenarioPetitionInfo(u32(advisor), &key, &portraitMode, &title, &description);
    else if (mode == 1)
      valid = CtGetScenarioAdvisorInfo(u32(advisor), group, 0, &key, &portraitMode, &title, &description);
    else if (mode == 2)
      valid = owner->GetDialogEntry(u32(advisor), &key, &portraitMode, &title, &description);
    UiAdvisorButtonInterface *portrait = nullptr;
    if (valid && CtScenarioCreatePortrait(kCtScenarioProgressPortraitId, i32(portraitMode), key, &portrait)) {
      WnWindow *view = portrait->AsIGZWin();
      if (window->ChildAdd(view)) {
        u32 white = view->MakeFillColor(255, 255, 255), black = view->MakeFillColor(64, 64, 64);
        portrait->SetOutlineColors(black, white);
        view->GZWinMoveTo((121 - view->GetW()) / 2 + 349, 55);
        portraitBottom = view->GetB();
        portraitWidth = view->GetW();
      } else
        window->GetWindowManager()->DestroyWindow(view);
      portrait->Release();
    }
  }
  CtScenarioTruncateText(&title, 2 * portraitWidth, font);
  WnLabelControl *label = CtGetScenarioWindowResources()->CreateLabel(kCtScenarioProgressPortraitTitleId, title);
  if (label) {
    WnWindow *view = label->GetWindow();
    label->SetFont(font);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetAlignment(kWnLabelCenterMiddle);
    if (!window->ChildAdd(view))
      view->Shutdown();
    view->SetArea(349, portraitBottom, 470, 212);
    label->Release();
  }
  {
    CtScenarioText goalsCaption(kRsSupplementalGameScenarioGoalsText, kRsSupplementalGameTextGroup);
    if (goalsCaption.IsValid())
      CtScenarioCreateLabel(window, kCtScenarioGoalsHeadingId, 18, 220, *goalsCaption.GetString(), smallFont, false);
  }
  WnIconListControl *list = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnIconListClassId, IID_WnIconListControl, reinterpret_cast<void **>(&list))) {
    WnWindow *view = list->AsIGZWin();
    list->EnableScrollBar(1, 0);
    view->SetID(kCtScenarioGoalsListId);
    view->SetFlag(kWnFlagReorderable, false);
    list->SetDrawSunkenRect(1);
    list->SetOpaque(1);
    list->SetListItemFontEffects(font);
    ScRect area = {18, 244, 470, 385};
    view->SetArea(area);
    list->SetIconToTextSpacingEffects(4);
    if (window->ChildAdd(view))
      PopulateGoals(list);
    else
      window->GetWindowManager()->DestroyWindow(view);
  }
  WnFlatControl *flat = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
    WnWindow *view = flat->GetWindow();
    view->SetID(kCtScenarioProgressTextBackgroundId);
    view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorUiBackground));
    flat->SetOutlineFlags(32);
    view->SetArea(18, persistText ? 70 : 56, 341, 210);
    if (window->ChildAdd(view)) {
      view->SendToBack();
      view->SetFlag(kWnFlagReorderable, false);
    } else
      window->GetWindowManager()->DestroyWindow(view);
    flat->Release();
  }
  WnEditorControl *editor = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
    WnWindow *view = editor->AsIGZWin();
    i32 top = persistText ? 70 : 56;
    view->SetID(kCtScenarioProgressTextEditorId);
    editor->SetFont(font);
    editor->SetTextGutterX(4);
    view->SetSize(319, 204 - top);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    SetDialogText(editor);
    if (window->ChildAdd(editor->AsIGZWin()))
      view->GZWinMoveTo(20, top + 3);
    else
      window->GetWindowManager()->DestroyWindow(view);
    editor->Release();
  }
  GxBuffer *closeImage;
  {
    CtScenarioImageResource image(
        kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
    closeImage = image.buffer;
  }
  WnButton *close;
  {
    ScString empty;
    close = CtGetScenarioWindowResources()->CreateButton(
        kCtScenarioProgressCloseId, empty, closeImage, closeImage->GetBufferArea(), 32);
  }
  if (close) {
    WnWindow *view = close->GetWindow();
    close->SetButtonFlag(kWnButtonShowText, false);
    view->SetFlag(kWnFlagReorderable, false);
    view->GZWinMoveTo(422, 394);
    window->ChildAdd(view);
    close->Release();
  }
  if (persistText)
    CtScenarioCreateMayorInput(window, font, smallFont);
  else {
    CtScenarioText prompt(5, kRsSupplementalGameTextGroup);
    if (prompt.IsValid()) {
      i32 top = (90 - smallFont->Height(0)) / 2;
      CtScenarioCreateLabel(window, kCtScenarioStatusHeadingId, 18, top, *prompt.GetString(), smallFont, false);
    }
  }
  windowOpen = 1;
  return 1;
}

u8 CtScenarioProgress::OnButton(WnWindow *window, u32 command) {
  ApBoxX *box = CtGetScenarioBoxX();
  if (command != kUiFrameCloseButtonId && command != kCtScenarioProgressCloseId)
    return 0;
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
  CloseWindow(window);
  return 1;
}

// Save a nonempty mayor name when requested, then close the modal window or notify its parent.
u8 CtScenarioProgress::CloseWindow(WnWindow *window) {
  ScString name;
  if (!window)
    return 0;
  if (persistText) {
    WnWindow *child = window->GetChildWindowFromID(kCtScenarioMayorEditId);
    WnTextControl *edit = nullptr;
    if (child && child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&edit))) {
      edit->GetText(&name);
      if (name.Strlen())
        CtGetScenarioCity()->SetMayorName(name);
    }
  }
  if (window->GetFlag(kWnFlagModal))
    CtGetScenarioWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  else {
    WnWindowManager *manager = window->GetWindowManager();
    manager->SendMessageToTarget(window->GetParentWin(), kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  }
  windowOpen = 0;
  return 1;
}

u8 CtScenarioProgress::OnShutdown(WnWindow *window) {
  ApAdvisorPrefs *settings = CtGetScenarioApplication()->GetAppPreferences()->AdvMgrPrefs();
  settings->main_x = u32(window->GetL());
  settings->main_y = u32(window->GetT());
  settings->ready = 1;
  windowOpen = 0;
  return 1;
}

u8 CtScenarioProgress::CreateProgressWindow(WnWindow **out, bool persist) {
  *out = nullptr;
  if (windowOpen)
    return 0;
  persistText = persist;
  UiGenericInterface *generic = nullptr;
  if (!CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericCloseButton, 1);
  generic->GetWindow()->PrivateBuffer(true);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtScenarioProgressWindowId);
  return 1;
}

CtScenarioChoiceDialog::CtScenarioChoiceDialog()
    : references(0), windowOpen(0), owner(nullptr), bound(0), persistText(1) {
}

CtScenarioChoiceDialog::~CtScenarioChoiceDialog() {
  if (bound)
    Unbind();
}

bool CtScenarioChoiceDialog::QueryInterface(u32 iid, void **out) {
  if (iid == IID_UiWindowCallback && !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (iid != IID_UiWindowCallback && iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 CtScenarioChoiceDialog::AddRef() {
  return ++references;
}

// Drop the choice dialog's reference and destroy it when the signed count is no longer positive.
u32 CtScenarioChoiceDialog::Release() {
  --references;
  if (i32(references) <= 0) {
    delete this;
    return 0;
  }
  return references;
}

// Dispatch choice buttons and window lifecycle messages, retaining the original temporary string lifetime.
bool CtScenarioChoiceDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  ScString temporary;
  if (message == kWnMsg_Command) {
    if (first == 1)
      return OnButton(window, u32(second)) != 0;
  } else if (message == kWnMsg_Init) {
    if (window)
      return OnInit(window) != 0;
  } else if (message == kWnMsg_Shutdown && window)
    return OnShutdown(window) != 0;
  return false;
}

// Retain the scenario layer before marking this dialog's binding active.
u8 CtScenarioChoiceDialog::Bind(CtScenarioLayerImpl *layer) {
  owner = layer;
  layer->AddRef();
  bound = 1;
  return 1;
}

// Clear the active binding before releasing and clearing its owner.
u8 CtScenarioChoiceDialog::Unbind() {
  if (bound) {
    bound = 0;
    ScRelease(&owner);
  }
  return 1;
}

// Initialize the choice window's portrait, rank list, result editor and three localized selection controls.
u8 CtScenarioChoiceDialog::OnInit(WnWindow *window) {
  i32 portraitBottom = 0, portraitWidth = 94;
  FtFont *font = CtGetScenarioWindowResources()->GetFont(kWnFontUiSmall);
  FtFont *smallFont = CtGetScenarioWindowResources()->GetFont(kWnFontUiHeading);
  ScString title, caption, description;
  WnWindow *mainWindow = window->GetMainWindow();
  if (!font || !smallFont)
    return 0;
  window->SetSize(445, 398);
  if (mainWindow) {
    i32 y = (mainWindow->GetH() - window->GetH()) / 2;
    i32 x = (mainWindow->GetW() - window->GetW()) / 2;
    window->GZWinMoveTo(x, y);
  }
  owner->GetProgressCaption(&caption);
  window->SetCaption(caption);
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect first = {320, 34, 441, 188}, second = {4, 34, 13, 392}, third = {12, 294, 441, 303};
    generic->AddPanel(kCtScenarioChoicePortraitPanelId, 0, 13, &first);
    generic->AddPanel(kCtScenarioChoiceLeftBorderId, 0, 5, &second);
    generic->AddPanel(kCtScenarioChoiceTopDividerId, 0, 14, &third);
    ScRect divider = {146, 302, 155, 386};
    generic->AddPanel(kCtScenarioChoiceFirstDividerId, 0, 5, &divider);
    divider.left += 142;
    divider.right += 142;
    generic->AddPanel(kCtScenarioChoiceSecondDividerId, 0, 5, &divider);
    divider.left += 142;
    divider.right = 441;
    generic->AddPanel(kCtScenarioChoiceRightBorderId, 0, 5, &divider);
    ScRect bottom = {12, 383, 441, 392};
    generic->AddPanel(kCtScenarioChoiceBottomBorderId, 0, 14, &bottom);
    generic->Release();
  }
  i32 advisor, group, mode;
  owner->GetScenarioAdvisor(&advisor, &group, &mode);
  RsKey key;
  u32 portraitMode = 5;
  u8 valid = u8((reinterpret_cast<uptr>(window) >> 24) & 0xff);
  if (mode == 0)
    valid = CtGetScenarioPetitionInfo(u32(advisor), &key, &portraitMode, &title, &description);
  else if (mode == 1)
    valid = CtGetScenarioAdvisorInfo(u32(advisor), group, 0, &key, &portraitMode, &title, &description);
  else if (mode == 2)
    valid = owner->GetDialogEntry(u32(advisor), &key, &portraitMode, &title, &description);
  UiAdvisorButtonInterface *portrait = nullptr;
  if (valid && CtScenarioCreatePortrait(kCtScenarioChoicePortraitId, i32(portraitMode), key, &portrait)) {
    WnWindow *view = portrait->AsIGZWin();
    if (window->ChildAdd(view)) {
      u32 white = view->MakeFillColor(255, 255, 255), black = view->MakeFillColor(64, 64, 64);
      portrait->SetOutlineColors(black, white);
      view->GZWinMoveTo((121 - view->GetW()) / 2 + 320, 34);
      portraitBottom = view->GetB();
      portraitWidth = view->GetW();
    } else
      window->GetWindowManager()->DestroyWindow(view);
    portrait->Release();
  }
  CtScenarioTruncateText(&title, 2 * portraitWidth, font);
  WnLabelControl *label = CtGetScenarioWindowResources()->CreateLabel(kCtScenarioChoicePortraitTitleId, title);
  if (label) {
    WnWindow *view = label->GetWindow();
    label->SetFont(font);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetAlignment(kWnLabelCenterMiddle);
    if (!window->ChildAdd(view))
      view->Shutdown();
    view->SetArea(320, portraitBottom, 441, 188);
    label->Release();
  }
  {
    CtScenarioText heading(kRsSupplementalGameScenarioCompletedText, kRsSupplementalGameTextGroup);
    if (heading.IsValid()) {
      i32 top = (89 - smallFont->Height(0)) / 2;
      CtScenarioCreateLabel(window, kCtScenarioCompletedHeadingId, 15, top, *heading.GetString(), smallFont, false);
    }
  }
  WnFlatControl *flat = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
    WnWindow *view = flat->GetWindow();
    view->SetID(kCtScenarioRanksBackgroundId);
    view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorUiBackground));
    flat->SetOutlineFlags(32);
    view->SetArea(15, 55, 315, 190);
    if (window->ChildAdd(view)) {
      view->SendToBack();
      view->SetFlag(kWnFlagReorderable, false);
    } else
      window->GetWindowManager()->DestroyWindow(view);
    flat->Release();
  }
  {
    CtScenarioText heading(kRsSupplementalGameMayorResultsFormatText, kRsSupplementalGameTextGroup);
    if (heading.IsValid()) {
      ScString formatted;
      heading.Format(&formatted, CtScenarioExpandToken, owner);
      CtScenarioCreateLabel(window, kCtScenarioMayorResultsHeadingId, 17, 57, formatted, font, false);
    }
  }
  WnIconListControl *list = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnIconListClassId, IID_WnIconListControl, reinterpret_cast<void **>(&list))) {
    WnWindow *view = list->AsIGZWin();
    list->EnableScrollBar(1, 0);
    view->SetID(kCtScenarioRanksListId);
    view->SetFlag(kWnFlagReorderable, false);
    list->SetDrawSunkenRect(0);
    list->SetOpaque(1);
    list->SetListItemFontEffects(font);
    ScRect area = {17, 80, 315, 190};
    view->SetArea(area);
    list->SetIconToTextSpacingEffects(4);
    if (window->ChildAdd(view))
      PopulateRanks(list);
    else
      window->GetWindowManager()->DestroyWindow(view);
  }
  flat = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
    WnWindow *view = flat->GetWindow();
    view->SetID(kCtScenarioChoiceOptionBackgroundId);
    view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorUiBackground));
    flat->SetOutlineFlags(32);
    view->SetArea(15, 202, 441, 292);
    if (window->ChildAdd(view)) {
      view->SendToBack();
      view->SetFlag(kWnFlagReorderable, false);
    } else
      window->GetWindowManager()->DestroyWindow(view);
    flat->Release();
  }
  WnEditorControl *editor = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
    ScString result;
    WnWindow *view = editor->AsIGZWin();
    view->SetID(kCtScenarioResultsTextEditorId);
    editor->SetFont(font);
    editor->SetTextGutterX(4);
    view->SetSize(422, 84);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    owner->GetResultsDialogText(&result);
    editor->SetText(result);
    if (window->ChildAdd(editor->AsIGZWin()))
      view->GZWinMoveTo(15, 204);
    else
      window->GetWindowManager()->DestroyWindow(view);
    editor->Release();
  }
  {
    ScRect buttonArea = {0, 0, 26, 25};
    ScString empty, optionText;
    CtScenarioImageResource buttonImage(kCtScenarioChoiceButtonImageId, kRsDialogImageGroup,
        GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
    GxBuffer *image = buttonImage.buffer;
    if (image)
      image->GetBufferArea(&buttonArea);
    for (i32 index = 0; index < 3; ++index) {
      i32 left = 15 + 142 * index;
      u32 id = kCtScenarioReplayId + u32(index);
      flat = nullptr;
      if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
              kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
        WnWindow *view = flat->GetWindow();
        view->SetID(kCtScenarioChoiceOptionBackgroundId);
        view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorUiBackground));
        flat->SetOutlineFlags(32);
        view->SetArea(left, 305, left + 128, 381);
        if (window->ChildAdd(view)) {
          view->SendToBack();
          view->SetFlag(kWnFlagReorderable, false);
        } else
          window->GetWindowManager()->DestroyWindow(view);
        flat->Release();
      }
      WnButton *button = CtGetScenarioWindowResources()->CreateFixedButton(id, empty, image, &buttonArea, 0);
      if (button) {
        WnWindow *view = button->GetWindow();
        if (!image) {
          view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorSilver));
          view->SetSize(26, 25);
          button->SetButtonFlag(kWnButtonAutoFit, false);
        }
        button->SetButtonFlag(kWnButtonShowText, false);
        if (window->ChildAdd(view))
          view->GZWinMoveTo(left + 64 - view->GetW() / 2, 345);
        else
          window->GetWindowManager()->DestroyWindow(view);
        button->Release();
      }
      {
        CtScenarioText choice(8 + u32(index), kRsSupplementalGameTextGroup);
        optionText = choice.GetText();
      }
      WnLabelControl *choiceLabel = CtGetScenarioWindowResources()->CreateLabel(id, optionText);
      if (choiceLabel) {
        choiceLabel->SetFont(font);
        choiceLabel->GetWindow()->SetSize(100, 30);
        choiceLabel->SetBackground(false);
        choiceLabel->SetLabelFlag(kWnLabelWrapText, true);
        choiceLabel->Fit();
        if (window->ChildAdd(choiceLabel->GetWindow())) {
          choiceLabel->SetAlignment(kWnLabelCenterMiddle);
          choiceLabel->GetWindow()->GZWinMoveTo(left + 14, 312);
        } else
          window->GetWindowManager()->DestroyWindow(choiceLabel->GetWindow());
        choiceLabel->Release();
      }
    }
  }
  windowOpen = 1;
  return 1;
}

// Replace the choice list with formatted scenario ranks and mark the currently selected entry with its image.
u8 CtScenarioChoiceDialog::PopulateRanks(WnIconListControl *list) {
  u32 selectedColor = CtGetScenarioWindowResources()->GetColor(kWnThemeTextColorId);
  u32 otherColor = CtGetScenarioWindowResources()->GetColor(24);
  std::vector<ScString> ranks;
  i32 selected;
  GxBuffer *selectedImage = nullptr;
  list->RemoveAllListItems();
  owner->GetRankList(&ranks, &selected);
  i32 index = 0;
  for (ScString &rank : ranks) {
    if (index == selected) {
      if (!selectedImage) {
        {
          CtScenarioImageResource image(kCtScenarioGoalAchievedImageId, kRsDialogImageGroup,
              GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
          selectedImage = image.buffer;
        }
        if (selectedImage)
          selectedImage->AddRef();
      }
      list->InsertListItem(selectedImage, rank, selectedColor, -1);
    } else
      list->InsertListItem(nullptr, rank, otherColor, -1);
    ++index;
  }
  return 1;
}

u8 CtScenarioChoiceDialog::OnShutdown(WnWindow *) {
  windowOpen = 0;
  return 1;
}

u8 CtScenarioChoiceDialog::CloseWindow(WnWindow *window, i32 result) {
  if (!window)
    return 0;
  CtGetScenarioWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), uptr(result));
  windowOpen = 0;
  return 1;
}

// Translate the three choice button IDs to modal results and announce the click before closing.
u8 CtScenarioChoiceDialog::OnButton(WnWindow *window, u32 command) {
  ApBoxX *box = CtGetScenarioBoxX();
  if (command < kCtScenarioReplayId || command > kCtScenarioMainMenuId)
    return 0;
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
  CloseWindow(window, i32(command - kCtScenarioReplayId));
  return 1;
}

// Create the choice window with the installed styles, attach this callback and publish its window identity.
u8 CtScenarioChoiceDialog::CreateChoiceWindow(WnWindow **out) {
  *out = nullptr;
  if (windowOpen)
    return 0;
  UiGenericInterface *generic = nullptr;
  if (!CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericCloseButton, 0);
  generic->GetWindow()->PrivateBuffer(true);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtScenarioChoiceWindowId);
  return 1;
}

i32 CtScenarioLayerImpl::EndScenario(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  ScMessageServer *server = CtGetScenarioMessageServer();
  i32 rank = EvaluateArgument(command, 0);
  selectedRank = ranks.find(u32(rank)) == ranks.end() ? -1 : rank;
  DisableLoadSave();
  SetRunning(0);
  if (finishWindow)
    return 0;
  if (choiceDialog->CreateChoiceWindow(&finishWindow)) {
    city->SimHiddenPause();
    if (!finishNotificationRegistered)
      finishNotificationRegistered = server->AddNotification(static_cast<CtLayer *>(this), kApMsgIdle);
  }
  return 1;
}

// Count the selected appearance across the whole city using the larger of its two cell bounds.
i32 CtScenarioLayerImpl::CountStructuresWithAppearanceInCity(CtScenarioCommand *command, i32 execute) {
  u32 radius = width, destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  RsKey key;
  key.type = u32(EvaluateArgument(command, 1));
  key.group = u32(EvaluateArgument(command, 2));
  key.instance = u32(EvaluateArgument(command, 3));
  if (height > radius)
    radius = height;
  return CountAppearance(destination, key, 0, 0, radius + 1);
}

// Count an appearance around a one-based location, clamping out-of-range coordinates to zero.
i32 CtScenarioLayerImpl::CountStructuresWithAppearanceNearLocation(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = 0;
  RsKey key;
  key.type = u32(EvaluateArgument(command, 1));
  key.group = u32(EvaluateArgument(command, 2));
  key.instance = u32(EvaluateArgument(command, 3));
  u32 x = u32(EvaluateArgument(command, 4)) - 1, z = u32(EvaluateArgument(command, 5)) - 1;
  u32 radius = u32(EvaluateArgument(command, 6));
  if (x > width)
    x = 0;
  if (z > height)
    z = 0;
  return CountAppearance(destination, key, x, z, radius);
}

// Enumerate surface occupants in the clipped cell square and count those whose attribute key matches all three words.
i32 CtScenarioLayerImpl::CountAppearance(u32 destination, const RsKey &key, u32 x, u32 z, u32 radius) {
  u32 left = x > radius ? x - radius : 0, top = z > radius ? z - radius : 0;
  u32 right = x + radius, bottom = z + radius;
  if (right > width)
    right = width;
  if (bottom > height)
    bottom = height;
  GeBounds bounds;
  bounds.minimum = {i32(left << 8), i32(top << 8), 0};
  bounds.maximum = {i32(right << 8), i32(bottom << 8), 0};
  variables[destination] = 0;
  OcIterator *iterator;
  city->SurfaceOccupantManager()->GetOccupants(&iterator, &bounds, nullptr);
  for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next()) {
    GeAttrib *attributes = occupant->GetAttribData();
    if (attributes) {
      RsKey *candidate = attributes->Key();
      if (candidate->type == key.type && candidate->group == key.group && candidate->instance == key.instance)
        variables[destination] = i32(u32(variables[destination]) + 1);
    }
  }
  iterator->Release();
  return 1;
}

// Count matching occupants, checking each underground layer only when the preceding layer had no matches.
i32 CtScenarioLayerImpl::CountStructures(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 destination;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  u32 type = u32(EvaluateArgument(command, 1));
  CtScenarioTypeTest *test = new CtScenarioTypeTest(type);
  test->AddRef();
  variables[destination] = 0;
  for (u32 level = 0; level < 3 && !variables[destination]; ++level) {
    ScOccupantManager *manager = level == 0 ? city->SurfaceOccupantManager()
        : level == 1                        ? city->UndergroundOccupantManagerLevelOne()
                                            : city->UndergroundOccupantManagerLevelTwo();
    OcIterator *iterator;
    manager->GetOccupants(&iterator, nullptr, test);
    for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next())
      variables[destination] = i32(u32(variables[destination]) + 1);
    iterator->Release();
  }
  test->Release();
  return 1;
}

// Count matching occupants near a clipped one-based location, falling back through underground layers only if empty.
i32 CtScenarioLayerImpl::CountStructuresNearLocation(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 destination;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = 0;
  u32 type = u32(EvaluateArgument(command, 1));
  u32 x = u32(EvaluateArgument(command, 2)) - 1, z = u32(EvaluateArgument(command, 3)) - 1;
  u32 radius = u32(EvaluateArgument(command, 4));
  if (x > width)
    x = 0;
  if (z > height)
    z = 0;
  u32 left = x > radius ? x - radius : 0, top = z > radius ? z - radius : 0;
  u32 right = x + radius, bottom = z + radius;
  if (right > width)
    right = width;
  if (bottom > height)
    bottom = height;
  GeBounds bounds;
  bounds.minimum = {i32(left << 8), i32(top << 8), 0};
  bounds.maximum = {i32(right << 8), i32(bottom << 8), 0};
  CtScenarioTypeTest *test = new CtScenarioTypeTest(type);
  test->AddRef();
  for (u32 level = 0; level < 3 && !variables[destination]; ++level) {
    ScOccupantManager *manager = level == 0 ? city->SurfaceOccupantManager()
        : level == 1                        ? city->UndergroundOccupantManagerLevelOne()
                                            : city->UndergroundOccupantManagerLevelTwo();
    OcIterator *iterator;
    manager->GetOccupants(&iterator, &bounds, test);
    for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next())
      variables[destination] = i32(u32(variables[destination]) + 1);
    iterator->Release();
  }
  test->Release();
  return 1;
}

i32 CtScenarioLayerImpl::PopUpMsg(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DisableLoadSave();
  CtScenarioPopupMessage *payload = new CtScenarioPopupMessage;
  payload->first = EvaluateArgument(command, 0);
  payload->second = EvaluateArgument(command, 1);
  payload->third = EvaluateArgument(command, 2) == 1;
  payload->fourth = EvaluateArgument(command, 3);
  payload->fifth = EvaluateArgument(command, 4);
  payload->sixth = EvaluateArgument(command, 5);
  ScMessage message{kCtMsg_ShowScenarioPopup, reinterpret_cast<uptr>(payload), 0, 0};
  if (popupFlag)
    scriptPaused = 1;
  CtGetScenarioMessageServer()->MessagePost(message, false);
  return 1;
}

CtScenarioConfirmDialog::CtScenarioConfirmDialog()
    : references(0), windowOpen(0), owner(nullptr), bound(0), allowConfirm(0) {
}

CtScenarioConfirmDialog::~CtScenarioConfirmDialog() {
  if (bound)
    Unbind();
}

bool CtScenarioConfirmDialog::QueryInterface(u32 iid, void **out) {
  if (iid == IID_UiWindowCallback && !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (iid != IID_UiWindowCallback && iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 CtScenarioConfirmDialog::AddRef() {
  return ++references;
}

// Drop a reference and destroy the confirmation callback when its signed count is no longer positive.
u32 CtScenarioConfirmDialog::Release() {
  --references;
  if (i32(references) <= 0) {
    delete this;
    return 0;
  }
  return references;
}

// Dispatch confirmation button, keyboard and lifecycle messages, preserving the temporary string lifetime.
bool CtScenarioConfirmDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  ScString temporary;
  if (message == kWnMsg_Command) {
    if (first == 1)
      return OnButton(window, u32(second)) != 0;
  } else if (message == kWnMsg_KeyDown) {
    if (first == 13) {
      CloseWindow(window, 1);
      return true;
    }
    if (first == 27 || (first == 115 && (second & 4))) {
      CloseWindow(window, 0);
      return true;
    }
  } else if (message == kWnMsg_Init) {
    if (window)
      return OnInit(window) != 0;
  } else if (message == kWnMsg_Shutdown && window)
    return OnShutdown(window) != 0;
  return false;
}

// Retain the scenario owner and then mark the dialog binding active.
u8 CtScenarioConfirmDialog::Bind(CtScenarioLayerImpl *layer) {
  owner = layer;
  layer->AddRef();
  bound = 1;
  return 1;
}

// Clear the binding before releasing and clearing the retained scenario owner.
u8 CtScenarioConfirmDialog::Unbind() {
  if (bound) {
    bound = 0;
    ScRelease(&owner);
  }
  return 1;
}

// Build the confirmation dialog's portrait, text, panels and buttons from scenario and localized resources.
// Modes 2 and default retain the original test of the earlier generic-interface pointer's high byte.
u8 CtScenarioConfirmDialog::OnInit(WnWindow *window) {
  i32 portraitBottom = 0, portraitWidth = 94;
  FtFont *font = CtGetScenarioWindowResources()->GetFont(kWnFontUiSmall);
  ScString title, description;
  u32 portraitMode = 5;
  RsKey portraitKey;
  if (!window)
    return 0;
  window->SetSize(391, 237);
  WnWindow *parent = window->GetMainWindow();
  i32 y = (parent->GetH() - window->GetH()) / 2;
  i32 x = (parent->GetW() - window->GetW()) / 2;
  window->GZWinMoveTo(x, y);
  UiGenericInterface *generic = nullptr;
  bool haveGeneric = window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  u8 pointerByte = u8((reinterpret_cast<uptr>(generic) >> 24) & 0xff);
  if (haveGeneric) {
    ScRect first = {265, 34, 387, 190}, second = {4, 34, 16, 232}, third = {15, 197, 387, 232};
    generic->AddPanel(kCtScenarioPortraitPanelId, 0, 13, &first);
    generic->AddPanel(kCtScenarioLeftPanelId, 0, 5, &second);
    generic->AddPanel(kCtScenarioFooterPanelId, 0, 8, &third);
    generic->Release();
  }
  u8 havePortrait = pointerByte;
  if (fifth == 0)
    havePortrait = CtGetScenarioPetitionInfo(u32(fourth), &portraitKey, &portraitMode, &title, &description);
  else if (fifth == 1)
    havePortrait = CtGetScenarioAdvisorInfo(u32(fourth), sixth, 0, &portraitKey, &portraitMode, &title, &description);
  else if (fifth == 2)
    owner->GetDialogEntry(u32(fourth), &portraitKey, &portraitMode, &title, &description);
  UiAdvisorButtonInterface *portrait = nullptr;
  if (havePortrait &&
      CtScenarioCreatePortrait(kCtScenarioPortraitControlId, i32(portraitMode), portraitKey, &portrait)) {
    WnWindow *view = portrait->AsIGZWin();
    if (window->ChildAdd(view)) {
      u32 white = view->MakeFillColor(255, 255, 255), black = view->MakeFillColor(64, 64, 64);
      portrait->SetOutlineColors(black, white);
      view->GZWinMoveTo((122 - view->GetW()) / 2 + 265, 40);
      portraitBottom = view->GetB();
      portraitWidth = view->GetW();
    } else
      window->GetWindowManager()->DestroyWindow(view);
    portrait->Release();
  }
  window->SetCaption(description);
  CtScenarioTruncateText(&title, 2 * portraitWidth, font);
  WnLabelControl *label = CtGetScenarioWindowResources()->CreateLabel(kCtScenarioTitleLabelId, title);
  if (label) {
    WnWindow *view = label->GetWindow();
    label->SetFont(font);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetAlignment(kWnLabelCenterMiddle);
    if (!window->ChildAdd(view))
      view->Shutdown();
    view->SetArea(265, portraitBottom, 387, 190);
    label->Release();
  }
  WnFlatControl *flat = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
    WnWindow *view = flat->GetWindow();
    view->SetID(kCtScenarioBodyPanelId);
    view->SetFillColor(CtGetScenarioWindowResources()->GetColor(kWnColorUiBackground));
    flat->SetOutlineFlags(32);
    view->SetArea(23, 41, 258, 190);
    if (window->ChildAdd(view)) {
      view->SendToBack();
      view->SetFlag(kWnFlagReorderable, false);
    } else
      window->GetWindowManager()->DestroyWindow(view);
    flat->Release();
  }
  GxBuffer *background;
  {
    CtScenarioImageResource image(kCtScenarioConfirmBackgroundImageId, kRsUiImageGroup, GxVramRequest::SystemMemory,
        GxColorType(GxColorType::Color::RGB16));
    background = image.buffer;
  }
  if (background) {
    ScRect area = *background->GetBufferArea();
    WnBitmapControl *bitmap = nullptr;
    if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
            kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
      WnWindow *view = bitmap->GetWindow();
      area.left += 32;
      area.right += 32;
      area.top += 58;
      area.bottom += 58;
      view->SetFlag(kWnFlagReorderable, false);
      view->SetArea(area);
      bitmap->SetImage(background);
      if (!window->ChildAdd(view))
        window->GetWindowManager()->DestroyWindow(view);
      bitmap->Release();
    }
  }
  WnEditorControl *editor = nullptr;
  if (CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
    WnWindow *view = editor->AsIGZWin();
    view->SetID(kCtScenarioTextEditorId);
    editor->SetFont(font);
    editor->SetTextGutterX(8);
    editor->SetTextGutterY(4);
    view->SetSize(190, 143);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    editor->SetText(text);
    if (window->ChildAdd(view))
      view->GZWinMoveTo(66, 44);
    else
      window->GetWindowManager()->DestroyWindow(view);
    editor->Release();
  }
  if (allowConfirm) {
    ScString empty;
    WnButton *button =
        CtGetScenarioWindowResources()->CreateButton(kCtScenarioConfirmButtonId, empty, nullptr, nullptr, 0);
    if (button) {
      WnWindow *view = button->GetWindow();
      if (fifth == 0 || fifth == 1) {
        CtScenarioText caption(fifth == 0 ? 11 : 12, kRsSupplementalGameTextGroup);
        if (caption.IsValid())
          view->SetCaption(*caption.GetString());
      }
      button->Fit();
      button->SetButtonFlag(kWnButtonAutoFit, false);
      view->SetW(145);
      view->SetFlag(kWnFlagReorderable, false);
      view->GZWinMoveTo((235 - view->GetW()) / 2 + 23, 214 - view->GetH() / 2);
      window->ChildAdd(view);
      button->Release();
    }
  }
  GxBuffer *closeImage;
  {
    CtScenarioImageResource image(
        kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
    closeImage = image.buffer;
  }
  ScString empty;
  WnButton *close = CtGetScenarioWindowResources()->CreateButton(
      kCtScenarioCloseButtonId, empty, closeImage, closeImage->GetBufferArea(), 32);
  if (close) {
    WnWindow *view = close->GetWindow();
    close->SetButtonFlag(kWnButtonShowText, false);
    view->SetFlag(kWnFlagReorderable, false);
    view->GZWinMoveTo(336, 214 - view->GetH() / 2);
    window->ChildAdd(view);
    close->Release();
  }
  windowOpen = 1;
  return 1;
}

u8 CtScenarioConfirmDialog::OnShutdown(WnWindow *) {
  windowOpen = 0;
  return 1;
}

u8 CtScenarioConfirmDialog::CloseWindow(WnWindow *window, i32 result) {
  if (!window)
    return 0;
  windowOpen = 0;
  if (!allowConfirm)
    result = 0;
  CtGetScenarioWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), uptr(result));
  return 1;
}

// Map the confirmation and dismissal buttons to modal results and emit the original click event.
u8 CtScenarioConfirmDialog::OnButton(WnWindow *window, u32 command) {
  ApBoxX *box = CtGetScenarioBoxX();
  i32 result;
  if (command == kCtScenarioConfirmButtonId)
    result = 1;
  else if (command == kCtScenarioCloseButtonId || command == kUiFrameCloseButtonId)
    result = 0;
  else
    return 0;
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
  CloseWindow(window, result);
  return 1;
}

// Copy the popup text and construct the confirmation window with the original styles and callback.
u8 CtScenarioConfirmDialog::CreateConfirmWindow(WnWindow **out, const ScStringView &message, u8 allow) {
  *out = nullptr;
  if (windowOpen)
    return 0;
  allowConfirm = allow;
  text.Copy(message);
  UiGenericInterface *generic = nullptr;
  if (!CtGetScenarioDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericCloseButton, 1);
  generic->GetWindow()->PrivateBuffer(true);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtScenarioConfirmWindowId);
  return 1;
}

// Insert or replace one INI command ID and update the highest signed value seen in this enumeration.
static void CtReadScenarioCommandId(const ScString &name, const ScString &value, void *) {
  i32 id = i32(GeParseNumber(value.ToChar()));
  CtScenarioCommandIds[std::string(name.Data(), name.Strlen())] = id;
  if (id > CtScenarioLastCommand)
    CtScenarioLastCommand = id;
}

// Bind a handler only when its command name was present and its unsigned ID is within the table.
static void CtRegisterScenarioCommand(
    const ScString &name, i32 (CtScenarioLayerImpl::*method)(CtScenarioCommand *, i32)) {
  auto found = CtScenarioCommandIds.find(std::string(name.Data(), name.Strlen()));
  if (found == CtScenarioCommandIds.end())
    return;
  u32 id = u32(found->second);
  if (id <= u32(CtScenarioLastCommand)) {
    assert(id < CtScenarioHandlerStorage.capacity);
    CtScenarioHandlers[id].method = method;
  }
}

// Collect one CR-terminated script line while skipping line-feed bytes.
static u8 CtReadScenarioLine(RsStream *stream, ScString *text) {
  text->Erase(0, text->Strlen());
  u8 value = u8(uptr(text));
  for (;;) {
    if (!stream->ReadBlock(&value, 1))
      return 0;
    if (value == '\n')
      continue;
    if (value == '\r') {
      value = 0;
      text->Append(&value, 1);
      return 1;
    }
    text->Append(&value, 1);
  }
}

// Store the scenario advisor selection and replace an unavailable explicit advisor with the default.
void CtScenarioLayerImpl::SetScenarioAdvisor(i32 id, i32 mode) {
  scenarioAdvisor = id;
  scenarioAdvisorMode = mode;
  if (mode == 1) {
    AdAdvisor *advisor;
    if (!city->GetAdvisor(u32(id), &advisor))
      scenarioAdvisor = kAdUtilitiesAdvisorId;
  }
}

i32 CtScenarioLayerImpl::GetScenarioAdvisor(i32 *id, i32 *group, i32 *mode) {
  *id = scenarioAdvisor;
  *group = textGroup;
  *mode = scenarioAdvisorMode;
  return scenarioAdvisorMode;
}

// Read the scenario's initial script key, progress caption, advisor selection and text group.
u8 CtScenarioLayerImpl::ReadScenarioHeader(RsRecord *record) {
  ScString line;
  RsStream *stream;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)))
    return 0;
  u8 result = 0;
  if (CtReadScenarioLine(stream, &line) && CtReadScenarioLine(stream, &line) && CtReadScenarioLine(stream, &line)) {
    i32 first, second, third;
    i32 count = sscanf(line.ToChar(), "%d %d %d", &first, &second, &third);
    if (count >= 1)
      initialScriptKey.type = u32(first);
    if (count >= 2)
      initialScriptKey.group = u32(second);
    if (count >= 3)
      initialScriptKey.instance = u32(third);
    if (count == 3 && CtReadScenarioLine(stream, &line) && sscanf(line.ToChar(), "%d %d", &first, &second) == 2) {
      SetProgressCaption(first, second);
      if (CtReadScenarioLine(stream, &line) && sscanf(line.ToChar(), "%d %d", &first, &second) == 2) {
        SetScenarioAdvisor(first, second);
        if (CtReadScenarioLine(stream, &line) && sscanf(line.ToChar(), "%d", &scenarioTextGroup) == 1)
          result = 1;
      }
    }
  }
  stream->Release();
  return result;
}

// Load the optional scenario dialog-entry table from its record and retain the first value per key. A fourth conversion
// would consume an absent original sscanf argument; preserve ordinary malformed-line failures.
u8 CtScenarioLayerImpl::ReadDialogEntries(RsDatabaseSegment *segment) {
  RsKey key{kCtScenarioDialogEntriesRecordType, kCtScenarioDialogEntriesRecordGroup, 0};
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  ScString line;
  if (!segment->OpenRecord(key, &record))
    return 1;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    segment->CloseRecord(&record);
    return 0;
  }
  CtReadScenarioLine(stream, &line);
  i32 count;
  if (sscanf(line.ToChar(), "%d", &count) != 1)
    return 0;
  for (i32 index = 0; index < count; ++index) {
    i32 id, title, description, fourth;
    if (!CtReadScenarioLine(stream, &line))
      return 0;
    i32 parsed = sscanf(line.ToChar(), "%d %d %d %d", &id, &title, &description, &fourth);
    assert(parsed != 4);
    if (parsed != 3)
      return 0;
    dialogEntries.try_emplace(u32(id), CtScenarioDialogEntry{u32(id), u32(title), u32(description)});
  }
  stream->Release();
  segment->CloseRecord(&record);
  return 1;
}

// Read the optional ticker table and translate its color components through the main window.
// Missing records are accepted; original early failures retain the open record and stream.
u8 CtScenarioLayerImpl::ReadTickerEntries(RsDatabaseSegment *segment) {
  RsKey key{kCtScenarioTickerEntriesRecordType, kCtScenarioTickerEntriesRecordGroup, 0};
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  ScString line;
  if (!segment->OpenRecord(key, &record))
    return 1;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    segment->CloseRecord(&record);
    return 0;
  }
  WnWindowManager *manager = CtGetScenarioWindowManager();
  if (!manager)
    return 0;
  WnWindow *window = manager->GetMainWindow();
  if (!window)
    return 0;
  CtReadScenarioLine(stream, &line);
  i32 count;
  if (sscanf(line.ToChar(), "%d", &count) != 1)
    return 0;
  for (i32 index = 0; index < count; ++index) {
    if (!CtReadScenarioLine(stream, &line))
      continue;
    ScString name = line.SplitToken("=");
    if (!name.Strlen() || !line.Strlen())
      break;
    u32 id = GeParseNumber(name.ToChar());
    ScString part = line.SplitToken(",");
    u8 style = u8(GeParseNumber(part.ToChar()) - 1);
    part = line.SplitToken(",");
    u8 red = u8(GeParseNumber(part.ToChar()));
    part = line.SplitToken(",");
    u8 green = u8(GeParseNumber(part.ToChar()));
    part = line.SplitToken(",");
    u8 blue = u8(GeParseNumber(part.ToChar()));
    u32 firstColor = window->MakeFillColor(red, green, blue);
    part = line.SplitToken(",");
    red = u8(GeParseNumber(part.ToChar()));
    part = line.SplitToken(",");
    green = u8(GeParseNumber(part.ToChar()));
    part = line.SplitToken(",");
    blue = u8(GeParseNumber(part.ToChar()));
    u32 secondColor = window->MakeFillColor(red, green, blue);
    part = line.SplitToken(",");
    u8 flags = u8(GeParseNumber(part.ToChar()));
    u32 value = GeParseNumber(line.ToChar());
    tickerEntries[id] = {style, flags, firstColor, secondColor, value};
  }
  stream->Release();
  segment->CloseRecord(&record);
  return 1;
}

// Resolve the selected dialog entry's two text resources and publish its image group and instance.
u8 CtScenarioLayerImpl::GetDialogEntry(u32 id, RsKey *image, u32 *mode, ScString *title, ScString *description) {
  auto entry = dialogEntries.find(id);
  assert(entry != dialogEntries.end());
  const CtScenarioDialogEntry &values = entry->second;
  if (CtScenarioResourceManager) {
    CtScenarioResourceManager->RemoveResource({kRsTextResourceType, u32(scenarioTextGroup), values.title});
    CtScenarioResourceManager->RemoveResource({kRsTextResourceType, u32(scenarioTextGroup), values.description});
  }
  CtScenarioText titleResource(values.title, u32(scenarioTextGroup));
  CtScenarioText descriptionResource(values.description, u32(scenarioTextGroup));
  assert(titleResource.text);
  {
    ScString copy(*titleResource.text);
    *title = copy;
  }
  assert(descriptionResource.text);
  {
    ScString copy(*descriptionResource.text);
    *description = copy;
  }
  image->group = kCtScenarioPortraitImageGroup;
  image->instance = values.image;
  *mode = 1;
  return 1;
}

CtScenarioDirector::CtScenarioDirector() {
  AddClass(kCtScenarioLayerClassId, CtCreateScenarioLayer, 0);
  AddClass(kCtScenarioResourceFactoryClassId, CtCreateScenarioResourceFactory, 0);
}

CtScenarioDirector::~CtScenarioDirector() {
}

u32 CtScenarioDirector::GetDirectorID() const {
  return kCtScenarioDirectorId;
}

// Acquire the installed common OnStart temporary and release its live registry identity at scope exit.
class CtScenarioRegistryRef {
public:
  // Query the scenario framework into the escaped output slot, initially empty.
  CtScenarioRegistryRef() : object(nullptr) {
    CtGetScenarioFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  }

  // Release the currently published identity after the director registration callback returns.
  ~CtScenarioRegistryRef() {
    if (object)
      static_cast<ScRegistry *>(object)->Release();
  }

  void *object;
};

bool CtScenarioDirector::OnStart(ScCom *) {
  CtScenarioRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

// Borrow the lazy static scenario director, including its two registrations and process-exit destruction.
ScDirector *CtGetScenarioDirector() {
  static CtScenarioDirector director;
  return &director;
}

// Borrow the framework currently held by the scenario director.
ScFramework *CtGetScenarioFramework() {
  return CtGetScenarioDirector()->FrameWork();
}

// Borrow the resource manager after publishing and releasing the framework's temporary identity.
// A missing service is queried again on the next call, matching the original null-cache condition.
RsManager *CtGetScenarioResourceManager() {
  static RsManager *cached = nullptr;
  if (!cached) {
    RsManager *manager = nullptr;
    CtGetScenarioFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, reinterpret_cast<void **>(&manager));
    cached = manager;
    if (manager)
      manager->Release();
  }
  return cached;
}

// Create the resource-factory product and return its unretained primary factory identity.
ScUnknown *CtCreateScenarioResourceFactory() {
  return static_cast<RsFactoryInterface *>(new CtScenarioResourceFactory);
}

// Preserve the original factory's recursive lock over its complete load or stream operation.
class CtScenarioResourceGuard {
public:
  // Acquire the shared scenario resource region, including nested factory operations.
  CtScenarioResourceGuard() {
    CtScenarioResourceLock.Lock();
  }

  // Release the same region on normal and exceptional exits.
  ~CtScenarioResourceGuard() {
    CtScenarioResourceLock.Unlock();
  }
};

// Hold the scenario layer's original recursive goal-and-rank lock across a mutation.
class CtScenarioGoalGuard {
public:
  // Acquire the layer-owned lock before evaluating and updating its goal collections.
  explicit CtScenarioGoalGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the same lock on both normal and exceptional exits.
  ~CtScenarioGoalGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};
