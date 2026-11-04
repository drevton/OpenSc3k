#include <assert.h>
#include <string.h>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/files.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"

// Actual kind-face records; each original kind occupies120bytes.
const BdFace BdCellFaces[16][8] = {
    {
        {16, {4, 7, 6, 5}, {0, 1, 2, 3}},
        {3, {5, 6, 2, 1}, {0, 1, 2, 3}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 5, 1, 0}, {0, 1, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {16, {4, 7, 6, -1}, {0, 1, 2, 255}},
        {2, {4, 6, 2, 0}, {0, 1, 2, 3}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {48, {2, 3, 0, -1}, {1, 2, 3, 255}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {1, {4, 1, 0, -1}, {0, 2, 3, 255}},
        {11, {4, 7, 2, 1}, {0, 1, 2, 3}},
        {5, {7, 3, 2, -1}, {1, 2, 3, 255}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {9, {7, 6, 1, -1}, {0, 1, 2, 255}},
        {11, {4, 7, 1, -1}, {0, 1, 3, 255}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 1, 0, -1}, {0, 2, 3, 255}},
        {3, {6, 2, 1, -1}, {1, 2, 3, 255}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {9, {7, 1, 0, -1}, {0, 2, 3, 255}},
        {11, {7, 2, 1, -1}, {1, 2, 3, 255}},
        {5, {7, 3, 2, -1}, {1, 2, 3, 255}},
        {7, {7, 0, 3, -1}, {0, 2, 3, 255}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {16, {4, 7, 6, 5}, {0, 1, 2, 3}},
        {5, {6, 7, 3, -1}, {0, 1, 2, 255}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 5, 0, -1}, {0, 1, 3, 255}},
        {40, {5, 6, 3, 0}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {16, {4, 7, 6, -1}, {0, 1, 2, 255}},
        {3, {6, 2, 1, -1}, {1, 2, 3, 255}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 1, 0, -1}, {0, 2, 3, 255}},
        {26, {4, 6, 1, -1}, {0, 1, 3, 255}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {26, {7, 2, 0, -1}, {1, 2, 3, 255}},
        {5, {7, 3, 2, -1}, {1, 2, 3, 255}},
        {7, {7, 0, 3, -1}, {0, 2, 3, 255}},
        {48, {2, 3, 0, -1}, {1, 2, 3, 255}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {56, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {1, {4, 9, 1, 0}, {0, 6, 2, 3}},
        {35, {4, 7, 10, 9}, {0, 1, 2, 3}},
        {5, {7, 3, 2, 10}, {1, 2, 3, 4}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {3, {9, 10, 2, 1}, {4, 6, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {1, {8, 1, 0, -1}, {4, 2, 3, 255}},
        {35, {8, 11, 2, 1}, {0, 1, 2, 3}},
        {5, {11, 3, 2, -1}, {6, 2, 3, 255}},
        {7, {11, 8, 0, 3}, {4, 6, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {33, {7, 6, 9, -1}, {0, 1, 2, 255}},
        {35, {4, 7, 9, -1}, {0, 1, 3, 255}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 9, 1, 0}, {0, 6, 2, 3}},
        {3, {6, 2, 1, 9}, {1, 2, 3, 4}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {33, {11, 10, 1, -1}, {0, 1, 2, 255}},
        {35, {8, 11, 1, -1}, {0, 1, 3, 255}},
        {5, {10, 11, 3, 2}, {4, 6, 2, 3}},
        {7, {11, 8, 0, 3}, {4, 5, 2, 3}},
        {1, {8, 1, 0, -1}, {4, 2, 3, 255}},
        {3, {10, 2, 1, -1}, {6, 2, 3, 255}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {33, {7, 9, 8, -1}, {0, 2, 3, 255}},
        {35, {7, 10, 9, -1}, {1, 2, 3, 255}},
        {5, {7, 3, 2, 10}, {1, 2, 3, 4}},
        {7, {7, 8, 0, 3}, {0, 6, 2, 3}},
        {1, {8, 9, 1, 0}, {4, 6, 2, 3}},
        {3, {9, 10, 2, 1}, {4, 6, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {33, {11, 1, 0, -1}, {0, 2, 3, 255}},
        {35, {11, 2, 1, -1}, {1, 2, 3, 255}},
        {5, {11, 3, 2, -1}, {6, 2, 3, 255}},
        {7, {11, 0, 3, -1}, {4, 2, 3, 255}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
    {
        {16, {4, 7, 6, 5}, {0, 1, 2, 3}},
        {3, {5, 6, 2, 1}, {0, 1, 2, 3}},
        {5, {6, 7, 3, 2}, {0, 1, 2, 3}},
        {7, {7, 4, 0, 3}, {0, 1, 2, 3}},
        {1, {4, 5, 1, 0}, {0, 1, 2, 3}},
        {48, {0, 3, 2, 1}, {0, 1, 2, 3}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {-1, {0, 0, 0, 0}, {0, 0, 0, 0}},
    },
};

// Original twelve-vertex rotation permutations.
const u8 BdCellRotations[4][12] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    {1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8},
    {2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9},
    {3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10},
};

// Adjacent-cell vertex correspondence;31 marks the nonshared vertex bit.
const u8 BdCellAdjacentVertices[6][12] = {
    {31, 31, 31, 31, 0, 1, 2, 3, 31, 31, 31, 31},
    {4, 5, 6, 7, 31, 31, 31, 31, 31, 31, 31, 31},
    {31, 31, 1, 0, 31, 31, 5, 4, 31, 31, 8, 9},
    {3, 2, 31, 31, 7, 6, 31, 31, 11, 10, 31, 31},
    {1, 31, 31, 2, 5, 31, 31, 6, 9, 31, 31, 10},
    {31, 0, 3, 31, 31, 4, 7, 31, 31, 8, 11, 31},
};

// Defined fields initialized by12011B53. Its copied eight-byte scratch padding is intentionally omitted.
const BdNeighbor BdCellNeighbors[6] = {
    {{0, 0, -1}, 15},
    {{0, 0, 1}, 240},
    {{0, 1, 0}, 51},
    {{0, -1, 0}, 204},
    {{-1, 0, 0}, 102},
    {{1, 0, 0}, 153},
};

// Actual per-kind/per-orientation face submission lists, including their terminating negative entries.
const i8 BdCellFaceOrder[16][4][6] = {
    {{0, 3, 4, -1, -1, -1}, {0, 2, 3, -1, -1, -1}, {0, 1, 2, -1, -1, -1}, {0, 1, 4, -1, -1, -1}},
    {{0, 3, -1, -1, -1, -1}, {0, 2, 3, -1, -1, -1}, {0, 2, -1, -1, -1, -1}, {0, 1, -1, -1, -1, -1}},
    {{0, 3, -1, -1, -1, -1}, {2, 3, -1, -1, -1, -1}, {1, 2, -1, -1, -1, -1}, {0, 1, -1, -1, -1, -1}},
    {{0, 3, 4, -1, -1, -1}, {2, 3, -1, -1, -1, -1}, {1, 5, 2, -1, -1, -1}, {0, 5, 4, 1, -1, -1}},
    {{0, 3, -1, -1, -1, -1}, {2, 3, -1, -1, -1, -1}, {1, 2, -1, -1, -1, -1}, {0, 1, -1, -1, -1, -1}},
    {{0, 2, 3, -1, -1, -1}, {0, 1, 2, -1, -1, -1}, {0, 1, -1, -1, -1, -1}, {0, 3, -1, -1, -1, -1}},
    {{0, 3, 4, 5, -1, -1}, {0, 2, 3, -1, -1, -1}, {0, 1, 2, 5, -1, -1}, {0, 1, 4, 5, -1, -1}},
    {{0, 2, -1, -1, -1, -1}, {1, 2, -1, -1, -1, -1}, {0, 1, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1}},
    {{0, -1, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1}},
    {{0, 3, 1, -1, -1, -1}, {2, 3, 1, -1, -1, -1}, {1, 2, 4, -1, -1, -1}, {0, 1, 4, -1, -1, -1}},
    {{0, 3, 1, -1, -1, -1}, {2, 3, 1, -1, -1, -1}, {1, 2, -1, -1, -1, -1}, {0, 1, -1, -1, -1, -1}},
    {{0, 3, 4, 1, -1, -1}, {2, 3, 0, 1, -1, -1}, {1, 5, 2, 0, -1, -1}, {0, 5, 4, 1, -1, -1}},
    {{0, 3, 4, 1, -1, -1}, {2, 3, 0, 1, -1, -1}, {1, 5, 2, 0, -1, -1}, {0, 5, 4, 1, -1, -1}},
    {{0, 3, 1, 4, -1, -1}, {2, 3, 0, 1, -1, -1}, {1, 2, 0, 5, -1, -1}, {0, 1, 4, 5, -1, -1}},
    {{0, 3, 1, -1, -1, -1}, {2, 3, 0, 1, -1, -1}, {1, 2, 0, -1, -1, -1}, {0, 1, -1, -1, -1, -1}},
    {{0, 3, 4, -1, -1, -1}, {0, 2, 3, -1, -1, -1}, {0, 1, 2, -1, -1, -1}, {0, 1, 4, -1, -1, -1}}};

bool BD_STDCALL BdZeroViewBytes(const BdCell *cell) {
  return (cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind && cell->parts[1].first == 0 &&
      cell->parts[2].first == 0 && cell->parts[3].first == 0 && cell->parts[4].first == 0;
}

// Compare the first byte in all four views after requiring object kind on both records.
bool BD_STDCALL BdEqualViewBytes(const BdCell *first, const BdCell *second) {
  return (first->link_height_kind & kBdCellKindMask) == kBdCellObjectKind &&
      (second->link_height_kind & kBdCellKindMask) == kBdCellObjectKind &&
      first->parts[1].first == second->parts[1].first && first->parts[2].first == second->parts[2].first &&
      first->parts[3].first == second->parts[3].first && first->parts[4].first == second->parts[4].first;
}

// Project a tile's displacement from an object origin and compare the first two views' signed byte offsets.
// These stored offsets wrap at eight bits, so compare their byte representation rather than widening the record.
bool BdGrid::MatchesObjectOrigin(const BdPoint3 *tile, const BdPoint3 *origin, const BdCell *cell) {
  i32 x = tile->x - origin->x, y = tile->y - origin->y, z = tile->z - origin->z;
  i32 first_x, second_x, first_y, second_y;
  switch (rotation) {
  case 0:
    first_x = y - x;
    second_x = x + y;
    first_y = x + y - 2 * z;
    second_y = x - y - 2 * z;
    break;
  case 1:
    first_x = -x - y;
    second_x = y - x;
    first_y = y - x - 2 * z;
    second_y = x + y - 2 * z;
    break;
  case 2:
    first_x = x - y;
    second_x = -x - y;
    first_y = -x - y - 2 * z;
    second_y = y - x - 2 * z;
    break;
  case 3:
    first_x = x + y;
    second_x = x - y;
    first_y = x - y - 2 * z;
    second_y = -x - y - 2 * z;
    break;
  default:
    return false;
  }
  return static_cast<u8>(cell->parts[1].offset_x) == static_cast<u8>(first_x) &&
      static_cast<u8>(cell->parts[1].offset_y) == static_cast<u8>(first_y) &&
      static_cast<u8>(cell->parts[2].offset_x) == static_cast<u8>(second_x) &&
      static_cast<u8>(cell->parts[2].offset_y) == static_cast<u8>(second_y);
}

// Reserve the current saved-cell position in the next undo record when recording and capacity allow it.
// This prepares the record and last-change cursor; callers append the actual operation and advance both counts.
bool BdGrid::PrepareChange() {
  if (changes_disabled || change_count >= 0x7ffe || saved_cell_count >= 0x7ffd)
    return false;
  changes[change_count].saved_index = saved_cell_count;
  last_change = change_count;
  return true;
}

// Reset the undo cursors and both active record counts without clearing their retained payload storage.
void BdGrid::ResetChanges() {
  saved_cell_count = 0;
  change_count = 0;
  last_change = 0;
}

// End the preceding undo group by negating its final positive before-code when a record exists.
void BdGrid::CloseChangeGroup() {
  if (static_cast<i32>(change_count) > 0) {
    i8 *before = &changes[change_count - 1].before;
    if (*before > 0)
      *before = static_cast<i8>(-*before);
  }
}

// Mark the supplied horizontal volume in the existing dirty-column table without changing its vertical state.
// The original loop counters start and wrap at 16 bits while their signed end bounds retain all 32 bits.
void BdGrid::MarkDirtyColumns(const BdVolume *volume) {
  for (u16 x = static_cast<u16>(volume->origin.x);
      static_cast<i32>(x) < static_cast<i32>(static_cast<u32>(volume->origin.x) + volume->size.x); ++x)
    for (u16 y = static_cast<u16>(volume->origin.y);
        static_cast<i32>(y) < static_cast<i32>(static_cast<u32>(volume->origin.y) + volume->size.y); ++y)
      dirty_columns[x][y] = 1;
}

// Copy a localized value into the grid before releasing its temporary resource handle.
// The intermediate value has its own allocation, as in the original construction sequence.
static ScString BdGridText(u32 instance) {
  BdTextResource resource(instance, kRsArchitectTextGroup);
  ScString value = resource.GetRawText();
  return ScString(value);
}

// Construct the grid's owned strings, empty storage and material table.
BdGrid::BdGrid()
    : localized_465(BdGridText(kRsArchitectMaxisText)), localized_466(BdGridText(kRsArchitectNewBuildingText)) {
  initialized = 0;
  view_mode = 2;
  mode = 2;
  mode_bytes[0] = 0;
  mode_bytes[1] = 28;
  mode_bytes[2] = 0;
  field_04c = 0;
  field_054 = 0;
  buffer = nullptr;
  side = 0;
  height = 0;
  bounds.origin = {0, 0, 0};
  bounds.size = {0, 0, 0};
  rotation = 0;
  cells = nullptr;
  columns = nullptr;
  render_columns = nullptr;
  slice_z = nullptr;
  slice_y = nullptr;
  slice_x = nullptr;
  aux_columns = nullptr;
  dirty_columns = nullptr;
  aux_bounds.origin = {0, 0, 0};
  aux_bounds.size = {0, 0, 0};
  changed = 0;
  field_1a8 = 0;
  changes_disabled = 0;
  for (u32 i = 0; i < 0x7FFF; ++i)
    changes[i].tile = {0, 0, 0};
  references = 0;
  materials = ScAddRefNotNull(new BdMaterials);
}

BdGrid::~BdGrid() {
  SetupStorage(0, 0);
  materials->Release();
}

// Release every allocated row and then its pointer table in the source order.
static void BdFreeGridColumns(u32 **table, u32 side) {
  for (u32 x = 0; x < side; ++x)
    delete[] table[x];
  delete[] table;
}

// Allocate or release the linked-cell pool, seven tables and the grid's actual graphics buffer. Ground cells and undo
// metadata are initialized only after the original buffer callbacks complete.
bool BdGrid::SetupStorage(u32 width, u32 depth) {
  if (!width && !depth) {
    if (cells) {
      delete[] cells;
      cells = nullptr;
    }
    if (columns) {
      BdFreeGridColumns(columns, side);
      columns = nullptr;
    }
    if (render_columns) {
      BdFreeGridColumns(render_columns, side);
      render_columns = nullptr;
    }
    if (slice_z) {
      BdFreeGridColumns(slice_z, side);
      slice_z = nullptr;
    }
    if (slice_y) {
      BdFreeGridColumns(slice_y, side);
      slice_y = nullptr;
    }
    if (slice_x) {
      BdFreeGridColumns(slice_x, side);
      slice_x = nullptr;
    }
    if (aux_columns) {
      BdFreeGridColumns(aux_columns, side);
      aux_columns = nullptr;
    }
    if (dirty_columns) {
      for (u32 x = 0; x < static_cast<u32>(side); ++x)
        delete[] dirty_columns[x];
      delete[] dirty_columns;
      dirty_columns = nullptr;
    }
    ScRelease(&buffer);
    initialized = 0;
    return true;
  }
  SetupStorage(0, 0);
  side = static_cast<i32>(width);
  height = static_cast<i32>(depth);
  bounds.origin = {0, 0, 0};
  bounds.size = {side, side, height};
  view_mode = static_cast<u8>(width) >> 3;
  cells = new BdCell[0x20000];
  columns = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    columns[x] = new u32[width];
  render_columns = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    render_columns[x] = new u32[width];
  slice_z = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    slice_z[x] = new u32[width];
  slice_y = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    slice_y[x] = new u32[depth];
  slice_x = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    slice_x[x] = new u32[depth];
  aux_columns = new u32 *[width];
  for (u32 x = 0; x < width; ++x)
    aux_columns[x] = new u32[width];
  dirty_columns = new u8 *[width];
  for (u32 x = 0; x < width; ++x)
    dirty_columns[x] = new u8[width];
  memset(static_cast<void *>(cells), 0, sizeof(BdCell) * 0x20000);
  for (u32 x = 0; x < width; ++x) {
    memset(columns[x], 0, sizeof(u32) * width);
    memset(render_columns[x], 0, sizeof(u32) * width);
    memset(slice_z[x], 0, sizeof(u32) * width);
    memset(aux_columns[x], 0, sizeof(u32) * width);
    memset(dirty_columns[x], 0, width);
  }
  for (u32 x = 0; x < width; ++x) {
    memset(slice_y[x], 0, sizeof(u32) * depth);
    memset(slice_x[x], 0, sizeof(u32) * depth);
  }
  BdGetGraphics()->CreateBuffer(&buffer);
  GxColorType current = BdGetGraphics()->GetScreenColorType();
  buffer->Init(200, 200, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (buffer->Lock(GxLockFlags::Fill)) {
    buffer->Fill(0, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  ResetStorage();
  InitializeGround();
  ResetChanges();
  initialized = 1;
  return true;
}

// Form the rotated vertex mask of one trusted kind-face record, including its optional fourth vertex.
static u32 BdFaceMask(const BdFace *face, const u8 *rotation) {
  u32 mask = (1u << rotation[face->vertex[0]]) | (1u << rotation[face->vertex[1]]) | (1u << rotation[face->vertex[2]]);
  if (face->vertex[3] != -1)
    mask |= 1u << rotation[face->vertex[3]];
  return mask;
}

// Translate a neighboring face into the current cell's vertex space. Bit31 rejects faces crossing an unshared vertex.
static u32 BdAdjacentFaceMask(const BdFace *face, const u8 *rotation, const u8 *adjacent) {
  u32 mask = (1u << adjacent[rotation[face->vertex[0]]]) | (1u << adjacent[rotation[face->vertex[1]]]) |
      (1u << adjacent[rotation[face->vertex[2]]]);
  if (face->vertex[3] != -1)
    mask |= 1u << adjacent[rotation[face->vertex[3]]];
  return mask;
}

// Publish each face, then hide compatible shared faces or restore incompatible ones in the original neighbor order.
void BdGrid::PublishFaces(const BdPoint3 *tile, BdCell *cell) {
  const BdFace *faces = BdCellFaces[(cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask];
  const u8 *vertex_order = BdCellRotations[cell->link_height_kind >> kBdCellRotationShift];
  bool object = (cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind;
  BdPoint3 origin = {};
  cell->face_mask = 0;
  if (object)
    GetObjectOrigin(tile, &origin, cell);
  for (u32 face = 0; faces[face].flags != -1; ++face) {
    u32 bit = 1u << face, vertices = BdFaceMask(&faces[face], vertex_order);
    bool compatible = true;
    cell->face_mask |= bit;
    ++face_count;
    for (u32 direction = 0; direction < 6; ++direction) {
      const BdNeighbor *neighbor = &BdCellNeighbors[direction];
      if ((vertices & neighbor->vertices) != vertices)
        continue;
      BdPoint3 point = {tile->x + neighbor->offset.x, tile->y + neighbor->offset.y, tile->z + neighbor->offset.z};
      if (!BdContains(&point, &bounds)) {
        if (mode >= 2 && tile->z == 0 && face != 0 && (cell->link_height_kind & kBdCellKindMask) == 0 &&
            (cell->face_mask & bit)) {
          cell->face_mask &= ~bit;
          ++hidden_face_count;
        }
        continue;
      }
      i32 index = FindCell(&point, object, 0);
      if (!index)
        continue;
      BdCell *other = &cells[index];
      bool other_object = (other->link_height_kind & kBdCellKindMask) == kBdCellObjectKind;
      if (object != other_object ||
          (object && (!BdEqualViewBytes(cell, other) || !MatchesObjectOrigin(&point, &origin, other))))
        compatible = false;
      const BdFace *other_faces = BdCellFaces[(other->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask];
      const u8 *other_rotation = BdCellRotations[other->link_height_kind >> kBdCellRotationShift];
      for (u32 other_face = 0; other_faces[other_face].flags != -1; ++other_face) {
        u32 other_bit = 1u << other_face;
        u32 other_vertices =
            BdAdjacentFaceMask(&other_faces[other_face], other_rotation, BdCellAdjacentVertices[direction]);
        if (other_vertices & kScIntegerSignBit)
          continue;
        u32 intersection = vertices & other_vertices;
        if (intersection == vertices) {
          if (compatible && (cell->face_mask & bit)) {
            cell->face_mask &= ~bit;
            ++hidden_face_count;
          } else if (!compatible && !(cell->face_mask & bit)) {
            cell->face_mask |= bit;
            --hidden_face_count;
          }
        }
        if (intersection == other_vertices) {
          if (compatible && (other->face_mask & other_bit)) {
            other->face_mask &= ~other_bit;
            ++hidden_face_count;
          } else if (!compatible && !(other->face_mask & other_bit)) {
            other->face_mask |= other_bit;
            --hidden_face_count;
          }
        }
      }
    }
  }
}

// Removing a face restores mapped neighbor faces and subtracts their hidden-face contributions in source order.
void BdGrid::RemoveFaces(const BdPoint3 *tile, BdCell *cell) {
  const BdFace *faces = BdCellFaces[(cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask];
  const u8 *vertex_order = BdCellRotations[cell->link_height_kind >> kBdCellRotationShift];
  bool object = (cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind;
  for (u32 face = 0; faces[face].flags != -1; ++face) {
    u32 vertices = BdFaceMask(&faces[face], vertex_order);
    --face_count;
    for (u32 direction = 0; direction < 6; ++direction) {
      const BdNeighbor *neighbor = &BdCellNeighbors[direction];
      if ((vertices & neighbor->vertices) != vertices)
        continue;
      BdPoint3 point = {tile->x + neighbor->offset.x, tile->y + neighbor->offset.y, tile->z + neighbor->offset.z};
      if (!BdContains(&point, &bounds))
        continue;
      i32 index = FindCell(&point, object, 0);
      if (!index)
        continue;
      BdCell *other = &cells[index];
      const BdFace *other_faces = BdCellFaces[(other->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask];
      const u8 *other_rotation = BdCellRotations[other->link_height_kind >> kBdCellRotationShift];
      for (u32 other_face = 0; other_faces[other_face].flags != -1; ++other_face) {
        u32 other_vertices =
            BdAdjacentFaceMask(&other_faces[other_face], other_rotation, BdCellAdjacentVertices[direction]);
        if (other_vertices & kScIntegerSignBit)
          continue;
        if ((vertices & other_vertices) == vertices && !(cell->face_mask & (1u << face)))
          --hidden_face_count;
        u32 other_bit = 1u << other_face;
        if (!(other->face_mask & other_bit)) {
          other->face_mask |= other_bit;
          --hidden_face_count;
        }
      }
    }
  }
}

// Republish each linked cell in one column, reading its next link after its face effects finish.
// This native shared loop preserves the two visits used by the original checkerboard traversal.
static void BdPublishColumn(BdGrid *grid, u32 x, u32 y) {
  for (u32 index = grid->columns[x][y]; index; index = grid->cells[index].link_height_kind & kBdCellNextMask) {
    BdCell *cell = &grid->cells[index];
    BdPoint3 tile = {static_cast<i32>(x), static_cast<i32>(y),
        static_cast<i32>((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask)};
    grid->PublishFaces(&tile, cell);
  }
}

// Republish column faces in the original checkerboard and alternating boundary order across the complete grid side.
// Keep the existing counters: the hidden-cell cleanup caller performs its separate recount afterward.
void BdGrid::RebuildFaces() {
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    u32 interior = x != 0 && x < static_cast<u32>(side) - 1;
    for (u32 y = x & interior; y < static_cast<u32>(side); y += interior + 1)
      BdPublishColumn(this, x, y);
    if (interior) {
      u32 y = (static_cast<u32>(side) - 1) & ((x & 1) - 1);
      BdPublishColumn(this, x, y);
    }
  }
}

// Rebuild visibility, recount all faces, and mark fully hidden nonobject cells above ground before removing them.
// Refresh render slices on both sides of removal, preserve successor links across recycling, and clear the changed byte last.
const BdVolume *BdGrid::RemoveHiddenCells() {
  BdPoint3 cursor = {0, 0, 1};
  BuildRenderSlices(&bounds, &cursor);
  RebuildFaces();
  hidden_face_count = 0;
  face_count = 0;
  for (i32 x = bounds.origin.x; x < static_cast<i32>(static_cast<u32>(bounds.origin.x) + bounds.size.x); ++x)
    for (i32 y = bounds.origin.y; y < static_cast<i32>(static_cast<u32>(bounds.origin.y) + bounds.size.y); ++y)
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask) {
        BdCell *cell = &cells[index];
        const BdFace *faces = BdCellFaces[(cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask];
        for (u32 face = 0; faces[face].flags != -1; ++face) {
          ++face_count;
          if (!(cell->face_mask & (1u << face)))
            ++hidden_face_count;
        }
        if (!cell->face_mask && (cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind &&
            (cell->link_height_kind & kBdCellHeightMask))
          cell->face_mask = 256;
      }
  for (i32 x = bounds.origin.x; x < static_cast<i32>(static_cast<u32>(bounds.origin.x) + bounds.size.x); ++x)
    for (i32 y = bounds.origin.y; y < static_cast<i32>(static_cast<u32>(bounds.origin.y) + bounds.size.y); ++y) {
      u32 index = columns[x][y];
      while (index) {
        BdCell *cell = &cells[index];
        u32 mask = cell->face_mask, packed = cell->link_height_kind;
        index = packed & kBdCellNextMask;
        if (mask & 256) {
          BdPoint3 tile = {x, y, static_cast<i32>((packed >> kBdCellHeightShift) & kBdCellHeightValueMask)};
          RemoveCell(&tile);
        }
      }
    }
  cursor = {0, 0, 0};
  const BdVolume *result = BuildRenderSlices(&bounds, &cursor);
  changed = 0;
  return result;
}

static_assert(sizeof(BdCellPart) == 4);
static_assert(sizeof(BdCell) == 32);

BdCell::BdCell() : link_height_kind(kBdCellFreeHeight), face_mask(255), parts{} {
}

// Restore an existing cell to the same empty reserved-height state as the original reset routine.
void BdCell::Reset() {
  link_height_kind = kBdCellFreeHeight;
  face_mask = 255;
  for (u32 i = 0; i < 6; ++i)
    parts[i] = {};
}

// Convert the defined kind, rotation, height, face mask and next index from one 76-byte legacy cell record.
// Preserve every existing projected material/view byte and return the packed word left by the original method.
u32 BdCell::LoadLegacy(const BdLegacyCell *source) {
  u32 packed = (link_height_kind & ~kBdCellNextMask) | source->next;
  packed = (packed & kBdCellWithoutKind) | ((source->kind & kBdCellKindValueMask) << kBdCellKindShift);
  packed = (packed & kBdCellWithoutRotation) | ((static_cast<u32>(source->rotation) & 3) << kBdCellRotationShift);
  face_mask = source->face_mask;
  link_height_kind = (packed & kBdCellWithoutHeight) |
      ((static_cast<u32>(source->height) & kBdCellHeightValueMask) << kBdCellHeightShift);
  return link_height_kind;
}

// Recognize an object's origin cell by its kind and all four views' zero coordinate offsets.
bool BD_STDCALL BdIsObjectOrigin(const BdCell *cell) {
  return (cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind && cell->parts[1].offset_x == 0 &&
      cell->parts[1].offset_y == 0 && cell->parts[2].offset_x == 0 && cell->parts[2].offset_y == 0 &&
      cell->parts[3].offset_x == 0 && cell->parts[3].offset_y == 0 && cell->parts[4].offset_x == 0 &&
      cell->parts[4].offset_y == 0;
}

// Search an ordered column chain for the requested height.
i32 BdGrid::FindCell(const BdPoint3 *tile, u8 include_objects, u8 require_origin) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return 0;
  u32 index = columns[tile->x][tile->y];
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    index = packed & kBdCellNextMask;
  }
  if (!index)
    return 0;
  BdCell *cell = &cells[index];
  if (((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) > static_cast<u32>(tile->z))
    return 0;
  if (include_objects) {
    if (require_origin && (cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind && !BdIsObjectOrigin(cell))
      return 0;
  } else if ((cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind)
    return 0;
  return static_cast<i32>(index);
}

// Recover an object's tile origin from its first two projected views and the grid's rotation.
// Unsupported rotations leave the output untouched; signed divisions truncate toward zero.
bool BdGrid::GetObjectOrigin(const BdPoint3 *tile, BdPoint3 *origin, const BdCell *cell) {
  i32 a = cell->parts[1].offset_y, b = cell->parts[2].offset_x, c = cell->parts[2].offset_y;
  i32 x, y, z = (b - a) / 2;
  switch (rotation) {
  case 0:
    x = (c + 2 * b - a) / 2;
    y = (a - c) / 2;
    break;
  case 1:
    x = (c - a) / 2;
    y = (c + 2 * b - a) / 2;
    break;
  case 2:
    x = (a - 2 * b - c) / 2;
    y = (c - a) / 2;
    break;
  case 3:
    x = (a - c) / 2;
    y = (a - 2 * b - c) / 2;
    break;
  default:
    return false;
  }
  *origin = *tile;
  origin->x -= x;
  origin->y -= y;
  origin->z -= z;
  return true;
}

u32 BdGrid::AddRef() {
  return ++references;
}

// Release a retained grid and destroy it through its real virtual destructor when the count reaches zero.
// The destructor releases the grid storage, graphics buffer, material table and owned strings.
u32 BdGrid::Release() {
  u32 remaining = --references;
  if (remaining == 0)
    delete this;
  return remaining;
}

// These module-local words are the original fast-insertion cursor, shared by all grid instances.
// Its coordinate sentinel is initialized once and is never subsequently assigned by game code.
static i32 BdQuickX = -1, BdQuickY = -1, BdQuickZ = -1;
static u32 BdQuickPrevious, BdQuickCurrent, BdQuickTarget, BdQuickNext;

// Build the three eight-bit projection masks for every cell kind and rotation from the actual face vertices.
// The original lookup views overlap in initialized static storage; keep the full accessed bytes in one bounded table.
void BdGrid::BuildCoverage() {
  static const u8 mapping[28] = {4, 5, 6, 7, 4, 5, 6, 7, 0, 1, 1, 0, 4, 5, 5, 4, 0, 0, 3, 3, 4, 4, 7, 7,
      31, 31, 31, 31};
  for (u32 kind = 0; kind < 16; ++kind) {
    for (u32 view = 0; view < 4; ++view) {
      u8 bottom = 0, left = 0, right = 0;
      const BdFace *face = BdCellFaces[kind];
      for (; face->flags >= 0; ++face) {
        u32 count = face->vertex[3] == -1 ? 3 : 4;
        for (u32 i = 0; i < count; ++i) {
          u8 vertex = BdCellRotations[view][face->vertex[i]];
          bottom |= static_cast<u8>(1u << mapping[vertex]);
          left |= static_cast<u8>(1u << mapping[8 + vertex]);
          right |= static_cast<u8>(1u << mapping[16 + vertex]);
        }
      }
      bottom_coverage[kind][view] = bottom;
      left_coverage[kind][view] = left;
      right_coverage[kind][view] = right;
    }
  }
}

// Insert or replace a raw cell without publishing faces or recording an undo operation.
// Preserve the input rotation mutation, terminal-link quirk, module cursor, maximum height and dirty effects.
bool BdGrid::InsertRawCell(const BdPoint3 *tile, BdCell *value) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return false;
  if (!(cells[free_head].link_height_kind & kBdCellNextMask)) {
    RemoveHiddenCells();
    CompactCells();
    ReconcileMode();
    if (!(cells[free_head].link_height_kind & kBdCellNextMask))
      return false;
  }
  if (!(value->link_height_kind & kBdCellKindMask))
    value->link_height_kind =
        (value->link_height_kind & kBdCellWithoutRotation) | (static_cast<u32>(rotation) << kBdCellRotationShift);
  u32 index;
  if (BdQuickX == tile->x && BdQuickY == tile->y && BdQuickZ <= tile->z)
    index = BdQuickCurrent;
  else {
    index = columns[tile->x][tile->y];
    BdQuickPrevious = 0;
    BdQuickCurrent = index;
  }
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    BdQuickPrevious = index;
    index = packed & kBdCellNextMask;
    BdQuickCurrent = index;
  }
  if (!index ||
      ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) != static_cast<u32>(tile->z)) {
    BdQuickTarget = free_head;
    free_head = cells[free_head].link_height_kind & kBdCellNextMask;
    BdQuickNext = BdQuickCurrent;
    if (BdQuickPrevious)
      cells[BdQuickPrevious].link_height_kind =
          (cells[BdQuickPrevious].link_height_kind & kBdCellWithoutNext) | (BdQuickTarget & kBdCellNextMask);
    else
      columns[tile->x][tile->y] = BdQuickTarget;
    ++cell_count;
  } else {
    if (!memcmp(value, &cells[index], sizeof(BdCell)))
      return false;
    BdQuickTarget = BdQuickCurrent;
    BdQuickNext = cells[BdQuickCurrent].link_height_kind & kBdCellNextMask;
  }
  cells[BdQuickTarget] = *value;
  if (BdQuickNext)
    cells[BdQuickTarget].link_height_kind = (cells[BdQuickTarget].link_height_kind & kBdCellWithoutNext) | BdQuickNext;
  u32 z = (value->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask;
  if (z > highest_z)
    highest_z = z;
  dirty_columns[tile->x][tile->y] = 1;
  changed = 1;
  return true;
}

// Reset the cell pool, bounds, selected column tables and undo cursors for the current dimensions.
// Preserve the final pool record and untouched render tables, then mark every in-bounds column dirty.
void BdGrid::ResetStorage() {
  bounds.origin = {0, 0, 0};
  bounds.size = {side, side, height};
  free_head = 1;
  cells[0].link_height_kind &= kBdCellWithoutNextOrHeight;
  for (u32 index = 1; index < kBdCellNextMask; ++index)
    cells[index].link_height_kind = (cells[index].link_height_kind & kBdCellWithoutNextOrHeight) | kBdCellFreeHeight |
        ((index + 1) & kBdCellNextMask);
  cells[0].link_height_kind &= kBdCellWithoutNextOrHeight;
  for (u32 x = 0; x < static_cast<u32>(side); ++x)
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      columns[x][y] = 0;
      aux_columns[x][y] = 0;
      slice_z[x][y] = 0;
    }
  MarkDirtyColumns(&bounds);
  highest_z = 1;
  cell_count = 1;
  hidden_face_count = 0;
  face_count = 0;
  aux_cell_count = 0;
  ResetChanges();
}

// Seed the missing height-zero cell in each column using the mode-specific ground template.
// Retain the original byte-sized iteration counters, material decrements and direct raw insertion order.
void BdGrid::InitializeGround() {
  BdCell ground;
  ground.link_height_kind &= kBdCellWithoutHeightOrKind;
  if (mode < 2)
    ground.link_height_kind |= kBdCellGroundKind;
  SetFirstMaterial(&ground, 0, nullptr);
  if (mode >= 2)
    for (u8 part = 1; part < 5; ++part)
      SetFirstMaterial(&ground, part, nullptr);
  ground.face_mask = 1;
  for (u8 x = 0; x < static_cast<u32>(side); ++x)
    for (u8 y = 0; y < static_cast<u32>(side); ++y) {
      u32 index = columns[x][y];
      if (!index || (cells[index].link_height_kind & kBdCellHeightMask)) {
        BdPoint3 tile = {x, y, 0};
        InsertRawCell(&tile, &ground);
      }
    }
}

// Raise imported cells, rebuild missing ground, and merge the first raised ground cell into its column base.
// Preserve the original direct packed material copy and delete the raised cell through the real grid method.
void BdGrid::NormalizeImportedGround() {
  BdPoint3 shift = {0, 0, -1};
  ShiftCells(&shift);
  InitializeGround();
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      u32 head = columns[x][y], index = head;
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if ((packed & kBdCellHeightMask) >= 0x20000)
          break;
        index = packed & kBdCellNextMask;
      }
      if ((cells[index].link_height_kind & kBdCellKindMask) == kBdCellGroundKind) {
        cells[head].parts[0] = cells[index].parts[0];
        BdPoint3 tile = {static_cast<i32>(x), static_cast<i32>(y), 1};
        DeleteCell(&tile);
      }
    }
  }
}

// Compare object-origin proximity using the original wrapped DWORD sum of squared coordinate differences.
// This shared arithmetic keeps the unsigned distance ordering used to resolve overlapping object cells.
static u32 BdOriginDistance(const BdPoint3 *tile, const BdPoint3 *origin) {
  u32 x = static_cast<u32>(tile->x) - static_cast<u32>(origin->x),
      y = static_cast<u32>(tile->y) - static_cast<u32>(origin->y),
      z = static_cast<u32>(tile->z) - static_cast<u32>(origin->z);
  return x * x + y * y + z * z;
}

// Insert every cell of an object volume, deriving its four projected view offsets from the shared origin.
// Clamp the inclusive endpoints to the grid and retain the original rotation-specific 32-bit wrap and byte truncation.
bool BdGrid::InsertObjectBox(
    const BdPoint3 *first, const BdPoint3 *second, const BdPoint3 *origin, const BdCell *value) {
  BdCell cell = *value;
  i32 x0 = first->x < second->x ? first->x : second->x, x1 = first->x > second->x ? first->x : second->x;
  i32 y0 = first->y < second->y ? first->y : second->y, y1 = first->y > second->y ? first->y : second->y;
  i32 z0 = first->z < second->z ? first->z : second->z, z1 = first->z > second->z ? first->z : second->z;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (z0 < 0)
    z0 = 0;
  if (x1 >= side)
    x1 = side - 1;
  if (y1 >= side)
    y1 = side - 1;
  if (z1 >= height)
    z1 = height - 1;
  if (x1 < x0 || y1 < y0 || z1 < z0)
    return false;
  for (i32 x = x0; x <= x1; ++x) {
    for (i32 y = y0; y <= y1; ++y) {
      u32 dx = static_cast<u32>(x) - static_cast<u32>(origin->x);
      u32 dy = static_cast<u32>(y) - static_cast<u32>(origin->y), horizontal[4];
      switch (rotation) {
      case 0:
        horizontal[0] = dy - dx;
        horizontal[1] = dx + dy;
        break;
      case 1:
        horizontal[0] = 0u - dx - dy;
        horizontal[1] = dy - dx;
        break;
      case 2:
        horizontal[0] = dx - dy;
        horizontal[1] = 0u - dx - dy;
        break;
      case 3:
        horizontal[0] = dx + dy;
        horizontal[1] = dx - dy;
        break;
      default:
        return false;
      }
      horizontal[2] = 0u - horizontal[0];
      horizontal[3] = 0u - horizontal[1];
      for (i32 z = z0; z <= z1; ++z) {
        u32 dz = 2u * (static_cast<u32>(origin->z) - static_cast<u32>(z)), vertical[4];
        switch (rotation) {
        case 0:
          vertical[0] = dz + dy + dx;
          vertical[1] = dz - dy + dx;
          vertical[2] = dz - dy - dx;
          vertical[3] = dz + dy - dx;
          break;
        case 1:
          vertical[0] = dz + dy - dx;
          vertical[1] = dz + dy + dx;
          vertical[2] = dz - dy + dx;
          vertical[3] = dz - dy - dx;
          break;
        case 2:
          vertical[0] = dz - dy - dx;
          vertical[1] = dz + dy - dx;
          vertical[2] = dz + dy + dx;
          vertical[3] = dz - dy + dx;
          break;
        default:
          vertical[0] = dz - dy + dx;
          vertical[1] = dz - dy - dx;
          vertical[2] = dz + dy - dx;
          vertical[3] = dz + dy + dx;
          break;
        }
        for (u32 view = 0; view < 4; ++view) {
          cell.parts[view + 1].offset_x = static_cast<i8>(static_cast<u8>(horizontal[view]));
          cell.parts[view + 1].offset_y = static_cast<i8>(static_cast<u8>(vertical[view]));
        }
        cell.link_height_kind = (cell.link_height_kind & kBdCellWithoutNextOrHeight) |
            ((static_cast<u32>(z) & kBdCellHeightValueMask) << kBdCellHeightShift);
        BdPoint3 tile = {x, y, z};
        InsertCell(&tile, &cell);
      }
    }
  }
  return true;
}

// Insert or merge a cell into its ordered column, resolving object origins and removing displaced object extents.
// Retain input rotation, exhausted-storage cleanup, undo ordering, dirty marking and the maximum packed height.
bool BdGrid::InsertCell(const BdPoint3 *tile, BdCell *value) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return false;
  if (!(cells[free_head].link_height_kind & kBdCellNextMask)) {
    RemoveHiddenCells();
    CompactCells();
    ReconcileMode();
    if (!(cells[free_head].link_height_kind & kBdCellNextMask))
      return false;
  }
  u32 kind = value->link_height_kind & kBdCellKindMask, distance = 0;
  bool object = kind == kBdCellObjectKind, origin_cell = false;
  if (!kind || object)
    value->link_height_kind =
        (value->link_height_kind & kBdCellWithoutRotation) | (static_cast<u32>(rotation) << kBdCellRotationShift);
  if (object) {
    origin_cell = BdIsObjectOrigin(value);
    BdPoint3 origin = {};
    GetObjectOrigin(tile, &origin, value);
    distance = BdOriginDistance(tile, &origin);
  }
  u32 index = columns[tile->x][tile->y], previous = 0, target, next;
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    previous = index;
    index = packed & kBdCellNextMask;
  }
  bool replace = false;
  if (index &&
      ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u32>(tile->z)) {
    BdCell *old = &cells[index];
    if (!memcmp(value, old, sizeof(BdCell)))
      return false;
    bool old_object = (old->link_height_kind & kBdCellKindMask) == kBdCellObjectKind;
    if (object) {
      if (!old_object && (old->link_height_kind & kBdCellKindMask) != kBdCellGroundKind)
        return false;
      if (old_object && !BdZeroViewBytes(old)) {
        BdPoint3 origin = {};
        GetObjectOrigin(tile, &origin, old);
        if (distance > BdOriginDistance(tile, &origin))
          return false;
      }
    }
    replace =
        !old_object || !BdIsObjectOrigin(old) || BdZeroViewBytes(old) || (BdEqualViewBytes(value, old) && origin_cell);
    if (!replace) {
      BdObjectInfo info;
      BdPoint3 origin = {};
      GetObjectOrigin(tile, &origin, old);
      GetObjectInfo(old, &info);
      BdPoint3 half = {info.size.x >> 1, info.size.y >> 1, info.size.z >> 1};
      BdPoint3 last = {static_cast<i32>(static_cast<u32>(origin.x) + half.x),
          static_cast<i32>(static_cast<u32>(origin.y) + half.y), static_cast<i32>(static_cast<u32>(origin.z) + half.z)};
      if (info.size.z && !(info.size.z & 1))
        half.z = static_cast<i32>(static_cast<u32>(half.z) - 1);
      BdPoint3 first = {static_cast<i32>(static_cast<u32>(origin.x) - half.x),
          static_cast<i32>(static_cast<u32>(origin.y) - half.y), static_cast<i32>(static_cast<u32>(origin.z) - half.z)};
      i32 origin_index = FindCell(&origin, 1, 1);
      if (origin_index) {
        BdCell match = cells[origin_index];
        DeleteObjectBox(&first, &last, &origin, &match);
      }
      previous = 0;
      index = columns[tile->x][tile->y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
          break;
        previous = index;
        index = packed & kBdCellNextMask;
      }
    }
  }
  if (replace) {
    target = index;
    if (PrepareChange()) {
      BdChange *change = &changes[change_count];
      change->before = 1;
      change->after = 2;
      change->tile = *tile;
      ++change_count;
      saved_cells[saved_cell_count] = cells[index];
      ++saved_cell_count;
    }
    next = cells[index].link_height_kind & kBdCellNextMask;
  } else {
    target = free_head;
    free_head = cells[target].link_height_kind & kBdCellNextMask;
    next = index;
    if (previous)
      cells[previous].link_height_kind =
          (cells[previous].link_height_kind & kBdCellWithoutNext) | (target & kBdCellNextMask);
    else
      columns[tile->x][tile->y] = target;
    ++cell_count;
  }
  BdCell *cell = &cells[target];
  *cell = *value;
  cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | next;
  PublishFaces(tile, cell);
  if (PrepareChange()) {
    BdChange *change = &changes[change_count];
    change->before = 2;
    change->after = 1;
    change->tile = *tile;
    ++change_count;
    saved_cells[saved_cell_count] = *value;
    ++saved_cell_count;
  }
  u32 z = (value->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask;
  if (z > highest_z)
    highest_z = z;
  dirty_columns[tile->x][tile->y] = 1;
  changed = 1;
  return true;
}

// Insert the mutable cell throughout an inclusive endpoint box in x/y/z order, normalizing each endpoint axis first.
// Update packed height before every call and return true even when individual insertions reject their coordinates.
bool BdGrid::InsertBox(const BdPoint3 *first, const BdPoint3 *second, BdCell *value) {
  BdPoint3 low = {first->x < second->x ? first->x : second->x, first->y < second->y ? first->y : second->y,
      first->z < second->z ? first->z : second->z};
  BdPoint3 high = {first->x > second->x ? first->x : second->x, first->y > second->y ? first->y : second->y,
      first->z > second->z ? first->z : second->z};
  for (i32 x = low.x; x <= high.x; x = static_cast<i32>(static_cast<u32>(x) + 1))
    for (i32 y = low.y; y <= high.y; y = static_cast<i32>(static_cast<u32>(y) + 1))
      for (i32 z = low.z; z <= high.z; z = static_cast<i32>(static_cast<u32>(z) + 1)) {
        BdPoint3 tile = {x, y, z};
        value->link_height_kind = (value->link_height_kind & kBdCellWithoutHeight) |
            ((static_cast<u32>(z) & kBdCellHeightValueMask) << kBdCellHeightShift);
        InsertCell(&tile, value);
      }
  return true;
}

// Apply the selected recorded edit without recording another change, then restore the saved-cell cursor.
// Rotation edits post their original messages after rotating; other codes retain the source's no-op behavior.
u32 BdGrid::ApplyChange(i8 operation) {
  changes_disabled = 1;
  i32 absolute = operation < 0 ? -static_cast<i32>(operation) : static_cast<i32>(operation);
  i8 code = static_cast<i8>(absolute);
  switch (code) {
  case 1:
    InsertCell(&changes[change_count].tile, &saved_cells[changes[change_count].saved_index]);
    break;
  case 2:
    DeleteCell(&changes[change_count].tile);
    break;
  case 3:
  case 4: {
    Rotate(code == 3 ? -128 : 128);
    ScMessage message = {
        code == 3 ? kWnMsg_ArchitectRotateSelectionNegative : kWnMsg_ArchitectRotateSelectionPositive, 0, 0, 0};
    BdGetMessageServer()->MessagePost(message, false);
    break;
  }
  case 7:
    ReplaceCell(&changes[change_count].tile, &saved_cells[changes[change_count].saved_index]);
    break;
  default:
    break;
  }
  u32 saved = changes[change_count].saved_index;
  changes_disabled = 0;
  saved_cell_count = saved;
  return saved;
}

// Undo the most recent change and every preceding record in its positive continuation group.
// Each record retains its own operation code and restores the saved-cell cursor after application.
bool BdGrid::Undo() {
  if (!change_count)
    return false;
  do {
    --change_count;
    ApplyChange(changes[change_count].before);
  } while (static_cast<i32>(change_count) > 0 && changes[change_count - 1].before > 0);
  return true;
}

// Redo the next available change and its positive continuation group using each record's after code.
bool BdGrid::Redo() {
  if (static_cast<i32>(change_count) > static_cast<i32>(last_change))
    return false;
  do {
    ApplyChange(changes[change_count].after);
    ++change_count;
  } while (static_cast<i32>(change_count) <= static_cast<i32>(last_change) &&
      changes[change_count - 1].before > 0);
  return true;
}

static i32 BdLineGet(const BdPoint3 *point, u32 axis) {
  return axis == 0 ? point->x : axis == 1 ? point->y : point->z;
}

// Store a raster coordinate through the actual named point member.
// Values supplied by the line methods are already sign-extended from their low words.
static void BdLineSet(BdPoint3 *point, u32 axis, i32 value) {
  if (axis == 0)
    point->x = value;
  else if (axis == 1)
    point->y = value;
  else
    point->z = value;
}

// Insert cells along a planar integer line, retaining signed low-word intermediates and the full-width final endpoint.
// A shared endpoint axis is the defined source domain; individual cell results do not change the final true result.
bool BdGrid::InsertLine(const BdPoint3 *first, const BdPoint3 *second, BdCell *value) {
  u32 mask = (first->x == second->x ? 1u : 0u) | (first->y == second->y ? 2u : 0u) | (first->z == second->z ? 4u : 0u);
  assert(mask);
  u32 primary_axis = mask & 1 ? (mask & 2 ? 2 : 1) : 0;
  u32 secondary_axis = mask & 1 ? (mask & 2 ? 0 : 2) : (mask & 2 ? 2 : 1);
  u32 primary = static_cast<u16>(BdLineGet(first, primary_axis));
  u32 secondary = static_cast<u16>(BdLineGet(first, secondary_axis));
  i32 delta_primary = static_cast<i16>(BdLineGet(second, primary_axis)) - static_cast<i16>(primary);
  i32 delta_secondary = static_cast<i16>(BdLineGet(second, secondary_axis)) - static_cast<i16>(secondary);
  i32 primary_step = delta_primary > 0 ? 1 : delta_primary < 0 ? -1 : 0;
  i32 secondary_step = delta_secondary > 0 ? 1 : delta_secondary < 0 ? -1 : 0;
  i32 major = delta_primary < 0 ? -delta_primary : delta_primary;
  i32 minor = delta_secondary < 0 ? -delta_secondary : delta_secondary;
  bool steep = static_cast<i16>(minor) > static_cast<i16>(major);
  if (steep) {
    i32 previous = static_cast<i16>(major);
    major = minor;
    minor = previous;
  }
  u32 twice_minor = static_cast<u32>(minor) * 2, error = twice_minor - static_cast<u32>(major);
  i32 remaining = static_cast<i16>(major);
  BdPoint3 start = {static_cast<i16>(first->x), static_cast<i16>(first->y), static_cast<i16>(first->z)};
  while (remaining > 0) {
    BdPoint3 point = start;
    BdLineSet(&point, primary_axis, static_cast<i16>(primary));
    BdLineSet(&point, secondary_axis, static_cast<i16>(secondary));
    value->link_height_kind = (value->link_height_kind & kBdCellWithoutHeight) |
        ((static_cast<u32>(point.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
    InsertCell(&point, value);
    while (static_cast<i16>(error) > 0) {
      if (steep)
        primary += static_cast<u32>(primary_step);
      else
        secondary += static_cast<u32>(secondary_step);
      error -= static_cast<u32>(major) * 2;
    }
    if (steep)
      secondary += static_cast<u32>(secondary_step);
    else
      primary += static_cast<u32>(primary_step);
    error += twice_minor;
    --remaining;
  }
  BdPoint3 point = *second;
  value->link_height_kind = (value->link_height_kind & kBdCellWithoutHeight) |
      ((static_cast<u32>(point.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
  InsertCell(&point, value);
  return true;
}

// Delete cells along a planar integer line, retaining signed low-word intermediates and the full-width final endpoint.
// A shared endpoint axis is the defined source domain; individual cell results do not change the final true result.
bool BdGrid::DeleteLine(const BdPoint3 *first, const BdPoint3 *second) {
  u32 mask = (first->x == second->x ? 1u : 0u) | (first->y == second->y ? 2u : 0u) | (first->z == second->z ? 4u : 0u);
  assert(mask);
  u32 primary_axis = mask & 1 ? (mask & 2 ? 2 : 1) : 0;
  u32 secondary_axis = mask & 1 ? (mask & 2 ? 0 : 2) : (mask & 2 ? 2 : 1);
  u32 primary = static_cast<u16>(BdLineGet(first, primary_axis));
  u32 secondary = static_cast<u16>(BdLineGet(first, secondary_axis));
  i32 delta_primary = static_cast<i16>(BdLineGet(second, primary_axis)) - static_cast<i16>(primary);
  i32 delta_secondary = static_cast<i16>(BdLineGet(second, secondary_axis)) - static_cast<i16>(secondary);
  i32 primary_step = delta_primary > 0 ? 1 : delta_primary < 0 ? -1 : 0;
  i32 secondary_step = delta_secondary > 0 ? 1 : delta_secondary < 0 ? -1 : 0;
  i32 major = delta_primary < 0 ? -delta_primary : delta_primary;
  i32 minor = delta_secondary < 0 ? -delta_secondary : delta_secondary;
  bool steep = static_cast<i16>(minor) > static_cast<i16>(major);
  if (steep) {
    i32 previous = static_cast<i16>(major);
    major = minor;
    minor = previous;
  }
  u32 twice_minor = static_cast<u32>(minor) * 2, error = twice_minor - static_cast<u32>(major);
  i32 remaining = static_cast<i16>(major);
  BdPoint3 start = {static_cast<i16>(first->x), static_cast<i16>(first->y), static_cast<i16>(first->z)};
  while (remaining > 0) {
    BdPoint3 point = start;
    BdLineSet(&point, primary_axis, static_cast<i16>(primary));
    BdLineSet(&point, secondary_axis, static_cast<i16>(secondary));
    DeleteCell(&point);
    while (static_cast<i16>(error) > 0) {
      if (steep)
        primary += static_cast<u32>(primary_step);
      else
        secondary += static_cast<u32>(secondary_step);
      error -= static_cast<u32>(major) * 2;
    }
    if (steep)
      secondary += static_cast<u32>(secondary_step);
    else
      primary += static_cast<u32>(primary_step);
    error += twice_minor;
    --remaining;
  }
  BdPoint3 point = *second;
  DeleteCell(&point);
  return true;
}

// Replace all256 usage totals afterward, allowing the material table to clear values whose new count is zero.
void BdGrid::RecountMaterials() {
  u32 uses[256] = {};
  i32 end_x = bounds.origin.x + bounds.size.x, end_y = bounds.origin.y + bounds.size.y;
  for (i32 x = bounds.origin.x; x < end_x; ++x) {
    for (i32 y = bounds.origin.y; y < end_y; ++y) {
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask) {
        BdCell *cell = &cells[index];
        for (u32 i = 0; i < 6; ++i) {
          ++uses[cell->parts[i].first];
          ++uses[cell->parts[i].second];
        }
      }
    }
  }
  for (u32 i = 0; i < 256; ++i)
    materials->SetUse(i, uses[i]);
}

void BdGrid::SetFirstMaterial(BdCell *cell, u8 part, BdAsset *value) {
  u32 index = 0;
  if (value) {
    materials->Find(value, &index);
    if (index) {
      cell->parts[part].first = static_cast<u8>(index);
      materials->IncrementUse(index);
    }
  } else {
    index = cell->parts[part].first;
    cell->parts[part].first = 0;
    materials->DecrementUse(index);
  }
}

void BdGrid::SetFirstMaterialOffset(BdCell *cell, u8 part, BdAsset *value, i8 offset) {
  u32 index = 0;
  if (value) {
    materials->Find(value, &index);
    if (index) {
      cell->parts[part].first = static_cast<u8>(index);
      cell->parts[part].offset_x = offset;
      materials->IncrementUse(index);
    }
  } else {
    index = cell->parts[part].first;
    cell->parts[part].first = 0;
    cell->parts[part].offset_x = 0;
    materials->DecrementUse(index);
  }
}

void BdGrid::SetSecondMaterial(BdCell *cell, u8 part, BdAsset *value) {
  u32 index = 0;
  if (value) {
    materials->Find(value, &index);
    if (index) {
      cell->parts[part].second = static_cast<u8>(index);
      materials->IncrementUse(index);
    }
  } else {
    index = cell->parts[part].second;
    cell->parts[part].second = 0;
    materials->DecrementUse(index);
  }
}

// Assign the second material byte and its paired offset, preserving all other cell fields.
void BdGrid::SetSecondMaterialOffset(BdCell *cell, u8 part, BdAsset *value, i8 offset) {
  u32 index = 0;
  if (value) {
    materials->Find(value, &index);
    if (index) {
      cell->parts[part].second = static_cast<u8>(index);
      cell->parts[part].offset_y = offset;
      materials->IncrementUse(index);
    }
  } else {
    index = cell->parts[part].second;
    cell->parts[part].second = 0;
    cell->parts[part].offset_y = 0;
    materials->DecrementUse(index);
  }
}

void BdGrid::ClearMaterials() {
  for (i32 x = bounds.origin.x; x < static_cast<i32>(static_cast<u32>(bounds.origin.x) + bounds.size.x); ++x)
    for (i32 y = bounds.origin.y; y < static_cast<i32>(static_cast<u32>(bounds.origin.y) + bounds.size.y); ++y)
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask)
        for (u8 part = 0; part < 6; ++part) {
          SetFirstMaterialOffset(&cells[index], part, nullptr, 0);
          SetSecondMaterialOffset(&cells[index], part, nullptr, 0);
        }
}

// Retain the grid's existing material table before storing its pointer through the original output argument.
BdMaterials *BdGrid::GetMaterials(BdMaterials **value) {
  materials->AddRef();
  BdMaterials *retained = materials;
  *value = retained;
  return retained;
}

// Select the object cell's material byte for its stored rotation and resolve that indexed asset's metadata.
// A non-object cell follows the original null-asset builder path and leaves the descriptor unchanged.
bool BdGrid::GetObjectInfo(const BdCell *cell, BdObjectInfo *info) {
  if ((cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind)
    return GetMaterialInfo(cell->parts[(cell->link_height_kind >> kBdCellRotationShift) + 1].first, info);
  return BdFillObjectInfo(nullptr, info);
}

// Resolve the indexed material pointer before asking the actual catalog-backed builder to populate the descriptor.
// Report successful material resolution even when the builder rejects its definition, matching the original byte result.
bool BdGrid::GetMaterialInfo(u8 index, BdObjectInfo *info) {
  BdAsset *asset = nullptr;
  if (!materials->Get(index, &asset))
    return false;
  BdFillObjectInfo(asset, info);
  return true;
}

// Find the greatest height in all linked columns, then apply the selected view's height limit.
// Other view modes leave the height unrestricted.
u32 BdGrid::GetLimitedHeight() {
  u32 highest = 0;
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask) {
        u32 value = (cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask;
        if (value > highest)
          highest = value;
      }
    }
  }
  static const u8 limits[] = {28, 64, 108, 144, 80};
  if (view_mode >= 1 && view_mode <= 5 && highest > limits[view_mode - 1])
    return limits[view_mode - 1];
  return highest;
}

i32 BdGrid::CountUsedMaterials() {
  i32 count = 0;
  for (u32 index = 0; index < 256; ++index) {
    u32 uses = 0;
    materials->GetUse(index, &uses);
    if (uses)
      ++count;
  }
  return count;
}

i32 BdGrid::CountSpecialMaterials() {
  i32 count = 0;
  for (u32 index = 0; index < 256; ++index) {
    BdAsset *asset = nullptr;
    if (materials->Get(index, &asset) && asset->IsKind6Or7Or16())
      ++count;
  }
  return count;
}

// Count every linked cell in the grid's column table.
i32 BdGrid::CountCells() {
  i32 count = 0;
  for (u32 x = 0; x < static_cast<u32>(side); ++x)
    for (u32 y = 0; y < static_cast<u32>(side); ++y)
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask)
        ++count;
  return count;
}

// Unlink a cell at the requested height, record its original removal payload, and restore neighboring face visibility.
// Reset the released record before returning it to the free list, preserving the original counter and undo ordering.
bool BdGrid::RemoveCell(const BdPoint3 *tile) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return false;
  u32 index = columns[tile->x][tile->y], previous = 0;
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    previous = index;
    index = packed & kBdCellNextMask;
  }
  if (!index)
    return false;
  BdCell *cell = &cells[index];
  if (((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) != static_cast<u32>(tile->z))
    return false;
  cell->face_mask = 0;
  u32 next = cell->link_height_kind & kBdCellNextMask;
  if (previous)
    cells[previous].link_height_kind = (cells[previous].link_height_kind & kBdCellWithoutNext) | next;
  else
    columns[tile->x][tile->y] = next;
  cell->link_height_kind &= kBdCellWithoutNext;
  if (PrepareChange()) {
    BdChange *change = &changes[change_count];
    change->before = 1;
    change->after = 2;
    change->tile = *tile;
    ++change_count;
    saved_cells[saved_cell_count] = *cell;
    ++saved_cell_count;
  }
  RemoveFaces(tile, cell);
  cell->Reset();
  cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | (free_head & kBdCellNextMask);
  cell->link_height_kind |= kBdCellFreeHeight;
  --cell_count;
  free_head = index;
  changed = 1;
  return true;
}

// Delete a cell with its visibility mask retained in the undo payload, then restore neighboring faces and recycle it.
// Mark the affected column dirty after updating the free-list head, preserving the normal deletion path's write order.
bool BdGrid::DeleteCell(const BdPoint3 *tile) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return false;
  u32 index = columns[tile->x][tile->y], previous = 0;
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    previous = index;
    index = packed & kBdCellNextMask;
  }
  if (!index)
    return false;
  BdCell *cell = &cells[index];
  if (((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) != static_cast<u32>(tile->z))
    return false;
  u32 next = cell->link_height_kind & kBdCellNextMask;
  if (previous)
    cells[previous].link_height_kind = (cells[previous].link_height_kind & kBdCellWithoutNext) | next;
  else
    columns[tile->x][tile->y] = next;
  cell->link_height_kind &= kBdCellWithoutNext;
  if (PrepareChange()) {
    BdChange *change = &changes[change_count];
    change->before = 1;
    change->after = 2;
    change->tile = *tile;
    ++change_count;
    saved_cells[saved_cell_count] = *cell;
    ++saved_cell_count;
  }
  RemoveFaces(tile, cell);
  cell->Reset();
  cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | (free_head & kBdCellNextMask);
  cell->link_height_kind |= kBdCellFreeHeight;
  --cell_count;
  free_head = index;
  dirty_columns[tile->x][tile->y] = 1;
  changed = 1;
  return true;
}

// Delete only object cells satisfying optional projected-view identity and origin filters.
// Preserve the ordinary deletion undo, neighbor visibility, free-list, and dirty-column ordering.
bool BdGrid::DeleteObjectCell(const BdPoint3 *tile, const BdCell *match, const BdPoint3 *origin) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0)
    return false;
  u32 index = columns[tile->x][tile->y], previous = 0;
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    previous = index;
    index = packed & kBdCellNextMask;
  }
  if (!index)
    return false;
  BdCell *cell = &cells[index];
  if (((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) != static_cast<u32>(tile->z))
    return false;
  if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind || (match && !BdEqualViewBytes(cell, match)))
    return false;
  if (origin && !MatchesObjectOrigin(tile, origin, cell))
    return false;
  u32 next = cell->link_height_kind & kBdCellNextMask;
  if (previous)
    cells[previous].link_height_kind = (cells[previous].link_height_kind & kBdCellWithoutNext) | next;
  else
    columns[tile->x][tile->y] = next;
  cell->link_height_kind &= kBdCellWithoutNext;
  if (PrepareChange()) {
    BdChange *change = &changes[change_count];
    change->before = 1;
    change->after = 2;
    change->tile = *tile;
    ++change_count;
    saved_cells[saved_cell_count] = *cell;
    ++saved_cell_count;
  }
  RemoveFaces(tile, cell);
  cell->Reset();
  cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | (free_head & kBdCellNextMask);
  cell->link_height_kind |= kBdCellFreeHeight;
  --cell_count;
  free_head = index;
  dirty_columns[tile->x][tile->y] = 1;
  changed = 1;
  return true;
}

// Clamp an inclusive endpoint box to the grid and delete matching object cells in x/y/z order.
// Copy the origin before traversal and report whether the clipped box is valid, even when no cell matches.
bool BdGrid::DeleteObjectBox(
    const BdPoint3 *first, const BdPoint3 *second, const BdPoint3 *origin, const BdCell *match) {
  BdPoint3 saved_origin = *origin;
  i32 low_x = first->x < second->x ? first->x : second->x;
  i32 high_x = first->x > second->x ? first->x : second->x;
  i32 low_y = first->y < second->y ? first->y : second->y;
  i32 high_y = first->y > second->y ? first->y : second->y;
  i32 low_z = first->z < second->z ? first->z : second->z;
  i32 high_z = first->z > second->z ? first->z : second->z;
  if (low_x < 0)
    low_x = 0;
  if (high_x >= side)
    high_x = side - 1;
  if (low_y < 0)
    low_y = 0;
  if (high_y >= side)
    high_y = side - 1;
  if (low_z < 0)
    low_z = 0;
  if (high_z >= height)
    high_z = height - 1;
  if (high_x < low_x || high_y < low_y || high_z < low_z)
    return false;
  for (i32 x = low_x; x <= high_x; ++x) {
    for (i32 y = low_y; y <= high_y; ++y) {
      for (i32 z = low_z; z <= high_z; ++z) {
        BdPoint3 tile = {x, y, z};
        DeleteObjectCell(&tile, match, &saved_origin);
      }
    }
  }
  return true;
}

// Delete cells throughout the normalized inclusive endpoint box without clipping the original iteration bounds.
// Each cell deletion performs its own game checks; the box method always returns true after traversal.
bool BdGrid::DeleteBox(const BdPoint3 *first, const BdPoint3 *second) {
  BdPoint3 low = {first->x < second->x ? first->x : second->x, first->y < second->y ? first->y : second->y,
      first->z < second->z ? first->z : second->z};
  BdPoint3 high = {first->x > second->x ? first->x : second->x, first->y > second->y ? first->y : second->y,
      first->z > second->z ? first->z : second->z};
  for (i32 x = low.x; x <= high.x; x = static_cast<i32>(static_cast<u32>(x) + 1))
    for (i32 y = low.y; y <= high.y; y = static_cast<i32>(static_cast<u32>(y) + 1))
      for (i32 z = low.z; z <= high.z; z = static_cast<i32>(static_cast<u32>(z) + 1)) {
        BdPoint3 tile = {x, y, z};
        DeleteCell(&tile);
      }
  return true;
}

// Build the clipped column starts and the cursor's horizontal and vertical cell slices.
// Initial column thresholds retain all 32 bits; cell-zero height reads and dirty clearing follow the original order.
const BdVolume *BdGrid::BuildRenderSlices(const BdVolume *volume, const BdPoint3 *cursor) {
  for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x) {
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y) {
      u32 index = columns[x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(volume->origin.z))
          break;
        index = packed & kBdCellNextMask;
      }
      render_columns[x][y] = index;
      index = render_columns[x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(cursor->z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_z[x][y] = ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) ==
              static_cast<u16>(cursor->z)
          ? index
          : 0;
    }
  }
  for (i32 z = volume->origin.z; z < volume->origin.z + volume->size.z; ++z) {
    for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x) {
      u32 index = render_columns[x][cursor->y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_y[x][z] =
          ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u16>(z)
          ? index
          : 0;
    }
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y) {
      u32 index = render_columns[cursor->x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_x[y][z] =
          ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u16>(z)
          ? index
          : 0;
    }
  }
  for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x)
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y)
      dirty_columns[x][y] = 0;
  return volume;
}

// Refresh the clipped column starts and the cursor's horizontal and vertical cell slices.
// The tick path narrows initial height thresholds to16 bits. Preserve cell-zero reads and dirty clearing order.
const BdVolume *BdGrid::RefreshRenderSlices(const BdVolume *volume, const BdPoint3 *cursor) {
  for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x) {
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y) {
      u32 index = columns[x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(volume->origin.z))
          break;
        index = packed & kBdCellNextMask;
      }
      render_columns[x][y] = index;
      index = render_columns[x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(cursor->z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_z[x][y] = ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) ==
              static_cast<u16>(cursor->z)
          ? index
          : 0;
    }
  }
  for (i32 z = volume->origin.z; z < volume->origin.z + volume->size.z; ++z) {
    for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x) {
      u32 index = render_columns[x][cursor->y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_y[x][z] =
          ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u16>(z)
          ? index
          : 0;
    }
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y) {
      u32 index = render_columns[cursor->x][y];
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u16>(z))
          break;
        index = packed & kBdCellNextMask;
      }
      slice_x[y][z] =
          ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u16>(z)
          ? index
          : 0;
    }
  }
  for (i32 x = volume->origin.x; x < volume->origin.x + volume->size.x; ++x)
    for (i32 y = volume->origin.y; y < volume->origin.y + volume->size.y; ++y)
      dirty_columns[x][y] = 0;
  return volume;
}

// Replace a cell for undo replay or insert it into an ordered column when that height is absent.
// Retain the original free-list guard, mutable input rotation, old/new undo payloads, and face-publication ordering.
bool BdGrid::ReplaceCell(const BdPoint3 *tile, BdCell *value) {
  if (tile->z >= height || tile->x >= side || tile->y >= side || tile->z < 0 || tile->x < 0 || tile->y < 0 ||
      !(cells[free_head].link_height_kind & kBdCellNextMask))
    return false;
  u32 kind = value->link_height_kind & kBdCellKindMask;
  if (kind == 0 || kind == kBdCellObjectKind)
    value->link_height_kind =
        (value->link_height_kind & kBdCellWithoutRotation) | (static_cast<u32>(rotation) << kBdCellRotationShift);
  u32 index = columns[tile->x][tile->y], previous = 0, target, next;
  while (index) {
    u32 packed = cells[index].link_height_kind;
    if (((packed >> kBdCellHeightShift) & kBdCellHeightValueMask) >= static_cast<u32>(tile->z))
      break;
    previous = index;
    index = packed & kBdCellNextMask;
  }
  if (index &&
      ((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == static_cast<u32>(tile->z)) {
    if (!memcmp(value, &cells[index], sizeof(BdCell)))
      return false;
    target = index;
    if (PrepareChange()) {
      BdChange *change = &changes[change_count];
      change->before = 1;
      change->after = 2;
      change->tile = *tile;
      ++change_count;
      saved_cells[saved_cell_count] = cells[index];
      ++saved_cell_count;
    }
    next = cells[index].link_height_kind & kBdCellNextMask;
  } else {
    target = free_head;
    free_head = cells[target].link_height_kind & kBdCellNextMask;
    if (previous)
      cells[previous].link_height_kind =
          (cells[previous].link_height_kind & kBdCellWithoutNext) | (target & kBdCellNextMask);
    else
      columns[tile->x][tile->y] = target;
    ++cell_count;
    next = index;
  }
  BdCell *cell = &cells[target];
  *cell = *value;
  if (next)
    cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | (next & kBdCellNextMask);
  PublishFaces(tile, cell);
  if (PrepareChange()) {
    BdChange *change = &changes[change_count];
    change->before = 2;
    change->after = 1;
    change->tile = *tile;
    ++change_count;
    saved_cells[saved_cell_count] = *value;
    ++saved_cell_count;
  }
  changed = 1;
  return true;
}

static i32 BdStorageAdd(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) + static_cast<u32>(second));
}

static i32 BdStorageSubtract(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) - static_cast<u32>(second));
}

// Shift column pointers within the grid bounds in overlap-safe order, then adjust every remaining packed cell height.
void BdGrid::ShiftCells(const BdPoint3 *delta) {
  i32 dx = delta->x, dy = delta->y, dz = delta->z;
  if (dx) {
    i32 direction = dx > 0 ? 1 : -1;
    i32 x = dx > 0 ? bounds.origin.x : BdStorageSubtract(BdStorageAdd(bounds.origin.x, bounds.size.x), 1);
    u32 magnitude = dx < 0 ? 0u - static_cast<u32>(dx) : static_cast<u32>(dx);
    i32 remaining = BdStorageSubtract(bounds.size.x, static_cast<i32>(magnitude));
    while (remaining > 0) {
      i32 source = BdStorageAdd(x, dx);
      for (i32 y = bounds.origin.y; y < BdStorageAdd(bounds.origin.y, bounds.size.y); ++y) {
        columns[x][y] = columns[source][y];
        columns[source][y] = 0;
      }
      x = BdStorageAdd(x, direction);
      --remaining;
    }
  }
  if (dy) {
    i32 direction = dy > 0 ? 1 : -1;
    i32 y = dy > 0 ? bounds.origin.y : BdStorageSubtract(BdStorageAdd(bounds.origin.y, bounds.size.y), 1);
    u32 magnitude = dy < 0 ? 0u - static_cast<u32>(dy) : static_cast<u32>(dy);
    i32 remaining = BdStorageSubtract(bounds.size.y, static_cast<i32>(magnitude));
    while (remaining > 0) {
      i32 source = BdStorageAdd(y, dy);
      for (i32 x = bounds.origin.x; x < BdStorageAdd(bounds.origin.x, bounds.size.x); ++x) {
        columns[x][y] = columns[x][source];
        columns[x][source] = 0;
      }
      y = BdStorageAdd(y, direction);
      --remaining;
    }
  }
  if (dz) {
    u32 height_delta = static_cast<u32>(dz) << kBdCellHeightShift;
    for (i32 x = bounds.origin.x; x < BdStorageAdd(bounds.origin.x, bounds.size.x); ++x) {
      for (i32 y = bounds.origin.y; y < BdStorageAdd(bounds.origin.y, bounds.size.y); ++y) {
        for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask) {
          u32 packed = cells[index].link_height_kind;
          u32 shifted = (packed & kBdCellWithoutNext) - height_delta;
          cells[index].link_height_kind = (packed & ~kBdCellHeightMask) | (shifted & kBdCellHeightMask);
        }
      }
    }
  }
}

// Return auxiliary-grid records to the main free list over the auxiliary volume's x/y bounds.
// Preserve their payloads and auxiliary column indices; only packed free links/heights and the count change.
void BdGrid::FreeAuxCells() {
  if (!aux_cell_count)
    return;
  for (i32 x = aux_bounds.origin.x; x < aux_bounds.origin.x + aux_bounds.size.x; ++x) {
    for (i32 y = aux_bounds.origin.y; y < aux_bounds.origin.y + aux_bounds.size.y; ++y) {
      u32 index = aux_columns[x][y];
      if (index) {
        BdCell *cell = &cells[index];
        cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutNext) | (free_head & kBdCellNextMask);
        cell->link_height_kind |= kBdCellFreeHeight;
        --aux_cell_count;
        free_head = index;
      }
    }
  }
}

// Replace the saved clipboard with cells at the selection's first Z level from the current Z slice.
// Copy through the real free-cell chain and clear the copied cells' links while retaining their other fields.
const BdVolume *BdGrid::CopySlice(const BdVolume *volume) {
  FreeAuxCells();
  aux_bounds = *volume;
  for (i32 x = volume->origin.x; x < BdStorageAdd(volume->origin.x, volume->size.x); ++x) {
    for (i32 y = volume->origin.y; y < BdStorageAdd(volume->origin.y, volume->size.y); ++y) {
      u32 index = slice_z[x][y];
      if (index &&
          static_cast<i32>((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) ==
              volume->origin.z) {
        u32 destination = free_head;
        aux_columns[x][y] = destination;
        free_head = cells[free_head].link_height_kind & kBdCellNextMask;
        cells[destination] = cells[index];
        cells[destination].link_height_kind &= kBdCellWithoutNext;
        ++aux_cell_count;
      } else {
        aux_columns[x][y] = 0;
      }
    }
  }
  return volume;
}

// Save the selected Z slice first, then delete each matching live cell through the normal grid operation.
void BdGrid::CutSlice(const BdVolume *volume) {
  CopySlice(volume);
  BdPoint3 tile = {volume->origin.x, volume->origin.y, volume->origin.z};
  for (; tile.x < BdStorageAdd(volume->origin.x, volume->size.x); ++tile.x) {
    for (tile.y = volume->origin.y; tile.y < BdStorageAdd(volume->origin.y, volume->size.y); ++tile.y) {
      u32 index = slice_z[tile.x][tile.y];
      if (index &&
          static_cast<i32>((cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == tile.z)
        DeleteCell(&tile);
    }
  }
}

// Translate the saved slice into the requested selection and insert each contained saved cell at its new height.
// Update the clipboard cell's packed Z before passing that same mutable cell to the actual insertion operation.
void BdGrid::PasteSlice(const BdVolume *volume) {
  if (!aux_cell_count)
    return;
  BdPoint3 tile = {0, 0, volume->origin.z};
  for (i32 x = aux_bounds.origin.x; x < BdStorageAdd(aux_bounds.origin.x, aux_bounds.size.x); ++x) {
    tile.x = BdStorageAdd(x, BdStorageSubtract(volume->origin.x, aux_bounds.origin.x));
    for (i32 y = aux_bounds.origin.y; y < BdStorageAdd(aux_bounds.origin.y, aux_bounds.size.y); ++y) {
      tile.y = BdStorageSubtract(BdStorageAdd(y, volume->origin.y), aux_bounds.origin.y);
      if (BdContains(&tile, volume)) {
        u32 index = aux_columns[x][y];
        if (index) {
          BdCell *cell = &cells[index];
          cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutHeight) |
              ((static_cast<u32>(tile.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
          InsertCell(&tile, cell);
        }
      }
    }
  }
}

// Pack live column records into indices1..cell_count-1 and rebuild the remaining free chain.
// This complete effect candidate preserves the original in-place permutation and auxiliary-record release order.
BdCell *BdGrid::CompactCells() {
  BdCell temporary;
  FreeAuxCells();
  u32 hole = 0;
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      u32 previous = 0;
      for (u32 index = columns[x][y]; index; index = cells[index].link_height_kind & kBdCellNextMask) {
        if (index >= cell_count) {
          while ((cells[hole].link_height_kind & kBdCellHeightMask) != kBdCellFreeHeight)
            ++hole;
          cells[hole] = cells[index];
          index = hole;
          if (previous)
            cells[previous].link_height_kind =
                (cells[previous].link_height_kind & kBdCellWithoutNext) | (hole & kBdCellNextMask);
          else
            columns[x][y] = hole;
        }
        previous = index;
      }
    }
  }
  for (u32 index = cell_count; index < 0x20000; ++index) {
    cells[index].link_height_kind =
        (cells[index].link_height_kind & kBdCellWithoutNext) | ((index + 1) & kBdCellNextMask);
    cells[index].link_height_kind |= kBdCellFreeHeight;
  }
  cells[kBdCellNextMask].link_height_kind &= kBdCellWithoutNext;
  free_head = cell_count;
  u32 next = 1;
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      u32 index = columns[x][y];
      if (index) {
        columns[x][y] = next;
        u32 successor = next + 1;
        do {
          u32 old_next = cells[index].link_height_kind & kBdCellNextMask;
          cells[index].link_height_kind =
              (cells[index].link_height_kind & kBdCellWithoutNext) | (successor & kBdCellNextMask);
          ++next;
          ++successor;
          index = old_next;
        } while (index);
      }
    }
  }
  for (u32 index = 1; index < cell_count; ++index) {
    while ((cells[index].link_height_kind & kBdCellNextMask) != index + 1) {
      u32 destination = (cells[index].link_height_kind & kBdCellNextMask) - 1;
      temporary = cells[destination];
      cells[destination] = cells[index];
      cells[index] = temporary;
    }
  }
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      u32 index = columns[x][y];
      if (index > 1)
        cells[index - 1].link_height_kind &= kBdCellWithoutNext;
    }
  }
  cells[cell_count - 1].link_height_kind &= kBdCellWithoutNext;
  return &cells[cell_count - 1];
}

// Convert a simple grid to mode2 when higher cell kinds require it and adjust its ground-level records.
u8 BdGrid::ReconcileMode() {
  bool needs_mode = mode >= 2, corner_ground = false;
  for (u32 x = 0; x < static_cast<u32>(side); ++x) {
    for (u32 y = 0; y < static_cast<u32>(side); ++y) {
      u32 index = columns[x][y];
      if (index) {
        u32 packed = cells[index].link_height_kind;
        if (!(packed & kBdCellHeightMask) && (x == 0 || x == static_cast<u32>(side) - 1) &&
            (y == 0 || y == static_cast<u32>(side) - 1) && (packed & kBdCellKindMask) == kBdCellGroundKind)
          corner_ground = true;
      }
      while (index) {
        u32 packed = cells[index].link_height_kind;
        if ((packed & kBdCellKindMask) > kBdCellGroundKind)
          needs_mode = true;
        index = packed & kBdCellNextMask;
      }
    }
  }
  if (needs_mode && mode < 2) {
    mode_bytes[0] = 0;
    mode_bytes[2] = 0;
    mode = 2;
    mode_bytes[1] = 28;
    if (corner_ground) {
      for (u8 x = 0; x < static_cast<u32>(side); ++x) {
        for (u8 y = 0; y < static_cast<u32>(side); ++y) {
          u32 index = columns[x][y];
          BdCell *cell = &cells[index];
          if (index) {
            u32 packed = cell->link_height_kind;
            if ((packed & kBdCellKindMask) == kBdCellGroundKind && !(packed & kBdCellHeightMask)) {
              cell->link_height_kind = packed & kBdCellWithoutKind;
              index = packed & kBdCellNextMask;
              cell = &cells[index];
            }
            if (index) {
              u32 next;
              do {
                u32 z = ((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) + 1;
                if (z >= kBdCellHeightValueMask) {
                  next = cell->link_height_kind & kBdCellNextMask;
                  BdPoint3 tile = {x, y, static_cast<i32>(z - 1)};
                  DeleteCell(&tile);
                } else {
                  cell->link_height_kind = (cell->link_height_kind & kBdCellWithoutHeight) |
                      ((z & kBdCellHeightValueMask) << kBdCellHeightShift);
                  next = cell->link_height_kind & kBdCellNextMask;
                  cell = &cells[next];
                }
              } while (next);
            }
          }
        }
      }
    }
  }
  return 1;
}

// Turn every cell in one linked column by the supplied number of quarter turns.
// Preserve the packed lower30 bits and follow the original low17-bit successor chain.
void BdGrid::RotateColumn(u32 index, i16 amount) {
  while (index) {
    u32 packed = cells[index].link_height_kind;
    cells[index].link_height_kind = (packed & kBdCellWithoutRotation) ^
        ((static_cast<u32>(amount) << kBdCellRotationShift) + (packed & kBdCellRotationMask));
    index = packed & kBdCellNextMask;
  }
}

// Rotate the original four column groups and each linked cell, recording the inverse undo pair when enabled.
// Odd dimensions retain the source's untouched middle row and column.
void BdGrid::Rotate(i32 direction) {
  i16 amount = direction > 0 ? 1 : 3;
  rotation = static_cast<u8>((rotation + amount) & 3);
  i32 width = bounds.size.x, half = width / 2;
  for (i32 x = 0; x < half; ++x)
    for (i32 y = 0; y < half; ++y) {
      i32 far_x = width - 1 - x, far_y = width - 1 - y;
      if (direction > 0) {
        u32 saved = columns[x][y];
        columns[x][y] = columns[y][far_x];
        RotateColumn(columns[x][y], amount);
        columns[y][far_x] = columns[far_x][far_y];
        RotateColumn(columns[y][far_x], amount);
        columns[far_x][far_y] = columns[far_y][x];
        RotateColumn(columns[far_x][far_y], amount);
        columns[far_y][x] = saved;
        RotateColumn(columns[far_y][x], amount);
      } else {
        u32 saved = columns[far_y][x];
        columns[far_y][x] = columns[far_x][far_y];
        RotateColumn(columns[far_y][x], amount);
        columns[far_x][far_y] = columns[y][far_x];
        RotateColumn(columns[far_x][far_y], amount);
        columns[y][far_x] = columns[x][y];
        RotateColumn(columns[y][far_x], amount);
        columns[x][y] = saved;
        RotateColumn(columns[x][y], amount);
      }
    }
  if (PrepareChange()) {
    changes[change_count].before = direction > 0 ? 3 : 4;
    changes[change_count].after = direction > 0 ? 4 : 3;
    ++change_count;
  }
}
