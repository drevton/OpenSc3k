#include "terrain/tr_commands.h"

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"

#include "common/ids.h"

#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "occupants/animation_manager.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "terrain/api.h"
#include "terrain/tr_director.h"
#include "ui/api.h"
#include "window/api.h"

TrCommandOwner::TrCommandOwner() : references(0) {
}

TrCommandOwner::~TrCommandOwner() {
}

u32 TrCommandOwner::AddRef() {
  TrGetDirector()->AddRef();
  return ++references;
}

// A zero count stays zero. Otherwise remove the module reference before decrementing the live count.
u32 TrCommandOwner::RemoveRef() {
  if (references) {
    TrGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

// Consult the virtual removal result and destroy through the secondary lifetime interface only when it returns zero.
u32 TrCommandOwner::Release() {
  u32 value = RemoveRef();
  if (!value)
    delete this;
  return value;
}

u32 TrCommandOwner::RefCount() {
  return references;
}

TrCommand::TrCommand() {
}

// Drain pending inputs before outputs, transferring each pointer out of its queue before releasing it.
// The descriptor sequences and backing storage retire after the queued argument callbacks finish.
TrCommand::~TrCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool TrCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 TrCommand::AddRef() {
  return TrCommandOwner::AddRef();
}

u32 TrCommand::Release() {
  return TrCommandOwner::Release();
}

// Retain the supplied input before publishing it at the back of the pending argument sequence.
bool TrCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Leave an empty output untouched; otherwise publish the oldest retained input before removing its queue entry.
bool TrCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 TrCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate input descriptors in order, observing callback-appended descriptors before checking the live end.
bool TrCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    UiCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain the supplied output before publishing it at the back of the result sequence.
bool TrCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

bool TrCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 TrCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate output descriptors in order and pass the original separate type, flags and context arguments.
bool TrCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    UiCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool TrCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool TrCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

// Preserve the shared stroke-feedback phase through the real framework application query and borrowed BoxX interface.
// Release the application before dispatching the five event words; the event reply is unused.
static void TrToolFeedback(i32 event, i32 first, i32 second, i32 third, i32 fourth) {
  void *object = nullptr;
  if (TrGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &object)) {
    ApGameInterface *application = static_cast<ApGameInterface *>(object);
    ApBoxX *box = application->GetBoxX();
    application->Release();
    box->Event(event, first, second, third, fourth);
  }
}

TrRaiseTool::TrRaiseTool()
    : extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, terrain(nullptr),
      demolition(nullptr), mode(0), unknown_flag(0) {
}

// Unsubscribe the primary message receiver before releasing the retained terrain and demolition interfaces.
// Each field clears after its callback; the command queues then retire before the common drag-tool interfaces.
TrRaiseTool::~TrRaiseTool() {
  TrGetMessageServer()->RemoveNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  ScRelease(&terrain);
  ScRelease(&demolition);
}

bool TrRaiseTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return TrCommand::QueryInterface(iid, out);
}

u32 TrRaiseTool::AddRef() {
  TrGetDirector()->AddRef();
  return ++TrCommandOwner::references;
}

u32 TrRaiseTool::Release() {
  return TrCommandOwner::Release();
}

// A zero count remains zero; normal destruction is performed by the separate inherited release protocol.
u32 TrRaiseTool::RemoveRef() {
  if (TrCommandOwner::references) {
    TrGetDirector()->RemoveRef();
    --TrCommandOwner::references;
  }
  return TrCommandOwner::references;
}

// Accept the original view-change notification and translate only view IDs0/1/2 into modes1/2/3.
// Other payload values leave the mode unchanged; unrelated message types are declined without inspecting their payload.
bool TrRaiseTool::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityDataViewChanged)
    return false;
  u32 view_id = u32(message->data1);
  if (view_id < 3)
    mode = view_id + 1;
  return true;
}

// The subscription callback precedes view lookup; an unrecognized current view preserves the live existing mode.
bool TrRaiseTool::Init() {
  TrGetMessageServer()->AddNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  u32 view_id = u32(TrGetCurrentView()->GetCityViewID());
  if (view_id < 3)
    mode = view_id + 1;
  return 1;
}

bool TrRaiseTool::Shutdown() {
  TrGetMessageServer()->RemoveNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  return TrTool::Shutdown();
}

bool TrRaiseTool::GetCursor(u32 *out) {
  *out = kSpRaiseTerrainCursorId;
  return 1;
}

// Retain the city's actual terrain and demolition interfaces before constructing the full-cell extent and activating.
// The extended extent adds one cell in X/Z; activation subsequently replaces only the common inclusive input bounds.
bool TrRaiseTool::Execute() {
  CtCity *city = TrGetCity();
  terrain = ScAddRefNotNull(city->DirtBag());
  demolition = ScAddRefNotNull(city->DemolitionLayer());
  bounds.minimum = {0, 0, 0};
  u32 height = city->CellCountZ(), width = city->CellCountX();
  bounds.maximum = {i32(width << kCtMicroCoordinateShift), i32(height << kCtMicroCoordinateShift), 0};
  extent = bounds;
  extent.maximum.x = i32(u32(extent.maximum.x) + kCtMicroUnitsPerCell);
  extent.maximum.z = i32(u32(extent.maximum.z) + kCtMicroUnitsPerCell);
  return Attach(TrGetCityWindow());
}

// Edits ignore both producer replies; refusal reads the live extra byte after the first feedback callback.
bool TrRaiseTool::ExecuteOnMouseUp(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanRaiseTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (invalid || unaffordable) {
    TrToolFeedback(1, invalid ? 30 : 32, 0, 0, 0);
    if (unknown_flag)
      TrToolFeedback(5, 18, 0, 0, 0);
  } else {
    demolition->DemolishFromDirtChange(mode, &affected, 1, false, &demolition_cost);
    terrain->RaiseTerrain(request, &affected, &terrain_cost, false);
    TrToolFeedback(1, 18, 0, 0, 0);
    TrToolFeedback(5, 18, 0, 0, 0);
  }
  return 1;
}

// Refusal emits one event; success marks the extra byte only after all three feedback callbacks have completed.
bool TrRaiseTool::ExecuteOnMouseDrag(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanRaiseTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (invalid || unaffordable)
    TrToolFeedback(5, 18, 0, 0, 0);
  else {
    demolition->DemolishFromDirtChange(mode, &affected, 1, false, &demolition_cost);
    terrain->RaiseTerrain(request, &affected, &terrain_cost, false);
    TrToolFeedback(1, 18, 0, 0, 0);
    TrToolFeedback(6, 18, 0, 1024, 0);
    TrToolFeedback(5, 18, 0, 1024, 0);
    unknown_flag = 1;
  }
  return 1;
}

bool TrRaiseTool::ExecuteOnMouseMove(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanRaiseTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  return 1;
}

// Any of the three error bytes selects color36; the genuine damage output is present but remains untouched.
bool TrRaiseTool::ExecuteCellDraw(const CtCoord &point, GeBounds *) {
  if (!panning)
    cursor->SetCell(point, invalid || unaffordable || conflict ? 36 : 35);
  return 1;
}

TrLowerTool::TrLowerTool()
    : extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, terrain(nullptr),
      demolition(nullptr), mode(0), unknown_flag(0) {
}

// Unsubscribe the primary message receiver before releasing the retained terrain and demolition interfaces.
// Each field clears after its callback; the command queues then retire before the common drag-tool interfaces.
TrLowerTool::~TrLowerTool() {
  TrGetMessageServer()->RemoveNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  ScRelease(&terrain);
  ScRelease(&demolition);
}

bool TrLowerTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return TrCommand::QueryInterface(iid, out);
}

u32 TrLowerTool::AddRef() {
  TrGetDirector()->AddRef();
  return ++TrCommandOwner::references;
}

u32 TrLowerTool::Release() {
  return TrCommandOwner::Release();
}

// A zero count remains zero; normal destruction is performed by the separate inherited release protocol.
u32 TrLowerTool::RemoveRef() {
  if (TrCommandOwner::references) {
    TrGetDirector()->RemoveRef();
    --TrCommandOwner::references;
  }
  return TrCommandOwner::references;
}

// Accept the original view-change notification and translate only view IDs0/1/2 into modes1/2/3.
// Other payload values leave the mode unchanged; unrelated message types are declined without inspecting their payload.
bool TrLowerTool::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityDataViewChanged)
    return false;
  u32 view_id = u32(message->data1);
  if (view_id < 3)
    mode = view_id + 1;
  return true;
}

// The subscription callback precedes view lookup; an unrecognized current view preserves the live existing mode.
bool TrLowerTool::Init() {
  TrGetMessageServer()->AddNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  u32 view_id = u32(TrGetCurrentView()->GetCityViewID());
  if (view_id < 3)
    mode = view_id + 1;
  return 1;
}

bool TrLowerTool::Shutdown() {
  TrGetMessageServer()->RemoveNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  return TrTool::Shutdown();
}

bool TrLowerTool::GetCursor(u32 *out) {
  *out = kSpLowerTerrainCursorId;
  return 1;
}

// Retain the city's actual terrain and demolition interfaces before constructing the full-cell extent and activating.
// The extended extent adds one cell in X/Z; activation subsequently replaces only the common inclusive input bounds.
bool TrLowerTool::Execute() {
  CtCity *city = TrGetCity();
  terrain = ScAddRefNotNull(city->DirtBag());
  demolition = ScAddRefNotNull(city->DemolitionLayer());
  bounds.minimum = {0, 0, 0};
  u32 height = city->CellCountZ(), width = city->CellCountX();
  bounds.maximum = {i32(width << kCtMicroCoordinateShift), i32(height << kCtMicroCoordinateShift), 0};
  extent = bounds;
  extent.maximum.x = i32(u32(extent.maximum.x) + kCtMicroUnitsPerCell);
  extent.maximum.z = i32(u32(extent.maximum.z) + kCtMicroUnitsPerCell);
  return Attach(TrGetCityWindow());
}

// Edits ignore both producer replies; refusal reads the live extra byte after the first feedback callback.
bool TrLowerTool::ExecuteOnMouseUp(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanLowerTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (invalid || unaffordable) {
    TrToolFeedback(1, invalid ? 30 : 32, 0, 0, 0);
    if (unknown_flag)
      TrToolFeedback(5, 19, 0, 0, 0);
  } else {
    demolition->DemolishFromDirtChange(mode, &affected, 1, false, &demolition_cost);
    terrain->LowerTerrain(request, &affected, &terrain_cost, false);
    TrToolFeedback(1, 19, 0, 0, 0);
    TrToolFeedback(5, 19, 0, 0, 0);
  }
  return 1;
}

// Refusal emits one event; success marks the extra byte only after all three feedback callbacks have completed.
bool TrLowerTool::ExecuteOnMouseDrag(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanLowerTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (invalid || unaffordable)
    TrToolFeedback(5, 19, 0, 0, 0);
  else {
    demolition->DemolishFromDirtChange(mode, &affected, 1, false, &demolition_cost);
    terrain->LowerTerrain(request, &affected, &terrain_cost, false);
    TrToolFeedback(1, 19, 0, 0, 0);
    TrToolFeedback(6, 19, 0, 1024, 0);
    TrToolFeedback(5, 19, 0, 1024, 0);
    unknown_flag = 1;
  }
  return 1;
}

bool TrLowerTool::ExecuteOnMouseMove(const CtCoord &point) {
  u32 x = u32(point.x >> kCtMicroCoordinateShift), z = u32(point.z >> kCtMicroCoordinateShift), terrain_cost = 0,
      demolition_cost = 0;
  GeBounds request = {{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0},
      {i32((x + 1) << kCtMicroCoordinateShift), i32((z + 1) << kCtMicroCoordinateShift), 0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!terrain->CanLowerTerrain(request, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  return 1;
}

// Any of the three error bytes selects color36; the genuine damage output is present but remains untouched.
bool TrLowerTool::ExecuteCellDraw(const CtCoord &point, GeBounds *) {
  if (!panning)
    cursor->SetCell(point, invalid || unaffordable || conflict ? 36 : 35);
  return 1;
}

TrLevelTool::TrLevelTool()
    : extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, terrain(nullptr),
      demolition(nullptr), mode(0) {
}

TrLevelTool::~TrLevelTool() {
  ScRelease(&terrain);
  ScRelease(&demolition);
}

bool TrLevelTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return TrCommand::QueryInterface(iid, out);
}

u32 TrLevelTool::AddRef() {
  TrGetDirector()->AddRef();
  return ++TrCommandOwner::references;
}

u32 TrLevelTool::Release() {
  return TrCommandOwner::Release();
}

// A zero count remains zero; normal destruction is performed by the separate inherited release protocol.
u32 TrLevelTool::RemoveRef() {
  if (TrCommandOwner::references) {
    TrGetDirector()->RemoveRef();
    --TrCommandOwner::references;
  }
  return TrCommandOwner::references;
}

// Accept the original view-change notification and translate only view IDs0/1/2 into modes1/2/3.
// Other payload values leave the mode unchanged; unrelated message types are declined without inspecting their payload.
bool TrLevelTool::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityDataViewChanged)
    return false;
  u32 view_id = u32(message->data1);
  if (view_id < 3)
    mode = view_id + 1;
  return true;
}

// The subscription callback precedes view lookup; an unrecognized current view preserves the live existing mode.
bool TrLevelTool::Init() {
  TrGetMessageServer()->AddNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  u32 view_id = u32(TrGetCurrentView()->GetCityViewID());
  if (view_id < 3)
    mode = view_id + 1;
  return 1;
}

bool TrLevelTool::Shutdown() {
  TrGetMessageServer()->RemoveNotification(static_cast<TrTool *>(this), kWnMsg_CityDataViewChanged);
  return TrTool::Shutdown();
}

bool TrLevelTool::GetCursor(u32 *out) {
  *out = kSpLevelTerrainCursorId;
  return 1;
}

// Retain the city's actual terrain and demolition interfaces before constructing the full-cell extent and activating.
// The extended extent adds one cell in X/Z; activation subsequently replaces only the common inclusive input bounds.
bool TrLevelTool::Execute() {
  CtCity *city = TrGetCity();
  terrain = ScAddRefNotNull(city->DirtBag());
  demolition = ScAddRefNotNull(city->DemolitionLayer());
  bounds.minimum = {0, 0, 0};
  u32 height = city->CellCountZ(), width = city->CellCountX();
  bounds.maximum = {i32(width << kCtMicroCoordinateShift), i32(height << kCtMicroCoordinateShift), 0};
  extent = bounds;
  extent.maximum.x = i32(u32(extent.maximum.x) + kCtMicroUnitsPerCell);
  extent.maximum.z = i32(u32(extent.maximum.z) + kCtMicroUnitsPerCell);
  return Attach(TrGetCityWindow());
}

// Read the anchor terrain height before quoting; apply only affordable edits and then emit one original feedback event.
bool TrLevelTool::ExecuteOnMouseUp(const GeBounds &area) {
  u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift), x2 = u32(area.maximum.x >> kCtMicroCoordinateShift);
  u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift), z2 = u32(area.maximum.z >> kCtMicroCoordinateShift);
  u32 stored_z = u32(selection.minimum.z >> kCtMicroCoordinateShift),
      stored_z2 = u32(selection.maximum.z >> kCtMicroCoordinateShift);
  u32 terrain_cost = 0, demolition_cost = 0;
  GeBounds request = {{i32((x < x2 ? x : x2) << kCtMicroCoordinateShift),
                          i32((stored_z < stored_z2 ? stored_z : stored_z2) << kCtMicroCoordinateShift), 0},
      {i32(((x < x2 ? x2 : x) + 1) << kCtMicroCoordinateShift), i32(((z < z2 ? z2 : z) + 1) << kCtMicroCoordinateShift),
          0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  u8 altitude;
  terrain->GetVertexAltitudeDirt(x, z, &altitude);
  if (!terrain->CanLevelTerrain(request, altitude, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (invalid || unaffordable)
    TrToolFeedback(1, invalid ? 30 : 32, 0, 0, 0);
  else {
    demolition->DemolishFromDirtChange(mode, &affected, 1, false, &demolition_cost);
    terrain->LevelTerrain(request, altitude, &affected, &terrain_cost, false);
    TrToolFeedback(1, 15, 0, 0, 0);
  }
  return 1;
}

// The lower Z edge uses the live stored selection; other horizontal edges and the height anchor use the argument.
bool TrLevelTool::ExecuteOnMouseDrag(const GeBounds &area) {
  u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift), x2 = u32(area.maximum.x >> kCtMicroCoordinateShift);
  u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift), z2 = u32(area.maximum.z >> kCtMicroCoordinateShift);
  u32 stored_z = u32(selection.minimum.z >> kCtMicroCoordinateShift),
      stored_z2 = u32(selection.maximum.z >> kCtMicroCoordinateShift);
  u32 terrain_cost = 0, demolition_cost = 0;
  GeBounds request = {{i32((x < x2 ? x : x2) << kCtMicroCoordinateShift),
                          i32((stored_z < stored_z2 ? stored_z : stored_z2) << kCtMicroCoordinateShift), 0},
      {i32(((x < x2 ? x2 : x) + 1) << kCtMicroCoordinateShift), i32(((z < z2 ? z2 : z) + 1) << kCtMicroCoordinateShift),
          0}};
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  u8 altitude;
  terrain->GetVertexAltitudeDirt(x, z, &altitude);
  if (!terrain->CanLevelTerrain(request, altitude, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  return 1;
}

// Only the upper X/Z edges expand by one cell; both altitude coordinates retain the picked point's altitude.
bool TrLevelTool::ExecuteOnMouseMove(const CtCoord &point) {
  GeBounds request = {point, point};
  request.maximum.x = i32(u32(request.maximum.x) + kCtMicroUnitsPerCell);
  request.maximum.z = i32(u32(request.maximum.z) + kCtMicroUnitsPerCell);
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  u32 terrain_cost = 0, demolition_cost = 0;
  u8 altitude;
  terrain->GetVertexAltitudeDirt(
      u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift), &altitude);
  if (!terrain->CanLevelTerrain(request, altitude, &affected, &terrain_cost) ||
      !demolition->CanDemolishFromDirtChange(mode, &affected, 1, &demolition_cost))
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  return 1;
}

// Any of the three error bytes selects color36; the genuine damage output is present but remains untouched.
bool TrLevelTool::ExecuteRectDraw(const GeBounds &area, GeBounds *) {
  if (!panning)
    cursor->SetRect(area, invalid || unaffordable || conflict ? 36 : 35);
  return 1;
}

TrWaterTool::TrWaterTool()
    : extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, terrain(nullptr),
      demolition(nullptr), zones(nullptr), surface(nullptr), underground_one(nullptr), underground_two(nullptr) {
}

// Release terrain, demolition, zones and the three occupant managers in their actual retained-field order.
// Each field clears after its callback; the command queues then retire before the common drag-tool interfaces.
TrWaterTool::~TrWaterTool() {
  ScRelease(&terrain);
  ScRelease(&demolition);
  ScRelease(&zones);
  ScRelease(&surface);
  ScRelease(&underground_one);
  ScRelease(&underground_two);
}

bool TrWaterTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return TrCommand::QueryInterface(iid, out);
}

u32 TrWaterTool::AddRef() {
  TrGetDirector()->AddRef();
  return ++TrCommandOwner::references;
}

u32 TrWaterTool::Release() {
  return TrCommandOwner::Release();
}

// A zero count remains zero; normal destruction is performed by the separate inherited release protocol.
u32 TrWaterTool::RemoveRef() {
  if (TrCommandOwner::references) {
    TrGetDirector()->RemoveRef();
    --TrCommandOwner::references;
  }
  return TrCommandOwner::references;
}

bool TrWaterTool::GetCursor(u32 *out) {
  *out = kSpPaintWaterCursorId;
  return 1;
}

bool TrWaterTool::IsValidHandlerByView(i32 view_kind, u32) const {
  return view_kind == 0;
}

// Retain terrain, zones, three occupant levels and demolition before constructing the full-cell extent.
// Post the current view flags with the original message before resolving the window and activating the handler.
bool TrWaterTool::Execute() {
  CtCity *city = TrGetCity();
  terrain = ScAddRefNotNull(city->DirtBag());
  zones = ScAddRefNotNull(city->ZoneLayer());
  surface = ScAddRefNotNull(city->SurfaceOccupantManager());
  underground_one = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
  underground_two = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
  demolition = ScAddRefNotNull(city->DemolitionLayer());
  bounds.minimum = {0, 0, 0};
  u32 height = city->CellCountZ(), width = city->CellCountX();
  bounds.maximum = {i32(width << kCtMicroCoordinateShift), i32(height << kCtMicroCoordinateShift), 0};
  extent = bounds;
  extent.maximum.x = i32(u32(extent.maximum.x) + kCtMicroUnitsPerCell);
  extent.maximum.z = i32(u32(extent.maximum.z) + kCtMicroUnitsPerCell);
  u32 view_flags = TrGetCurrentView()->GetCityViewFlags();
  ScMessage message(kWnMsg_SetCityViewParams, 0, view_flags, 1);
  TrGetMessageServer()->MessagePost(message, false);
  return Attach(TrGetCityWindow());
}

// Preserve terrain corner queries, wet/occupant refusal, collection lifetime and quote-before-application ordering.
bool TrWaterTool::ExecuteOnMouseUp(const CtCoord &point) {
  GeBounds request = {point, point}, neighborhood = {point, point};
  request.maximum.x = i32(u32(request.maximum.x) + kCtMicroUnitsPerCell);
  request.maximum.z = i32(u32(request.maximum.z) + kCtMicroUnitsPerCell);
  if (request.minimum.x < extent.minimum.x)
    request.minimum.x = extent.minimum.x;
  if (request.minimum.z < extent.minimum.z)
    request.minimum.z = extent.minimum.z;
  if (request.maximum.x > extent.maximum.x)
    request.maximum.x = extent.maximum.x;
  if (request.maximum.z > extent.maximum.z)
    request.maximum.z = extent.maximum.z;
  request.minimum.altitude = request.maximum.altitude = INT32_MAX;
  neighborhood.minimum.x = i32(u32(neighborhood.minimum.x) - kCtMicroUnitsPerCell);
  neighborhood.minimum.z = i32(u32(neighborhood.minimum.z) - kCtMicroUnitsPerCell);
  neighborhood.maximum.x = i32(u32(neighborhood.maximum.x) + kCtMicroUnitsPerCell);
  neighborhood.maximum.z = i32(u32(neighborhood.maximum.z) + kCtMicroUnitsPerCell);
  if (neighborhood.minimum.x < bounds.minimum.x)
    neighborhood.minimum.x = bounds.minimum.x;
  if (neighborhood.minimum.z < bounds.minimum.z)
    neighborhood.minimum.z = bounds.minimum.z;
  if (neighborhood.maximum.x > bounds.maximum.x)
    neighborhood.maximum.x = bounds.maximum.x;
  if (neighborhood.maximum.z > bounds.maximum.z)
    neighborhood.maximum.z = bounds.maximum.z;
  neighborhood.minimum.altitude = neighborhood.maximum.altitude = INT32_MAX;
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  GeBounds demolition_affected = affected;
  u32 terrain_cost = 0, demolition_cost = 0;
  u32 x = u32(request.minimum.x >> kCtMicroCoordinateShift), z = u32(request.minimum.z >> kCtMicroCoordinateShift);
  u32 x2 = u32(request.maximum.x >> kCtMicroCoordinateShift), z2 = u32(request.maximum.z >> kCtMicroCoordinateShift);
  u8 heights[4];
  terrain->GetVertexAltitudeDirt(x, z, &heights[0]);
  terrain->GetVertexAltitudeDirt(x, z2, &heights[1]);
  terrain->GetVertexAltitudeDirt(x2, z, &heights[2]);
  terrain->GetVertexAltitudeDirt(x2, z2, &heights[3]);
  u32 first_x = u32(neighborhood.minimum.x >> kCtMicroCoordinateShift),
      first_z = u32(neighborhood.minimum.z >> kCtMicroCoordinateShift);
  u32 last_x = u32(neighborhood.maximum.x >> kCtMicroCoordinateShift),
      last_z = u32(neighborhood.maximum.z >> kCtMicroCoordinateShift);
  if (terrain->CellWaterVertCount(x, z) == 4)
    SetStatus(0, true);
  if (!invalid) {
    ScOccupant *occupant = nullptr;
    for (u32 cell_x = first_x; cell_x <= last_x && !invalid; ++cell_x) {
      for (u32 cell_z = first_z; cell_z <= last_z && !invalid; ++cell_z) {
        if (surface->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
          if (!occupant->IsGeomFlag(1024) || !occupant->CanUserRemove())
            SetStatus(0, true);
          occupant->Release();
          occupant = nullptr;
        }
      }
    }
  }
  SpSelection *selection = nullptr;
  if (terrain->CanPlaceWater(request, &affected, &terrain_cost)) {
    if (!demolition->CanDemolishOccupants(
            surface, &neighborhood, nullptr, 1, &demolition_cost, &demolition_affected, &selection))
      SetStatus(0, true);
    selection->Release();
  } else
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  if (!invalid && !unaffordable) {
    demolition->DemolishOccupants(surface, &neighborhood, nullptr, 1, false, &demolition_cost, &demolition_affected);
    for (u32 cell_x = u32(neighborhood.minimum.x >> kCtMicroCoordinateShift);
        cell_x <= u32(neighborhood.maximum.x >> kCtMicroCoordinateShift); ++cell_x) {
      for (u32 cell_z = u32(neighborhood.minimum.z >> kCtMicroCoordinateShift);
          cell_z <= u32(neighborhood.maximum.z >> kCtMicroCoordinateShift); ++cell_z) {
        u8 zone = 0;
        zones->GetValue(cell_x, cell_z, &zone);
      }
    }
    terrain->PlaceWater(request, &affected, &terrain_cost, false);
    TrToolFeedback(1, 20, 0, 0, 0);
  }
  return 1;
}

bool TrWaterTool::ExecuteOnMouseDrag(const CtCoord &point) {
  return TrWaterTool::ExecuteOnMouseUp(point);
}

// Wet or protected-occupant refusal remains sticky while terrain and demolition quotations still run in original order.
bool TrWaterTool::ExecuteOnMouseMove(const CtCoord &point) {
  GeBounds request = {point, point}, neighborhood = {point, point};
  request.maximum.x = i32(u32(request.maximum.x) + kCtMicroUnitsPerCell);
  request.maximum.z = i32(u32(request.maximum.z) + kCtMicroUnitsPerCell);
  if (request.minimum.x < extent.minimum.x)
    request.minimum.x = extent.minimum.x;
  if (request.minimum.z < extent.minimum.z)
    request.minimum.z = extent.minimum.z;
  if (request.maximum.x > extent.maximum.x)
    request.maximum.x = extent.maximum.x;
  if (request.maximum.z > extent.maximum.z)
    request.maximum.z = extent.maximum.z;
  request.minimum.altitude = request.maximum.altitude = INT32_MAX;
  neighborhood.minimum.x = i32(u32(neighborhood.minimum.x) - kCtMicroUnitsPerCell);
  neighborhood.minimum.z = i32(u32(neighborhood.minimum.z) - kCtMicroUnitsPerCell);
  neighborhood.maximum.x = i32(u32(neighborhood.maximum.x) + kCtMicroUnitsPerCell);
  neighborhood.maximum.z = i32(u32(neighborhood.maximum.z) + kCtMicroUnitsPerCell);
  if (neighborhood.minimum.x < bounds.minimum.x)
    neighborhood.minimum.x = bounds.minimum.x;
  if (neighborhood.minimum.z < bounds.minimum.z)
    neighborhood.minimum.z = bounds.minimum.z;
  if (neighborhood.maximum.x > bounds.maximum.x)
    neighborhood.maximum.x = bounds.maximum.x;
  if (neighborhood.maximum.z > bounds.maximum.z)
    neighborhood.maximum.z = bounds.maximum.z;
  neighborhood.minimum.altitude = neighborhood.maximum.altitude = INT32_MAX;
  GeBounds affected = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  GeBounds demolition_affected = affected;
  u32 terrain_cost = 0, demolition_cost = 0;
  u32 x = u32(request.minimum.x >> kCtMicroCoordinateShift), z = u32(request.minimum.z >> kCtMicroCoordinateShift);
  u32 x2 = u32(request.maximum.x >> kCtMicroCoordinateShift), z2 = u32(request.maximum.z >> kCtMicroCoordinateShift);
  u8 heights[4];
  terrain->GetVertexAltitudeDirt(x, z, &heights[0]);
  terrain->GetVertexAltitudeDirt(x, z2, &heights[1]);
  terrain->GetVertexAltitudeDirt(x2, z, &heights[2]);
  terrain->GetVertexAltitudeDirt(x2, z2, &heights[3]);
  u32 first_x = u32(neighborhood.minimum.x >> kCtMicroCoordinateShift),
      first_z = u32(neighborhood.minimum.z >> kCtMicroCoordinateShift);
  u32 last_x = u32(neighborhood.maximum.x >> kCtMicroCoordinateShift),
      last_z = u32(neighborhood.maximum.z >> kCtMicroCoordinateShift);
  if (terrain->CellWaterVertCount(x, z) == 4)
    SetStatus(0, true);
  if (!invalid) {
    ScOccupant *occupant = nullptr;
    for (u32 cell_x = first_x; cell_x <= last_x && !invalid; ++cell_x) {
      for (u32 cell_z = first_z; cell_z <= last_z && !invalid; ++cell_z) {
        if (surface->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
          if (!occupant->IsGeomFlag(1024) || !occupant->CanUserRemove())
            SetStatus(0, true);
          occupant->Release();
          occupant = nullptr;
        }
      }
    }
  }
  SpSelection *selection = nullptr;
  if (terrain->CanPlaceWater(request, &affected, &terrain_cost)) {
    if (!demolition->CanDemolishOccupants(
            surface, &neighborhood, nullptr, 1, &demolition_cost, &demolition_affected, &selection))
      SetStatus(0, true);
    selection->Release();
  } else
    SetStatus(0, true);
  cost = i32(terrain_cost + demolition_cost);
  CheckBudget();
  return 1;
}

// Any of the three error bytes selects color36; the genuine damage output is present but remains untouched.
bool TrWaterTool::ExecuteCellDraw(const CtCoord &point, GeBounds *) {
  if (!panning)
    cursor->SetCell(point, invalid || unaffordable || conflict ? 36 : 35);
  return 1;
}

// Allocate the actual command selected by the original class IDs, then publish only its requested interface.
// Unknown classes clear the genuine output; a failed interface query destroys the zero-reference command directly.
bool TrToolFactory::CreateCommand(u32 class_id, u32 iid, void **out) {
  TrTool *tool;
  switch (class_id) {
  case kTrWaterToolClassId:
    tool = new TrWaterTool;
    break;
  case kTrRaiseToolAliasClassId:
  case kTrRaiseToolClassId:
    tool = new TrRaiseTool;
    break;
  case kTrLevelToolAliasClassId:
  case kTrLevelToolClassId:
    tool = new TrLevelTool;
    break;
  case kTrLowerToolAliasClassId:
  case kTrLowerToolClassId:
    tool = new TrLowerTool;
    break;
  default:
    *out = nullptr;
    return false;
  }
  if (tool->QueryInterface(iid, out))
    return true;
  delete tool;
  return false;
}

// Return the current view borrowed from the actual city window, preserving the original missing-window null result.
// Neither the window nor its current view receives an additional reference from this module helper.
SpViewInterface *TrGetCurrentView() {
  SpCityWindowView *window = TrGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

TrTool::TrTool()
    : manager(nullptr), window(nullptr), view(nullptr), cursor(nullptr), budget(nullptr),
      bounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, panning(0), cursor_visible(0),
      edge_scroll(0), cost_visible(0), cost(0), invalid(0), unaffordable(0), conflict(0), retiring(0) {
}

// Release budget, cursor map, current view and window in order, clearing each field after its callback.
// The window manager is borrowed and the destructor does not shut down the active tool or reset unrelated state.
TrTool::~TrTool() {
  ScRelease(&budget);
  ScRelease(&cursor);
  ScRelease(&view);
  ScRelease(&window);
}

bool TrTool::DoMessage(ScMessage *) {
  return false;
}

bool TrTool::Init() {
  return 1;
}

bool TrTool::Shutdown() {
  window->SetEdgeScroll(false);
  window->SetCostDisplay(false, 0, 0);
  edge_scroll = 0;
  return 1;
}

bool TrTool::OnKeyDown(u32 key, u32 flags) {
  return window->DefOnKeyDown(key, flags);
}

bool TrTool::OnKeyUp(u32 key, u32 flags) {
  return window->DefOnKeyUp(key, flags);
}

// Retirement callbacks cannot replace the captured default-handler reply.
bool TrTool::OnMouseDownL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseDownL(x, y, flags);
  Dettach(TrGetCityWindow());
  return result;
}

// Window mouse-scroll coordinates retain their actual signed WORD input width.
bool TrTool::OnMouseDownR(i32 x, i32 y, u32) {
  panning = 1;
  window->SetMouseScroll(i16(x), i16(y), true);
  edge_scroll = 0;
  window->SetEdgeScroll(false);
  cost_visible = 0;
  window->SetCostDisplay(false, 0, 0);
  return 1;
}

// Return the original captured default-handler byte after those callbacks finish.
bool TrTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseUpL(x, y, flags);
  Dettach(TrGetCityWindow());
  return result;
}

// The cost-visible byte is set without issuing an extra cost-display call.
bool TrTool::OnMouseUpR(i32 x, i32 y, u32) {
  panning = 0;
  window->SetMouseScroll(i16(x), i16(y), false);
  edge_scroll = 1;
  window->SetEdgeScroll(true);
  cost_visible = 1;
  Dettach(TrGetCityWindow());
  return 1;
}

// Default motion runs before deferred retirement, preserving its reply through the later window callbacks.
bool TrTool::OnMouseMove(i32 x, i32 y, u32 flags) {
  if (!edge_scroll) {
    window->SetCellCursorSize(1, 1);
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  bool result = window->DefOnMouseMove(x, y, flags);
  Dettach(TrGetCityWindow());
  return result;
}

bool TrTool::GetCursor(u32 *out) {
  *out = kSpCenterCursorId;
  return 1;
}

// Only active panning/cost states issue window calls; the original flags survive and the cost word is cleared last.
void TrTool::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
}

bool TrTool::IsValidHandlerByView(i32, u32) const {
  return 1;
}

// Return false without retirement; otherwise preserve the original Boolean handler-removal reply.
// Preserve its genuine unused window input and clear visible panning/cursor state before handler retirement.
bool TrTool::Dettach(SpCityWindowView *) {
  if (!retiring)
    return false;
  if (panning) {
    panning = 0;
    window->SetMouseScroll(0, 0, false);
  }
  if (cursor_visible) {
    cursor_visible = 0;
    window->SetCellCursorVisible(false);
  }
  return window->SetActiveHandler(nullptr);
}

// Clear the three original status bytes in order. The authentic ClearStatus declaration returns void.
// Deferred retirement and the displayed cost are separate state and survive this phase.
void TrTool::ClearStatus() {
  invalid = 0;
  unaffordable = 0;
  conflict = 0;
}

// Set only the selected original status flag. Other selector values leave all three flags unchanged.
// The installed commands inline these stores; no separate DLL entry is asserted for this source method.
void TrTool::SetStatus(i32 status, bool enabled) {
  if (status == 0)
    invalid = enabled;
  else if (status == 1)
    unaffordable = enabled;
  else if (status == 2)
    conflict = enabled;
}

void TrTool::UpdateStatusDisplay() {
  i32 amount = cost;
  unaffordable = amount != 0 && i64(amount) > budget->GetTotalFunds();
  if (!panning)
    window->SetCostDisplay(true, amount, unaffordable != 0);
}

// The live cost is read after the budget callback and compared as a signed DWORD against the signed64 funds value.
void TrTool::CheckBudget() {
  i64 funds = budget->GetTotalFunds();
  i32 amount = cost;
  unaffordable = amount != 0 && i64(amount) > funds;
}

bool TrTool::ContainsPoint(i32 x, i32 z, i32) const {
  return x >= bounds.minimum.x && z >= bounds.minimum.z && x <= bounds.maximum.x && z <= bounds.maximum.z;
}

// Retained fields and inclusive city bounds are ready before external handler callbacks; cursor setup still follows
// refusal.
bool TrTool::Attach(SpCityWindowView *input) {
  manager = TrGetWindowManager();
  window = input;
  input->AddRef();
  view = ScAddRefNotNull(window->GetCurrentView());
  cursor = ScAddRefNotNull(window->GetCellCursorMap());
  CtCity *city = TrGetCity();
  bounds.minimum = {0, 0, 0};
  u32 last_z = city->CellCountZ() - 1, width = city->CellCountX();
  bounds.maximum.x = i32((width << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell);
  bounds.maximum.z = i32(last_z << kCtMicroCoordinateShift);
  bounds.maximum.altitude = 0;
  budget = ScAddRefNotNull(city->BudgetLayer());
  bool accepted = false;
  void *object;
  if (QueryInterface(IID_SpViewHandler, &object)) {
    SpViewHandler *handler = static_cast<SpViewHandler *>(object);
    accepted = window->SetActiveHandler(handler);
    handler->Release();
    object = nullptr;
  }
  cursor->SetDefaultColor(0, 35);
  u32 cursor_id;
  GetCursor(&cursor_id);
  window->SetActiveCursor(cursor_id);
  return accepted;
}

TrDragTool::TrDragTool()
    : dragging(0), current{INT32_MAX, INT32_MAX, INT32_MAX}, previous{INT32_MAX, INT32_MAX, INT32_MAX} {
}

// A successful pick shows the cursor even outside the tool bounds; only an accepted coordinate begins the stroke.
bool TrDragTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  bool result = false;
  if (view->PerformPick(x, y, &point, false)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (ContainsPoint(point.x, point.z, point.altitude)) {
      current = point;
      ClearStatus();
      result = ExecuteOnMouseDown(current);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteCellDraw(current, &damage);
    }
  }
  return result;
}

// Modifier bit0 skips finishing, but cursor/display retirement and the live deferred-retirement phase still run.
bool TrDragTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (ContainsPoint(point.x, point.z, point.altitude))
        current = point;
    }
    if (ContainsPoint(current.x, current.z, current.altitude)) {
      ClearStatus();
      ExecuteOnMouseUp(current);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteCellDraw(current, &damage);
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  if (edge_scroll)
    cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  Dettach(TrGetCityWindow());
  return 1;
}

// Only active non-panning drags flush queued input; panning can update the picked point without editing terrain.
bool TrDragTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (cursor_visible) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
  } else if (!cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false) && ContainsPoint(point.x, point.z, point.altitude)) {
    GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
    if (ContainsPoint(point.x, point.z, point.altitude)) {
      current = point;
      if (!panning) {
        ClearStatus();
        if (dragging) {
          ExecuteOnMouseDrag(current);
          manager->FlushInputMessageQueue();
        } else
          ExecuteOnMouseMove(current);
        UpdateStatusDisplay();
        ExecuteCellDraw(current, &damage);
      }
    }
  }
  return 1;
}

bool TrDragTool::ExecuteOnMouseDown(const CtCoord &) {
  return 1;
}

bool TrDragTool::ExecuteOnMouseUp(const CtCoord &) {
  return 1;
}

bool TrDragTool::ExecuteOnMouseDrag(const CtCoord &) {
  return 1;
}

bool TrDragTool::ExecuteOnMouseMove(const CtCoord &) {
  return 1;
}

// The damage output argument is genuinely present but untouched by this shared default implementation.
bool TrDragTool::ExecuteCellDraw(const CtCoord &point, GeBounds *) {
  if (edge_scroll)
    cursor->SetCell(point, 35);
  return 1;
}

void TrDragTool::ExecuteOnResetState() {
}

TrRectTool::TrRectTool()
    : dragging(0), selection{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}},
      previous_selection{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}} {
}

// A valid pick activates and shows the cursor before bounds acceptance; the callback receives the local picked point.
bool TrRectTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  if (view->PerformPick(x, y, &point, false)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (ContainsPoint(point.x, point.z, point.altitude)) {
      selection.minimum = point;
      ClearStatus();
      ExecuteOnMouseDown(point);
      UpdateStatusDisplay();
    }
  }
  return 1;
}

// Cursor clearing is unconditional; only the end X/Z words become minus one, retaining the original end altitude.
bool TrRectTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (ContainsPoint(selection.minimum.x, selection.minimum.z, selection.minimum.altitude) &&
          ContainsPoint(point.x, point.z, point.altitude))
        selection.maximum = point;
    }
    if (!panning && ContainsPoint(selection.minimum.x, selection.minimum.z, selection.minimum.altitude) &&
        ContainsPoint(selection.maximum.x, selection.maximum.z, selection.maximum.altitude)) {
      if (edge_scroll)
        ExecuteRectDraw(selection, &damage);
      ClearStatus();
      ExecuteOnMouseUp(selection);
      UpdateStatusDisplay();
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  selection.maximum.x = -1;
  selection.maximum.z = -1;
  dragging = 0;
  Dettach(TrGetCityWindow());
  return 1;
}

// Unlike the point drag receiver, an active rectangle flushes queued input after cost display and preview.
bool TrRectTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (cursor_visible) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
  } else if (!cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false) && ContainsPoint(point.x, point.z, point.altitude)) {
    if (dragging) {
      if (cursor->InBounds(selection.minimum) && cursor->InBounds(point) && !panning) {
        selection.maximum = point;
        ClearStatus();
        ExecuteOnMouseDrag(selection);
        UpdateStatusDisplay();
        if (edge_scroll)
          ExecuteRectDraw(selection, &damage);
        manager->FlushInputMessageQueue();
      }
    } else if (cursor->InBounds(point) && !panning) {
      selection.maximum = point;
      selection.minimum = point;
      ClearStatus();
      ExecuteOnMouseMove(point);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteRectDraw(selection, &damage);
    }
  }
  return 1;
}

// Both altitudes, the active-drag byte and previous selection survive this private effect phase.
void TrRectTool::OnResetState() {
  TrTool::OnResetState();
  selection.maximum.z = INT32_MAX;
  selection.maximum.x = INT32_MAX;
  selection.minimum.z = INT32_MAX;
  selection.minimum.x = INT32_MAX;
}

bool TrRectTool::ExecuteOnMouseDown(const CtCoord &) {
  return 1;
}

bool TrRectTool::ExecuteOnMouseUp(const GeBounds &) {
  return 1;
}

bool TrRectTool::ExecuteOnMouseDrag(const GeBounds &) {
  return 1;
}

bool TrRectTool::ExecuteOnMouseMove(const CtCoord &) {
  return 1;
}

// The separate damage output is genuinely present and remains untouched by the shared default.
bool TrRectTool::ExecuteRectDraw(const GeBounds &area, GeBounds *) {
  if (edge_scroll)
    cursor->SetRect(area, 35);
  return 1;
}

void TrRectTool::ExecuteOnResetState() {
}
