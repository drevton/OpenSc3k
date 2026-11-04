#define _CRT_SECURE_NO_WARNINGS
#include "network/nw_manager.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "network/nw_module.h"
#include "network/nw_occupant.h"
#include "network/nw_records.h"
#include "network/nw_rules.h"
#include "network/nw_tool.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Run the bounded occupancy flood, crossing/removal phases and all three real tiling-rule families.
// The input is an owned by-value list; output bounds describe work before slope and rule-stage removals.
void NwManager::ApplyTiling(NwPositionList positions, i32 networkType, GeBounds *box) {
  NwPositionList work, changed, deferred, flood, pending, complex;
  static const u8 networks[6] = {0, 2, 5, 1, 4, 3};
  ScOccupantManager *layer = nullptr;
  NwRuleCollection *finalRules = nullptr, *simpleRules = nullptr, *complexRules = nullptr;
  OcIteratorTest *positive = nullptr, *negative = nullptr, *crossing = nullptr;
  u8 writeSide = 0, allowWater = 0;
  switch (networkType) {
  case kNwRoadNetwork:
  case kNwRailNetwork:
  case kNwPowerNetwork:
  case kNwHighwayNetwork:
  case kNwPipeNetwork:
  case kNwSubwayNetwork: {
    u32 network = networks[networkType - 1];
    layer = networkType == kNwPipeNetwork ? undergroundOne
        : networkType == kNwSubwayNetwork ? undergroundTwo
                                          : surfaceLayer;
    finalRules = NwFinalRules[network];
    simpleRules = NwSimpleRules[network];
    complexRules = NwComplexRules[network];
    positive = NwPredicates[network];
    negative = NwPredicates[9 + network];
    static const u8 crossings[6] = {16, 17, 18, 19, 8, 8};
    crossing = NwPredicates[crossings[networkType - 1]];
    writeSide = networkType == kNwRoadNetwork || networkType == kNwRailNetwork || networkType == kNwHighwayNetwork;
    allowWater = networkType == kNwPipeNetwork || networkType == kNwSubwayNetwork;
    break;
  }
  default:
    break;
  }
  i32 dx = 0, dz = 0;
  NwPositionNode *head = positions.head;
  if (networkType == kNwHighwayNetwork) {
    NwPositionNode *first = head->next, *second = first->next;
    if (second != head) {
      dx = i32(u32(second->position.x >> kCtMicroCoordinateShift) - u32(first->position.x >> kCtMicroCoordinateShift));
      dz = i32(u32(second->position.z >> kCtMicroCoordinateShift) - u32(first->position.z >> kCtMicroCoordinateShift));
    }
  }
  for (NwPositionNode *node = head->next; node != head; node = node->next) {
    i32 radius =
        networkType == kNwRoadNetwork || networkType == kNwHighwayNetwork || networkType == kNwSubwayNetwork ? 5 : 2;
    u32 occupied[32] = {}, reached[32] = {};
    i32 width = i32(city->CellCountX()), height = i32(city->CellCountZ());
    i32 centerX = node->position.x >> kCtMicroCoordinateShift, centerZ = node->position.z >> kCtMicroCoordinateShift;
    for (i32 x = centerX - radius; x < centerX + radius; ++x)
      for (i32 z = centerZ - radius; z < centerZ + radius; ++z)
        if (x >= 0 && x < width && z >= 0 && z < height) {
          CtCoord target = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
          bool accepted = false;
          for (NwPositionNode *input = head->next; input != head; input = input->next)
            if (!((u32(input->position.x) ^ u32(target.x)) & kCtMicroCellMask) &&
                !((u32(input->position.z) ^ u32(target.z)) & kCtMicroCellMask))
              accepted = true;
          ScOccupant *occupant = nullptr;
          if (accepted || (layer->GetOccupant(target, &occupant) && positive->Test(occupant)))
            occupied[16 + z - centerZ] |= 1u << (16 + x - centerX);
          ScRelease(&occupant);
        }
    reached[16] = 1u << 16;
    for (u32 iteration = 0; iteration < 32; ++iteration) {
      bool modified = false;
      for (u32 row = 0; row < 32; ++row) {
        u32 old = reached[row], value = occupied[row] & (old | (old << 1) | (old >> 1));
        modified |= value != old;
        reached[row] = value;
      }
      for (u32 row = 1; row < 31; ++row) {
        u32 old = reached[row], value = occupied[row] & (reached[row - 1] | old | reached[row + 1]);
        modified |= value != old;
        reached[row] = value;
      }
      if (!modified)
        break;
    }
    for (i32 x = centerX - radius; x < centerX + radius; ++x)
      for (i32 z = centerZ - radius; z < centerZ + radius; ++z)
        if (x >= 0 && x < width && z >= 0 && z < height && (reached[16 + z - centerZ] & (1u << (16 + x - centerX)))) {
          CtCoord target = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
          flood.Append(target);
        }
  }
  NwSortPositions(&flood);
  u8 toggle = 0;
  for (NwPositionNode *node = head->next; node != head; node = node->next) {
    ScOccupant *occupant = nullptr;
    toggle = !toggle;
    if (layer->GetOccupant(node->position, &occupant)) {
      u8 replaced = 0;
      if (NwPredicates[6]->Test(occupant))
        replaced = ReplaceCrossing(
            layer, node->position, positive, crossing, writeSide, allowWater, networkType, i16(dx), i16(dz), toggle);
      occupant->Release();
      occupant = nullptr;
      if (replaced)
        changed.Append(node->position);
    }
  }
  for (NwPositionNode *node = flood.head->next; node != flood.head; node = node->next)
    if (layer->IsInBounds(node->position)) {
      ScOccupant *occupant = nullptr;
      if (layer->GetOccupant(node->position, &occupant)) {
        if (positive->Test(occupant)) {
          u32 piece = GetTileId(occupant);
          if (IsProtectedPiece(networkType, piece)) {
            if (networkType == kNwPowerNetwork)
              complex.Append(node->position);
          } else if (occupant->CanUserRemove()) {
            work.Append(node->position);
            layer->RemoveOccupant(occupant);
          }
        }
        occupant->Release();
      }
    }
  flood.Clear();
  for (NwPositionNode *node = head->next; node != head; node = node->next)
    work.Append(node->position);
  NwSortPositions(&work);
  for (NwPositionNode *node = work.head->next; node != work.head; node = node->next)
    if (layer->IsInBounds(node->position)) {
      ScOccupant *occupant = nullptr;
      if (layer->GetOccupant(node->position, &occupant)) {
        u32 piece = GetTileId(occupant);
        if (positive->Test(occupant) && IsProtectedPiece(networkType, piece))
          changed.Append(node->position);
        occupant->Release();
      }
    }
  NwRemovePositions(&work, changed);
  changed.Clear();
  NwPositionList slopes;
  for (NwPositionNode *node = work.head->next; node != work.head; node = node->next)
    if (!PlacementFlat(node->position) && PlacementSlope(node->position))
      slopes.Append(node->position);
  for (NwPositionNode *node = work.head->next; node != work.head; node = node->next) {
    pending.Append(node->position);
    complex.Append(node->position);
  }
  *box = GeBounds{{0, 0, 0}, {0, 0, 0}};
  if (work.head->next != work.head) {
    const CtCoord *first = &work.head->next->position;
    box->minimum.x = box->maximum.x = first->x;
    box->minimum.z = box->maximum.z = first->z;
    box->minimum.altitude = box->maximum.altitude = kCtUnknownCoordinate;
    for (NwPositionNode *node = work.head->next; node != work.head; node = node->next) {
      if (node->position.x < box->minimum.x)
        box->minimum.x = node->position.x;
      if (node->position.z < box->minimum.z)
        box->minimum.z = node->position.z;
      if (node->position.x > box->maximum.x)
        box->maximum.x = node->position.x;
      if (node->position.z > box->maximum.z)
        box->maximum.z = node->position.z;
    }
  }
  NwRemovePositions(&work, slopes);
  toggle = (u32(dx) + u32(dz)) < kScIntegerSignBit;
  for (NwPositionNode *node = slopes.head->next; node != slopes.head; node = node->next) {
    u32 piece;
    u8 state;
    toggle = !toggle;
    if (SelectSlope(node->position, networkType, &slopes, &piece, &state, toggle)) {
      NwRuleRecords results;
      NwRuleRecord *record = results.records.Append();
      record->piece = piece;
      record->direction = 31;
      record->state = state;
      if (CommitRules(layer, node->position, &results, positive, crossing, writeSide, allowWater, networkType))
        changed.Append(node->position);
    }
  }
  NwRemovePositions(&work, changed);
  changed.Clear();
  deferred.Clear();
  u32 before, after;
  do {
    before = NwPositionCount(work);
    ApplyRules(&work, &pending, &changed, &deferred, simpleRules, layer, positive, negative, crossing, writeSide,
        allowWater, 8, networkType);
    NwRemovePositions(&work, changed);
    changed.Clear();
    after = NwPositionCount(work);
  } while (before != after);
  for (u32 iteration = 0; iteration < 5; ++iteration) {
    ApplyRules(&complex, &pending, &changed, &deferred, complexRules, layer, positive, negative, crossing, writeSide,
        allowWater, 24, networkType);
    NwRemovePositions(&work, changed);
    u32 count = NwPositionCount(changed);
    changed.Clear();
    if (!count)
      break;
  }
  for (NwPositionNode *node = deferred.head->next; node != deferred.head; node = node->next)
    work.Append(node->position);
  ApplyRules(&work, &pending, &changed, &deferred, finalRules, layer, positive, negative, crossing, writeSide,
      allowWater, 4, networkType);
  NwRemovePositions(&work, changed);
  changed.Clear();
  work.Clear();
  pending.Clear();
}

// Test the enum-selected original global predicate against the actual occupant receiver.
// Unsupported enum values fail before reading any predicate owner.
bool NwManager::MatchesNetwork(u32 networkType, ScOccupant *occupant) {
  NwPredicate *predicate;
  switch (networkType) {
  case kNwRoadNetwork:
    predicate = NwPredicates[0];
    break;
  case kNwRailNetwork:
    predicate = NwPredicates[2];
    break;
  case kNwPowerNetwork:
    predicate = NwPredicates[5];
    break;
  case kNwHighwayNetwork:
    predicate = NwPredicates[1];
    break;
  case kNwPipeNetwork:
    predicate = NwPredicates[4];
    break;
  case kNwSubwayNetwork:
    predicate = NwPredicates[3];
    break;
  default:
    return false;
  }
  return predicate->Test(occupant);
}

// Query the original selector10 predicate before looking up the enum-selected layer at a cell coordinate.
// Empty or unsupported layers are eligible; the queried predicate is deliberately not released by this method.
bool NwManager::CanReplaceAt(i32 x, i32 z, i32 networkType) {
  void *view;
  if (!QueryPredicate(&view, IID_OcIteratorTest, 10))
    return false;
  ScOccupantManager *layer = nullptr;
  switch (networkType) {
  case kNwRoadNetwork:
  case kNwRailNetwork:
  case kNwPowerNetwork:
  case kNwHighwayNetwork:
    layer = surfaceLayer;
    break;
  case kNwPipeNetwork:
    layer = undergroundOne;
    break;
  case kNwSubwayNetwork:
    layer = undergroundTwo;
    break;
  default:
    break;
  }
  bool result = true;
  ScOccupant *occupant = nullptr;
  if (layer) {
    CtCoord position = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
    if (layer->GetOccupant(position, &occupant)) {
      result = static_cast<OcIteratorTest *>(view)->Test(occupant);
      occupant->Release();
    }
  }
  return result;
}

// Detect another surface transport family or a protected piece in the requested family.
// Major type5 bypasses all family checks, and protection receives the sign-extended low piece WORD.
bool NwManager::HasNetworkConflict(u32 networkType, CtCoord position) {
  ScOccupant *occupant = nullptr;
  bool conflict = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    if (occupant->MajorType() != 5) {
      i32 piece = i32(GetTileId(occupant) & 65535u);
      if (piece >= 32768)
        piece -= 65536;
      u16 matches = 0;
      for (u32 type = 1; type <= 4; ++type)
        if (networkType != type && MatchesNetwork(type, occupant))
          ++matches;
      if (MatchesNetwork(networkType, occupant) && IsProtectedPiece(networkType, u32(piece)))
        ++matches;
      conflict = matches != 0;
    }
    occupant->Release();
  }
  return conflict;
}

// Split the live ordered node ring into source collinear runs, retaining the last successful prefix count.
// Visit alternating highway nodes; water and conflicts break runs, and the second node establishes direction.
void NwManager::DecomposeRuns(
    NwPositionList *nodes, NwPositionList *output, i32 networkType, u8 apply, i32 *processed) {
  i32 dx = 0, dz = 0, lastX = 0, lastZ = 0, highwayX = 0, highwayZ = 0, startX = 0, startZ = 0;
  u32 visited = 0;
  u16 length = 0;
  bool active = false, successful = true;
  NwPositionNode *head = nodes->head, *node = head->next;
  if (networkType == kNwHighwayNetwork && node->next != head) {
    highwayX = (node->next->position.x >> kCtMicroCoordinateShift) - (node->position.x >> kCtMicroCoordinateShift);
    highwayZ = (node->next->position.z >> kCtMicroCoordinateShift) - (node->position.z >> kCtMicroCoordinateShift);
  }
  *processed = 0;
  while (node != head) {
    bool wet = false;
    if (networkType != kNwPipeNetwork && networkType != kNwSubwayNetwork)
      wet = PlacementWater(node->position);
    if (active) {
      bool extend = length == 1;
      if (!extend) {
        if (networkType == kNwHighwayNetwork && node->next != head)
          wet = wet || PlacementWater(node->next->position);
        bool conflict = HasNetworkConflict(networkType, node->position);
        if (!conflict && !wet && u16(dx) == u16(u32(node->position.x >> kCtMicroCoordinateShift) - u32(lastX)) &&
            u16(dz) == u16(u32(node->position.z >> kCtMicroCoordinateShift) - u32(lastZ))) {
          ++length;
          extend = true;
        } else {
          if (length >= 3) {
            if (BuildStraightRun(u16(startX), u16(startZ), u16(lastX), u16(lastZ), i16(highwayX), i16(highwayZ),
                    u16(networkType), output, apply)) {
              if (successful)
                *processed = i32(visited);
            } else
              successful = false;
          }
          if (!wet && !conflict) {
            if (successful)
              *processed = i32(visited);
            startX = lastX;
            startZ = lastZ;
            extend = true;
            length = 1;
          } else {
            active = false;
            if (successful)
              *processed = i32(visited);
          }
        }
      }
      if (extend) {
        if (length == 1) {
          dx = i32(u32(node->position.x >> kCtMicroCoordinateShift) - u32(lastX));
          dz = i32(u32(node->position.z >> kCtMicroCoordinateShift) - u32(lastZ));
          length = 2;
        }
        lastX = i32(u32(lastX) + u32(dx));
        lastZ = i32(u32(lastZ) + u32(dz));
      }
    } else if (!wet) {
      if (successful)
        *processed = i32(visited);
      lastX = node->position.x >> kCtMicroCoordinateShift;
      lastZ = node->position.z >> kCtMicroCoordinateShift;
      startX = lastX;
      startZ = lastZ;
      active = true;
      length = 1;
    }
    if (networkType == kNwHighwayNetwork && node->next != head) {
      node = node->next;
      ++visited;
    }
    node = node->next;
    ++visited;
  }
  if ((!active || length < 2 ||
          BuildStraightRun(u16(startX), u16(startZ), u16(lastX), u16(lastZ), i16(highwayX), i16(highwayZ),
              u16(networkType), output, apply)) &&
      successful)
    *processed = i32(visited);
}

static void NwCollectStrip(ScOccupantManager *layer, OcIteratorTest *positive, i32 x0, i32 z0, i32 x1, i32 z1,
    ScOccupant **occupant, CtCoord *position, NwPositionList *output) {
  for (i32 x = x0; x <= x1; ++x)
    for (i32 z = z0; z <= z1; ++z)
      if (layer->GetOccupantAtCell(u32(x), u32(z), occupant)) {
        if (positive->Test(*occupant)) {
          (*occupant)->GetLocation(position);
          output->Append(*position);
        }
        (*occupant)->Release();
      }
}

// Expand and clip the requested micro-coordinate rectangle, then collect its four inclusive perimeter strips.
// Preserve strip overlap, exact traversal order, live retained outputs and partial location-output history.
void NwManager::CollectRegion(
    ScOccupantManager *layer, OcIteratorTest *positive, u32 type, const GeBounds &rectangle, NwPositionList *output) {
  i32 distance = type & 0x12 ? 2 : 1;
  i32 x0 = i32(u32(rectangle.minimum.x) - (u32(distance) << kCtMicroCoordinateShift));
  i32 z0 = i32(u32(rectangle.minimum.z) - (u32(distance) << kCtMicroCoordinateShift));
  i32 x1 = i32(u32(rectangle.maximum.x) + (u32(distance) << kCtMicroCoordinateShift));
  i32 z1 = i32(u32(rectangle.maximum.z) + (u32(distance) << kCtMicroCoordinateShift));
  if (x0 < bounds.minimum.x)
    x0 = bounds.minimum.x;
  if (z0 < bounds.minimum.z)
    z0 = bounds.minimum.z;
  if (x1 > bounds.maximum.x)
    x1 = bounds.maximum.x;
  if (z1 > bounds.maximum.z)
    z1 = bounds.maximum.z;
  x0 >>= 8;
  z0 >>= 8;
  x1 >>= 8;
  z1 >>= 8;
  i32 maximumX = bounds.maximum.x >> kCtMicroCoordinateShift, maximumZ = bounds.maximum.z >> kCtMicroCoordinateShift;
  i32 firstEnd = z0 + distance, leftEnd = x0 + distance, rightStart = x1 - distance;
  i32 middleEnd = z1 - distance, rightEnd = z1 - distance, topStart = z1 - distance;
  if (firstEnd > maximumZ)
    firstEnd = maximumZ;
  if (leftEnd > maximumX)
    leftEnd = maximumX;
  if (rightStart < 0)
    rightStart = 0;
  if (middleEnd < 0)
    middleEnd = 0;
  if (rightEnd < 0)
    rightEnd = maximumZ;
  if (topStart < 0)
    topStart = 0;
  ScOccupant *occupant = nullptr;
  CtCoord position = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwCollectStrip(layer, positive, x0, z0, x1, firstEnd, &occupant, &position, output);
  NwCollectStrip(layer, positive, x0, firstEnd, leftEnd, middleEnd, &occupant, &position, output);
  NwCollectStrip(layer, positive, rightStart, firstEnd, x1, rightEnd, &occupant, &position, output);
  NwCollectStrip(layer, positive, x0, topStart, x1, z1, &occupant, &position, output);
}

// Reload a selected network's real layer and predicate, collect its work and invoke the owning tiling operation.
// The caller queue remains independent from the by-value operation argument and is cleared after the operation.
static void NwReapplyPass(
    NwManager *manager, const GeBounds &rectangle, u32 type, NwPositionList *nodes, GeBounds *box) {
  static const u8 networks[6] = {0, 2, 5, 1, 4, 3};
  ScOccupantManager *layer = type == 5 ? manager->undergroundOne
      : type == 6                      ? manager->undergroundTwo
                                       : manager->surfaceLayer;
  manager->CollectRegion(layer, NwPredicates[networks[type - 1]], type, rectangle, nodes);
  manager->ApplyTiling(*nodes, type, box);
  nodes->Clear();
}

// Reapply the mask-selected network families over clipped bounds, with highway work expanding the road rectangle.
// Sentinel X/Z coordinates skip all game work; valid rectangles retain inverted and overlapping bounds unchanged.
void NwManager::RefreshNetwork(const GeBounds &rectangle, u32 mask) {
  NwPositionList nodes;
  GeBounds working = rectangle;
  GeBounds box{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (rectangle.minimum.x == kCtUnknownCoordinate || rectangle.minimum.z == kCtUnknownCoordinate ||
      rectangle.maximum.x == kCtUnknownCoordinate || rectangle.maximum.z == kCtUnknownCoordinate)
    return;
  if (working.minimum.x < bounds.minimum.x)
    working.minimum.x = bounds.minimum.x;
  if (working.minimum.z < bounds.minimum.z)
    working.minimum.z = bounds.minimum.z;
  if (working.maximum.x > bounds.maximum.x)
    working.maximum.x = bounds.maximum.x;
  if (working.maximum.z > bounds.maximum.z)
    working.maximum.z = bounds.maximum.z;
  working.minimum.altitude = working.maximum.altitude = kCtUnknownCoordinate;
  if (mask & 3) {
    NwReapplyPass(this, working, 4, &nodes, &box);
    if (box.minimum.x || box.maximum.x || box.maximum.z || box.minimum.z) {
      if (box.minimum.x < working.minimum.x)
        working.minimum.x = box.minimum.x;
      if (box.minimum.z < working.minimum.z)
        working.minimum.z = box.minimum.z;
      if (box.maximum.x > working.maximum.x)
        working.maximum.x = box.maximum.x;
      if (box.maximum.z > working.maximum.z)
        working.maximum.z = box.maximum.z;
      working.minimum.altitude = working.maximum.altitude = kCtUnknownCoordinate;
    }
    NwReapplyPass(this, working, 1, &nodes, &box);
  }
  if (mask & 4)
    NwReapplyPass(this, working, 2, &nodes, &box);
  if (mask & 0x20)
    NwReapplyPass(this, working, 3, &nodes, &box);
  if (mask & 8)
    NwReapplyPass(this, working, 5, &nodes, &box);
  if (mask & 0x10)
    NwReapplyPass(this, working, 6, &nodes, &box);
}

// Write one layer through its actual iterator and descriptor, counting each accepted occupant before writing its words.
u8 NwManager::SaveLayer(
    RsDatabaseSegment *segment, ScOccupantManager *layer, OcIteratorTest *test, const RsKey &key, u32 *count) {
  *count = 0;
  OcIterator *iterator = nullptr;
  u8 result = layer->GetOccupants(&iterator, nullptr, test);
  if (result) {
    NwRecordFrame frame(city, segment, key, 4, 0);
    result = frame.valid;
    RsStream *stream = frame.Stream();
    if (result)
      result = stream != nullptr;
    NwRecord descriptor(1);
    for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next()) {
      if (!result)
        break;
      descriptor.Clear();
      if (occupant->Save(city, &descriptor)) {
        ++*count;
        result = descriptor.SerialWrite(city, stream);
      }
    }
    if (!result || !*count) {
      if (frame.record)
        frame.record->aborted = 1;
      frame.valid = 0;
    }
  }
  if (iterator) {
    iterator->Shutdown();
    iterator->Release();
  }
  return result;
}

// Save the three network layers in order, then write their counts and descriptor codec into the metadata record.
// Layer records use the manager's live city; the metadata frame uses the original city argument.
bool NwManager::Save(CtCity *target, RsDatabaseSegment *segment) {
  ScOccupantManager *surface = surfaceLayer;
  if (!surface || !undergroundOne || !undergroundTwo || !NwPredicates[6])
    return false;
  RsKey metadata = {kRsCityLayerRecordType, kNwManagerLayerType, 0},
        surfaceKey = {kRsCityLayerRecordType, kNwManagerLayerType, 1};
  RsKey firstKey = {kRsCityLayerRecordType, kNwManagerLayerType, 2},
        secondKey = {kRsCityLayerRecordType, kNwManagerLayerType, 3};
  u32 surfaceCount = 0, firstCount = 0, secondCount = 0;
  if (!SaveLayer(segment, surface, NwPredicates[6], surfaceKey, &surfaceCount) ||
      !SaveLayer(segment, undergroundOne, NwPredicates[6], firstKey, &firstCount) ||
      !SaveLayer(segment, undergroundTwo, NwPredicates[6], secondKey, &secondCount))
    return false;
  NwRecordFrame frame(target, segment, metadata, 4, 0);
  u8 result = frame.valid;
  RsStream *stream = frame.Stream();
  result = result && stream != nullptr && stream->WriteU32(surfaceCount) && stream->WriteU32(firstCount) &&
      stream->WriteU32(secondCount);
  if (result) {
    NwRecord descriptor(1);
    result = stream->WriteU32(1);
  }
  if (!result) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return result != 0;
}

// Read metadata, then consume each counted layer record through the actual descriptor and virtual occupant creator.
// Metadata errors fail initialization; individual layer errors abort that record and continue to the next layer.
bool NwManager::Init(CtCity *target, RsDatabaseSegment *segment) {
  if (!surfaceLayer || !undergroundOne || !undergroundTwo)
    return true;
  u32 counts[3] = {}, codec = 0;
  u16 version;
  u8 result;
  RsKey key = {kRsCityLayerRecordType, kNwManagerLayerType, 0};
  {
    NwRecordFrame frame(target, segment, key);
    result = frame.valid;
    RsStream *stream = frame.Stream();
    if (result)
      result = stream != nullptr;
    version = frame.version;
    if (version == 4) {
      result = result && stream->ReadU32(counts) && stream->ReadU32(counts + 1) && stream->ReadU32(counts + 2) &&
          stream->ReadU32(&codec);
    }
    if (!result) {
      if (frame.record)
        frame.record->aborted = 1;
      frame.valid = 0;
    }
  }
  if (!result)
    return false;
  if (version != 4)
    return result != 0;
  void *output = nullptr;
  NwRecord descriptor(1);
  for (u32 layer = 0; layer < 3; ++layer) {
    if (!counts[layer])
      continue;
    key.instance = layer + 1;
    NwRecordFrame frame(target, segment, key);
    result = frame.valid;
    RsStream *stream = frame.Stream();
    if (result)
      result = stream != nullptr;
    for (u32 index = 0; index < counts[layer] && result; ++index) {
      descriptor.Clear();
      result = descriptor.SerialRead(city, codec, stream);
      if (result) {
        if (CreateOccupant(&descriptor, IID_ScOccupant, &output)) {
          ScOccupantManager *owner = layer == 0 ? surfaceLayer : layer == 1 ? undergroundOne : undergroundTwo;
          owner->InsertOccupant(static_cast<ScOccupant *>(output));
        }
        if (output) {
          static_cast<ScOccupant *>(output)->Release();
          output = nullptr;
        }
      }
    }
    if (!result) {
      if (frame.record)
        frame.record->aborted = 1;
      frame.valid = 0;
    }
  }
  return true;
}

// Copy the genuine filesystem data directory when present, including the original borrowed service callbacks.
static void NwDataDirectory(ScString *directory) {
  RsFileSystem *system = NwGetFileSystem();
  if (system) {
    const char *data = system->DataDirectory();
    if (data)
      directory->FromChar(data);
  }
}

static void NwCatalogDirectory(ScString *directory) {
  NwDataDirectory(directory);
  directory->Append("Res\\TilingRules\\");
}

// Construct all seven file owners and empty catalogs before reading ROAD/HWAY/RAIL/SUBW/POWR/PIPE/DIAG in order.
// Ordinary file failures do not reject startup; dispose file owners in that same read order and return one.
u8 NwManager::LoadCatalogs() {
  static const char *names[7] = {"ROAD_GRND_Set.txt", "HWAY_GRND_Set.txt", "RAIL_GRND_Set.txt", "SUBW_GRND_Set.txt",
      "PIPE_GRND_Set.txt", "POWR_GRND_Set.txt", "DIAG_Set.txt"};
  static const u8 order[7] = {0, 1, 2, 3, 5, 4, 6};
  ScString directory;
  NwCatalogDirectory(&directory);
  RsFile *files[7];
  for (u32 i = 0; i < 7; ++i) {
    ScString name(directory);
    name.Append(names[i]);
    files[i] = new RsFile(name);
  }
  for (u32 i = 0; i < 6; ++i)
    NwPieceCatalogs[i] = new NwPieceCatalog{};
  NwDiagonalCatalog = new NwPieceCatalog{};
  // This native boundary defines the one byte that the source reader deliberately leaves untouched.
  // Every defined nonempty first read overwrites it before parsing; later reads preserve actual prior history.
  char scratch[kNwCatalogScratchBufferBytes] = {};
  for (u32 i = 0; i < 7; ++i) {
    u32 index = order[i];
    GrokFileTileSet(files[index], scratch, index < 6 ? NwPieceCatalogs[index] : NwDiagonalCatalog);
  }
  for (u32 i = 0; i < 7; ++i)
    delete files[order[i]];
  return 1;
}

// Construct every original path before loading final, simple and complex files into their existing global owners.
// Use ROAD/RAIL/HWAY/SUBW/PIPE/POWR order, retain failed-load owners and return the final publication byte.
u8 NwManager::LoadRules() {
  static const char *files[3][6] = {{"ROAD_GRND_final.txt", "HWAY_GRND_final.txt", "RAIL_GRND_final.txt",
                                        "SUBW_GRND_final.txt", "PIPE_GRND_final.txt", "POWR_GRND_final.txt"},
      {"ROAD_GRND_SimpleRules.txt", "HWAY_GRND_SIMPLERULES.txt", "RAIL_GRND_SIMPLERULES.txt",
          "SUBW_GRND_SIMPLERULES.txt", "PIPE_GRND_SIMPLERULES.txt", "POWR_GRND_SIMPLERULES.txt"},
      {"ROAD_GRND_ComplexRules.txt", "HWAY_GRND_COMPLEXRULES.txt", "RAIL_GRND_COMPLEXRULES.txt",
          "SUBW_GRND_COMPLEXRULES.txt", "PIPE_GRND_COMPLEXRULES.txt", "POWR_GRND_COMPLEXRULES.txt"}};
  static const u8 networks[6] = {0, 2, 1, 3, 4, 5}, construction[3] = {1, 2, 0};
  NwRuleCollection **collections[3] = {NwFinalRules, NwSimpleRules, NwComplexRules};
  ScString directory, names[3][6];
  NwCatalogDirectory(&directory);
  for (u32 i = 0; i < 6; ++i) {
    u32 network = networks[i];
    for (u32 j = 0; j < 3; ++j) {
      u32 family = construction[j];
      names[family][network] = directory;
      names[family][network].Append(files[family][network]);
    }
  }
  u8 result = 0;
  for (u32 family = 0; family < 3; ++family)
    for (u32 i = 0; i < 6; ++i) {
      u32 network = networks[i];
      result = collections[family][network]->AddFile(names[family][network]);
    }
  return result;
}

// Construct all twenty-three paths and file owners before publishing empty conversion and piece-list tables.
// Read each family with power before pipe, ignore individual file failures, then delete files in physical order.
u8 NwManager::LoadData() {
  static const char *files[23] = {"ROAD_GRND_Convert.txt", "HWAY_GRND_Convert.txt", "RAIL_GRND_Convert.txt",
      "SUBW_GRND_Convert.txt", "PIPE_GRND_Convert.txt", "POWR_GRND_Convert.txt", "ROAD_GRND_Complex_Convert.txt",
      "HWAY_GRND_Complex_Convert.txt", "RAIL_GRND_Complex_Convert.txt", "SUBW_GRND_Complex_Convert.txt",
      "PIPE_GRND_Complex_Convert.txt", "POWR_GRND_Complex_Convert.txt", "Collapse.txt", "Road_GRND_Protected.txt",
      "HWAY_GRND_Protected.txt", "RAIL_GRND_Protected.txt", "SUBW_GRND_Protected.txt", "PIPE_GRND_Protected.txt",
      "POWR_GRND_Protected.txt", "ROAD_GRND_Bridges.txt", "HWAY_GRND_Bridges.txt", "RAIL_GRND_Bridges.txt",
      "POWR_GRND_Bridges.txt"};
  static const u8 construction[23] = {12, 0, 6, 13, 19, 2, 8, 15, 21, 1, 7, 14, 20, 3, 9, 16, 4, 10, 17, 5, 11, 18, 22};
  static const u8 order[6] = {0, 1, 2, 3, 5, 4};
  ScString directory, names[23];
  NwCatalogDirectory(&directory);
  for (u32 i = 0; i < 23; ++i) {
    u32 index = construction[i];
    names[index] = directory;
    names[index].Append(files[index]);
  }
  RsFile *owners[23];
  for (u32 i = 0; i < 23; ++i)
    owners[i] = new RsFile(names[i]);
  NwConversions **conversions[2] = {NwSimpleConversions, NwComplexConversions};
  for (u32 family = 0; family < 2; ++family)
    for (u32 i = 0; i < 6; ++i)
      conversions[family][i] = new NwConversions{};
  NwCollapsePieces = new NwPieceList{};
  for (u32 i = 0; i < 6; ++i)
    NwProtectedTables[i] = new NwPieceList{};
  for (u32 i = 0; i < 4; ++i)
    NwBridgePieces[i] = new NwPieceList{};
  // Define the unavailable initial byte-zero history at this native boundary, as in Set catalog startup.
  char scratch[kNwCatalogScratchBufferBytes] = {};
  for (u32 family = 0; family < 2; ++family)
    for (u32 i = 0; i < 6; ++i) {
      u32 index = order[i];
      GrokFileConvertSets(owners[family * 6 + index], scratch, conversions[family][index]);
    }
  GrokFileProtectedSets(owners[12], scratch, NwCollapsePieces);
  for (u32 i = 0; i < 6; ++i) {
    u32 index = order[i];
    GrokFileProtectedSets(owners[13 + index], scratch, NwProtectedTables[index]);
  }
  for (u32 i = 0; i < 4; ++i)
    GrokFileProtectedSets(owners[19 + i], scratch, NwBridgePieces[i]);
  for (u32 i = 0; i < 23; ++i)
    delete owners[i];
  return 1;
}

// Register the actual command factory, allocate every rule owner, and run the three real resource-loading families.
// On success publish the concrete predicates and generator before acquiring the original tiling hookup references.
bool NwManager::StaticInit(CtDefinition *) {
  NwCommandFactory *commands = new NwCommandFactory;
  commands->AddRef();
  void *registry = nullptr;
  NwGetFrameWork()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &registry);
  static_cast<ScCommandFactory *>(registry)->AddCommandFactory(commands);
  commands->Release();
  static const u8 order[6] = {0, 2, 1, 3, 4, 5};
  NwRuleCollection **collections[3] = {NwFinalRules, NwSimpleRules, NwComplexRules};
  for (u32 family = 0; family < 3; ++family)
    for (u32 i = 0; i < 6; ++i)
      collections[family][order[i]] = new NwRuleCollection;
  bool result = false;
  {
    ScString directory;
    NwDataDirectory(&directory);
    if (LoadCatalogs() && LoadData() && LoadRules()) {
      debugRules = 0;
      NwPredicates[0] = new NwRoadPredicate;
      NwPredicates[1] = new NwHighwayPredicate;
      NwPredicates[2] = new NwRailPredicate;
      NwPredicates[3] = new NwSubwayPredicate;
      NwPredicates[5] = new NwPowerPredicate;
      NwPredicates[4] = new NwPipePredicate;
      NwPredicates[6] = new NwNetworkPredicate;
      NwPredicates[7] = new NwTransitPredicate;
      NwPredicates[8] = new NwFloraPredicate;
      NwPredicates[9] = new NwNotRoadPredicate;
      NwPredicates[10] = new NwNotHighwayPredicate;
      NwPredicates[11] = new NwNotRailPredicate;
      NwPredicates[12] = new NwNotSubwayPredicate;
      NwPredicates[14] = new NwNotPowerPredicate;
      NwPredicates[13] = new NwNotPipePredicate;
      NwPredicates[15] = new NwNotNetworkPredicate;
      NwPredicates[16] = new NwRoadOverlap;
      NwPredicates[17] = new NwRailOverlap;
      NwPredicates[19] = new NwHighwayOverlap;
      NwPredicates[18] = new NwPowerOverlap;
      NwPredicates[21] = new NwSubwayOverlap;
      NwPredicates[20] = new NwPipeOverlap;
      NwPredicates[22] = new NwRoadJoin;
      NwPredicates[23] = new NwRailJoin;
      NwPredicates[25] = new NwHighwayJoin;
      NwPredicates[24] = new NwPowerJoin;
      NwPredicates[27] = new NwSubwayJoin;
      NwPredicates[26] = new NwPipeJoin;
      NwPredicates[28] = new NwRotationA;
      NwPredicates[29] = new NwRotationB;
      NwPredicates[31] = new NwRotationC;
      NwPredicates[30] = new NwRotationD;
      NwPredicates[33] = new NwRotationE;
      NwPredicates[32] = new NwRotationF;
      NwPredicates[34] = new NwRotationG;
      NwPredicates[35] = new NwRotationH;
      NwPredicates[37] = new NwRotationI;
      NwPredicates[36] = new NwRotationJ;
      NwPredicates[39] = new NwRotationK;
      NwPredicates[38] = new NwRotationL;
      NwPredicates[40] = new NwDecorEligible;
      NwPredicates[41] = new NwDecorated;
      NwDecorationRandom = new NwRandom(kScRandomSeedFromClock);
      if (NwGetCom()->GetClassObject(kNwTrafficBlockFactoryClassId, IID_NwTrafficBlockFactory, &NwTrafficFactoryCache))
        QueryPredicate(&NwTrafficPredicate, IID_OcIteratorTest, 1);
      else
        NwTrafficFactoryCache = nullptr;
      result = true;
    }
  }
  if (registry)
    static_cast<ScCommandFactory *>(registry)->Release();
  return result;
}

NwPredicate *NwPredicates[42] = {};
void *NwTrafficFactoryCache = nullptr;
void *NwTrafficPredicate = nullptr;

// Invalid selectors clear the output; a failed interface query deletes the selected owner without clearing its global.
bool NwManager::QueryPredicate(void **out, u32 iid, i32 selector) {
  static const u8 indices[34] = {6, 7, 0, 1, 2, 3, 4, 5, 6, 6, 16, 19, 17, 20, 21, 18, 28, 31, 29, 32, 33, 30, 34, 37,
      35, 38, 39, 36, 22, 25, 23, 26, 27, 24};
  if (u32(selector) >= 34) {
    *out = nullptr;
    return false;
  }
  NwPredicate *predicate = NwPredicates[indices[selector]];
  if (predicate->QueryInterface(iid, out))
    return true;
  delete predicate;
  return false;
}

// Release the actual global rule, catalog, predicate, conversion and piece-list owners in their original order.
// Release the hookup references first and clear live groups before file destruction, retaining borrowed service caches.
bool NwManager::StaticShutdown() {
  if (NwTrafficFactoryCache) {
    static_cast<ScUnknown *>(NwTrafficFactoryCache)->Release();
    NwTrafficFactoryCache = nullptr;
  }
  static_cast<OcIteratorTest *>(NwTrafficPredicate)->Release();
  NwTrafficPredicate = nullptr;
  static const u8 order[6] = {0, 1, 2, 3, 5, 4};
  static const u8 predicates[42] = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 11, 12, 14, 13, 15, 16, 17, 18, 19, 20, 21, 22,
      23, 24, 25, 26, 27, 34, 35, 36, 37, 38, 39, 28, 29, 30, 31, 32, 33, 40, 41};
  NwRuleCollection **collections[3] = {NwFinalRules, NwSimpleRules, NwComplexRules};
  for (u32 family = 0; family < 3; ++family)
    for (u32 i = 0; i < 6; ++i) {
      u32 index = order[i];
      NwRuleCollection *collection = collections[family][index];
      if (!collection)
        continue;
      u32 count = collection->files.size;
      for (u32 file = 0; file < count; ++file) {
        NwRuleTable *rules = &collection->files.data[file]->rules;
        for (NwRule *rule = rules->begin; rule != rules->end; ++rule) {
          for (NwRuleGroup *group = rule->begin; group != rule->end; ++group) {
            ScArray<NwRuleRecord> *records[2] = {&group->results.records, &group->conditions.records};
            for (u32 part = 0; part < 2; ++part) {
              free(records[part]->data);
              records[part]->data = nullptr;
              records[part]->size = records[part]->capacity = 0;
            }
          }
          rule->end = rule->begin;
          rule->selector = 0;
        }
      }
      delete collections[family][index];
      collections[family][index] = nullptr;
    }
  for (u32 i = 0; i < 7; ++i) {
    u32 index = i < 6 ? order[i] : 6;
    NwPieceCatalog **catalog = index < 6 ? NwPieceCatalogs + index : &NwDiagonalCatalog;
    if (*catalog) {
      delete[] (*catalog)->begin;
      delete *catalog;
      *catalog = nullptr;
    }
  }
  for (u32 i = 0; i < 42; ++i) {
    u32 index = predicates[i];
    if (NwPredicates[index]) {
      delete NwPredicates[index];
      NwPredicates[index] = nullptr;
    }
  }
  NwConversions **conversions[2] = {NwSimpleConversions, NwComplexConversions};
  for (u32 family = 0; family < 2; ++family)
    for (u32 i = 0; i < 6; ++i) {
      u32 index = order[i];
      if (conversions[family][index]) {
        delete[] conversions[family][index]->begin;
        delete conversions[family][index];
        conversions[family][index] = nullptr;
      }
    }
  for (u32 i = 0; i < 6; ++i) {
    u32 index = order[i];
    if (NwProtectedTables[index]) {
      delete[] NwProtectedTables[index]->begin;
      delete NwProtectedTables[index];
      NwProtectedTables[index] = nullptr;
    }
  }
  NwPieceList **pieces[5] = {
      &NwCollapsePieces, NwBridgePieces, NwBridgePieces + 1, NwBridgePieces + 2, NwBridgePieces + 3};
  for (u32 i = 0; i < 5; ++i)
    if (*pieces[i]) {
      delete[] (*pieces[i])->begin;
      delete *pieces[i];
      *pieces[i] = nullptr;
    }
  ScDelete(&NwDecorationRandom);
  return 1;
}

// Apply support collapse with an owned temporary position list, retaining the caller's bounds and prompt arguments.
// The list is destroyed only after the complete recursive collapse operation returns.
void NwManager::CollapseSupport(
    ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation, GeBounds *affected, bool prompt) {
  NwPositionList removed;
  CollapseSupport(layer, position, piece, rotation, affected, true, &removed, prompt);
}

// Obtain any neighbor-removal decision, then follow paired supports or collapse an ordinary connected structure.
// Paired supports are scanned up to the terrain boundary, retaining sprite rotation and altitude matching.
void NwManager::CollapseSupport(ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation, GeBounds *affected,
    bool apply, NwPositionList *removed, bool prompt) {
  i32 decision = -1;
  ScOccupant *occupant = nullptr;
  if (apply &&
      layer->GetOccupantAtCell(
          u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &occupant)) {
    ShouldWeRemoveConnection(occupant, &decision, prompt);
    occupant->Release();
    occupant = nullptr;
  }
  static const u32 kNwPairedSupportPieces[6][4] = {{0x4000, 0x3f00, 0x4002, 0x3f02},
      {0x466800, 0x466300, 0x466802, 0x466302}, {0x466e00, 0x466d00, 0x466e02, 0x466d02},
      {0x466c00, 0x466b00, 0x466c02, 0x466b02}, {0x46ab00, 0x46aa00, 0x46ab02, 0x46aa02},
      {0x46a900, 0x46a800, 0x46a902, 0x46a802}};
  u32 packed = (u32(piece) << kNwPieceCodeShift) + rotation, direction = 0;
  bool support = false;
  while (direction < 4 && !support) {
    for (u32 family = 0; family < 6; ++family)
      if (packed == kNwPairedSupportPieces[family][direction])
        support = true;
    ++direction;
  }
  if (!support) {
    CollapseStructure(layer, position, piece, rotation, affected, &decision, apply, removed);
    return;
  }
  u16 pairedPiece = piece;
  // Installed code advances the direction index before its pair lookups. Its past-end comparisons are all false;
  // skip those reads instead of reading adjacent tables or the following string as an out-of-bounds array element.
  if (direction < 4) {
    if (packed == kNwPairedSupportPieces[3][direction])
      pairedPiece = u16(kNwPairedSupportPieces[2][direction] >> kNwPieceCodeShift);
    if (packed == kNwPairedSupportPieces[2][direction])
      pairedPiece = u16(kNwPairedSupportPieces[3][direction] >> kNwPieceCodeShift);
    if (packed == kNwPairedSupportPieces[5][direction])
      pairedPiece = u16(kNwPairedSupportPieces[4][direction] >> kNwPieceCodeShift);
    if (packed == kNwPairedSupportPieces[4][direction])
      pairedPiece = u16(kNwPairedSupportPieces[5][direction] >> kNwPieceCodeShift);
  }
  if (pairedPiece != piece)
    CollapseStructure(layer, position, piece, rotation, affected, &decision, apply, removed);
  i32 dx = 0, dz = 0;
  switch (direction) {
  case 1:
    dx = -1;
    break;
  case 2:
    dz = -1;
    break;
  case 3:
    dx = 1;
    break;
  default:
    dz = direction == 4;
    break;
  }
  i32 x = (position.x >> kCtMicroCoordinateShift) + dx, z = (position.z >> kCtMicroCoordinateShift) + dz;
  u8 reversed = rotation ^ 2;
  bool found = false;
  CtCoord current{INT32_MAX, INT32_MAX, INT32_MAX};
  do {
    if (!terrain->InCellBounds(u32(x), u32(z)))
      break;
    current = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
    if (layer->GetOccupant(current, &occupant)) {
      u8 actualRotation = 0;
      occupant->GetSpriteInst()->GetRotationOffset(&actualRotation);
      u32 actualPiece = GetTileId(occupant);
      bool sameRotation = reversed == actualRotation;
      bool sameAltitude = occupant->GetAlt() == (position.altitude >> kCtMicroCoordinateShift);
      if (pairedPiece == actualPiece && sameRotation && sameAltitude) {
        found = true;
        MyRemoveOccupant(layer, u32(x), u32(z), affected, &decision, apply, removed);
        GeBounds ignored;
        CollapseStructure(layer, current, pairedPiece, reversed, &ignored, &decision, apply, removed);
      } else {
        found = false;
      }
      occupant->Release();
      occupant = nullptr;
    }
    x += dx;
    z += dz;
  } while (!found);
}

// Find the removed piece's collapse group and propagate its nearby connected support footprint before removal.
// Special corner pieces, three-way supports and directional groups retain their original visit and callback order.
void NwManager::CollapseStructure(ScOccupantManager *layer, CtCoord position, u16 piece, u8 rotation,
    GeBounds *affected, i32 *decision, bool apply, NwPositionList *removed) {
  affected->minimum = affected->maximum = position;
  i32 centerX = position.x >> kCtMicroCoordinateShift, centerZ = position.z >> kCtMicroCoordinateShift;
  if (piece == 15052) {
    if (rotation == 0)
      MyRemoveOccupantIfMatch(layer, u32(centerX - 1), u32(centerZ - 1), 15052, 2, decision, apply, removed);
    if (rotation == 2)
      MyRemoveOccupantIfMatch(layer, u32(centerX + 1), u32(centerZ + 1), 15052, 0, decision, apply, removed);
  }
  if (piece == 11223) {
    if (rotation == 0)
      MyRemoveOccupantIfMatch(layer, u32(centerX - 1), u32(centerZ + 1), 11223, 2, decision, apply, removed);
    if (rotation == 2)
      MyRemoveOccupantIfMatch(layer, u32(centerX + 1), u32(centerZ - 1), 11223, 0, decision, apply, removed);
  }
  if (!NwCollapsePieces || NwCollapsePieces->begin == NwCollapsePieces->end)
    return;
  const u32 *cursor = NwCollapsePieces->begin, *end = NwCollapsePieces->end;
  u32 pieces[30], pieceCount = 0, group = 0;
  i32 forcedGroup = piece == 11224 || piece == 15106 ? 18 : -1;
  bool found = false;
  while (cursor != end) {
    if (i32(group) == forcedGroup) {
      found = true;
      break;
    }
    if (found)
      break;
    u32 recordSize = *cursor++;
    ++group;
    if (recordSize >= 30)
      return;
    pieceCount = 0;
    for (u32 remaining = recordSize - 1; cursor != end && remaining; --remaining) {
      u32 candidate = *cursor++;
      pieces[pieceCount++] = candidate;
      if (candidate == piece && forcedGroup < 0)
        found = true;
    }
  }
  if (i32(group) == forcedGroup)
    found = true;
  if (!found)
    return;
  // Search only the retained resource group. Membership uses full piece IDs and has no game callbacks.
  auto matches = [&](u32 candidate) {
    for (u32 index = 0; index < pieceCount; ++index)
      if (pieces[index] == candidate)
        return true;
    return false;
  };
  CtCoord current{INT32_MAX, INT32_MAX, INT32_MAX};
  i32 width = i32(city->CellCountX()), height = i32(city->CellCountZ());
  u32 present[16]{}, connected[16]{};
  ScOccupant *occupant = nullptr;
  for (i32 x = centerX - 8; x < centerX + 8; ++x)
    for (i32 z = centerZ - 8; z < centerZ + 8; ++z) {
      if (x < 0 || x >= width || z < 0 || z >= height)
        continue;
      current = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
      if (layer->GetOccupant(current, &occupant)) {
        if (matches(GetTileId(occupant)))
          present[z - centerZ + 8] |= 1u << (x - centerX + 8);
        occupant->Release();
        occupant = nullptr;
      }
    }
  connected[8] = 0x100;
  for (u32 pass = 0; pass < 32; ++pass) {
    bool changed = false;
    for (u32 row = 0; row < 16; ++row) {
      u32 old = connected[row], value = present[row] & (old | (old << 1) | (old >> 1));
      changed = changed || value != old;
      connected[row] = value;
    }
    for (u32 row = 1; row < 15; ++row) {
      u32 old = connected[row], value = present[row] & (connected[row + 1] | old | connected[row - 1]);
      changed = changed || value != old;
      connected[row] = value;
    }
    if (!changed)
      break;
  }
  static const i32 directions[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  // Walk past the supplied starting cell while matching support pieces remain, preserving the shared lookup slot.
  // Each accepted cell uses the original removal helper before releasing the caller's retained occupant.
  auto removeRun = [&](i32 x, i32 z, i32 dx, i32 dz) {
    bool same = true;
    while (same) {
      x += dx;
      z += dz;
      current = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
      if (!layer->GetOccupant(current, &occupant))
        break;
      same = matches(GetTileId(occupant));
      if (same)
        MyRemoveOccupant(layer, u32(x), u32(z), affected, decision, apply, removed);
      occupant->Release();
      occupant = nullptr;
    }
  };
  if (group == 3) {
    u16 lengths[4];
    for (i32 direction = 3; direction >= 0; --direction) {
      lengths[direction] = 0;
      i32 x = centerX, z = centerZ;
      bool same = true;
      while (same && lengths[direction] < 3) {
        x += directions[direction][0];
        z += directions[direction][1];
        current = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
        if (!layer->GetOccupant(current, &occupant))
          break;
        same = matches(GetTileId(occupant));
        if (same)
          ++lengths[direction];
        occupant->Release();
        occupant = nullptr;
      }
    }
    i32 longest = 3;
    while (longest >= 0 && lengths[longest] < 2)
      --longest;
    if (longest >= 0) {
      i32 side = longest ^ 2;
      if (lengths[side])
        side = longest ^ 3;
      i32 offset = side ^ 1, x = centerX + directions[offset][0], z = centerZ + directions[offset][1];
      removeRun(x, z, directions[longest][0], directions[longest][1]);
      removeRun(x, z, directions[longest ^ 1][0], directions[longest ^ 1][1]);
    }
  }
  bool scanArea = group != 16 && group != 17, scanZ = group != 16, scanX = group != 17;
  for (i32 direction = 3; direction >= 0; --direction)
    if (direction < 2 ? scanZ : scanX)
      removeRun(centerX, centerZ, directions[direction][0], directions[direction][1]);
  if (scanArea)
    for (i32 x = centerX - 8; x < centerX + 8; ++x)
      for (i32 z = centerZ - 8; z < centerZ + 8; ++z)
        if (connected[z - centerZ + 8] & (1u << (x - centerX + 8)))
          MyRemoveOccupant(layer, u32(x), u32(z), affected, decision, apply, removed);
}

// Expand the affected bounds and collect a removable network occupant, optionally removing it and its neighbor links.
// Tunnel entrances also clear their terrain block after the apply phase, even if the decision prevented removal.
void NwManager::MyRemoveOccupant(
    ScOccupantManager *layer, u32 x, u32 z, GeBounds *affected, i32 *decision, bool apply, NwPositionList *removed) {
  CtCoord position{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  if (!layer->GetOccupant(position, &occupant))
    return;
  if (occupant->IsGeomFlag(kOcGeomNetwork) && occupant->CanUserRemove()) {
    affected->UnionXZ(*affected, position);
    u32 piece = GetTileId(occupant);
    if (apply) {
      ShouldWeRemoveConnection(occupant, decision, false);
      if (*decision)
        layer->RemoveOccupant(occupant);
      static const u32 tunnelPieces[16] = {0x53, 0x468a, 0x4694, 0x3eb1, 0x3eaa, 0x2c0b, 0x5b, 0x57, 0x2c24, 0x4689,
          0x4693, 0x3eab, 0x5a, 0x2c01, 0x3ebd, 0x3ebe};
      for (u32 tunnel : tunnelPieces)
        if (piece == tunnel)
          terrain->BlockCell(x, z, false);
    }
    removed->Append(position);
  }
  occupant->Release();
}

// Expand the affected bounds for a removable network occupant and perform the noninteractive neighbor cleanup.
// This overload neither collects positions nor clears tunnel terrain blocks.
void NwManager::MyRemoveOccupant(ScOccupantManager *layer, u32 x, u32 z, GeBounds *affected, i32 *decision) {
  CtCoord position{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  if (!layer->GetOccupant(position, &occupant))
    return;
  if (occupant->IsGeomFlag(kOcGeomNetwork) && occupant->CanUserRemove()) {
    affected->UnionXZ(*affected, position);
    ShouldWeRemoveConnection(occupant, decision, false);
    if (*decision)
      layer->RemoveOccupant(occupant);
  }
  occupant->Release();
}

// Collect only a removable network occupant whose full piece identifier and sprite rotation match the request.
// Applying the operation removes neighbor links before the occupant; the retained occupant is always released last.
void NwManager::MyRemoveOccupantIfMatch(ScOccupantManager *layer, u32 x, u32 z, u32 piece, i32 rotation, i32 *decision,
    bool apply, NwPositionList *removed) {
  CtCoord position{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  if (!terrain->InCellBounds(x, z) || !layer->GetOccupant(position, &occupant))
    return;
  if (occupant->IsGeomFlag(kOcGeomNetwork) && occupant->CanUserRemove()) {
    u8 actualRotation;
    occupant->GetSpriteInst()->GetRotationOffset(&actualRotation);
    u32 actualPiece = GetTileId(occupant);
    if (piece == actualPiece && rotation == actualRotation) {
      if (apply) {
        ShouldWeRemoveConnection(occupant, decision, false);
        if (*decision)
          layer->RemoveOccupant(occupant);
      }
      removed->Append(position);
    }
  }
  occupant->Release();
}

// Query all neighbor connections carried by the occupant, optionally asking before removing those links.
// The integer decision remains true for a piece without neighbor links; cancellation preserves every link.
void NwManager::ShouldWeRemoveConnection(ScOccupant *occupant, i32 *decision, bool prompt) {
  *decision = 1;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  u32 flags = occupant->GetGeomFlag();
  static const u32 masks[6] = {kOcGeomRoad, kOcGeomHighway, kOcGeomRail, kOcGeomSubway, kOcGeomPipe, kOcGeomPower};
  static const u32 kinds[6] = {1, 4, 2, 6, 5, 3};
  bool connected = false;
  for (u32 index = 0; index < 6; ++index)
    if (flags & masks[index])
      connected = neighbors->HasConnection(kinds[index], i32(x), i32(z)) || connected;
  if (!connected)
    return;
  *decision = prompt ? ConfirmConstruction(kNwConfirmRemoveNeighborText, 1) != 0 : 1;
  if (*decision > 0)
    for (u32 index = 0; index < 6; ++index)
      if (flags & masks[index])
        neighbors->RemoveConnection(kinds[index], i32(x), i32(z));
}

// Ask permission for a network occupant before testing its live user-removable flag and removing it from its layer.
// A refused category, cancelled confirmation or nonremovable occupant leaves the layer untouched.
u8 NwManager::RemoveConnectionInteractive(ScOccupantManager *manager, ScOccupant *occupant) {
  if (!occupant->IsGeomFlag(kOcGeomNetwork))
    return 0;
  i32 decision = 0;
  ShouldWeRemoveConnection(occupant, &decision, true);
  if (!decision || !occupant->CanUserRemove())
    return 0;
  manager->RemoveOccupant(occupant);
  return 1;
}

// Shut down static network data, then initialize it again with the application's current city definition.
// The shutdown result is ignored; the final initialization result is returned directly.
bool NwManager::ReloadDefinition() {
  StaticShutdown();
  CtDefinition *definition = NwGetCityDefinition();
  return StaticInit(definition);
}

u32 NwManager::GetRoadCount() const {
  return roadCount;
}

u32 NwManager::GetHighwayCount() const {
  return highwayCount;
}

u32 NwManager::GetRailCount() const {
  return railCount;
}

u32 NwManager::GetSubwayCount() const {
  return subwayCount;
}

u32 NwManager::GetPipeCount() const {
  return pipeCount;
}

u32 NwManager::GetPowerCount() const {
  return powerCount;
}

// Count damaged road occupants through the real surface iterator, then divide by the live road count after its release.
// Preserve unsigned count wrap and the original floating-point result when the road count is zero.
f64 NwManager::GetTransitDamage() const {
  OcIterator *iterator = nullptr;
  surfaceLayer->GetOccupants(&iterator, &bounds, NwPredicates[41]);
  u32 count = 0;
  for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next())
    ++count;
  iterator->Shutdown();
  iterator->Release();
  return f64(count) / f64(roadCount);
}

// Selection deliberately permits one extra addition; replacement ordering and reference effects remain distinct by
// branch.
void NwManager::SetTransitDamage(f32 fraction) {
  u32 maxX = u32(bounds.maximum.x >> kCtMicroCoordinateShift), maxZ = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  u32 eligible = 0, decorated = 0, x = 0;
  ScOccupant *occupant = nullptr;
  do {
    u32 z = 0;
    do {
      if (surfaceLayer->GetOccupantAtCell(x, z, &occupant)) {
        if (NwPredicates[0]->Test(occupant))
          ++eligible;
        if (NwPredicates[41]->Test(occupant))
          ++decorated;
        occupant->Release();
      }
      ++z;
    } while (z <= maxZ);
    ++x;
  } while (x <= maxX);
  u32 target = u32(i64(f64(eligible) * fraction));
  if (target == decorated)
    return;
  if (target > decorated) {
    u32 needed = target - decorated, chosen = 0, skipped = 0;
    i32 interval = i32(eligible / needed - 1);
    if (interval < 0)
      interval = 0;
    u32 threshold = interval > 0 ? NwDecorationRandom->Bounded(u32(interval)) : 0;
    OcIterator *iterator = nullptr;
    surfaceLayer->GetOccupants(&iterator, &bounds, NwPredicates[0]);
    ScArray<ScOccupant *> picked;
    for (ScOccupant *current = iterator->Current(); current; current = iterator->Next()) {
      if (skipped < threshold)
        ++skipped;
      else {
        ScOccupant *entry = iterator->Current();
        if (NwPredicates[40]->Test(entry)) {
          entry->AddRef();
          *picked.Append() = entry;
          ++chosen;
          threshold = interval > 0 ? NwDecorationRandom->Bounded(u32(interval)) : 0;
          skipped = 0;
        }
      }
      if (chosen > needed)
        break;
    }
    iterator->Shutdown();
    iterator->Release();
    iterator = nullptr;
    u32 rotation = needed;
    for (u32 i = 0; i < picked.size; ++i) {
      ScOccupant *entry = picked.data[i];
      u32 piece = GetTileId(entry), replacement;
      if (piece == 29)
        replacement = 15040 + NwDecorationRandom->Next() % 3;
      else if (piece == 35)
        replacement = 9051;
      else
        continue;
      SpSprite *sprite = entry->GetSpriteInst();
      if (sprite)
        sprite->GetRotationOffset(reinterpret_cast<u8 *>(&rotation));
      void *created = nullptr;
      if (entry->CanSimRemove() && CreateOccupant(&created, IID_ScOccupant, replacement, u8(rotation))) {
        CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
        entry->GetLocation(&point);
        static_cast<ScOccupant *>(created)->SetLocation(point);
        SpSprite *newSprite = static_cast<ScOccupant *>(created)->GetSpriteInst();
        if (newSprite)
          newSprite->SetRotationOffset(u8(rotation));
        surfaceLayer->RemoveOccupant(entry);
        surfaceLayer->InsertOccupant(static_cast<ScOccupant *>(created));
        void *connection = nullptr;
        if (static_cast<ScOccupant *>(created)->QueryInterface(IID_NwConnection, &connection)) {
          static_cast<NwConnection *>(connection)->SetDamage(1);
          static_cast<NwConnection *>(connection)->Release();
          connection = nullptr;
        }
        static_cast<ScOccupant *>(created)->Release();
      }
    }
    for (u32 i = 0; i < picked.size; ++i)
      picked.data[i]->Release();
    picked.Clear();
  } else {
    u32 remaining = decorated - target;
    if (i32(remaining) > 0) {
      i32 interval = i32(decorated / remaining - 1);
      if (interval < 0)
        interval = 0;
      u32 threshold = interval > 0 ? NwDecorationRandom->Bounded(u32(interval)) : 0, skipped = 0;
      OcIterator *iterator = nullptr;
      surfaceLayer->GetOccupants(&iterator, &bounds, NwPredicates[41]);
      ScArray<ScOccupant *> picked;
      for (ScOccupant *current = iterator->Current(); current; current = iterator->Next()) {
        if (skipped < threshold)
          ++skipped;
        else {
          ScOccupant *entry = iterator->Current();
          entry->AddRef();
          *picked.Append() = entry;
          threshold = interval > 0 ? NwDecorationRandom->Bounded(u32(interval)) : 0;
          skipped = 0;
        }
      }
      iterator->Shutdown();
      iterator->Release();
      iterator = nullptr;
      u32 rotation = 0;
      for (u32 i = 0; i < picked.size; ++i) {
        ScOccupant *entry = picked.data[i];
        rotation &= kNwRotationWithoutLowByte;
        CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
        u32 piece = GetTileId(entry), replacement;
        if (piece == 15040 || piece == 15041 || piece == 15042)
          replacement = 29;
        else if (piece == 9051)
          replacement = 35;
        else
          continue;
        entry->GetSpriteInst()->GetRotationOffset(reinterpret_cast<u8 *>(&rotation));
        entry->GetLocation(&point);
        void *created = nullptr;
        if (entry->CanSimRemove() && CreateOccupant(&created, IID_ScOccupant, replacement, u8(rotation))) {
          static_cast<ScOccupant *>(created)->GetSpriteInst()->SetRotationOffset(u8(rotation));
          surfaceLayer->RemoveOccupant(entry);
          static_cast<ScOccupant *>(created)->SetLocation(point);
          surfaceLayer->InsertOccupant(static_cast<ScOccupant *>(created));
          static_cast<ScOccupant *>(created)->Release();
          created = nullptr;
        }
        entry->Release();
        picked.data[i] = nullptr;
      }
      picked.Clear();
    }
  }
  damageFraction = fraction;
}

static const i32 NwPreparationCorners[4][2] = {{0, 0}, {-1, -1}, {0, -1}, {-1, 0}};

// Validate every changed vertex's four cells across the three layers before preparing road terrain.
// Execution queues matching networks once, clears removable surface occupants and publishes zone resets at the vertex.
u8 NwManager::ClearRegion(u32 networkType, SpSelection *selection, GeBounds *affected, u32 *cost,
    NwPositionList *removed, u8 charge, u8 apply) {
  ScOccupantManager *layer = nullptr;
  bool underground = false;
  if (networkType >= 1 && networkType <= 4)
    layer = surfaceLayer;
  else if (networkType == kNwPipeNetwork || networkType == kNwSubwayNetwork) {
    layer = networkType == kNwPipeNetwork ? undergroundOne : undergroundTwo;
    underground = true;
  }
  if (!terrain->CanPrepareForRoad(selection, affected, cost))
    return 0;
  GeBounds area = *affected;
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScOccupant *occupant = nullptr;
  u8 valid = 1;
  u32 maxX = u32(area.maximum.x >> kCtMicroCoordinateShift);
  for (u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift); x <= maxX; ++x) {
    u32 maxZ = u32(area.maximum.z >> kCtMicroCoordinateShift);
    for (u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift); z <= maxZ; ++z) {
      if (terrain->ChangedByLastOperation(x, z))
        for (u32 corner = 0; corner < 4; ++corner) {
          point = {i32((x + NwPreparationCorners[corner][0]) << kCtMicroCoordinateShift),
              i32((z + NwPreparationCorners[corner][1]) << kCtMicroCoordinateShift), 0};
          if (point.x < bounds.minimum.x || point.z < bounds.minimum.z || point.x > bounds.maximum.x ||
              point.z > bounds.maximum.z)
            continue;
          occupant = nullptr;
          if (surfaceLayer->GetOccupant(point, &occupant)) {
            u32 piece = GetTileId(occupant);
            valid = occupant->MajorType() == 5 ||
                (MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece));
            occupant->Release();
            occupant = nullptr;
          }
          if (undergroundOne->GetOccupant(point, &occupant)) {
            u32 piece = GetTileId(occupant);
            valid = valid && MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece);
            occupant->Release();
            occupant = nullptr;
          }
          if (undergroundTwo->GetOccupant(point, &occupant)) {
            u32 piece = GetTileId(occupant);
            valid = valid && MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece);
            occupant->Release();
          }
        }
      if (!valid)
        break;
    }
    if (!valid)
      break;
  }
  if (valid && apply) {
    valid = terrain->PrepareForRoad(selection, affected, cost, charge != 0);
    area = *affected;
    ScOccupant *surface = nullptr;
    maxX = u32(area.maximum.x >> kCtMicroCoordinateShift);
    if (valid)
      for (u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift); x <= maxX; ++x) {
        u32 maxZ = u32(area.maximum.z >> kCtMicroCoordinateShift);
        for (u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift); z <= maxZ; ++z)
          if (terrain->ChangedByLastOperation(x, z))
            for (u32 corner = 0; corner < 4; ++corner) {
              point = {i32((x + NwPreparationCorners[corner][0]) << kCtMicroCoordinateShift),
                  i32((z + NwPreparationCorners[corner][1]) << kCtMicroCoordinateShift), 0};
              if (point.x < bounds.minimum.x || point.z < bounds.minimum.z || point.x > bounds.maximum.x ||
                  point.z > bounds.maximum.z)
                continue;
              occupant = nullptr;
              if (underground && surfaceLayer->GetOccupant(point, &surface)) {
                if (surface->MajorType() == 5) {
                  surfaceLayer->RemoveOccupant(surface);
                  u8 value = 0;
                  zone->SetValue(x, z, &value);
                }
                surface->Release();
              }
              if (layer->GetOccupant(point, &occupant)) {
                if (MatchesNetwork(networkType, occupant) && !NwSlopeContains(*removed, u32(point.x), u32(point.z)))
                  removed->Append(point);
                if (occupant->MajorType() == 5 && occupant->CanUserRemove()) {
                  layer->RemoveOccupant(occupant);
                  u8 value = 0;
                  zone->SetValue(x, z, &value);
                }
                occupant->Release();
              } else {
                u8 value = 0;
                zone->SetValue(x, z, &value);
              }
            }
      }
  }
  return valid;
}

// Find a common terrain level for the endpoint rectangle and validate the changed vertices' four adjacent cells.
// Commit only after every in-bounds cell permits replacement; the original cost argument is unused.
u8 NwManager::PrepareEndpoints(CtCoord first, CtCoord last, u32 *, i32 networkType) {
  if (PlacementFlat(first) && PlacementFlat(last))
    return 1;
  GeBounds changed{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  u32 firstX = u32(first.x >> kCtMicroCoordinateShift), firstZ = u32(first.z >> kCtMicroCoordinateShift);
  u32 lastX = u32(last.x >> kCtMicroCoordinateShift), lastZ = u32(last.z >> kCtMicroCoordinateShift), cost = 0;
  GeBounds requested{{i32((firstX < lastX ? firstX : lastX) << kCtMicroCoordinateShift),
                         i32((firstZ < lastZ ? firstZ : lastZ) << kCtMicroCoordinateShift), 0},
      {i32(((firstX > lastX ? firstX : lastX) + 1) << kCtMicroCoordinateShift),
          i32(((firstZ > lastZ ? firstZ : lastZ) + 1) << kCtMicroCoordinateShift), 0}};
  u8 altitude;
  if (!terrain->GetOptimalLevel(requested, &altitude, &changed, &cost))
    return 0;
  first = changed.minimum;
  last = changed.maximum;
  bool valid = true;
  for (u32 x = u32(first.x >> kCtMicroCoordinateShift); valid && x <= u32(last.x >> kCtMicroCoordinateShift); ++x)
    for (u32 z = u32(first.z >> kCtMicroCoordinateShift); valid && z <= u32(last.z >> kCtMicroCoordinateShift); ++z)
      if (terrain->ChangedByLastOperation(x, z))
        for (u32 corner = 0; valid && corner < 4; ++corner) {
          u32 cellX = x + NwPreparationCorners[corner][0], cellZ = z + NwPreparationCorners[corner][1];
          CtCoord point{i32(cellX << kCtMicroCoordinateShift), i32(cellZ << kCtMicroCoordinateShift), 0};
          if (point.x >= bounds.minimum.x && point.z >= bounds.minimum.z && point.x <= bounds.maximum.x &&
              point.z <= bounds.maximum.z)
            valid = CanReplaceAt(i32(cellX), i32(cellZ), networkType);
        }
  if (!valid)
    return 0;
  return terrain->LevelTerrain(requested, altitude, &changed, &cost, true);
}

// Validate only the surface cells touched by highway preparation, preserving the four-corner order and live bounds.
// Applying preparation removes user-removable major-type-five occupants and clears empty cells in the zone map.
u8 NwManager::ClearRegionLayer(
    u32 networkType, SpSelection *selection, GeBounds *affected, u32 *cost, NwPositionList *, u8 charge, u8 apply) {
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (!terrain->CanPrepareForHighway(selection, affected, cost))
    return 0;
  GeBounds area = *affected;
  ScOccupant *occupant = nullptr;
  u8 valid = 1;
  u32 maxX = u32(area.maximum.x >> kCtMicroCoordinateShift);
  for (u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift); x <= maxX; ++x) {
    u32 maxZ = u32(area.maximum.z >> kCtMicroCoordinateShift);
    for (u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift); z <= maxZ; ++z) {
      if (terrain->ChangedByLastOperation(x, z))
        for (u32 corner = 0; corner < 4; ++corner) {
          point = {i32((x + NwPreparationCorners[corner][0]) << kCtMicroCoordinateShift),
              i32((z + NwPreparationCorners[corner][1]) << kCtMicroCoordinateShift), 0};
          if (point.x < bounds.minimum.x || point.z < bounds.minimum.z || point.x > bounds.maximum.x ||
              point.z > bounds.maximum.z)
            continue;
          occupant = nullptr;
          if (surfaceLayer->GetOccupant(point, &occupant)) {
            u32 piece = GetTileId(occupant);
            valid = occupant->MajorType() == 5 ||
                (MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece));
            occupant->Release();
          }
        }
      if (!valid)
        break;
    }
    if (!valid)
      break;
  }
  if (valid && apply) {
    valid = terrain->PrepareForHighway(selection, affected, cost, charge != 0);
    area = *affected;
    maxX = u32(area.maximum.x >> kCtMicroCoordinateShift);
    if (valid)
      for (u32 x = u32(area.minimum.x >> kCtMicroCoordinateShift); x <= maxX; ++x) {
        u32 maxZ = u32(area.maximum.z >> kCtMicroCoordinateShift);
        for (u32 z = u32(area.minimum.z >> kCtMicroCoordinateShift); z <= maxZ; ++z)
          if (terrain->ChangedByLastOperation(x, z))
            for (u32 corner = 0; corner < 4; ++corner) {
              point = {i32((x + NwPreparationCorners[corner][0]) << kCtMicroCoordinateShift),
                  i32((z + NwPreparationCorners[corner][1]) << kCtMicroCoordinateShift), 0};
              if (point.x < bounds.minimum.x || point.z < bounds.minimum.z || point.x > bounds.maximum.x ||
                  point.z > bounds.maximum.z)
                continue;
              occupant = nullptr;
              if (surfaceLayer->GetOccupant(point, &occupant)) {
                if (occupant->MajorType() == 5 && occupant->CanUserRemove()) {
                  surfaceLayer->RemoveOccupant(occupant);
                  u8 value = 0;
                  zone->SetValue(x, z, &value);
                }
                occupant->Release();
              } else {
                u8 value = 0;
                zone->SetValue(x, z, &value);
              }
            }
      }
  }
  return valid;
}

// Build the original one- or two-cell-wide coordinate strip, sampling endpoint altitudes before terrain preparation.
// Applied narrow strips repair incompatible slopes through a retained selection and the original hundred-pass limit.
bool NwManager::BuildStraightRun(
    u16 x0, u16 z0, u16 x1, u16 z1, i16 dx, i16 dz, u16 networkType, NwPositionList *output, u8 apply) {
  u32 network = networkType, startX = x0, startZ = z0, endX = x1, endZ = z1;
  ScOccupantManager *layer = nullptr;
  if (network >= 1 && network <= 4)
    layer = surfaceLayer;
  else if (network == 5 || network == 6)
    layer = network == 5 ? undergroundOne : undergroundTwo;
  if (!terrain->InCellBounds(startX, startZ))
    return false;
  i32 offsetX = dx, offsetZ = dz;
  if (!terrain->InCellBounds(startX + offsetX, startZ + offsetZ) || !terrain->InCellBounds(endX, endZ) ||
      !terrain->InCellBounds(endX + offsetX, endZ + offsetZ))
    return false;
  NwSelection *selection = new NwSelection;
  selection->AddRef();
  bool result = true;
  if (x0 == x1 || z0 == z1) {
    i32 stepX = i32(u32(x1) - u32(x0)), stepZ = i32(u32(z1) - u32(z0));
    if (i16(stepX) > 1)
      stepX = 1;
    if (i16(stepZ) > 1)
      stepZ = 1;
    if (i16(stepX) < -1)
      stepX = -1;
    if (i16(stepZ) < -1)
      stepZ = -1;
    u32 x = u32(x0), z = u32(z0);
    bool wide = (u16(dx) | u16(dz)) != 0, sloped = true, last;
    CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, other = position;
    do {
      u32 cellX = u16(x), cellZ = u16(z), microX = cellX << kCtMicroCoordinateShift,
          microZ = cellZ << kCtMicroCoordinateShift;
      position = {i32(microX), i32(microZ), 0};
      bool flat = PlacementFlat(position), slope = PlacementSlope(position);
      bool endpoint = (cellX == startX && cellZ == startZ) || (cellX == endX && cellZ == endZ);
      if (flat && endpoint) {
        u8 altitude;
        terrain->GetVertexAltitudeDirt(cellX, cellZ, &altitude);
        position = {i32(microX), i32(microZ), wide ? 0 : i32(u32(altitude) << kCtMicroCoordinateShift)};
      }
      if (sloped)
        sloped = slope;
      if (wide) {
        u32 otherX = (cellX + offsetX) << kCtMicroCoordinateShift,
            otherZ = (cellZ + offsetZ) << kCtMicroCoordinateShift;
        other = {i32(otherX), i32(otherZ), 0};
        if (flat && PlacementFlat(other)) {
          if (endpoint) {
            u8 altitude;
            terrain->GetVertexAltitudeDirt(cellX, cellZ, &altitude);
            other = {i32(otherX), i32(otherZ), i32(u32(altitude) << kCtMicroCoordinateShift)};
            position = {i32(microX), i32(microZ), other.altitude};
          }
        } else
          flat = false;
        selection->Append(position);
        selection->Append(other);
        if (sloped)
          sloped = flat;
      } else
        selection->Append(position);
      last = cellX == endX && cellZ == endZ;
      x += u32(stepX);
      z += u32(stepZ);
    } while (!last);
    if (!sloped) {
      GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
      u32 cost = 0;
      if (wide)
        result = ClearRegionLayer(network, selection, &affected, &cost, output, 0, apply) != 0;
      else {
        u8 prepared = ClearRegion(network, selection, &affected, &cost, output, 0, apply);
        result = prepared != 0;
        if (apply && prepared && (network == 1 || network == 2 || network == 3 || network == 5 || network == 6)) {
          NwPositionList pending;
          NwSelection *repair = new NwSelection;
          repair->AddRef();
          repair->Clear();
          bool rescan = true, again = true;
          CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
          for (u32 pass = 0; pass < 100; ++pass) {
            if (rescan) {
              again = false;
              u32 maxX = u32(affected.maximum.x >> kCtMicroCoordinateShift),
                  minZ = u32(affected.minimum.z >> kCtMicroCoordinateShift);
              u32 maxZ = u32(affected.maximum.z >> kCtMicroCoordinateShift);
              for (u32 scanX = u32(affected.minimum.x >> kCtMicroCoordinateShift); scanX <= maxX; ++scanX)
                for (u32 scanZ = minZ; scanZ <= maxZ; ++scanZ)
                  if (terrain->ChangedByLastOperation(scanX, scanZ)) {
                    ScOccupant *occupant = nullptr;
                    bool eligible = scanX >= startX && scanX <= endX && scanZ >= startZ && scanZ <= endZ;
                    point = {i32(scanX << kCtMicroCoordinateShift), i32(scanZ << kCtMicroCoordinateShift), 0};
                    if (!eligible && layer->GetOccupant(point, &occupant)) {
                      eligible = MatchesNetwork(network, occupant);
                      occupant->Release();
                      occupant = nullptr;
                    }
                    if (eligible && !PlacementSlope(point)) {
                      i8 signature[4];
                      PlacementSignature(point, signature);
                      pending.Append(point);
                    }
                  }
            }
            if (pending.head->next != pending.head) {
              point = pending.head->next->position;
              pending.RemoveXZ(point);
              repair->Clear();
              repair->Append(point);
              u8 repaired = ClearRegion(network, repair, &affected, &cost, output, 0, apply);
              rescan = repaired != 0;
              result = result && repaired != 0;
              again = true;
            }
            if (!again)
              break;
          }
          repair->Release();
        }
      }
    }
  }
  selection->Release();
  return result;
}

// Query current cursor colors in selection order and charge only cells without the matching road occupant.
u8 NwManager::RoadCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (surfaceLayer->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[0]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentRoad)));
  *cost = charged * price;
  return 1;
}

// Query current cursor colors in selection order and charge only cells without the matching highway occupant.
u8 NwManager::HighwayCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (surfaceLayer->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[1]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentHighway)));
  *cost = charged * price;
  return 1;
}

// Query current cursor colors in selection order and charge only cells without the matching rail occupant.
u8 NwManager::RailCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (surfaceLayer->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[2]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentRail)));
  *cost = charged * price;
  return 1;
}

// Query current cursor colors in selection order and charge only cells without the matching subway occupant.
u8 NwManager::SubwayCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (undergroundTwo->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[3]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentSubway)));
  *cost = charged * price;
  return 1;
}

// Query current cursor colors in selection order and charge only cells without the matching pipe occupant.
u8 NwManager::PipeCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (undergroundOne->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[4]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentPipe)));
  *cost = charged * price;
  return 1;
}

// Query current cursor colors in selection order and charge only cells without the matching power occupant.
u8 NwManager::PowerCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  u32 charged = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    ScOccupant *occupant = nullptr;
    if (surfaceLayer->GetOccupant(positions[i], &occupant)) {
      if (!NwPredicates[5]->Test(occupant))
        ++charged;
      occupant->Release();
      occupant = nullptr;
    } else
      ++charged;
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentPowerLine)));
  *cost = charged * price;
  return 1;
}

// Price every selected diagonal coordinate after the original color callbacks and copied-position count.
// The color output is intentionally not a filter; retain the live count until after the budget callback.
u8 NwManager::DiagonalCost(SpSelection *selection, u32 *cost) const {
  u32 count = 0;
  CtCoord positions[513];
  for (u32 i = 0; i < 513; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  NwPositionList points;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  selection->Reset();
  selection->Next(513, positions, &count);
  for (u32 i = 0; i < count; ++i) {
    u32 color = 35;
    cursor->GetColor(positions[i], &color);
    points.Append(positions[i]);
  }
  count = 0;
  for (NwPositionNode *point = points.head->next; point != points.head; point = point->next)
    ++count;
  u32 price = u32(budget->GetCost(u32(kCtAgentHighwayOffRamp)));
  *cost = count * price;
  return 1;
}

u8 NwManager::BuildRoad(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwRoadNetwork, cost, affected, focus);
}

u8 NwManager::BuildHighway(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwHighwayNetwork, cost, affected, focus);
}

u8 NwManager::BuildRail(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwRailNetwork, cost, affected, focus);
}

u8 NwManager::BuildSubway(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwSubwayNetwork, cost, affected, focus);
}

u8 NwManager::BuildPipe(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwPipeNetwork, cost, affected, focus);
}

u8 NwManager::BuildPower(SpSelection *selection, u32 *cost, GeBounds *affected, CtCoord *focus) {
  return BuildSelection(selection, kNwPowerNetwork, cost, affected, focus);
}

// Build only cursor-color-thirty-five points, preserving endpoint preparation, bridges, rule runs and ramp ordering.
// Retile the resulting coordinates before charging the accumulated cost; the original selection/focus inputs are unused.
bool NwManager::BuildBatch(ScOccupantManager *layer, SpSelection *, i32 networkType, u32 cost, GeBounds *affected,
    CtCoord *positions, u32 count, CtCoord *) {
  CtCoord first{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, last = first, previous = first,
                                                                                   earlier = first;
  NwPositionList filtered;
  bool started = false;
  u8 bridgeStatus = 0;
  u32 totalCost = cost, bridgeCost = 0;
  i32 processed = 0;
  CtCoord replacement;
  i32 dx = 0, dz = 0;
  UiCityCursor *cursor = NwGetCityWindow()->GetCellCursorMap();
  u32 color;
  for (u32 i = 0; i < count; ++i) {
    cursor->GetColor(positions[i], &color);
    if (color != 35)
      continue;
    filtered.Append(positions[i]);
    if (!started) {
      previous = positions[i];
      last = positions[i];
      first = positions[i];
      started = true;
    }
    earlier = previous;
    previous = last;
    last = positions[i];
  }
  u32 accepted = 0;
  for (NwPositionNode *node = filtered.head->next; node != filtered.head; node = node->next)
    ++accepted;
  if (!accepted || i64(u64(cost)) > budget->GetTotalFunds())
    return false;
  if (networkType == kNwHighwayNetwork) {
    NwPositionNode *node = filtered.head->next;
    if (node->next != filtered.head) {
      dx = i32(
          u32(node->next->position.x >> kCtMicroCoordinateShift) - u32(node->position.x >> kCtMicroCoordinateShift));
      dz = i32(
          u32(node->next->position.z >> kCtMicroCoordinateShift) - u32(node->position.z >> kCtMicroCoordinateShift));
    }
  }
  i32 offsetX = i16(dx), offsetZ = i16(dz);
  CtCoord endpoint = first,
          adjacent{i32((u32(first.x >> kCtMicroCoordinateShift) + u32(offsetX)) << kCtMicroCoordinateShift),
              i32((u32(first.z >> kCtMicroCoordinateShift) + u32(offsetZ)) << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  bool occupied = false;
  for (u32 side = 0; side < 2; ++side) {
    CtCoord *point = side ? &adjacent : &endpoint;
    bool found = layer->GetOccupant(*point, &occupant);
    if (found) {
      if (occupant->IsGeomFlag(1024) && occupant->CanUserRemove()) {
        layer->RemoveOccupant(occupant);
        found = false;
      }
      occupant->Release();
      occupant = nullptr;
    }
    occupied = occupied || found;
  }
  bool slopes = PlacementSlope(endpoint) && PlacementSlope(adjacent);
  u32 preparation;
  if (!occupied && !slopes)
    PrepareEndpoints(endpoint, adjacent, &preparation, networkType);
  NwPositionList bridgeRemoved;
  NwPieceList *pieces = nullptr;
  switch (networkType) {
  case kNwRoadNetwork:
    pieces = NwBridgePieces[0];
    break;
  case kNwRailNetwork:
    pieces = NwBridgePieces[2];
    break;
  case kNwPowerNetwork:
    pieces = NwBridgePieces[3];
    break;
  case kNwHighwayNetwork:
    pieces = NwBridgePieces[1];
    break;
  }
  if (pieces && accepted > 3) {
    bridgeStatus = u8(networkType == kNwHighwayNetwork
            ? BuildHighwayBridge(layer, &filtered, networkType, pieces, &bridgeRemoved, &bridgeCost)
            : BuildBridge(layer, &filtered, networkType, pieces, &bridgeRemoved, &bridgeCost));
    for (NwPositionNode *node = bridgeRemoved.head->next; node != bridgeRemoved.head; node = node->next)
      filtered.RemoveXZ(node->position);
    bridgeRemoved.Clear();
    totalCost = cost + bridgeCost;
    if (bridgeStatus)
      return false;
  }
  {
    NwPositionList changed;
    DecomposeRuns(&filtered, &changed, networkType, 1, &processed);
    for (NwPositionNode *node = changed.head->next; node != changed.head; node = node->next)
      if (!NwSlopeContains(filtered, u32(node->position.x), u32(node->position.z)))
        filtered.Append(node->position);
    bool ramp;
    if (networkType == kNwHighwayNetwork) {
      ramp = BuildHighwayRamp(layer, accepted, networkType, first, last, previous, earlier, i16(dx), i16(dz)) != 0;
      if (ramp) {
        filtered.PopFront();
        filtered.PopFront();
      }
    } else {
      replacement = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
      ramp = BuildSlopeRamp(layer, accepted, networkType, first, last, previous, &replacement, i16(dx), i16(dz)) != 0;
      if (ramp) {
        filtered.PopFront();
        filtered.Append(replacement);
      }
    }
    if (!ramp) {
      endpoint = last;
      adjacent = {i32((u32(last.x >> kCtMicroCoordinateShift) - u32(offsetX)) << kCtMicroCoordinateShift),
          i32((u32(last.z >> kCtMicroCoordinateShift) - u32(offsetZ)) << kCtMicroCoordinateShift), 0};
      bool firstFound = layer->GetOccupant(endpoint, &occupant);
      if (firstFound) {
        occupant->Release();
        occupant = nullptr;
      }
      bool secondFound = layer->GetOccupant(adjacent, &occupant);
      if (secondFound) {
        occupant->Release();
        occupant = nullptr;
      }
      slopes = PlacementSlope(endpoint) && PlacementSlope(adjacent);
      if (!firstFound && !secondFound && !slopes)
        PrepareEndpoints(endpoint, adjacent, &preparation, networkType);
    }
  }
  ConnectNeighbors(&filtered, networkType, totalCost);
  ApplyTiling(filtered, networkType, affected);
  if (networkType == kNwHighwayNetwork) {
    NwPositionList roads;
    GeBounds roadBounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
    CollectRegion(layer, NwPredicates[0], 1, *affected, &roads);
    if (roads.head->next != roads.head)
      ApplyTiling(roads, 1, &roadBounds);
    roads.Clear();
  }
  budget->WithdrawFunds(totalCost);
  return bridgeStatus == 0;
}

// Capture the selected layer before its virtual cost callback, then submit the selection's original1280-point batches.
// A refusal freezes further submission; the byte reply remains the last submitted batch result, or zero for no batch.
u8 NwManager::BuildSelection(SpSelection *selection, i32 networkType, u32 *cost, GeBounds *affected, CtCoord *focus) {
  ScOccupantManager *layer;
  switch (networkType) {
  case kNwRoadNetwork:
    layer = surfaceLayer;
    RoadCost(selection, cost);
    break;
  case kNwRailNetwork:
    layer = surfaceLayer;
    RailCost(selection, cost);
    break;
  case kNwPowerNetwork:
    layer = surfaceLayer;
    PowerCost(selection, cost);
    break;
  case kNwHighwayNetwork:
    layer = surfaceLayer;
    HighwayCost(selection, cost);
    break;
  case kNwPipeNetwork:
    layer = undergroundOne;
    PipeCost(selection, cost);
    break;
  case kNwSubwayNetwork:
    layer = undergroundTwo;
    SubwayCost(selection, cost);
    break;
  default:
    layer = surfaceLayer;
    if (networkType == 8)
      DiagonalCost(selection, cost);
    break;
  }
  u32 count = 0;
  CtCoord positions[1280];
  for (u32 i = 0; i < 1280; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u8 result = 0;
  selection->Reset();
  do {
    selection->Next(1280, positions, &count);
    if (!count)
      break;
    result = BuildBatch(layer, selection, networkType, *cost, affected, positions, count, focus);
    if (!count)
      break;
  } while (result);
  return result;
}
