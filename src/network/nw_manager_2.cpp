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

// Maintain network counts from occupant notifications and route the original region and decoration requests.
// Unrecognized types leave their payloads unread; every handled notification returns true after its effects.
bool NwManager::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kOcMsg_SurfaceInserted:
    AddSurface(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kOcMsg_SurfaceRemoved:
    RemoveSurface(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kOcMsg_PipeInserted:
    AddPipe(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kOcMsg_PipeRemoved:
    RemovePipe(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kOcMsg_SubwayInserted:
    AddSubway(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kOcMsg_SubwayRemoved:
    RemoveSubway(reinterpret_cast<ScOccupant *>(message->data1));
    break;
  case kNwMsg_RefreshNetwork: {
    u32 first = u32(message->data2), second = u32(message->data3);
    GeBounds rectangle{
        {i32((first >> 16) << kCtMicroCoordinateShift), i32((first & 0xffff) << kCtMicroCoordinateShift), 0},
        {i32((second >> 16) << kCtMicroCoordinateShift), i32((second & 0xffff) << kCtMicroCoordinateShift), 0}};
    RefreshNetwork(rectangle, u32(message->data1));
    break;
  }
  case kNwMsg_SetTransitDamage:
    SetTransitDamage(NwDamageFraction(u32(message->data1)));
    break;
  default:
    return false;
  }
  return true;
}

// Capture the occupant geometry flags once before incrementing each live surface-network count selected by its bits.
void NwManager::AddSurface(ScOccupant *occupant) {
  u32 mask = occupant->GetGeomFlag();
  if (mask & kOcGeomRoad)
    ++roadCount;
  if (mask & kOcGeomHighway)
    ++highwayCount;
  if (mask & kNwCatalogScratchBufferBytes)
    ++railCount;
  if (mask & kOcGeomPower)
    ++powerCount;
}

// Decrement the same four surface counts after capturing one virtual geometry result.
void NwManager::RemoveSurface(ScOccupant *occupant) {
  u32 mask = occupant->GetGeomFlag();
  if (mask & kOcGeomRoad)
    --roadCount;
  if (mask & kOcGeomHighway)
    --highwayCount;
  if (mask & kNwCatalogScratchBufferBytes)
    --railCount;
  if (mask & kOcGeomPower)
    --powerCount;
}

void NwManager::AddPipe(ScOccupant *occupant) {
  if (occupant->GetGeomFlag() & kOcGeomPipe)
    ++pipeCount;
}

void NwManager::RemovePipe(ScOccupant *occupant) {
  if (occupant->GetGeomFlag() & kOcGeomPipe)
    --pipeCount;
}

void NwManager::AddSubway(ScOccupant *occupant) {
  if (occupant->GetGeomFlag() & kOcGeomSubway)
    ++subwayCount;
}

void NwManager::RemoveSubway(ScOccupant *occupant) {
  if (occupant->GetGeomFlag() & kOcGeomSubway)
    --subwayCount;
}

struct NwLegacyRule {
  u16 source;
  i16 piece;
  u8 rotation;
  i16 differences[3];
};

static const NwLegacyRule NwLegacyRules0[] = {
    {14, 92, 0, {0, 0, 0}},
    {15, 92, 1, {0, 0, 0}},
    {16, 11263, 1, {0, -2, -2}},
    {17, 11263, 0, {-2, -2, 0}},
    {18, 11263, 2, {0, 2, 2}},
    {19, 11263, 3, {2, 2, 0}},
    {20, 20, 0, {0, 0, 0}},
    {21, 23, 2, {0, 0, 0}},
    {22, 20, 2, {0, 0, 0}},
    {23, 23, 0, {0, 0, 0}},
    {24, 24, 0, {0, 0, 0}},
    {25, 26, 2, {0, 0, 0}},
    {26, 24, 2, {0, 0, 0}},
    {27, 26, 0, {0, 0, 0}},
    {28, 28, 0, {0, 0, 0}},
    {29, 29, 0, {0, 0, 0}},
    {30, 29, 1, {0, 0, 0}},
    {31, 31, 0, {0, -2, -2}},
    {32, 32, 0, {-2, -2, 0}},
    {33, 31, 2, {0, 2, 2}},
    {34, 32, 2, {2, 2, 0}},
    {35, 35, 0, {0, 0, 0}},
    {36, 35, 3, {0, 0, 0}},
    {37, 35, 2, {0, 0, 0}},
    {38, 35, 1, {0, 0, 0}},
    {39, 39, 0, {0, 0, 0}},
    {40, 39, 3, {0, 0, 0}},
    {41, 39, 2, {0, 0, 0}},
    {42, 39, 1, {0, 0, 0}},
    {43, 43, 0, {0, 0, 0}},
    {44, 44, 0, {0, 0, 0}},
    {45, 44, 1, {0, 0, 0}},
    {46, 47, 0, {0, -2, -2}},
    {47, 46, 0, {-2, -2, 0}},
    {48, 47, 2, {0, 2, 2}},
    {49, 46, 2, {2, 2, 0}},
    {50, 50, 0, {0, 0, 0}},
    {51, 50, 3, {0, 0, 0}},
    {52, 50, 2, {0, 0, 0}},
    {53, 50, 1, {0, 0, 0}},
    {54, 54, 0, {0, 0, 0}},
    {55, 54, 3, {0, 0, 0}},
    {56, 54, 2, {0, 0, 0}},
    {57, 54, 1, {0, 0, 0}},
    {58, 58, 0, {0, 0, 0}},
    {59, 60, 0, {0, 0, 0}},
    {60, 59, 0, {0, 0, 0}},
    {61, 60, 2, {0, 0, 0}},
    {62, 59, 2, {0, 0, 0}},
    {63, 63, 0, {0, 0, 0}},
    {64, 64, 0, {0, 0, 0}},
    {65, 63, 2, {0, 0, 0}},
    {66, 64, 2, {0, 0, 0}},
    {67, 67, 0, {0, 0, 0}},
    {68, 67, 1, {0, 0, 0}},
    {69, 69, 0, {0, 0, 0}},
    {70, 69, 1, {0, 0, 0}},
    {71, 71, 0, {0, 0, 0}},
    {72, 71, 1, {0, 0, 0}},
};

static const NwLegacyRule NwLegacyRules1[] = {
    {1, 16012, 0, {0, 0, 0}},
    {2, 16013, 0, {0, 0, 0}},
    {3, 11615, 0, {0, -2, -2}},
    {4, 11614, 0, {-2, -2, 0}},
    {5, 11615, 2, {0, 2, 2}},
    {6, 11614, 2, {2, 2, 0}},
    {7, 11621, 0, {0, 0, 0}},
    {8, 11622, 0, {0, 0, 0}},
    {9, 11621, 2, {0, 0, 0}},
    {10, 11622, 2, {0, 0, 0}},
    {11, 329, 0, {0, 0, 0}},
    {12, 332, 2, {0, 0, 0}},
    {13, 329, 2, {0, 0, 0}},
    {14, 332, 0, {0, 0, 0}},
    {15, 333, 0, {0, 0, 0}},
};

static const NwLegacyRule NwLegacyRules2[] = {
    {16, 335, 0, {0, 0, 0}},
    {17, 334, 0, {0, 0, 0}},
    {18, 11647, 0, {0, -2, -2}},
    {19, 11648, 0, {-2, -2, 0}},
    {20, 11647, 2, {0, 2, 2}},
    {21, 11648, 2, {2, 2, 0}},
    {22, 11642, 0, {0, 0, 0}},
    {23, 11641, 2, {0, 0, 0}},
    {24, 11642, 2, {0, 0, 0}},
    {25, 11641, 0, {0, 0, 0}},
    {26, 344, 0, {0, 0, 0}},
    {27, 347, 2, {0, 0, 0}},
    {28, 344, 2, {0, 0, 0}},
    {29, 347, 0, {0, 0, 0}},
    {30, 348, 0, {0, 0, 0}},
};

static const NwLegacyRule NwLegacyRules3[] = {
    {31, 16012, 0, {0, 0, 0}},
    {32, 16013, 0, {0, 0, 0}},
    {33, 16012, 0, {0, 0, 0}},
    {34, 16013, 0, {0, 0, 0}},
};

static const NwLegacyRule NwLegacyRules4[] = {
    {31, 335, 0, {0, 0, 0}},
    {32, 334, 0, {0, 0, 0}},
    {33, 335, 0, {0, 0, 0}},
    {34, 334, 0, {0, 0, 0}},
};

// Sample the four terrain corners in source order and return their signed byte differences.
// A failed map-bounds check returns before touching the genuine signature output.
bool NwManager::PlacementSignature(const CtCoord &position, i8 signature[4]) {
  u32 x = (u32)(position.x >> kCtMicroCoordinateShift);
  if (x >= city->CellCountX())
    return 0;
  u32 z = (u32)(position.z >> kCtMicroCoordinateShift);
  if (z >= city->CellCountZ())
    return 0;
  u8 h00, h10, h11, h01;
  terrain->GetVertexAltitudeDirt((u32)(position.x >> kCtMicroCoordinateShift), z, &h00);
  terrain->GetVertexAltitudeDirt(
      (u32)((position.x >> kCtMicroCoordinateShift) + 1), (u32)(position.z >> kCtMicroCoordinateShift), &h10);
  terrain->GetVertexAltitudeDirt(
      (u32)((position.x >> kCtMicroCoordinateShift) + 1), (u32)((position.z >> kCtMicroCoordinateShift) + 1), &h11);
  terrain->GetVertexAltitudeDirt(
      (u32)(position.x >> kCtMicroCoordinateShift), (u32)((position.z >> kCtMicroCoordinateShift) + 1), &h01);
  signature[0] = 0;
  signature[1] = (i8)(h10 - h00);
  signature[2] = (i8)(h11 - h00);
  signature[3] = (i8)(h01 - h00);
  return 1;
}

// Import the legacy surface and underground tile planes in X-major order using the installed conversion tables.
bool NwManager::Init(CtCity *target, ScImporter *source) {
  if (!source->MapIsPresent(kNwLegacyRequiredSourcePlanes)) {
    Shutdown();
    return 0;
  }
  u32 dimension = source->GetMapSize(2), property;
  source->GetMiscValue(2, reinterpret_cast<i32 *>(&property));
  if (property >= 4)
    property = 0;
  void *object = nullptr;
  ScOccupantManager *surface = target->SurfaceOccupantManager();
  ScOccupantManager *underground1 = target->UndergroundOccupantManagerLevelOne();
  ScOccupantManager *underground2 = target->UndergroundOccupantManagerLevelTwo();
  ScDirtBag *height = target->DirtBag();
  u8 cell, altitude_surface, altitude1, altitude2, altitude_pair2, altitude_pair1;
  i8 signature[4];
  for (u32 x = 0; x < dimension; ++x)
    for (u32 z = 0; z < dimension; ++z) {
      source->GetValue(2, x, z, &cell);
      u8 surface_cell = cell;
      bool water = terrain->IsWater(x, z);
      if (surface_cell > 13 && surface_cell < 73) {
        const NwLegacyRule &rule = NwLegacyRules0[surface_cell - 14];
        if (!water && CreateOccupant(&object, IID_ScOccupant, (u32)(i32)rule.piece, rule.rotation)) {
          CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
          height->GetVertexAltitudeDirt(x, z, &altitude_surface);
          position.x = (i32)(x << kCtMicroCoordinateShift);
          position.z = (i32)(z << kCtMicroCoordinateShift);
          position.altitude = (i32)altitude_surface << kCtMicroCoordinateShift;
          PlacementSignature(position, signature);
          ScOccupant *occupant = (ScOccupant *)object;
          if (signature[1] == rule.differences[0] && signature[2] == rule.differences[1] &&
              signature[3] == rule.differences[2]) {
            occupant->SetLocation(position);
            surface->InsertOccupant(occupant);
          }
          occupant->Release();
          object = nullptr;
        }
      }
      source->GetValue(8, x, z, &cell);
      if (cell && cell < 16) {
        const NwLegacyRule &rule = NwLegacyRules1[cell - 1];
        if (CreateOccupant(&object, IID_ScOccupant, (u32)(u16)rule.piece, rule.rotation)) {
          CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
          height->GetVertexAltitudeDirt(x, z, &altitude2);
          position.x = (i32)(x << kCtMicroCoordinateShift);
          position.z = (i32)(z << kCtMicroCoordinateShift);
          position.altitude = (i32)altitude2 << kCtMicroCoordinateShift;
          PlacementSignature(position, signature);
          ScOccupant *occupant = (ScOccupant *)object;
          if (signature[1] == rule.differences[0] && signature[2] == rule.differences[1] &&
              signature[3] == rule.differences[2]) {
            occupant->SetLocation(position);
            underground2->InsertOccupant(occupant);
          }
          occupant->Release();
          object = nullptr;
        }
      } else if (cell > 15 && cell < 31) {
        const NwLegacyRule &rule = NwLegacyRules2[cell - 16];
        if (CreateOccupant(&object, IID_ScOccupant, (u32)(u16)rule.piece, rule.rotation)) {
          CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
          height->GetVertexAltitudeDirt(x, z, &altitude1);
          position.x = (i32)(x << kCtMicroCoordinateShift);
          position.z = (i32)(z << kCtMicroCoordinateShift);
          position.altitude = (i32)altitude1 << kCtMicroCoordinateShift;
          PlacementSignature(position, signature);
          ScOccupant *occupant = (ScOccupant *)object;
          if (signature[1] == rule.differences[0] && signature[2] == rule.differences[1] &&
              signature[3] == rule.differences[2]) {
            occupant->SetLocation(position);
            underground1->InsertOccupant(occupant);
          }
          occupant->Release();
          object = nullptr;
        }
      } else if (cell > 30 && cell < 35) {
        const NwLegacyRule &first = NwLegacyRules3[cell - 31], &second = NwLegacyRules4[cell - 31];
        i16 difference1 = second.differences[0], difference2 = second.differences[1];
        i16 difference3 = second.differences[2];
        if (CreateOccupant(&object, IID_ScOccupant, (u32)(i32)first.piece, first.rotation)) {
          CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
          height->GetVertexAltitudeDirt(x, z, &altitude_pair2);
          position.x = (i32)(x << kCtMicroCoordinateShift);
          position.z = (i32)(z << kCtMicroCoordinateShift);
          position.altitude = (i32)altitude_pair2 << kCtMicroCoordinateShift;
          PlacementSignature(position, signature);
          ScOccupant *occupant = (ScOccupant *)object;
          if (signature[1] == difference1 && signature[2] == difference2 && signature[3] == difference3) {
            occupant->SetLocation(position);
            underground2->InsertOccupant(occupant);
          }
          occupant->Release();
          object = nullptr;
        }
        if (CreateOccupant(&object, IID_ScOccupant, (u32)(i32)second.piece, second.rotation)) {
          CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
          height->GetVertexAltitudeDirt(x, z, &altitude_pair1);
          position.x = (i32)(x << kCtMicroCoordinateShift);
          position.z = (i32)(z << kCtMicroCoordinateShift);
          position.altitude = (i32)altitude_pair1 << kCtMicroCoordinateShift;
          PlacementSignature(position, signature);
          ScOccupant *occupant = (ScOccupant *)object;
          if (signature[1] == difference1 && signature[2] == difference2 && signature[3] == difference3) {
            occupant->SetLocation(position);
            underground1->InsertOccupant(occupant);
          }
          occupant->Release();
          object = nullptr;
        }
      }
    }
  return 1;
}

// The original module starts with six null catalog pointers; its Set-file loader owns their later population.
NwPieceCatalog *NwPieceCatalogs[6] = {};

// Scan only the instance field of each live resource key; type and group do not select membership.
static bool NwCatalogContains(const NwPieceCatalog &catalog, u32 piece) {
  for (RsKey *key = catalog.begin; key != catalog.end; ++key)
    if (key->instance == piece)
      return true;
  return false;
}

bool NwManager::IsRoadPiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[0], piece);
}

bool NwManager::IsHighwayPiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[1], piece);
}

bool NwManager::IsRailPiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[2], piece);
}

bool NwManager::IsSubwayPiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[3], piece);
}

bool NwManager::IsPipePiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[4], piece);
}

bool NwManager::IsPowerPiece(u32 piece) {
  return NwCatalogContains(*NwPieceCatalogs[5], piece);
}

// Query all six live virtual memberships in source order before selecting crossing or single-network type.
// The strict highway exception interval changes only road/highway and highway-only classifications.
u8 NwManager::ClassifyPiece(u32 piece) {
  bool road = IsRoadPiece(piece), highway = IsHighwayPiece(piece), rail = IsRailPiece(piece);
  bool subway = IsSubwayPiece(piece), power = IsPowerPiece(piece), pipe = IsPipePiece(piece);
  if (road) {
    if (rail)
      return 5;
    if (highway)
      return piece > 0x3b19 && piece < 0x3b26 ? 14 : 6;
  } else if (rail && highway)
    return 7;
  if (subway && rail)
    return 8;
  if (road && power)
    return 11;
  if (rail && power)
    return 12;
  if (highway && power)
    return 13;
  if (road)
    return 1;
  if (highway)
    return piece > 0x3b19 && piece < 0x3b26 ? 15 : 2;
  if (rail)
    return 3;
  if (subway)
    return 4;
  if (pipe)
    return 9;
  return power ? 10 : 0;
}

// Failed acquisition delegates to virtual Shutdown; successful bounds use the original input city in Z/X order.
bool NwManager::Init(CtCity *target) {
  u32 maxX, maxZ;
  if (!target)
    goto failed;
  city = target;
  target->AddRef();
  surfaceLayer = city->SurfaceOccupantManager();
  if (!surfaceLayer)
    goto failed;
  surfaceLayer->AddRef();
  undergroundOne = city->UndergroundOccupantManagerLevelOne();
  if (!undergroundOne)
    goto failed;
  undergroundOne->AddRef();
  undergroundTwo = city->UndergroundOccupantManagerLevelTwo();
  if (!undergroundTwo)
    goto failed;
  undergroundTwo->AddRef();
  terrain = city->DirtBag();
  if (!terrain)
    goto failed;
  terrain->AddRef();
  zone = city->ZoneLayer();
  if (!zone)
    goto failed;
  zone->AddRef();
  budget = city->BudgetLayer();
  if (!budget)
    goto failed;
  budget->AddRef();
  neighbors = city->NeighborsLayer();
  if (!neighbors)
    goto failed;
  neighbors->AddRef();
  bounds.minimum.x = 0;
  bounds.minimum.z = 0;
  bounds.minimum.altitude = 0;
  maxZ = target->CellCountZ() - 1;
  maxX = (target->CellCountX() << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell;
  bounds.maximum.altitude = 0;
  bounds.maximum.x = (i32)maxX;
  bounds.maximum.z = (i32)(maxZ << kCtMicroCoordinateShift);
  return true;
failed:
  Shutdown();
  return false;
}

// Reject missing attributes without changing output; otherwise borrow the actual key and forward its full instance.
// Keep zero rotation and the original IID/output arguments when invoking the actual virtual piece-creation dependency.
bool NwManager::CreateOccupant(GeAttrib *attributes, u32 iid, void **out) {
  if (!attributes)
    return false;
  RsKey *key = attributes->Key();
  return CreateOccupant(out, iid, key->instance, 0) != 0;
}

bool NwManager::CreateOccupant(RsKey *key, u32 iid, void **out) {
  return CreateOccupant(out, iid, key->instance, 0) != 0;
}

// Classify the piece before creating its primary network interface and initializing the real piece receiver.
u8 NwManager::CreateOccupant(void **out, u32 iid, u32 piece, u32 rotation) {
  u8 kind = ClassifyPiece(piece);
  void *object = nullptr;
  u8 result = NwMakePiece(kind, IID_NwConnection, &object);
  if (result) {
    result = static_cast<NwConnection *>(object)->Init(piece, rotation);
    if (result)
      result = static_cast<NwConnection *>(object)->QueryInterface(iid, out);
  }
  if (object)
    static_cast<NwConnection *>(object)->Release();
  return result;
}

// Read the original network-piece property before classifying and creating an occupant interface.
u8 NwManager::CreateOccupant(OcPersistInfo *info, u32 iid, void **out) {
  u32 piece = 0;
  u8 result = 0;
  if (info->GetIntData(kNwPieceTileIdKey, &piece)) {
    u8 kind = ClassifyPiece(piece);
    void *object = nullptr;
    result = NwMakePiece(kind, IID_ScOccupant, &object);
    if (result) {
      result = static_cast<ScOccupant *>(object)->Init(city, info);
      if (result)
        result = static_cast<ScOccupant *>(object)->QueryInterface(iid, out);
    }
    if (object)
      static_cast<ScOccupant *>(object)->Release();
  }
  return result;
}

int NwManager::HasTransportAt(const CtCoord &position) const {
  return HasRoadAt(position) || HasHighwayAt(position) || HasRailAt(position) || HasSubwayAt(position) ||
      HasRoadFallbackAt(position);
}

// Test the surface road predicate only after a successful lookup; false publication is not released.
bool NwManager::HasRoadAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    result = NwPredicates[0]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the surface highway predicate only after a successful lookup; false publication is not released.
bool NwManager::HasHighwayAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    result = NwPredicates[1]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the surface rail predicate only after a successful lookup; false publication is not released.
bool NwManager::HasRailAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    result = NwPredicates[2]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the second-underground subway predicate only after a successful lookup; false publication is not released.
bool NwManager::HasSubwayAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (undergroundTwo->GetOccupant(position, &occupant)) {
    result = NwPredicates[3]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the first-underground pipe predicate only after a successful lookup; false publication is not released.
bool NwManager::HasPipeAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (undergroundOne->GetOccupant(position, &occupant)) {
    result = NwPredicates[4]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the surface power predicate only after a successful lookup; false publication is not released.
bool NwManager::HasPowerAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    result = NwPredicates[5]->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Test the distinct fallback road predicate only after a successful lookup; false publication is not released.
bool NwManager::HasRoadFallbackAt(const CtCoord &position) const {
  ScOccupant *occupant = nullptr;
  bool result = false;
  if (surfaceLayer->GetOccupant(position, &occupant)) {
    result = NwPredicates[0]->Test(occupant);
    occupant->Release();
  }
  return result;
}

static const i8 NwSlopeSignatures[8][4] = {{0, -1, -1, 0}, {0, 0, -1, -1}, {0, 1, 1, 0}, {0, 0, 1, 1}, {0, -2, -2, 0},
    {0, 0, -2, -2}, {0, 2, 2, 0}, {0, 0, 2, 2}};

/* DWORD=pieceWORD<<8 | stateBYTE. Exact installed3195C..31A3C rows;
   native table order matches those addresses, not network-number order. */
static const u32 kNwSlopePieces[8][8] = {
    {0x002bc200, 0x002bc100, 0x002bc202, 0x002bc102, 0x00002000, 0x00001f00, 0x00002002, 0x00001f02},
    {0x003e8800, 0x003e8900, 0x003e8802, 0x003e8902, 0x00002e00, 0x00002f00, 0x00002e02, 0x00002f02},
    {0x002d6000, 0x002d6100, 0x002d6002, 0x002d6102, 0x002d5e00, 0x002d5f00, 0x002d5e02, 0x002d5f02},
    {0x002d8200, 0x002d8100, 0x002d8202, 0x002d8102, 0x002d8000, 0x002d7f00, 0x002d8002, 0x002d7f02},
    {0x002bfe00, 0x002bfe01, 0x002bfe02, 0x002bfe03, 0x002bff00, 0x002bff01, 0x002bff02, 0x002bff03},
    {0x0046a100, 0x0046a000, 0x0046a102, 0x0046a002, 0x0046a300, 0x0046a200, 0x0046a302, 0x0046a202},
    {0x003b0000, 0x003aff00, 0x003afe02, 0x003b0102, 0x003afc00, 0x003afb00, 0x00370e02, 0x003afd02},
    {0x003afe00, 0x003b0100, 0x003b0002, 0x003aff02, 0x00370e00, 0x003afd00, 0x003afc02, 0x003afb02}};

// Compare the exact fixed-point X/Z words in list order. Altitude does not affect membership.
static bool NwSlopeContains(const NwPositionList &list, u32 x, u32 z) {
  for (NwPositionNode *node = list.head->next; node != list.head; node = node->next)
    if ((u32)node->position.x == x && (u32)node->position.z == z)
      return true;
  return false;
}

// Acquire the current application for regional connection feedback and release it before using its borrowed BoxX.
// A failed application query leaves the original connection effects intact without delivering feedback.
static void NwNotifyConnection(i32 event) {
  void *application = nullptr;
  if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, &application)) {
    ApBoxX *box = static_cast<ApGameInterface *>(application)->GetBoxX();
    static_cast<ApGameInterface *>(application)->Release();
    box->Event(kAuEventPlayEffect, event, 0, 0, 0);
  }
}

// Offer one regional connection at a boundary endpoint, preserving highway pairs and the original edge-query order.
// Confirm and charge only the chosen connection, then deliver its network-specific BoxX event through a fresh application.
void NwManager::ConnectNeighbors(NwPositionList *positions, i32 networkType, u32 constructionCost) {
  NwPositionNode *head = positions->head;
  if (head->next == head)
    return;
  bool attempted = false;
  if (networkType == kNwHighwayNetwork) {
    CtCoord first{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        second{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
    u16 maximumX = u16(terrain->CellCountX() - 1), maximumZ = u16(terrain->CellCountZ() - 1);
    u8 edges = 0;
    for (u32 end = 0; end < 2; ++end) {
      head = positions->head;
      NwPositionNode *node = end ? head->previous : head->next;
      u32 visited = 0, found = 0;
      i32 x = 0, z = 0;
      while (node != head && visited < 4 && found < 2) {
        ++visited;
        x = node->position.x >> kCtMicroCoordinateShift;
        z = node->position.z >> kCtMicroCoordinateShift;
        if (!x || !z || x == maximumX || z == maximumZ) {
          if (!found)
            first = node->position;
          else
            second = node->position;
          ++found;
        }
        node = end ? node->previous : node->next;
      }
      if (!found)
        continue;
      i32 firstX = first.x >> kCtMicroCoordinateShift, firstZ = first.z >> kCtMicroCoordinateShift;
      bool connected = neighbors->HasConnection(kNwHighwayNetwork, firstX, firstZ) &&
          neighbors->HasConnection(
              kNwHighwayNetwork, second.x >> kCtMicroCoordinateShift, second.z >> kCtMicroCoordinateShift);
      if (!x)
        edges |= 8;
      if (u32(x) == terrain->VertexCountX() - 2)
        edges |= 2;
      if (!z)
        edges |= 1;
      if (u32(z) == terrain->VertexCountZ() - 2)
        edges |= 4;
      bool available = false;
      if ((edges & 1) && neighbors->NeighborFlag(1))
        available = true;
      if ((edges & 2) && neighbors->NeighborFlag(2))
        available = true;
      if ((edges & 4) && neighbors->NeighborFlag(3))
        available = true;
      if ((edges & 8) && neighbors->NeighborFlag(0))
        available = true;
      if (!available || connected || attempted)
        continue;
      u32 cost = u32(budget->GetCost(kCtAgentHighwayNeighbor));
      if (cost && i64(u64(cost + constructionCost)) > budget->GetTotalFunds())
        ConfirmConstruction(kNwConfirmInsufficientFundsText, 0);
      else if (ConfirmConstruction(kNwConfirmNeighborText, i32(cost))) {
        if (!neighbors->HasConnection(kNwHighwayNetwork, firstX, firstZ))
          neighbors->AddConnection(kNwHighwayNetwork, firstX, firstZ);
        i32 secondX = second.x >> kCtMicroCoordinateShift, secondZ = second.z >> kCtMicroCoordinateShift;
        if (!neighbors->HasConnection(kNwHighwayNetwork, secondX, secondZ))
          neighbors->AddConnection(kNwHighwayNetwork, secondX, secondZ);
        budget->WithdrawFunds(cost);
        NwNotifyConnection(36);
      }
      attempted = true;
    }
    return;
  }
  CtCoord first = head->next->position, last = first;
  for (NwPositionNode *node = head->next; node != head; node = node->next)
    last = node->position;
  for (u32 end = 0; end < 2; ++end) {
    CtCoord point = end ? last : first;
    i32 x = point.x >> kCtMicroCoordinateShift, z = point.z >> kCtMicroCoordinateShift;
    u8 edges = 0;
    if (!x)
      edges = 8;
    if (u32(x) == terrain->VertexCountX() - 2)
      edges |= 2;
    if (!z)
      edges |= 1;
    if (u32(z) == terrain->VertexCountZ() - 2)
      edges |= 4;
    if (edges) {
      bool connected = neighbors->HasConnection(networkType, x, z), available = false;
      if (edges & 1)
        available = neighbors->NeighborFlag(1) != 0;
      if ((edges & 2) && neighbors->NeighborFlag(2))
        available = true;
      if ((edges & 4) && neighbors->NeighborFlag(3))
        available = true;
      if ((edges & 8) && neighbors->NeighborFlag(0))
        available = true;
      if (available && !connected && !attempted) {
        u32 key = kCtAgentRoadNeighbor;
        i32 event = 36;
        switch (networkType) {
        case kNwRailNetwork:
          key = kCtAgentRailNeighbor;
          event = 37;
          break;
        case kNwPowerNetwork:
          key = kCtAgentPowerNeighbor;
          event = 46;
          break;
        case kNwPipeNetwork:
          key = kCtAgentPipeNeighbor;
          event = 45;
          break;
        case kNwSubwayNetwork:
          key = kCtAgentSubwayNeighbor;
          event = 37;
          break;
        }
        u32 cost = u32(budget->GetCost(key));
        if (cost && i64(u64(cost + constructionCost)) > budget->GetTotalFunds())
          ConfirmConstruction(kNwConfirmInsufficientFundsText, 0);
        else if (ConfirmConstruction(kNwConfirmNeighborText, i32(cost))) {
          neighbors->AddConnection(networkType, x, z);
          budget->WithdrawFunds(cost);
          NwNotifyConnection(event);
        }
        attempted = true;
      }
    }
    if (first.x == last.x && first.z == last.z)
      break;
  }
}

// Scan paired highway coordinates, copy both directional bridge sides and shape the three-vertex-wide approaches.
// Preserve shared second-side counts, scratch-height reuse, partial effects and cost publication before selection release.
u32 NwManager::BuildHighwayBridge(ScOccupantManager *layer, NwPositionList *positions, i32 networkType,
    NwPieceList *pieces, NwPositionList *removed, u32 *cost) {
  *cost = 0;
  NwSelection *selection = new NwSelection;
  NwPositionNode *head = positions->head, *first = head->next;
  u32 deltaX = 0, deltaZ = 0;
  if (first->next != head) {
    deltaX =
        u32(first->next->position.x >> kCtMicroCoordinateShift) - u32(first->position.x >> kCtMicroCoordinateShift);
    deltaZ =
        u32(first->next->position.z >> kCtMicroCoordinateShift) - u32(first->position.z >> kCtMicroCoordinateShift);
  }
  selection->AddRef();
  bool cancelled = false, valid = true;
  if (pieces && pieces->begin != pieces->end) {
    bool blocked = false, foundWater = false;
    u8 waterNow = 0, third = 0;
    // The first water-pair read overwrites this native initializer before any defined source use.
    u8 water = 23, minimumSample;
    u32 startX = 0, startZ = 0, endX = 0, endZ = 0;
    ScOccupant *occupant = nullptr;
    head = positions->head;
    NwPositionNode *node = head->next;
    CtCoord selectionPoint;
    while (node != head && (waterNow || !foundWater)) {
      if (blocked)
        break;
      bool firstWater = PlacementWater(node->position);
      if (firstWater && surfaceLayer->GetOccupant(node->position, &occupant)) {
        blocked = true;
        occupant->Release();
        occupant = nullptr;
      }
      NwPositionNode *next = node->next;
      waterNow = 0;
      if (next == head)
        blocked = true;
      else {
        node = next;
        waterNow = PlacementWater(node->position);
        if (waterNow && surfaceLayer->GetOccupant(node->position, &occupant)) {
          blocked = true;
          occupant->Release();
          occupant = nullptr;
        }
      }
      waterNow = firstWater || waterNow;
      if (!waterNow && foundWater)
        third = 1;
      if (!foundWater && waterNow) {
        foundWater = true;
        startX = u32(node->position.x >> kCtMicroCoordinateShift) - deltaX;
        startZ = u32(node->position.z >> kCtMicroCoordinateShift) - deltaZ;
        ReadAltitude(
            u32(node->position.x >> kCtMicroCoordinateShift), u32(node->position.z >> kCtMicroCoordinateShift), &water);
        if (ReadAltitude(u32(node->position.x >> kCtMicroCoordinateShift) - deltaX,
                u32(node->position.z >> kCtMicroCoordinateShift) - deltaZ, &minimumSample) &&
            water > minimumSample)
          water = minimumSample;
      }
      if (waterNow) {
        endX = u32(node->position.x >> kCtMicroCoordinateShift);
        endZ = u32(node->position.z >> kCtMicroCoordinateShift);
        removed->Append(node->position);
        selectionPoint = {
            i32((endX - deltaX) << kCtMicroCoordinateShift), i32((endZ - deltaZ) << kCtMicroCoordinateShift), 0};
        removed->Append(selectionPoint);
        if (ReadAltitude(u32(node->position.x >> kCtMicroCoordinateShift),
                u32(node->position.z >> kCtMicroCoordinateShift), &minimumSample) &&
            water > minimumSample)
          water = minimumSample;
        if (ReadAltitude(u32(node->position.x >> kCtMicroCoordinateShift) - deltaX,
                u32(node->position.z >> kCtMicroCoordinateShift) - deltaZ, &minimumSample) &&
            water > minimumSample)
          water = minimumSample;
      }
      node = node->next;
    }
    if (!blocked && foundWater && third) {
      u32 minX = startX < endX ? startX : endX, maxX = startX > endX ? startX : endX;
      u32 minZ = startZ < endZ ? startZ : endZ, maxZ = startZ > endZ ? startZ : endZ;
      bool vertical = !(maxX - minX > 1 || maxZ == minZ || (maxZ - minZ == 1 && maxX != minX && deltaX == 0));
      u32 length = vertical ? maxZ - minZ + 1 : maxX - minX + 1;
      NwBridgePattern patterns[2];
      const NwBridgePattern *decoded[2] = {};
      bool confirmed = false, matched = false;
      u32 startCount = 0, middle = 0, finishCount = 0, spanCost = 0;
      for (u32 pass = 0; pass < 2; ++pass) {
        const u32 *cursor = pieces->begin, *record = nullptr, *catalogEnd = pieces->end;
        u32 wanted = pass ? (vertical ? 1 : 2) : (vertical ? 3 : 0);
        matched = false;
        do {
          u32 words = NwBridgeReadWord(&cursor, catalogEnd), direction = NwBridgeReadWord(&cursor, catalogEnd);
          NwBridgeSkipWords(&cursor, catalogEnd, 1);
          if (direction == wanted) {
            matched = true;
            record = cursor;
            break;
          }
          usize skip = u32(words - 3), available = usize(catalogEnd - cursor);
          cursor += skip < available ? skip : available;
        } while (cursor != catalogEnd);
        if (!pass) {
          spanCost = length * u32(budget->GetCost(kCtAgentGirderBridgePart));
          if (i64(u64(spanCost)) > budget->GetTotalFunds()) {
            ConfirmConstruction(kNwConfirmInsufficientFundsText, 0);
            confirmed = false;
            cancelled = true;
          } else {
            confirmed = ConfirmConstruction(kNwConfirmBridgeText, i32(spanCost)) != 0;
            cancelled = !confirmed;
          }
        }
        if (confirmed && matched && record != catalogEnd) {
          NwReadBridgePattern(record, catalogEnd, &patterns[pass]);
          decoded[pass] = &patterns[pass];
          startCount = patterns[pass].count[0];
          middle = patterns[pass].count[2];
          finishCount = patterns[pass].count[4];
        }
      }
      if (confirmed && matched) {
        u32 repeats = 0;
        if (middle <= length) {
          u32 covered = middle;
          do {
            if (repeats >= 256)
              break;
            ++repeats;
            covered += middle;
          } while (covered <= length);
        }
        u32 remainder = length - middle * repeats, leading = remainder >> 1, trailing = remainder - leading;
        u32 axisX = vertical ? 0 : 1, axisZ = vertical ? 1 : 0, crossX = axisZ, crossZ = axisX;
        u32 near3X = minX - 3 * axisX, near3Z = minZ - 3 * axisZ;
        u8 sampleA, sampleB, approach = 20;
        if (ReadAltitude(near3X, near3Z, &sampleA) && ReadAltitude(near3X + crossX, near3Z + crossZ, &sampleB) &&
            ReadAltitude(near3X + 2 * crossX, near3Z + 2 * crossZ, &third))
          approach = u8((u32(sampleA) + sampleB + third) / 3);
        if (i32(approach) < i32(water) - 2)
          approach = u8(water - 2);
        if (i32(approach) > i32(water) + 6)
          approach = u8(water + 6);
        sampleA = approach;
        u32 clearCost = 0;
        GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
            {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
        static const i32 nearOffsets[4] = {-3, -4, -1, 0};
        for (u32 row = 0; valid && row < 4; ++row)
          for (u32 column = 0; valid && column < 3; ++column)
            valid = CanPrepareVertex(minX + u32(nearOffsets[row]) * axisX + column * crossX,
                minZ + u32(nearOffsets[row]) * axisZ + column * crossZ,
                !row && !column ? approach
                    : row < 2   ? sampleA
                                : u8(water + 2),
                networkType);
        u32 farX = minX + length * axisX, farZ = minZ + length * axisZ;
        u32 far3X = farX + 3 * axisX, far3Z = farZ + 3 * axisZ;
        if (ReadAltitude(far3X, far3Z, &sampleA) && ReadAltitude(far3X + crossX, far3Z + crossZ, &sampleB) &&
            ReadAltitude(far3X + 2 * crossX, far3Z + 2 * crossZ, &sampleB))
          approach = u8((u32(sampleA) + sampleB + third) / 3);
        if (i32(approach) < i32(water) - 2)
          approach = u8(water - 2);
        if (i32(approach) > i32(water) + 6)
          approach = u8(water + 6);
        static const u32 farOffsets[4] = {3, 4, 1, 0};
        for (u32 row = 0; valid && row < 4; ++row)
          for (u32 column = 0; valid && column < 3; ++column)
            valid = CanPrepareVertex(farX + farOffsets[row] * axisX + column * crossX,
                farZ + farOffsets[row] * axisZ + column * crossZ, row < 2 ? approach : u8(water + 2), networkType);
        for (u32 row = 0; valid && row < 4; ++row)
          for (u32 column = 0; valid && column < 3; ++column)
            valid = PrepareVertex(minX + u32(nearOffsets[row]) * axisX + column * crossX,
                minZ + u32(nearOffsets[row]) * axisZ + column * crossZ, row < 2 ? sampleA : u8(water + 2), networkType);
        if (valid) {
          selection->Clear();
          for (u32 row = 0; row < 4; ++row)
            for (u32 column = 0; column < 2; ++column) {
              u32 x = minX - (4 - row) * axisX + column * crossX;
              u32 z = minZ - (4 - row) * axisZ + column * crossZ;
              if (terrain->InVertexBounds(x, z)) {
                selectionPoint = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift),
                    row == 0       ? i32(u32(sampleA) << kCtMicroCoordinateShift)
                        : row == 3 ? i32((u32(water) + 2) << kCtMicroCoordinateShift)
                                   : 0};
                selection->Append(selectionPoint);
              }
            }
          NwPositionList cleared;
          valid = ClearRegionLayer(networkType, selection, &affected, &clearCost, &cleared, 1, 1) != 0;
        }
        if (valid) {
          u32 x = minX + axisX, z = minZ + axisZ;
          while ((x == minX || x < farX) && (z == minZ || z < farZ)) {
            for (u32 column = 0; column < 3; ++column)
              if (ReadAltitude(x + column * crossX, z + column * crossZ, &waterNow) && waterNow > water && valid)
                valid = PrepareVertex(x + column * crossX, z + column * crossZ, water, networkType);
            x += axisX;
            z += axisZ;
            if (!valid)
              break;
          }
        }
        if (ReadAltitude(far3X, far3Z, &sampleA) && ReadAltitude(far3X + crossX, far3Z + crossZ, &sampleB) &&
            ReadAltitude(far3X + 2 * crossX, far3Z + 2 * crossZ, &sampleB))
          approach = u8((u32(sampleA) + sampleB + third) / 3);
        if (i32(approach) < i32(water) - 2)
          approach = u8(water - 2);
        if (i32(approach) > i32(water) + 6)
          approach = u8(water + 6);
        sampleA = approach;
        for (u32 row = 0; valid && row < 4; ++row)
          for (u32 column = 0; valid && column < 3; ++column)
            valid = PrepareVertex(farX + farOffsets[row] * axisX + column * crossX,
                farZ + farOffsets[row] * axisZ + column * crossZ,
                !row && !column ? approach
                    : row < 2   ? sampleA
                                : u8(water + 2),
                networkType);
        if (valid) {
          selection->Clear();
          for (u32 row = 0; row < 4; ++row)
            for (u32 column = 0; column < 2; ++column) {
              u32 x = farX + (3 - row) * axisX + column * crossX;
              u32 z = farZ + (3 - row) * axisZ + column * crossZ;
              selectionPoint = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift),
                  !row           ? i32(u32(sampleA) << kCtMicroCoordinateShift)
                      : row == 3 ? i32((u32(water) + 2) << kCtMicroCoordinateShift)
                                 : 0};
              selection->Append(selectionPoint);
            }
          NwPositionList cleared;
          valid = ClearRegionLayer(networkType, selection, &affected, &clearCost, &cleared, 1, 1) != 0;
        }
        if (valid) {
          NwValidateBridgePair(decoded[0], decoded[1], startCount, middle, finishCount, repeats, leading, trailing);
          u32 x = minX, z = minZ;
          CtCoord point{i32(minX << kCtMicroCoordinateShift), i32(minZ << kCtMicroCoordinateShift), 0};
          if (startCount) {
            x -= axisX;
            z -= axisZ;
            for (u32 side = 0; side < 2; ++side) {
              point = {i32((x + side * crossX) << kCtMicroCoordinateShift),
                  i32((z + side * crossZ) << kCtMicroCoordinateShift), 0};
              PlaceSpan(layer, point, patterns[side].start[0], u8(water + 2), 8, 0);
              removed->Append(point);
            }
            x += axisX;
            z += axisZ;
          }
          for (u32 i = 0; i < leading; ++i) {
            for (u32 side = 0; side < 2; ++side) {
              point = {i32((x + side * crossX) << kCtMicroCoordinateShift),
                  i32((z + side * crossZ) << kCtMicroCoordinateShift), 0};
              PlaceSpan(layer, point, patterns[side].leading[0], water, 8, 0);
              removed->Append(point);
            }
            x += axisX;
            z += axisZ;
          }
          for (u32 repeat = 0; repeat < repeats; ++repeat)
            for (u32 i = 0; i < middle; ++i) {
              for (u32 side = 0; side < 2; ++side) {
                point = {i32((x + side * crossX) << kCtMicroCoordinateShift),
                    i32((z + side * crossZ) << kCtMicroCoordinateShift), 0};
                PlaceSpan(layer, point, patterns[side].span[i], water, 8, 0);
                removed->Append(point);
              }
              x += axisX;
              z += axisZ;
            }
          for (u32 i = 0; i < trailing; ++i) {
            for (u32 side = 0; side < 2; ++side) {
              point = {i32((x + side * crossX) << kCtMicroCoordinateShift),
                  i32((z + side * crossZ) << kCtMicroCoordinateShift), 0};
              PlaceSpan(layer, point, patterns[side].trailing[0], water, 8, 0);
              removed->Append(point);
            }
            x += axisX;
            z += axisZ;
          }
          if (finishCount)
            for (u32 side = 0; side < 2; ++side) {
              point = {i32((x + side * crossX) << kCtMicroCoordinateShift),
                  i32((z + side * crossZ) << kCtMicroCoordinateShift), 0};
              PlaceSpan(layer, point, patterns[side].finish[0], u8(water + 2), 8, 0);
              removed->Append(point);
            }
          *cost = spanCost;
        } else
          ConfirmConstruction(kNwConfirmBridgeDamageText, 0);
      }
    }
  }
  selection->Release();
  return cancelled || !valid;
}

// Find an unobstructed water segment, select its bridge pattern and shape both land approaches before placing pieces.
// Preserve the early water-output appends, partial terrain effects and original DWORD failure result and cost publication.
u32 NwManager::BuildBridge(ScOccupantManager *layer, NwPositionList *positions, i32 networkType, NwPieceList *pieces,
    NwPositionList *removed, u32 *cost) {
  NwSelection *selection = new NwSelection;
  *cost = 0;
  selection->AddRef();
  bool cancelled = false, valid = true, built = false;
  u32 spanCost = 0;
  if (pieces && pieces->begin != pieces->end && positions->head->next != positions->head) {
    bool blocked = false, foundWater = false, afterWater = false, captureNextX = false;
    u8 waterNow = 0;
    u32 startX = 0, startZ = 0, endX = 0, endZ = 0, nextX = 0;
    // Every reached altitude read overwrites this native scratch initializer before a defined source use.
    u8 water = 23, minimumSample;
    ScOccupant *occupant = nullptr;
    NwPositionNode *head = positions->head, *node = head->next;
    while (node != head && (waterNow || !foundWater)) {
      waterNow = PlacementWater(node->position);
      if (waterNow) {
        if (surfaceLayer->GetOccupant(node->position, &occupant)) {
          blocked = true;
          occupant->Release();
          occupant = nullptr;
        }
      } else if (foundWater)
        afterWater = true;
      if (captureNextX) {
        nextX = u32(node->position.x >> kCtMicroCoordinateShift);
        captureNextX = false;
      }
      if (!foundWater && waterNow) {
        startX = u32(node->position.x >> kCtMicroCoordinateShift);
        startZ = u32(node->position.z >> kCtMicroCoordinateShift);
        foundWater = ReadAltitude(startX, startZ, &water) != 0;
        captureNextX = foundWater;
      }
      if (waterNow) {
        endX = u32(node->position.x >> kCtMicroCoordinateShift);
        endZ = u32(node->position.z >> kCtMicroCoordinateShift);
        removed->Append(node->position);
        if (ReadAltitude(u32(node->position.x >> kCtMicroCoordinateShift),
                u32(node->position.z >> kCtMicroCoordinateShift), &minimumSample) &&
            water > minimumSample)
          water = minimumSample;
      }
      node = node->next;
    }
    if (!blocked && foundWater && afterWater) {
      u32 minX = startX < endX ? startX : endX, maxX = startX > endX ? startX : endX;
      u32 minZ = startZ < endZ ? startZ : endZ, maxZ = startZ > endZ ? startZ : endZ;
      bool vertical = !(maxX - minX > 1 || maxZ == minZ || (maxZ - minZ == 1 && maxX != minX && startX == nextX));
      u32 length = vertical ? maxZ - minZ + 1 : maxX - minX + 1;
      const u32 *cursor = pieces->begin, *record = nullptr, *catalogEnd = pieces->end;
      do {
        u32 words = NwBridgeReadWord(&cursor, catalogEnd);
        u32 direction = NwBridgeReadWord(&cursor, catalogEnd), minimum = NwBridgeReadWord(&cursor, catalogEnd);
        if (direction == u32(vertical) && minimum <= length) {
          record = cursor;
          break;
        }
        usize skip = u32(words - 3), available = usize(catalogEnd - cursor);
        cursor += skip < available ? skip : available;
      } while (cursor != catalogEnd);
      spanCost = length * u32(budget->GetCost(kCtAgentBridgePart));
      bool confirmed;
      if (i64(u64(spanCost)) > budget->GetTotalFunds()) {
        ConfirmConstruction(kNwConfirmInsufficientFundsText, 0);
        cancelled = true;
        confirmed = false;
      } else {
        confirmed =
            ConfirmConstruction(networkType == kNwPowerNetwork ? kNwConfirmPowerPlatformsText : kNwConfirmBridgeText,
                i32(spanCost)) != 0;
        cancelled = !confirmed;
      }
      if (confirmed && record && record != catalogEnd) {
        NwBridgePattern pattern;
        NwReadBridgePattern(record, catalogEnd, &pattern);
        u32 repeats = 0, middle = pattern.count[2];
        if (middle <= length) {
          u32 covered = middle;
          do {
            if (repeats >= 256)
              break;
            ++repeats;
            covered += middle;
          } while (covered <= length);
        }
        u32 remainder = length - middle * repeats, leading = remainder >> 1, trailing = remainder - leading;
        u32 axisX = vertical ? 0 : 1, axisZ = vertical ? 1 : 0, crossX = axisZ, crossZ = axisX;
        u8 rise = networkType == kNwPowerNetwork ? 1 : 5, deck = networkType == kNwPowerNetwork ? 1 : 3;
        u8 sampleA, sampleB, approach = 20;
        u32 near3X = minX - 3 * axisX, near3Z = minZ - 3 * axisZ;
        if (ReadAltitude(near3X, near3Z, &sampleA) && ReadAltitude(near3X + crossX, near3Z + crossZ, &sampleB))
          approach = u8((u32(sampleA) + sampleB) / 2);
        if (i32(approach) < i32(water) + rise - 4)
          approach = u8(water + rise - 4);
        if (i32(approach) > i32(water) + rise + 4)
          approach = u8(water + rise + 4);
        u32 clearCost = 0;
        GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
            {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
        sampleA = approach;
        valid = CanPrepareVertex(near3X, near3Z, approach, networkType) &&
            CanPrepareVertex(near3X + crossX, near3Z + crossZ, sampleA, networkType) &&
            CanPrepareVertex(minX - 4 * axisX, minZ - 4 * axisZ, sampleA, networkType) &&
            CanPrepareVertex(minX + crossX - 4 * axisX, minZ + crossZ - 4 * axisZ, sampleA, networkType) &&
            CanPrepareVertex(minX - axisX, minZ - axisZ, u8(water + rise), networkType) &&
            CanPrepareVertex(minX + crossX - axisX, minZ + crossZ - axisZ, u8(water + rise), networkType) &&
            CanPrepareVertex(minX, minZ, u8(water + deck), networkType) &&
            CanPrepareVertex(minX + crossX, minZ + crossZ, u8(water + deck), networkType);
        u32 farX = minX + length * axisX, farZ = minZ + length * axisZ;
        u32 far3X = farX + 3 * axisX, far3Z = farZ + 3 * axisZ;
        approach = u8(water + 1);
        if (ReadAltitude(far3X, far3Z, &sampleA) && ReadAltitude(far3X + crossX, far3Z + crossZ, &sampleB))
          approach = u8((u32(sampleA) + sampleB) / 2);
        if (i32(approach) < i32(water) + rise - 4)
          approach = u8(water + rise - 4);
        if (i32(approach) > i32(water) + rise + 4)
          approach = u8(water + rise + 4);
        if (valid)
          valid = CanPrepareVertex(far3X, far3Z, approach, networkType) &&
              CanPrepareVertex(far3X + crossX, far3Z + crossZ, approach, networkType) &&
              CanPrepareVertex(farX + 4 * axisX, farZ + 4 * axisZ, approach, networkType) &&
              CanPrepareVertex(farX + crossX + 4 * axisX, farZ + crossZ + 4 * axisZ, approach, networkType) &&
              CanPrepareVertex(farX + axisX, farZ + axisZ, u8(water + rise), networkType) &&
              CanPrepareVertex(farX + 1, farZ + 1, u8(water + rise), networkType) &&
              CanPrepareVertex(farX, farZ, u8(water + deck), networkType) &&
              CanPrepareVertex(farX + crossX, farZ + crossZ, u8(water + deck), networkType);
        if (valid)
          valid = PrepareVertex(near3X, near3Z, sampleA, networkType) &&
              PrepareVertex(near3X + crossX, near3Z + crossZ, sampleA, networkType) &&
              PrepareVertex(minX - 4 * axisX, minZ - 4 * axisZ, sampleA, networkType) &&
              PrepareVertex(minX + crossX - 4 * axisX, minZ + crossZ - 4 * axisZ, sampleA, networkType) &&
              PrepareVertex(minX - axisX, minZ - axisZ, u8(water + rise), networkType) &&
              PrepareVertex(minX + crossX - axisX, minZ + crossZ - axisZ, u8(water + rise), networkType) &&
              PrepareVertex(minX, minZ, u8(water + deck), networkType) &&
              PrepareVertex(minX + crossX, minZ + crossZ, u8(water + deck), networkType);
        CtCoord selectionPoint;
        if (valid) {
          selection->Clear();
          if (terrain->InVertexBounds(minX - 4 * axisX, minZ - 4 * axisZ)) {
            selectionPoint = {i32((minX - 4 * axisX) << kCtMicroCoordinateShift),
                i32((minZ - 4 * axisZ) << kCtMicroCoordinateShift), i32(u32(sampleA) << kCtMicroCoordinateShift)};
            selection->Append(selectionPoint);
          }
          if (terrain->InVertexBounds(near3X, near3Z)) {
            selectionPoint = {i32(near3X << kCtMicroCoordinateShift), i32(near3Z << kCtMicroCoordinateShift), 0};
            selection->Append(selectionPoint);
          }
          if (terrain->InVertexBounds(minX - 2 * axisX, minZ - 2 * axisZ)) {
            selectionPoint = {i32((minX - 2 * axisX) << kCtMicroCoordinateShift),
                i32((minZ - 2 * axisZ) << kCtMicroCoordinateShift), 0};
            selection->Append(selectionPoint);
          }
          if (terrain->InVertexBounds(minX - axisX, minZ - axisZ)) {
            selectionPoint = {i32((minX - axisX) << kCtMicroCoordinateShift),
                i32((minZ - axisZ) << kCtMicroCoordinateShift), i32((u32(water) + rise) << kCtMicroCoordinateShift)};
            selection->Append(selectionPoint);
          }
          NwPositionList cleared;
          valid = ClearRegion(networkType, selection, &affected, &clearCost, &cleared, 1, 1) != 0;
        }
        if (valid) {
          u32 x = minX + axisX, z = minZ + axisZ, otherX = x + crossX, otherZ = z + crossZ;
          while ((x == minX || x < farX) && (z == minZ || z < farZ)) {
            if (ReadAltitude(x, z, &waterNow) && waterNow > water && valid)
              valid = PrepareVertex(x, z, water, networkType);
            if (ReadAltitude(otherX, otherZ, &waterNow) && waterNow > water && valid)
              valid = PrepareVertex(otherX, otherZ, water, networkType);
            x += axisX;
            z += axisZ;
            otherX += axisX;
            otherZ += axisZ;
            if (!valid)
              break;
          }
        }
        approach = u8(water + 1);
        if (ReadAltitude(far3X, far3Z, &sampleA) && ReadAltitude(far3X + crossX, far3Z + crossZ, &sampleB))
          approach = u8((u32(sampleA) + sampleB) / 2);
        if (i32(approach) < i32(water) + rise - 4)
          approach = u8(water + rise - 4);
        if (i32(approach) > i32(water) + rise + 4)
          approach = u8(water + rise + 4);
        sampleA = approach;
        if (valid)
          valid = PrepareVertex(far3X, far3Z, approach, networkType) &&
              PrepareVertex(far3X + crossX, far3Z + crossZ, sampleA, networkType) &&
              PrepareVertex(farX + 4 * axisX, farZ + 4 * axisZ, sampleA, networkType) &&
              PrepareVertex(farX + crossX + 4 * axisX, farZ + crossZ + 4 * axisZ, sampleA, networkType) &&
              PrepareVertex(farX + axisX, farZ + axisZ, u8(water + rise), networkType) &&
              PrepareVertex(farX + 1, farZ + 1, u8(water + rise), networkType) &&
              PrepareVertex(farX, farZ, u8(water + deck), networkType) &&
              PrepareVertex(farX + crossX, farZ + crossZ, u8(water + deck), networkType);
        if (valid) {
          selection->Clear();
          selectionPoint = {i32(far3X << kCtMicroCoordinateShift), i32(far3Z << kCtMicroCoordinateShift),
              i32(u32(sampleA) << kCtMicroCoordinateShift)};
          selection->Append(selectionPoint);
          selectionPoint = {i32((farX + 2 * axisX) << kCtMicroCoordinateShift),
              i32((farZ + 2 * axisZ) << kCtMicroCoordinateShift), 0};
          selection->Append(selectionPoint);
          selectionPoint = {
              i32((farX + axisX) << kCtMicroCoordinateShift), i32((farZ + axisZ) << kCtMicroCoordinateShift), 0};
          selection->Append(selectionPoint);
          selectionPoint = {i32(farX << kCtMicroCoordinateShift), i32(farZ << kCtMicroCoordinateShift),
              i32((u32(water) + rise) << kCtMicroCoordinateShift)};
          selection->Append(selectionPoint);
          NwPositionList cleared;
          valid = ClearRegion(networkType, selection, &affected, &clearCost, &cleared, 1, 1) != 0;
        }
        if (valid) {
          NwValidateBridgeFillers(&pattern, leading, trailing);
          u32 x = minX, z = minZ;
          CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
          if (pattern.count[0]) {
            x = minX - axisX;
            z = minZ - axisZ;
            NwPositionList slopes;
            u32 piece = farZ;
            u8 state = rise;
            for (u32 step = 2; step; --step) {
              point = {i32((x - step * axisX) << kCtMicroCoordinateShift),
                  i32((z - step * axisZ) << kCtMicroCoordinateShift), 0};
              if (!SelectSlope(point, networkType, &slopes, &piece, &state, 0)) {
                state = u8(vertical);
                if (networkType == kNwRoadNetwork)
                  piece = 29;
                else if (networkType == kNwRailNetwork)
                  piece = 44;
                else if (networkType == kNwPowerNetwork)
                  piece = 92;
              }
              PlaceSpan(layer, point, (piece << kNwPieceCodeShift) + state, -1, 8, 0);
              removed->Append(point);
            }
            point = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
            PlaceSpan(layer, point, pattern.start[0],
                u8(water +
                    (networkType == kNwPowerNetwork ? 1
                            : vertical              ? 7
                                                    : 5)),
                8, 0);
            removed->Append(point);
            x += axisX;
            z += axisZ;
          }
          for (u32 i = 0; i < leading; ++i) {
            point = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
            PlaceSpan(layer, point, pattern.leading[0], water, 8, 0);
            removed->Append(point);
            x += axisX;
            z += axisZ;
          }
          static const u32 blocking[16] = {0x53, 0x468a, 0x4694, 0x3eb1, 0x3eaa, 0x2c0b, 0x5b, 0x57, 0x2c24, 0x4689,
              0x4693, 0x3eab, 0x5a, 0x2c01, 0x3ebd, 0x3ebe};
          for (u32 repeat = 0; repeat < repeats; ++repeat)
            for (u32 i = 0; i < middle; ++i) {
              point = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
              for (u32 test = 0; test < 16; ++test)
                if (pattern.span[i] == blocking[test])
                  terrain->BlockCell(x, z, true);
              PlaceSpan(layer, point, pattern.span[i], water, 8, 0);
              removed->Append(point);
              x += axisX;
              z += axisZ;
            }
          for (u32 i = 0; i < trailing; ++i) {
            point = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
            PlaceSpan(layer, point, pattern.trailing[0], water, 8, 0);
            removed->Append(point);
            x += axisX;
            z += axisZ;
          }
          if (pattern.count[4]) {
            point = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
            PlaceSpan(layer, point, pattern.finish[0],
                u8(water +
                    (networkType == kNwPowerNetwork ? 1
                            : vertical              ? 7
                                                    : 5)),
                8, 0);
            removed->Append(point);
            x += axisX;
            z += axisZ;
            NwPositionList slopes;
            u32 piece = minZ;
            u8 state = deck;
            for (u32 step = 0; step < 2; ++step) {
              point = {i32((x + step * axisX) << kCtMicroCoordinateShift),
                  i32((z + step * axisZ) << kCtMicroCoordinateShift), 0};
              if (!SelectSlope(point, networkType, &slopes, &piece, &state, 0)) {
                state = u8(vertical);
                if (networkType == kNwRoadNetwork)
                  piece = 29;
                else if (networkType == kNwRailNetwork)
                  piece = 44;
                else if (networkType == kNwPowerNetwork)
                  piece = 92;
              }
              PlaceSpan(layer, point, (piece << kNwPieceCodeShift) + state, -1, 8, 0);
              removed->Append(point);
            }
          }
          built = true;
        } else
          ConfirmConstruction(kNwConfirmBridgeDamageText, 0);
      }
    }
  }
  selection->Release();
  if (built)
    *cost = spanCost;
  return cancelled || !valid;
}

// Search a three-vertex-wide highway slope, confirm its cost and reshape both ramp approaches in source order.
// Place all eight pieces only after successful reshaping; a failed vertex reports the original construction error.
u8 NwManager::BuildHighwayRamp(ScOccupantManager *layer, u32 count, i32 networkType, CtCoord, CtCoord last,
    CtCoord previous, CtCoord earlier, i16 dx, i16 dz) {
  if (networkType != kNwHighwayNetwork || count <= 3)
    return 0;
  u32 rawX = u32(last.x >> kCtMicroCoordinateShift) - u32(earlier.x >> kCtMicroCoordinateShift),
      rawZ = u32(last.z >> kCtMicroCoordinateShift) - u32(earlier.z >> kCtMicroCoordinateShift);
  i16 stepX = i16(rawX), stepZ = i16(rawZ);
  if (i32(stepX) * dx || i32(stepZ) * dz || stepX < -1 || stepX > 1 || stepZ < -1 || stepZ > 1 || i32(stepX) * stepZ ||
      (!stepX && !stepZ))
    return 0;
  u32 x = u32(previous.x >> kCtMicroCoordinateShift), z = u32(previous.z >> kCtMicroCoordinateShift);
  u32 vertices[4][3][2];
  if (stepX) {
    if (rawX & 0x8000)
      ++x;
    if (u32(dz) & 0x8000)
      --z;
    for (u32 row = 0; row < 4; ++row)
      for (u32 column = 0; column < 3; ++column) {
        vertices[row][column][0] = x + (row - 1) * rawX;
        vertices[row][column][1] = z + column;
      }
  } else {
    if (u32(dx) & 0x8000)
      --x;
    if (rawZ & 0x8000)
      ++z;
    for (u32 row = 0; row < 4; ++row)
      for (u32 column = 0; column < 3; ++column) {
        vertices[row][column][0] = x + column;
        vertices[row][column][1] = z + (row - 1) * rawZ;
      }
  }
  for (u32 row = 0; row < 4; ++row)
    for (u32 column = 0; column < 3; ++column) {
      vertices[row][column][0] = u32(i32(i16(vertices[row][column][0])));
      vertices[row][column][1] = u32(i32(i16(vertices[row][column][1])));
    }
  for (u32 column = 0; column < 3; ++column)
    if (!terrain->InVertexBounds(vertices[3][column][0], vertices[3][column][1]))
      return 0;
  u8 levels[3][3];
  for (u32 row = 0; row < 3; ++row)
    for (u32 column = 0; column < 3; ++column)
      terrain->GetVertexAltitude(vertices[row + 1][column][0], vertices[row + 1][column][1], &levels[row][column]);
  if (levels[1][1] + 2 * (levels[1][0] - levels[0][0]) - levels[0][1] <= 6 ||
      levels[2][1] + 2 * (levels[2][0] - levels[1][0]) - levels[1][1] <= 6)
    return 0;
  u8 low = levels[0][0];
  if (levels[0][1] < low)
    low = levels[0][1];
  if (levels[0][2] < low)
    low = levels[0][2];
  u8 high = u8(low + 8);
  u32 offsetX = u32(2 * stepX), offsetZ = u32(2 * stepZ), length = 2;
  bool found = false;
  do {
    if (!terrain->InCellBounds(offsetX + stepX + vertices[3][2][0], offsetZ + stepZ + vertices[3][2][1]) ||
        !terrain->InCellBounds(offsetX + stepX + vertices[3][1][0], offsetZ + stepZ + vertices[3][1][1]) ||
        !terrain->InCellBounds(offsetX + stepX + vertices[3][0][0], offsetZ + stepZ + vertices[3][0][1]))
      break;
    offsetX += stepX;
    offsetZ += stepZ;
    u8 a, b, c;
    terrain->GetVertexAltitude(offsetX + vertices[3][0][0], offsetZ + vertices[3][0][1], &a);
    terrain->GetVertexAltitude(offsetX + vertices[3][1][0], offsetZ + vertices[3][1][1], &b);
    terrain->GetVertexAltitude(offsetX + vertices[3][2][0], offsetZ + vertices[3][2][1], &c);
    u8 minimum = a;
    if (b < minimum)
      minimum = b;
    if (c < minimum)
      minimum = c;
    ++length;
    found = minimum < high;
  } while (!found);
  if (!found || u16(length) < 3)
    return 0;
  length = u16(length);
  u32 cost = 2 * length * u32(budget->GetCost(kCtAgentTunnel));
  if (!ConfirmConstruction(kNwConfirmTunnelText, i32(cost)))
    return 0;
  bool valid = true;
  for (u32 row = 0; valid && row < 4; ++row)
    for (u32 column = 0; valid && column < 3; ++column)
      valid = PrepareVertex(vertices[row][column][0], vertices[row][column][1],
          row < 2        ? low
              : row == 2 ? u8(low + 4)
                         : high,
          4);
  for (u32 column = 0; valid && column < 3; ++column)
    valid = PrepareVertex(offsetX + vertices[3][column][0], offsetZ + vertices[3][column][1], high, 4);
  for (u32 row = 0; row < 3; ++row) {
    offsetX += u32(2 * stepX);
    offsetZ += u32(2 * stepZ);
    for (u32 column = 0; valid && column < 3; ++column)
      valid = PrepareVertex(offsetX + vertices[2 - row][column][0], offsetZ + vertices[2 - row][column][1],
          row == 0 ? u8(low + 4) : low, 4);
  }
  if (!valid) {
    ConfirmConstruction(kNwConfirmTunnelDamageText, 0);
    return 0;
  }
  u32 direction = stepX == -1 ? 0 : stepZ == -1 ? 1 : stepX == 1 ? 2 : 3;
  static const i8 positions[4][4][2] = {{{0, 0}, {0, -1}, {-1, 0}, {-1, -1}}, {{-1, 0}, {0, 0}, {-1, -1}, {0, -1}},
      {{-1, -1}, {-1, 0}, {0, -1}, {0, 0}}, {{0, -1}, {-1, -1}, {0, 0}, {-1, 0}}};
  static const i32 kNwHighwayRampPieces[4][4] = {{0x46ab00, 0x46aa00, 0x46ab02, 0x46aa02},
      {0x46a900, 0x46a800, 0x46a902, 0x46a802}, {0x466e00, 0x466d00, 0x466e02, 0x466d02},
      {0x466c00, 0x466b00, 0x466c02, 0x466b02}};
  CtCoord endpoints[4] = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  for (u32 end = 0; end < 2; ++end) {
    u32 centerX = vertices[2][1][0], centerZ = vertices[2][1][1];
    if (end) {
      centerX += offsetX - u32(4 * stepX);
      centerZ += offsetZ - u32(4 * stepZ);
      direction = (direction + 2) & 3;
    }
    for (u32 piece = 0; piece < 4; ++piece) {
      CtCoord position{i32((centerX + positions[direction][piece][0]) << kCtMicroCoordinateShift),
          i32((centerZ + positions[direction][piece][1]) << kCtMicroCoordinateShift), 0};
      PlaceSpan(layer, position, kNwHighwayRampPieces[piece][direction], i16(piece < 2 ? low : u32(low) + 4),
          piece < 2 ? 8 : direction, piece < 2 ? 0 : length + 1);
    }
  }
  return 1;
}

// Find a road or rail ramp beyond the last accepted point, confirm its price and reshape both endpoint vertices.
// Publish the far endpoint even after a vertex rejection, report that failure, and retain the original withdrawal order.
u8 NwManager::BuildSlopeRamp(ScOccupantManager *layer, u32, i32 networkType, CtCoord, CtCoord last, CtCoord previous,
    CtCoord *focus, i16 dx, i16 dz) {
  if (networkType != kNwRoadNetwork && networkType != kNwRailNetwork)
    return 0;
  u32 startX = u32(last.x >> kCtMicroCoordinateShift), startZ = u32(last.z >> kCtMicroCoordinateShift);
  i16 stepX = i16(startX - u32(previous.x >> kCtMicroCoordinateShift)),
      stepZ = i16(startZ - u32(previous.z >> kCtMicroCoordinateShift));
  if (i32(stepX) * dx || i32(stepZ) * dz || stepX < -1 || stepX > 1 || stepZ < -1 || stepZ > 1 || i32(stepX) * stepZ ||
      !terrain->InCellBounds(startX, startZ))
    return 0;
  u8 a, b, c, d;
  terrain->GetVertexAltitude(startX, startZ, &a);
  terrain->GetVertexAltitude(startX + 1, startZ, &b);
  terrain->GetVertexAltitude(startX + 1, startZ + 1, &c);
  terrain->GetVertexAltitude(startX, startZ + 1, &d);
  i32 rise = 0;
  if (stepX > 0)
    rise = b + c - d - a;
  if (stepX < 0)
    rise = a + d - c - b;
  if (stepZ > 0)
    rise = c + d - b - a;
  if (stepZ < 0)
    rise = a + b - d - c;
  if (rise < 4)
    return 0;
  u8 low = a;
  if (b < low)
    low = b;
  if (c < low)
    low = c;
  if (d < low)
    low = d;
  u8 high = u8(low + 4);
  u32 endX = startX + stepX, endZ = startZ + stepZ, length = 1;
  bool found = false;
  do {
    u32 nextX = endX + stepX, nextZ = endZ + stepZ;
    if (!terrain->InCellBounds(nextX, nextZ))
      break;
    endX = nextX;
    endZ = nextZ;
    terrain->GetVertexAltitude(nextX, nextZ, &a);
    terrain->GetVertexAltitude(nextX + 1, nextZ, &b);
    terrain->GetVertexAltitude(nextX + 1, nextZ + 1, &c);
    terrain->GetVertexAltitude(nextX, nextZ + 1, &d);
    u8 minimum = a;
    if (b < minimum)
      minimum = b;
    if (c < minimum)
      minimum = c;
    if (d < minimum)
      minimum = d;
    ++length;
    found = minimum < high;
  } while (!found);
  if (!found || u16(length) < 3)
    return 0;
  length = u16(length);
  u32 cost = (length - 1) * u32(budget->GetCost(kCtAgentTunnel));
  if (i64(u64(cost)) > budget->GetTotalFunds() || !ConfirmConstruction(kNwConfirmTunnelText, i32(cost)))
    return 0;
  u32 direction = stepX == -1 ? 0 : stepZ == -1 ? 1 : stepX == 1 ? 2 : 3;
  static const u8 heights[4][4] = {{1, 0, 0, 1}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 1}};
  static const u8 corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  bool valid = true;
  for (u32 corner = 0; valid && corner < 4; ++corner)
    valid = PrepareVertex(
        startX + corners[corner][0], startZ + corners[corner][1], heights[direction][corner] ? high : low, networkType);
  for (u32 corner = 0; valid && corner < 4; ++corner)
    valid = PrepareVertex(
        endX + corners[corner][0], endZ + corners[corner][1], heights[direction][corner] ? low : high, networkType);
  u32 extraX = endX + (stepX == -1 ? kScBackwardGridStep : stepX == 1 ? 2 : 0);
  u32 extraZ = endZ + (stepZ == -1 ? kScBackwardGridStep : stepZ == 1 ? 2 : 0);
  valid = valid && PrepareVertex(extraX, extraZ, low, networkType) &&
      PrepareVertex(extraX + (stepZ != 0), extraZ + (stepX != 0), low, networkType);
  focus->x = i32((endX + (stepX == -1 ? kScBackwardGridStep : stepX == 1 ? 1 : 0)) << kCtMicroCoordinateShift);
  focus->altitude = 0;
  focus->z = i32((endZ + (stepZ == -1 ? kScBackwardGridStep : stepZ == 1 ? 1 : 0)) << kCtMicroCoordinateShift);
  static const i32 kNwSlopeRampPieces[2][4] = {
      {0x4000, 0x3f00, 0x4002, 0x3f02}, {0x466800, 0x466300, 0x466802, 0x466302}};
  i32 packed = kNwSlopeRampPieces[networkType - 1][direction];
  CtCoord position;
  if (packed && valid) {
    position = {i32(startX << kCtMicroCoordinateShift), i32(startZ << kCtMicroCoordinateShift), kCtUnknownCoordinate};
    PlaceSpan(layer, position, packed, low, direction, length);
  }
  direction = (direction + 2) & 3;
  packed = kNwSlopeRampPieces[networkType - 1][direction];
  if (packed && valid) {
    position.x = i32(endX << kCtMicroCoordinateShift);
    position.altitude = kCtUnknownCoordinate;
    position.z = i32(endZ << kCtMicroCoordinateShift);
    PlaceSpan(layer, position, packed, low, direction, length);
  }
  if (!valid)
    ConfirmConstruction(kNwConfirmTunnelDamageText, 0);
  budget->WithdrawFunds(cost);
  return valid;
}

// Create the decoded ramp piece before replacing a removable occupant at the supplied position.
// Resolve its altitude and optional span, insert its occupant view, clear the zone and release the created reference.
void NwManager::PlaceSpan(
    ScOccupantManager *layer, const CtCoord &position, u32 packed, i16 altitude, i32 direction, u32 length) {
  ScOccupant *previous = nullptr;
  u32 piece;
  u8 state;
  NwRuleDecode(i32(packed), &piece, &state);
  void *created;
  if (!CreateOccupant(&created, IID_ScOccupant, piece, state))
    return;
  if (layer->GetOccupant(position, &previous)) {
    if (previous->CanUserRemove())
      layer->RemoveOccupant(previous);
    previous->Release();
    previous = nullptr;
  }
  u8 level;
  if (!(altitude & 0x8000))
    level = u8(altitude);
  else
    terrain->GetVertexAltitudeDirt(
        u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &level);
  if (direction != 8) {
    void *connection = nullptr;
    if (static_cast<ScOccupant *>(created)->QueryInterface(IID_NwConnection, &connection)) {
      static_cast<NwConnection *>(connection)->SetTeleport(direction, i32(length));
      static_cast<NwConnection *>(connection)->Release();
    }
  }
  CtCoord target{position.x, position.z, i32(u32(level) << kCtMicroCoordinateShift)};
  static_cast<ScOccupant *>(created)->SetLocation(target);
  layer->InsertOccupant(static_cast<ScOccupant *>(created));
  u8 value = 0;
  zone->SetValue(u32(target.x >> kCtMicroCoordinateShift), u32(target.z >> kCtMicroCoordinateShift), &value);
  static_cast<ScOccupant *>(created)->Release();
}

// Read the vertex altitude through the current terrain provider when its bounds check accepts the coordinates.
// Otherwise publish the original fallback altitude of twenty-three and return the false byte.
u8 NwManager::ReadAltitude(u32 x, u32 z, u8 *altitude) {
  if (terrain->InVertexBounds(x, z)) {
    terrain->GetVertexAltitude(x, z, altitude);
    return 1;
  }
  *altitude = 23;
  return 0;
}

// Apply the requested vertex altitude only when the live terrain provider accepts the vertex coordinates.
// Forward the provider's byte reply without adding a network result or changing its terrain checks.
u8 NwManager::SetAltitude(u32 x, u32 z, u8 altitude) {
  if (!terrain->InVertexBounds(x, z))
    return 0;
  return terrain->SetVertexAltitude(x, z, altitude);
}

// Prefer the retained network interface's ID, then fall back to the borrowed attribute key's entire instance word.
// The query output is read live before release; a failed query's publication is ignored without a release.
u32 NwManager::GetTileId(ScOccupant *occupant) {
  void *view = nullptr;
  if (occupant->QueryInterface(IID_NwConnection, &view)) {
    u32 result = static_cast<NwConnection *>(view)->TileId();
    static_cast<NwConnection *>(view)->Release();
    return result;
  }
  return occupant->GetAttribData()->Key()->instance;
}

// Query the live surface occupant only after terrain accepts both unsigned tile words, then resolve its full piece ID.
u32 NwManager::GetTileId(u16 x, u16 z) {
  u32 tileX = x, tileZ = z, result = 0;
  CtCoord point = {i32(tileX << kCtMicroCoordinateShift), i32(tileZ << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  if (terrain->InCellBounds(tileX, tileZ) && surfaceLayer->GetOccupant(point, &occupant)) {
    NwConnection *piece = nullptr;
    if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&piece))) {
      result = piece->TileId();
      piece->Release();
    } else
      result = occupant->GetAttribData()->Key()->instance;
    occupant->Release();
  }
  return result;
}

// Check the changing vertex's adjacent cells without altering terrain, zones or occupants.
// Always visit all three layers within a corner: a matching network may restore an earlier rejected result.
bool NwManager::CanPrepareVertex(u32 x, u32 z, u8 altitude, i32 networkType) {
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScOccupant *occupant = nullptr;
  if (!terrain->InVertexBounds(x, z))
    return false;
  u8 previous;
  terrain->GetVertexAltitude(x, z, &previous);
  if (previous == altitude)
    return true;
  static const i32 corners[4][2] = {{-1, -1}, {-1, 0}, {0, 0}, {0, -1}};
  bool valid = true;
  for (u32 corner = 0; valid && corner < 4; ++corner) {
    u32 cellX = x + corners[corner][0], cellZ = z + corners[corner][1];
    if (!terrain->InCellBounds(cellX, cellZ))
      continue;
    point = {i32(cellX << kCtMicroCoordinateShift), i32(cellZ << kCtMicroCoordinateShift), 0};
    for (u32 index = 0; index < 3; ++index) {
      ScOccupantManager *layer = index == 0 ? surfaceLayer : index == 1 ? undergroundOne : undergroundTwo;
      if (layer->GetOccupant(point, &occupant)) {
        u32 piece = 0;
        void *connection = nullptr;
        if (occupant->QueryInterface(IID_NwConnection, &connection)) {
          piece = static_cast<NwConnection *>(connection)->TileId();
          static_cast<NwConnection *>(connection)->Release();
        }
        valid = (valid && occupant->MajorType() == 5) ||
            (MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece));
        occupant->Release();
        occupant = nullptr;
      }
    }
  }
  return valid;
}

// Validate all three occupant layers around a changing vertex before setting its altitude.
// Then clear adjacent zones and remove user-removable major-type-five surface occupants in the original corner order.
bool NwManager::PrepareVertex(u32 x, u32 z, u8 altitude, i32 networkType) {
  CtCoord point{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScOccupant *occupant = nullptr;
  if (!terrain->InVertexBounds(x, z))
    return false;
  u8 previous;
  terrain->GetVertexAltitude(x, z, &previous);
  if (previous == altitude)
    return true;
  static const i32 corners[4][2] = {{-1, -1}, {-1, 0}, {0, 0}, {0, -1}};
  bool valid = true;
  for (u32 corner = 0; valid && corner < 4; ++corner) {
    u32 cellX = x + corners[corner][0], cellZ = z + corners[corner][1];
    if (!terrain->InCellBounds(cellX, cellZ))
      continue;
    point = {i32(cellX << kCtMicroCoordinateShift), i32(cellZ << kCtMicroCoordinateShift), 0};
    for (u32 index = 0; valid && index < 3; ++index) {
      ScOccupantManager *layer = index == 0 ? surfaceLayer : index == 1 ? undergroundOne : undergroundTwo;
      if (layer->GetOccupant(point, &occupant)) {
        u32 piece = GetTileId(occupant);
        valid = occupant->MajorType() == 5 ||
            (MatchesNetwork(networkType, occupant) && !IsProtectedPiece(networkType, piece));
        occupant->Release();
        occupant = nullptr;
      }
    }
  }
  if (!valid)
    return false;
  SetAltitude(x, z, altitude);
  for (u32 corner = 0; corner < 4; ++corner) {
    u32 cellX = x + corners[corner][0], cellZ = z + corners[corner][1];
    if (!terrain->InCellBounds(cellX, cellZ))
      continue;
    point = {i32(cellX << kCtMicroCoordinateShift), i32(cellZ << kCtMicroCoordinateShift), 0};
    u8 value = 0;
    zone->SetValue(cellX, cellZ, &value);
    if (surfaceLayer->GetOccupant(point, &occupant)) {
      if (occupant->MajorType() == 5 && occupant->CanUserRemove())
        surfaceLayer->RemoveOccupant(occupant);
      occupant->Release();
      occupant = nullptr;
    }
  }
  return true;
}

// Require a fresh four-corner signature with identical differences. Failed bounds leave the terrain unread.
bool NwManager::PlacementFlat(const CtCoord &position) {
  i8 signature[4];
  return PlacementSignature(position, signature) && signature[0] == signature[1] && signature[0] == signature[2] &&
      signature[0] == signature[3];
}

// Admit flat tiles and slopes of at most two altitude units along either axis.
// Interpret each corner difference as a signed byte before comparing its magnitude.
bool NwManager::PlacementSlope(const CtCoord &position) {
  i8 signature[4];
  if (!PlacementSignature(position, signature))
    return false;
  i32 difference = (i32)signature[0] - signature[2];
  return difference >= -2 && difference <= 2 &&
      ((signature[0] == signature[1] && signature[2] == signature[3]) ||
          (signature[1] == signature[2] && signature[3] == signature[0]));
}

bool NwManager::PlacementWater(const CtCoord &position) {
  return terrain->IsWater((u32)(position.x >> kCtMicroCoordinateShift), (u32)(position.z >> kCtMicroCoordinateShift));
}

// Match the installed corner patterns and choose the network's slope piece and rotation.
// Power and highway rows consult exact planned neighbors; publish the byte before the piece word.
u8 NwManager::SelectSlope(
    const CtCoord &position, i32 networkType, NwPositionList *slopes, u32 *piece, u8 *state, u8 toggle) {
  u32 table, row;
  switch (networkType) {
  case kNwRoadNetwork:
    table = 0;
    break;
  case kNwRailNetwork:
    table = 1;
    break;
  case kNwPowerNetwork:
    table =
        ((u32)(position.x >> kCtMicroCoordinateShift) + (u32)(position.z >> kCtMicroCoordinateShift)) % 5u == 0 ? 5 : 4;
    break;
  case kNwHighwayNetwork:
    table = toggle ? 6 : 7;
    break;
  case kNwPipeNetwork:
    table = 3;
    break;
  case kNwSubwayNetwork:
    table = 2;
    break;
  default:
    return 0;
  }
  i8 signature[4];
  if (!PlacementSignature(position, signature))
    return 0;
  for (row = 0; row < 8; ++row)
    if (signature[0] == NwSlopeSignatures[row][0] && signature[1] == NwSlopeSignatures[row][1] &&
        signature[2] == NwSlopeSignatures[row][2] && signature[3] == NwSlopeSignatures[row][3])
      break;
  if (row == 8)
    return 0;
  if (networkType == kNwPowerNetwork) {
    u32 dx = (row & 1) == 0, dz = row & 1;
    u32 x = (u32)(position.x >> kCtMicroCoordinateShift), z = (u32)(position.z >> kCtMicroCoordinateShift);
    bool positive = NwSlopeContains(*slopes, (x + dx) << kCtMicroCoordinateShift, (z + dz) << kCtMicroCoordinateShift);
    bool negative = NwSlopeContains(*slopes, (x - dx) << kCtMicroCoordinateShift, (z - dz) << kCtMicroCoordinateShift);
    if (!positive || !negative)
      table = 5;
  } else if (networkType == kNwHighwayNetwork) {
    u32 x = (u32)(position.x >> kCtMicroCoordinateShift), z = (u32)(position.z >> kCtMicroCoordinateShift);
    bool present = NwSlopeContains(
        *slopes, (x + (row & 1)) << kCtMicroCoordinateShift, (z + ((row & 1) == 0)) << kCtMicroCoordinateShift);
    table = present ? 7 : 6;
  }
  u32 word = kNwSlopePieces[table][row];
  *state = (u8)word;
  *piece = (word >> kNwPieceCodeShift) & 65535u;
  return 1;
}

// Original protected-table pointers start null and are populated by the network rule-data loading family.
NwPieceList *NwProtectedTables[6] = {};

// Select the protected-ID table for the actual network enum, then compare complete piece DWORDs.
// Unsupported network types return false before reading a module-global table.
bool NwManager::IsProtectedPiece(i32 networkType, u32 piece) {
  u32 table;
  switch (networkType) {
  case kNwRoadNetwork:
    table = 0;
    break;
  case kNwRailNetwork:
    table = 2;
    break;
  case kNwPowerNetwork:
    table = 5;
    break;
  case kNwHighwayNetwork:
    table = 1;
    break;
  case kNwPipeNetwork:
    table = 4;
    break;
  case kNwSubwayNetwork:
    table = 3;
    break;
  default:
    return false;
  }
  u32 *entry = NwProtectedTables[table]->begin, *end = NwProtectedTables[table]->end;
  for (; entry != end; ++entry)
    if (*entry == piece)
      return true;
  return false;
}

// Apply the installed metadata conversion scan, comparing every entry against the original piece and state. Publish
// identity first and let the last matching entry win; byte copies support packed record outputs.
void NwManager::ConvertPiece(i32 directionLimit, i32 networkType, u32 piece, u8 state, u32 *outPiece, u8 *outState) {
  NwConversion *current = nullptr, *end = nullptr;
  if ((directionLimit == 4 || directionLimit == 8 || directionLimit == 24) && networkType >= 1 && networkType <= 6) {
    static const u8 physicalIndex[6] = {0, 2, 5, 1, 4, 3};
    u8 index = physicalIndex[networkType - 1];
    NwConversions *table = directionLimit == 24 ? NwComplexConversions[index] : NwSimpleConversions[index];
    current = table->begin;
    end = table->end;
  }
  memcpy(outPiece, &piece, sizeof(piece));
  *outState = state;
  for (; current != end; ++current) {
    if (current->fromPiece == piece && current->fromState == state) {
      u32 target = current->toPiece;
      memcpy(outPiece, &target, sizeof(target));
      *outState = current->toState;
    }
  }
}

// Classify an occupant through the original positive/negative filters and its real attribute and sprite interfaces.
// Apply converted piece/state before the final direction lookup; a negative hit only changes piece/state.
u8 NwManager::ClassifyNeighbor(NwRuleRecord *record, const CtCoord &origin, ScOccupant *occupant, i32 x, i32 z,
    i32 directionLimit, i32 networkType, OcIteratorTest *positive, OcIteratorTest *negative) {
  if (positive->Test(occupant)) {
    GeAttrib *attributes = occupant->GetAttribData();
    const RsKey *key = attributes->Key();
    u16 piece;
    memcpy(&piece, &key->instance, sizeof(piece));
    u8 rotation;
    if (SpSprite *sprite = occupant->GetSpriteInst())
      sprite->GetRotationOffset(&rotation);
    else
      rotation = 0;
    ConvertPiece(directionLimit, networkType, piece, rotation, &record->piece, &record->state);
    i32 originX = origin.x >> kCtMicroCoordinateShift;
    if (originX != x || (origin.z >> kCtMicroCoordinateShift) != z) {
      i8 dz = i8(u32(z) - reinterpret_cast<const u8 *>(&origin.z)[1]);
      i8 dx = i8(u32(x) - u32(originX));
      NwRuleDirection(dz, dx, &record->direction);
    }
  } else if (negative->Test(occupant)) {
    bool generic = occupant->IsGeomFlag(kOcGeomFlora);
    record->piece = 0;
    record->state = generic ? 5 : 0;
  }
  return 1;
}

// Collect the original occupied/pending neighborhood and map-edge connections into owned rule records.
// The retained occupants and queried connection interfaces are released after their original observations.
u8 NwManager::CollectNeighborhood(NwNeighborhood **out, NwPositionList *pending, NwPositionList *work,
    NwRuleCollection *, ScOccupantManager *layer, OcIteratorTest *positive, OcIteratorTest *negative,
    const CtCoord &origin, i32 directionLimit, i32 networkType) {
  *out = new NwNeighborhood;
  i32 maximumX = i32(city->CellCountX() - 1), maximumZ = i32(city->CellCountZ() - 1);
  CtCoord lower = origin, upper = origin;
  if (directionLimit == 24) {
    i32 centerX = lower.x >> kCtMicroCoordinateShift, centerZ = lower.z >> kCtMicroCoordinateShift;
    i16 edge = -1;
    if (centerX == 0)
      edge = 0;
    if (centerX == i32(terrain->VertexCountX() - 2))
      edge = 2;
    if (centerZ == 0)
      edge = 1;
    if (centerZ == i32(terrain->VertexCountZ() - 2))
      edge = 3;
    for (i32 x = centerX - 2; x <= centerX + 2; ++x) {
      for (i32 z = centerZ - 2; z <= centerZ + 2; ++z) {
        CtCoord position = {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
        NwRuleRecord *record = new NwRuleRecord;
        if (layer->IsInBounds(position)) {
          ScOccupant *occupant = nullptr;
          if (layer->GetOccupant(position, &occupant)) {
            if (ClassifyNeighbor(record, origin, occupant, x, z, 24, networkType, positive, negative))
              NwRuleDirection(i8(u32(z) - reinterpret_cast<const u8 *>(&origin.z)[1]),
                  i8(u32(x) - reinterpret_cast<const u8 *>(&origin.x)[1]), &record->direction);
            if (record->piece == 0 && record->state == 5) {
              for (NwPositionNode *node = work->head->next; node != work->head; node = node->next)
                if ((node->position.x >> kCtMicroCoordinateShift) == x &&
                    (node->position.z >> kCtMicroCoordinateShift) == z)
                  record->state = 1;
            }
            occupant->Release();
          } else {
            record->piece = 0;
            record->state = 5;
            NwRuleDirection(i8(u32(z) - reinterpret_cast<const u8 *>(&origin.z)[1]),
                i8(u32(x) - reinterpret_cast<const u8 *>(&origin.x)[1]), &record->direction);
            for (NwPositionNode *node = work->head->next; node != work->head; node = node->next)
              if ((node->position.x >> kCtMicroCoordinateShift) == x &&
                  (node->position.z >> kCtMicroCoordinateShift) == z)
                record->state = 1;
          }
        } else {
          record->piece = 0;
          record->state = 5;
          NwRuleDirection(i8(u32(z) - reinterpret_cast<const u8 *>(&origin.z)[1]),
              i8(u32(x) - reinterpret_cast<const u8 *>(&origin.x)[1]), &record->direction);
          if (edge == record->direction && neighbors->HasConnection(networkType, centerX, centerZ))
            record->state = 1;
        }
        *(*out)->observations.records.Append() = *record;
        delete record;
      }
    }
  } else {
    lower.x = (lower.x >> kCtMicroCoordinateShift) != 0 ? lower.x - kCtMicroUnitsPerCell : 0;
    lower.z = (lower.z >> kCtMicroCoordinateShift) != 0 ? lower.z - kCtMicroUnitsPerCell : 0;
    upper.x = (upper.x >> kCtMicroCoordinateShift) >= i16(maximumX) ? i32(i16(maximumX)) * kCtMicroUnitsPerCell
                                                                    : upper.x + kCtMicroUnitsPerCell;
    upper.z = (upper.z >> kCtMicroCoordinateShift) >= i16(maximumZ) ? i32(i16(maximumZ)) * kCtMicroUnitsPerCell
                                                                    : upper.z + kCtMicroUnitsPerCell;
    i32 firstX = (lower.x < upper.x ? lower.x : upper.x) >> kCtMicroCoordinateShift;
    i32 lastX = upper.x >> kCtMicroCoordinateShift, firstZ = lower.z >> kCtMicroCoordinateShift,
        lastZ = upper.z >> kCtMicroCoordinateShift;
    ScOccupant *occupant = nullptr;
    for (i32 x = firstX; x <= lastX; ++x) {
      for (i32 z = firstZ; z <= lastZ; ++z) {
        if (!layer->GetOccupantAtCell(u32(x), u32(z), &occupant))
          continue;
        if (positive->Test(occupant)) {
          u8 direction;
          NwRuleDirection(i8(u32(z) - reinterpret_cast<const u8 *>(&origin.z)[1]),
              i8(u32(x) - reinterpret_cast<const u8 *>(&origin.x)[1]), &direction);
          if (direction < directionLimit) {
            bool connected = true;
            if (directionLimit == 4 &&
                (networkType == kNwRoadNetwork || networkType == kNwRailNetwork || networkType == kNwSubwayNetwork)) {
              u32 kind = networkType == kNwRoadNetwork ? 0 : networkType == kNwRailNetwork ? 1 : 2;
              u8 first = 0, second = 0, third = 0, fourth = 0;
              if (networkType != kNwSubwayNetwork || !occupant->IsGeomFlag(0x40100)) {
                void *view = nullptr;
                if (occupant->QueryInterface(IID_NwConnection, &view)) {
                  u32 side = (direction + 2) & 3;
                  static_cast<NwConnection *>(view)->GetConnectionSamples(
                      kind, 1, side, &first, &second, &third, &fourth);
                  if (!first && !third && !second && !fourth)
                    static_cast<NwConnection *>(view)->GetConnectionSamples(
                        kind, 0, side, &first, &second, &third, &fourth);
                  static_cast<NwConnection *>(view)->Release();
                }
                connected = first || second || third || fourth;
              }
            }
            if (connected)
              (*out)->directionMask |= 1u << direction;
          }
        }
        NwRuleRecord *record = new NwRuleRecord;
        if (ClassifyNeighbor(record, origin, occupant, x, z, directionLimit, networkType, positive, negative)) {
          *(*out)->observations.records.Append() = *record;
          delete record;
        }
        occupant->Release();
        occupant = nullptr;
      }
    }
    NwPositionNode *head = pending->head;
    for (NwPositionNode *node = head->next; node != head; node = node->next) {
      if (abs((node->position.z >> kCtMicroCoordinateShift) - (origin.z >> kCtMicroCoordinateShift)) < 3 &&
          abs((node->position.x >> kCtMicroCoordinateShift) - (origin.x >> kCtMicroCoordinateShift)) < 3) {
        u8 direction;
        NwRuleDirection(
            i8(reinterpret_cast<const u8 *>(&node->position.z)[1] - reinterpret_cast<const u8 *>(&origin.z)[1]),
            i8(reinterpret_cast<const u8 *>(&node->position.x)[1] - reinterpret_cast<const u8 *>(&origin.x)[1]),
            &direction);
        if (layer->GetOccupantAtCell(u32(node->position.x >> kCtMicroCoordinateShift),
                u32(node->position.z >> kCtMicroCoordinateShift), &occupant)) {
          if (!positive->Test(occupant) && direction < directionLimit)
            (*out)->directionMask |= 1u << direction;
          occupant->Release();
          occupant = nullptr;
        } else if (direction < directionLimit)
          (*out)->directionMask |= 1u << direction;
      }
    }
    i32 x = origin.x >> kCtMicroCoordinateShift, z = origin.z >> kCtMicroCoordinateShift;
    u32 edges = x == 0 ? 8 : 0;
    if (x == i32(terrain->VertexCountX() - 2))
      edges |= 2;
    if (z == 0)
      edges |= 1;
    if (z == i32(terrain->VertexCountZ() - 2))
      edges |= 4;
    for (u32 direction = 1; edges; ++direction, edges >>= 1)
      if ((edges & 1) && neighbors->HasConnection(networkType, x, z))
        (*out)->directionMask |= 1u << (direction > 3 ? 0 : direction);
  }
  return 1;
}

// Apply the result records in order, retaining the last insert reply across removals and failed creations.
// Replacement requires an accepted, removable incumbent. The retained insert state controls the zone-map sample.
u8 NwManager::CommitRules(ScOccupantManager *layer, const CtCoord &origin, NwRuleRecords *results,
    OcIteratorTest *replacement, OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32) {
  if (!allowWater && PlacementWater(origin))
    return 0;
  u8 inserted = 0;
  void *view = nullptr;
  const NwRuleRecord *records = results->records.data;
  u32 count = results->records.size;
  for (u32 index = 0; index < count; ++index) {
    u32 piece = records[index].piece;
    u8 state = records[index].state;
    i8 dx, dz;
    NwRuleOffset(records[index].direction, &dx, &dz);
    CtCoord target = {i32((u32(origin.x >> kCtMicroCoordinateShift) + u32(i32(dx))) << kCtMicroCoordinateShift),
        i32((u32(origin.z >> kCtMicroCoordinateShift) + u32(i32(dz))) << kCtMicroCoordinateShift), 0};
    if (!layer->IsInBounds(target))
      continue;
    if (piece) {
      if (CreateOccupant(&view, IID_ScOccupant, piece, state)) {
        u8 altitude;
        terrain->GetVertexAltitudeDirt(
            u32(target.x >> kCtMicroCoordinateShift), u32(target.z >> kCtMicroCoordinateShift), &altitude);
        CtCoord position = {target.x, target.z, i32(u32(altitude) << kCtMicroCoordinateShift)};
        static_cast<ScOccupant *>(view)->SetLocation(position);
        inserted = layer->InsertOccupant(static_cast<ScOccupant *>(view));
        if (!inserted) {
          ScOccupant *incumbent = nullptr;
          if (layer->GetOccupant(target, &incumbent)) {
            if ((replacement->Test(incumbent) || crossing->Test(incumbent)) && incumbent->CanUserRemove()) {
              layer->RemoveOccupant(incumbent);
              inserted = layer->InsertOccupant(static_cast<ScOccupant *>(view));
            }
            incumbent->Release();
          }
        }
        static_cast<ScOccupant *>(view)->Release();
        view = nullptr;
      }
    } else {
      ScOccupant *occupant = static_cast<ScOccupant *>(view);
      if (layer->GetOccupantAtCell(
              u32(target.x >> kCtMicroCoordinateShift), u32(target.z >> kCtMicroCoordinateShift), &occupant)) {
        if (occupant->CanUserRemove())
          layer->RemoveOccupant(occupant);
        occupant->Release();
        occupant = nullptr;
      }
      view = occupant;
    }
    if (inserted && writeSide) {
      u8 value = 0;
      zone->GetValue(u32(target.x >> kCtMicroCoordinateShift), u32(target.z >> kCtMicroCoordinateShift), &value);
    }
  }
  return inserted;
}

static const u32 kNwCrossingMatrix[6][6] = {{0, 0, 0, 0, 0, 0}, {0, 0, 0x4500, 0x4300, 0x2bfa02, 0x3afa02},
    {0, 0x4501, 0, 0x4700, 0x3e8602, 0x3e8702}, {0, 0x4301, 0x4701, 0, 0x3abc02, 0x3abd02},
    {0, 0x2bfa00, 0x3e8600, 0x3abc00, 0, 0}, {0, 0x3afa00, 0x3e8700, 0x3abd00, 0, 0}};

// Replace a recognized crossing using the installed orientation matrix and low-WORD highway direction tests.
// Keep lookup and factory outputs live across callbacks, and sample zoning only after a successful insertion.
u8 NwManager::ReplaceCrossing(ScOccupantManager *layer, const CtCoord &position, OcIteratorTest *positive,
    OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32 networkType, i16 dx, i16 dz, u8 toggle) {
  if (!allowWater && PlacementWater(position))
    return 0;
  u8 inserted = 0;
  ScOccupant *occupant = nullptr;
  if (!layer->GetOccupant(position, &occupant))
    return inserted;
  u32 piece = GetTileId(occupant);
  if ((positive->Test(occupant) || crossing->Test(occupant)) && !IsProtectedPiece(networkType, piece)) {
    // The foreign getter overwrites this scratch byte before every defined use.
    u8 rotation = 0;
    occupant->GetSpriteInst()->GetRotationOffset(&rotation);
    u32 requested = u8(networkType), existing = 0;
    bool recognized = true, forward;
    if (networkType == kNwHighwayNetwork) {
      i32 x = dx, z = dz;
      if (u16(dz))
        requested = 5;
      forward = toggle ? x <= 0 && z <= 0 : x >= 0 && z >= 0;
    } else
      forward = rotation != 0;
    switch (piece) {
    case 14:
    case 92:
      existing = 3;
      break;
    case 15:
      existing = 3;
      if (!rotation)
        forward = true;
      break;
    case 29:
      existing = 1;
      break;
    case 11204:
      existing = 1;
      if (networkType != kNwHighwayNetwork && rotation == 2)
        forward = false;
      break;
    case 44:
      existing = 2;
      break;
    case 73:
      existing = 4;
      break;
    case 15051:
      existing = 5;
      break;
    default:
      recognized = false;
      break;
    }
    u32 packed = forward ? kNwCrossingMatrix[requested][existing] : kNwCrossingMatrix[existing][requested];
    if (recognized && packed) {
      u32 targetPiece;
      u8 state;
      NwRuleDecode(i32(packed), &targetPiece, &state);
      void *view;
      if (CreateOccupant(&view, IID_ScOccupant, targetPiece, state)) {
        if (occupant->CanUserRemove()) {
          layer->RemoveOccupant(occupant);
          // The terrain provider supplies this byte; argument padding is not game state.
          u8 altitude = 0;
          terrain->GetVertexAltitude(
              u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &altitude);
          CtCoord target = {position.x, position.z, i32(u32(altitude) << kCtMicroCoordinateShift)};
          static_cast<ScOccupant *>(view)->SetLocation(target);
          inserted = layer->InsertOccupant(static_cast<ScOccupant *>(view));
        }
        static_cast<ScOccupant *>(view)->Release();
      }
    }
  }
  occupant->Release();
  occupant = nullptr;
  if (inserted && writeSide) {
    u8 value = 0;
    zone->GetValue(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &value);
  }
  return inserted;
}

// Evaluate each flat work cell through the real collector and rule matcher, validating each selected result in order.
// Commit accepted rules and append live target coordinates to the changed or deferred queue without deduplication.
void NwManager::ApplyRules(NwPositionList *work, NwPositionList *pending, NwPositionList *changed,
    NwPositionList *deferred, NwRuleCollection *rules, ScOccupantManager *layer, OcIteratorTest *positive,
    OcIteratorTest *negative, OcIteratorTest *crossing, u8 writeSide, u8 allowWater, i32 directionLimit,
    i32 networkType) {
  NwPositionNode *head = work->head;
  for (NwPositionNode *node = head->next; node != head; node = node->next) {
    const CtCoord *origin = &node->position;
    if (!PlacementFlat(*origin))
      continue;
    NwNeighborhood *neighborhood;
    CollectNeighborhood(
        &neighborhood, pending, work, rules, layer, positive, negative, *origin, directionLimit, networkType);
    NwRuleRecords results;
    if (rules->Match(neighborhood, &results) && results.records.size) {
      const NwRuleRecord *records = results.records.data;
      u32 count = results.records.size;
      bool accepted = true;
      for (u32 index = 0; index < count && accepted; ++index) {
        u32 resultPiece = records[index].piece;
        u8 state = records[index].state;
        i8 dx, dz;
        NwRuleOffset(records[index].direction, &dx, &dz);
        CtCoord target = {i32((u32(origin->x >> kCtMicroCoordinateShift) + u32(i32(dx))) << kCtMicroCoordinateShift),
            i32((u32(origin->z >> kCtMicroCoordinateShift) + u32(i32(dz))) << kCtMicroCoordinateShift), 0};
        if (layer->IsInBounds(target)) {
          if (!PlacementFlat(target))
            accepted = false;
          ScOccupant *occupant = nullptr;
          if (layer->GetOccupant(target, &occupant)) {
            u32 piece = GetTileId(occupant);
            if ((!positive->Test(occupant) && !crossing->Test(occupant)) ||
                (networkType != kNwPowerNetwork && IsProtectedPiece(networkType, piece) && (resultPiece || state != 5)))
              accepted = false;
            occupant->Release();
            occupant = nullptr;
          }
        }
      }
      if (accepted) {
        CommitRules(layer, *origin, &results, positive, crossing, writeSide, allowWater, networkType);
        records = results.records.data;
        count = results.records.size;
        for (u32 index = 0; index < count; ++index) {
          i8 dx, dz;
          NwRuleOffset(records[index].direction, &dx, &dz);
          CtCoord target = {i32((u32(origin->x >> kCtMicroCoordinateShift) + u32(i32(dx))) << kCtMicroCoordinateShift),
              i32((u32(origin->z >> kCtMicroCoordinateShift) + u32(i32(dz))) << kCtMicroCoordinateShift), 0};
          if (!records[index].piece && !records[index].state)
            deferred->Append(target);
          else
            changed->Append(target);
        }
      }
    }
    delete neighborhood;
  }
}

// Count the actual linked nodes rather than caching a size that callbacks could invalidate.
static u32 NwPositionCount(const NwPositionList &positions) {
  u32 count = 0;
  for (NwPositionNode *node = positions.head->next; node != positions.head; node = node->next)
    ++count;
  return count;
}

static bool NwPositionLess(const CtCoord &first, const CtCoord &second) {
  return first.x < second.x || (first.x == second.x && first.z < second.z);
}

// Merge two sorted coordinate chains stably, leaving their nodes and payloads independently owned.
static NwPositionNode *NwMergePositions(NwPositionNode *first, NwPositionNode *second) {
  NwPositionNode *result = nullptr, **tail = &result;
  while (first && second) {
    NwPositionNode **selected = NwPositionLess(second->position, first->position) ? &second : &first;
    *tail = *selected;
    *selected = (*selected)->next;
    tail = &(*tail)->next;
  }
  *tail = first ? first : second;
  return result;
}

// Split and sort a null-terminated coordinate chain without replacing its stable node storage.
static NwPositionNode *NwSortPositionChain(NwPositionNode *first, u32 count) {
  if (count < 2)
    return first;
  u32 leftCount = count / 2;
  NwPositionNode *split = first;
  for (u32 index = 1; index < leftCount; ++index)
    split = split->next;
  NwPositionNode *second = split->next;
  split->next = nullptr;
  return NwMergePositions(NwSortPositionChain(first, leftCount), NwSortPositionChain(second, count - leftCount));
}

// Restore the original stable signed-coordinate sort and unique payload compaction, retaining its ignored tail.
// The original caller discards unique's returned end; deleting tail nodes would change subsequent game work.
static void NwSortPositions(NwPositionList *positions) {
  u32 count = NwPositionCount(*positions);
  if (count < 2)
    return;
  NwPositionNode *head = positions->head;
  head->previous->next = nullptr;
  head->next = NwSortPositionChain(head->next, count);
  NwPositionNode *previous = head, *node = head->next;
  while (node) {
    node->previous = previous;
    previous = node;
    node = node->next;
  }
  previous->next = head;
  head->previous = previous;
  NwPositionNode *last = head->next;
  for (node = last->next; node != head; node = node->next)
    if (node->position.x != last->position.x || node->position.z != last->position.z) {
      last = last->next;
      if (last != node)
        last->position = node->position;
    }
}

// Apply queued removals in their original order without snapshotting either list's coordinates.
static void NwRemovePositions(NwPositionList *positions, const NwPositionList &remove) {
  NwPositionNode *head = remove.head;
  for (NwPositionNode *node = head->next; node != head; node = node->next)
    positions->RemoveXZ(node->position);
}
