#include "economy/ec_sector.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_neighbor_gate.h"
#include "economy/ec_pollution.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_random.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

// Commercial connection tuning begins with this supply before configuration overrides are read.
enum EcCommercialSupplyDefault : u32 {
  kEcCommercialDefaultInitialConnections = 0x1e848,
};

u8 EcPlaceBlock(EcBlockGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 max_distance,
    bool check_transport, u32 minimum, u32 maximum_size, u32 threshold, u32 shore_chance, const GeCellRect &rectangle,
    u32 identifier, i32 demand, i32 maximum) {
  assert(maximum != 0);
  if (i32(100u * u32(demand)) / maximum < i32(threshold))
    return 0;
  EcBlockGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  GeCellRect area = rectangle;
  i32 width = i32(area.right - area.left + 1u), height = i32(area.bottom - area.top + 1u);
  if (width < i32(minimum) || height < i32(minimum))
    return 0;
  if (width > i32(maximum_size)) {
    ScPointXZ left_bottom{area.left, area.bottom}, left_top{area.left, area.top};
    i32 left_roads = i32(zones->GetRoadCount(left_bottom, left_top));
    ScPointXZ right_top{area.right, area.top}, right_bottom{area.right, area.bottom};
    i32 right_roads = i32(zones->GetRoadCount(right_top, right_bottom));
    if (left_roads <= right_roads)
      area.left = area.right - maximum_size + 1u;
    else
      area.right = area.left + maximum_size - 1u;
    width = i32(maximum_size);
  }
  if (height > i32(maximum_size)) {
    ScPointXZ left_top{area.left, area.top}, right_top{area.right, area.top};
    i32 top_roads = i32(zones->GetRoadCount(left_top, right_top));
    ScPointXZ right_bottom{area.right, area.bottom}, left_bottom{area.left, area.bottom};
    i32 bottom_roads = i32(zones->GetRoadCount(right_bottom, left_bottom));
    if (top_roads <= bottom_roads)
      area.top = area.bottom - maximum_size + 1u;
    else
      area.bottom = area.top + maximum_size - 1u;
    height = i32(maximum_size);
  }
  u8 used[100]{};
  for (u32 x = area.left; x <= area.right; ++x) {
    for (u32 z = area.top; z <= area.bottom; ++z) {
      u32 offset_x = x - area.left, offset_z = z - area.top;
      u8 *cell = &used[10u * offset_x + offset_z];
      if (*cell) {
        *cell = 1;
        continue;
      }
      ScPointXZ point{x, z};
      if (check_transport && !zones->IsNearTransport(point, max_distance)) {
        *cell = 1;
        continue;
      }
      GeBuildingType *building;
      if (random->Bounded(100) >= shore_chance)
        building = group->shoreline.data[random->Bounded(group->shoreline.size)];
      else
        building = group->dry.data[random->Bounded(group->dry.size)];
      u32 size = building->SizeInCellsX();
      if (size + offset_x - 1u < u32(width) && size + offset_z - 1u < u32(height) &&
          zones->PlaceBuilding(building, point, 4, 0, true) && size) {
        for (u32 row = 0; row < size; ++row)
          for (u32 column = 0; column < size; ++column)
            used[10u * (offset_x + row) + offset_z + column] = 1;
      }
    }
  }
  return 1;
}

// Allocate the original descending columns and fill complete DWORDs.
EcPackedGrid::EcPackedGrid(u32 width, u32 height, const u32 *fill) : width(width), height(height) {
  columns = new u32 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new u32[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Release original owned columns in descending order, then their pointer table; native pointers retain actual ownership.
EcPackedGrid::~EcPackedGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

EcDwordMap::EcDwordMap()
    : references(0), initialized(false), width(0), height(0), coordinate_shift(1), published(nullptr) {
}

EcDwordMap::~EcDwordMap() {
}

// Retire prior map state through its virtual shutdown, then load city width before height and allocate the coarse map.
bool EcDwordMap::Init(CtCity *city, const u32 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  published = new EcPackedGrid(width >> coordinate_shift, height >> coordinate_shift, fill);
  initialized = true;
  return true;
}

// Delete an active grid and clear its pointer before retiring the initialized flag. Repeated shutdown returns true.
bool EcDwordMap::Shutdown() {
  if (initialized && published) {
    delete published;
    published = nullptr;
  }
  initialized = false;
  return true;
}

u32 EcDwordMap::CellCountX() const {
  return width;
}

u32 EcDwordMap::CellCountZ() const {
  return height;
}

void EcDwordMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = static_cast<i32>(published->columns[x >> coordinate_shift][z >> coordinate_shift]);
}

bool EcDwordMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool EcDwordMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool EcDwordMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void EcDwordMap::GetValue(u32 x, u32 z, u32 *value) const {
  *value = published->columns[x >> coordinate_shift][z >> coordinate_shift];
}

// Forwarded body 1000B181 fills inclusive coarse-coordinate spans with complete DWORD values.
void EcDwordMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u32 *value) {
  x0 >>= coordinate_shift;
  z0 >>= coordinate_shift;
  x1 >>= coordinate_shift;
  z1 >>= coordinate_shift;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      published->columns[x][z] = *value;
}

void EcDwordMap::SetValue(u32 x, u32 z, const u32 *value) {
  published->columns[x >> coordinate_shift][z >> coordinate_shift] = *value;
}

void EcDwordMap::SetAllCells(const u32 *value) {
  for (u32 x = 0; x < published->width; ++x)
    memset(published->columns[x], *value, sizeof(u32) * static_cast<usize>(published->height));
}

// Increment the live local count before retaining the DLL director, then reload the count after that external call.
// A reentrant director callback can change the shared count.
u32 EcDwordMap::AddRef() {
  ++references;
  EcGetDirector()->AddRef();
  return references;
}

// Retire the module reference before decrementing the live count; a zero count invokes the actual virtual destructor.
// An already-zero count also deletes the object without introducing a director-release call.
u32 EcDwordMap::Release() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

// Read descending columns through the original typed unsigned-array interface, retaining failure across all calls.
bool EcPackedGrid::SerialRead(RsStream *record) {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadU32Array(columns[x], height))
      result = false;
  return result;
}

// Write every descending column through its actual typed array operation, reloading live metadata after callbacks.
bool EcPackedGrid::SerialWrite(RsStream *record) const {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteU32Array(columns[x], height))
      result = false;
  return result;
}

bool EcDwordMap::SerialRead(RsStream *record) {
  return published->SerialRead(record);
}

bool EcDwordMap::SerialWrite(RsStream *record) const {
  return published->SerialWrite(record);
}

// Allocate columns in the original descending order, filling complete WORDs rather than repeated bytes.
EcWordGrid::EcWordGrid(u32 width, u32 height, const u16 *fill) : width(width), height(height) {
  columns = new u16 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new u16[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Release owned columns in descending order, then the column table. No borrowed game interfaces are released here.
EcWordGrid::~EcWordGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

EcWordMap::EcWordMap() : references(0), initialized(false), grid(nullptr) {
}

EcWordMap::~EcWordMap() {
  if (initialized)
    Shutdown();
}

// Accept the three original interface IDs, clearing the output on rejection.
bool EcWordMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcWordMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapI32, EcWordMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, EcWordMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before acquiring the original module director reference, then reload the live count.
// The folded body is shared by several concrete map specializations.
u32 EcWordMap::AddRef() {
  ++references;
  EcGetDirector()->AddRef();
  return references;
}

// Retire the module reference before decrementing the live local count. A zero count deletes the original object.
// The folded body is shared by several concrete map specializations.
u32 EcWordMap::Release() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

u32 EcWordMap::CellCountX() const {
  return grid->width;
}

u32 EcWordMap::CellCountZ() const {
  return grid->height;
}

void EcWordMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x][z];
}

bool EcWordMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool EcWordMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool EcWordMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool EcWordMap::Init(CtCity *city, const u16 *fill) {
  if (initialized)
    Shutdown();
  // Braced arguments sequence height before width; new allocates the grid object before either getter.
  u32 height;
  grid = new EcWordGrid{(height = city->CellCountZ(), city->CellCountX()), height, fill};
  initialized = true;
  return true;
}

// Retire an initialized allocation and clear the active flag. Repeated shutdown preserves its successful result.
bool EcWordMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

void EcWordMap::GetValue(u32 x, u32 z, u16 *value) const {
  *value = grid->columns[x][z];
}

void EcWordMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u16 *value) {
  for (u32 x = x0; x <= x1; ++x)
    memset(grid->columns[x] + z0, *value, 2 * (static_cast<usize>(z1) - z0 + 1));
}

void EcWordMap::SetValue(u32 x, u32 z, const u16 *value) {
  grid->columns[x][z] = *value;
}

void EcWordMap::SetAllCells(const u16 *value) {
  for (u32 x = 0; x < grid->width; ++x)
    memset(grid->columns[x], *value, 2 * static_cast<usize>(grid->height));
}

// Read descending columns through the original typed unsigned-array interface, retaining failure across all calls.
bool EcWordGrid::SerialRead(RsStream *record) {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadU16Array(columns[x], height))
      result = false;
  return result;
}

// Write every descending column through its actual typed array operation, reloading live metadata after callbacks.
bool EcWordGrid::SerialWrite(RsStream *record) const {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteU16Array(columns[x], height))
      result = false;
  return result;
}

bool EcWordMap::SerialRead(RsStream *record) {
  return grid->SerialRead(record);
}

bool EcWordMap::SerialWrite(RsStream *record) const {
  return grid->SerialWrite(record);
}

// Allocate descending signed-WORD columns and initialize full scalar values.
EcShortGrid::EcShortGrid(u32 width, u32 height, const i16 *fill) : width(width), height(height) {
  columns = new i16 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new i16[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Release columns in descending order before releasing their pointer table, matching shared body 1000BA6F.
EcShortGrid::~EcShortGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

EcShortMap::EcShortMap() : references(0), initialized(false), width(0), height(0), coordinate_shift(1), grid(nullptr) {
}

EcShortMap::~EcShortMap() {
}

// Accept the original unknown, cell-map-base and signed-WORD map IDs; clear rejected outputs.
bool EcShortMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcShortMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, EcShortMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapI16, EcShortMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Acquire the director reference after incrementing the local count, then reload the live count (1000AFAE).
u32 EcShortMap::AddRef() {
  ++references;
  EcGetDirector()->AddRef();
  return references;
}

// Retire the director reference before the local count; zero deletes the original map (shared 1000B558).
u32 EcShortMap::Release() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

// Load city width before height, then allocate the shifted signed-WORD grid after retiring previous state.
bool EcShortMap::Init(CtCity *city, const i16 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  grid = new EcShortGrid(width >> coordinate_shift, height >> coordinate_shift, fill);
  initialized = true;
  return true;
}

// Shared body 1000B042 deletes an active grid before clearing initialized; repeated shutdown returns true.
bool EcShortMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

u32 EcShortMap::CellCountX() const {
  return width;
}

u32 EcShortMap::CellCountZ() const {
  return height;
}

void EcShortMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

bool EcShortMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool EcShortMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool EcShortMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void EcShortMap::GetValue(u32 x, u32 z, i16 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

// Fill inclusive shifted spans with full WORD stores, retaining the original X-major order.
void EcShortMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value) {
  x0 >>= coordinate_shift;
  z0 >>= coordinate_shift;
  x1 >>= coordinate_shift;
  z1 >>= coordinate_shift;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid->columns[x][z] = *value;
}

void EcShortMap::SetValue(u32 x, u32 z, const i16 *value) {
  grid->columns[x >> coordinate_shift][z >> coordinate_shift] = *value;
}

void EcShortMap::SetAllCells(const i16 *value) {
  for (u32 x = 0; x < grid->width; ++x)
    memset(grid->columns[x], *value, sizeof(i16) * static_cast<usize>(grid->height));
}

// Read descending columns through the original raw block transport, retaining failure while visiting every column.
bool EcShortGrid::SerialRead(RsStream *record) {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadBlock(columns[x], 2 * height))
      result = false;
  return result;
}

// Write every descending column even after a failed block, reloading the live height before each callback.
bool EcShortGrid::SerialWrite(RsStream *record) const {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteBlock(columns[x], 2 * height))
      result = false;
  return result;
}

bool EcShortMap::SerialRead(RsStream *record) {
  return grid->SerialRead(record);
}

bool EcShortMap::SerialWrite(RsStream *record) const {
  return grid->SerialWrite(record);
}

// Allocate descending signed-DWORD columns and initialize full scalar values.
EcIntGrid::EcIntGrid(u32 width, u32 height, const i32 *fill) : width(width), height(height) {
  columns = new i32 *[width];
  for (u32 x = width; x-- > 0;) {
    columns[x] = new i32[height];
    for (u32 z = height; z-- > 0;)
      columns[x][z] = *fill;
  }
}

// Release columns in descending order before releasing their pointer table, matching shared body 1000BB4B.
EcIntGrid::~EcIntGrid() {
  if (columns) {
    for (u32 x = width; x-- > 0;)
      delete[] columns[x];
    delete[] columns;
  }
}

EcIntMap::EcIntMap() : references(0), initialized(false), width(0), height(0), coordinate_shift(1), grid(nullptr) {
}

EcIntMap::~EcIntMap() {
}

// Accept the original unknown, cell-map-base and signed-DWORD map IDs; clear rejected outputs.
bool EcIntMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcIntMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, EcIntMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapI32, EcIntMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Acquire the director reference after incrementing the local count, then reload the live count (1000AFAE).
u32 EcIntMap::AddRef() {
  ++references;
  EcGetDirector()->AddRef();
  return references;
}

// Retire the director reference before the local count; zero deletes the original map (shared 1000B558).
u32 EcIntMap::Release() {
  if (references) {
    EcGetDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

// Load city width before height, then allocate the shifted signed-DWORD grid after retiring previous state.
bool EcIntMap::Init(CtCity *city, const i32 *fill) {
  if (initialized)
    Shutdown();
  width = city->CellCountX();
  height = city->CellCountZ();
  grid = new EcIntGrid(width >> coordinate_shift, height >> coordinate_shift, fill);
  initialized = true;
  return true;
}

// Shared body 1000B042 deletes an active grid before clearing initialized; repeated shutdown returns true.
bool EcIntMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

u32 EcIntMap::CellCountX() const {
  return width;
}

u32 EcIntMap::CellCountZ() const {
  return height;
}

void EcIntMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

bool EcIntMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool EcIntMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool EcIntMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

void EcIntMap::GetValue(u32 x, u32 z, i32 *value) const {
  *value = grid->columns[x >> coordinate_shift][z >> coordinate_shift];
}

// Shared body 1000B181 also serves the published DWORD-map rectangle method.
// Fill inclusive shifted spans with full DWORD stores, retaining the original X-major order.
void EcIntMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i32 *value) {
  x0 >>= coordinate_shift;
  z0 >>= coordinate_shift;
  x1 >>= coordinate_shift;
  z1 >>= coordinate_shift;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid->columns[x][z] = *value;
}

void EcIntMap::SetValue(u32 x, u32 z, const i32 *value) {
  grid->columns[x >> coordinate_shift][z >> coordinate_shift] = *value;
}

void EcIntMap::SetAllCells(const i32 *value) {
  for (u32 x = 0; x < grid->width; ++x)
    memset(grid->columns[x], *value, sizeof(i32) * static_cast<usize>(grid->height));
}

// Read descending columns through the original raw block transport, retaining failure while visiting every column.
bool EcIntGrid::SerialRead(RsStream *record) {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->ReadBlock(columns[x], 4 * height))
      result = false;
  return result;
}

// Write every descending column even after a failed block, reloading the live height before each callback.
bool EcIntGrid::SerialWrite(RsStream *record) const {
  bool result = true;
  for (u32 x = width; x-- > 0;)
    if (!record->WriteBlock(columns[x], 4 * height))
      result = false;
  return result;
}

bool EcIntMap::SerialRead(RsStream *record) {
  return grid->SerialRead(record);
}

bool EcIntMap::SerialWrite(RsStream *record) const {
  return grid->SerialWrite(record);
}

EcDemandManipulator::EcDemandManipulator() : references(0), layer(nullptr) {
}

EcDemandManipulator::~EcDemandManipulator() {
}

// Publish the same primary identity for Unknown or the actual
// city-layer manipulator IID before retaining it. An unsupported query clears the output.
bool EcDemandManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this receiver before retaining SIMRCI, then return the count after the synchronous director callback.
u32 EcDemandManipulator::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Release the module hold before decrementing a nonzero count. Zero deletes through the actual virtual destructor.
u32 EcDemandManipulator::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 count = references;
  if (!count)
    delete this;
  return count;
}

bool EcDemandManipulator::Apply(CtAgent *) {
  layer->EndOfMonth();
  return true;
}

bool EcDemandManipulator::IsLockingCity() {
  return false;
}

bool EcDemandManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcDemandManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcDemandManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

u8 EcDemandManipulator::Init(EcValveLayer *owner) {
  if (!owner)
    return 0;
  layer = owner;
  owner->AddRef();
  return 1;
}

u8 EcDemandManipulator::Shutdown() {
  ScRelease(&layer);
  return 1;
}

// Actual ordered valve key/value entries keep stable identities across synchronous game callbacks.
struct EcDemandValveEntry {
  EcDemandValveEntry *next;
  u32 identifier;
  EcValve *valve;
};

struct EcDemandAgentEntry {
  EcDemandAgentEntry *next;
  u32 identifier;
  EcDemandAgentManipulator *manipulator;
};

// Original tuning rows contain a full identifier and two signed16-bit supply/demand coefficients.
struct EcDemandEffect {
  EcDemandEffect *next;
  u32 identifier;
  i16 supply, demand;
};

struct EcDemandEffectGroup {
  EcDemandEffectGroup *next;
  u32 identifier;
  EcDemandEffect *first;
};

struct EcDemandOrdinanceEffect {
  EcDemandOrdinanceEffect *next;
  u32 identifier;
  f32 demand;
};

struct EcDemandOrdinanceGroup {
  EcDemandOrdinanceGroup *next;
  u32 identifier;
  EcDemandOrdinanceEffect *first;
};

// Original global containers own their rows independently, including retirement at module exit.
struct EcDemandEffectTable {
  EcDemandEffectGroup *first = nullptr;
  ~EcDemandEffectTable();
};

struct EcDemandOrdinanceTable {
  EcDemandOrdinanceGroup *first = nullptr;
  ~EcDemandOrdinanceTable();
};

// Original58720/58710/58700 own independent agent, connection and ordinance tuning records.
static EcDemandEffectTable EcDemandAgentEffects, EcDemandConnectionEffects;
static EcDemandOrdinanceTable EcDemandOrdinanceEffects;

// Find a full unsigned tuning key without inserting a group or changing its ordered effect rows.
static EcDemandEffectGroup *EcFindDemandEffects(EcDemandEffectGroup *group, u32 identifier) {
  while (group && group->identifier < identifier)
    group = group->next;
  return group && group->identifier == identifier ? group : nullptr;
}

// Preserve the installed biased-double rounding and its sign-dependent DWORD combination for ordinance demand.
// This is the native arithmetic phase inside EndOfMonth, not a replacement game callback or a general math contract.
static i32 EcDemandOrdinanceDelta(i32 demand, f32 scale) {
  f64 product = f64(demand) * scale, rounded = product + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  u32 low = u32(bits);
  return i32(demand <= 0 ? kScDoubleIntegerLowWordBias - low : low + kScDoubleIntegerLowWordBias);
}

EcDemand::EcDemand()
    : references(0), city(nullptr), neighbors(nullptr), ordinances(nullptr), valves(nullptr), monthly(nullptr),
      agents(nullptr), agent_tree(nullptr), density(nullptr), current_density(nullptr), taxable{}, next_taxable{} {
}

// Run the actual city-layer cleanup before retiring remaining trivial container records in reverse member order.
// Real service/valve retirement remains the explicit Shutdown dependency, with no fabricated successful cleanup body.
EcDemand::~EcDemand() {
  EcDemand::Shutdown();
  while (agents) {
    EcDemandAgentEntry *entry = agents;
    agents = entry->next;
    delete entry;
  }
  while (valves) {
    EcDemandValveEntry *entry = valves;
    valves = entry->next;
    delete entry;
  }
}

// Publish the primary valve-layer or actual separate city-layer view, then retain through the primary receiver.
// Unsupported interfaces leave the caller's output alone.
bool EcDemand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcValveLayer);
  HANDLE_QUERY_INTERFACE(IID_EcValveLayer, EcValveLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the shared receiver count before pinning SIMRCI, then reload its value after that callback.
u32 EcDemand::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a positive count. A live zero count deletes both interface views together.
u32 EcDemand::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 count = references;
  if (!count)
    delete this;
  return count;
}

bool EcDemand::DoMessage(ScMessage *) {
  return false;
}

// Retain city,neighbors and ordinances in order, preserving partial initialization on a missing required layer.
// Bind the retained monthly manipulator and agent tree before constructing the previous and current density maps.
bool EcDemand::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  neighbors = city->NeighborsLayer();
  if (!neighbors)
    return false;
  neighbors->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return false;
  ordinances->AddRef();
  monthly = ScAddRefNotNull(new EcDemandManipulator);
  monthly->Init(static_cast<EcValveLayer *>(this));
  agent_tree = ScAddRefNotNull(EcGetRciAgentTree());
  density = ScAddRefNotNull(new EcByteMap);
  u8 zero = 0;
  if (!density->Init(city, &zero))
    return false;
  current_density = ScAddRefNotNull(new EcByteMap);
  u8 current_zero = 0;
  return current_density->Init(city, &current_zero);
}

bool EcDemand::Init(CtCity *, ScImporter *) {
  return true;
}

// Find or insert the original null valve value at a full unsigned key, retaining stable entries across callbacks.
// This typed game collection replaces only the library map operation used inside the recovered methods.
EcValve **EcDemand::ValveSlot(u32 identifier) {
  EcDemandValveEntry **position = &valves;
  while (*position && (*position)->identifier < identifier)
    position = &(*position)->next;
  if (!*position || (*position)->identifier != identifier)
    *position = new EcDemandValveEntry{*position, identifier, nullptr};
  return &(*position)->valve;
}

// Acquire the real valve class, initialize it and query the requested identity before publishing map ownership.
// Failed initialization releases directly; failed acquisition/query shuts down any published valve before release.
bool EcDemand::CreateNewValve(u32 identifier, u32 iid, void **out) {
  void *object = nullptr;
  bool acquired = EcGetRciCom()->GetClassObject(kEcValveClassId, IID_EcValve, &object);
  EcValve *valve = static_cast<EcValve *>(object);
  if (acquired) {
    if (!valve->Init(city, identifier, static_cast<CtLayer *>(this))) {
      valve->Release();
      return false;
    }
    if (valve->QueryInterface(iid, out)) {
      *ValveSlot(identifier) = valve;
      return true;
    }
  }
  if (valve) {
    valve->Shutdown();
    valve->Release();
  }
  return false;
}

// Replace an existing valve only after its live shutdown/release callbacks, then retain the supplied replacement.
// A null input succeeds without querying its identifier or touching the collection.
bool EcDemand::AddValveToLayer(EcValve *valve) {
  if (valve) {
    u32 identifier = valve->GetId();
    EcDemandValveEntry *entry = valves;
    while (entry && entry->identifier < identifier)
      entry = entry->next;
    if (entry && entry->identifier == identifier) {
      entry->valve->Shutdown();
      entry->valve->Release();
      entry->valve = valve;
    } else
      *ValveSlot(identifier) = valve;
    valve->AddRef();
  }
  return true;
}

// Query the existing valve at the full DWORD key. Missing keys decline without inserting or changing the output.
bool EcDemand::GetValvePointer(u32 identifier, u32 iid, void **out) {
  EcDemandValveEntry *entry = valves;
  while (entry && entry->identifier < identifier)
    entry = entry->next;
  return entry && entry->identifier == identifier ? entry->valve->QueryInterface(iid, out) : false;
}

// Begin each live valve in ascending identifier order, ignoring individual replies before reporting success.
bool EcDemand::SimulationBegin() {
  for (EcDemandValveEntry *entry = valves; entry; entry = entry->next)
    entry->valve->SimulationBegin();
  return true;
}

bool EcDemand::SimulationEnd() {
  return true;
}

u32 EcDemand::GetLayerType() {
  return kEcDemandLayerType;
}

bool EcDemand::DebugClassTag(ScStringView *) {
  return false;
}

bool EcDemand::DebugTypeTag(ScStringView *) {
  return false;
}

// Return the first signed supply coefficient matching the full agent and valve identifiers, or zero when absent.
i16 EcDemand::GetAgentSupplyEffect(u32 valve, u32 agent) {
  EcDemandEffectGroup *group = EcFindDemandEffects(EcDemandAgentEffects.first, agent);
  if (group)
    for (EcDemandEffect *effect = group->first; effect; effect = effect->next)
      if (effect->identifier == valve)
        return effect->supply;
  return 0;
}

// Return the matching signed demand coefficient without normalizing its raw16-bit value.
i16 EcDemand::GetAgentDemandEffect(u32 valve, u32 agent) {
  EcDemandEffectGroup *group = EcFindDemandEffects(EcDemandAgentEffects.first, agent);
  if (group)
    for (EcDemandEffect *effect = group->first; effect; effect = effect->next)
      if (effect->identifier == valve)
        return effect->demand;
  return 0;
}

void EcDemand::GetDensity(u32 x, u32 z, u8 *out) {
  density->GetValue(x, z, out);
}

u32 EcDemand::GetTaxableResidentialDensity() {
  return taxable[0];
}

u32 EcDemand::GetTaxableCommercialDensity() {
  return taxable[1];
}

u32 EcDemand::GetTaxableIndustrialDensity() {
  return taxable[2];
}

void EcDemand::DebugSetValve(ScStringView *, ScStringView *, ScStringView *) {
}

// Apply ordered connection and ordinance effects before ticking every valve, retaining any refused monthly reply.
// Publish current density, clear its working plane and advance all three taxable counters afterward.
bool EcDemand::EndOfMonth() {
  bool result = true;
  for (u32 kind = 0; kind < 4; ++kind) {
    u32 count;
    switch (kind) {
    case 0:
      count = neighbors->GetTotalRoadConnections();
      break;
    case 1:
      count = neighbors->GetTotalRailConnections();
      break;
    case 2:
      count = neighbors->GetTotalHighwayConnections();
      break;
    default:
      count = neighbors->GetTotalSubwayConnections();
      break;
    }
    if (count) {
      EcDemandEffectGroup *group = EcFindDemandEffects(EcDemandConnectionEffects.first, kind);
      if (group) {
        for (EcDemandEffect *effect = group->first; effect; effect = effect->next) {
          EcValve *valve = *ValveSlot(effect->identifier);
          if (valve) {
            valve->AddToSupplyValue(i32(count * u32(i32(effect->supply))));
            valve->AddToDemandValue(i32(count * u32(i32(effect->demand))));
          }
        }
      }
    }
  }
  for (EcDemandOrdinanceGroup *group = EcDemandOrdinanceEffects.first; group; group = group->next) {
    if (ordinances->QueryOrdinanceOn(group->identifier)) {
      for (EcDemandOrdinanceEffect *effect = group->first; effect; effect = effect->next) {
        EcValve *valve = *ValveSlot(effect->identifier);
        if (valve) {
          i32 demand = valve->QueryDemandValue();
          valve->AddToDemandValue(EcDemandOrdinanceDelta(demand, effect->demand));
        }
      }
    }
  }
  for (EcDemandValveEntry *entry = valves; entry; entry = entry->next)
    if (!entry->valve->EndOfMonth())
      result = false;
  for (u32 x = 0; x < density->CellCountX(); ++x) {
    for (u32 z = 0; z < density->CellCountZ(); ++z) {
      u8 value;
      current_density->GetValue(x, z, &value);
      density->SetValue(x, z, &value);
    }
  }
  u8 zero = 0;
  current_density->SetAllCells(&zero);
  taxable[0] = next_taxable[0];
  taxable[1] = next_taxable[1];
  taxable[2] = next_taxable[2];
  next_taxable[0] = 0;
  next_taxable[1] = 0;
  next_taxable[2] = 0;
  return result;
}

// Construct the real demand receiver and return its unretained primary valve identity to the module class registry.
ScUnknown *EcCreateDemand() {
  return static_cast<EcValveLayer *>(new EcDemand);
}

// Each original binding owns the queried valve view and copies a full identifier plus two signed16-bit coefficients.
struct EcDemandBinding {
  EcDemandBinding *next;
  EcValve *valve;
  u32 identifier;
  i16 supply, demand;
};

EcDemandAgentManipulator::EcDemandAgentManipulator() : references(0), layer(nullptr), identifier(0), bindings(nullptr) {
}

EcDemandAgentManipulator::~EcDemandAgentManipulator() {
  ClearBindings();
}

// Remove copied effect records without releasing their stored interfaces, matching original library-list erasure.
// Initialization and type replacement use this phase separately from explicit Shutdown's interface retirement.
void EcDemandAgentManipulator::ClearBindings() {
  while (bindings) {
    EcDemandBinding *entry = bindings;
    bindings = entry->next;
    delete entry;
  }
}

// Publish the same primary view for the actual manipulator IID or Unknown, then acquire its virtual reference.
bool EcDemandAgentManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain this receiver before the module and reload its count after the director callback.
u32 EcDemandAgentManipulator::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Release a positive module hold before decrementing the local count, deleting through this class's destructor at zero.
u32 EcDemandAgentManipulator::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 count = references;
  if (!count)
    delete this;
  return count;
}

bool EcDemandAgentManipulator::IsLockingCity() {
  return false;
}

bool EcDemandAgentManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcDemandAgentManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcDemandAgentManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Retain and publish the concrete layer before erasing old copied bindings.
u8 EcDemandAgentManipulator::Init(EcDemand *owner) {
  layer = ScAddRefNotNull(owner);
  ClearBindings();
  return 1;
}

u8 EcDemandAgentManipulator::Shutdown() {
  ScRelease(&layer);
  for (EcDemandBinding *entry = bindings; entry; entry = entry->next) {
    if (entry->valve) {
      entry->valve->Release();
      entry->valve = nullptr;
    }
  }
  ClearBindings();
  return 1;
}

// Equal identifiers skip all work; copied bindings acquire exactly the reference supplied by successful QI.
u8 EcDemandAgentManipulator::SetAgentType(u32 value) {
  if (identifier != value) {
    identifier = value;
    ClearBindings();
    EcDemandEffectGroup *group = EcFindDemandEffects(EcDemandAgentEffects.first, identifier);
    if (group) {
      for (EcDemandEffect *effect = group->first; effect; effect = effect->next) {
        void *object = nullptr;
        if (layer->GetValvePointer(effect->identifier, IID_EcValve, &object)) {
          EcDemandBinding **position = &bindings;
          while (*position)
            position = &(*position)->next;
          *position = new EcDemandBinding{
              nullptr, static_cast<EcValve *>(object), effect->identifier, effect->supply, effect->demand};
        }
      }
    }
  }
  return 1;
}

static u8 EcDemandDensityDelta(u8 value, i16 delta) {
  i32 sum = i32(value) + delta;
  return u8(sum < 0 ? 0 : sum > 255 ? 255 : sum);
}

// Apply configured supply/demand per agent, preserving the byte-wrapped footprint multiplier and occupancy refusal.
// Occupants also update saturated working density and ancestor-filtered taxable totals through live layer fields.
bool EcDemandAgentManipulator::Apply(CtAgent *agent) {
  void *occupant_object = nullptr, *building_object = nullptr;
  if (agent->QueryInterface(IID_ScOccupant, &occupant_object)) {
    u8 rows = static_cast<ScOccupant *>(occupant_object)->SizeInCellsZ();
    u8 multiplier = u8(static_cast<ScOccupant *>(occupant_object)->SizeInCellsX() * rows);
    GeCellRect bounds;
    static_cast<ScOccupant *>(occupant_object)->GetBoundingRectInCellCoords(&bounds);
    u32 occupant_type = static_cast<ScOccupant *>(occupant_object)->TypeId();
    static_cast<ScOccupant *>(occupant_object)->Release();
    occupant_object = nullptr;
    if (agent->QueryInterface(IID_BdBuilding, &building_object)) {
      u32 state = static_cast<BdBuilding *>(building_object)->GetOccupancyState();
      static_cast<BdBuilding *>(building_object)->Release();
      if (state != 1)
        return true;
    }
    u32 factor = multiplier;
    for (EcDemandBinding *entry = bindings; entry; entry = entry->next) {
      EcValve *valve = entry->valve;
      i16 supply = entry->supply, demand = entry->demand;
      valve->AddToSupplyValue(i32(supply) * i32(factor));
      valve->AddToDemandValue(i32(demand) * i32(factor));
      u32 kind = valve->GetId();
      if (kind == kEcResidentialValveId || kind == kEcCommercialValveId || kind == kEcIndustrialValveId) {
        for (u32 x = bounds.left; x <= bounds.right; ++x) {
          for (u32 z = bounds.top; z <= bounds.bottom; ++z) {
            u8 value;
            layer->current_density->GetValue(x, z, &value);
            value = EcDemandDensityDelta(value, supply);
            layer->current_density->SetValue(x, z, &value);
          }
        }
        if (layer->agent_tree->IsAncestor(kind, occupant_type)) {
          u32 bucket = kind == kEcResidentialValveId ? 0 : kind == kEcCommercialValveId ? 1 : 2;
          layer->next_taxable[bucket] +=
              u32(i32(supply)) * (bounds.right - bounds.left + 1) * (bounds.bottom - bounds.top + 1);
        }
      } else if (kind == kEcWorkforceValveId) {
        for (u32 x = bounds.left; x <= bounds.right; ++x) {
          for (u32 z = bounds.top; z <= bounds.bottom; ++z) {
            u8 value;
            layer->current_density->GetValue(x, z, &value);
            value = EcDemandDensityDelta(value, demand);
            layer->current_density->SetValue(x, z, &value);
          }
        }
      }
    }
    return true;
  }
  if (agent->QueryInterface(IID_BdBuilding, &building_object)) {
    u32 state = static_cast<BdBuilding *>(building_object)->GetOccupancyState();
    static_cast<BdBuilding *>(building_object)->Release();
    if (state != 1)
      return true;
  }
  for (EcDemandBinding *entry = bindings; entry; entry = entry->next) {
    EcValve *valve = entry->valve;
    i16 supply = entry->supply, demand = entry->demand;
    valve->AddToSupplyValue(supply);
    valve->AddToDemandValue(demand);
    valve->GetId();
  }
  return true;
}

// Find or create a stable agent-cache value at the original full unsigned identifier.
// The typed collection retains the original ascending traversal without recreating general library-tree machinery.
EcDemandAgentManipulator **EcDemand::AgentSlot(u32 identifier) {
  EcDemandAgentEntry **position = &agents;
  while (*position && (*position)->identifier < identifier)
    position = &(*position)->next;
  if (!*position || (*position)->identifier != identifier)
    *position = new EcDemandAgentEntry{*position, identifier, nullptr};
  return &(*position)->manipulator;
}

// Return the cached borrowed manipulator or monthly callback after querying the actual type ID, then publishing flags4.
bool EcDemand::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  u32 identifier = type.Id();
  if (!out)
    return false;
  *flags = 4;
  *out = nullptr;
  EcDemandAgentEntry *entry = agents;
  while (entry && entry->identifier < identifier)
    entry = entry->next;
  if (entry && entry->identifier == identifier) {
    *out = entry->manipulator;
    return true;
  }
  if (identifier == kCtAgentType_AreaAgent) {
    *out = monthly;
    return true;
  }
  if (EcFindDemandEffects(EcDemandAgentEffects.first, identifier)) {
    EcDemandAgentManipulator *manipulator = new EcDemandAgentManipulator;
    manipulator->AddRef();
    if (manipulator->Init(this) && manipulator->SetAgentType(identifier)) {
      *AgentSlot(identifier) = manipulator;
      *out = manipulator;
      return true;
    }
    manipulator->Release();
  }
  return true;
}

// Retire monthly,city,neighbors,ordinances and agent-tree holds before releasing ordered valves and agent callbacks.
// Remove exact cached entries after their callbacks, preserving newly inserted records; clear density and totals last.
bool EcDemand::Shutdown() {
  if (monthly) {
    monthly->Shutdown();
    monthly->Release();
    monthly = nullptr;
  }
  ScRelease(&city);
  ScRelease(&neighbors);
  ScRelease(&ordinances);
  ScRelease(&agent_tree);
  while (valves) {
    EcDemandValveEntry *entry = valves;
    if (entry->valve) {
      entry->valve->Shutdown();
      entry->valve->Release();
    }
    EcDemandValveEntry **position = &valves;
    while (*position != entry)
      position = &(*position)->next;
    *position = entry->next;
    delete entry;
  }
  while (agents) {
    EcDemandAgentEntry *entry = agents;
    entry->manipulator->Shutdown();
    entry->manipulator->Release();
    EcDemandAgentEntry **position = &agents;
    while (*position != entry)
      position = &(*position)->next;
    *position = entry->next;
    delete entry;
  }
  if (density) {
    density->Shutdown();
    density->Release();
    density = nullptr;
  }
  if (current_density) {
    current_density->Shutdown();
    current_density->Release();
    current_density = nullptr;
  }
  taxable[0] = 0;
  taxable[1] = 0;
  taxable[2] = 0;
  next_taxable[0] = 0;
  next_taxable[1] = 0;
  next_taxable[2] = 0;
  return true;
}

// Retire plain supply/demand tuning rows and their owning groups, without introducing game-object releases.
// This native storage phase replaces the original map/list destruction rather than a separate game method.
static void EcClearDemandEffects(EcDemandEffectGroup **groups) {
  while (*groups) {
    EcDemandEffectGroup *group = *groups;
    while (group->first) {
      EcDemandEffect *effect = group->first;
      group->first = effect->next;
      delete effect;
    }
    *groups = group->next;
    delete group;
  }
}

// Retire only the actual ordinance coefficient rows and groups, preserving their independent static ownership.
static void EcClearDemandOrdinances(EcDemandOrdinanceGroup **groups) {
  while (*groups) {
    EcDemandOrdinanceGroup *group = *groups;
    while (group->first) {
      EcDemandOrdinanceEffect *effect = group->first;
      group->first = effect->next;
      delete effect;
    }
    *groups = group->next;
    delete group;
  }
}

EcDemandEffectTable::~EcDemandEffectTable() {
  EcClearDemandEffects(&first);
}

EcDemandOrdinanceTable::~EcDemandOrdinanceTable() {
  EcClearDemandOrdinances(&first);
}

// Clear agent,connection and ordinance tuning in the original table order, including every owned effect row.
// No live valve/manipulator bindings are shut down by this static-table operation.
bool EcDemand::StaticShutdown() {
  EcClearDemandEffects(&EcDemandAgentEffects.first);
  EcClearDemandEffects(&EcDemandConnectionEffects.first);
  EcClearDemandOrdinances(&EcDemandOrdinanceEffects.first);
  return true;
}

// Parse one configured agent or connection row, replacing the existing ordered effects for that identifier.
static void EcReadDemandEffects(const ScString &key, const ScString &value, void *context) {
  if (!key.Strlen() || !value.Strlen())
    return;
  EcDemandEffectTable *table = static_cast<EcDemandEffectTable *>(context);
  EcDemandEffectGroup **slot = &table->first;
  u32 identifier = EcParseTuningNumber(key.Data());
  while (*slot && (*slot)->identifier < identifier)
    slot = &(*slot)->next;
  if (!*slot || (*slot)->identifier != identifier)
    *slot = new EcDemandEffectGroup{*slot, identifier, nullptr};
  EcDemandEffectGroup *group = *slot;
  while (group->first) {
    EcDemandEffect *effect = group->first;
    group->first = effect->next;
    delete effect;
  }
  EcDemandEffect **tail = &group->first;
  ScString remaining(value);
  bool more;
  do {
    ScString before(remaining), row = remaining.SplitToken("+");
    more = !remaining.IsEqual(&before);
    if (!more)
      row = remaining;
    ScString valve = row.SplitToken(":"), supply = row.SplitToken(":");
    *tail = new EcDemandEffect{nullptr, EcParseTuningNumber(valve.Data()), i16(EcParseTuningNumber(supply.Data())),
        i16(EcParseTuningNumber(row.Data()))};
    tail = &(*tail)->next;
  } while (more);
}

// Parse one ordinance row into the ordered per-valve fractional demand effects.
// The original callback's cleanup return is unused by the INI enumeration.
static void EcReadDemandOrdinances(const ScString &key, const ScString &value, void *) {
  if (!key.Strlen() || !value.Strlen())
    return;
  u32 identifier = EcParseTuningNumber(key.Data());
  EcDemandOrdinanceGroup **slot = &EcDemandOrdinanceEffects.first;
  while (*slot && (*slot)->identifier < identifier)
    slot = &(*slot)->next;
  if (!*slot || (*slot)->identifier != identifier)
    *slot = new EcDemandOrdinanceGroup{*slot, identifier, nullptr};
  EcDemandOrdinanceGroup *group = *slot;
  while (group->first) {
    EcDemandOrdinanceEffect *effect = group->first;
    group->first = effect->next;
    delete effect;
  }
  EcDemandOrdinanceEffect **tail = &group->first;
  ScString remaining(value);
  bool more;
  do {
    ScString before(remaining), row = remaining.SplitToken("+");
    more = !remaining.IsEqual(&before);
    if (!more)
      row = remaining;
    ScString valve = row.SplitToken(",");
    *tail = new EcDemandOrdinanceEffect{nullptr, EcParseTuningNumber(valve.Data()), f32(f64(atol(row.Data())) * 0.001)};
    tail = &(*tail)->next;
  } while (more);
}

// Reload the agent and connection coefficients, then apply configured ordinance rows from the same tuning file. The
// original leaves ordinance entries absent from the new file intact until static shutdown.
bool EcDemand::StaticInit(CtDefinition *) {
  EcClearDemandEffects(&EcDemandAgentEffects.first);
  EcClearDemandEffects(&EcDemandConnectionEffects.first);
  ScString path;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SC3ValveLayer.ini");
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  ScString agent("AgentValveEffects"), connection("ConnectionValveEffects"), ordinance("OrdinanceEffects");
  ini.EnumerateSection(&agent, EcReadDemandEffects, &EcDemandAgentEffects);
  ini.EnumerateSection(&connection, EcReadDemandEffects, &EcDemandConnectionEffects);
  ini.EnumerateSection(&ordinance, EcReadDemandOrdinances, nullptr);
  return true;
}

// Restore the two density maps and taxable counters from the versioned demand record.
// A failed record is marked in the frame; the original city-layer entry still returns true.
bool EcDemand::Init(CtCity *owner, RsDatabaseSegment *segment) {
  city = owner;
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcDemandLayerType, 0});
  u8 result = frame.good;
  RsStream *stream = nullptr;
  if (result) {
    stream = frame.GetStream();
    if (!stream)
      result = 0;
    if (result && frame.text_flag) {
      ScString ignored;
      result = stream->ReadString(&ignored);
    }
    if (result)
      result = density->SerialRead(stream);
  }
  if (frame.version >= 2) {
    if (result)
      result = current_density->SerialRead(stream);
    for (u32 i = 0; result && i < 3; ++i)
      result = stream->ReadU32(&taxable[i]);
    for (u32 i = 0; result && i < 3; ++i)
      result = stream->ReadU32(&next_taxable[i]);
  }
  if (result && frame.text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  if (!result)
    frame.MarkFailed();
  return true;
}

// Store the two density maps followed by published and accumulating taxable totals.
// Mark the frame failed on the first rejected operation.
bool EcDemand::Save(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcDemandLayerType, 0}, 2, 0);
  RsStream *stream = frame.GetStream();
  bool result = stream && density->SerialWrite(stream) && current_density->SerialWrite(stream);
  for (u32 i = 0; result && i < 3; ++i)
    result = stream->WriteU32(taxable[i]);
  for (u32 i = 0; result && i < 3; ++i)
    result = stream->WriteU32(next_taxable[i]);
  if (!result)
    frame.MarkFailed();
  return result;
}

bool EcDemand::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  return message->type == kWnMsg_QueryOpened && HandleQuery(message, source);
}

// Release the acquired query view after its original no-content response or raster insertion.
u8 EcDemand::HandleQuery(ScMessage *message, ScUnknown *source) {
  u32 kind = u32(message->data1);
  if (!source)
    return 0;
  if (kind == kUiQueryTagDialog || kind == kUiQueryTagDebug || kind == kUiQueryTagStandard) {
    void *view = nullptr;
    u32 iid = kind == kUiQueryTagDialog ? IID_UiQueryWindow : IID_UiQuerySection;
    if (!source->QueryInterface(iid, &view))
      return 0;
    static_cast<ScUnknown *>(view)->Release();
    return 1;
  }
  if (kind == kUiQueryTagMaps) {
    void *view = nullptr;
    if (!source->QueryInterface(IID_UiRasterQuery, &view))
      return 0;
    UiRasterQuery *query = static_cast<UiRasterQuery *>(view);
    void *created = nullptr;
    u8 result = CreateRasterSource(kUiPopulationMapRasterId, IID_GxRasterSource, &created);
    if (result) {
      GxRasterSource *raster = static_cast<GxRasterSource *>(created);
      result = query->AddRaster(raster);
      raster->Release();
    }
    query->Release();
    return result;
  }
  return 0;
}

// Retain the actual pollution owner, capture its Z dimension before X, then publish kind and dimensions virtually.
EcMapSource::EcMapSource(u32 value, EcPollution *source, u32) : owner(source) {
  owner->AddRef();
  i32 z = static_cast<i32>(owner->CellCountZ()), x = static_cast<i32>(owner->CellCountX());
  SetId(value);
  SetBaseSize(x, z);
}

EcMapSource::~EcMapSource() {
  ScRelease(&owner);
}

EcAirSource::EcAirSource(EcPollution *source) : EcMapSource(kUiAirPollutionMapRasterId, source, 0) {
}

// Clamp against the maximum captured before the pollution getter, then scale through the live borrowed palette.
// Zero values and absent palettes leave the original output untouched; multiplication retains DWORD wrap.
bool EcAirSource::GetColor(u32 x, u32 z, u32 *out) {
  u32 maximum = EcAirMaximum, value = z;
  owner->GetAirValue(x, z, &value);
  if (value > maximum)
    value = maximum;
  if (!value)
    return false;
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i32 count = colors->GetNumberEntries();
  return colors->GetColor(static_cast<i32>(value * static_cast<u32>(count - 1) / maximum), out);
}

EcWaterSource::EcWaterSource(EcPollution *source) : EcMapSource(kUiWaterPollutionMapRasterId, source, 0) {
}

// Read water pollution after capturing its maximum, then preserve the original zero/null-palette exits and scale.
// The palette receiver captured before Count is retained for the final lookup even if a callback replaces the field.
bool EcWaterSource::GetColor(u32 x, u32 z, u32 *out) {
  u32 maximum = EcWaterMaximum, value = z;
  owner->GetWaterValue(x, z, &value);
  if (value > maximum)
    value = maximum;
  if (!value)
    return false;
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i32 count = colors->GetNumberEntries();
  return colors->GetColor(static_cast<i32>(value * static_cast<u32>(count - 1) / maximum), out);
}

EcGarbageSource::EcGarbageSource(EcPollution *source) : EcMapSource(kUiGarbagePollutionMapRasterId, source, 0) {
}

// Map actual garbage values through the maximum captured before the virtual query and then the borrowed palette.
// Preserve unsigned scale arithmetic and the palette's genuine Boolean/output contract.
bool EcGarbageSource::GetColor(u32 x, u32 z, u32 *out) {
  u32 maximum = EcGarbageMaximum, value = z;
  owner->GetGarbageValue(x, z, &value);
  if (value > maximum)
    value = maximum;
  if (!value)
    return false;
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i32 count = colors->GetNumberEntries();
  return colors->GetColor(static_cast<i32>(value * static_cast<u32>(count - 1) / maximum), out);
}

EcRadiationSource::EcRadiationSource(EcPollution *source) : EcMapSource(kUiRadiationMapRasterId, source, 0) {
}

// Return no sample for a nonradioactive cell; otherwise perform both original graphics-service lookups and RGB conversion.
// Only the low WORD of RGB is widened into the original DWORD output.
bool EcRadiationSource::GetColor(u32 x, u32 z, u32 *out) {
  if (!owner->IsRadioactive(x, z))
    return false;
  EcGetGraphics();
  *out = EcGetGraphics()->ConvertRGBValueToNative16Bit(255, 100, 0);
  return true;
}

EcNeighborGate::EcNeighborGate() {
}

EcNeighborGate::~EcNeighborGate() {
}

bool EcNeighborGate::Init(CtCity *city, OcPersistInfo *info) {
  return EcCivicBuilding::Init(city, info);
}

bool EcNeighborGate::Save(CtCity *city, OcPersistInfo *info) {
  return EcCivicBuilding::Save(city, info);
}

u8 EcNeighborGate::ReadRecord(CtCity *, RsStream *) {
  return 0;
}

u8 EcNeighborGate::WriteRecord(CtCity *, RsStream *) {
  return 0;
}

i32 EcNeighborGate::MinimumExecutionInterval() {
  i32 interval = EcCivicBuilding::MinimumExecutionInterval();
  return interval >= 3 ? 3 : interval;
}

// Run the civic agent first and check gate placement on phase three, preserving the base result.
bool EcNeighborGate::Simulate(i32 elapsed) {
  bool result = EcCivicBuilding::Simulate(elapsed);
  if (elapsed == 3)
    ValidatePlacement();
  return result;
}

// Count adjacent gate occupants in original order and remove this occupant when unsupported by connected water.
void EcNeighborGate::ValidatePlacement() {
  CtCity *city = EcGetRciCity();
  if (!city)
    return;
  ScDirtBag *terrain = city->DirtBag();
  ScOccupantManager *surface = city->SurfaceOccupantManager();
  u32 x, z;
  GetLocationInCellCoords(&x, &z);
  if (surface) {
    u8 neighbors = 0;
    ScOccupant *occupant;
    if (x && surface->GetOccupantAtCell(x - 1, z, &occupant)) {
      if (occupant->TypeId() == kCtAgentSeaport)
        ++neighbors;
      occupant->Release();
    }
    if (x < surface->GetCellsInX() - 1 && surface->GetOccupantAtCell(x + 1, z, &occupant)) {
      if (occupant->TypeId() == kCtAgentSeaport)
        ++neighbors;
      occupant->Release();
    }
    if (neighbors > 1)
      return;
    if (z && surface->GetOccupantAtCell(x, z - 1, &occupant)) {
      if (occupant->TypeId() == kCtAgentSeaport)
        ++neighbors;
      occupant->Release();
    }
    if (z < surface->GetCellsInZ() - 1 && surface->GetOccupantAtCell(x, z + 1, &occupant)) {
      if (occupant->TypeId() == kCtAgentSeaport)
        ++neighbors;
      occupant->Release();
    }
    if (neighbors > 1)
      return;
  }
  if (terrain) {
    u32 distance;
    if ((!terrain->IsWater(x, z) || !terrain->Distance(x, z, &distance)) && CanSimRemove()) {
      surface = city->SurfaceOccupantManager();
      if (surface)
        surface->RemoveOccupantAtCell(x, z);
    }
  }
}

static ScLock EcOccupantLock; // Original module-wide recursive reference lock at20918.
