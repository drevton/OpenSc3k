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

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
static constexpr u32 kGeFloraRecordType = 0x406b1196;
static constexpr u32 kGeFloraRecordGroup = 0x80ab8ab0;

bool GeBounds::CoordInBoundsXZ(CtCoord point) {
  return point.x >= minimum.x && point.z >= minimum.z && point.x <= maximum.x && point.z <= maximum.z;
}

// Combine the two horizontal rectangles in field order, including when the destination aliases either input.
// Both altitude endpoints are reset to the original unknown-value sentinel.
void GeBounds::UnionXZ(const GeBounds &first, const GeBounds &second) {
  minimum.x = first.minimum.x < second.minimum.x ? first.minimum.x : second.minimum.x;
  minimum.z = first.minimum.z < second.minimum.z ? first.minimum.z : second.minimum.z;
  maximum.x = first.maximum.x > second.maximum.x ? first.maximum.x : second.maximum.x;
  maximum.z = first.maximum.z > second.maximum.z ? first.maximum.z : second.maximum.z;
  minimum.altitude = kCtUnknownCoordinate;
  maximum.altitude = kCtUnknownCoordinate;
}

// Order each horizontal endpoint pair independently and reset both altitude endpoints to the unknown sentinel.
void GeBounds::NormalizeXZ() {
  if (minimum.x > maximum.x) {
    i32 value = minimum.x;
    minimum.x = maximum.x;
    maximum.x = value;
  }
  if (minimum.z > maximum.z) {
    i32 value = minimum.z;
    minimum.z = maximum.z;
    maximum.z = value;
  }
  minimum.altitude = kCtUnknownCoordinate;
  maximum.altitude = kCtUnknownCoordinate;
}

// Intersect the horizontal extents in original field order, including when either input is the destination.
// A disjoint intersection remains inverted; both altitude endpoints become the unknown sentinel.
void GeBounds::IntersectXZ(const GeBounds &first, const GeBounds &second) {
  minimum.x = first.minimum.x > second.minimum.x ? first.minimum.x : second.minimum.x;
  minimum.z = first.minimum.z > second.minimum.z ? first.minimum.z : second.minimum.z;
  maximum.x = first.maximum.x < second.maximum.x ? first.maximum.x : second.maximum.x;
  maximum.z = first.maximum.z < second.maximum.z ? first.maximum.z : second.maximum.z;
  minimum.altitude = kCtUnknownCoordinate;
  maximum.altitude = kCtUnknownCoordinate;
}

// Extend a horizontal rectangle to contain the by-value point in original field order.
// Preserve aliasing with the source bounds and replace both altitude endpoints with the unknown sentinel.
void GeBounds::UnionXZ(const GeBounds &first, CtCoord point) {
  minimum.x = first.minimum.x < point.x ? first.minimum.x : point.x;
  minimum.z = first.minimum.z < point.z ? first.minimum.z : point.z;
  maximum.x = first.maximum.x > point.x ? first.maximum.x : point.x;
  maximum.z = first.maximum.z > point.z ? first.maximum.z : point.z;
  minimum.altitude = kCtUnknownCoordinate;
  maximum.altitude = kCtUnknownCoordinate;
}

// Expand both horizontal axes by the requested number of fixed-point cells with unsigned wraparound.
// Reset both altitude endpoints to the original unknown sentinel.
void GeBounds::GrowXZCell(u32 cells) {
  u32 distance = cells << kCtMicroCoordinateShift;
  minimum.x = i32(u32(minimum.x) - distance);
  minimum.z = i32(u32(minimum.z) - distance);
  maximum.x = i32(u32(maximum.x) + distance);
  maximum.z = i32(u32(maximum.z) + distance);
  minimum.altitude = kCtUnknownCoordinate;
  maximum.altitude = kCtUnknownCoordinate;
}

GeByteMap::GeByteMap() : references(0), initialized(0), width(0), height(0), coordinateShift(2), grid(nullptr) {
}

GeByteMap::~GeByteMap() {
}

bool GeByteMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, ScByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapU8, ScByteMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the map's reference before retaining its module director, then return the live count after that call.
u32 GeByteMap::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Drop a module reference before decrementing the live nonzero object count. Zero dispatches complete destruction.
u32 GeByteMap::Release() {
  if (references) {
    GeGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

u32 GeByteMap::CellCountX() const {
  return width;
}

u32 GeByteMap::CellCountZ() const {
  return height;
}

void GeByteMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinateShift][z >> coordinateShift];
}

bool GeByteMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool GeByteMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool GeByteMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool GeByteMap::SerialRead(RsStream *record) {
  return grid->Read(record);
}

bool GeByteMap::SerialWrite(RsStream *record) const {
  return grid->Write(record);
}

// Shut down an initialized map through its virtual override before querying city dimensions in X/Z order.
// Construct the coarse grid using the live shift and publish it before marking the map initialized.
bool GeByteMap::Init(CtCity *city, const u8 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  u32 shift = coordinateShift, countX = width >> shift, countZ = height >> shift;
  grid = new GeFlagGrid(countX, countZ, fill);
  initialized = 1;
  return true;
}

// Delete a published grid only while initialized, clearing its pointer after destruction. Dimensions remain unchanged.
bool GeByteMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = 0;
  return true;
}

void GeByteMap::GetValue(u32 x, u32 z, u8 *value) const {
  *value = grid->columns[x >> coordinateShift][z >> coordinateShift];
}

void GeByteMap::SetValue(u32 x, u32 z, const u8 *value) {
  grid->columns[x >> coordinateShift][z >> coordinateShift] = *value;
}

// Fill the inclusive scaled rectangle in ascending X/Z order. Read the supplied byte separately for each assignment.
void GeByteMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  u32 shift = coordinateShift, startZ = z0 >> shift, endZ = z1 >> shift;
  for (u32 x = x0 >> shift; x <= x1 >> shift; ++x)
    for (u32 z = startZ; z <= endZ; ++z)
      grid->columns[x][z] = *value;
}

// Fill every coarse column, capturing the grid and its row length before the original live-width traversal.
void GeByteMap::SetAllCells(const u8 *value) {
  GeFlagGrid *storage = grid;
  u32 countZ = storage->height;
  for (u32 x = 0; x < storage->width; ++x)
    memset(storage->columns[x], *value, countZ);
}

GeCellIterator::GeCellIterator(GeLayer *value)
    : references(0), manager(value), test(nullptr), left(0), top(0), right(0), bottom(0), current(nullptr), x(0), z(0) {
  manager->AddRef();
}

GeCellIterator::~GeCellIterator() {
  GeCellIterator::Shutdown();
  ScRelease(&manager);
}

// Publish either supported iterator identity and acquire one reference. Unsupported queries clear the output.
bool GeCellIterator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeCellIterator);
  HANDLE_QUERY_INTERFACE(IID_OcIterator, GeCellIterator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeCellIterator::AddRef() {
  GeGetDirector()->AddRef();
  return ++references;
}

// Remove the DLL reference before decrementing a nonzero iterator count. Shutdown and delete at zero.
u32 GeCellIterator::Release() {
  if (references) {
    GeGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    Shutdown();
    delete this;
    return 0;
  }
  return references;
}

// Establish the truncated 16-bit region and retained filter, then reset to its first occupant.
// Bounds words one/four are Z in the canonical X/Z/altitude coordinate layout.
bool GeCellIterator::Init(const GeBounds *bounds, OcIteratorTest *value) {
  if (test)
    return false;
  if (bounds) {
    left = u16(bounds->minimum.x >> kCtMicroCoordinateShift);
    top = u16(bounds->minimum.z >> kCtMicroCoordinateShift);
    u16 edge = u16(bounds->maximum.x >> kCtMicroCoordinateShift), limit = u16(manager->GetCellsInX() - 1);
    right = edge < limit ? edge : limit;
    edge = u16(bounds->maximum.z >> kCtMicroCoordinateShift);
    limit = u16(manager->GetCellsInZ() - 1);
    bottom = edge < limit ? edge : limit;
  } else {
    left = top = 0;
    right = u16(manager->GetCellsInX() - 1);
    bottom = u16(manager->GetCellsInZ() - 1);
  }
  test = ScAddRefNotNull(value ? value : new GeIteratorTest);
  Reset();
  return true;
}

// Release the retained filter before the current occupant, clearing each field after its callback returns.
bool GeCellIterator::Shutdown() {
  ScRelease(&test);
  ScRelease(&current);
  return true;
}

// Release the previous occupant and scan columns from the region's first cell. Exhaustion clears both cursor words.
ScOccupant *GeCellIterator::Reset() {
  ScRelease(&current);
  x = left;
  while (x <= right) {
    z = top;
    while (z <= bottom) {
      current = QueryCell(x, z);
      if (current)
        return current;
      ++z;
    }
    ++x;
  }
  x = z = 0;
  return current;
}

// Borrow the occupant currently retained by the iterator without acquiring another reference.
ScOccupant *GeCellIterator::Current() {
  return current;
}

// Release the current occupant and continue after the cursor, scanning the second axis before advancing the first.
ScOccupant *GeCellIterator::Next() {
  ScRelease(&current);
  for (;;) {
    if (++z > bottom) {
      ++x;
      z = top;
      if (x > right)
        return nullptr;
    }
    current = QueryCell(x, z);
    if (current)
      return current;
  }
}

// Resolve occupied-cell redirects and select the clipped lower-right footprint cell so an occupant is yielded once.
// Keep the grid locked through bounds/filter callbacks and reference acquisition, using the original empty-overlap result.
ScOccupant *GeCellIterator::QueryCell(u16 cellX, u16 cellZ) {
  ScLock *lock = manager->gridLock;
  lock->Lock();
  u8 flags = manager->cellFlags->columns[cellX][cellZ];
  if (flags & 1) {
    GeCellGrid *grid = manager->cells;
    GeCell value = grid->columns[cellX][cellZ];
    u16 firstX = left, firstZ = top, lastX = right, lastZ = bottom;
    ScOccupant *occupant =
        flags & 2 ? grid->columns[u16(value.anchor)][u16(value.anchor >> 16)].occupant : value.occupant;
    GeCellRect bounds;
    occupant->GetBoundingRectInCellCoords(&bounds);
    u32 selectedX = lastX, selectedZ = bounds.bottom;
    if (bounds.right < firstX || bounds.left > lastX || bounds.bottom < firstZ || bounds.top > lastZ) {
      selectedX = 0;
      selectedZ = 0;
    } else {
      if (bounds.right < selectedX)
        selectedX = bounds.right;
      if (bounds.bottom >= lastZ)
        selectedZ = lastZ;
    }
    if (selectedX == cellX && selectedZ == cellZ && test->Test(occupant)) {
      occupant->AddRef();
      lock->Unlock();
      return occupant;
    }
  }
  lock->Unlock();
  return nullptr;
}

GeRandom *GeFloraController::random;

GeFloraController::GeFloraController()
    : application(nullptr), city(nullptr), terrain(nullptr), initialized(0), references(0), occupants(nullptr),
      bounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}},
      extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}} {
}

GeFloraController::~GeFloraController() {
}

// Publish supported identities at the same primary receiver before acquiring one controller reference. Unsupported
// identifiers clear the output.
bool GeFloraController::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeFlora);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, GeFlora);
  HANDLE_QUERY_INTERFACE(IID_GeFlora, GeFlora);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the actual reference word before retaining the DLL. Return the live count after the module callback.
u32 GeFloraController::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Decrement a nonzero controller count, then remove a module reference even if the count began at zero.
// Delete at the live zero count without calling Shutdown or clearing retained service pointers.
u32 GeFloraController::Release() {
  if (references)
    --references;
  GeGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool GeFloraController::StaticShutdown() {
  ScDelete(&random);
  return true;
}

// Release the occupant manager, terrain, budget and city in order, clearing each pointer after its callback.
// The original retains the application pointer and does not clear the initialized byte here.
bool GeFloraController::Shutdown() {
  ScRelease(&occupants);
  ScRelease(&terrain);
  ScRelease(&budget);
  ScRelease(&city);
  return true;
}

u32 GeFloraController::GetLayerType() {
  return kGeFloraControllerLayerType;
}

bool GeFloraController::DoMessage(ScMessage *) {
  return false;
}

bool GeFloraController::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool GeFloraController::SimulationBegin() {
  return true;
}

bool GeFloraController::SimulationEnd() {
  return true;
}

bool GeFloraController::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return false;
}

// Acquire the city services in their original publication/retention order, then publish the application QI output.
// Successful initialization records inclusive cell bounds and exclusive world extents; failures delegate to Shutdown.
bool GeFloraController::Init(CtCity *value) {
  if (initialized)
    Shutdown();
  if (value) {
    city = value;
    value->AddRef();
    terrain = value->DirtBag();
    if (terrain) {
      terrain->AddRef();
      budget = value->BudgetLayer();
      if (budget) {
        budget->AddRef();
        occupants = value->SurfaceOccupantManager();
        if (occupants) {
          occupants->AddRef();
          ApApp *app = GeGetFramework()->Application();
          if (app && app->QueryInterface(IID_ApGameInterface, &application)) {
            initialized = 1;
            u32 x = value->CellCountX(), z = value->CellCountZ();
            bounds.minimum = {0, 0, 0};
            bounds.maximum = {i32((x << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell),
                i32((z << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell), 0};
            extent.minimum = bounds.minimum;
            extent.maximum = {i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift), 0};
            return true;
          }
        }
      }
    }
  }
  Shutdown();
  return false;
}

// Scan legacy plane two from the last cell toward the origin, translating flora cell codes 6..12 to densities 1..7.
// Planting results do not change import success; only a failed plane open calls Shutdown and returns false.
bool GeFloraController::Init(CtCity *, ScImporter *importer) {
  if (!importer->MapIsPresent(kCtLegacyXbldPlane)) {
    Shutdown();
    return false;
  }
  i32 last = i32(importer->GetMapSize(kCtLegacyXbldPlane)) - 1;
  for (i32 x = last; x >= 0; --x) {
    for (i32 z = last; z >= 0; --z) {
      u8 cell;
      importer->GetValue(kCtLegacyXbldPlane, u32(x), u32(z), &cell);
      i32 density;
      if (Convert2KIdToFloraDensity(cell, &density)) {
        CtCoord point{x << kCtMicroCoordinateShift, z << kCtMicroCoordinateShift, 0};
        SetDensity(point, u8(density));
      }
    }
  }
  return true;
}

// Decode the original SC2 flora cell range into a signed density and write zero for all other cell codes.
u8 GeFloraController::Convert2KIdToFloraDensity(u8 value, i32 *density) {
  if (value < 6 || value > 12) {
    *density = 0;
    return 0;
  }
  *density = i32(value) - 5;
  return 1;
}

// Classify flora from the byte altitude above sea level and the shore predicate, breaking overlapping bands randomly.
// Consume those random draws before the final shore selection, as the original does even when shore wins.
u32 GeFloraController::ChooseKind(const CtCoord &point) {
  u8 altitude;
  terrain->GetVertexAltitude(point.x >> kCtMicroCoordinateShift, point.z >> kCtMicroCoordinateShift, &altitude);
  altitude = u8(altitude - terrain->GetGlobalSeaLevel());
  bool shore = false;
  if (terrain->IsWater(point.x >> kCtMicroCoordinateShift, point.z >> kCtMicroCoordinateShift))
    shore = !terrain->IsRealWater(point.x >> kCtMicroCoordinateShift, point.z >> kCtMicroCoordinateShift);
  bool low = altitude != 0 && altitude < 5, middle = altitude > 2 && altitude < 127;
  bool high = altitude > 7 && altitude < 127;
  if (low && middle) {
    if (random->Next(2))
      middle = false;
    else
      low = false;
  }
  if (middle && high) {
    if (random->Next(2))
      high = false;
    else
      middle = false;
  }
  if (shore)
    return 1;
  if (low)
    return 2;
  if (middle)
    return 3;
  return high ? 4 : 3;
}

// Replace removable flora while preserving its kind, or choose a kind for an empty location using terrain and RNG.
// Set the created occupant's vertex altitude before insertion, capture acceptance, then release the temporary reference.
u8 GeFloraController::SetDensity(const CtCoord &point, u8 density) {
  CtCoord location = point;
  ScOccupant *previous = nullptr;
  u8 kind = 5, result = 0;
  bool allowed = false;
  if (occupants->GetOccupant(location, &previous)) {
    if (previous->MajorType() == 5 && previous->CanUserRemove()) {
      kind = previous->SubTypeInfo() >> 4;
      occupants->RemoveOccupant(previous);
      allowed = true;
    }
    previous->Release();
    previous = nullptr;
  } else
    allowed = true;
  if (allowed) {
    void *object = nullptr;
    if (kind == 5)
      kind = u8(ChooseKind(location));
    if (!terrain->IsWater(location.x >> kCtMicroCoordinateShift, location.z >> kCtMicroCoordinateShift) &&
        Create(kind, density, IID_ScOccupant, &object)) {
      u8 altitude;
      terrain->GetVertexAltitude(
          location.x >> kCtMicroCoordinateShift, location.z >> kCtMicroCoordinateShift, &altitude);
      location.altitude = u16(u16(altitude) << kCtMicroCoordinateShift);
      ScOccupant *created = static_cast<ScOccupant *>(object);
      created->SetLocation(location);
      result = occupants->InsertOccupant(created);
      created->Release();
    }
  }
  return result;
}

// Copy the borrowed attribute key before invoking the genuine key-based factory overload.
// Null attributes fail without touching the caller's output; the delegated result and output pass through unchanged.
bool GeFloraController::CreateOccupant(GeAttrib *attributes, u32 iid, void **out) {
  if (!attributes)
    return false;
  RsKey key = *attributes->Key();
  return CreateOccupant(&key, iid, out);
}

// Construct the actual flora occupant only for its original resource type. Hold it through key initialization and
// requested-interface discovery, then release the temporary reference while preserving the saved Boolean and output.
bool GeFloraController::CreateOccupant(RsKey *key, u32 iid, void **out) {
  if (key->type != kRsFloraAttributesResourceType)
    return false;
  GeOccupant *occupant = static_cast<GeOccupant *>(new GePlant);
  occupant->AddRef();
  bool result = false;
  if (occupant->Init(key))
    result = occupant->QueryInterface(iid, out);
  occupant->Release();
  return result;
}

GeFloraTest::~GeFloraTest() {
}

bool GeFloraTest::Test(ScOccupant *occupant) {
  return occupant->MajorType() == 5;
}

// Load the versioned packed flora grid and recreate its nonempty cells in ascending X/Z order. Dimension, stream and
// debug-marker failures select record abort; unsuccessful individual actor creation does not reject the loaded record.
bool GeFloraController::Init(CtCity *world, RsDatabaseSegment *segment) {
  NwRecordFrame frame(world, segment, RsKey{kGeFloraRecordType, kGeFloraRecordGroup, 0});
  u16 version = frame.version;
  u8 success = frame.valid, debug = frame.debug;
  RsStream *stream = frame.Stream();
  if (success && debug) {
    ScString marker;
    success = stream->ReadString(&marker);
  }
  if (version >= 3) {
    u32 width = 0, height = 0;
    if (success) {
      success = stream->ReadU32(&width);
      if (success)
        success = stream->ReadU32(&height);
    }
    if (width != world->CellCountX() || height != world->CellCountZ())
      success = 0;
    u8 zero = 0;
    GeFlagGrid *grid = new GeFlagGrid(width, height, &zero);
    if (success) {
      success = grid->Read(stream);
      if (success) {
        void *created = nullptr;
        for (u32 x = 0; x < width; ++x) {
          for (u32 z = 0; z < height; ++z) {
            u8 cell = grid->columns[x][z];
            if (cell && Create(cell >> 4, cell & 15, IID_ScOccupant, &created)) {
              u8 altitude;
              terrain->GetVertexAltitude(x, z, &altitude);
              static_cast<ScOccupant *>(created)->SetLocationInCellCoords(x, z);
              static_cast<ScOccupant *>(created)->SetAlt(altitude);
              occupants->InsertOccupant(static_cast<ScOccupant *>(created));
              static_cast<ScOccupant *>(created)->Release();
              created = nullptr;
            }
          }
        }
      }
    }
    delete grid;
  }
  if (success && debug) {
    ScString marker;
    success = stream->ReadString(&marker);
  }
  if (!success) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return success != 0;
}

// Collect flora subtypes at their current cell coordinates and write the version-three byte grid. Retire grid and
// iterator/filter ownership before closing the actual archive frame, preserving abort selection on body-write failure.
bool GeFloraController::Save(CtCity *world, RsDatabaseSegment *segment) {
  GeFloraTest *test = new GeFloraTest;
  test->AddRef();
  OcIterator *iterator = nullptr;
  if (!occupants->GetOccupants(&iterator, nullptr, test)) {
    test->Release();
    return false;
  }
  u32 width = world->CellCountX(), height = world->CellCountZ();
  u8 zero = 0;
  GeFlagGrid *grid = new GeFlagGrid(width, height, &zero);
  u32 x = 0, z = 0;
  for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next()) {
    occupant->GetLocationInCellCoords(&x, &z);
    if (x < width && z < height)
      grid->columns[x][z] = occupant->SubTypeInfo();
  }
  NwRecordFrame frame(world, segment, RsKey{kGeFloraRecordType, kGeFloraRecordGroup, 0}, 3, 0);
  u8 success = frame.valid;
  RsStream *stream = frame.Stream();
  if (success) {
    success = stream->WriteU32(width);
    if (success) {
      success = stream->WriteU32(height);
      if (success)
        success = grid->Write(stream);
    }
  }
  delete grid;
  if (iterator) {
    iterator->Shutdown();
    iterator->Release();
    iterator = nullptr;
  }
  test->Release();
  if (!success) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return success != 0;
}

GePlant::GePlant() {
}

GePlant::~GePlant() {
  GeOccupant::Shutdown();
}

bool GePlant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GePlantIdentity, GePlantIdentity);
  return GeOccupant::QueryInterface(iid, out);
}

u32 GePlant::AddRef() {
  return GeOccupant::AddRef();
}

u32 GePlant::Release() {
  return GeOccupant::Release();
}

u32 GePlant::GetGeomFlag() {
  return 0x404;
}

u8 GePlant::MajorType() const {
  return 5;
}

// Borrow the occupant subobject at complete-object offset four. The installed body is folded with other +4 interface
// getters; its flora table establishes the returned interface here.
ScOccupant *GePlant::AsISC3Occupant() {
  return static_cast<GeOccupant *>(this);
}

static const u16 GePlantInstances[100] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7054, 7054, 7054,
    7055, 7054, 7055, 7054, 7055, 7054, 7055, 7054, 7055, 7054, 7055, 7054, 7055, 7057, 7057, 7057, 7057, 7040, 7040,
    7041, 7041, 7042, 7042, 7043, 7043, 7044, 7044, 7045, 7045, 7046, 7053, 7046, 7053, 9200, 9200, 9200, 9200, 6, 6, 7,
    7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 7056, 12, 7056, 7057, 7057, 7057, 7057, 7000, 7000, 7001, 7001, 7002, 7002, 7003,
    7003, 7004, 7004, 7005, 7005, 7006, 7052, 7006, 7052, 9201, 9201, 9201, 9201};

// Select the original resource variant only for valid full-width kind/density inputs, then create its primary flora view.
// Apply the density effect before requested-interface discovery and release the temporary view after capturing success.
bool GeFloraController::Create(u32 kind, u32 density, u32 iid, void **out) {
  if (kind > 4 || density > 9)
    return false;
  u8 index = u8(2 * (density + 10 * kind) + (random->Next(2) != 0));
  RsKey key{kRsFloraAttributesResourceType, kRsFloraAttributesGroup, GePlantInstances[index]};
  void *published = nullptr;
  bool result = CreateOccupant(&key, IID_GePlantIdentity, &published);
  if (result) {
    auto *plant = static_cast<GePlantIdentity *>(published);
    plant->SetFloraDensity(density);
    result = plant->QueryInterface(iid, out);
    plant->Release();
  }
  return result;
}

// Initialize through the base key method before classifying the live key's instance in the original flora table.
// The first match replaces the entire instance subtype byte; missing instances leave that byte unchanged.
bool GePlant::Init(RsKey *key) {
  if (!GeOccupant::Init(key))
    return false;
  for (u8 i = 0; i < 100; ++i)
    if (key->instance == GePlantInstances[i]) {
      u8 subtype = u8(16 * (i / 20)) | ((i % 20) >> 1);
      flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << 16);
      break;
    }
  return true;
}

u32 GePlant::GetFloraDensity() {
  return (flags >> 16) & 15;
}

u32 GePlant::GetFloraType() {
  return (flags >> 20) & 15;
}

void GePlant::SetFloraDensity(u32 density) {
  flags |= u32(density & 15) << 16;
}

void GePlant::SetFloraType(u32 type) {
  flags |= u32(u8(type << 4)) << 16;
}

// Dispatch the two supported subtype masks through the live primary getters. Their full DWORD values pass through;
// other masks return zero without consulting either virtual getter.
u32 GePlant::GetSubTypeData(u32 mask) {
  if (mask == 0xff)
    return GetFloraDensity();
  if (mask == 0xff00)
    return GetFloraType();
  return 0;
}

// Use the localized special name for density nine and otherwise delegate directly to the base attribute name getter.
// A missing localized resource leaves the caller's string untouched; successful copy ignores the string method's result.
bool GePlant::GetName(ScStringView *out) {
  if (GetFloraDensity() != 9)
    return GeOccupant::GetName(out);
  GeTextResource resource(kRsSupplementalGameDevouredTreeText, kRsSupplementalGameTextGroup);
  if (!resource.text)
    return false;
  ScString value = resource.Val();
  out->Copy(value);
  return true;
}

bool GeFloraController::DebugClassTag(ScStringView *) {
  return false;
}

bool GeFloraController::DebugTypeTag(ScStringView *) {
  return false;
}

// Publish zero before looking up the occupant, then expose only the density nibble of actual major-type-five flora.
u8 GeFloraController::GetDensity(const CtCoord &point, u8 *density) {
  ScOccupant *occupant = nullptr;
  *density = 0;
  if (!occupants->GetOccupant(point, &occupant))
    return 1;
  u8 result = 0;
  if (occupant->MajorType() == 5) {
    result = 1;
    *density = occupant->SubTypeInfo() & 15;
  }
  occupant->Release();
  return result;
}

// Increase a retrieved density only when its signed byte is below six, preserving wrap of unusual supplied byte values.
// Return the actual density setter's byte without replacing its refusal or insertion result.
u8 GeFloraController::IncreaseDensity(const CtCoord &point) {
  u8 density = 0;
  if (GetDensity(point, &density) && i8(density) < 6) {
    ++density;
    return SetDensity(point, density);
  }
  return 0;
}

// Decrease only a retrieved signed density above two and forward the setter's complete byte reply.
u8 GeFloraController::DecreaseDensity(const CtCoord &point) {
  u8 density = 0;
  if (GetDensity(point, &density) && i8(density) > 2) {
    --density;
    return SetDensity(point, density);
  }
  return 0;
}

// Refuse nonflora occupancy and fully watered neighborhoods with no dry vertex in the clipped two-cell radius.
// Publish the live budget's type300 price even on refusal; no occupant or terrain test bypasses that cost lookup.
u8 GeFloraController::CanGrow(const CtCoord &point, u32 *cost) {
  ScOccupant *occupant = nullptr;
  bool blocked = false;
  if (occupants->GetOccupant(point, &occupant)) {
    if (occupant->MajorType() != 5)
      blocked = true;
    occupant->Release();
    occupant = nullptr;
  }
  if (!blocked &&
      terrain->CellWaterVertCount(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift)) ==
          4) {
    GeBounds area{point, point};
    area.GrowXZCell(2);
    area.IntersectXZ(area, extent);
    u32 firstX = u32(area.minimum.x >> kCtMicroCoordinateShift),
        firstZ = u32(area.minimum.z >> kCtMicroCoordinateShift);
    u32 lastX = u32(area.maximum.x >> kCtMicroCoordinateShift), lastZ = u32(area.maximum.z >> kCtMicroCoordinateShift);
    bool dry = false;
    for (u32 x = firstX; x <= lastX && !dry; ++x)
      for (u32 z = firstZ; z <= lastZ && !dry; ++z)
        dry = !terrain->IsWater(x, z);
    blocked = !dry;
  }
  *cost = u32(budget->GetCost(u32(kCtAgentFlora)));
  return !blocked;
}

// Recheck planting permission and price, then increase density before withdrawing the cost from the live budget.
// A refused density change leaves funds untouched; preserve the withdrawal operation's actual Boolean byte.
u8 GeFloraController::Grow(const CtCoord &point) {
  u32 cost = 0;
  if (CanGrow(point, &cost) && IncreaseDensity(point))
    return budget->WithdrawFunds(cost);
  return 0;
}

u8 GeFloraController::InsertTrees(const CtCoord &point, i32) {
  return IncreaseDensity(point);
}

u8 GeFloraController::CreateOccupant(u32 iid, void **out) {
  GeOccupant *occupant = static_cast<GeOccupant *>(new GePlant);
  return occupant->QueryInterface(iid, out);
}

GeFloraTool::GeFloraTool() : flora(nullptr) {
}

GeFloraTool::~GeFloraTool() {
  ScRelease(&flora);
}

bool GeFloraTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return GeCommand::QueryInterface(iid, out);
}

u32 GeFloraTool::AddRef() {
  return GeCommand::AddRef();
}

u32 GeFloraTool::Release() {
  return GeCommand::Release();
}

// Acquire the current city's flora layer, post the tree-view flag request, then attach the real point handler.
// Repeated execution replaces the stored flora publication without releasing its previous hold, as in the original.
bool GeFloraTool::Execute() {
  CtCity *city = GeGetCity();
  if (!city)
    return false;
  flora = ScAddRefNotNull(city->FloraLayer());
  u32 flags = GeGetView()->GetCityViewFlags() | 0x10;
  ScMessage message(kWnMsg_SetCityViewParams, 0, flags, 1);
  GeGetMessageServer()->MessagePost(message, false);
  return AttachHost(GeGetCityWindow());
}

bool GeFloraTool::GetCursor(u32 *out) {
  *out = kSpPlantTreeCursorId;
  return 1;
}

bool GeFloraTool::IsValidHandlerByView(i32 viewKind, u32 flags) const {
  return viewKind == 0 && (flags & 0x10);
}

// Recheck permission and funds before growing at the current point; refusal and successful growth both request feedback.
// The audio manager is borrowed before releasing the freshly queried live application publication.
u8 GeFloraTool::Commit(const CtCoord *position) {
  u32 price = 0;
  if (!flora->CanGrow(*position, &price))
    invalid = 1;
  cost = i32(price);
  CheckCost();
  if (invalid || insufficientFunds || flora->Grow(*position)) {
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, kAuEffectUiLayTrees, 0, 0, 0);
    }
  }
  return 1;
}

// Grow while dragging only when permission and funds checks leave both refusal bytes clear.
// Unlike release-time commitment, this path requests audio feedback regardless of the growth operation's byte reply.
u8 GeFloraTool::PreviewDrag(const CtCoord *position) {
  u32 price = 0;
  if (!flora->CanGrow(*position, &price))
    invalid = 1;
  cost = i32(price);
  CheckCost();
  if (!invalid && !insufficientFunds) {
    flora->Grow(*position);
    void *publication = nullptr;
    if (GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
      ApBoxX *audio = static_cast<ApGameInterface *>(publication)->GetBoxX();
      static_cast<ApGameInterface *>(publication)->Release();
      audio->Event(kAuEventPlayEffect, kAuEffectUiLayTrees, 0, 0, 0);
    }
  }
  return 1;
}

// Refresh hover permission and price without growing flora or requesting audio feedback.
u8 GeFloraTool::PreviewPoint(const CtCoord *position) {
  u32 price = 0;
  if (!flora->CanGrow(*position, &price))
    invalid = 1;
  cost = i32(price);
  CheckCost();
  return 1;
}

// Draw the current point in warning color36 for any preview refusal byte, otherwise35; mouse scrolling suppresses drawing.
// The original leaves the separate affected-bounds output untouched and does not add a cursor-visible test here.
u8 GeFloraTool::PaintCursor(const CtCoord *position, GeBounds *) {
  if (!mouseScroll)
    cursorMap->SetCell(*position, invalid || insufficientFunds || field58 ? 36 : 35);
  return 1;
}

GeFloraFactory::GeFloraFactory() : factoryReferences(0) {
}

GeFloraFactory::~GeFloraFactory() {
}

// Increment the concrete count before retaining the director and return its live value after that callback.
u32 GeFloraFactory::AddRef() {
  ++factoryReferences;
  GeGetDirector()->AddRef();
  return factoryReferences;
}

// Decrement the concrete count before removing the module hold, deleting only when the live count is zero afterward.
u32 GeFloraFactory::Release() {
  --factoryReferences;
  GeGetDirector()->RemoveRef();
  if (!factoryReferences) {
    delete this;
    return 0;
  }
  return factoryReferences;
}

// Construct only the actual tree command class and publish its requested interface without an extra allocation hold.
// A refused interface destroys the complete unretained tool directly; class mismatches clear the output.
bool GeFloraFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  if (classId != kGeFloraToolClassId) {
    *out = nullptr;
    return 0;
  }
  GeFloraTool *tool = new GeFloraTool;
  if (!tool->QueryInterface(iid, out)) {
    delete tool;
    return 0;
  }
  return 1;
}

// Register the actual tree-command factory using the scoped class-factory service before replacing the shared RNG.
bool GeFloraController::StaticInit(CtDefinition *) {
  GeFloraFactory *factory = new GeFloraFactory;
  void *publication = nullptr;
  if (factory->QueryInterface(IID_ScCommandFactory, &publication)) {
    void *manager = nullptr;
    GeGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &manager);
    static_cast<ScCommandFactory *>(manager)->AddCommandFactory(static_cast<ScCommandFactory *>(publication));
    if (manager)
      static_cast<ScCommandFactory *>(manager)->Release();
  }
  factory->Release();
  random = new GeRandom(kScRandomSeedFromClock);
  return true;
}

// Return the actual primary flora-layer identity from a newly constructed controller without adding a reference.
ScUnknown *GeCreateFloraController() {
  return static_cast<GeFlora *>(new GeFloraController);
}

GeCellGrid::GeCellGrid(u32 countX, u32 countZ) : width(countX), height(countZ), columns(new GeCell *[countX]) {
  for (i32 x = i32(width - 1); x >= 0; --x)
    columns[x] = new GeCell[height]{};
}

// Destroy each column from the highest X to the lowest, then the table. C++ supplies the scalar-deleting wrapper.
GeCellGrid::~GeCellGrid() {
  if (columns) {
    for (i32 x = i32(width - 1); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Allocate byte columns from highest X to lowest, then fill each column from highest Z to lowest.
// Read the supplied byte at each assignment, after the corresponding allocation has completed.
GeFlagGrid::GeFlagGrid(u32 countX, u32 countZ, const u8 *fill)
    : width(countX), height(countZ), columns(new u8 *[countX]) {
  for (i32 x = i32(width - 1); x >= 0; --x) {
    columns[x] = new u8[height];
    for (i32 z = i32(height - 1); z >= 0; --z)
      columns[x][z] = *fill;
  }
}

// Destroy each flag column from the highest X to the lowest, then the table. The original null-table branch is
// retained.
GeFlagGrid::~GeFlagGrid() {
  if (columns) {
    for (i32 x = i32(width - 1); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Read every column in descending X order, retaining writes made by the stream even when it reports failure.
// A failed row clears the aggregate result without stopping subsequent reads.
bool GeFlagGrid::Read(RsStream *stream) {
  bool result = true;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->ReadBytes(columns[x], height))
      result = false;
  return result;
}

// Write every column in descending X order. Only the aggregate result is normalized to Boolean.
// Raw nonzero stream bytes count as success, and a failed row does not suppress later writes.
bool GeFlagGrid::Write(RsStream *stream) {
  bool result = true;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->WriteBytes(columns[x], height))
      result = false;
  return result;
}

u32 GeAnimatedOccupantLimit;

// Original CRT initializer0CDA0 supplies the sentinel before any city-manager startup.
static u32 GeAnimationBase = UINT32_MAX;

static usize GeWorkingCount(const GeWorkingSet &set) {
  return set.begin ? usize(set.end - set.begin) : 0;
}

// Append one borrowed occupant to an actual manager working range, preserving its existing geometric growth policy.
static void GeWorkingAppend(GeWorkingSet *set, ScOccupant *occupant) {
  if (set->end == set->storageEnd) {
    usize count = GeWorkingCount(*set), capacity = count ? count * 2 : 1;
    ScOccupant **storage = new ScOccupant *[capacity];
    for (usize i = 0; i < count; ++i)
      storage[i] = set->begin[i];
    storage[count] = occupant;
    delete[] set->begin;
    set->begin = storage;
    set->end = storage + count + 1;
    set->storageEnd = storage + capacity;
  } else {
    *set->end = occupant;
    ++set->end;
  }
}

// Replace an independent borrowed range, reusing capacity when possible and publishing its completed live extent.
static void GeWorkingAssign(GeWorkingSet *target, const GeWorkingSet &source) {
  usize count = GeWorkingCount(source), capacity = target->begin ? usize(target->storageEnd - target->begin) : 0;
  if (count > capacity) {
    ScOccupant **storage = new ScOccupant *[count];
    for (usize i = 0; i < count; ++i)
      storage[i] = source.begin[i];
    delete[] target->begin;
    target->begin = storage;
    target->storageEnd = storage + count;
  } else
    for (usize i = 0; i < count; ++i)
      target->begin[i] = source.begin[i];
  target->end = target->begin ? target->begin + count : nullptr;
}

GeLayer::GeLayer()
    : initialized(0), visible(0), references(0), working{}, lastZoom(-1), lastRotation(-1),
      random(new GeRandom(UINT32_MAX)), window{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}},
      field88(0), field8C(UINT32_MAX), gridLock(new ScLock), animationLock(new ScLock), cells(nullptr),
      cellFlags(nullptr), maximumX(0), maximumZ(0), maximumY(0), fieldAC(0), receiversBegin(nullptr),
      receiversEnd(nullptr), receiversStorageEnd(nullptr) {
}

// Destroy the random receiver and locks before releasing receiver and working-set storage. Grid shutdown and occupant
// reference release belong to the separate original lifecycle calls; the destructor does not perform them.
GeLayer::~GeLayer() {
  ScDelete(&random);
  ScDelete(&gridLock);
  ScDelete(&animationLock);
  if (receiversStorageEnd != receiversBegin)
    delete[] receiversBegin;
  for (i32 i = 4; i >= 0; --i)
    if (working[i].storageEnd != working[i].begin)
      delete[] working[i].begin;
}

// Build both city-sized grids before publishing initialization and acquiring the retained iterator/filter. Configure
// cached animation limits from installed tuning data, apply video preferences, then register the four message types.
bool GeLayer::Init(CtCity *city, u32 sender, u32 inserted, u32 removed, u32 changed, u32 layer) {
  if (initialized)
    return false;
  insertMessage = inserted;
  removeMessage = removed;
  changeMessage = changed;
  maximumX = city->CellCountX() - 1;
  maximumZ = city->CellCountZ() - 1;
  maximumY = city->CellCountY() - 1;
  u32 countZ = city->CellCountZ(), countX = city->CellCountX();
  cells = new GeCellGrid(countX, countZ);
  countZ = city->CellCountZ();
  countX = city->CellCountX();
  u8 empty = 0;
  GeFlagGrid *flags = new GeFlagGrid(countX, countZ, &empty);
  occupantLayer = layer;
  senderId = sender;
  cellFlags = flags;
  initialized = 1;
  GetIterator(&iterator);
  animationTest = ScAddRefNotNull(new GeAnimationTest);
  GeIni reader;
  ScString path, value;
  GeGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Tune.ini", 16);
  reader.SetPath(path);
  GeGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\Sys.pak", 12);
  reader.SetCompressedPath(path);
  if (GeAnimationBase == UINT32_MAX) {
    ScString key("MaxAnimationsBase"), section("BuildingAnimationTunables");
    if (reader.ReadEntry(section, key, &value))
      GeAnimationBase = GeParseNumber(value.ToChar());
    else
      GeAnimationBase = 10;
    const u8 *performance = reinterpret_cast<const u8 *>(GeGetSystemPerformanceInfo());
    GeAnimatedOccupantLimit = GeAnimationBase + static_cast<u32>(3 * static_cast<i8>(performance[4]));
  }
  if (occupantLayer == 1) {
    u32 animations;
    const u8 *video = reinterpret_cast<const u8 *>(GeGetPreferences()->VideoPrefs());
    memcpy(&animations, video + 8, sizeof(animations));
    field88 = animations != 0;
  } else
    field88 = 0;
  if (!field88 || !visible)
    ClearAnimatedOccupantList(true);
  ScMessageServer *messages = GeGetMessageServer();
  messages->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CameraFocusChanged);
  messages->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CameraChanging);
  messages->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_AnimationTickManagerReady);
  messages->AddNotification(static_cast<ScMessageTarget *>(this), kApMsgPreferencesChanged);
  return true;
}

u32 GeLayer::GetCellsInX() {
  return maximumX + 1;
}

u32 GeLayer::GetCellsInZ() {
  return maximumZ + 1;
}

u32 GeLayer::GetCellsInY() {
  return maximumY + 1;
}

u32 GeLayer::GetSenderId() const {
  return senderId;
}

u32 GeLayer::GetOccLayer() const {
  return occupantLayer;
}

u8 GeLayer::SetVisible(u8 value) {
  visible = value;
  return value;
}

bool GeLayer::IsInBoundsCell(u32 x, u32 z) {
  return x <= maximumX && z <= maximumZ;
}

bool GeLayer::IsInBounds(const CtCoord &point) {
  return IsInBoundsCell(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift));
}

// Test all four corners in the original order, stopping on the first rejection. Reversed bounds are not normalized
// here.
bool GeLayer::IsInBounds(const GeBounds &bounds) {
  u32 left = u32(bounds.minimum.x >> kCtMicroCoordinateShift), top = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 right = u32(bounds.maximum.x >> kCtMicroCoordinateShift),
      bottom = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  return IsInBoundsCell(left, top) && IsInBoundsCell(left, bottom) && IsInBoundsCell(right, bottom) &&
      IsInBoundsCell(right, top);
}

// Convert the two micro-coordinate corners to inclusive cell bounds. The private vacancy method handles normalization
// and upper-edge clipping.
bool GeLayer::IsEmpty(const GeBounds &bounds) {
  GeCellRect region = {u32(bounds.minimum.x >> kCtMicroCoordinateShift),
      u32(bounds.minimum.z >> kCtMicroCoordinateShift), u32(bounds.maximum.x >> kCtMicroCoordinateShift),
      u32(bounds.maximum.z >> kCtMicroCoordinateShift)};
  return IsVacantCell(region);
}

// Normalize reversed unsigned corners and clip only their upper edges. Lock even an empty scan and stop at the first
// occupied cell.
bool GeLayer::IsVacantCell(const GeCellRect &bounds) {
  u32 left = bounds.left, right = bounds.right, top = bounds.top, bottom = bounds.bottom;
  if (left > right) {
    u32 swap = left;
    left = right;
    right = swap;
  }
  if (top > bottom) {
    u32 swap = top;
    top = bottom;
    bottom = swap;
  }
  if (right > maximumX)
    right = maximumX;
  if (bottom > maximumZ)
    bottom = maximumZ;
  gridLock->Lock();
  for (u32 x = left; x <= right; ++x)
    for (u32 z = top; z <= bottom; ++z)
      if (cellFlags->columns[x][z] & 1) {
        gridLock->Unlock();
        return false;
      }
  gridLock->Unlock();
  return true;
}

// Read the occupancy bit while holding the original grid lock. The source expects coordinates already in bounds.
bool GeLayer::DoesOccupantExistAtLocation(u32 x, u32 z) {
  gridLock->Lock();
  u8 flags = cellFlags->columns[x][z];
  gridLock->Unlock();
  return (flags & 1) != 0;
}

bool GeLayer::GetOccupant(const CtCoord &point, ScOccupant **out) {
  return GetOccupantAtCell(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift), out);
}

// Resolve a redirect only when an occupant output is requested. Publish the result before unlocking, then retain
// through the published output pointer.
bool GeLayer::GetOccupantAtCell(u32 x, u32 z, ScOccupant **out) {
  if (!IsInBoundsCell(x, z)) {
    *out = nullptr;
    return false;
  }
  gridLock->Lock();
  u8 flags = cellFlags->columns[x][z];
  if (!out) {
    gridLock->Unlock();
    return (flags & 1) != 0;
  }
  if (!(flags & 1)) {
    *out = nullptr;
    gridLock->Unlock();
    return false;
  }
  if (flags & 2) {
    u32 anchor = cells->columns[x][z].anchor;
    *out = cells->columns[u16(anchor)][anchor >> 16].occupant;
  } else
    *out = cells->columns[x][z].occupant;
  if (*out) {
    gridLock->Unlock();
    (*out)->AddRef();
    return true;
  }
  gridLock->Unlock();
  return false;
}

// Publish the sender and replace any existing receiver before appending under the grid lock.
// Acquire ownership after unlocking, then optionally replay occupants through the original virtual boundary.
bool GeLayer::RegisterChangeReceiver(CtChangeReceiver *receiver, u32 *sender, bool replay) {
  if (!receiver)
    return false;
  *sender = senderId;
  UnregisterChangeReceiver(receiver);
  gridLock->Lock();
  if (receiversEnd == receiversStorageEnd) {
    usize count = receiversBegin ? usize(receiversEnd - receiversBegin) : 0;
    usize capacity = count ? count * 2 : 1;
    CtChangeReceiver **storage = new CtChangeReceiver *[capacity];
    for (usize i = 0; i < count; ++i)
      storage[i] = receiversBegin[i];
    storage[count] = receiver;
    delete[] receiversBegin;
    receiversBegin = storage;
    receiversEnd = storage + count + 1;
    receiversStorageEnd = storage + capacity;
  } else {
    *receiversEnd = receiver;
    ++receiversEnd;
  }
  gridLock->Unlock();
  receiver->AddRef();
  if (replay)
    PostOccupantAll(receiver);
  return true;
}

// Remove the last matching receiver and preserve the order of its successors.
// Release its retained reference only after the shortened range has been unlocked.
bool GeLayer::UnregisterChangeReceiver(CtChangeReceiver *receiver) {
  if (!receiver)
    return false;
  gridLock->Lock();
  usize count = receiversBegin ? usize(receiversEnd - receiversBegin) : 0;
  for (usize i = count; i;) {
    --i;
    if (receiversBegin[i] == receiver) {
      for (usize j = i + 1; j < count; ++j)
        receiversBegin[j - 1] = receiversBegin[j];
      --receiversEnd;
      gridLock->Unlock();
      receiver->Release();
      return true;
    }
  }
  gridLock->Unlock();
  return false;
}

// Release receivers in reverse registration order without taking the grid lock.
// Shorten the live range after each release, preserving the state visible to callbacks.
bool GeLayer::UnregisterAllChangeReceivers() {
  usize count = receiversBegin ? usize(receiversEnd - receiversBegin) : 0;
  while (count) {
    receiversBegin[--count]->Release();
    --receiversEnd;
  }
  return true;
}

// Replay each borrowed iterator occupant using the sender word visible at that callback.
// Ignore setup and notification results, then shut down and release the retained iterator in source order.
bool GeLayer::PostOccupantAll(CtChangeReceiver *receiver) {
  OcIterator *current;
  GetIterator(&current);
  current->Init(nullptr, nullptr);
  for (ScOccupant *occupant = current->Reset(); occupant; occupant = current->Next())
    receiver->OccupantInserted(senderId, occupant);
  current->Shutdown();
  current->Release();
  return true;
}

bool GeLayer::GetOccupants(OcIterator **out, const GeBounds *bounds, OcIteratorTest *test) {
  GetIterator(out);
  return (*out)->Init(bounds, test);
}

// Construct an iterator that retains this manager, then publish it before acquiring its initial iterator reference.
bool GeLayer::GetIterator(OcIterator **out) {
  *out = new GeCellIterator(this);
  (*out)->AddRef();
  return true;
}

// Publish the primary manager or message-target identity before retaining the object. Unsupported interface identifiers
// clear the output.
bool GeLayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupantManager);
  HANDLE_QUERY_INTERFACE(IID_ScOccupantManager, ScOccupantManager);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the manager count under its grid lock, then retain the DLL after unlocking.
// Return the live count after the module callback completes.
u32 GeLayer::AddRef() {
  gridLock->Lock();
  ++references;
  gridLock->Unlock();
  GeGetDirector()->AddRef();
  return references;
}

// Decrement a nonzero manager count under the grid lock and unlock before any module call or deletion.
// The original zero-count branch deletes directly, without Shutdown or removing the final module reference.
u32 GeLayer::Release() {
  gridLock->Lock();
  if (references)
    --references;
  if (!references) {
    gridLock->Unlock();
    delete this;
    return 0;
  }
  gridLock->Unlock();
  GeGetDirector()->RemoveRef();
  return references;
}

// Stop animation, detach the four original notifications and release registered change receivers.
// Remove occupants before their grid, then release the iterator and animation filter, then clear initialization last.
bool GeLayer::Shutdown() {
  if (initialized) {
    ClearAnimatedOccupantList(true);
    ScMessageServer *server = GeGetMessageServer();
    server->RemoveNotification(this, kWnMsg_CameraFocusChanged);
    server->RemoveNotification(this, kWnMsg_CameraChanging);
    server->RemoveNotification(this, kWnMsg_AnimationTickManagerReady);
    server->RemoveNotification(this, kApMsgPreferencesChanged);
    UnregisterAllChangeReceivers();
    if (cells) {
      RemoveAllOccupants();
      delete cells;
      cells = nullptr;
    }
    ScDelete(&cellFlags);
    ScRelease(&iterator);
    ScRelease(&animationTest);
    initialized = 0;
  }
  return true;
}

// Snapshot the borrowed animated occupants and empty their live range while holding the animation lock.
// Unlock before optionally notifying the snapshot, preserving additions made by unlock or animation callbacks.
void GeLayer::ClearAnimatedOccupantList(bool notify) {
  animationLock->Lock();
  usize count = working[0].begin ? usize(working[0].end - working[0].begin) : 0;
  ScOccupant **snapshot = count ? new ScOccupant *[count] : nullptr;
  for (usize i = 0; i < count; ++i)
    snapshot[i] = working[0].begin[i];
  working[0].end = working[0].begin;
  animationLock->Unlock();
  if (notify)
    for (usize i = 0; i < count; ++i)
      snapshot[i]->AnimStop();
  delete[] snapshot;
}

// Replay borrowed occupants using one mutable message whose type and final payload survive callback edits.
// Capture the insertion message type after iterator setup; overwrite only the occupant and major-type payloads.
bool GeLayer::PostOccupantAll(ScMessageTarget *target) {
  OcIterator *current;
  GetIterator(&current);
  current->Init(nullptr, nullptr);
  ScMessage message = {insertMessage, 0, 0, 0};
  for (ScOccupant *occupant = current->Reset(); occupant; occupant = current->Next()) {
    message.data1 = reinterpret_cast<uptr>(occupant);
    message.data2 = occupant->MajorType();
    target->DoMessage(&message);
  }
  current->Shutdown();
  current->Release();
  return true;
}

// Read the occupant's actual footprint and forward its minimum cell to the original removal virtual.
// The bounds output must be written by the supplied initialized occupant; this wrapper does not infer location data.
void GeLayer::RemoveOccupant(ScOccupant *occupant) {
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  RemoveOccupantAtCell(bounds.left, bounds.top);
}

void GeLayer::RemoveOccupant(const CtCoord &point) {
  RemoveOccupantAtCell(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift));
}

// Forward the occupant's footprint minimum to cell insertion and return that method's Boolean acceptance result.
bool GeLayer::InsertOccupant(ScOccupant *occupant) {
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  return InsertOccupantAtCell(bounds.left, bounds.top, occupant);
}

// Expand the occupant footprint by one cell where the original checks allow, then initialize a retained iterator.
// Both upper-edge checks use the stored X maximum; preserve that behavior rather than substituting the Z maximum.
bool GeLayer::GetNeighbors(OcIterator **out, ScOccupant *occupant, OcIteratorTest *test) {
  GeCellRect rectangle;
  occupant->GetBoundingRectInCellCoords(&rectangle);
  if (rectangle.left)
    --rectangle.left;
  if (rectangle.top)
    --rectangle.top;
  u32 maximum = maximumX;
  if (rectangle.right < maximum)
    ++rectangle.right;
  if (rectangle.bottom < maximum)
    ++rectangle.bottom;
  GeBounds bounds{{i32(rectangle.left << kCtMicroCoordinateShift), i32(rectangle.top << kCtMicroCoordinateShift), 0},
      {i32(rectangle.right << kCtMicroCoordinateShift), i32(rectangle.bottom << kCtMicroCoordinateShift), 0}};
  GetIterator(out);
  return (*out)->Init(&bounds, test);
}

// Snapshot occupied roots in the clipped cell rectangle, selecting redirected occupants once at their clipped origin.
bool GeLayer::GetOccupantsInCell(
    const GeBounds &bounds, u32 capacity, ScOccupant **out, u32 *count, bool (*test)(ScOccupant *occupant)) {
  u32 left = u32(bounds.minimum.x >> kCtMicroCoordinateShift), top = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 right = u32(bounds.maximum.x >> kCtMicroCoordinateShift),
      bottom = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  *count = 0;
  if (!capacity && out)
    return false;
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
  gridLock->Lock();
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      u8 flags = cellFlags->columns[x][z];
      if (!(flags & 1))
        continue;
      ScOccupant *occupant;
      if (flags & 2) {
        u32 anchor = cells->columns[x][z].anchor;
        occupant = cells->columns[anchor & 0xffff][anchor >> 16].occupant;
        GeCellRect rectangle;
        occupant->GetBoundingRectInCellCoords(&rectangle);
        if (rectangle.left >= left && rectangle.top >= top)
          continue;
        u32 firstX = rectangle.left < left ? left : rectangle.left;
        u32 firstZ = rectangle.top < top ? top : rectangle.top;
        if (x != firstX || z != firstZ)
          continue;
      } else
        occupant = cells->columns[x][z].occupant;
      *candidates.Append() = occupant;
    }
  }
  gridLock->Unlock();
  for (u32 i = 0; i < candidates.size; ++i) {
    ScOccupant *occupant = candidates.data[i];
    if (test && !test(occupant))
      continue;
    if (!out) {
      ++*count;
      continue;
    }
    out[*count] = occupant;
    ++*count;
    occupant->AddRef();
    if (*count >= capacity)
      return true;
  }
  return *count != 0;
}
