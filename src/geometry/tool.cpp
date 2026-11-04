#include "geometry/tool.h"

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "ui/api.h"
#include "window/api.h"

GeAreaTool::GeAreaTool()
    : active(0), start{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
                     {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      current{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      region{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      occupants(nullptr) {
}

GeAreaTool::~GeAreaTool() {
}

// Start edge scrolling before inspecting the picked cell, snapping both initial footprints through separate occupant
// calls.
// A missing occupant produces two one-point footprints, whose union is published before the actual press callback.
bool GeAreaTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (view->PerformPick(x, y, &picked, true)) {
    active = 1;
    edgeScroll = 1;
    window->SetEdgeScroll(true);
    if (bounds.CoordInBoundsXZ(picked)) {
      ScOccupant *occupant = nullptr;
      if (occupants->GetOccupantAtCell(
              u32(picked.x >> kCtMicroCoordinateShift), u32(picked.z >> kCtMicroCoordinateShift), &occupant)) {
        occupant->GetBounds(&start);
        occupant->GetBounds(&current);
        occupant->Release();
      } else {
        current.maximum = picked;
        current.minimum = picked;
        start.maximum = picked;
        start.minimum = picked;
      }
      region.UnionXZ(start, current);
      return BeginAt(&start);
    }
  }
  return 0;
}

// Refresh the final footprint on a successful pick, then commit the union only when both complete footprints lie in
// bounds.
// Preserve reset, commit, visible painting and cost-display order before unconditional edge, map and display cleanup.
bool GeAreaTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &picked, true)) {
      active = 0;
      if (bounds.CoordInBoundsXZ(picked)) {
        ScOccupant *occupant = nullptr;
        if (occupants->GetOccupantAtCell(
                u32(picked.x >> kCtMicroCoordinateShift), u32(picked.z >> kCtMicroCoordinateShift), &occupant)) {
          occupant->GetBounds(&current);
          occupant->Release();
        } else {
          current.maximum = picked;
          current.minimum = picked;
          if (start.minimum.x == kCtUnknownCoordinate) {
            start.maximum = picked;
            start.minimum = picked;
          }
        }
      }
    }
    if (bounds.CoordInBoundsXZ(start.minimum) && bounds.CoordInBoundsXZ(start.maximum) &&
        bounds.CoordInBoundsXZ(current.minimum) && bounds.CoordInBoundsXZ(current.maximum)) {
      region.UnionXZ(start, current);
      ResetCost();
      Commit(&region);
      if (cursorVisible)
        PaintCursor(&region, &affected);
      ShowCost();
    }
  }
  edgeScroll = 0;
  window->SetEdgeScroll(false);
  cursorMap->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  active = 0;
  FinishRelease(GeGetCityWindow());
  return 1;
}

// Track occupant footprints under the current pointer, retaining the press footprint only while the active gesture
// lasts.
// Preview a valid union or hover footprint with the original display, paint and input-flush ordering.
bool GeAreaTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!cursorVisible) {
    window->SetCellCursorVisible(true);
    cursorVisible = 1;
  }
  if (active) {
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
  if (view->PerformPick(x, y, &picked, true) && bounds.CoordInBoundsXZ(picked)) {
    if (active) {
      ScOccupant *occupant = nullptr;
      if (occupants->GetOccupantAtCell(
              u32(picked.x >> kCtMicroCoordinateShift), u32(picked.z >> kCtMicroCoordinateShift), &occupant)) {
        occupant->GetBounds(&current);
        occupant->Release();
      } else {
        current.maximum = picked;
        current.minimum = picked;
        if (start.minimum.x == kCtUnknownCoordinate) {
          start.maximum = picked;
          start.minimum = picked;
        }
      }
      region.UnionXZ(start, current);
    } else {
      ScOccupant *occupant = nullptr;
      if (occupants->GetOccupantAtCell(
              u32(picked.x >> kCtMicroCoordinateShift), u32(picked.z >> kCtMicroCoordinateShift), &occupant)) {
        occupant->GetBounds(&start);
        occupant->GetBounds(&current);
        occupant->Release();
      } else {
        current.maximum = picked;
        current.minimum = picked;
        start.maximum = picked;
        start.minimum = picked;
      }
      region = start;
    }
    if (!mouseScroll) {
      if (active) {
        if (bounds.CoordInBoundsXZ(region.minimum) && bounds.CoordInBoundsXZ(region.maximum)) {
          ResetCost();
          PreviewDrag(&region);
          ShowCost();
          if (cursorVisible)
            PaintCursor(&region, &affected);
          windows->FlushInputMessageQueue();
        }
      } else if (bounds.CoordInBoundsXZ(start.minimum) && bounds.CoordInBoundsXZ(start.maximum)) {
        ResetCost();
        PreviewHover(&start);
        ShowCost();
        if (cursorVisible)
          PaintCursor(&start, &affected);
      }
    }
  }
  return 1;
}

// Cancel reached displays, invalidate only the start footprint's X/Z words and copy it into both later bounds.
// Preserve the original altitude words and handler flags while clearing the stored price.
void GeAreaTool::OnResetState() {
  if (mouseScroll)
    window->SetMouseScroll(0, 0, false);
  if (costVisible)
    window->SetCostDisplay(false, 0, 0);
  start.maximum.z = kCtUnknownCoordinate;
  start.maximum.x = kCtUnknownCoordinate;
  start.minimum.z = kCtUnknownCoordinate;
  start.minimum.x = kCtUnknownCoordinate;
  current = start;
  region = start;
  cost = 0;
}

u8 GeAreaTool::BeginAt(const GeBounds *) {
  return 1;
}

u8 GeAreaTool::Commit(const GeBounds *) {
  return 1;
}

u8 GeAreaTool::PreviewDrag(const GeBounds *) {
  return 1;
}

u8 GeAreaTool::PreviewHover(const GeBounds *) {
  return 1;
}

// Draw the supplied footprint in color35 only when the cursor is visible, leaving the separate bounds output untouched.
u8 GeAreaTool::PaintCursor(const GeBounds *area, GeBounds *) {
  if (cursorVisible)
    cursorMap->SetRect(*area, 35);
  return 1;
}

u8 GeAreaTool::FinishRelease(SpCityWindowView *host) {
  return DetachIfRequested(host);
}

GeBuildingTool::GeBuildingTool(const RsKey &key, bool singleUse, bool freeOfCharge)
    : buildings(nullptr), key(key), freeOfCharge(freeOfCharge), singleUse(singleUse), widthMinusOne(0),
      depthMinusOne(0), cursorId(0), soundId(0) {
}

GeBuildingTool::~GeBuildingTool() {
  ScRelease(&buildings);
}

bool GeBuildingTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return GeCommand::QueryInterface(iid, out);
}

u32 GeBuildingTool::AddRef() {
  return GeCommand::AddRef();
}

u32 GeBuildingTool::Release() {
  return GeCommand::Release();
}

// Retain the current building layer, derive preview dimensions and cursor/sound identifiers, then attach the handler.
// Repeated execution replaces the publication without releasing its previous hold, matching the original ownership.
bool GeBuildingTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  buildings = ScAddRefNotNull(city->BuildingLayer());
  void *publication = nullptr;
  if (GeGetOccupantAttribCache()->GetGenericAttrib(key, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX() - 1);
    depthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ() - 1);
    RsKey resource;
    static_cast<GeAttrib *>(publication)->GetPlopCursorKey(&resource);
    cursorId = resource.instance;
    static_cast<GeAttrib *>(publication)->GetSoundKeyA(&resource);
    soundId = resource.instance;
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  u32 flags = GeGetView()->GetCityViewFlags();
  ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
  GeGetMessageServer()->MessagePost(message, false);
  return AttachHost(GeGetCityWindow());
}

bool GeBuildingTool::GetCursor(u32 *out) {
  *out = cursorId ? cursorId : kSpRewardsCursorId;
  return 1;
}

bool GeBuildingTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return viewKind == 0;
}

// Refresh the placement refusal byte and price before checking current funds; beginning a gesture does not place anything.
u8 GeBuildingTool::BeginAt(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  return 1;
}

u8 GeBuildingTool::PreviewDrag(const CtCoord *position) {
  return GeBuildingTool::BeginAt(position);
}

u8 GeBuildingTool::PreviewPoint(const CtCoord *position) {
  return GeBuildingTool::BeginAt(position);
}

// Recheck placement and funds, apply only when both allow it, then request refusal or positional success feedback.
// The single-use detach byte is set after successful placement even when the application audio query refuses.
u8 GeBuildingTool::Commit(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds)
    invalid = !buildings->PlaceBuilding(*position, key, &affected, &price, freeOfCharge);
  if (invalid || insufficientFunds) {
    u32 event = invalid ? 30 : 32;
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
    }
  } else {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayCellEffect, u32(position->x >> kCtMicroCoordinateShift),
          u32(position->z >> kCtMicroCoordinateShift), 0, 0);
    }
    if (singleUse)
      detachAfterInput = 1;
  }
  return 1;
}

// Draw the footprint in warning or normal color unless mouse scrolling suppresses it, then publish its full bounds.
u8 GeBuildingTool::PaintCursor(const CtCoord *position, GeBounds *affected) {
  GeBounds footprint = {*position, *position};
  footprint.maximum.x = i32(u32(footprint.maximum.x) + (u32(widthMinusOne) << kCtMicroCoordinateShift));
  footprint.maximum.z = i32(u32(footprint.maximum.z) + (u32(depthMinusOne) << kCtMicroCoordinateShift));
  if (!mouseScroll)
    cursorMap->SetRect(footprint, invalid || insufficientFunds || field58 ? 36 : 35);
  *affected = footprint;
  return 1;
}

GePlumbingTool::GePlumbingTool(const RsKey &key, bool singleUse, bool freeOfCharge)
    : GeBuildingTool(key, singleUse, freeOfCharge) {
}

GePlumbingTool::~GePlumbingTool() {
}

// Retain the building layer and load preview resources while preserving this tool's original view-message ordering.
bool GePlumbingTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  buildings = ScAddRefNotNull(city->BuildingLayer());
  void *publication = nullptr;
  if (GeGetOccupantAttribCache()->GetGenericAttrib(key, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX() - 1);
    depthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ() - 1);
    RsKey resource;
    static_cast<GeAttrib *>(publication)->GetPlopCursorKey(&resource);
    cursorId = resource.instance;
    static_cast<GeAttrib *>(publication)->GetSoundKeyA(&resource);
    soundId = resource.instance;
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  i32 viewKind = GeGetView()->GetCityViewID();
  if (viewKind != 0 && viewKind != 2) {
    u32 flags = GeGetView()->GetCityViewFlags();
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    GeGetMessageServer()->MessagePost(message, false);
  }
  return AttachHost(GeGetCityWindow());
}

bool GePlumbingTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return viewKind == 0 || viewKind == 2;
}

// Refresh this layer's placement permission and price before checking funds; preserve its original constant reply.
u8 GePlumbingTool::BeginAt(const CtCoord *position) {
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  u32 price = 0;
  invalid = !buildings->CanPlacePlumbingBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  return 1;
}

u8 GePlumbingTool::PreviewDrag(const CtCoord *position) {
  return GePlumbingTool::BeginAt(position);
}

u8 GePlumbingTool::PreviewPoint(const CtCoord *position) {
  return GePlumbingTool::BeginAt(position);
}

// Recheck this layer's placement and funds before applying, then preserve refusal and positional success feedback.
// Successful single-use placement requests detachment after audio handling, including a refused application query.
u8 GePlumbingTool::Commit(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlacePlumbingBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds)
    invalid = !buildings->PlacePlumbingBuilding(*position, key, &affected, &price, freeOfCharge);
  if (invalid || insufficientFunds) {
    u32 event = invalid ? 30 : 32;
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
    }
  } else {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayCellEffect, u32(position->x >> kCtMicroCoordinateShift),
          u32(position->z >> kCtMicroCoordinateShift), 0, 0);
    }
    if (singleUse)
      detachAfterInput = 1;
  }
  return 1;
}

GeSubwayTool::GeSubwayTool(const RsKey &key, bool singleUse, bool freeOfCharge)
    : GeBuildingTool(key, singleUse, freeOfCharge) {
}

GeSubwayTool::~GeSubwayTool() {
}

// Retain the building layer and load preview resources while preserving this tool's original view-message ordering.
bool GeSubwayTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  buildings = ScAddRefNotNull(city->BuildingLayer());
  void *publication = nullptr;
  if (GeGetOccupantAttribCache()->GetGenericAttrib(key, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX() - 1);
    depthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ() - 1);
    RsKey resource;
    static_cast<GeAttrib *>(publication)->GetPlopCursorKey(&resource);
    cursorId = resource.instance;
    static_cast<GeAttrib *>(publication)->GetSoundKeyA(&resource);
    soundId = resource.instance;
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  i32 viewKind = GeGetView()->GetCityViewID();
  if (u32(viewKind) >= 2) {
    u32 flags = GeGetView()->GetCityViewFlags();
    ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
    GeGetMessageServer()->MessagePost(message, false);
  }
  return AttachHost(GeGetCityWindow());
}

bool GeSubwayTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return u32(viewKind) < 2;
}

// Refresh this layer's placement permission and price before checking funds; preserve its original constant reply.
u8 GeSubwayTool::BeginAt(const CtCoord *position) {
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  u32 price = 0;
  invalid = !buildings->CanPlaceSubwayBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  return 1;
}

u8 GeSubwayTool::PreviewDrag(const CtCoord *position) {
  return GeSubwayTool::BeginAt(position);
}

u8 GeSubwayTool::PreviewPoint(const CtCoord *position) {
  return GeSubwayTool::BeginAt(position);
}

// Recheck this layer's placement and funds before applying, then preserve refusal and positional success feedback.
// Successful single-use placement requests detachment after audio handling, including a refused application query.
u8 GeSubwayTool::Commit(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceSubwayBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds)
    invalid = !buildings->PlaceSubwayBuilding(*position, key, &affected, &price, freeOfCharge);
  if (invalid || insufficientFunds) {
    u32 event = invalid ? 30 : 32;
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
    }
  } else {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayCellEffect, u32(position->x >> kCtMicroCoordinateShift),
          u32(position->z >> kCtMicroCoordinateShift), 0, 0);
    }
    if (singleUse)
      detachAfterInput = 1;
  }
  return 1;
}

GeShoreTool::GeShoreTool(const RsKey &key, bool singleUse, bool freeOfCharge)
    : GeBuildingTool(key, singleUse, freeOfCharge) {
}

GeShoreTool::~GeShoreTool() {
}

// Retain the building layer and load preview resources while preserving this tool's original view-message ordering.
bool GeShoreTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  buildings = ScAddRefNotNull(city->BuildingLayer());
  u32 flags = GeGetView()->GetCityViewFlags();
  ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
  GeGetMessageServer()->MessagePost(message, false);
  void *publication = nullptr;
  if (GeGetOccupantAttribCache()->GetGenericAttrib(key, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX() - 1);
    depthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ() - 1);
    RsKey resource;
    static_cast<GeAttrib *>(publication)->GetPlopCursorKey(&resource);
    cursorId = resource.instance;
    static_cast<GeAttrib *>(publication)->GetSoundKeyA(&resource);
    soundId = resource.instance;
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  return AttachHost(GeGetCityWindow());
}

// Refresh this layer's placement permission and price before checking funds; preserve its original constant reply.
u8 GeShoreTool::BeginAt(const CtCoord *position) {
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  u32 price = 0;
  invalid = !buildings->CanPlaceShorelineBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  return 0;
}

u8 GeShoreTool::PreviewDrag(const CtCoord *position) {
  return GeShoreTool::BeginAt(position);
}

u8 GeShoreTool::PreviewPoint(const CtCoord *position) {
  return GeShoreTool::BeginAt(position);
}

// Recheck this layer's placement and funds before applying, then preserve refusal and positional success feedback.
// Successful single-use placement requests detachment after audio handling, including a refused application query.
u8 GeShoreTool::Commit(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceShorelineBuilding(*position, key, &affected, &price);
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds)
    invalid = !buildings->PlaceShorlineBuilding(*position, key, &affected, &price, freeOfCharge);
  if (invalid || insufficientFunds) {
    u32 event = invalid ? 30 : 32;
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
    }
  } else {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayCellEffect, u32(position->x >> kCtMicroCoordinateShift),
          u32(position->z >> kCtMicroCoordinateShift), 0, 0);
    }
    if (singleUse)
      detachAfterInput = 1;
  }
  return 1;
}

GeDemolitionTool::GeDemolitionTool()
    : surface(nullptr), undergroundOne(nullptr), undergroundTwo(nullptr), demolition(nullptr), field328(0) {
}

// Release live demolition, surface and underground services in order before command and handler base destruction.
GeDemolitionTool::~GeDemolitionTool() {
  ScRelease(&demolition);
  ScRelease(&surface);
  ScRelease(&undergroundOne);
  ScRelease(&undergroundTwo);
}

bool GeDemolitionTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return GeCommand::QueryInterface(iid, out);
}

u32 GeDemolitionTool::AddRef() {
  return GeCommand::AddRef();
}

u32 GeDemolitionTool::Release() {
  return GeCommand::Release();
}

// Select the borrowed current manager from the actual view-change payload; unrelated messages leave it untouched.
bool GeDemolitionTool::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityDataViewChanged)
    return false;
  u32 kind = u32(message->data1);
  if (kind == 2)
    occupants = undergroundOne;
  else if (kind == 1)
    occupants = undergroundTwo;
  else
    occupants = surface;
  return true;
}

// Subscribe before querying the current view, then borrow the corresponding already-retained manager.
bool GeDemolitionTool::Init() {
  GeGetMessageServer()->AddNotification(this, kWnMsg_CityDataViewChanged);
  i32 kind = GeGetView()->GetCityViewID();
  if (kind == 2)
    occupants = undergroundOne;
  else if (kind == 1)
    occupants = undergroundTwo;
  else
    occupants = surface;
  return 1;
}

bool GeDemolitionTool::Shutdown() {
  GeGetMessageServer()->RemoveNotification(this, kWnMsg_CityDataViewChanged);
  return GeTool::Shutdown();
}

// Acquire the city demolition and occupant services in order before reading the live development-seven price.
// Attach the real area handler afterward and restore palette0/color35 only when activation succeeds.
bool GeDemolitionTool::Execute() {
  CtCity *city = GeGetCity();
  demolition = ScAddRefNotNull(city->DemolitionLayer());
  surface = ScAddRefNotNull(city->SurfaceOccupantManager());
  undergroundOne = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
  undergroundTwo = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
  unitCost = u32(GeGetCity()->BudgetLayer()->GetCost(kEcCostDemolish));
  bool result = AttachHost(GeGetCityWindow());
  if (result)
    cursorMap->SetDefaultColor(0, 35);
  return result;
}

bool GeDemolitionTool::GetCursor(u32 *out) {
  *out = kSpDemolishClearCursorId;
  return 1;
}

// Collect a normalized removal area and check its partial price before applying only when permission and funds allow.
// Feedback distinguishes invalid30, insufficient32 and applied5 while preserving live application-release ordering.
u8 GeDemolitionTool::Commit(const GeBounds *area) {
  u32 price = 0;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  GeBounds normalized = *area;
  normalized.NormalizeXZ();
  SpSelection *selection = nullptr;
  if (!demolition->CanDemolishOccupants(occupants, &normalized, nullptr, 1, &price, &affected, &selection))
    invalid = 1;
  selection->Release();
  selection = nullptr;
  cost = i32(price);
  CheckCost();
  u32 event;
  if (invalid)
    event = 30;
  else if (insufficientFunds)
    event = 32;
  else {
    demolition->DemolishOccupants(occupants, &normalized, nullptr, 1, false, &price, &affected);
    event = 5;
  }
  void *publication = nullptr;
  if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
    ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
    static_cast<ApGameInterface *>(publication)->Release();
    audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
  return 1;
}

// Normalize the active-area copy before collecting permission and price, preserving any preexisting refusal byte.
u8 GeDemolitionTool::PreviewDrag(const GeBounds *area) {
  u32 price = 0;
  GeBounds normalized = *area;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  normalized.NormalizeXZ();
  SpSelection *selection = nullptr;
  if (!demolition->CanDemolishOccupants(occupants, &normalized, nullptr, 1, &price, &affected, &selection))
    invalid = 1;
  selection->Release();
  selection = nullptr;
  cost = i32(price);
  CheckCost();
  return 1;
}

// Collect the hover footprint as supplied without the drag path's normalization, then refresh stored price and funds.
u8 GeDemolitionTool::PreviewHover(const GeBounds *area) {
  u32 price = 0;
  GeBounds queried = *area;
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  SpSelection *selection = nullptr;
  if (!demolition->CanDemolishOccupants(occupants, &queried, nullptr, 1, &price, &affected, &selection))
    invalid = 1;
  selection->Release();
  selection = nullptr;
  cost = i32(price);
  CheckCost();
  return 1;
}

// Draw the supplied area unless mouse scrolling suppresses it; any preview refusal byte selects warning color36.
u8 GeDemolitionTool::PaintCursor(const GeBounds *area, GeBounds *) {
  if (!mouseScroll)
    cursorMap->SetRect(*area, invalid || insufficientFunds || field58 ? 36 : 35);
  return 1;
}

GeHoverTool::GeHoverTool()
    : active(0), point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, field76(INT32_MAX),
      field80(INT32_MAX), field84(INT32_MAX) {
}

GeHoverTool::~GeHoverTool() {
}

// Store a successful pick before testing bounds, then deliver the local picked point to the press callback.
// Failed picking clears the active byte and cursor map regardless of the cursor-visible byte.
bool GeHoverTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (view->PerformPick(x, y, &picked, true)) {
    point = picked;
    active = 1;
    if (bounds.CoordInBoundsXZ(picked))
      return BeginAt(&picked);
  } else {
    active = 0;
    cursorMap->Clear(true);
  }
  return 0;
}

// Unless suppressed, update an in-bounds point even after a refused pick, paint it when visible, then commit it.
// Always clear the map and cost display before the virtual release callback and synchronous completion notification.
bool GeHoverTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &picked, true))
      active = 0;
    if (bounds.CoordInBoundsXZ(picked))
      point = picked;
    if (bounds.CoordInBoundsXZ(point)) {
      if (cursorVisible)
        PaintCursor(&point, &affected);
      Commit(&point);
    }
  }
  active = 0;
  cursorMap->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  FinishRelease(GeGetCityWindow());
  ScMessage message(kWnMsg_ProcessDeferredCityHandlerReset, 0, 0, 0);
  GeGetMessageServer()->MessageSend(message);
  return 1;
}

// Make the cursor visible, update mouse scrolling, then retain an in-bounds pick before previewing its local copy.
// Preserve the preview byte across the live visible-cursor check and paint callback; scrolling or failed picking
// returns0.
bool GeHoverTool::OnMouseMove(i32 x, i32 y, u32) {
  u8 result = 0;
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!cursorVisible) {
    window->SetCellCursorVisible(true);
    cursorVisible = 1;
  }
  if (active && mouseScroll) {
    edgeScroll = 0;
    window->SetEdgeScroll(false);
  }
  if (mouseScroll)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &picked, true) && bounds.CoordInBoundsXZ(picked)) {
    point = picked;
    active = 1;
    if (!mouseScroll) {
      result = PreviewPoint(&picked);
      if (cursorVisible)
        PaintCursor(&point, &affected);
    }
  }
  return result;
}

// Cancel reached mouse scrolling and cost display without clearing the stored cost or changing the handler flags.
void GeHoverTool::OnResetState() {
  if (mouseScroll)
    window->SetMouseScroll(0, 0, false);
  if (costVisible)
    window->SetCostDisplay(false, 0, 0);
}

u8 GeHoverTool::BeginAt(const CtCoord *) {
  return 1;
}

u8 GeHoverTool::Commit(const CtCoord *) {
  return 1;
}

u8 GeHoverTool::PreviewPoint(const CtCoord *) {
  return 1;
}

// Draw a visible one-point rectangle in color35; the original leaves the separate affected-bounds output untouched.
u8 GeHoverTool::PaintCursor(const CtCoord *position, GeBounds *) {
  if (cursorVisible) {
    GeBounds footprint{*position, *position};
    cursorMap->SetRect(footprint, 35);
  }
  return 1;
}

u8 GeHoverTool::FinishRelease(SpCityWindowView *host) {
  return DetachIfRequested(host);
}

GeLocationTool::GeLocationTool() {
}

GeLocationTool::~GeLocationTool() {
}

bool GeLocationTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return GeCommand::QueryInterface(iid, out);
}

u32 GeLocationTool::AddRef() {
  return GeCommand::AddRef();
}

u32 GeLocationTool::Release() {
  return GeCommand::Release();
}

// Require a current city, then attach the actual hover handler to the current city window without acquiring another
// layer.
bool GeLocationTool::Execute() {
  if (!GeGetCity())
    return false;
  return AttachHost(GeGetCityWindow());
}

bool GeLocationTool::GetCursor(u32 *out) {
  *out = kSpQueryCursorId;
  return 1;
}

// Build the command's retained point and two unsigned-zero inputs before sending the synchronous begin notification.
// Release command and argument holds in order before posting completion; failed creation or input append still
// returns1.
u8 GeLocationTool::Commit(const CtCoord *position) {
  void *factory = nullptr;
  GeGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &factory);
  void *command;
  if (static_cast<ScCommandFactory *>(factory)->GetCommand(
          kUiQueryCommandClassId, IID_UiCommandInterface, &command)) {
    ScValue *pointValue = new ScValue;
    pointValue->AddRef();
    ScValue *firstZero = new ScValue;
    firstZero->AddRef();
    ScValue *secondZero = new ScValue;
    secondZero->AddRef();
    ScPoint location{position->x >> kCtMicroCoordinateShift, position->z >> kCtMicroCoordinateShift};
    pointValue->RefPoint(&location);
    bool ready = static_cast<UiCommandInterface *>(command)->PushBackInput(pointValue);
    if (ready) {
      firstZero->SetValUint32(0);
      ready = static_cast<UiCommandInterface *>(command)->PushBackInput(firstZero);
    }
    if (ready) {
      secondZero->SetValUint32(0);
      ready = static_cast<UiCommandInterface *>(command)->PushBackInput(secondZero);
    }
    if (ready) {
      ScMessage begin(kGeMsg_QueryCommandStarting, 0, 0, 0);
      GeGetMessageServer()->MessageSend(begin);
      static_cast<UiCommandInterface *>(command)->Execute();
    }
    static_cast<UiCommandInterface *>(command)->Release();
    pointValue->Release();
    firstZero->Release();
    secondZero->Release();
    if (ready) {
      ScMessage complete(kGeMsg_QueryCommandCompleted, 0, 0, 0);
      GeGetMessageServer()->MessagePost(complete, false);
    }
  }
  if (factory)
    static_cast<ScCommandFactory *>(factory)->Release();
  return 1;
}

u8 GeLocationTool::PaintCursor(const CtCoord *position, GeBounds *) {
  cursorMap->SetCell(*position, 35);
  return 1;
}

// Construct the actual handler and command identities, then copy four separate keys and clear combined dimensions.
GeMultiTool::GeMultiTool(
    const RsKey &first, const RsKey &second, const RsKey &third, const RsKey &fourth, bool singleUse, bool freeOfCharge)
    : buildings(nullptr), first(first), second(second), third(third), fourth(fourth), freeOfCharge(freeOfCharge),
      singleUse(singleUse), widthMinusOne(0), depthMinusOne(0), cursorId(0), soundId(0) {
}

GeMultiTool::~GeMultiTool() {
  ScRelease(&buildings);
}

bool GeMultiTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return GeCommand::QueryInterface(iid, out);
}

u32 GeMultiTool::AddRef() {
  return GeCommand::AddRef();
}

u32 GeMultiTool::Release() {
  return GeCommand::Release();
}

// Retain the building layer, combine byte dimensions from the first three keys and load cursor/sound from the first.
// Query each definition through a fresh cache accessor, then post view flags and attach the handler.
// Repeated execution replaces the publication without releasing its previous hold, matching the original ownership.
bool GeMultiTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  buildings = ScAddRefNotNull(city->BuildingLayer());
  void *publication = nullptr;
  if (GeGetOccupantAttribCache()->GetGenericAttrib(first, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX() - 1);
    depthMinusOne = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ() - 1);
    RsKey resource;
    static_cast<GeAttrib *>(publication)->GetPlopCursorKey(&resource);
    cursorId = resource.instance;
    static_cast<GeAttrib *>(publication)->GetSoundKeyA(&resource);
    soundId = resource.instance;
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  if (GeGetOccupantAttribCache()->GetGenericAttrib(second, IID_GeAttrib, &publication)) {
    widthMinusOne = u8(widthMinusOne + static_cast<GeAttrib *>(publication)->SizeInCellsX());
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  if (GeGetOccupantAttribCache()->GetGenericAttrib(third, IID_GeAttrib, &publication)) {
    depthMinusOne = u8(depthMinusOne + static_cast<GeAttrib *>(publication)->SizeInCellsZ());
    static_cast<GeAttrib *>(publication)->Release();
    publication = nullptr;
  }
  u32 flags = GeGetView()->GetCityViewFlags();
  ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
  GeGetMessageServer()->MessagePost(message, false);
  return AttachHost(GeGetCityWindow());
}

bool GeMultiTool::GetCursor(u32 *out) {
  *out = cursorId ? cursorId : kSpRewardsCursorId;
  return 1;
}

bool GeMultiTool::IsValidHandlerByView(i32 viewKind, u32) const {
  return viewKind == 0;
}

// Refresh the placement refusal byte and price before checking current funds; beginning a gesture does not place anything.
u8 GeMultiTool::BeginAt(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceMultiBuilding(*position, first, second, third, fourth, &affected, &price);
  cost = i32(price);
  CheckCost();
  return 1;
}

u8 GeMultiTool::PreviewDrag(const CtCoord *position) {
  return GeMultiTool::BeginAt(position);
}

u8 GeMultiTool::PreviewPoint(const CtCoord *position) {
  return GeMultiTool::BeginAt(position);
}

// Recheck placement and funds, apply only when both allow it, then request refusal or positional success feedback.
// The single-use detach byte is set after successful placement even when the application audio query refuses.
u8 GeMultiTool::Commit(const CtCoord *position) {
  u32 price = 0;
  GeBounds affected = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  invalid = !buildings->CanPlaceMultiBuilding(*position, first, second, third, fourth, &affected, &price);
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds)
    invalid = !buildings->PlaceMultiBuilding(*position, first, second, third, fourth, &affected, &price, freeOfCharge);
  if (invalid || insufficientFunds) {
    u32 event = invalid ? 30 : 32;
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, event, 0, 0, 0);
    }
  } else {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayCellEffect, u32(position->x >> kCtMicroCoordinateShift),
          u32(position->z >> kCtMicroCoordinateShift), 0, 0);
    }
    if (singleUse)
      detachAfterInput = 1;
  }
  return 1;
}

// Draw the combined footprint using only the placement refusal byte for its color, then publish the full bounds.
// Funds and field58 do not affect this original cursor color; mouse scrolling still suppresses drawing.
u8 GeMultiTool::PaintCursor(const CtCoord *position, GeBounds *affected) {
  GeBounds footprint = {*position, *position};
  footprint.maximum.x = i32(u32(footprint.maximum.x) + (u32(widthMinusOne) << kCtMicroCoordinateShift));
  footprint.maximum.z = i32(u32(footprint.maximum.z) + (u32(depthMinusOne) << kCtMicroCoordinateShift));
  if (!mouseScroll)
    cursorMap->SetRect(footprint, invalid ? 36 : 35);
  *affected = footprint;
  return 1;
}

GeTool::GeTool()
    : windows(nullptr), window(nullptr), view(nullptr), cursorMap(nullptr), budget(nullptr),
      bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      mouseScroll(0), edgeScroll(0), cursorVisible(0), costVisible(0), cost(0), invalid(0), insufficientFunds(0),
      field58(0), detachAfterInput(0) {
}

// The cached window manager is borrowed and is not released by this owner.
GeTool::~GeTool() {
  ScRelease(&budget);
  ScRelease(&cursorMap);
  ScRelease(&view);
  ScRelease(&window);
}

bool GeTool::DoMessage(ScMessage *) {
  return false;
}

bool GeTool::Init() {
  return 1;
}

bool GeTool::Shutdown() {
  window->SetCellCursorVisible(false);
  window->SetCostDisplay(false, 0, 0);
  cursorVisible = 0;
  return 1;
}

bool GeTool::OnKeyDown(u32 key, u32 flags) {
  return window->DefOnKeyDown(key, flags);
}

bool GeTool::OnKeyUp(u32 key, u32 flags) {
  return window->DefOnKeyUp(key, flags);
}

// Capture the default left-down reply before the live global city-window lookup and optional detachment callbacks.
bool GeTool::OnMouseDownL(i32 x, i32 y, u32 flags) {
  u8 result = window->DefOnMouseDownL(x, y, flags);
  DetachIfRequested(GeGetCityWindow());
  return result;
}

// Begin mouse scrolling before hiding the selection cursor and cost display, retaining the original field/store order.
bool GeTool::OnMouseDownR(i32 x, i32 y, u32) {
  mouseScroll = 1;
  window->SetMouseScroll(i16(x), i16(y), true);
  cursorVisible = 0;
  window->SetCellCursorVisible(false);
  costVisible = 0;
  window->SetCostDisplay(false, 0, 0);
  return 1;
}

// Forward left-up first and preserve its reply across a later optional host transition.
bool GeTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  u8 result = window->DefOnMouseUpL(x, y, flags);
  DetachIfRequested(GeGetCityWindow());
  return result;
}

// End mouse scrolling and restore the cell cursor before publishing the cost-visible state and optional detachment.
bool GeTool::OnMouseUpR(i32 x, i32 y, u32) {
  mouseScroll = 0;
  window->SetMouseScroll(i16(x), i16(y), false);
  cursorVisible = 1;
  window->SetCellCursorVisible(true);
  costVisible = 1;
  DetachIfRequested(GeGetCityWindow());
  return 1;
}

bool GeTool::OnMouseMove(i32 x, i32 y, u32 flags) {
  if (!cursorVisible) {
    window->SetCellCursorSize(1, 1);
    window->SetCellCursorVisible(true);
    cursorVisible = 1;
  }
  u8 result = window->DefOnMouseMove(x, y, flags);
  DetachIfRequested(GeGetCityWindow());
  return result;
}

bool GeTool::GetCursor(u32 *out) {
  *out = kSpCenterCursorId;
  return 1;
}

// Cancel reached scrolling/display operations and reset the current cost without inventing an ordinary reply value.
void GeTool::OnResetState() {
  if (mouseScroll)
    window->SetMouseScroll(0, 0, false);
  if (costVisible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
}

bool GeTool::IsValidHandlerByView(i32, u32) const {
  return 1;
}

// Retain the actual host, its live view/cursor and the city's budget before querying and activating this handler.
// Cursor palette and identifier updates follow activation even when activation refuses the published handler.
bool GeTool::AttachHost(SpCityWindowView *host) {
  windows = GeGetWindowManager();
  window = ScAddRefNotNull(host);
  view = ScAddRefNotNull(window->GetCurrentView());
  cursorMap = ScAddRefNotNull(window->GetCellCursorMap());
  CtCity *city = GeGetCity();
  bounds.minimum = {0, 0, 0};
  u32 depth = city->CellCountZ() - 1, width = city->CellCountX();
  bounds.maximum.z = i32(depth << kCtMicroCoordinateShift);
  bounds.maximum.x = i32((width << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell);
  bounds.maximum.altitude = 0;
  budget = ScAddRefNotNull(city->BudgetLayer());
  bool result = false;
  void *publication;
  if (QueryInterface(IID_SpViewHandler, &publication)) {
    result = window->SetActiveHandler(static_cast<SpViewHandler *>(publication));
    static_cast<SpViewHandler *>(publication)->Release();
    publication = nullptr;
  }
  cursorMap->SetDefaultColor(0, 35);
  u32 cursor;
  GetCursor(&cursor);
  window->SetActiveCursor(cursor);
  return result;
}

// Detach only when requested, clearing each active scrolling flag before its callback and finally removing the handler.
// Keep the original unused city-window argument and its caller-side global lookup separate from the retained host.
bool GeTool::DetachIfRequested(SpCityWindowView *) {
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

void GeTool::ResetCost() {
  invalid = insufficientFunds = field58 = 0;
}

// Compare a captured signed32 cost to signed64 funds only when nonzero, then show it when mouse scrolling is inactive.
// Reload the live insufficient-funds flag and scrolling state after the budget callback.
void GeTool::ShowCost() {
  i32 value = cost;
  insufficientFunds = value && i64(value) > budget->GetTotalFunds();
  if (!mouseScroll)
    window->SetCostDisplay(true, value, insufficientFunds != 0);
}

// Query funds before reading the current signed cost and update only the insufficient-funds byte.
void GeTool::CheckCost() {
  i64 funds = budget->GetTotalFunds();
  i32 value = cost;
  insufficientFunds = value && i64(value) > funds;
}

GeDragTool::GeDragTool()
    : dragging(0), point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, field76(INT32_MAX),
      field80(INT32_MAX), field84(INT32_MAX) {
}

GeDragTool::~GeDragTool() {
}

bool GeDragTool::Init() {
  return GeTool::Init();
}

bool GeDragTool::Shutdown() {
  return GeTool::Shutdown();
}

// Pick the press point before starting gesture and edge-scroll state, then invoke the actual point callback in bounds.
// Preserve the callback outcome and repaint only when the cursor remains visible after the cost-display callback.
bool GeDragTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  u8 result = 0;
  if (view->PerformPick(x, y, &picked, false)) {
    dragging = edgeScroll = 1;
    window->SetEdgeScroll(true);
    if (bounds.CoordInBoundsXZ(picked)) {
      point = picked;
      ResetCost();
      result = BeginAt(&point);
      ShowCost();
      if (cursorVisible)
        PaintCursor(&point, &affected);
    }
  }
  return result;
}

// Commit the retained current point unless the supplied flag's low bit suppresses execution, then end edge scrolling.
// Clear the cell map only when visible and preserve the final global-window lookup before optional detachment.
bool GeDragTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &picked, false)) {
      dragging = 0;
      if (bounds.CoordInBoundsXZ(picked))
        point = picked;
    }
    if (bounds.CoordInBoundsXZ(point)) {
      ResetCost();
      Commit(&point);
      ShowCost();
      if (cursorVisible)
        PaintCursor(&point, &affected);
    }
  }
  edgeScroll = 0;
  window->SetEdgeScroll(false);
  if (cursorVisible)
    cursorMap->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  DetachIfRequested(GeGetCityWindow());
  return 1;
}

// Update live scrolling state before picking and previewing the current point; drag preview flushes queued input.
// Repaint through the concrete cursor callback after cost display without adding another visible-cursor gate.
bool GeDragTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
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
  if (view->PerformPick(x, y, &picked, false) && bounds.CoordInBoundsXZ(picked)) {
    GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
    if (bounds.CoordInBoundsXZ(picked)) {
      point = picked;
      if (!mouseScroll) {
        ResetCost();
        if (dragging) {
          PreviewDrag(&point);
          windows->FlushInputMessageQueue();
        } else
          PreviewPoint(&point);
        ShowCost();
        PaintCursor(&point, &affected);
      }
    }
  }
  return 1;
}

u8 GeDragTool::BeginAt(const CtCoord *) {
  return 1;
}

u8 GeDragTool::Commit(const CtCoord *) {
  return 1;
}

u8 GeDragTool::PreviewDrag(const CtCoord *) {
  return 1;
}

u8 GeDragTool::PreviewPoint(const CtCoord *) {
  return 1;
}

// Draw the visible point cursor in palette color35; the generic implementation leaves affected bounds untouched.
u8 GeDragTool::PaintCursor(const CtCoord *position, GeBounds *) {
  if (cursorVisible)
    cursorMap->SetCell(*position, 35);
  return 1;
}
