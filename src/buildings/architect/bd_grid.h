#ifndef SC3_BUILDINGS_ARCHITECT_BD_GRID_H
#define SC3_BUILDINGS_ARCHITECT_BD_GRID_H

#include "buildings/architect/bd_geometry.h"
#include "common/sc3_string.h"

// Six four-byte payload slots serve block faces; object cells use the four middle slots as projected views.
// The signed high bytes of those views participate in the object's tile-origin transform.
struct BdCellPart {
  u8 first;
  i8 offset_x;
  u8 second;
  i8 offset_y;
};

// The 76-byte legacy resource record retains unexplored payload bytes between its face mask and next-cell index.
struct BdLegacyCell {
  u32 kind;
  u8 rotation, field_005;
  u16 height;
  u32 face_mask;
  u8 field_00c[60];
  u16 next;
  u8 field_04a[2];
};
static_assert(sizeof(BdLegacyCell) == 76 && offsetof(BdLegacyCell, next) == 72);

// Bit fields of the linked-column cell word, as packed by BdCell::LoadLegacy.
enum BdCellMask : u32 {
  kBdCellKindValueMask = 15,
  kBdCellRotationValueMask = 3,
  kBdCellNextMask = 0x1ffff,
  kBdCellHeightValueMask = 0x1ff,
  kBdCellHeightMask = 0x03fe0000,
  kBdCellKindMask = 0x3c000000,
  kBdCellWithoutNext = 0xfffe0000,
  kBdCellWithoutHeight = 0xfc01ffff,
  kBdCellWithoutKind = 0xc3ffffff,
  kBdCellRotationMask = 0xc0000000,
  kBdCellWithoutRotation = 0x3fffffff,
  kBdCellWithoutHeightOrKind = 0xc001ffff,
  kBdCellWithoutNextOrHeight = 0xfc000000,
};

// Object cells occupy kind fifteen; unused records carry the reserved all-ones height.
enum BdCellPackedValue : u32 {
  kBdCellGroundKind = 0x20000000,
  kBdCellObjectKind = 0x3c000000,
  kBdCellFreeHeight = 0x03fe0000,
};

// Positions of the cell height, shape kind and quarter-turn rotation within the linked-column word.
enum BdCellShift : u32 {
  kBdCellHeightShift = 17,
  kBdCellKindShift = 26,
  kBdCellRotationShift = 30,
};

struct BdCell {
  BdCell();
  void Reset();
  // Preserve the observed packed word; the original private nominal return declaration remains unresolved.
  u32 LoadLegacy(const BdLegacyCell *source);

  u32 link_height_kind, face_mask;
  BdCellPart parts[6];
};

bool BD_STDCALL BdIsObjectOrigin(const BdCell *cell);
bool BD_STDCALL BdZeroViewBytes(const BdCell *cell);
bool BD_STDCALL BdEqualViewBytes(const BdCell *first, const BdCell *second);

struct BdChange {
  i8 before, after;
  BdPoint3 tile;
  u32 saved_index;
};

class BdAsset;
class BdMaterials;
class BdObjectInfo;
class GxBuffer;

// Storage and reached behavior of the BAT's linked-column tile grid.
class BdGrid {
public:
  BdGrid();
  virtual ~BdGrid();
  u32 AddRef();
  u32 Release();

  i32 FindCell(const BdPoint3 *tile, u8 include_objects, u8 require_origin);
  bool GetObjectOrigin(const BdPoint3 *tile, BdPoint3 *origin, const BdCell *cell);
  bool MatchesObjectOrigin(const BdPoint3 *tile, const BdPoint3 *origin, const BdCell *cell);
  bool PrepareChange();
  void CloseChangeGroup();
  // Complete marking effects; all inspected callers discard the original incidental input-pointer return.
  void MarkDirtyColumns(const BdVolume *volume);
  bool RemoveCell(const BdPoint3 *tile);
  bool DeleteCell(const BdPoint3 *tile);
  bool DeleteObjectCell(const BdPoint3 *tile, const BdCell *match, const BdPoint3 *origin);
  bool DeleteObjectBox(const BdPoint3 *first, const BdPoint3 *second, const BdPoint3 *origin, const BdCell *match);
  bool ReplaceCell(const BdPoint3 *tile, BdCell *value);
  bool InsertCell(const BdPoint3 *tile, BdCell *value);
  bool InsertRawCell(const BdPoint3 *tile, BdCell *value);
  void ResetChanges();
  void RotateColumn(u32 index, i16 amount);
  void Rotate(i32 direction);
  // Complete shift effects; the original incidental return declaration remains unresolved.
  void ShiftCells(const BdPoint3 *delta);
  u32 ApplyChange(i8 operation);
  bool Undo();
  bool Redo();
  void ResetStorage();
  void InitializeGround();
  // Complete imported-ground effects; the original incidental return declaration remains unresolved.
  void NormalizeImportedGround();
  // Complete coverage-table effects; callers discard the original incidental pointer result.
  void BuildCoverage();
  bool SetupStorage(u32 side, u32 height);
  bool InsertBox(const BdPoint3 *first, const BdPoint3 *second, BdCell *value);
  // Preserve the observed Boolean result; the private nominal return declaration remains unresolved.
  bool InsertObjectBox(const BdPoint3 *first, const BdPoint3 *second, const BdPoint3 *origin, const BdCell *value);
  bool DeleteBox(const BdPoint3 *first, const BdPoint3 *second);
  bool InsertLine(const BdPoint3 *first, const BdPoint3 *second, BdCell *value);
  bool DeleteLine(const BdPoint3 *first, const BdPoint3 *second);
  void RecountMaterials();
  u32 GetLimitedHeight();
  i32 CountUsedMaterials();
  i32 CountSpecialMaterials();
  i32 CountCells();
  // Complete material effects; the unused private return declarations remain unresolved.
  void SetFirstMaterial(BdCell *cell, u8 part, BdAsset *value);
  void SetFirstMaterialOffset(BdCell *cell, u8 part, BdAsset *value, i8 offset);
  void SetSecondMaterial(BdCell *cell, u8 part, BdAsset *value);
  void SetSecondMaterialOffset(BdCell *cell, u8 part, BdAsset *value, i8 offset);
  void ClearMaterials();
  // Preserve the observed retained pointer and genuine output; private nominal return spelling remains unresolved.
  BdMaterials *GetMaterials(BdMaterials **value);
  bool GetObjectInfo(const BdCell *cell, BdObjectInfo *info);
  bool GetMaterialInfo(u8 index, BdObjectInfo *info);
  void FreeAuxCells();
  // Preserve the observed volume pointer; the private nominal return declaration remains unresolved.
  const BdVolume *CopySlice(const BdVolume *volume);
  // Callers discard the incidental loop result; the private nominal return declaration remains unresolved.
  void CutSlice(const BdVolume *volume);
  void PasteSlice(const BdVolume *volume);
  // Complete compaction effects; the unused original private return declaration remains unresolved.
  BdCell *CompactCells();
  u8 ReconcileMode();
  // Full effect candidates; original private return declarations remain unresolved.
  void PublishFaces(const BdPoint3 *tile, BdCell *cell);
  void RemoveFaces(const BdPoint3 *tile, BdCell *cell);
  // Complete visibility effects; the unused original private return declaration remains unresolved.
  void RebuildFaces();
  // Preserve the observed bounds-pointer result; the private nominal return spelling remains unresolved.
  const BdVolume *RemoveHiddenCells();
  // Preserve the observed input-volume pointer return; private nominal pointer/reference/void spelling is unresolved.
  const BdVolume *BuildRenderSlices(const BdVolume *volume, const BdPoint3 *cursor);
  const BdVolume *RefreshRenderSlices(const BdVolume *volume, const BdPoint3 *cursor);

  i32 side, height;
  u8 rotation;
  BdCell *cells;
  u32 **columns;
  u8 changes_disabled;
  BdChange changes[0x7fff];
  u32 change_count;
  BdCell saved_cells[0x7fff];
  u32 last_change, saved_cell_count, references;
  BdMaterials *materials;
  BdVolume bounds;
  u8 mode;
  u32 face_count, hidden_face_count;
  u32 free_head, cell_count, highest_z;
  u8 changed;
  u8 **dirty_columns;
  u32 **aux_columns;
  BdVolume aux_bounds;
  u32 aux_cell_count;
  u8 mode_bytes[3];
  u32 **render_columns, **slice_z, **slice_y, **slice_x;
  u8 view_mode;
  u8 bottom_coverage[16][4], left_coverage[16][4], right_coverage[16][4];
  u8 initialized;
  u32 field_04c, field_054, field_1a8;
  GxBuffer *buffer;
  ScString localized_465, localized_466, field_038, field_194;
};

#endif
