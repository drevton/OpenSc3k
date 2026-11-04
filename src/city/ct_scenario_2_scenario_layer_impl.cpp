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

// Replace the output with valid goal strings and their achieved flags while holding the goal lock.
void CtScenarioLayerImpl::GetGoalList(std::vector<CtScenarioProgressEntry> *out) {
  ScString temporary;
  CtScenarioGoalGuard guard(&goalLock);
  out->clear();
  for (auto &entry : goals) {
    CtScenarioGoal &goal = entry.second;
    if (goal.IsTextValid()) {
      out->emplace_back();
      goal.GetText(&out->back().text);
      out->back().achieved = goal.achieved;
    }
  }
}

// Replace the output with valid formatted rank strings while holding the goal-and-rank lock.
// Report the displayed index of the selected rank, or minus one if it has no valid text.
void CtScenarioLayerImpl::GetRankList(std::vector<ScString> *out, i32 *selected) {
  CtScenarioGoalGuard guard(&goalLock);
  out->clear();
  *selected = -1;
  for (auto &entry : ranks) {
    CtScenarioRank &rank = entry.second;
    RsKey key(kRsTextResourceType, u32(rank.first), u32(rank.second));
    if (CtScenarioResourceManager)
      CtScenarioResourceManager->RemoveResource(key);
    CtScenarioText resource(u32(rank.second), u32(rank.first));
    if (resource.IsValid()) {
      ScString formatted;
      resource.Format(&formatted, CtScenarioExpandToken, this);
      if (entry.first == u32(selectedRank))
        *selected = i32(out->size());
      out->push_back(formatted);
    }
  }
}

CtScenarioGoal::CtScenarioGoal() : object(nullptr), achieved(0), first(0), second(0) {
}

CtScenarioGoal::~CtScenarioGoal() {
  delete object;
}

// Store the goal's two descriptor words and leave the second in the observed return register.
i32 CtScenarioGoal::Initialize(i32 firstValue, i32 secondValue) {
  first = firstValue;
  second = secondValue;
  return secondValue;
}

// Lazily refresh and create the text object for this goal's stored group and instance.
void CtScenarioGoal::EnsureText() {
  if (!object) {
    RsKey key{kRsTextResourceType, u32(first), u32(second)};
    if (CtScenarioResourceManager)
      CtScenarioResourceManager->RemoveResource(key);
    object = new CtScenarioText(u32(second), u32(first));
  }
}

i32 CtScenarioGoal::IsTextValid() {
  EnsureText();
  return object && object->IsValid();
}

u8 CtScenarioGoal::GetText(ScStringView *out) {
  if (!IsTextValid())
    return 0;
  object->Format(out, CtScenarioExpandToken, CtCurrentScenarioLayer);
  return 1;
}

CtScenarioText::CtScenarioText(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

CtScenarioText::~CtScenarioText() {
  ScRelease(&text);
}

// Copy the resource string when present, or return an independently owned empty string.
ScString CtScenarioText::GetText() {
  if (text)
    return ScString(*text);
  ScString empty;
  return ScString(empty);
}

// Borrow the current resolved string without adding a reference.
ScStringView *CtScenarioText::GetString() {
  return text;
}

// Copy the fixed text-resource type and stored group and instance into the supplied key.
// The observed output pointer is returned; the private nominal return remains unidentified.
RsKey *CtScenarioText::GetResourceKey(RsKey *out) {
  *out = {kRsTextResourceType, key.group, key.instance};
  return out;
}

// Copy the resource text, apply its substitution callback and publish the resulting byte range.
u8 CtScenarioText::Format(ScStringView *out, CtScenarioTokenCallback callback, CtScenarioLayerImpl *layer) {
  if (!text)
    return 0;
  ScString copy(*text);
  Substitute(&copy, callback, layer);
  out->FromChar(copy.Data(), copy.Strlen());
  return 1;
}

// Scan each percent marker, applying built-in substitutions before consulting the scenario callback.
void CtScenarioText::Substitute(ScString *source, CtScenarioTokenCallback callback, CtScenarioLayerImpl *layer) {
  CtScenarioToken token;
  u32 start = 0;
  for (;;) {
    u32 length = source->Strlen();
    if (start >= length)
      break;
    const char *found = static_cast<const char *>(memchr(source->Data() + start, '%', length - start));
    if (!found)
      break;
    u32 position = u32(found - source->Data());
    if (!CtScenarioApplyBuiltinToken(source, &position) && callback(source, position, &token, layer))
      CtScenarioApplyToken(source, &token, &position);
    start = position + 1;
  }
}

bool CtScenarioText::IsValid() {
  return text != nullptr;
}

void CtScenarioText::Resolve() {
  if (!CtGetScenarioResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text)))
    text = nullptr;
}

CtScenarioScript::CtScenarioScript() : commandCount(0), commands(nullptr) {
}

CtScenarioScript::~CtScenarioScript() {
  Clear();
}

// Return the resource or Unknown identity and retain it; unsupported IDs clear the output.
bool CtScenarioScript::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsResource);
  HANDLE_QUERY_INTERFACE(IID_RsResource, RsResource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the resource reference count under the scenario module's own recursive lock.
u32 CtScenarioScript::AddRef() {
  CtScenarioResourceLock.Lock();
  u32 result = ++references;
  CtScenarioResourceLock.Unlock();
  return result;
}

u32 CtScenarioScript::Release() {
  CtScenarioResourceLock.Lock();
  bool last = references-- == 1;
  CtScenarioResourceLock.Unlock();
  if (last) {
    delete this;
    return 0;
  }
  return references;
}

// Free each command's argument array and then the command array without resetting its count.
// The original private success return's Boolean or byte declaration remains unidentified.
u8 CtScenarioScript::Clear() {
  if (commands) {
    for (i32 i = 0; i < commandCount; ++i)
      delete[] commands[i].arguments;
    delete[] commands;
    commands = nullptr;
  }
  return 1;
}

// Read the header and command records from the script stream, retaining its original count and order.
// Reuse the opcode and argument-count scratch cells across partial rows; initially unwritten values are initialized.
u8 CtScenarioScript::Read(RsStream *stream) {
  ScString line;
  CtReadScenarioLine(stream, &line);
  CtReadScenarioLine(stream, &line);
  i32 count = commandCount;
  if (sscanf(line.ToChar(), "%d", &count) == 1)
    commandCount = i16(count);
  if (!commandCount) {
    commands = nullptr;
    return 1;
  }
  assert(commandCount >= 0);
  commands = new CtScenarioCommand[u32(commandCount)]{};
  i32 opcode = 0, argumentCount = 0;
  for (i32 i = 0; i < commandCount; ++i) {
    CtReadScenarioLine(stream, &line);
    i32 first = 0;
    sscanf(line.ToChar(), "%d %d %d", &first, &opcode, &argumentCount);
    CtScenarioCommand *command = &commands[i];
    command->unknown0 = u32(first);
    command->opcode = i16(opcode);
    command->argumentCount = u8(argumentCount);
    command->arguments = command->argumentCount ? new CtScenarioArgument[command->argumentCount]{} : nullptr;
    for (u32 j = 0; j < command->argumentCount; ++j) {
      CtReadScenarioLine(stream, &line);
      CtScenarioArgument *argument = &command->arguments[j];
      sscanf(line.ToChar(), "%d %d", &argumentCount, &argument->value);
      argument->variable = u8(argumentCount);
    }
    CtReadScenarioLine(stream, &line);
  }
  return 1;
}

CtScenarioResourceFactory::CtScenarioResourceFactory() {
}

CtScenarioResourceFactory::~CtScenarioResourceFactory() {
}

bool CtScenarioResourceFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsFactoryInterface, RsFactoryInterface);
  return RsModuleObject::QueryInterface(iid, out);
}

u32 CtScenarioResourceFactory::AddRef() {
  CtGetScenarioDirector()->AddRef();
  return ++references;
}

// At and, release the scenario director before applying the captured final-reference decision.
u32 CtScenarioResourceFactory::Release() {
  bool last = references == 1;
  CtGetScenarioDirector()->RemoveRef();
  if (last) {
    delete this;
    return 0;
  }
  return --references;
}

// Create the resource, copy the record key, read it and publish the requested identity on success.
bool CtScenarioResourceFactory::Load(RsRecord *record, u32 iid, void **out) {
  CtScenarioResourceGuard guard;
  RsResource *resource;
  bool result = false;
  if (New(record->GetKey()->type, IID_RsResource, reinterpret_cast<void **>(&resource))) {
    RsKey *source = record->GetKey(), *destination = resource->GetKey();
    *destination = *source;
    if (ReadRecord(resource, record))
      result = resource->QueryInterface(iid, out);
    resource->Release();
  }
  return result;
}

bool CtScenarioResourceFactory::New(u32, u32 iid, void **out) {
  CtScenarioScript *script = new CtScenarioScript;
  return script->QueryInterface(iid, out);
}

bool CtScenarioResourceFactory::ReadRecord(RsResource *resource, RsRecord *record) {
  CtScenarioResourceGuard guard;
  RsStream *stream;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    result = SerialRead(resource, stream);
    stream->Release();
  }
  return result;
}

bool CtScenarioResourceFactory::WriteRecord(const RsResource *resource, RsRecord *record) {
  CtScenarioResourceGuard guard;
  RsStream *stream;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    result = SerialWrite(const_cast<RsResource *>(resource), stream);
    stream->Release();
  }
  return result;
}

bool CtScenarioResourceFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<CtScenarioScript *>(resource)->Read(stream) != 0;
}

bool CtScenarioResourceFactory::SerialWrite(RsResource *, RsStream *) {
  return false;
}

bool CtScenarioLayerImpl::IsScenarioRunning() {
  return running != 0;
}

// Initialize the scenario layer's service pointers, collections, calendar defaults and recursive goal lock.
CtScenarioLayerImpl::CtScenarioLayerImpl()
    : initialized(0), running(0), unknown10(0), unknown11(0), references(0), scenarioFlag16(0), dispatching(0),
      unknown18(0), unknown19(0), city(nullptr), width(0), height(0), crime(nullptr), buildings(nullptr),
      landValue(nullptr), water(nullptr), pollution(nullptr), power(nullptr), residential(nullptr), budget(nullptr),
      traffic(nullptr), aura(nullptr), surfaceOccupants(nullptr), ordinances(nullptr), dailyManipulator(nullptr),
      otherManipulator(nullptr), elapsedTicks(0), startMonth(0), startYear(0), currentDay(1000), currentMonth(0),
      currentYear(0), nextCommand(nullptr), scenarioAdvisor(kAdUtilitiesAdvisorId), textGroup(3),
      scenarioAdvisorMode(1), scenarioTextGroup(0), selectedRank(-1), resultsText(nullptr), dialogText(nullptr),
      progressDialog(nullptr), confirmDialog(nullptr), choiceDialog(nullptr), finishWindow(nullptr),
      progressCaption(nullptr), random(kScRandomSeedFromClock), scenarioSegment(nullptr), scriptPaused(0), popupFlag(0),
      finishNotificationRegistered(0), scenarioSpeedEnabled(1) {
}

CtScenarioLayerImpl::~CtScenarioLayerImpl() {
  if (initialized)
    Shutdown();
}

bool CtScenarioLayerImpl::CreateProgressDlgWin(WnWindow **out, bool persist) {
  return progressDialog->CreateProgressWindow(out, persist) != 0;
}

// Copy selected scenario records from the retained segment to the destination without compression.
bool CtScenarioLayerImpl::SaveUncompressedScenarioRelatedRecords(RsDatabaseSegment *segment) {
  RsKeyList keys;
  scenarioSegment->GetResourceKeyList(&keys);
  u8 *buffer = nullptr;
  u32 capacity = 0;
  bool result = true;
  u32 count = keys.Size();
  for (u32 index = 0; index < count && result; ++index) {
    RsKey key = *keys.GetKey(index);
    for (u32 filter = 0; filter < CtScenarioResourceFilters.size; ++filter) {
      const CtScenarioResourceFilter &entry = CtScenarioResourceFilters.data[filter];
      if (entry.type != key.type || entry.group != key.group)
        continue;
      RsRecord *sourceRecord = nullptr, *destinationRecord = nullptr;
      RsStream *sourceStream = nullptr, *destinationStream = nullptr;
      result = scenarioSegment->OpenRecord(key, &sourceRecord);
      if (result) {
        result = sourceRecord->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&sourceStream));
        if (result) {
          result = segment->OpenRecordForWrite(key, &destinationRecord);
          if (result) {
            result = destinationRecord->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&destinationStream));
            if (result) {
              u32 length = 0;
              sourceStream->GetSize(&length);
              if (key.type == kRsTextResourceType && scenarioTextGroup)
                length += 4;
              if (length > capacity) {
                delete[] buffer;
                buffer = new u8[length];
                capacity = length;
              }
              result = sourceStream->ReadBlock(buffer, length);
              if (result)
                result = destinationStream->WriteBlock(buffer, length);
              destinationStream->Release();
              segment->CloseRecord(&destinationRecord);
              sourceStream->Release();
              scenarioSegment->CloseRecord(&sourceRecord);
            } else
              segment->CloseRecord(&destinationRecord);
          }
        } else
          scenarioSegment->AbortRecord(&sourceRecord);
      }
      break;
    }
  }
  delete[] buffer;
  return result;
}

// Publish the scenario identity or the separate city-layer identity, and retain the selected object.
bool CtScenarioLayerImpl::QueryInterface(u32 iid, void **out) {
  if (iid == IID_ScUnknown || iid == IID_CtLayer)
    *out = static_cast<CtLayer *>(this);
  else if (iid == IID_CtScenarioLayer)
    *out = static_cast<CtScenarioLayer *>(this);
  else {
    *out = nullptr;
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  return true;
}

u32 CtScenarioLayerImpl::AddRef() {
  return ++references;
}

u32 CtScenarioLayerImpl::Release() {
  return ScReleaseImpl(this, &references);
}

void CtScenarioLayerImpl::GetScenarioFilePath(ScStringView *out) {
  out->Copy(scenarioFilePath);
}

void CtScenarioLayerImpl::GetProgressCaption(ScStringView *out) {
  if (progressCaption && progressCaption->IsValid())
    out->Copy(*progressCaption->GetString());
}

void CtScenarioLayerImpl::GetDialogText(ScStringView *out) {
  if (dialogText && dialogText->IsValid())
    dialogText->Format(out, CtScenarioExpandToken, this);
}

void CtScenarioLayerImpl::GetResultsDialogText(ScStringView *out) {
  if (resultsText && resultsText->IsValid())
    resultsText->Format(out, CtScenarioExpandToken, this);
}

// Replace the progress caption and fall back to the original default text if the requested key fails.
void CtScenarioLayerImpl::SetProgressCaption(i32 group, i32 instance) {
  delete progressCaption;
  RsKey key{kRsTextResourceType, u32(group), u32(instance)};
  if (CtScenarioResourceManager)
    CtScenarioResourceManager->RemoveResource(key);
  progressCaption = new CtScenarioText(u32(instance), u32(group));
  if (!progressCaption->IsValid()) {
    delete progressCaption;
    progressCaption = new CtScenarioText(kRsSupplementalGameScenarioText, kRsSupplementalGameTextGroup);
  }
}

bool CtScenarioLayerImpl::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

// Route the five scenario notifications to their original layer handlers.
// Other messages are accepted without modifying the receiver.
bool CtScenarioLayerImpl::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kCtMsg_ShowScenarioStatus:
  case kCtMsg_ShowScenarioPopup:
  case kCtMsg_ShowScenarioEndDialog:
    HandleSimulationMessage(message);
    break;
  case kCtMsg_SimulatorStarting:
    OnSimulationBoundary();
    break;
  case kApMsgIdle:
    OnFinishDialog();
    break;
  }
  return true;
}

// Finish the pending end-scenario dialog, restore simulation state and route its modal result.
void CtScenarioLayerImpl::OnFinishDialog() {
  ScMessageServer *server = CtGetScenarioMessageServer();
  WnWindowManager *manager = CtGetScenarioWindowManager();
  if (!finishWindow || !server || !manager || manager->IsModal())
    return;
  if (finishNotificationRegistered) {
    server->RemoveNotification(static_cast<CtLayer *>(this), kApMsgIdle);
    finishNotificationRegistered = 0;
  }
  manager = CtGetScenarioWindowManager();
  u32 result = manager->DoModalWin(finishWindow);
  finishWindow->Shutdown();
  finishWindow->Release();
  finishWindow = nullptr;
  city->SimHiddenUnpause();
  ScMessage speed{kCtMsg_SetSimulationControlsEnabled, 1, 0, 0};
  server->MessagePost(speed, false);
  EnableLoadSave();
  if (result == 0)
    CtScenarioOpenCity(&scenarioFilePath);
  else if (result == 1)
    CtScenarioOpenCity(nullptr);
  else if (result == 2)
    CtScenarioMainMenu();
}

// Serialize recursive scenario notifications through the original FIFO before reenabling load/save.
// The queued end-state branch reads the initial message's data word, as in the installed receiver.
void CtScenarioLayerImpl::HandleSimulationMessage(ScMessage *message) {
  if (dispatching) {
    *pendingMessages.Append() = *message;
    return;
  }
  dispatching = 1;
  ScMessage current = *message;
  for (;;) {
    if (current.type == kCtMsg_ShowScenarioStatus)
      HandleDialog(false);
    else if (current.type == kCtMsg_ShowScenarioPopup)
      HandlePopup(&current);
    else if (current.type == kCtMsg_ShowScenarioEndDialog)
      HandleDialog(message->data1 != 0);
    if (!pendingMessages.size)
      break;
    current = pendingMessages.data[0];
    --pendingMessages.size;
    if (pendingMessages.size)
      memmove(pendingMessages.data, pendingMessages.data + 1, pendingMessages.size * sizeof(ScMessage));
  }
  dispatching = 0;
  EnableLoadSave();
}

// Invoke the original scenario-dialog command with the supplied end-scenario flag.
void CtScenarioLayerImpl::HandleDialog(bool endScenario) {
  if (!running)
    return;
  UiCommandInterface *command = nullptr;
  if (!CtGetScenarioCommandFactory()->GetCommand(
          kUiScenarioProgressCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)))
    return;
  DisableLoadSave();
  ScValue *argument = new ScValue(u32(endScenario), kScValueUint32);
  command->PushBackInput(argument);
  command->Execute();
}

// Show a running scenario's popup, route its confirmation and then release the posted payload.
void CtScenarioLayerImpl::HandlePopup(ScMessage *message) {
  if (!running)
    return;
  CtScenarioPopupMessage *payload = reinterpret_cast<CtScenarioPopupMessage *>(message->data1);
  RsKey key{kRsTextResourceType, u32(payload->first), u32(payload->second)};
  if (CtScenarioResourceManager)
    CtScenarioResourceManager->RemoveResource(key);
  CtScenarioText resource(u32(payload->second), u32(payload->first));
  ScString text;
  if (resource.IsValid()) {
    city->SimHiddenPause();
    resource.Format(&text, CtScenarioExpandToken, this);
    confirmDialog->fourth = payload->fourth;
    confirmDialog->sixth = payload->sixth;
    confirmDialog->fifth = payload->fifth;
    WnWindow *window = nullptr;
    if (confirmDialog->CreateConfirmWindow(&window, text, payload->third) && window) {
      u32 result = CtGetScenarioWindowManager()->DoModalWin(window);
      window->Shutdown();
      window->Release();
      if (result == 1)
        HandleDialog(false);
    }
    city->SimHiddenUnpause();
  }
  scriptPaused = 0;
  delete payload;
}

void CtScenarioLayerImpl::DisableLoadSave() {
  if (city)
    city->SetPlayerLoadSaveEnabled(false);
}

void CtScenarioLayerImpl::EnableLoadSave() {
  if (city)
    city->SetPlayerLoadSaveEnabled(true);
}

// Publish the raw running byte before setting or clearing the city's scenario mode.
void CtScenarioLayerImpl::SetRunning(u8 value) {
  running = value;
  if (city) {
    if (value)
      city->SetCityModeFlag(kCtModeScenario);
    else
      city->ClearCityModeFlag(kCtModeScenario);
  }
}

// Run the scenario boundary tick with the popup flag suppressed and post the next state message.
void CtScenarioLayerImpl::OnSimulationBoundary() {
  if (running) {
    popupFlag = 0;
    TickScenario(0);
    bool active = running != 0;
    popupFlag = 1;
    if (active) {
      ScMessage message{kCtMsg_ShowScenarioEndDialog, scenarioFlag16 == 0, 0, 0};
      CtGetScenarioMessageServer()->MessagePost(message, false);
    }
  }
}

u8 CtScenarioLayerImpl::LoadScript(const RsKey &key, i32 initialIndex) {
  CtScenarioResourceManager->RemoveResource(key);
  CtScenarioScript *script;
  if (!CtScenarioResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&script)))
    return 0;
  CtScenarioCursor cursor{u32(initialIndex), script};
  PushCursor(cursor);
  script->Release();
  return 1;
}

// Run each script in the captured scheduled range in its existing order.
void CtScenarioLayerImpl::RunScheduledScripts(const ScArray<CtScenarioScript *> &scripts) {
  if (!scripts.size)
    return;
  CtScenarioScript **entry = scripts.data, **end = scripts.data + scripts.size;
  while (entry != end)
    RunScript(*entry++);
}

// Send the captured notification words and erase the live queue only when a message server exists.
void CtScenarioLayerImpl::FlushNotifications() {
  u32 *entry = pendingNotifications.data;
  u32 count = pendingNotifications.size;
  ScMessageServer *server = CtGetScenarioMessageServer();
  if (server) {
    for (u32 i = 0; i < count; ++i) {
      ScMessage message{kApMsgDismissNotification, entry[i], 0, 0};
      server->MessageSend(message);
    }
    pendingNotifications.Clear();
  }
}

// Clear the running mode, notify listeners and release cursors and all four scheduled script ranges.
void CtScenarioLayerImpl::StopScenario() {
  SetRunning(0);
  FlushNotifications();
  if (cursors.size) {
    CtScenarioCursor *entry = cursors.data, *end = cursors.data + cursors.size;
    while (entry != end) {
      entry->script->Release();
      entry->script = nullptr;
      ++entry;
    }
  }
  cursors.Clear();
  for (u32 period = 0; period < 4; ++period) {
    ScArray<CtScenarioScript *> &scripts = scheduledScripts[period];
    if (scripts.size) {
      CtScenarioScript **entry = scripts.data, **end = scripts.data + scripts.size;
      while (entry != end)
        (*entry++)->Release();
    }
    scripts.Clear();
  }
  nextCommand = nullptr;
}

// Start the initial script for a fresh scenario or resume the restored cursor, then publish the running mode.
u8 CtScenarioLayerImpl::StartScenario(u8 restored) {
  if (!restored) {
    if (!LoadScript(initialScriptKey, -1))
      return 0;
    u32 day;
    city->GetDate(&currentMonth, &day, &currentYear);
    startMonth = currentMonth;
    startYear = currentYear;
  }
  nextCommand = NextCommand();
  SetRunning(1);
  city->SetTipOfTheDayNeeded(false);
  return 1;
}

// Update the date and elapsed clock, run scheduled scripts and advance due scenario commands.
void CtScenarioLayerImpl::TickScenario(i32 mode) {
  if (!initialized || !running)
    return;
  u32 oldDay = currentDay, oldMonth = currentMonth, oldYear = currentYear;
  city->GetDate(&currentMonth, &currentDay, &currentYear);
  if (mode == 1)
    ++elapsedTicks;
  else if (mode == 2 && oldDay != currentDay)
    elapsedTicks = 1440u * ((elapsedTicks + 1440u) / 1440u);
  if (currentDay != oldDay) {
    RunScheduledScripts(scheduledScripts[0]);
    if (currentDay % 7u == 0)
      RunScheduledScripts(scheduledScripts[1]);
    if (currentMonth != oldMonth)
      RunScheduledScripts(scheduledScripts[2]);
    if (currentYear != oldYear)
      RunScheduledScripts(scheduledScripts[3]);
  }
  if (nextCommand) {
    while (running) {
      CtScenarioCommand *command = nextCommand;
      if (command->unknown0 > elapsedTicks)
        break;
      Dispatch(command, 1);
      nextCommand = NextCommand();
      if (!nextCommand)
        break;
    }
  }
  if (!running)
    StopScenario();
}

// Read command IDs from the installed loose or packaged INI and register the original handlers.
bool CtScenarioLayerImpl::StaticInit(CtDefinition *) {
  GeIni ini;
  ScString path;
  CtScenarioLastCommand = -1;
  CtGetScenarioApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3ScenarioLayer.INI");
  ini.SetPath(path);
  CtGetScenarioApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  ScString section("CommandNameToIdMapping");
  ini.EnumEntries(section, CtReadScenarioCommandId, nullptr);
  CtScenarioResourceManager = CtGetScenarioResourceManager();
  u32 count = u32(CtScenarioLastCommand) + 1u;
  CtScenarioHandlerStorage.Reserve(count);
  CtScenarioHandlers = CtScenarioHandlerStorage.data;
  for (u32 i = 0; i < count; ++i)
    CtScenarioHandlers[i].method = nullptr;
  CtRegisterScenarioCommand(ScString("cmdAdd"), &CtScenarioLayerImpl::Add);
  CtRegisterScenarioCommand(ScString("cmdAddGoal"), &CtScenarioLayerImpl::AddGoal);
  CtRegisterScenarioCommand(ScString("cmdAllGoalsMet"), &CtScenarioLayerImpl::AllGoalsMet);
  CtRegisterScenarioCommand(ScString("cmdAddRankInfo"), &CtScenarioLayerImpl::AddRankInfo);
  CtRegisterScenarioCommand(ScString("cmdAndCondition"), &CtScenarioLayerImpl::AndCondition);
  CtRegisterScenarioCommand(ScString("cmdAssign"), &CtScenarioLayerImpl::Assign);
  CtRegisterScenarioCommand(ScString("cmdBeginBlock"), &CtScenarioLayerImpl::BeginBlock);
  CtRegisterScenarioCommand(ScString("cmdCountUnpoweredBuildings"), &CtScenarioLayerImpl::CountUnpoweredBuildings);
  CtRegisterScenarioCommand(ScString("cmdCountUnwateredBuildings"), &CtScenarioLayerImpl::CountUnwateredBuildings);
  CtRegisterScenarioCommand(ScString("cmdComputePercentage"), &CtScenarioLayerImpl::ComputePercentage);
  CtRegisterScenarioCommand(ScString("cmdCountStructures"), &CtScenarioLayerImpl::CountStructures);
  CtRegisterScenarioCommand(
      ScString("cmdCountStructuresNearLocation"), &CtScenarioLayerImpl::CountStructuresNearLocation);
  CtRegisterScenarioCommand(
      ScString("cmdCountStructuresWithAppearanceInCity"), &CtScenarioLayerImpl::CountStructuresWithAppearanceInCity);
  CtRegisterScenarioCommand(ScString("cmdCountStructuresWithAppearanceNearLocation"),
      &CtScenarioLayerImpl::CountStructuresWithAppearanceNearLocation);
  CtRegisterScenarioCommand(ScString("cmdDecrement"), &CtScenarioLayerImpl::Decrement);
  CtRegisterScenarioCommand(ScString("cmdDisableNewNeighborDeals"), &CtScenarioLayerImpl::DisableNewNeighborDeals);
  CtRegisterScenarioCommand(ScString("cmdDivide"), &CtScenarioLayerImpl::Divide);
  CtRegisterScenarioCommand(ScString("cmdEnableNewNeighborDeals"), &CtScenarioLayerImpl::EnableNewNeighborDeals);
  CtRegisterScenarioCommand(ScString("cmdEnableDisableBusinessDeal"), &CtScenarioLayerImpl::EnableDisableBusinessDeal);
  CtRegisterScenarioCommand(ScString("cmdEndScenario"), &CtScenarioLayerImpl::EndScenario);
  CtRegisterScenarioCommand(ScString("cmdEqual"), &CtScenarioLayerImpl::Equal);
  CtRegisterScenarioCommand(ScString("cmdGenerateRandomNum"), &CtScenarioLayerImpl::GenerateRandomNum);
  CtRegisterScenarioCommand(ScString("cmdGetAura"), &CtScenarioLayerImpl::GetAura);
  CtRegisterScenarioCommand(ScString("cmdGetCrimeValue"), &CtScenarioLayerImpl::GetCrimeValue);
  CtRegisterScenarioCommand(ScString("cmdGetCurrentDate"), &CtScenarioLayerImpl::GetCurrentDate);
  CtRegisterScenarioCommand(ScString("cmdGetFundsAvailable"), &CtScenarioLayerImpl::GetFundsAvailable);
  CtRegisterScenarioCommand(ScString("cmdGetPollutionValue"), &CtScenarioLayerImpl::GetPollutionValue);
  CtRegisterScenarioCommand(ScString("cmdGetLandValue"), &CtScenarioLayerImpl::GetLandValue);
  CtRegisterScenarioCommand(ScString("cmdGetPopulation"), &CtScenarioLayerImpl::GetPopulation);
  CtRegisterScenarioCommand(ScString("cmdGetSurplusPower"), &CtScenarioLayerImpl::GetSurplusPower);
  CtRegisterScenarioCommand(ScString("cmdGetSurplusWater"), &CtScenarioLayerImpl::GetSurplusWater);
  CtRegisterScenarioCommand(ScString("cmdGetTotalDebt"), &CtScenarioLayerImpl::GetTotalDebt);
  CtRegisterScenarioCommand(ScString("cmdGetTrafficDensity"), &CtScenarioLayerImpl::GetTrafficDensity);
  CtRegisterScenarioCommand(ScString("cmdGetRCItaxes"), &CtScenarioLayerImpl::GetRciTaxes);
  CtRegisterScenarioCommand(ScString("cmdGoalStatusIs"), &CtScenarioLayerImpl::GoalStatusIs);
  CtRegisterScenarioCommand(ScString("cmdGreaterThan"), &CtScenarioLayerImpl::GreaterThan);
  CtRegisterScenarioCommand(ScString("cmdIf"), &CtScenarioLayerImpl::If);
  CtRegisterScenarioCommand(ScString("cmdIfElse"), &CtScenarioLayerImpl::IfElse);
  CtRegisterScenarioCommand(ScString("cmdIncrement"), &CtScenarioLayerImpl::Increment);
  CtRegisterScenarioCommand(ScString("cmdIsOrdinanceEnacted"), &CtScenarioLayerImpl::IsOrdinance);
  CtRegisterScenarioCommand(ScString("cmdIsBusinessDealActive"), &CtScenarioLayerImpl::IsBusinessDealActive);
  CtRegisterScenarioCommand(ScString("cmdIsNeighborConnection"), &CtScenarioLayerImpl::IsNeighborConnection);
  CtRegisterScenarioCommand(ScString("cmdIsNeighborDealPresent"), &CtScenarioLayerImpl::IsNeighborDealPresent);
  CtRegisterScenarioCommand(ScString("cmdLessThan"), &CtScenarioLayerImpl::LessThan);
  CtRegisterScenarioCommand(ScString("cmdMakeDialogText"), &CtScenarioLayerImpl::MakeDialogText);
  CtRegisterScenarioCommand(ScString("cmdMarkGoalStatus"), &CtScenarioLayerImpl::MarkGoalStatus);
  CtRegisterScenarioCommand(ScString("cmdMoveCamera"), &CtScenarioLayerImpl::MoveCamera);
  CtRegisterScenarioCommand(ScString("cmdMultiply"), &CtScenarioLayerImpl::Multiply);
  CtRegisterScenarioCommand(ScString("cmdNotEqual"), &CtScenarioLayerImpl::NotEqual);
  CtRegisterScenarioCommand(ScString("cmdOfferRewardBuilding"), &CtScenarioLayerImpl::OfferRewardBuilding);
  CtRegisterScenarioCommand(ScString("cmdOfferBusinessDeal"), &CtScenarioLayerImpl::OfferBusinessDeal);
  CtRegisterScenarioCommand(ScString("cmdOrCondition"), &CtScenarioLayerImpl::OrCondition);
  CtRegisterScenarioCommand(ScString("cmdPlaySound"), &CtScenarioLayerImpl::PlaySound);
  CtRegisterScenarioCommand(ScString("cmdPopUpMsg"), &CtScenarioLayerImpl::PopUpMsg);
  CtRegisterScenarioCommand(ScString("cmdRepeatRegular"), &CtScenarioLayerImpl::RepeatRegular);
  CtRegisterScenarioCommand(ScString("cmdEnableDisableDisasters"), &CtScenarioLayerImpl::SetDisastersEnabled);
  CtRegisterScenarioCommand(ScString("cmdSetResultsDlgText"), &CtScenarioLayerImpl::SetResultsDialogText);
  CtRegisterScenarioCommand(ScString("cmdSetScenarioSpeed"), &CtScenarioLayerImpl::SetScenarioSpeed);
  CtRegisterScenarioCommand(ScString("cmdSetVariable"), &CtScenarioLayerImpl::SetVariable);
  CtRegisterScenarioCommand(ScString("cmdShowNewsticker"), &CtScenarioLayerImpl::ShowNewsticker);
  CtRegisterScenarioCommand(ScString("cmdStartEarthquake"), &CtScenarioLayerImpl::StartEarthquake);
  CtRegisterScenarioCommand(ScString("cmdStartFire"), &CtScenarioLayerImpl::StartFire);
  CtRegisterScenarioCommand(ScString("cmdStartLocusts"), &CtScenarioLayerImpl::StartLocusts);
  CtRegisterScenarioCommand(ScString("cmdStartRiot"), &CtScenarioLayerImpl::StartRiot);
  CtRegisterScenarioCommand(ScString("cmdStartSpaceJunk"), &CtScenarioLayerImpl::StartSpaceJunk);
  CtRegisterScenarioCommand(ScString("cmdStartTornado"), &CtScenarioLayerImpl::StartTornado);
  CtRegisterScenarioCommand(ScString("cmdStartUFOAttack"), &CtScenarioLayerImpl::StartUfoAttack);
  CtRegisterScenarioCommand(ScString("cmdStartToxicCloud"), &CtScenarioLayerImpl::StartToxicCloud);
  CtRegisterScenarioCommand(ScString("cmdStartWhirlpool"), &CtScenarioLayerImpl::StartWhirlpool);
  CtRegisterScenarioCommand(ScString("cmdSubRoutine"), &CtScenarioLayerImpl::Subroutine);
  CtRegisterScenarioCommand(ScString("cmdSubtract"), &CtScenarioLayerImpl::Subtract);
  CtRegisterScenarioCommand(ScString("cmdGetMonthsElapsed"), &CtScenarioLayerImpl::GetMonthsElapsed);
  *CtScenarioResourceFilters.Append() = {kCtScenarioHeaderRecordType, kCtScenarioHeaderRecordGroup};
  *CtScenarioResourceFilters.Append() = {kCtScenarioTickerEntriesRecordType, kCtScenarioTickerEntriesRecordGroup};
  *CtScenarioResourceFilters.Append() = {kCtScenarioDialogEntriesRecordType, kCtScenarioDialogEntriesRecordGroup};
  *CtScenarioResourceFilters.Append() = {kRsImageResourceType, kCtScenarioPortraitImageGroup};
  *CtScenarioResourceFilters.Append() = {kCtScenarioScriptResourceType, kCtScenarioScriptResourceGroup};
  *CtScenarioResourceFilters.Append() = {kRsCitySummaryResourceType, kRsCitySummaryGroup};
  *CtScenarioResourceFilters.Append() = {kRsTextResourceType, kRsCitySummaryGroup};
  *CtScenarioResourceFilters.Append() = {kRsImageResourceType, kRsCitySummaryGroup};
  return true;
}

bool CtScenarioLayerImpl::StaticShutdown() {
  CtScenarioHandlerStorage.Clear();
  return true;
}

// Retain the city services and scenario callbacks, then subscribe the layer to its four notifications.
bool CtScenarioLayerImpl::Init(CtCity *value) {
  ScMessageServer *server = CtGetScenarioMessageServer();
  if (initialized)
    return true;
  initialized = 1;
  CtCurrentScenarioLayer = this;
  memset(variables, 0, sizeof(variables));
  city = ScAddRefNotNull(value);
  width = city->CellCountX() - 1;
  height = city->CellCountZ() - 1;
  surfaceOccupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  buildings = ScAddRefNotNull(city->BuildingLayer());
  crime = ScAddRefNotNull(city->CrimeLayer());
  landValue = ScAddRefNotNull(city->LandValueLayer());
  water = ScAddRefNotNull(city->PlumbingLayer());
  power = ScAddRefNotNull(city->PowerLayer());
  residential = ScAddRefNotNull(city->ResidentialLayer());
  budget = ScAddRefNotNull(city->BudgetLayer());
  traffic = ScAddRefNotNull(city->TrafficLayer());
  pollution = ScAddRefNotNull(city->PollutionLayer());
  ordinances = ScAddRefNotNull(city->OrdinanceLayer());
  CtLayer *auraLayer = city->GetSpecificLayer(kCtAuraLayerType);
  if (auraLayer)
    auraLayer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
  dailyManipulator = ScAddRefNotNull(new CtScenarioManipulator(this, 2));
  otherManipulator = ScAddRefNotNull(new CtScenarioManipulator(this, 1));
  progressDialog = ScAddRefNotNull(new CtScenarioProgress());
  progressDialog->Bind(this);
  confirmDialog = ScAddRefNotNull(new CtScenarioConfirmDialog());
  confirmDialog->Bind(this);
  choiceDialog = ScAddRefNotNull(new CtScenarioChoiceDialog());
  choiceDialog->Bind(this);
  server->AddNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioEndDialog);
  server->AddNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioPopup);
  server->AddNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioStatus);
  server->AddNotification(static_cast<CtLayer *>(this), kCtMsg_SimulatorStarting);
  return true;
}

bool CtScenarioLayerImpl::Init(CtCity *, ScImporter *) {
  return true;
}

// Open the scenario header and saved-state record, load its auxiliary tables and start or resume its script.
bool CtScenarioLayerImpl::Init(CtCity *owner, RsDatabaseSegment *segment) {
  RsKey headerKey{kCtScenarioHeaderRecordType, kCtScenarioHeaderRecordGroup, 0};
  RsRecord *record = nullptr;
  if (!segment->OpenRecord(headerKey, &record))
    return true;
  if (running)
    StopScenario();
  scenarioSegment = ScAddRefNotNull(segment);
  if (!CtScenarioResourceManager->RegisterDBSegment(scenarioSegment))
    return false;
  RsFileSegment *file = nullptr;
  if (!segment->QueryInterface(IID_RsFileSegment, reinterpret_cast<void **>(&file)) || !file)
    return false;
  file->GetPath(&scenarioFilePath);
  file->Release();
  ScPath path(scenarioFilePath);
  ScString lowered(path.Extension());
  lowered.MakeLower();
  ScString extension(".snr");
  if (!path.Extension().IsEqual(&extension)) {
    running = 0;
    return true;
  }
  ReadTickerEntries(scenarioSegment);
  ReadDialogEntries(scenarioSegment);
  u8 read = ReadScenarioHeader(record);
  segment->CloseRecord(&record);
  if (!read)
    return false;
  ScArchiveFrame frame(owner, segment, {kRsCityLayerRecordType, kCtScenarioLayerType, 0});
  RsStream *stream = frame.GetStream();
  if (frame.valid && stream) {
    if (!ReadSavedState(stream))
      return false;
    scenarioFlag16 = 1;
    return StartScenario(1) != 0;
  }
  return StartScenario(0) != 0;
}

// Restore scenario timing, variables, scheduled scripts, goals, ranks and text keys from the archive stream.
// Failed reads retain their reached output writes and later goal/rank sections still run in the original order.
u8 CtScenarioLayerImpl::ReadSavedState(RsStream *stream) {
  RsKey key;
  // Stock may inspect a count that a failed first read never initialized; that undefined input has no preserved value.
  // Once a read writes a count, retain it across later failed or partial reads exactly as the installed stack slot does.
  u32 count = 0, index = 0;
  u8 ok = stream->ReadU32(&elapsedTicks);
  ok = ok && stream->ReadU32(&startMonth) && stream->ReadU32(&startYear);
  for (u32 i = 0; i < 512 && ok; ++i)
    ok = stream->ReadI32(&variables[i]);
  u32 period = 0;
  while (period < 4 && ok) {
    ok = stream->ReadU32(&count);
    for (u32 i = 0; i < count; ++i) {
      ok = stream->ReadKey(&key);
      AddPeriodicScript(period, key);
    }
    ++period;
  }
  if (period == 4 && ok && stream->ReadU32(&count)) {
    i32 cursorIndex = 0;
    bool readCursor = true;
    if (count) {
      readCursor = stream->ReadI32(&cursorIndex) && stream->ReadKey(&key);
      if (readCursor)
        LoadScript(key, cursorIndex);
    }
    if (readCursor && stream->ReadU32(reinterpret_cast<u32 *>(&scenarioAdvisor)) &&
        stream->ReadU32(reinterpret_cast<u32 *>(&textGroup)))
      stream->ReadU32(reinterpret_cast<u32 *>(&scenarioAdvisorMode));
  }
  ok = stream->ReadU32(&count);
  index = 0;
  while (index < count && ok) {
    CtScenarioGoal goal;
    u32 id = 0, first = 0, second = 0;
    u8 achieved = 0;
    ok = stream->ReadU32(&id);
    if (ok) {
      ok = stream->ReadU8(&achieved);
      goal.achieved = achieved != 0;
      if (ok) {
        ok = stream->ReadU32(&first);
        if (ok) {
          ok = stream->ReadU32(&second);
          if (ok) {
            goal.Initialize(i32(first), i32(second));
            CtScenarioGoal &stored = goals[id];
            stored.object = goal.object;
            stored.achieved = goal.achieved;
            stored.first = goal.first;
            stored.second = goal.second;
          }
        }
      }
    }
    ++index;
  }
  ok = stream->ReadU32(&count);
  if (ok)
    ok = stream->ReadI32(&selectedRank);
  // First-use unwritten rank words are undefined in stock. Later iterations retain prior values on partial failures.
  u32 rankId = index, rankIndex = 0, first = 0, second = 0;
  while (rankIndex < count && ok) {
    ok = stream->ReadU32(&rankId);
    if (ok) {
      ok = stream->ReadU32(&first);
      if (ok)
        ok = stream->ReadU32(&second);
    }
    CtScenarioRank &rank = ranks[rankId];
    rank.first = i32(first);
    rank.second = i32(second);
    ++rankIndex;
  }
  if (rankIndex >= count && ok) {
    ok = stream->ReadKey(&key);
    if (ok) {
      SetResultsText(i32(key.group), i32(key.instance));
      ok = stream->ReadKey(&key);
      if (ok) {
        SetDialogText(i32(key.group), i32(key.instance));
        u8 speed = 0;
        ok = stream->ReadU8(&speed);
        if (ok)
          scenarioSpeedEnabled = speed != 0;
      }
    }
  }
  return ok;
}

// Save the active scenario's script cursors, variables, goals, ranks and text keys to its city archive record.
bool CtScenarioLayerImpl::Save(CtCity *owner, RsDatabaseSegment *segment) {
  if (!IsScenarioRunning())
    return true;
  ScArchiveFrame frame(owner, segment, {kRsCityLayerRecordType, kCtScenarioLayerType, 0}, 1, 0);
  u8 valid = frame.valid;
  RsStream *stream = frame.GetStream();
  if (!valid || !stream)
    return false;
  u8 result = stream->WriteU32(elapsedTicks);
  result = result && stream->WriteU32(startMonth) && stream->WriteU32(startYear);
  for (u32 index = 0; index < 512 && result; ++index)
    result = stream->WriteI32(variables[index]);
  for (u32 period = 0; period < 4 && result; ++period) {
    ScArray<CtScenarioScript *> &scripts = scheduledScripts[period];
    result = stream->WriteU32(scripts.size);
    for (u32 index = 0; index < scripts.size && result; ++index)
      result = stream->WriteKey(scripts.data[index]->GetKey());
  }
  if (result) {
    result = stream->WriteU32(cursors.size);
    if (result && cursors.size) {
      CtScenarioCursor &cursor = cursors.data[0];
      result = stream->WriteI32(i32(cursor.index - 1));
      if (result)
        result = stream->WriteKey(cursor.script->GetKey());
    }
    result = result && stream->WriteU32(u32(scenarioAdvisor)) && stream->WriteU32(u32(textGroup));
    if (result)
      stream->WriteU32(u32(scenarioAdvisorMode));
  }
  result = stream->WriteU32(u32(goals.size()));
  for (auto &entry : goals) {
    if (!result)
      break;
    result = stream->WriteU32(entry.first);
    result = result && stream->WriteU8(entry.second.achieved != 0) && stream->WriteU32(u32(entry.second.first)) &&
        stream->WriteU32(u32(entry.second.second));
  }
  result = stream->WriteU32(u32(ranks.size()));
  if (result)
    result = stream->WriteU32(u32(selectedRank));
  for (auto &entry : ranks) {
    if (!result)
      break;
    result = stream->WriteU32(entry.first);
    result = result && stream->WriteU32(u32(entry.second.first)) && stream->WriteU32(u32(entry.second.second));
  }
  RsKey key;
  if (resultsText)
    resultsText->GetResourceKey(&key);
  if (result)
    result = stream->WriteKey(&key);
  key = RsKey();
  if (dialogText)
    dialogText->GetResourceKey(&key);
  result = result && stream->WriteKey(&key) && stream->WriteU8(scenarioSpeedEnabled != 0);
  return result != 0;
}

bool CtScenarioLayerImpl::SimulationBegin() {
  city->GetDate(&currentMonth, &currentDay, &currentYear);
  SetRunning(running);
  if (running) {
    ScMessage speed{kCtMsg_SetSimulationControlsEnabled, scenarioSpeedEnabled != 0, 0, 0};
    CtGetScenarioMessageServer()->MessagePost(speed, false);
  }
  return true;
}

bool CtScenarioLayerImpl::SimulationEnd() {
  return true;
}

// Unsubscribe and stop the scenario, then release its dialogs, city services and resource segment in original order.
bool CtScenarioLayerImpl::Shutdown() {
  ScMessageServer *server = CtGetScenarioMessageServer();
  if (!initialized)
    return true;
  initialized = 0;
  ScMessage speed{kCtMsg_SetSimulationControlsEnabled, 1, 0, 0};
  server->MessagePost(speed, false);
  if (finishNotificationRegistered) {
    server->RemoveNotification(static_cast<CtLayer *>(this), kApMsgIdle);
    finishNotificationRegistered = 0;
  }
  server->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_SimulatorStarting);
  server->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioStatus);
  server->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioPopup);
  server->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_ShowScenarioEndDialog);
  StopScenario();
  if (scenarioSegment) {
    CtScenarioResourceManager->UnregisterDBSegment(scenarioSegment);
    scenarioSegment->Release();
    scenarioSegment = nullptr;
  }
  if (choiceDialog) {
    choiceDialog->Unbind();
    choiceDialog->Release();
    choiceDialog = nullptr;
  }
  ScDelete(&resultsText);
  ScRelease(&confirmDialog);
  ScRelease(&progressDialog);
  ScRelease(&aura);
  ScRelease(&buildings);
  ScRelease(&crime);
  ScRelease(&landValue);
  ScRelease(&water);
  ScRelease(&pollution);
  ScRelease(&power);
  ScRelease(&residential);
  ScRelease(&budget);
  ScRelease(&traffic);
  ScRelease(&ordinances);
  ScRelease(&surfaceOccupants);
  ScRelease(&city);
  ScRelease(&dailyManipulator);
  ScRelease(&otherManipulator);
  ScDelete(&dialogText);
  ScDelete(&progressCaption);
  CtCurrentScenarioLayer = nullptr;
  return true;
}

// Return the matching borrowed manipulator and flag for a supported agent type.
// Unrecognized types leave the caller's flag unchanged while clearing the manipulator output.
bool CtScenarioLayerImpl::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  u32 id = type.Id();
  if (id == kCtAgentType_AreaAgent) {
    *out = dailyManipulator;
    *flags = 2;
  } else if (id == kCtAgentType_DisasterInstanceMinuteTick) {
    *out = otherManipulator;
    *flags = 0;
  }
  return true;
}

u32 CtScenarioLayerImpl::GetLayerType() {
  return kCtScenarioLayerType;
}

bool CtScenarioLayerImpl::DebugClassTag(ScStringView *) {
  return false;
}

bool CtScenarioLayerImpl::DebugTypeTag(ScStringView *) {
  return false;
}

// Read the requested immediate or variable argument; missing arguments evaluate to zero. A variable reference must
// remain inside the fixed variable bank.
i32 CtScenarioLayerImpl::EvaluateArgument(CtScenarioCommand *command, u32 index) {
  if (index >= command->argumentCount)
    return 0;
  CtScenarioArgument *argument = command->arguments + index;
  if (argument->variable) {
    assert(u32(argument->value) < 512);
    return variables[argument->value];
  }
  return argument->value;
}

// Send a formatted scenario ticker entry through the original synchronous message service.
i32 CtScenarioLayerImpl::ShowNewsticker(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  i32 group = EvaluateArgument(command, 0), instance = EvaluateArgument(command, 1);
  ScString *formatted = new ScString;
  RsKey key(kRsTextResourceType, u32(group), u32(instance));
  if (CtScenarioResourceManager)
    CtScenarioResourceManager->RemoveResource(key);
  CtScenarioText resource{u32(instance), u32(group)};
  resource.Format(formatted, CtScenarioExpandToken, this);
  ApNewsMessage message{};
  message.result = 0;
  message.group = u32(EvaluateArgument(command, 0));
  message.text = formatted;
  message.message = ScMessage(kCtMsg_ShowScenarioStatus);
  formatted->AddRef();
  auto entry = tickerEntries.find(u32(instance));
  ScMessage notification = entry == tickerEntries.end()
      ? ScMessage(kApMsgCreateNotification, reinterpret_cast<uptr>(&message), 0, 0)
      : ScMessage(kApMsgCreateNotificationWithTickerInfo, reinterpret_cast<uptr>(&message),
            reinterpret_cast<uptr>(&entry->second), 0);
  CtGetScenarioMessageServer()->MessageSend(notification);
  if (message.result)
    *pendingNotifications.Append() = message.result;
  formatted->Release();
  return 1;
}

// The byte return's original Boolean or byte declaration remains unidentified.
u8 CtScenarioLayerImpl::ResolveResultVariable(CtScenarioCommand *command, u32 *out) {
  *out = 0;
  if (!command->argumentCount)
    return 0;
  *out = u32(EvaluateArgument(command, 0));
  if (*out >= 512) {
    *out = 0;
    return 0;
  }
  return 1;
}

// The byte return's original Boolean or byte declaration remains unidentified.
u8 CtScenarioLayerImpl::ResolveVariable(CtScenarioCommand *command, u32 index, u32 *out) {
  *out = 0;
  if (index >= command->argumentCount)
    return 0;
  *out = u32(EvaluateArgument(command, index));
  if (*out >= 512) {
    *out = 0;
    return 0;
  }
  return 1;
}

// Add two evaluated arguments into the resolved variable and return the stored word.
i32 CtScenarioLayerImpl::Add(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  variables[destination] = i32(u32(first) + u32(second));
  return variables[destination];
}

i32 CtScenarioLayerImpl::Subtract(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  variables[destination] = i32(u32(first) - u32(second));
  return variables[destination];
}

// Multiply the evaluated arguments with the original word-width result.
i32 CtScenarioLayerImpl::Multiply(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  variables[destination] = i32(u32(first) * u32(second));
  return variables[destination];
}

// Divide two evaluated arguments; a zero divisor stores minus one.
// The original handles zero explicitly but cannot represent the signed overflow quotient.
i32 CtScenarioLayerImpl::Divide(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  assert(first != INT_MIN || second != -1);
  variables[destination] = second ? first / second : -1;
  return variables[destination];
}

// Multiply the first value by 100 before signed division; zero stores minus one.
// Apply the original word-width multiplication before checking the signed division invariant.
i32 CtScenarioLayerImpl::ComputePercentage(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  first = i32(u32(first) * 100u);
  assert(first != INT_MIN || second != -1);
  variables[destination] = second ? first / second : -1;
  return variables[destination];
}

i32 CtScenarioLayerImpl::Increment(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = i32(u32(variables[destination]) + 1u);
  return variables[destination];
}

i32 CtScenarioLayerImpl::Decrement(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = i32(u32(variables[destination]) - 1u);
  return variables[destination];
}

// Assign the evaluated value to the destination and return success rather than the value.
i32 CtScenarioLayerImpl::SetVariable(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (execute) {
    if (!ResolveResultVariable(command, &destination))
      return 0;
    variables[destination] = EvaluateArgument(command, 1);
  }
  return 1;
}

// Compare two evaluated arguments for equality; dry execution returns true.
i32 CtScenarioLayerImpl::Equal(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  i32 first = EvaluateArgument(command, 0);
  return first == EvaluateArgument(command, 1);
}

// Compare two evaluated arguments for inequality; dry execution returns true.
i32 CtScenarioLayerImpl::NotEqual(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  i32 first = EvaluateArgument(command, 0);
  return first != EvaluateArgument(command, 1);
}

// Compare the evaluated arguments as signed values; dry execution returns true.
i32 CtScenarioLayerImpl::GreaterThan(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  i32 first = EvaluateArgument(command, 0);
  return first > EvaluateArgument(command, 1);
}

// Compare the evaluated arguments as signed values; dry execution returns true.
i32 CtScenarioLayerImpl::LessThan(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  i32 first = EvaluateArgument(command, 0);
  return first < EvaluateArgument(command, 1);
}

i32 CtScenarioLayerImpl::ElapsedMonths() {
  return i32(currentMonth + 12u * (currentYear - startYear) - startMonth);
}

// Store and return the elapsed-month count in the resolved destination.
i32 CtScenarioLayerImpl::GetMonthsElapsed(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = ElapsedMonths();
  return variables[destination];
}

// Store the year, month and day in argument order and return success.
i32 CtScenarioLayerImpl::GetCurrentDate(CtScenarioCommand *command, i32 execute) {
  u32 yearSlot, monthSlot, daySlot, month, day, year;
  if (execute) {
    if (!ResolveVariable(command, 0, &yearSlot) || !ResolveVariable(command, 1, &monthSlot) ||
        !ResolveVariable(command, 2, &daySlot))
      return 0;
    city->GetDate(&month, &day, &year);
    variables[yearSlot] = i32(year);
    variables[monthSlot] = i32(month);
    variables[daySlot] = i32(day);
  }
  return 1;
}

i32 CtScenarioLayerImpl::GetAura(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = aura->GetMayorRating();
  return variables[destination];
}

// Store the crime layer's raw unsigned-byte average.
i32 CtScenarioLayerImpl::GetCrimeValue(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = crime->GetAverageValue();
  return variables[destination];
}

i32 CtScenarioLayerImpl::GetPopulation(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = residential->GetPopulation();
  return variables[destination];
}

// Store the low word of the budget's original 64-bit funds value.
i32 CtScenarioLayerImpl::GetFundsAvailable(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = i32(u32(budget->GetTotalFunds()));
  return variables[destination];
}

// Store the low word of the budget's original 64-bit borrowed amount.
i32 CtScenarioLayerImpl::GetTotalDebt(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = i32(u32(budget->GetTotalBorrowed()));
  return variables[destination];
}

// Resolve all three destinations before querying residential, commercial and industrial tax rates.
i32 CtScenarioLayerImpl::GetRciTaxes(CtScenarioCommand *command, i32 execute) {
  u32 first, second, third;
  if (execute) {
    if (!ResolveVariable(command, 0, &first) || !ResolveVariable(command, 1, &second) ||
        !ResolveVariable(command, 2, &third))
      return 0;
    variables[first] = budget->GetTaxRate(0);
    variables[second] = budget->GetTaxRate(1);
    variables[third] = budget->GetTaxRate(2);
  }
  return 1;
}

i32 CtScenarioLayerImpl::CountUnpoweredBuildings(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = i32(power->GetNumberOfUnpoweredConsumers());
  return variables[destination];
}

// Store production minus demand and clamp negative signed results to zero.
// Unlike most value queries, this command returns success instead of the stored value.
i32 CtScenarioLayerImpl::GetSurplusPower(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (execute) {
    if (!ResolveResultVariable(command, &destination))
      return 0;
    u32 produced = power->GetTotalCityPowerProduced();
    u32 needed = power->GetTotalPowerNeededByTheCity();
    variables[destination] = i32(produced - needed);
    if (variables[destination] < 0)
      variables[destination] = 0;
  }
  return 1;
}

i32 CtScenarioLayerImpl::CountUnwateredBuildings(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  u32 consumers = 0, first = 0, unwatered = u32(uptr(command));
  i64 capacity = 0, used = 0;
  water->GetWaterData(&consumers, &capacity, &used, &first, &unwatered);
  variables[destination] = i32(unwatered);
  return variables[destination];
}

// Store the low-word capacity minus use reported by the water layer.
// This command returns success independently of the stored surplus.
i32 CtScenarioLayerImpl::GetSurplusWater(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (execute) {
    if (!ResolveResultVariable(command, &destination))
      return 0;
    u32 consumers = 0, first = 0, second = u32(uptr(command));
    i64 capacity = 0, used = 0;
    water->GetWaterData(&consumers, &capacity, &used, &first, &second);
    variables[destination] = i32(u32(capacity) - u32(used));
  }
  return 1;
}

// Store the land-value average returned by the installed zero-extending byte producer.
i32 CtScenarioLayerImpl::GetLandValue(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  variables[destination] = landValue->GetAverageValue();
  return variables[destination];
}

// Select the air, water or garbage average; an unsupported selector leaves the destination unchanged.
// The installed garbage producer zero-extends its 16-bit field even though its public interface returns i16.
i32 CtScenarioLayerImpl::GetPollutionValue(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 selector = EvaluateArgument(command, 1), value;
  if (selector == 0)
    value = pollution->GetAverageAirValue();
  else if (selector == 1)
    value = pollution->GetAverageWaterValue();
  else if (selector == 2)
    value = u16(pollution->GetAverageGarbageValue());
  else
    return 0;
  variables[destination] = value;
  return variables[destination];
}

// Query average traffic over the city and store the single output byte.
i32 CtScenarioLayerImpl::GetTrafficDensity(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  u8 value = u8(u32(execute) >> 24);
  traffic->AverageVolume(0, 0, width, height, &value, 3);
  variables[destination] = value;
  return variables[destination];
}

// Test whether the requested ordinance is both available and enabled, then apply the requested sense.
i32 CtScenarioLayerImpl::IsOrdinance(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 id = u32(EvaluateArgument(command, 0));
  i32 sense = EvaluateArgument(command, 1);
  bool present = ordinances->QueryOrdinanceAvailable(id) && ordinances->QueryOrdinanceOn(id);
  return sense == 1 ? present : !present;
}

// Test a neighbor-side connection using the low bytes of the first two arguments.
i32 CtScenarioLayerImpl::IsNeighborConnection(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u8 network = u8(EvaluateArgument(command, 0)), side = u8(EvaluateArgument(command, 1));
  i32 sense = EvaluateArgument(command, 2);
  bool present = city->NeighborsLayer()->HasSideConnection(network, side);
  return sense == 1 ? present : !present;
}

// Fetch the requested neighbor deal and compare its two stored selector bytes.
i32 CtScenarioLayerImpl::IsNeighborDealPresent(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u8 type = u8(EvaluateArgument(command, 0)), neighbor = u8(EvaluateArgument(command, 1));
  i32 sense = EvaluateArgument(command, 2);
  CtDealRecord *deal = new CtDealRecord{};
  bool present = city->NeighborsLayer()->GetDeal(type, deal) && deal->value_10 == neighbor && deal->type == type;
  delete deal;
  return sense == 1 ? present : !present;
}

i32 CtScenarioLayerImpl::IsBusinessDealActive(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 type = u32(EvaluateArgument(command, 0));
  i32 sense = EvaluateArgument(command, 1);
  bool active = city->GetBudgetAdvisor()->IsBusinessDealActive(type);
  return sense == 1 ? active : !active;
}

i32 CtScenarioLayerImpl::OfferBusinessDeal(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    u32 type = u32(EvaluateArgument(command, 0));
    city->GetBudgetAdvisor()->OfferBusinessDeal(type);
  }
  return 1;
}

i32 CtScenarioLayerImpl::OfferRewardBuilding(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 type = u32(EvaluateArgument(command, 0));
  u8 result = u8(u32(execute) >> 24);
  AdCityPlanning *planning = city->GetCityPlanningAdvisor();
  if (planning)
    result = planning->AddBusinessDealBuildingToRewardList(type);
  return result != 0;
}

// Enable or disable city disasters from the evaluated Boolean argument.
i32 CtScenarioLayerImpl::SetDisastersEnabled(CtScenarioCommand *command, i32 execute) {
  if (execute)
    city->SetDisastersEnabled(EvaluateArgument(command, 0) != 0);
  return 1;
}

// Disable new neighbor deals for a valid type or the original all-types sentinel.
i32 CtScenarioLayerImpl::DisableNewNeighborDeals(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    u32 type = u32(EvaluateArgument(command, 0));
    if (type == UINT32_MAX || type <= 5) {
      AdUtilities *advisor = city->GetUtilityAdvisor();
      if (advisor)
        advisor->DisableNewDealsOfType(i32(type));
    }
  }
  return 1;
}

// Enable new neighbor deals for a valid type or the original all-types sentinel.
i32 CtScenarioLayerImpl::EnableNewNeighborDeals(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    u32 type = u32(EvaluateArgument(command, 0));
    if (type == UINT32_MAX || type <= 5) {
      AdUtilities *advisor = city->GetUtilityAdvisor();
      if (advisor)
        advisor->EnableNewDealsOfType(i32(type));
    }
  }
  return 1;
}

// Discard the cached script resource, load the requested key and run it before releasing the identity.
i32 CtScenarioLayerImpl::Subroutine(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  RsKey key{kCtScenarioScriptResourceType, u32(EvaluateArgument(command, 0)), u32(EvaluateArgument(command, 1))};
  CtScenarioResourceManager->RemoveResource(key);
  CtScenarioScript *script;
  if (!CtScenarioResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&script)))
    return 0;
  i32 result = RunScript(script);
  script->Release();
  return result;
}

i32 CtScenarioLayerImpl::RepeatRegular(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 period = u32(EvaluateArgument(command, 0));
  RsKey key{kCtScenarioScriptResourceType, u32(EvaluateArgument(command, 1)), u32(EvaluateArgument(command, 2))};
  return AddPeriodicScript(period, key) != 0;
}

// Draw an inclusive random integer between the evaluated endpoints, accepting either order.
i32 CtScenarioLayerImpl::GenerateRandomNum(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
  if (first == second)
    variables[destination] = first;
  else if (second <= first)
    variables[destination] = random.Range(second, i32(u32(first) + 1u));
  else
    variables[destination] = random.Range(first, i32(u32(second) + 1u));
  return variables[destination];
}

// Obtain the earthquake control and start it with a clamped one-tenth argument and Boolean mode.
i32 CtScenarioLayerImpl::StartEarthquake(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsTrackTypeId);
  if (!manager)
    return 0;
  DsTrackControl *control = nullptr;
  manager->QueryInterface(IID_DsTrackControl, reinterpret_cast<void **>(&control));
  assert(control);
  f64 scaled = f64(EvaluateArgument(command, 0)) * f64(0.1f);
  f32 strength = scaled < 0.0 ? 0.0f : scaled > 1.0 ? 1.0f : f32(scaled);
  bool automatic = EvaluateArgument(command, 1) != 0;
  bool result = control->Start(strength, automatic);
  control->Release();
  return result;
}

// Start fire at one-based input coordinates clamped to the layer's city bounds.
i32 CtScenarioLayerImpl::StartFire(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsFireTypeId);
  if (!manager)
    return 0;
  DsFireControl *control = nullptr;
  manager->QueryInterface(IID_DsFireControl, reinterpret_cast<void **>(&control));
  assert(control);
  i32 x = i32(u32(EvaluateArgument(command, 0)) - 1u), z = i32(u32(EvaluateArgument(command, 1)) - 1u);
  if (x < 0)
    x = 0;
  if (z < 0)
    z = 0;
  u32 clampedX = u32(x) > width ? width : u32(x), clampedZ = u32(z) > height ? height : u32(z);
  u8 result = control->IgniteCell(clampedX, clampedZ);
  control->Release();
  return result != 0;
}

// Obtain the UFO control and start it with the evaluated Boolean mode.
i32 CtScenarioLayerImpl::StartUfoAttack(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsUfoTypeId);
  if (!manager)
    return 0;
  DsUfoControl *control = nullptr;
  manager->QueryInterface(IID_DsUfoControl, reinterpret_cast<void **>(&control));
  assert(control);
  u8 automatic = EvaluateArgument(command, 0) != 0;
  u8 result = control->Start(automatic);
  control->Release();
  return result != 0;
}

// Obtain the locust control and start it with the evaluated Boolean mode.
i32 CtScenarioLayerImpl::StartLocusts(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsLocustTypeId);
  if (!manager)
    return 0;
  DsLocustControl *control = nullptr;
  manager->QueryInterface(IID_DsLocustControl, reinterpret_cast<void **>(&control));
  assert(control);
  u8 automatic = EvaluateArgument(command, 0) != 0;
  u8 result = control->Start(automatic);
  control->Release();
  return result != 0;
}

// Start a space-debris impact with a clamped one-tenth magnitude and Boolean mode.
i32 CtScenarioLayerImpl::StartSpaceJunk(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsImpactTypeId);
  if (!manager)
    return 0;
  DsImpactControl *control = nullptr;
  manager->QueryInterface(IID_DsImpactControl, reinterpret_cast<void **>(&control));
  assert(control);
  f64 scaled = f64(EvaluateArgument(command, 0)) * f64(0.1f);
  f32 strength = scaled < 0.0 ? 0.0f : scaled > 1.0 ? 1.0f : f32(scaled);
  u8 automatic = EvaluateArgument(command, 1) != 0;
  u8 result = control->Start(strength, automatic);
  control->Release();
  return result != 0;
}

// Start the water disaster with a clamped one-tenth magnitude and Boolean mode.
i32 CtScenarioLayerImpl::StartWhirlpool(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsWaterTypeId);
  if (!manager)
    return 0;
  DsWaterControl *control = nullptr;
  manager->QueryInterface(IID_DsWaterControl, reinterpret_cast<void **>(&control));
  assert(control);
  f64 scaled = f64(EvaluateArgument(command, 0)) * f64(0.1f);
  f32 strength = scaled < 0.0 ? 0.0f : scaled > 1.0 ? 1.0f : f32(scaled);
  u8 automatic = EvaluateArgument(command, 1) != 0;
  u8 result = control->Start(strength, automatic);
  control->Release();
  return result != 0;
}

// Start a riot at one-based coordinates clamped to city bounds and use the evaluated Boolean mode.
i32 CtScenarioLayerImpl::StartRiot(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsRiotTypeId);
  if (!manager)
    return 0;
  DsRiotControl *control = nullptr;
  manager->QueryInterface(IID_DsRiotControl, reinterpret_cast<void **>(&control));
  assert(control);
  i32 x = i32(u32(EvaluateArgument(command, 0)) - 1u), z = i32(u32(EvaluateArgument(command, 1)) - 1u);
  if (x < 0)
    x = 0;
  if (z < 0)
    z = 0;
  u32 clampedX = u32(x) > width ? width : u32(x), clampedZ = u32(z) > height ? height : u32(z);
  u8 automatic = EvaluateArgument(command, 2) != 0;
  DsRiotState *result = control->Start(clampedX, clampedZ, automatic);
  control->Release();
  return result != nullptr;
}

// Start a stationary toxic cloud at the original unsigned-clamped one-based coordinates.
i32 CtScenarioLayerImpl::StartToxicCloud(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsToxicTypeId);
  if (!manager)
    return 0;
  DsToxicControl *control = nullptr;
  manager->QueryInterface(IID_DsToxicControl, reinterpret_cast<void **>(&control));
  assert(control);
  u32 x = u32(EvaluateArgument(command, 0)) - 1u, z = u32(EvaluateArgument(command, 1)) - 1u;
  if (x > width)
    x = width;
  if (z > height)
    z = height;
  u8 automatic = EvaluateArgument(command, 2) != 0;
  ScVec3F velocity{0.0f, 0.0f, 0.0f};
  DsToxicMotion *result = control->Start(x, z, &velocity, 0, 0, 0, automatic);
  control->Release();
  return result != nullptr;
}

// Start a tornado along the normalized vector between two clamped cells.
// A zero-length direction returns immediately with the original retained control reference.
i32 CtScenarioLayerImpl::StartTornado(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  DsLayer *layer = city->DisasterLayer();
  if (!layer)
    return 0;
  DsManager *manager = layer->GetDisasterManager(kDsTornadoTypeId);
  if (!manager)
    return 0;
  DsTornadoControl *control = nullptr;
  manager->QueryInterface(IID_DsTornadoControl, reinterpret_cast<void **>(&control));
  assert(control);
  i32 x = i32(u32(EvaluateArgument(command, 0)) - 1u);
  i32 z = i32(u32(EvaluateArgument(command, 1)) - 1u);
  i32 endX = i32(u32(EvaluateArgument(command, 2)) - 1u);
  i32 endZ = i32(u32(EvaluateArgument(command, 3)) - 1u);
  i32 severity = EvaluateArgument(command, 4), duration = EvaluateArgument(command, 5);
  i32 speed = EvaluateArgument(command, 6);
  bool automatic = EvaluateArgument(command, 7) != 0;
  if (x < 0)
    x = 0;
  if (z < 0)
    z = 0;
  if (endX < 0)
    endX = 0;
  if (endZ < 0)
    endZ = 0;
  if (u32(x) > width)
    x = i32(width);
  if (u32(z) > height)
    z = i32(height);
  if (u32(endX) > width)
    endX = i32(width);
  if (u32(endZ) > height)
    endZ = i32(height);
  if (severity < 0)
    severity = 0;
  if (severity > 2)
    severity = 2;
  if (duration < 0)
    duration = 0;
  if (duration > 400)
    duration = 400;
  if (speed < 0)
    speed = 1;
  if (speed > 500)
    speed = 500;
  f64 dx = f64(endX) - f64(x), dz = f64(endZ) - f64(z);
  f32 storedX = f32(dx), storedZ = f32(dz);
  f64 length = sqrt(dx * dx + dz * dz);
  if (length == 0.0)
    return 0;
  ScVec3F direction{f32(f64(storedX) / length), 0.0f, f32(f64(storedZ) / length)};
  f32 units = f32(f64(speed) * f64(256.0f) * f64(1.6666666851961054e-5f));
  DsTornadoMotion *result = control->Start(u32(x), u32(z), &direction, i8(severity), units, duration, automatic);
  control->Release();
  return result != nullptr;
}

// Replace the keyed goal with an unmet entry containing the two evaluated descriptor words.
i32 CtScenarioLayerImpl::AddGoal(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    CtScenarioGoalGuard guard(&goalLock);
    u32 id = u32(EvaluateArgument(command, 0));
    i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
    goals.erase(id);
    CtScenarioGoal &goal = goals[id];
    goal.Initialize(first, second);
    goal.achieved = 0;
  }
  return 1;
}

// Report whether every stored goal is met; dry execution also returns true.
i32 CtScenarioLayerImpl::AllGoalsMet(CtScenarioCommand *, i32 execute) {
  if (execute)
    for (const auto &entry : goals)
      if (!entry.second.achieved)
        return 0;
  return 1;
}

// Look up the requested goal and compare its met status with the evaluated sense.
i32 CtScenarioLayerImpl::GoalStatusIs(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 id = u32(EvaluateArgument(command, 0));
  i32 sense = EvaluateArgument(command, 1);
  auto entry = goals.find(id);
  if (entry == goals.end())
    return 0;
  return sense ? entry->second.achieved != 0 : entry->second.achieved == 0;
}

// Update an existing goal under the goal lock and return whether the key was found.
i32 CtScenarioLayerImpl::MarkGoalStatus(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  CtScenarioGoalGuard guard(&goalLock);
  u32 id = u32(EvaluateArgument(command, 0));
  bool achieved = EvaluateArgument(command, 1) != 0;
  auto entry = goals.find(id);
  if (entry == goals.end())
    return 0;
  entry->second.achieved = achieved;
  return 1;
}

// Replace the rank entry selected by the first argument with the other two evaluated words.
i32 CtScenarioLayerImpl::AddRankInfo(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    CtScenarioGoalGuard guard(&goalLock);
    u32 id = u32(EvaluateArgument(command, 0));
    i32 first = EvaluateArgument(command, 1), second = EvaluateArgument(command, 2);
    ranks.erase(id);
    CtScenarioRank &rank = ranks[id];
    rank.first = first;
    rank.second = second;
  }
  return 1;
}

i32 CtScenarioLayerImpl::EnableDisableBusinessDeal(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    u32 type = u32(EvaluateArgument(command, 0));
    i32 enabled = EvaluateArgument(command, 1);
    city->GetBudgetAdvisor()->SetDealEnabled(type, enabled == 1);
  }
  return 1;
}

// Send the evaluated sound event to BoxX when that application service exists.
i32 CtScenarioLayerImpl::PlaySound(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    i32 sound = EvaluateArgument(command, 0);
    ApBoxX *box = CtGetScenarioBoxX();
    if (box)
      box->Event(kAuEventPlayEffect, uptr(u32(sound)), 0, 0, 0);
  }
  return 1;
}

i32 CtScenarioLayerImpl::SetScenarioSpeed(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    i32 speed = EvaluateArgument(command, 0), enabled = EvaluateArgument(command, 1);
    city->SetGameSpeed(speed);
    if (city->IsPaused())
      city->Unpause();
    ScMessage message{kCtMsg_SetSimulationControlsEnabled, uptr(u32(enabled)), 0, 0};
    CtGetScenarioMessageServer()->MessagePost(message, false);
    scenarioSpeedEnabled = enabled != 0;
  }
  return 1;
}

// Validate the city coordinates and post the requested camera movement and zoom.
// The current view's camera hint and the synchronous UI-state query run before the conditional notification.
i32 CtScenarioLayerImpl::MoveCamera(CtScenarioCommand *command, i32 execute) {
  if (!execute)
    return 1;
  u32 x = u32(EvaluateArgument(command, 0)), z = u32(EvaluateArgument(command, 1));
  if (x > width || z > height)
    return 0;
  u32 zoom = u32(EvaluateArgument(command, 2));
  if (zoom >= 5)
    zoom = 2;
  ScRect hint{};
  CtGetScenarioCurrentView()->GetCameraHint(&hint, nullptr);
  ScMessage query{kWnMsg_QueryCityViewParams};
  CtGetScenarioMessageServer()->MessageSend(query);
  if (query.data1) {
    ScMessage notify{kWnMsg_SetCityViewParams, 0, uptr(UINT32_MAX), 1};
    CtGetScenarioMessageServer()->MessagePost(notify, false);
  }
  ScMessage move{kWnMsg_FocusCityCameraAndZoom, x, z, zoom};
  CtGetScenarioMessageServer()->MessagePost(move, false);
  return 1;
}

i32 CtScenarioLayerImpl::SetTextGroup(i32 group) {
  textGroup = group;
  return group;
}

// Replace the dialog text after removing its resource key from the current manager.
// The private nominal return is unidentified; the observed pointer is returned directly.
CtScenarioText *CtScenarioLayerImpl::SetDialogText(i32 group, i32 instance) {
  ScDelete(&dialogText);
  RsKey key{kRsTextResourceType, u32(group), u32(instance)};
  if (CtScenarioResourceManager)
    CtScenarioResourceManager->RemoveResource(key);
  dialogText = new CtScenarioText(u32(instance), u32(group));
  return dialogText;
}

// Replace the result-dialog text after removing its resource key from the current manager.
// The private nominal return is unidentified; the observed pointer is returned directly.
CtScenarioText *CtScenarioLayerImpl::SetResultsText(i32 group, i32 instance) {
  ScDelete(&resultsText);
  RsKey key{kRsTextResourceType, u32(group), u32(instance)};
  if (CtScenarioResourceManager)
    CtScenarioResourceManager->RemoveResource(key);
  resultsText = new CtScenarioText(u32(instance), u32(group));
  return resultsText;
}

i32 CtScenarioLayerImpl::MakeDialogText(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    i32 group = EvaluateArgument(command, 0), instance = EvaluateArgument(command, 1);
    i32 textType = EvaluateArgument(command, 2);
    SetTextGroup(textType);
    SetDialogText(group, instance);
  }
  return 1;
}

// Configure the result dialog text and text group from the three evaluated arguments.
i32 CtScenarioLayerImpl::SetResultsDialogText(CtScenarioCommand *command, i32 execute) {
  if (execute) {
    i32 group = EvaluateArgument(command, 0), instance = EvaluateArgument(command, 1);
    i32 textType = EvaluateArgument(command, 2);
    SetTextGroup(textType);
    SetResultsText(group, instance);
  }
  return 1;
}

// Refresh and acquire the script before testing its period and appending it to the selected vector.
u8 CtScenarioLayerImpl::AddPeriodicScript(u32 period, const RsKey &key) {
  CtScenarioResourceManager->RemoveResource(key);
  CtScenarioScript *script;
  if (!CtScenarioResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&script)))
    return 0;
  if (period >= 4)
    return 0;
  *scheduledScripts[period].Append() = script;
  return 1;
}

// Advance the top script cursor and return its next command, or null after exhaustion.
// The cursor requires the nonnegative command count established by its script allocation.
CtScenarioCommand *CtScenarioLayerImpl::NextCommand() {
  if (!cursors.size)
    return nullptr;
  CtScenarioCursor *cursor = &cursors.data[cursors.size - 1];
  u32 index = ++cursor->index;
  CtScenarioScript *script = cursor->script;
  assert(script->commandCount >= 0);
  if (index >= u32(script->commandCount))
    return nullptr;
  return script->commands + index;
}

// Retain the cursor's script before adding the original index and pointer pair to the cursor stack.
void CtScenarioLayerImpl::PushCursor(const CtScenarioCursor &cursor) {
  if (cursor.script)
    cursor.script->AddRef();
  *cursors.Append() = cursor;
}

// Release the last cursor's script before removing the live last entry.
void CtScenarioLayerImpl::PopCursor() {
  if (cursors.size) {
    CtScenarioScript *script = cursors.data[cursors.size - 1].script;
    if (script)
      script->Release();
    --cursors.size;
  }
}

// Retain a new cursor and execute commands until exhaustion or the running flag is cleared.
// A paused script releases the city lock during the original alertable ten-millisecond wait and reacquires it afterward.
i32 CtScenarioLayerImpl::RunScript(CtScenarioScript *script) {
  CtScenarioCursor cursor{UINT32_MAX, script};
  PushCursor(cursor);
  i32 result = i32(u32(uptr(script)));
  CtScenarioCommand *command = NextCommand();
  while (command) {
    if (!running)
      return 1;
    if (!scriptPaused) {
      result = Dispatch(command, 1);
      command = NextCommand();
    } else {
      ScLock *lock = city->GetCriticalSection();
      if (lock)
        lock->Unlock();
      ScThread::SleepCurrentThread(10000);
      if (lock)
        lock->Lock();
    }
  }
  PopCursor();
  return result;
}

i32 CtScenarioLayerImpl::Assign(CtScenarioCommand *command, i32 execute) {
  u32 destination;
  if (!execute)
    return 1;
  if (!ResolveResultVariable(command, &destination))
    return 0;
  CtScenarioCommand *child = NextCommand();
  if (!child)
    return 0;
  i32 result = Dispatch(child, execute);
  if (execute == 1)
    variables[destination] = result;
  return result;
}

// Evaluate children through the configured end-and marker, dry-dispatching after a false result.
i32 CtScenarioLayerImpl::AndCondition(CtScenarioCommand *, i32 execute) {
  auto marker = CtScenarioCommandIds.find("cmdEndAnd");
  if (marker == CtScenarioCommandIds.end())
    return 0;
  i32 markerId = marker->second;
  i32 result = 1;
  CtScenarioCommand *child;
  while ((child = NextCommand()) != nullptr && child->opcode != markerId) {
    if (!Dispatch(child, execute)) {
      result = 0;
      execute = 0;
    }
  }
  return result;
}

// Evaluate children through the configured end-or marker, dry-dispatching after a true result.
i32 CtScenarioLayerImpl::OrCondition(CtScenarioCommand *, i32 execute) {
  auto marker = CtScenarioCommandIds.find("cmdEndOr");
  if (marker == CtScenarioCommandIds.end())
    return 0;
  i32 markerId = marker->second;
  i32 result = 0;
  CtScenarioCommand *child;
  while ((child = NextCommand()) != nullptr && child->opcode != markerId) {
    if (Dispatch(child, execute)) {
      result = 1;
      execute = 0;
    }
  }
  return result;
}

// Dispatch the sequential children up to the configured end-block marker and return the last result.
// A block must contain a command: the original returns an unwritten local for an empty block.
i32 CtScenarioLayerImpl::BeginBlock(CtScenarioCommand *, i32 execute) {
  auto marker = CtScenarioCommandIds.find("cmdEndBlock");
  if (marker == CtScenarioCommandIds.end())
    return 0;
  i32 markerId = marker->second;
  CtScenarioCommand *child = NextCommand();
  assert(child && child->opcode != markerId);
  i32 result = Dispatch(child, execute);
  while ((child = NextCommand()) != nullptr && child->opcode != markerId)
    result = Dispatch(child, execute);
  return result;
}

// Evaluate a condition and consume its following branch even when the branch is skipped.
// A skipped branch is dispatched with zero so it still advances the nested script.
i32 CtScenarioLayerImpl::If(CtScenarioCommand *, i32 execute) {
  CtScenarioCommand *child = NextCommand();
  if (!child)
    return 0;
  i32 condition = Dispatch(child, execute);
  child = NextCommand();
  if (!child)
    return 0;
  if (condition)
    return Dispatch(child, execute);
  Dispatch(child, 0);
  return condition;
}

// Consume both branches and execute only the branch selected by the condition.
// Each absent required child returns zero, including an absent skipped branch.
i32 CtScenarioLayerImpl::IfElse(CtScenarioCommand *, i32 execute) {
  CtScenarioCommand *child = NextCommand();
  if (!child)
    return 0;
  i32 condition = Dispatch(child, execute);
  child = NextCommand();
  if (!child)
    return 0;
  if (!condition) {
    Dispatch(child, 0);
    child = NextCommand();
    return child ? Dispatch(child, execute) : 0;
  }
  i32 result = Dispatch(child, execute);
  child = NextCommand();
  if (!child)
    return 0;
  Dispatch(child, 0);
  return result;
}

// Select the registered command and force dry execution when the scenario is not running.
// Preserve rejection above the registered range and require a nonnegative opcode for the actual table access.
i32 CtScenarioLayerImpl::Dispatch(CtScenarioCommand *command, i32 execute) {
  i32 opcode = command->opcode;
  if (opcode > CtScenarioLastCommand)
    return 0;
  assert(opcode >= 0);
  CtScenarioHandler &handler = CtScenarioHandlers[opcode];
  if (!handler.method)
    return 0;
  return (this->*handler.method)(command, running ? execute : 0);
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Return the same borrowed lazy director through the module's original loader entry.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return CtGetScenarioDirector();
}
#endif
