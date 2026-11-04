#include "geometry/grid.h"
#include <string.h>
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_building_data.h"
#include "geometry/ge_types.h"
#include "geometry/selection.h"
#include "geometry/utilities.h"
#include "network/nw_records.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/api.h"
#include "ui/api.h"

// Snapshot occupied cells around the clipped footprint in west, south, east and north order. Suppress only consecutive
// identical occupants across the whole perimeter, then unlock before filtering and retaining published outputs.
bool GeLayer::GetNeighborsOfOccupant(
    ScOccupant *occupant, u32 capacity, ScOccupant **out, u32 *count, bool (*test)(ScOccupant *occupant)) {
  *count = 0;
  if ((!capacity && out) || !occupant)
    return false;
  GeCellRect rectangle;
  occupant->GetBoundingRectInCellCoords(&rectangle);
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  u32 clipX = maximumX, clipZ = maximumZ;
  if (left > clipX)
    left = clipX;
  if (top > clipZ)
    top = clipZ;
  if (right > clipX)
    right = clipX;
  if (bottom > clipZ)
    bottom = clipZ;
  ScArray<ScOccupant *> candidates;
  ScOccupant *previous = nullptr;
  gridLock->Lock();
  // Resolve a perimeter cell without clearing the previous pointer across empty gaps or side boundaries.
  auto capture = [&](u32 x, u32 z) {
    u8 flags = cellFlags->columns[x][z];
    if (!(flags & 1))
      return;
    ScOccupant *neighbor;
    if (flags & 2) {
      u32 anchor = cells->columns[x][z].anchor;
      neighbor = cells->columns[anchor & 0xffff][anchor >> 16].occupant;
    } else
      neighbor = cells->columns[x][z].occupant;
    if (neighbor != previous)
      *candidates.Append() = neighbor;
    previous = neighbor;
  };
  if (left) {
    u32 z = top;
    do {
      capture(left - 1, z);
      ++z;
    } while (z <= bottom);
  }
  if (bottom < maximumZ) {
    u32 x = left;
    do {
      capture(x, bottom + 1);
      ++x;
    } while (x <= right);
  }
  if (right < maximumX) {
    u32 z = top;
    do {
      capture(right + 1, z);
      ++z;
    } while (z <= bottom);
  }
  if (top) {
    u32 x = left;
    do {
      capture(x, top - 1);
      ++x;
    } while (x <= right);
  }
  gridLock->Unlock();
  for (u32 i = 0; i < candidates.size; ++i) {
    ScOccupant *neighbor = candidates.data[i];
    if (test && !test(neighbor))
      continue;
    if (!out) {
      ++*count;
      continue;
    }
    out[*count] = neighbor;
    ++*count;
    neighbor->AddRef();
    if (*count >= capacity)
      return true;
  }
  return *count != 0;
}

// Visit occupied cells in descending signed X/Z order, reloading the Z maximum and cell flags after callbacks. The
// current public removal call does not reproduce the original private virtual dispatch.
void GeLayer::RemoveAllOccupants() {
  for (i32 x = i32(maximumX); x >= 0; --x)
    for (i32 z = i32(maximumZ); z >= 0; --z)
      if (cellFlags->columns[x][z] & 1)
        RemoveOccupantAtCell(u32(x), u32(z));
}

// Notify the live receiver range in reverse registration order, then send the changed-occupant message. Receiver and
// message-send results do not reject the change; callbacks may update the sender and final message identifier.
bool GeLayer::OccupantChanged(ScOccupant *occupant, u32 change) {
  usize count = receiversBegin ? usize(receiversEnd - receiversBegin) : 0;
  while (count)
    receiversBegin[--count]->OccupantChanged(senderId, occupant, i32(change), nullptr);
  ScMessage message{changeMessage, reinterpret_cast<uptr>(occupant), change, 0};
  GeGetMessageServer()->MessageSend(message);
  return true;
}

// Retain the occupant while publishing membership and location, then fill its root and redirected footprint under the
// grid lock. Notify receivers and the message service before selecting a unique visible animation-list entry.
bool GeLayer::InsertOccupantAtCell(u32 x, u32 z, ScOccupant *occupant) {
  if (!IsInBoundsCell(x, z) || !occupant)
    return false;
  occupant->AddRef();
  GeCellRect bounds{x, z, occupant->SizeInCellsX() + x - 1, occupant->SizeInCellsZ() + z - 1};
  occupant->AddRef();
  occupant->SetIsInCityFlag(GetOccLayer());
  occupant->SetLocationInCellCoords(x, z);
  gridLock->Lock();
  if (!IsVacantCell(bounds)) {
    gridLock->Unlock();
    occupant->ClearIsInCityFlag(GetOccLayer());
    occupant->Release();
    occupant->Release();
    return false;
  }
  for (u32 column = bounds.left; column <= bounds.right; ++column) {
    for (u32 row = bounds.top; row <= bounds.bottom; ++row) {
      if (column == bounds.left && row == bounds.top) {
        cellFlags->columns[column][row] = 1;
        cells->columns[column][row].occupant = occupant;
      } else {
        cellFlags->columns[column][row] = 3;
        cells->columns[column][row].anchor = u16(bounds.left) | (u32(u16(bounds.top)) << 16);
      }
    }
  }
  gridLock->Unlock();
  for (i32 i = receiversBegin ? i32(receiversEnd - receiversBegin) - 1 : -1; i >= 0; --i)
    receiversBegin[i]->OccupantInserted(senderId, occupant);
  u8 major = occupant->MajorType();
  ScMessage message{insertMessage, reinterpret_cast<uptr>(occupant), major, 0};
  GeGetMessageServer()->MessageSend(message);
  if (field88 && visible && lastZoom >= 2) {
    bool animated = animationTest->Test(occupant);
    animationLock->Lock();
    GeWorkingSet *set = &working[0];
    usize count = set->begin ? usize(set->end - set->begin) : 0;
    if ((count <= u32(GeAnimatedOccupantLimit - 2) || GeAnimatedOccupantLimit == 1) && animated &&
        i32(bounds.right << kCtMicroCoordinateShift) >= window.minimum.x &&
        i32(bounds.left << kCtMicroCoordinateShift) <= window.maximum.x &&
        i32(bounds.bottom << kCtMicroCoordinateShift) >= window.minimum.z &&
        i32(bounds.top << kCtMicroCoordinateShift) <= window.maximum.z) {
      ScOccupant **found = set->begin;
      while (found != set->end && *found != occupant)
        ++found;
      if (found == set->end) {
        GeWorkingAppend(set, occupant);
        animationLock->Unlock();
        occupant->AnimStart();
      } else
        animationLock->Unlock();
    } else
      animationLock->Unlock();
  }
  occupant->Release();
  return true;
}

// Restore F630's effects using the real manager, iterator, locks and borrowed working ranges. This native phase makes
// no return-signature parity claim.
void GeLayer::ReconcileAnimations(const GeBounds *bounds, u8 force) {
  if (!field88 || !visible || lastZoom < 2) {
    animationLock->Lock();
    bool populated = working[0].begin != working[0].end;
    animationLock->Unlock();
    if (populated)
      ClearAnimatedOccupantList(true);
    return;
  }
  if (force) {
    animationLock->Lock();
    working[4].end = working[4].begin;
    for (ScOccupant **entry = working[0].begin; entry != working[0].end; ++entry)
      GeWorkingAppend(&working[4], *entry);
    animationLock->Unlock();
    for (ScOccupant **entry = working[4].begin; entry != working[4].end; ++entry)
      (*entry)->AnimStop();
  }
  animationLock->Lock();
  working[1].end = working[1].begin;
  working[2].end = working[2].begin;
  working[3].end = working[3].begin;
  u32 gathered = 0;
  GeBounds query = *bounds;
  iterator->Init(&query, animationTest);
  for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next()) {
    if (gathered > 200)
      break;
    ++gathered;
    GeWorkingAppend(&working[1], occupant);
  }
  iterator->Shutdown();
  usize previous = GeWorkingCount(working[0]);
  for (usize i = 0; i < previous; ++i) {
    ScOccupant **found = working[1].begin;
    while (found != working[1].end && *found != working[0].begin[i])
      ++found;
    if (found != working[1].end) {
      GeWorkingAppend(&working[2], working[0].begin[i]);
      for (ScOccupant **entry = found + 1; entry != working[1].end; ++entry)
        entry[-1] = *entry;
      --working[1].end;
    }
  }
  i32 remaining = i32(GeAnimatedOccupantLimit - u32(GeWorkingCount(working[2])));
  usize candidates = GeWorkingCount(working[1]);
  if (remaining > 0 && candidates) {
    if (candidates > u32(remaining)) {
      do {
        u32 chosen = random->Next(u32(candidates));
        GeWorkingAppend(&working[2], working[1].begin[chosen]);
        working[1].begin[chosen] = working[1].begin[candidates - 1];
        --candidates;
        --remaining;
      } while (remaining);
    } else
      for (usize i = 0; i < candidates; ++i)
        GeWorkingAppend(&working[2], working[1].begin[i]);
  }
  previous = GeWorkingCount(working[0]);
  for (usize i = 0; i < previous; ++i) {
    ScOccupant **found = working[2].begin;
    while (found != working[2].end && *found != working[0].begin[i])
      ++found;
    if (found == working[2].end)
      GeWorkingAppend(&working[3], working[0].begin[i]);
  }
  GeWorkingAssign(&working[0], working[2]);
  usize selected = GeWorkingCount(working[2]);
  for (usize i = 0; i < selected; ++i)
    working[2].begin[i]->AnimStart();
  usize exited = GeWorkingCount(working[3]);
  for (usize i = 0; i < exited; ++i)
    working[3].begin[i]->AnimStop();
  animationLock->Unlock();
}

// Resolve redirected cells and clear the complete occupant footprint before notifying receivers and the message server.
// Remove the first animated membership and refill outside the lock, then stop, clear city membership and release.
void GeLayer::RemoveOccupantAtCell(u32 x, u32 z) {
  if (x > maximumX || z > maximumZ)
    return;
  gridLock->Lock();
  u8 flags = cellFlags->columns[x][z];
  if (!(flags & 1)) {
    gridLock->Unlock();
    return;
  }
  ScOccupant *occupant;
  if (flags & 2) {
    u32 anchor = cells->columns[x][z].anchor;
    occupant = cells->columns[u16(anchor)][u16(anchor >> 16)].occupant;
  } else
    occupant = cells->columns[x][z].occupant;
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  for (u32 column = bounds.left; column <= bounds.right; ++column) {
    for (u32 row = bounds.top; row <= bounds.bottom; ++row) {
      cellFlags->columns[column][row] = 0;
      cells->columns[column][row].anchor = 0;
    }
  }
  gridLock->Unlock();
  for (i32 i = receiversBegin ? i32(receiversEnd - receiversBegin) - 1 : -1; i >= 0; --i)
    receiversBegin[i]->OccupantRemoved(senderId, occupant);
  u8 major = occupant->MajorType();
  ScMessage message{removeMessage, reinterpret_cast<uptr>(occupant), major, 0};
  GeGetMessageServer()->MessageSend(message);
  animationLock->Lock();
  ScOccupant **found = working[0].begin;
  while (found != working[0].end && *found != occupant)
    ++found;
  bool removed = found != working[0].end;
  if (removed) {
    for (ScOccupant **entry = found + 1; entry != working[0].end; ++entry)
      entry[-1] = *entry;
    --working[0].end;
  }
  if (removed && field88 && visible && lastZoom >= 2 &&
      (GeWorkingCount(working[0]) <= u32(GeAnimatedOccupantLimit - 2) || GeAnimatedOccupantLimit == 1)) {
    GeBounds query = window;
    animationLock->Unlock();
    ReconcileAnimations(&query, 0);
  } else
    animationLock->Unlock();
  if (removed)
    occupant->AnimStop();
  occupant->ClearIsInCityFlag(GetOccLayer());
  occupant->Release();
}

// Deliver the elapsed DWORD to each live animated occupant while retaining the original animation lock.
// Retail cISC3AnimationTickable proves the original void DoAnimationTick contract.
void GeLayer::DoAnimationTick(u32 elapsed) {
  animationLock->Lock();
  for (ScOccupant **entry = working[0].begin; entry != working[0].end; ++entry)
    (*entry)->Tick(elapsed);
  animationLock->Unlock();
}

void GeLayer::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 100;
  *interval = 6;
}

// Apply live camera/filter and animation-preference messages before publishing new camera indices. Initial camera
// refresh redispatches virtually, and tick-manager registration retains the real secondary tickable identity.
bool GeLayer::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_CameraFocusChanged: {
    ScMessage forwarded = *message;
    i32 zoom = i32(u32(forwarded.data1)), rotation = i32(u32(forwarded.data2));
    GeGetView()->GetZoomAndRotation(&zoom, &rotation);
    forwarded.data1 = u32(zoom);
    forwarded.data2 = u32(rotation);
    forwarded.type = kWnMsg_CameraChanging;
    return DoMessage(&forwarded);
  }
  case kWnMsg_CameraChanging: {
    i32 zoom = i32(u32(message->data1)), rotation = i32(u32(message->data2));
    SpViewInterface *view = GeGetView();
    GeAnimationTest *test = animationTest;
    test->zeroZoneEnabled = (view->GetCityViewFlags() & 64) != 0;
    test->otherZoneEnabled = (view->GetCityViewFlags() & 32) != 0;
    if (!test->power) {
      test->power = GeGetCity()->PowerLayer();
      test->power->AddRef();
      test->zones = GeGetCity()->ZoneLayer();
      test->zones->AddRef();
    }
    field8C = u32(view->GetCityViewID());
    visible = field8C == 0 && occupantLayer == 1;
    if (!visible) {
      ClearAnimatedOccupantList(true);
      break;
    }
    ScRect tiles, pixels;
    view->GetCameraHint(&tiles, &pixels);
    window.minimum.x = i32(u32(tiles.left) << kCtMicroCoordinateShift);
    window.minimum.z = i32(u32(tiles.top) << kCtMicroCoordinateShift);
    window.maximum.x = i32(u32(tiles.right) << kCtMicroCoordinateShift);
    window.maximum.z = i32(u32(tiles.bottom) << kCtMicroCoordinateShift);
    if (zoom < 2)
      ClearAnimatedOccupantList(true);
    else {
      static const f32 scales[5] = {1, 1, 1.1f, 1.25f, 1.5f};
      f64 scale = scales[zoom];
      if (scale != 1) {
        f64 extra = scale - 1;
        u32 width = u32(window.maximum.x) - u32(window.minimum.x);
        i32 height = i32(u32(window.maximum.z) - u32(window.minimum.z));
        f32 xMargin = f32(f64(width) * extra * 0.5);
        f64 zMargin = extra * f64(height) * 0.5;
        i32 value = i32(u32(i64(f64(window.minimum.x) - xMargin)));
        window.minimum.x = value < 0 ? 0 : value;
        value = i32(u32(i64(f64(window.minimum.z) - zMargin)));
        window.minimum.z = value < 0 ? 0 : value;
        value = i32(u32(i64(f64(window.maximum.x) + xMargin)));
        i32 maximum = i32(maximumX << kCtMicroCoordinateShift);
        window.maximum.x = value > maximum ? maximum : value;
        value = i32(u32(i64(f64(window.maximum.z) + zMargin)));
        maximum = i32(maximumZ << kCtMicroCoordinateShift);
        window.maximum.z = value > maximum ? maximum : value;
      }
      if (field88 && visible)
        ReconcileAnimations(&window, u8(zoom != lastZoom || rotation != lastRotation));
    }
    lastZoom = zoom;
    lastRotation = rotation;
    break;
  }
  case kWnMsg_AnimationTickManagerReady:
    GeGetTickManager()->AddTickableObject(static_cast<SpTickable *>(this));
    break;
  case kApMsgPreferencesChanged: {
    u8 previous = field88;
    if (occupantLayer == 1) {
      u32 animations;
      const u8 *video = reinterpret_cast<const u8 *>(reinterpret_cast<ApPreferences *>(message->data1)->VideoPrefs());
      memcpy(&animations, video + 8, sizeof(animations));
      field88 = animations != 0;
    } else
      field88 = 0;
    if (previous != field88) {
      if (field88 && visible) {
        if (!field8C)
          ReconcileAnimations(&window, 0);
      } else
        ClearAnimatedOccupantList(true);
    }
    break;
  }
  }
  return true;
}

// Construct the actual occupant manager and return its primary manager identity without acquiring a reference.
ScUnknown *GeCreateLayer() {
  return static_cast<ScOccupantManager *>(new GeLayer);
}

static ScLock GeOccupantLock;

GeOccupant::GeOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

GeOccupant::~GeOccupant() {
  if (attributes)
    GeOccupant::Shutdown();
}

// Expose the four interfaces sharing the original receiver. A successful query acquires one reference.
bool GeOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, GeOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, GeOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, GeOccupant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool GeOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool GeOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 GeOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void GeOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void GeOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

bool GeOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 GeOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool GeOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void GeOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool GeOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void GeOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

// Expand the two-bit orientation into the original angle values. Pitch and roll are always zero.
void GeOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool GeOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand tile and altitude fields to micro coordinates.
void GeOccupant::GetLocation(CtCoord *out) const {
  out->x = i32((position & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((position >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude =
      i32(((position >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void GeOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 GeOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Obtain cell coordinates through the virtual getter before scaling. Preserve its behavior when output pointers alias.
void GeOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u16 GeOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Write inclusive micro-coordinate bounds when attribute data exists. Dimensions are narrowed to the bytes consumed by the binary.
void GeOccupant::GetBounds(GeBounds *out) const {
  if (attributes) {
    out->minimum.x = i32((position & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
    out->minimum.z = i32(((position >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
    out->minimum.altitude =
        i32(((position >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
    u32 upperAltitude = SizeInCellsY() + u32(u8(position >> kOcPositionAltitudeShift)) - 1;
    u32 upperZ = SizeInCellsZ() + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
    out->maximum.x = i32(
        (((position & kOcPositionCellValueMask) + SizeInCellsX()) << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell);
    out->maximum.z = i32(upperZ << kCtMicroCoordinateShift);
    out->maximum.altitude = i32(upperAltitude << kCtMicroCoordinateShift);
  }
}

// Write the inclusive footprint when attribute data exists. Missing attributes leave the output unchanged.
void GeOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (attributes) {
    out->left = position & kOcPositionCellValueMask;
    out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
    out->right = SizeInCellsX() + (position & kOcPositionCellValueMask) - 1;
    out->bottom = SizeInCellsZ() + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
  }
}

u32 GeOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << kCtMicroCoordinateShift;
}

u32 GeOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << kCtMicroCoordinateShift;
}

u32 GeOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << kCtMicroCoordinateShift;
}

// Replace the position fields from micro coordinates in source order. Preserve the two orientation bits.
void GeOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (u32(u8(u32(value.altitude) >> kCtMicroCoordinateShift)) << kOcPositionAltitudeShift);
}

void GeOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void GeOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

void GeOccupant::SetAltAnimCells(u16 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude & 0xff00) << kOcAltitudeFromMicroShift);
}

bool GeOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void GeOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void GeOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void GeOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

u32 GeOccupant::GetSubTypeData(u32) {
  return 0;
}

// Follow borrowed sprite links before releasing each instance. Clear the published head only after the final release.
void GeOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (current) {
    do {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    } while (current);
    *out = nullptr;
  }
}

void GeOccupant::AnimStart() {
}

void GeOccupant::AnimStop() {
}

i32 GeOccupant::Tick(u32) {
  return 0;
}

// Retain the module before locking and incrementing only the packed reference word. Return the captured count after
// unlocking; overflow wraps within sixteen bits without changing the instance subtype or flags.
u32 GeOccupant::AddRef() {
  GeGetDirector()->AddRef();
  GeOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  GeOccupantLock.Unlock();
  return count;
}

// Remove the module reference while the packed count is locked. Release the lock before shutdown and deletion at zero,
// including an initially unreferenced object; a surviving object returns its live count after unlocking.
u32 GeOccupant::Release() {
  GeOccupantLock.Lock();
  if (u16(flags)) {
    GeGetDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
  }
  if (!u16(flags)) {
    GeOccupantLock.Unlock();
    Shutdown();
    delete this;
    return 0;
  }
  GeOccupantLock.Unlock();
  return u16(flags);
}

u8 GeOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 GeOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 GeOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 GeOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void GeOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 GeOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u8 GeOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

u32 GeOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

// Borrow the retained attribute interface directly. This getter does not acquire a reference.
GeAttrib *GeOccupant::GetAttribData() {
  return attributes;
}

u32 GeOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 GeOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 GeOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 GeOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

// Replace retained attribute data before creating the default-layer sprite. Failure delegates cleanup to Shutdown; a
// zero instance subtype byte inherits the attribute subtype classification.
bool GeOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  if (!CreateSprite(GetDefaultLayerFlag(), &sprite)) {
    Shutdown();
    return false;
  }
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Replace the packed rotation and recreate its sprite only when the requested index differs.
u8 GeOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  CreateSprite(GetDefaultLayerFlag(), &sprite);
  return 1;
}

bool GeOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Borrow the agent type supplied by the retained attribute object. Missing attributes return a null pointer.
CtAgentType *GeOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

u32 GeOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Borrow the retained sprite pointer. This shared machine-code getter also serves unrelated fields in other original
// classes.
SpSprite *GeOccupant::GetSpriteInst() {
  return sprite;
}

// Return the borrowed sprite only for the virtual default layer. A mismatch does not call the sprite getter.
SpSprite *GeOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 GeOccupant::GetMemUse() const {
  return 20;
}

// Borrow the unindexed sprite definition from retained attributes. Missing attributes return null without a lookup.
SpDefinition *GeOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Forward the complete layer mask to the retained attribute interface. The definition remains borrowed.
SpDefinition *GeOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Reuse already bound attributes without consulting the key. Otherwise borrow the actual application cache, dispatch
// attribute initialization virtually and release its live output after capturing the initialization result.
bool GeOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!GeGetOccupantAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool initialized = Init(value);
  if (value)
    value->Release();
  return initialized;
}

// Acquire the virtual sprite-definition or agent-type subsystem only when base attributes exist. Failures preserve the
// original output; successful acquisition publishes the captured interface after its AddRef callback completes.
bool GeOccupant::GetSubSystemData(u32 id, void **out) {
  if (!attributes)
    return false;
  ScUnknown *value;
  if (id == IID_SpDefView)
    value = static_cast<SpDefView *>(GetSpriteAttrib());
  else if (id == IID_CtAgentType)
    value = CityAgentType();
  else
    return false;
  if (!value)
    return false;
  value->AddRef();
  *out = value;
  return true;
}

bool GeOccupant::DoMessage(ScMessage *) {
  return false;
}

u8 GeOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

bool GeOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool GeOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool GeOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

// Clear the caller's query-window output before inspecting retained attributes.
bool GeOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  GeAttrib *value = attributes;
  if (!value)
    return false;
  u32 classId = value->GetQueryDialogClass();
  return GeGetCom()->GetClassObject(classId, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

// Create an instance from the retained attribute's indexed definition, then apply the live packed rotation and virtual
// geometry flags. Failure still calls the live attribute's key getter and leaves any producer output publication intact.
bool GeOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (definition && definition->CreateInstance(out)) {
    (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
    u32 geometry = GetGeomFlag();
    (*out)->SetGeomFlag(geometry);
    return true;
  }
  attributes->Key();
  return false;
}

// Clear and release retained attributes before destroying the live sprite chain.
// Apply the original packed-state mask after both callbacks, preserving the reference word and live callback changes.
void GeOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Initialize missing attributes from the descriptor key before reading position and rotation. Failed stages still publish
// their initialized or partially written coordinates, then mark both removal flags rather than rolling the object back.
bool GeOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool ready = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    ready = Init(&key);
    u32 type = 0;
    if (ready)
      ready = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (ready)
    ready = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (ready)
    ready = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (ready) {
    u8 first = info->GetCanSimRemoveFlag();
    flags = (flags & kOcFlagsWithoutSimRemove) | (u32(first & 1) << kOcSimRemoveShift);
    u8 second = info->GetCanUserRemoveFlag();
    flags = (flags & kOcFlagsWithoutUserRemove) | (u32(second & 1) << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return ready;
}

// Write the retained key, virtual type and live packed position to the original descriptor. Preserve both virtual width
// calls in their original order, including the repeated X dimension, before writing live rotation and removal flags.
bool GeOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  info->SetTypeId(TypeId());
  u32 point = position;
  info->SetLocationInCells(point & kOcPositionCellValueMask, (point >> kOcPositionZShift) & kOcPositionCellValueMask,
      u8(point >> kOcPositionAltitudeShift));
  u8 second = SizeInCellsX(), first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  u32 state = flags;
  info->SetRemoveFlags(u8((state >> kOcSimRemoveShift) & 1), u8((state >> kOcUserRemoveShift) & 1));
  return true;
}

// Read the saved fields in their original order, ignoring primitive replies and the resource initializer's result.
// First-use staging is zeroed solely to exclude originally unwritten, undefined outputs from the native execution.
u8 GeOccupant::SerialRead(CtCity *, RsStream *record) {
  RsKey key;
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 type;
  u8 major, animation, sizeX, sizeZ, sizeY, subtype = 0, mark = 0, membership = 0, simRemove = 0, userRemove = 0;
  record->ReadU32(&type);
  record->ReadU8(&major);
  record->ReadU8(&animation);
  record->ReadU8(&sizeX);
  record->ReadU8(&sizeZ);
  record->ReadU8(&sizeY);
  record->ReadU8(&subtype);
  SetSubTypeInfo(subtype);
  record->ReadU8(&mark);
  SetMarkBit(mark != 0);
  record->ReadU8(&membership);
  SetIsInCityFlag(membership);
  record->ReadU8(&simRemove);
  SetSimRemove(simRemove != 0);
  record->ReadU8(&userRemove);
  SetUserRemove(userRemove != 0);
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  point.x = i32(x << kCtMicroCoordinateShift);
  point.z = i32(z << kCtMicroCoordinateShift);
  point.altitude = i32(altitude << kCtMicroCoordinateShift);
  SetLocation(point);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return 1;
}

// Serialize the retained base attribute resource and live virtual occupant fields. Re-read the escaped resource
// publication after callbacks, ignore primitive write replies and release the final live resource before field getters.
u8 GeOccupant::SerialWrite(CtCity *, RsStream *record) {
  GeAttrib *value = GeOccupant::GetAttribData();
  void *publication = nullptr;
  if (!value || !value->QueryInterface(IID_RsResource, &publication))
    return 0;
  record->WriteU32(*static_cast<RsResource *>(publication)->Type());
  record->WriteU32(*static_cast<RsResource *>(publication)->Group());
  record->WriteU32(*static_cast<RsResource *>(publication)->Instance());
  static_cast<RsResource *>(publication)->Release();
  publication = nullptr;
  record->WriteU32(TypeId());
  record->WriteU8(MajorType());
  record->WriteU8(AnimType());
  record->WriteU8(SizeInCellsX());
  record->WriteU8(SizeInCellsZ());
  record->WriteU8(SizeInCellsY());
  record->WriteU8(SubTypeInfo());
  record->WriteU8(u8(GetMarkBit()));
  record->WriteU8(u8(GetIsInCityFlag()));
  record->WriteU8(u8(CanSimRemove()));
  record->WriteU8(u8(CanUserRemove()));
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GetLocation(&point);
  record->WriteU32(u32(point.x >> kCtMicroCoordinateShift));
  record->WriteU32(u32(point.z >> kCtMicroCoordinateShift));
  record->WriteU32(u32(point.altitude >> kCtMicroCoordinateShift));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return 1;
}

// Dispatch recognized query messages through the acquired UI interface and the actual occupant virtual hook.
// Release the live interface output after the hook, preserving callback changes, and always report false.
bool GeOccupant::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  u32 route, iid;
  if (message->type == kWnMsg_QueryOpened) {
    switch (u32(message->data1)) {
    case kUiQueryTagDialog:
      route = 0;
      iid = 1401;
      break;
    case kUiQueryTagStandard:
      route = 1;
      iid = 1411;
      break;
    case kUiQueryTagDebug:
      route = 2;
      iid = 1411;
      break;
    default:
      return false;
    }
  } else if (message->type == kWnMsg_QueryClosed && u32(message->data1) == kUiQueryTagDialog) {
    route = 3;
    iid = 1401;
  } else
    return false;
  void *selected = nullptr;
  if (!source || !source->QueryInterface(iid, &selected))
    return false;
  switch (route) {
  case 0:
    DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
    break;
  case 1:
    DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
    break;
  case 2:
    DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
    break;
  case 3:
    DoExitQueryWindow(message, static_cast<UiQueryWindow *>(selected));
    break;
  }
  static_cast<ScUnknown *>(selected)->Release();
  return false;
}

u8 GeOccupant::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Submit a nonempty override architect name using the original localized label and row flags.
u8 GeOccupant::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  OcOverride *overrideAttrib = value->GetOverRideAttrib();
  if (!overrideAttrib)
    return 0;
  ScString architect;
  overrideAttrib->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  // Build the independent row label before retiring its temporary resource and intermediate copies.
  ScString label = []() {
    GeTextResource text(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString copy = text;
    ScString intermediate(copy);
    return ScString(intermediate);
  }();
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

u8 GeOccupant::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u8 GeOccupant::DoExitQueryWindow(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Construct the actual map and lock before defining the city state in the original city/manager/ready order.
GePortLayer::GePortLayer() {
  city = nullptr;
  occupants = nullptr;
  cityReady = 0;
}

GePortLayer::~GePortLayer() {
}

// Expose the layer/message/query or port-map identity, then acquire the shared map reference.
// Other identifiers delegate to the byte-map base; the change-receiver identity is not exposed by the original query.
bool GePortLayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_GePortMap, GePortMap);
  return GeByteMap::QueryInterface(iid, out);
}

u32 GePortLayer::AddRef() {
  return GeByteMap::AddRef();
}

u32 GePortLayer::Release() {
  return GeByteMap::Release();
}

u32 GePortLayer::CellCountX() const {
  return GeByteMap::CellCountX();
}

u32 GePortLayer::CellCountZ() const {
  return GeByteMap::CellCountZ();
}

void GePortLayer::GetValueSint32(u32 x, u32 z, i32 *value) const {
  GeByteMap::GetValueSint32(x, z, value);
}

bool GePortLayer::InBounds(u32 x, u32 z) const {
  return GeByteMap::InBounds(x, z);
}

bool GePortLayer::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return GeByteMap::InBounds(x0, z0, x1, z1);
}

bool GePortLayer::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return GeByteMap::ValidRect(x0, z0, x1, z1);
}

bool GePortLayer::SerialRead(RsStream *record) {
  return GeByteMap::SerialRead(record);
}

bool GePortLayer::SerialWrite(RsStream *record) const {
  return GeByteMap::SerialWrite(record);
}

bool GePortLayer::Init(CtCity *value, const u8 *fill) {
  return GeByteMap::Init(value, fill);
}

void GePortLayer::GetValue(u32 x, u32 z, u8 *value) const {
  GeByteMap::GetValue(x, z, value);
}

void GePortLayer::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  GeByteMap::SetValue(x0, z0, x1, z1, value);
}

void GePortLayer::SetValue(u32 x, u32 z, const u8 *value) {
  GeByteMap::SetValue(x, z, value);
}

void GePortLayer::SetAllCells(const u8 *value) {
  GeByteMap::SetAllCells(value);
}

bool GePortLayer::DoMessage(ScMessage *) {
  return false;
}

bool GePortLayer::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool GePortLayer::StaticInit(CtDefinition *) {
  return true;
}

bool GePortLayer::StaticShutdown() {
  return true;
}

// Retain the city and its live surface manager, then initialize the actual zero-filled map through its virtual overload.
// Any failure dispatches the same virtual Shutdown used when replacing an attached city.
bool GePortLayer::Init(CtCity *value) {
  u8 zero = 0;
  if (cityReady)
    Shutdown();
  if (value) {
    city = ScAddRefNotNull(value);
    occupants = city->SurfaceOccupantManager();
    if (occupants) {
      occupants->AddRef();
      if (Init(value, &zero)) {
        cityReady = 1;
        return true;
      }
    }
  }
  Shutdown();
  return false;
}

bool GePortLayer::Init(CtCity *, ScImporter *) {
  return true;
}

// Open the real layer record and read the initialized map, selecting record abort on frame or body failure.
// Frame destruction retains the existing stream-release and segment-close ordering.
bool GePortLayer::Init(CtCity *world, RsDatabaseSegment *segment) {
  NwRecordFrame frame(world, segment, RsKey{kRsCityLayerRecordType, kGePortLayerType, 0});
  bool success = frame.valid != 0;
  RsStream *stream = frame.Stream();
  if (success && !SerialRead(stream))
    success = false;
  if (!success) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return success;
}

// Save the map into the original version-one record without debug text, marking failed frames or writes for abort.
bool GePortLayer::Save(CtCity *world, RsDatabaseSegment *segment) {
  NwRecordFrame frame(world, segment, RsKey{kRsCityLayerRecordType, kGePortLayerType, 0}, 1, 0);
  bool success = frame.valid != 0;
  RsStream *stream = frame.Stream();
  if (success && !SerialWrite(stream))
    success = false;
  if (!success) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return success;
}

// Register the real change-receiver subobject with the retained surface manager. Missing manager returns false.
bool GePortLayer::SimulationBegin() {
  if (!occupants)
    return false;
  u32 sender;
  return occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, false);
}

bool GePortLayer::SimulationEnd() {
  return true;
}

// Clear city readiness before directly retiring the actual map base, preserving the original separate ownership phases.
bool GePortLayer::Shutdown() {
  if (occupants) {
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
    occupants->Release();
    occupants = nullptr;
  }
  ScRelease(&city);
  cityReady = 0;
  return GeByteMap::Shutdown();
}

bool GePortLayer::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

u32 GePortLayer::GetLayerType() {
  return kGePortLayerType;
}

bool GePortLayer::DebugClassTag(ScStringView *) {
  return false;
}

bool GePortLayer::DebugTypeTag(ScStringView *) {
  return false;
}

bool GePortLayer::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool GePortLayer::CellChangedAll(u32) {
  return true;
}

bool GePortLayer::CellChanged(u32, u32, u32) {
  return true;
}

bool GePortLayer::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

// Compute the original segment scan bounds without assuming independently sorted X/Z corners.
// For unequal X, the first point supplies the near Z edge and the greater-X point supplies the far Z edge.
static ScRect GePortSegmentRange(const ScPointXZ &first, const ScPointXZ &second) {
  ScRect range;
  if (first.x == second.x) {
    range.left = i32(first.x - 4);
    if (first.z >= second.z) {
      range.right = i32(first.x + 4);
      range.top = i32(second.z - 4);
      range.bottom = i32(first.z + 4);
    } else {
      range.right = i32(second.x + 4);
      range.top = i32(first.z - 4);
      range.bottom = i32(second.z + 4);
    }
  } else {
    range.top = i32(first.z - 4);
    if (first.x >= second.x) {
      range.left = i32(second.x - 4);
      range.right = i32(first.x + 4);
      range.bottom = i32(first.z + 4);
    } else {
      range.left = i32(first.x - 4);
      range.right = i32(second.x + 4);
      range.bottom = i32(second.z + 4);
    }
  }
  return range;
}

// Test bit zero over the captured segment range, advancing both coordinates by four.
// Retain every original virtual bounds/value call and the asymmetric bounds for non-collinear inputs.
u8 GePortLayer::HasSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) {
  ScRect range = GePortSegmentRange(first, second);
  u8 value = 0;
  for (u32 x = u32(range.left); i32(x) <= range.right; x += 4)
    for (u32 z = u32(range.top); i32(z) <= range.bottom; z += 4)
      if (InBounds(x, z)) {
        GetValue(x, z, &value);
        if (value & 1)
          return 1;
      }
  return 0;
}

// Test bit one over the original three-by-three coarse-cell neighborhood, skipping out-of-bounds sample coordinates.
u8 GePortLayer::HasNearbyFlag2(u32 x, u32 z) {
  i32 endX = i32(x + 4), endZ = i32(z + 4);
  u8 value = 0;
  for (u32 sampleX = x - 4; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 4; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        if (value & 2)
          return 1;
      }
  return 0;
}

// Test the low flag only after the actual virtual bounds predicate accepts the requested coordinates.
u8 GePortLayer::HasFlag1(u32 x, u32 z) {
  if (!InBounds(x, z))
    return 0;
  u8 value = 0;
  GetValue(x, z, &value);
  return value & 1;
}

// Report bit one as the original zero-or-one byte after a successful bounds check.
u8 GePortLayer::HasFlag2(u32 x, u32 z) {
  if (!InBounds(x, z))
    return 0;
  u8 value = 0;
  GetValue(x, z, &value);
  return (value & 2) != 0;
}

// Report whether any of the packed adjacency-count bits are present at the accepted map coordinate.
u8 GePortLayer::HasAdjacency(u32 x, u32 z) {
  if (!InBounds(x, z))
    return 0;
  u8 value = 0;
  GetValue(x, z, &value);
  return (value & 0x3c) != 0;
}

// Refuse a segment with an existing low flag, then set that flag over the captured four-step region under the real lock.
// The original manual unlock occurs only on the normal path; exceptions from virtual map calls retain the held lock.
u8 GePortLayer::SetSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) {
  if (HasSegmentFlag(first, second))
    return 0;
  ScRect range = GePortSegmentRange(first, second);
  mapLock.Lock();
  u8 value = 0;
  for (u32 x = u32(range.left); i32(x) <= range.right; x += 4)
    for (u32 z = u32(range.top); i32(z) <= range.bottom; z += 4)
      if (InBounds(x, z)) {
        GetValue(x, z, &value);
        value |= 1;
        SetValue(x, z, &value);
      }
  mapLock.Unlock();
  return 1;
}

// Refuse a neighborhood that already has bit one, then publish it in each accepted coarse cell under the original lock.
u8 GePortLayer::SetNearbyFlag2(u32 x, u32 z) {
  if (HasNearbyFlag2(x, z))
    return 0;
  i32 endX = i32(x + 4), endZ = i32(z + 4);
  mapLock.Lock();
  u8 value = 0;
  for (u32 sampleX = x - 4; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 4; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        value |= 2;
        SetValue(sampleX, sampleZ, &value);
      }
  mapLock.Unlock();
  return 1;
}

// Reject any saturated count in the five-by-five coarse neighborhood before the separate mutation traversal.
// Successful traversal increments the packed count, blocks the center terrain cell and unlocks before returning one.
u8 GePortLayer::IncrementAdjacency(u32 x, u32 z) {
  mapLock.Lock();
  i32 endX = i32(x + 8), endZ = i32(z + 8);
  u8 value = 0;
  for (u32 sampleX = x - 8; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 8; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        value &= 0x3c;
        if (value >= 0x3c) {
          mapLock.Unlock();
          return 0;
        }
      }
  for (u32 sampleX = x - 8; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 8; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        value = u8((value & 0xc3) | ((value & 0x3c) + 4));
        SetValue(sampleX, sampleZ, &value);
      }
  ScDirtBag *terrain = GeGetCity()->DirtBag();
  if (terrain)
    terrain->BlockCell(x, z, true);
  mapLock.Unlock();
  return 1;
}

// Apply 11DBE's complete clearing effects over the same captured region, without a precheck.
void GePortLayer::ClearSegmentFlag(const ScPointXZ &first, const ScPointXZ &second) {
  ScRect range = GePortSegmentRange(first, second);
  mapLock.Lock();
  u8 value = 0;
  for (u32 x = u32(range.left); i32(x) <= range.right; x += 4)
    for (u32 z = u32(range.top); i32(z) <= range.bottom; z += 4)
      if (InBounds(x, z)) {
        GetValue(x, z, &value);
        value &= 0xfe;
        SetValue(x, z, &value);
      }
  mapLock.Unlock();
}

// Preserve 11F12's original bit-one clearing traversal and manual normal-path unlock.
void GePortLayer::ClearNearbyFlag2(u32 x, u32 z) {
  i32 endX = i32(x + 4), endZ = i32(z + 4);
  mapLock.Lock();
  u8 value = 0;
  for (u32 sampleX = x - 4; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 4; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        value &= 0xfd;
        SetValue(sampleX, sampleZ, &value);
      }
  mapLock.Unlock();
}

// Apply 11FA9's decrement effects without clamping zero counts; the borrow can set upper byte bits as in the original.
// Unblock the center terrain cell while locked.
void GePortLayer::DecrementAdjacency(u32 x, u32 z) {
  mapLock.Lock();
  i32 endX = i32(x + 8), endZ = i32(z + 8);
  u8 value = 0;
  for (u32 sampleX = x - 8; i32(sampleX) <= endX; sampleX += 4)
    for (u32 sampleZ = z - 8; i32(sampleZ) <= endZ; sampleZ += 4)
      if (InBounds(sampleX, sampleZ)) {
        GetValue(sampleX, sampleZ, &value);
        value = u8((value & 0xc3) | ((value & 0x3c) - 4));
        SetValue(sampleX, sampleZ, &value);
      }
  ScDirtBag *terrain = GeGetCity()->DirtBag();
  if (terrain)
    terrain->BlockCell(x, z, false);
  mapLock.Unlock();
}

bool GePortLayer::OccupantInserted(u32, ScOccupant *occupant) {
  return HandleOccupantChange(0, occupant) != 0;
}

bool GePortLayer::OccupantRemoved(u32, ScOccupant *occupant) {
  return HandleOccupantChange(1, occupant) != 0;
}

bool GePortLayer::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

// Process only removal operation one: clear the type-four neighborhood or remove connected runway/port objects.
// Temporarily unregister the receiver around cascades, then re-register through the live manager, ignoring its result.
u8 GePortLayer::HandleOccupantChange(u32 operation, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  if (operation == 1) {
    u32 type = GeGetPortType(occupant);
    if (type == 1 || (type >= 7 && type <= 9)) {
      CtChangeReceiver *receiver = static_cast<CtChangeReceiver *>(this);
      occupants->UnregisterChangeReceiver(receiver);
      if (type == 1)
        RemoveRunway(occupant);
      else
        RemovePortNeighbors(occupant);
      u32 sender;
      occupants->RegisterChangeReceiver(receiver, &sender, false);
    } else if (type == 4) {
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      ClearNearbyFlag2(x, z);
    }
  }
  return 1;
}

void GePortLayer::RemoveRunway(ScOccupant *occupant) {
  u32 minX, minZ;
  occupant->GetLocationInCellCoords(&minX, &minZ);
  u32 maxX = minX, maxZ = minZ, scanX = minX + 1, scanZ = minZ;
  ScOccupant *current = nullptr;
  for (;;) {
    if (!occupants->IsInBoundsCell(scanX, scanZ) || !occupants->GetOccupantAtCell(scanX, scanZ, &current))
      break;
    bool more = false;
    if (GeGetPortType(current) == 1) {
      more = true;
      occupants->RemoveOccupant(current);
      maxX = scanX++;
    }
    current->Release();
    if (!more)
      break;
  }
  scanZ = minZ;
  scanX = minX - 1;
  for (;;) {
    if (!occupants->IsInBoundsCell(scanX, scanZ) || !occupants->GetOccupantAtCell(scanX, scanZ, &current))
      break;
    bool more = false;
    if (GeGetPortType(current) == 1) {
      more = true;
      occupants->RemoveOccupant(current);
      minX = scanX--;
    }
    current->Release();
    if (!more)
      break;
  }
  if (minX == maxX) {
    scanX = minX;
    scanZ = minZ + 1;
    for (;;) {
      if (!occupants->IsInBoundsCell(scanX, scanZ) || !occupants->GetOccupantAtCell(scanX, scanZ, &current))
        break;
      bool more = false;
      if (GeGetPortType(current) == 1) {
        more = true;
        occupants->RemoveOccupant(current);
        maxZ = scanZ++;
      }
      current->Release();
      if (!more)
        break;
    }
    scanX = minX;
    scanZ = minZ - 1;
    for (;;) {
      if (!occupants->IsInBoundsCell(scanX, scanZ) || !occupants->GetOccupantAtCell(scanX, scanZ, &current))
        break;
      bool more = false;
      if (GeGetPortType(current) == 1) {
        more = true;
        occupants->RemoveOccupant(current);
        minZ = scanZ--;
      }
      current->Release();
      if (!more)
        break;
    }
  }
  ScPointXZ first{minX, minZ}, second{maxX, maxZ};
  ClearSegmentFlag(first, second);
}

// Remove before recursing and Release the live acquired output afterwards; the discarded private result is unmarked.
void GePortLayer::RemovePortNeighbors(ScOccupant *occupant) {
  u32 originType = GeGetPortType(occupant);
  bool connector = originType == 8, pier = originType == 9;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  ScOccupant *current = nullptr;
  for (u32 direction = 0; direction < 4; ++direction) {
    u32 nextX = x, nextZ = z;
    switch (direction) {
    case 0:
      --nextX;
      break;
    case 1:
      --nextZ;
      break;
    case 2:
      ++nextX;
      break;
    default:
      ++nextZ;
      break;
    }
    if (!occupants->IsInBoundsCell(nextX, nextZ) || !occupants->GetOccupantAtCell(nextX, nextZ, &current))
      continue;
    u32 type = GeGetPortType(current);
    if (type == 7 || type == 8 || type == 9) {
      if (type == 9 && connector)
        DecrementAdjacency(x, z);
      else if (type == 8 && pier)
        DecrementAdjacency(nextX, nextZ);
      occupants->RemoveOccupant(current);
      RemovePortNeighbors(current);
    }
    current->Release();
  }
}

// Construct the actual port layer and return its city-layer subobject without acquiring an initial reference.
ScUnknown *GeCreatePortLayer() {
  return static_cast<CtLayer *>(new GePortLayer);
}
