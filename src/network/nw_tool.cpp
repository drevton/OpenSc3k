#include "network/nw_tool.h"

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "network/nw_manager.h"
#include "network/nw_module.h"
#include "occupants/animation_manager.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

// Tool state updates retain the upper flag bytes while replacing only the low state byte.
enum NwToolFlagWord : u32 {
  kNwToolUpperFlagBytes = 0xffffff00,
};

NwCommandOwner::NwCommandOwner() : references(0) {
}

NwCommandOwner::~NwCommandOwner() {
}

// Remove a reference through the live secondary virtual method and delete the complete owner only at zero.
u32 NwCommandOwner::Release() {
  u32 result = RemoveRef();
  if (!result)
    delete this;
  return result;
}

u32 NwCommandOwner::AddRef() {
  NwGetDirector()->AddRef();
  return ++references;
}

// Leave zero unchanged; otherwise remove the module reference before decrementing the live owner count.
u32 NwCommandOwner::RemoveRef() {
  if (references) {
    NwGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 NwCommandOwner::RefCount() {
  return references;
}

NwCommand::NwCommand() {
}

// Transfer pending inputs out before releasing each, then drain outputs before retiring the four sequence owners.
NwCommand::~NwCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool NwCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwCommand::AddRef() {
  return NwCommandOwner::AddRef();
}

u32 NwCommand::Release() {
  return NwCommandOwner::Release();
}

// Retain an input argument before publishing it at the tail of the pending sequence.
bool NwCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Publish the oldest retained input and then remove its queue entry; an empty queue leaves the output untouched.
bool NwCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 NwCommand::InputQueueSize() {
  return inputs.size;
}

// Visit input descriptors in insertion order, observing descriptors appended by callbacks before testing the live end.
bool NwCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    UiCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain an output argument before publishing it at the tail of the pending result sequence.
bool NwCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

// Transfer the oldest output reference to the caller; an empty queue leaves its output pointer untouched.
bool NwCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 NwCommand::OutputQueueSize() {
  return outputs.size;
}

// Visit output descriptors in insertion order, preserving callback growth of the live descriptor sequence.
bool NwCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    UiCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool NwCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool NwCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

NwRoadTool::NwRoadTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwRoadTool::~NwRoadTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwRoadTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwRoadTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwRoadTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, surface target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed setup retains the source's partial owners.
bool NwRoadTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->SurfaceOccupantManager());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 28) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 16) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 22))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentRoad);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 3, 1, 1, 1, price)) {
    u32 flags = NwGetCurrentView()->GetCityViewFlags();
    flags = (flags & kNwToolUpperFlagBytes) | ((u8)flags | 4u);
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    NwGetMessageServer()->MessagePost(message, false);
    if (AttachHost(NwGetCityWindow()))
      return true;
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwRoadTool::GetCursor(u32 *out) {
  *out = kSpRoadCursorId;
  return 1;
}

bool NwRoadTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 4) != 0;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwRoadFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the road and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwRoadTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwRoadFeedback(30);
  else if (insufficientFunds)
    NwRoadFeedback(32);
  else if (manager->BuildRoad(selection, &price, &affected, &focus))
    NwRoadFeedback(14);
  return 1;
}

bool NwRoadTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->RoadCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original road-cost body and callback ordering as drag preview.
bool NwRoadTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->RoadCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwRoadTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwHighwayTool::NwHighwayTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwHighwayTool::~NwHighwayTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwHighwayTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwHighwayTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwHighwayTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, surface target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed setup retains the source's partial owners.
bool NwHighwayTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->SurfaceOccupantManager());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 29) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 17) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 23))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentHighway);
  if (drag->Setup(1, target, primary, secondary, tertiary, terrain, 0, 3, 1, 1, 4, price)) {
    u32 flags = NwGetCurrentView()->GetCityViewFlags();
    flags = (flags & kNwToolUpperFlagBytes) | ((u8)flags | 4u);
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    NwGetMessageServer()->MessagePost(message, false);
    if (AttachHost(NwGetCityWindow()))
      return true;
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwHighwayTool::GetCursor(u32 *out) {
  *out = kSpAvenueCursorId;
  return 1;
}

bool NwHighwayTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 4) != 0;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwHighwayFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the highway and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwHighwayTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwHighwayFeedback(30);
  else if (insufficientFunds)
    NwHighwayFeedback(32);
  else if (manager->BuildHighway(selection, &price, &affected, &focus))
    NwHighwayFeedback(14);
  return 1;
}

bool NwHighwayTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->HighwayCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original highway-cost body and callback ordering as drag preview.
bool NwHighwayTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->HighwayCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwHighwayTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwRailTool::NwRailTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwRailTool::~NwRailTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwRailTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwRailTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwRailTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, surface target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed setup retains the source's partial owners.
bool NwRailTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->SurfaceOccupantManager());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 30) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 18) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 24))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentRail);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 3, 1, 1, 2, price)) {
    u32 flags = NwGetCurrentView()->GetCityViewFlags();
    flags = (flags & kNwToolUpperFlagBytes) | ((u8)flags | 4u);
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    NwGetMessageServer()->MessagePost(message, false);
    if (AttachHost(NwGetCityWindow()))
      return true;
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwRailTool::GetCursor(u32 *out) {
  *out = kSpRailroadCursorId;
  return 1;
}

bool NwRailTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 4) != 0;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwRailFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the rail and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwRailTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwRailFeedback(30);
  else if (insufficientFunds)
    NwRailFeedback(32);
  else if (manager->BuildRail(selection, &price, &affected, &focus))
    NwRailFeedback(51);
  return 1;
}

bool NwRailTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->RailCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original rail-cost body and callback ordering as drag preview.
bool NwRailTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->RailCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwRailTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwSubwayTool::NwSubwayTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwSubwayTool::~NwSubwayTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwSubwayTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwSubwayTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwSubwayTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, underground target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed setup retains the source's partial owners.
bool NwSubwayTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 31) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 19) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 25))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentSubway);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 0, 1, 1, 6, price)) {
    ScMessage message(kWnMsg_SetCityViewParams, 1, kSpCityViewDefaultFlagsRequest, 1);
    NwGetMessageServer()->MessagePost(message, false);
    if (AttachHost(NwGetCityWindow()))
      return true;
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwSubwayTool::GetCursor(u32 *out) {
  *out = kSpSubwayTubeCursorId;
  return 1;
}

bool NwSubwayTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return viewKind == 1;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwSubwayFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the subway and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwSubwayTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwSubwayFeedback(30);
  else if (insufficientFunds)
    NwSubwayFeedback(32);
  else if (manager->BuildSubway(selection, &price, &affected, &focus))
    NwSubwayFeedback(53);
  return 1;
}

bool NwSubwayTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->SubwayCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original subway-cost body and callback ordering as drag preview.
bool NwSubwayTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->SubwayCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwSubwayTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwPowerTool::NwPowerTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwPowerTool::~NwPowerTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwPowerTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwPowerTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwPowerTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, surface target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed attachment keeps all predicate references.
bool NwPowerTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->SurfaceOccupantManager());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 33) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 21) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 27))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentPowerLine);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 1, 0, 1, 3, price)) {
    u32 flags = NwGetCurrentView()->GetCityViewFlags();
    flags = (flags & kNwToolUpperFlagBytes) | ((u8)flags | 8u);
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    NwGetMessageServer()->MessagePost(message, false);
    return AttachHost(NwGetCityWindow());
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwPowerTool::GetCursor(u32 *out) {
  *out = kSpPowerLinesCursorId;
  return 1;
}

bool NwPowerTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 8) != 0;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwPowerFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the power and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwPowerTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwPowerFeedback(30);
  else if (insufficientFunds)
    NwPowerFeedback(32);
  else if (manager->BuildPower(selection, &price, &affected, &focus))
    NwPowerFeedback(16);
  return 1;
}

bool NwPowerTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->PowerCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original power-cost body and callback ordering as drag preview.
bool NwPowerTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->PowerCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwPowerTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwPipeTool::NwPipeTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwPipeTool::~NwPipeTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwPipeTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwPipeTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwPipeTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, underground target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed attachment keeps all predicate references.
bool NwPipeTool::Execute() {
  drag = ScAddRefNotNull(new NwDrag());
  CtCity *city = NwGetCity();
  target = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 32) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 20) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 26))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentPipe);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 0, 1, 1, 5, price)) {
    ScMessage message(kWnMsg_SetCityViewParams, 2, kSpCityViewDefaultFlagsRequest, 1);
    NwGetMessageServer()->MessagePost(message, false);
    return AttachHost(NwGetCityWindow());
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwPipeTool::GetCursor(u32 *out) {
  *out = kSpWaterPipesCursorId;
  return 1;
}

bool NwPipeTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return viewKind == 2;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwPipeFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback; otherwise place the pipe and report success.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwPipeTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwPipeFeedback(30);
  else if (insufficientFunds)
    NwPipeFeedback(32);
  else if (manager->BuildPipe(selection, &price, &affected, &focus))
    NwPipeFeedback(50);
  return 1;
}

bool NwPipeTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->PipeCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original pipe-cost body and callback ordering as drag preview.
bool NwPipeTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->PipeCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwPipeTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwDiagonalTool::NwDiagonalTool()
    : manager(nullptr), drag(nullptr), terrain(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr),
      target(nullptr) {
}

NwDiagonalTool::~NwDiagonalTool() {
  ScRelease(&drag);
  ScRelease(&manager);
  ScRelease(&terrain);
  ScRelease(&target);
}

bool NwDiagonalTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return NwCommand::QueryInterface(iid, out);
}

u32 NwDiagonalTool::AddRef() {
  return NwCommand::AddRef();
}

u32 NwDiagonalTool::Release() {
  return NwCommand::Release();
}

// Acquire the real stroke, transit manager, surface target and terrain before querying three ordered predicates.
// Configure the stroke and post the view change before attachment; failed setup retains the source's partial owners.
bool NwDiagonalTool::Execute() {
  drag = ScAddRefNotNull(new NwDiagonalDrag());
  CtCity *city = NwGetCity();
  manager = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  target = ScAddRefNotNull(city->SurfaceOccupantManager());
  terrain = ScAddRefNotNull(city->DirtBag());
  if (!manager->QueryPredicate(reinterpret_cast<void **>(&primary), IID_OcIteratorTest, 2) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&secondary), IID_OcIteratorTest, 0) ||
      !manager->QueryPredicate(reinterpret_cast<void **>(&tertiary), IID_OcIteratorTest, 0))
    return false;
  u32 price = city->BudgetLayer()->GetCost(kCtAgentHighway);
  if (drag->Setup(0, target, primary, secondary, tertiary, terrain, 0, 3, 0, 0, 4, price)) {
    u32 flags = NwGetCurrentView()->GetCityViewFlags();
    flags = (flags & kNwToolUpperFlagBytes) | ((u8)flags | 4u);
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    NwGetMessageServer()->MessagePost(message, false);
    if (AttachHost(NwGetCityWindow()))
      return true;
  }
  primary->Release();
  primary = nullptr;
  secondary->Release();
  secondary = nullptr;
  tertiary->Release();
  tertiary = nullptr;
  return false;
}

bool NwDiagonalTool::GetCursor(u32 *out) {
  *out = kSpOnrampCursorId;
  return 1;
}

bool NwDiagonalTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 4) != 0;
}

// Resolve feedback through a fresh application query, releasing that view before using its borrowed BoxX receiver.
static void NwDiagonalFeedback(i32 event) {
  ApGameInterface *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application))) {
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Refuse invalid or unaffordable previews with their distinct feedback.
// Otherwise place the diagonal and report success through the original feedback event.
// The gesture callback itself always returns the original accepting byte, including when placement refuses.
bool NwDiagonalTool::ExecuteOnMouseUp(SpSelection *selection) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (invalid)
    NwDiagonalFeedback(30);
  else if (insufficientFunds)
    NwDiagonalFeedback(32);
  else if (manager->BuildDiagonal(selection, &price, &affected, &focus))
    NwDiagonalFeedback(14);
  return 1;
}

bool NwDiagonalTool::ExecuteOnMouseDrag(SpSelection *selection) {
  u32 price = 0;
  manager->DiagonalCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// The point preview shares the same original diagonal-cost body and callback ordering as drag preview.
bool NwDiagonalTool::ExecuteOnMouseMove(SpSelection *selection) {
  u32 price = 0;
  manager->DiagonalCost(selection, &price);
  cost = (i32)price;
  CheckCost();
  return 1;
}

// Ask the retained stroke to build the actual selection and mark rejected lines invalid without changing its output.
bool NwDiagonalTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (drag && !drag->DrawLine(start, finish, out, &affected))
    invalid = 1;
  return 1;
}

NwSelection::NwSelection() : begin(nullptr), end(nullptr), storageEnd(nullptr), cursor(0), references(0) {
  Reserve(128);
}

// Copy all coordinates in stored order and then the source cursor; the new owner starts without references.
NwSelection::NwSelection(const NwSelection &source)
    : begin(nullptr), end(nullptr), storageEnd(nullptr), cursor(0), references(0) {
  const CtCoord *first = source.begin, *last = source.end;
  while (first != last)
    Append(*first++);
  cursor = source.cursor;
}

// Publish the actual selection/unknown interface before the live virtual retain; failed queries clear the output.
bool NwSelection::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpSelection);
  HANDLE_QUERY_INTERFACE(IID_SpSelection, SpSelection);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwSelection::AddRef() {
  return ++references;
}

// Release the owner at its final reference through the actual virtual destructor and return zero after deletion.
u32 NwSelection::Release() {
  u32 result = --references;
  if (!result) {
    delete this;
    return 0;
  }
  return result;
}

// Copy at most the requested number from the current cursor, preserving the source's wrapped cursor arithmetic.
// End of storage still returns one; the genuine count output reports how many positions were copied.
u8 NwSelection::Next(u32 maximum, CtCoord *out, u32 *count) {
  u32 copied = 0, size = begin ? u32(end - begin) : 0;
  while (copied < maximum && cursor + copied < size) {
    out[copied] = begin[cursor + copied];
    ++copied;
  }
  cursor += copied;
  *count = copied;
  return 1;
}

u8 NwSelection::Skip(u32 count) {
  cursor += count;
  return 1;
}

u8 NwSelection::Reset() {
  cursor = 0;
  return 1;
}

// Allocate an independent copy, publish its interface first, then retain that published clone once.
u8 NwSelection::Clone(SpSelection **out) {
  NwSelection *copy = new NwSelection(*this);
  *out = copy;
  copy->AddRef();
  return 1;
}

// Compute signed X/Z extents while reproducing the original empty -256 coordinates and altitude sentinels.
// The Boolean result compares the final first X word to -1, including the original true result for empty storage.
bool NwSelection::GetBounds(i32 bounds[6]) {
  bounds[0] = bounds[1] = bounds[3] = bounds[4] = -256;
  bounds[2] = bounds[5] = 0;
  if (begin != end) {
    bounds[0] = bounds[3] = begin->x;
    bounds[1] = bounds[4] = begin->z;
    bounds[2] = bounds[5] = begin->altitude;
    for (CtCoord *position = begin; position != end; ++position) {
      if (position->x < bounds[0])
        bounds[0] = position->x;
      if (position->z < bounds[1])
        bounds[1] = position->z;
      if (position->x > bounds[3])
        bounds[3] = position->x;
      if (position->z > bounds[4])
        bounds[4] = position->z;
      bounds[2] = bounds[5] = INT32_MAX;
    }
  }
  return bounds[0] != -1;
}

void NwSelection::Clear() {
  end = begin;
}

// Grow coordinate storage with ordinary array ownership and preserve every initialized stored position.
void NwSelection::Reserve(u32 capacity) {
  u32 size = begin ? u32(end - begin) : 0;
  if (capacity > UINT32_MAX / sizeof(CtCoord))
    ScFatal(SC_ERROR_NOMEM, "Selection coordinate allocation is too large.");
  CtCoord *storage = new CtCoord[capacity];
  for (u32 index = 0; index < size; ++index)
    storage[index] = begin[index];
  delete[] begin;
  begin = storage;
  end = begin + size;
  storageEnd = begin + capacity;
}

// Capture a possibly aliased coordinate before growth and append it to the actual contiguous range.
void NwSelection::Append(const CtCoord &position) {
  CtCoord value = position;
  if (end == storageEnd) {
    u32 capacity = begin ? u32(storageEnd - begin) : 0;
    if (capacity > UINT32_MAX / 2)
      ScFatal(SC_ERROR_NOMEM, "Selection coordinate capacity overflow.");
    Reserve(capacity ? capacity * 2 : 1);
  }
  *end++ = value;
}

// Retire coordinate storage after clearing the logical range and cursor as the original destructor does.
NwSelection::~NwSelection() {
  Clear();
  cursor = 0;
  delete[] begin;
}

u8 NwSelection::SortAndCompact() {
  ScSortCellPoints(begin, end);
  return 1;
}

// Append each successful source batch plus its initialized history entry when that entry is inside the array.
u8 NwSelection::AppendFrom(SpSelection *source) {
  CtCoord positions[32];
  for (u32 i = 0; i < 32; ++i)
    positions[i] = {INT32_MAX, INT32_MAX, INT32_MAX};
  u32 count = 0;
  source->Reset();
  while (source->Next(32, positions, &count))
    for (u32 i = 0; i < 32 && i <= count; ++i)
      Append(positions[i]);
  return 1;
}

static bool NwToolContains(const GeBounds *bounds, i32 x, i32 z, i32) {
  return x >= bounds->minimum.x && x <= bounds->maximum.x && z >= bounds->minimum.z && z <= bounds->maximum.z;
}

NwTool::NwTool()
    : windows(nullptr), window(nullptr), view(nullptr), cursorMap(nullptr), budget(nullptr),
      bounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, mouseScroll(0), edgeScroll(0),
      cursorVisible(0), costVisible(0), cost(0), invalid(0), insufficientFunds(0), field58(0), detachAfterInput(0) {
}

// The cached window manager is borrowed and is not released by this owner.
NwTool::~NwTool() {
  ScRelease(&budget);
  ScRelease(&cursorMap);
  ScRelease(&view);
  ScRelease(&window);
}

bool NwTool::DoMessage(ScMessage *) {
  return false;
}

bool NwTool::Init() {
  return 1;
}

bool NwTool::Shutdown() {
  window->SetCellCursorVisible(false);
  window->SetCostDisplay(false, 0, 0);
  cursorVisible = 0;
  return 1;
}

bool NwTool::OnKeyDown(u32 key, u32 flags) {
  return window->DefOnKeyDown(key, flags);
}

bool NwTool::OnKeyUp(u32 key, u32 flags) {
  return window->DefOnKeyUp(key, flags);
}

// Capture the default left-down reply before the live global city-window lookup and optional detachment callbacks.
bool NwTool::OnMouseDownL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseDownL(x, y, flags);
  DetachIfRequested(NwGetCityWindow());
  return result;
}

// Begin mouse scrolling before hiding the selection cursor and cost display, retaining the original field/store order.
bool NwTool::OnMouseDownR(i32 x, i32 y, u32) {
  mouseScroll = 1;
  window->SetMouseScroll(i16(x), i16(y), true);
  cursorVisible = 0;
  window->SetCellCursorVisible(false);
  costVisible = 0;
  window->SetCostDisplay(false, 0, 0);
  return 1;
}

// Forward left-up first and preserve its reply across a later optional host transition.
bool NwTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseUpL(x, y, flags);
  DetachIfRequested(NwGetCityWindow());
  return result;
}

// End mouse scrolling and restore the cell cursor before publishing the cost-visible state and optional detachment.
bool NwTool::OnMouseUpR(i32 x, i32 y, u32) {
  mouseScroll = 0;
  window->SetMouseScroll(i16(x), i16(y), false);
  cursorVisible = 1;
  window->SetCellCursorVisible(true);
  costVisible = 1;
  DetachIfRequested(NwGetCityWindow());
  return 1;
}

bool NwTool::OnMouseMove(i32 x, i32 y, u32 flags) {
  if (!cursorVisible) {
    window->SetCellCursorSize(1, 1);
    window->SetCellCursorVisible(true);
    cursorVisible = 1;
  }
  bool result = window->DefOnMouseMove(x, y, flags);
  DetachIfRequested(NwGetCityWindow());
  return result;
}

bool NwTool::GetCursor(u32 *out) {
  *out = kSpCenterCursorId;
  return 1;
}

// Cancel reached scrolling/display operations and reset the current cost without inventing an ordinary reply value.
void NwTool::OnResetState() {
  if (mouseScroll)
    window->SetMouseScroll(0, 0, false);
  if (costVisible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
}

bool NwTool::IsValidHandlerByView(i32, u32) const {
  return 1;
}

// Retain the actual host, its live view/cursor and the city's budget before querying and activating this handler.
// Cursor palette and identifier updates follow activation even when activation refuses the published handler.
bool NwTool::AttachHost(SpCityWindowView *host) {
  windows = NwGetWindowManager();
  window = ScAddRefNotNull(host);
  view = ScAddRefNotNull(window->GetCurrentView());
  cursorMap = ScAddRefNotNull(window->GetCellCursorMap());
  CtCity *city = NwGetCity();
  bounds.minimum = {0, 0, 0};
  u32 depth = city->CellCountZ() - 1, width = city->CellCountX();
  bounds.maximum.z = i32(depth << 8);
  bounds.maximum.x = i32((width << 8) - 256);
  bounds.maximum.altitude = 0;
  budget = ScAddRefNotNull(city->BudgetLayer());
  bool result = false;
  SpViewHandler *handler;
  if (QueryInterface(IID_SpViewHandler, reinterpret_cast<void **>(&handler))) {
    result = window->SetActiveHandler(handler);
    handler->Release();
    handler = nullptr;
  }
  cursorMap->SetDefaultColor(0, 35);
  u32 cursor;
  GetCursor(&cursor);
  window->SetActiveCursor(cursor);
  return result;
}

// Detach only when requested, clearing each active scrolling flag before its callback and finally removing the handler.
// Keep the original unused city-window argument and its caller-side global lookup separate from the retained host.
bool NwTool::DetachIfRequested(SpCityWindowView *) {
  if (!detachAfterInput)
    return false;
  if (mouseScroll) {
    mouseScroll = 0;
    window->SetMouseScroll(0, 0, false);
  }
  if (edgeScroll) {
    edgeScroll = 0;
    window->SetEdgeScroll(false);
  }
  return window->SetActiveHandler(nullptr);
}

void NwTool::ResetCost() {
  invalid = insufficientFunds = field58 = 0;
}

// Compare a captured signed32 cost to signed64 funds only when nonzero, then show it when mouse scrolling is inactive.
// Reload the live insufficient-funds flag and scrolling state after the budget callback.
void NwTool::ShowCost() {
  i32 value = cost;
  insufficientFunds = value && i64(value) > budget->GetTotalFunds();
  if (!mouseScroll)
    window->SetCostDisplay(true, value, insufficientFunds != 0);
}

// Query funds before reading the current signed cost and update only the insufficient-funds byte.
void NwTool::CheckCost() {
  i64 funds = budget->GetTotalFunds();
  i32 value = cost;
  insufficientFunds = value && i64(value) > funds;
}

NwDragTool::NwDragTool()
    : dragging(0), first{INT32_MAX, INT32_MAX, INT32_MAX}, last{INT32_MAX, INT32_MAX, INT32_MAX}, field88(INT32_MAX),
      field92(INT32_MAX), field96(INT32_MAX) {
}

NwDragTool::~NwDragTool() {
}

bool NwDragTool::Init() {
  return NwTool::Init();
}

bool NwDragTool::Shutdown() {
  return NwTool::Shutdown();
}

// Pick the press coordinate before starting drag/edge-scroll state, then begin only when the picked point is in bounds.
bool NwDragTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (view->PerformPick(x, y, &point, false)) {
    dragging = edgeScroll = 1;
    window->SetEdgeScroll(true);
    if (NwToolContains(&bounds, point.x, point.z, point.altitude)) {
      first = point;
      ResetCost();
      ExecuteOnMouseDown(first);
      ShowCost();
    }
  }
  return 1;
}

// Pick and optionally build/commit the released selection, keeping the live selection output until its final release.
// Finish edge scrolling, cursor clearing and optional detachment in the original callback order.
bool NwDragTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  SpSelection *selection = nullptr;
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (NwToolContains(&bounds, first.x, first.z, first.altitude) &&
          NwToolContains(&bounds, point.x, point.z, point.altitude))
        last = point;
    }
    if (cursorVisible && NwToolContains(&bounds, first.x, first.z, first.altitude) &&
        NwToolContains(&bounds, last.x, last.z, last.altitude)) {
      ResetCost();
      ExecuteLineDraw(first, last, &selection);
      ExecuteOnMouseUp(selection);
      ShowCost();
      ScRelease(&selection);
    }
  }
  edgeScroll = 0;
  window->SetEdgeScroll(false);
  cursorMap->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  DetachIfRequested(NwGetCityWindow());
  return 1;
}

// Drag previews flush queued window input before updating the current cost display.
bool NwDragTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (!cursorVisible) {
    window->SetCellCursorVisible(true);
    cursorVisible = 1;
  }
  if (dragging) {
    if (mouseScroll) {
      edgeScroll = 0;
      window->SetEdgeScroll(false);
    } else if (!edgeScroll) {
      edgeScroll = 1;
      window->SetEdgeScroll(true);
    }
  } else if (edgeScroll) {
    edgeScroll = 0;
    window->SetEdgeScroll(false);
  }
  if (mouseScroll || edgeScroll)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false)) {
    SpSelection *selection = nullptr;
    if (dragging) {
      if (NwToolContains(&bounds, first.x, first.z, first.altitude) &&
          NwToolContains(&bounds, point.x, point.z, point.altitude)) {
        last = point;
        if (cursorVisible && !mouseScroll) {
          ResetCost();
          ExecuteLineDraw(first, last, &selection);
          ExecuteOnMouseDrag(selection);
          windows->FlushInputMessageQueue();
          ShowCost();
        }
      }
    } else if (NwToolContains(&bounds, point.x, point.z, point.altitude)) {
      first = point;
      if (cursorVisible && !mouseScroll) {
        ResetCost();
        ExecuteLineDraw(first, first, &selection);
        ExecuteOnMouseMove(selection);
        ShowCost();
      }
    }
    if (selection)
      selection->Release();
  }
  return 1;
}

// Cancel reached scrolling/display operations, then invalidate only the original XZ endpoints and current cost.
void NwDragTool::OnResetState() {
  if (mouseScroll)
    window->SetMouseScroll(0, 0, false);
  if (costVisible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
  last.z = last.x = first.z = first.x = INT32_MAX;
}

bool NwDragTool::ExecuteOnMouseDown(const CtCoord &) {
  return 1;
}

bool NwDragTool::ExecuteOnMouseUp(SpSelection *) {
  return 1;
}

bool NwDragTool::ExecuteOnMouseDrag(SpSelection *) {
  return 1;
}

bool NwDragTool::ExecuteOnMouseMove(SpSelection *) {
  return 1;
}

// Draw the actual cursor line in color35 and return its genuine enumerator through the original output pointer.
// The generic method reports one regardless of the enumerator provider's Boolean reply.
bool NwDragTool::ExecuteLineDraw(const CtCoord &start, const CtCoord &finish, SpSelection **out) {
  cursorMap->SetLine(start, finish, 35);
  cursorMap->GetEnumerator(out);
  return 1;
}

void NwDragTool::ExecuteOnResetState() {
}
