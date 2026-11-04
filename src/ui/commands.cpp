#include "ui/commands.h"

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"

#include "common/ids.h"

#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/city_scheme.h"
#include "ui/generic_dialogs.h"
#include "ui/local_album.h"
#include "ui/neighbor_window.h"
#include "ui/override_editor.h"
#include "ui/preferences.h"
#include "ui/query_dialogs.h"
#include "ui/replacement_manager.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"

// Speed commands describe their operation/value words; query commands tag their coordinate-pair input.
enum UiCommandParameterId : u32 {
  kUiSpeedOperationParameterId = 0x610d29e1,
  kUiSpeedValueParameterId = 0xc10d2ccc,
  kUiQueryCoordinatesParameterId = 0x616c7df1,
};

UiCommandOwner::UiCommandOwner() : references(0) {
}

UiCommandOwner::~UiCommandOwner() {
}

u32 UiCommandOwner::AddRef() {
  UiGetDirector()->AddRef();
  return ++references;
}

// A zero count stays zero. Otherwise remove the module reference before decrementing the live count.
u32 UiCommandOwner::RemoveRef() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

// Consult the virtual removal result and destroy through the secondary lifetime interface only when it returns zero.
u32 UiCommandOwner::Release() {
  u32 value = RemoveRef();
  if (!value)
    delete this;
  return value;
}

u32 UiCommandOwner::RefCount() {
  return references;
}

UiCommand::UiCommand() {
}

// Drain pending inputs before outputs, transferring each pointer out of its queue before releasing it.
// The descriptor sequences and backing storage retire after the queued argument callbacks finish.
UiCommand::~UiCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool UiCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiCommand::AddRef() {
  return UiCommandOwner::AddRef();
}

u32 UiCommand::Release() {
  return UiCommandOwner::Release();
}

// Retain the supplied input before publishing it at the back of the pending argument sequence.
bool UiCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Leave an empty output untouched; otherwise publish the oldest retained input before removing its queue entry.
bool UiCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 UiCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate input descriptors in insertion order, observing descriptors appended by the callback before testing the end.
bool UiCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    UiCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain the supplied output before publishing it at the back of the result sequence.
bool UiCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

bool UiCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 UiCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate output descriptors in order and pass the original separate type, flags and context arguments.
bool UiCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    UiCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool UiCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool UiCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

UiCommandFactory::UiCommandFactory() {
}

UiCommandFactory::~UiCommandFactory() {
}

// Construct each original command with its distinct publication lifetime; unknown classes clear the output and refuse.
bool UiCommandFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  UiCommand *command;
  switch (classId) {
  case kUiProgressCommandClassId:
    command = new UiProgressCommand;
    break;
  case kUiMessageBoxCommandClassId: {
    UiMessageBoxCommand *message = UiCreateMessageBoxCommand();
    message->AddRef();
    bool result = message->QueryInterface(iid, out);
    message->Release();
    return result;
  }
  case kUiSpeedCommandClassId:
    command = new UiSpeedCommand;
    break;
  case kUiRasterVisibilityCommandClassId:
    command = new UiRasterVisibilityCommand;
    break;
  case kUiQueryCommandClassId:
    command = new UiQueryCommand;
    break;
  case kUiModalCommand12576ClassId:
    command = new UiModalCommand12576;
    break;
  case kUiScenarioProgressCommandClassId:
    command = new UiScenarioProgressCommand;
    break;
  case kUiAdvisorCommandClassId:
    command = new UiAdvisorCommand;
    break;
  case kUiBuildingCommandClassId:
    command = new UiBuildingCommand;
    break;
  case kUiModalCommand124CCClassId:
    command = new UiModalCommand124CC;
    break;
  case kUiTipCommandClassId:
    command = new UiTipCommand;
    break;
  case kUiEditOverridesCommandClassId: {
    UiEditOverridesCommand *editor = new UiEditOverridesCommand;
    editor->AddRef();
    bool result = editor->QueryInterface(iid, out);
    editor->Release();
    return result;
  }
  case kUiReplacementManagerCommandClassId: {
    UiReplacementManagerCommand *manager = new UiReplacementManagerCommand;
    manager->AddRef();
    bool result = manager->QueryInterface(iid, out);
    manager->Release();
    return result;
  }
  case kUiPreferencesCommandClassId:
    command = new UiPreferencesCommand;
    break;
  case kUiReplacementCommandClassId: {
    UiCommand *replacement = new UiReplacementCommand;
    replacement->AddRef();
    bool result = replacement->QueryInterface(iid, out);
    replacement->Release();
    return result;
  }
  case kUiNeighborCommandClassId:
    command = new UiNeighborCommand;
    break;
  case kUiSelectCitySchemeCommandClassId: {
    UiSelectCitySchemeCommand *selection = new UiSelectCitySchemeCommand;
    if (selection->QueryInterface(iid, out))
      return true;
    delete selection;
    return false;
  }
  case kUiBulkSnapshotCommandClassId: {
    UiBulkSnapshotCommand *snapshot = new UiBulkSnapshotCommand;
    if (snapshot->QueryInterface(iid, out))
      return true;
    delete snapshot;
    return false;
  }
  case kUiSnapshotCommandClassId: {
    UiSnapshotCommand *snapshot = new UiSnapshotCommand;
    if (snapshot->QueryInterface(iid, out))
      return true;
    delete snapshot;
    return false;
  }
  default:
    *out = nullptr;
    return false;
  }
  if (command->QueryInterface(iid, out))
    return true;
  delete command;
  return false;
}

// Construct the original command-factory owner with zero references and no child factories.
// Return its primary unknown view without retaining it; the director's catalog performs the requested query.
ScUnknown *UiCreateClass000005A6() {
  return new UiCommandFactory;
}

// Acquire the command service for this request and ask its borrowed factory for the requested interface.
// A null output, unavailable service or factory, failed lookup, or null published result returns false.
bool UiGetCommand(u32 class_id, u32 iid, void **out) {
  if (!out)
    return false;
  ScCommandService *service = nullptr;
  UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandService, reinterpret_cast<void **>(&service));
  bool result = false;
  if (service) {
    ScCommandFactory *factory = service->GetClassFactory();
    if (factory && factory->GetCommand(class_id, iid, out))
      result = *out != nullptr;
    service->Release();
  }
  return result;
}

u16 UiBuildingCommand::history;

UiBuildingCommand::UiBuildingCommand() {
  AddInputParameter(3, 0);
}

// Consume the descriptor's queued argument and publish its raw integer word before releasing it.
void UiBuildingCommand::ReadParameterEffects(i32, u32, void *receiver) {
  UiBuildingCommand *command = static_cast<UiBuildingCommand *>(receiver);
  ScVariant *argument;
  if (command->PopFrontInput(&argument)) {
    command->requested_type = (u32)*argument->AsUint32();
    argument->Release();
  }
}

bool UiBuildingCommand::Execute() {
  EnumInputParameters(ReadParameterEffects, this);
  return Choose(requested_type);
}

// Apply the original category-history cheat, pause the captured city and show the real building chooser as a modal child.
// Transfer any selected attribute into the current city's placement tool before retiring the chooser and resuming the old city.
bool UiBuildingCommand::Choose(u32 category) {
  CtCity *city = UiGetCity();
  ScCheatCodeManager *cheats = UiGetApplication()->GetCheatCodeManager();
  if (cheats->GetCheatCodesEnabled() && category == kUiQueryLandmarks && (history & 0x3f) == 0x1b) {
    category = kUiQueryBuildings;
    UiGetApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(kUiCheat_UnlockBuildingCatalog, true);
  } else {
    history = (u16)(history * 4u);
    switch (category) {
    case kUiQueryRewards:
      history |= 2;
      break;
    case kUiQueryPowerBuildings:
      history |= 1;
      break;
    case kUiQueryWasteBuildings:
      history |= 3;
      break;
    default:
      break;
    }
  }
  bool result = false;
  city->SimHiddenPause();
  WnWindow *window = nullptr;
  UiBuildingPicker *picker = nullptr;
  if (UiGetCom()->GetClassObject(kUiBuildingPickerClassId, IID_UiBuildingPicker, reinterpret_cast<void **>(&picker))) {
    picker->SetCategoryEffects(category);
    if (picker->Init()) {
      window = ScAddRefNotNull(picker->GetWindow());
      WnWindowManager *root_manager = UiGetWindowManager();
      WnWindowManager *dialog_manager = UiGetWindowManager();
      dialog_manager->DoModalWin(root_manager->GetMainWindow(), window);
      if (picker->HasSelection()) {
        GeAttrib *attrib = nullptr;
        picker->GetSelection(&attrib);
        if (attrib) {
          bool special = category == kUiQueryLandmarks || category == kUiQueryRewards;
          UiGetCity()->BuildingLayer()->DoPlaceBuildingTool(attrib, special, false);
          attrib->Release();
        }
      }
      result = true;
    }
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
    picker->Release();
  }
  city->SimHiddenUnpause();
  return result;
}

// Construct the command queues and advertise the same three integer descriptors used by the installed factory.
UiScenarioProgressCommand::UiScenarioProgressCommand() {
  AddInputParameter(3, 0);
  AddInputParameter(3, 0);
  AddInputParameter(3, 0);
}

// Consume the raw first integer and ask the captured city's real scenario layer to create its progress window.
// Pause only after successful creation, run the one-window modal operation, then resume before shutting down the window.
bool UiScenarioProgressCommand::Execute() {
  CtCity *city = UiGetCity();
  if (!city)
    return false;
  ScVariant *argument = nullptr;
  WnWindow *window = nullptr;
  if (!PopFrontInput(&argument))
    return false;
  bool enabled = *argument->AsUint32() != 0;
  argument->Release();
  CtScenarioLayer *scenario = city->ScenarioLayer();
  if (!scenario)
    return false;
  if (scenario->CreateProgressDlgWin(&window, enabled)) {
    city->SimHiddenPause();
    UiGetWindowManager()->DoModalWin(window);
    city->SimHiddenUnpause();
    window->Shutdown();
    window->Release();
  }
  return true;
}

UiModalCommand124CC::UiModalCommand124CC() {
}

// Acquire the original dialog interface and retain its borrowed primary only after initialization succeeds.
// Run the two-window modal operation, destroy and release the primary, then retire the provider; ordinary refusals still return true.
bool UiModalCommand124CC::Execute() {
  UiDialog62FA574B *dialog = nullptr;
  if (UiGetCom()->GetClassObject(kUiBusinessDealsClassId, IID_UiDialog62FA574B, reinterpret_cast<void **>(&dialog))) {
    if (dialog->Init()) {
      WnWindow *window = dialog->GetWindow();
      window->AddRef();
      WnWindowManager *root_manager = UiGetWindowManager();
      WnWindowManager *dialog_manager = UiGetWindowManager();
      dialog_manager->DoModalWin(root_manager->GetMainWindow(), window);
      UiGetWindowManager()->DestroyWindow(window);
      window->Release();
    }
    dialog->Release();
  }
  return true;
}

// The original scoped owner12666/12687 contains only the acquired manager interface and releases it at scope exit.
class UiDialogManagerRef {
public:
  UiDialogManagerRef();
  ~UiDialogManagerRef();
  WnWindowManager *window_manager;
};

UiDialogManagerRef::UiDialogManagerRef() : window_manager(nullptr) {
  UiGetFramework()->GetSystemService(
      kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&window_manager));
}

UiDialogManagerRef::~UiDialogManagerRef() {
  if (window_manager)
    window_manager->Release();
}

UiModalCommand12576::UiModalCommand12576() {
}

// Pause the current city, initialize the actual dialog and use a scoped acquired manager for its two-window modal loop.
// Reacquire the city for unpause after retiring the dialog, preserving changes to the application city during interaction.
bool UiModalCommand12576::Execute() {
  CtCity *city = UiGetCity();
  if (city)
    city->SimHiddenPause();
  UiLayerDialogInterface *dialog = nullptr;
  if (UiGetCom()->GetClassObject(
          kUiLayerDialogClassId, IID_UiLayerDialogInterface, reinterpret_cast<void **>(&dialog))) {
    if (dialog->CreateDialog()) {
      WnWindow *window = dialog->GetAssociatedIGZWin();
      if (window) {
        UiDialogManagerRef service;
        if (service.window_manager) {
          window->AddRef();
          service.window_manager->DoModalWin(service.window_manager->GetMainWindow(), window);
          service.window_manager->DestroyWindow(window);
          window->Release();
        }
      }
    }
    dialog->Release();
  }
  city = UiGetCity();
  if (city)
    city->SimHiddenUnpause();
  return true;
}

UiTipCommand::UiTipCommand() {
}

// Read the known leading TOD preference byte and consult the current city's pending-tip predicate when a city exists.
// Show only when no direct child already owns the tip identifier, then destroy the borrowed primary before provider release.
bool UiTipCommand::Execute() {
  u8 enabled = UiGetPreferences()->TODPrefs()->enabled;
  if (enabled) {
    CtCity *city = UiGetCity();
    if (city)
      enabled = city->IsTipOfTheDayNeeded();
    if (enabled) {
      WnWindow *root = UiGetWindowManager()->GetMainWindow();
      if (!root->GetChildWindowFromID(kUiTipDialogWindowId)) {
        UiTipDialogInterface *dialog = nullptr;
        if (UiGetCom()->GetClassObject(
                kUiTipDialogClassId, IID_UiTipDialogInterface, reinterpret_cast<void **>(&dialog))) {
          if (dialog->Init()) {
            WnWindow *window = dialog->GetWindow();
            WnWindowManager *root_manager = UiGetWindowManager();
            WnWindowManager *dialog_manager = UiGetWindowManager();
            dialog_manager->DoModalWin(root_manager->GetMainWindow(), window);
            UiGetWindowManager()->DestroyWindow(window);
          }
          dialog->Release();
        }
      }
    }
  }
  return true;
}

// Describe the operation and speed inputs separately, with the original integer output descriptor.
// Construction leaves the argument queues empty and does not acquire a city or module reference.
UiSpeedCommand::UiSpeedCommand() {
  AddInputParameter(3, kUiSpeedOperationParameterId);
  AddInputParameter(3, kUiSpeedValueParameterId);
  AddOutputParameter(3, kUiSpeedValueParameterId);
}

// Consume one raw integer operation after finding a city. Operations two through six select speeds zero through four;
// operation seven queues the current speed, while operation one only observes the remaining input count and fails.
bool UiSpeedCommand::Execute() {
  if (!InputQueueSize())
    return false;
  CtCity *city = UiGetCity();
  if (!city)
    return false;
  ScVariant *argument;
  PopFrontInput(&argument);
  i32 operation = (i32)*argument->AsUint32();
  argument->Release();
  if (operation == 1) {
    InputQueueSize();
    return false;
  }
  if (operation >= 2 && operation <= 6)
    return city->SetGameSpeed(operation - 2);
  if (operation != 7)
    return false;
  PushBackOutput(new ScValue((u32)city->GetGameSpeed(), kScValueUint32));
  return true;
}

UiMessageBoxCommand::UiMessageBoxCommand() {
}

// Return the newly constructed command primary at zero references; the calling factory performs its retained query.
UiMessageBoxCommand *UiCreateMessageBoxCommand() {
  return new UiMessageBoxCommand;
}

// Type refusal still retires the popped argument and leaves the caller's existing String unchanged.
bool UiMessageBoxCommand::ReadString(ScString *out) {
  ScVariant *argument = nullptr;
  if (!PopFrontInput(&argument))
    return false;
  bool accepted = argument->GetType() == kScValueCharArray;
  if (accepted) {
    u32 count = argument->GetCount();
    char *bytes = argument->RefRZChar();
    out->FromChar(bytes, count);
  }
  argument->Release();
  return accepted;
}

// Consume one exact scalar integer and preserve the genuine output on empty input or type refusal.
// The popped argument is released before the extraction result is returned.
bool UiMessageBoxCommand::ReadInteger(u32 *out) {
  ScVariant *argument = nullptr;
  if (!PopFrontInput(&argument))
    return false;
  bool accepted = argument->GetType() == kScValueUint32;
  if (accepted)
    *out = *argument->AsUint32();
  argument->Release();
  return accepted;
}

// Require exactly three queued arguments, but attempt all three extractions before rejecting any invalid type.
// A completed message box publishes its result through an independently retained scalar and returns the queue's reply.
bool UiMessageBoxCommand::Execute() {
  if (InputQueueSize() != 3)
    return false;
  ScString body, caption;
  u32 result = UINT32_MAX, style = 0;
  bool body_read = ReadString(&body);
  bool caption_read = ReadString(&caption);
  bool style_read = ReadInteger(&style);
  if (!style_read || !body_read || !caption_read || !Show(&result, body, caption, style))
    return false;
  ScValue *output = new ScValue(result, kScValueUint32);
  output->AddRef();
  bool accepted = PushBackOutput(output);
  output->Release();
  return accepted;
}

// Register the coordinate-pair descriptor followed by two independent integer descriptors.
// The original constructor leaves the capture fields unwritten; execution initializes only the consumed count.
UiQueryCommand::UiQueryCommand() {
  AddInputParameter(0x800e, kUiQueryCoordinatesParameterId);
  AddInputParameter(3, 0);
  AddInputParameter(3, 0);
}

// Consume only the coordinate or zero-flag descriptors, copying both coordinate words before incrementing the live count.
// Neither descriptor inspects the variant type; required queued arguments and initialized point bytes are preconditions.
void UiCaptureQueryInput(i32, u32 flags, void *context) {
  if (flags && flags != kUiQueryCoordinatesParameterId)
    return;
  UiQueryCommand *command = static_cast<UiQueryCommand *>(context);
  ScVariant *argument;
  command->PopFrontInput(&argument);
  if (flags) {
    ScPoint *point = argument->RefPoint();
    i32 x = point->x, y = point->y;
    command->tile.x = x;
    command->tile.y = y;
  }
  ++command->processed_count;
  argument->Release();
}

// Reset the consumed count before enumerating all three live descriptors, then dispatch the captured pair.
// The two trailing tile-query arguments are the original zero DWORDs rather than additional result outputs.
bool UiQueryCommand::Execute() {
  processed_count = 0;
  EnumInputParameters(UiCaptureQueryInput, this);
  return QueryTile(&tile, 0, 0);
}

UiAdvisorCommand::UiAdvisorCommand() {
}

bool UiAdvisorCommand::Execute() {
  return false;
}

// Query the newly allocated callback for its actual dialog identity, then retire that acquired reference after Show.
// Query refusal releases the original allocation; a failed Show still retires the successfully acquired view.
u8 UiMessageBoxCommand::Show(u32 *out, const ScStringView &body, const ScStringView &caption, u32 style) {
  UiMessageBox *owner = UiCreateMessageBox();
  if (!owner)
    return 0;
  UiMessageBox *dialog = nullptr;
  if (!owner->QueryInterface(IID_UiMessageBox, reinterpret_cast<void **>(&dialog))) {
    owner->Release();
    return 0;
  }
  u8 result = dialog->Show(out, body, caption, style);
  dialog->Release();
  return result;
}

UiCommandServiceRef::UiCommandServiceRef() : value(nullptr) {
  UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandService, reinterpret_cast<void **>(&value));
}

UiCommandServiceRef::~UiCommandServiceRef() {
  if (value)
    value->Release();
}

// Query a bounded tile or its retained occupant, prepare the real query window and bracket modal interaction with messages.
// Result two can run the attribute command; the original missing-service exit deliberately precedes city unlock/unpause.
bool UiQueryCommand::QueryTile(const ScPoint *point, u32, u32) {
  CtCity *city = UiGetCity();
  ScOccupantManager *occupants = UiGetCurrentView()->CurrentOccupantManager();
  WnWindowManager *windows = UiGetWindowManager();
  if (!occupants->IsInBoundsCell((u32)point->x, (u32)point->y))
    return false;
  city->SimHiddenPause();
  city->Lock();
  ScOccupant *occupant = nullptr;
  UiQueryWindow *query = nullptr;
  u8 ready;
  i32 modal_result = 1;
  if (!occupants->GetOccupantAtCell((u32)point->x, (u32)point->y, &occupant) || !occupant) {
    if (!UiGetCom()->GetClassObject(kUiQueryWindowClassId, IID_UiQueryWindow, reinterpret_cast<void **>(&query)))
      goto release_query;
    ScString caption;
    ScDirtBag *dirt = city->DirtBag();
    u8 altitude;
    dirt->GetVertexAltitudeDirt((u32)point->x, (u32)point->y, &altitude);
    CtCoord location = {(i32)((u32)point->x << 8), (i32)((u32)point->y << 8), (i32)((u32)altitude << 8)};
    u32 caption_id;
    if (dirt->IsWater((u32)point->x, (u32)point->y)) {
      EcLayer *pollution = city->PollutionLayer();
      caption_id = pollution && pollution->IsSaltWater((u32)point->x, (u32)point->y) ? 0xc3 : 0xc2;
    } else {
      CtZoneLayer *zones = city->ZoneLayer();
      u8 zone;
      if (zones) {
        zones->GetValue((u32)point->x, (u32)point->y, &zone);
        if (zone == 17) {
          EcLayer *pollution = city->PollutionLayer();
          if (pollution && !pollution->IsActiveLandfill((u32)point->x, (u32)point->y)) {
            ScString warning;
            {
              UiTextRef resource(kRsCityServiceDecommissionedText, kRsCityServiceTextGroup);
              warning = resource.GetText();
            }
            query->SetStatusText(warning);
          }
          caption_id = 0x1a;
        } else
          caption_id = 0xc1;
      } else
        caption_id = 0xc1;
    }
    {
      UiTextRef resource(caption_id, kRsCityServiceTextGroup);
      caption = resource.GetText();
    }
    query->SetCaption(caption);
    ready = query->InitAtLocation(&location, 1);
  } else {
    if (!occupant->CreateQueryDialog(&query))
      goto release_query;
    ready = query->InitForOccupant(occupant, 0);
    if (!ready)
      goto release_query;
    CtZoneLayer *zones = city->ZoneLayer();
    if (zones) {
      u8 zone;
      zones->GetValue((u32)point->x, (u32)point->y, &zone);
      if (zone == 17) {
        EcLayer *pollution = city->PollutionLayer();
        if (pollution && !pollution->IsActiveLandfill((u32)point->x, (u32)point->y)) {
          ScString warning;
          {
            UiTextRef resource(kRsCityServiceDecommissionedText, kRsCityServiceTextGroup);
            warning = resource.GetText();
          }
          query->SetStatusText(warning);
        }
      }
    }
  }
  if (ready) {
    if (!query)
      goto release_occupant;
    ApBoxX *notifications = UiGetApplication()->GetBoxX();
    if (notifications) {
      notifications->Event(kAuEventPlayCellInterfaceSound, point->x, point->y, 0, 0);
      notifications->Event(44, 0, 0, 0, 0);
    }
    WnWindow *window = query->GetWindow();
    window->AddRef();
    WnWindow *parent = windows->GetMainWindow();
    modal_result = windows->DoModalWin(parent, window);
    if (notifications)
      notifications->Event(45, 0, 0, 0, 0);
    if (window) {
      windows->DestroyWindow(window);
      window->Release();
    }
  }
release_query:
  ScRelease(&query);
  if (modal_result == 2 && occupant) {
    UiCommandServiceRef service;
    ScCommandFactory *factory = service.value ? service.value->GetClassFactory() : nullptr;
    if (!factory)
      return false;
    UiCommandInterface *command = nullptr;
    if (factory->GetCommand(
            kUiReplacementCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      GeAttrib *attributes = occupant->GetAttribData();
      ScValue *argument = new ScValue(static_cast<ScUnknown *>(attributes));
      argument->AddRef();
      command->PushBackInput(argument);
      argument->Release();
      command->Execute();
      command->Release();
      command = nullptr;
    }
  }
release_occupant:
  ScRelease(&occupant);
  city->Unlock();
  city->SimHiddenUnpause();
  return true;
}
