#ifndef SC3_BUILDINGS_ARCHITECT_BD_EDITOR_H
#define SC3_BUILDINGS_ARCHITECT_BD_EDITOR_H

#include "buildings/architect/bd_geometry.h"
#include "buildings/architect/bd_grid.h"
#include "window/api.h"

// Minimap drags and the editor share the bias applied to encoded pan fractions.
enum BdPanMessageEncoding : i32 {
  kBdPanMessageCoordinateBias = 0xf4240,
};

// Positive one-step increment used by editor zoom and selection/grid rotation.
inline constexpr i32 kBdForwardViewStep = 1;

class GxBuffer;
class BdGrid;
class BdRenderer;
class BdCatalog;
struct BdThumbnailViews;

extern u16 BdTextureWidth, BdTextureHeight;
extern u8 BdDragToolChanged, BdDragToolLatched;

// The source hit record is packed to 27 bytes; its screen position follows the hit category.
#pragma pack(push, 1)
struct BdHit {
  u8 face;
  u32 cell;
  BdPoint3 tile;
  u16 kind;
  BdPoint screen;
};
#pragma pack(pop)

// The editor inherits the actual root window and owns its grid, renderer, drawing buffer and image references.
class BdEditor : public WnArchitectRootWidget {
public:
  BdEditor();
  ~BdEditor() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool OnCommand(u32 kind, u32 command) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 delta) override;
  using WnArchitectRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnWindow::SetCursor;
  // Complete interaction effects; the private nominal return declaration remains unresolved.
  void ApplyTileOp(const BdHit *hit, u32 phase);
  virtual bool OnEditorCommand(u32 command);
  virtual bool SetToolPanel(WnWindow *window);
  // Retain the observed success byte; the original private bool/byte spelling remains unresolved.
  virtual u8 SetGridAndBuffer(BdGrid *value, GxBuffer *buffer);
  // Retain the observed success byte; the original private bool/byte spelling remains unresolved.
  virtual u8 SetGrid(BdGrid *value);
  virtual BdGrid *GetGrid();
  bool PickSelectionFace(const BdPoint *screen, BdPoint3 *tile, u16 *face);
  // Preserve the observed packed byte; the private nominal return type remains unresolved.
  u8 TextureOffset(u8 direction, const BdPoint3 *tile);
  // Complete paint effects; private nominal return declarations remain unresolved.
  void PaintPrimary(const BdHit *hit, u32 id);
  void PaintPrimaryNeighbor(BdPoint3 tile, u32 direction, u32 vertices, u8 previous, BdAsset *asset);
  void PaintPrimaryNearby(u32 cell, i32 x, i32 y, i32 z, u32 direction, u8 previous,
      f32 normal_x, f32 normal_y, f32 normal_z, u32 vertices, BdAsset *asset);
  i32 ProcessPrimaryPaint(u32 direction, u8 previous, f32 normal_x, f32 normal_y, f32 normal_z,
      u32 vertices, BdAsset *asset);
  bool PaintSecondTile(const BdPoint3 *tile, u8 part, u8 direction, BdAsset *value);
  // Preserve the observed Boolean; the private nominal declaration remains unresolved.
  bool PaintSecond(const BdHit *hit, u32 id);
  u32 EraseObject(const BdHit *hit);
  // Complete object-placement effects; the original incidental return declaration remains unresolved.
  void PlaceObject(const BdHit *hit, u32 id);
  bool PickCellFace(const BdPoint *screen, BdHit *hit);
  // Preserve the observed low-byte return; the original nominal private declaration remains unresolved.
  u8 UpdateHit(BdPoint *screen, BdHit *hit);
  bool HitTestHandle(const BdPoint *screen, u16 *handle);
  // Preserve the observed screen-y register result; private nominal integer/void declaration remains unresolved.
  i32 ProjectTile(const BdPoint3 *tile, BdPoint *screen);
  void DrawColumn(i32 x, i32 y, const u32 *tint);
  void DrawColumns();
  // Complete overlay effects; the private nominal return declaration remains unresolved.
  void DrawFootprint();
  void DrawVolumeEdges(u8 enabled);
  void DrawBottomShadow();
  void DrawLeftShadow();
  void DrawRightShadow();
  void DrawResizeHandles();
  // Preserve observed register results; private nominal return declarations remain unresolved.
  i32 SetProjectionAnchor(i32 x, i32 y);
  i32 UpdateGridSteps();
  void DrawIsoGrid();
  void DrawLeftGrid(u8 behind);
  void DrawRightGrid(u8 behind);
  void DrawBottomGrid(u8 behind);
  bool SetCursor(const BdPoint3 *tile);
  void SetBrush(u32 kind, u32 rotation);
  void SetPaintAsset(u32 id);
  // Preserve the observed assignment result; the private source return declaration remains unresolved.
  u32 SetPrimaryAsset(u32 id);
  void SetToolMode(u32 mode);
  // Complete drag-tool effects; its incidental private return declaration remains unresolved.
  void UpdateDragTool(u8 modifiers);
  // Complete view-switch effects; the original private nominal return remains unresolved.
  void SetToolView(i32 view);
  void SetObjectBrush(u32 material, const BdPoint3 *tile);
  // Preserve the final SDK post result; the private nominal bool/void declaration remains unresolved.
  bool UpdateGeometry();
  // Complete grid-view reset effects; callers discard the original incidental return.
  void ResetView();
  bool LoadImages();
  // Installed callers discard the original incidental renderer-pointer result.
  void SetDrawEdges(u8 enabled);
  // Installed callers discard the path-dependent incidental return.
  void RenderThumbnail();
  bool RenderExportView(i32 orientation, GxBuffer *const *source, BdThumbnailViews *out);
  // Complete scene effects; the source's private nominal return declaration is unresolved.
  void PaintScene();
  bool MoveSelection(const BdPoint3 *delta);
  bool Zoom(i32 delta);
  // Complete rotation effects; the original incidental return declaration is unresolved.
  void RotateView(i32 direction, u8 rotate_grid);
  // Complete dirty-area effects; private incidental return declarations remain unresolved.
  void InvalidateView(i32 view);
  void InvalidateViews(u16 mask);
  void ClearMarks();
  // Preserve the observed byte result; the private nominal bool/byte declaration remains unresolved.
  u8 MarkBox(const BdPoint3 *first, BdPoint3 *second);
  bool MarkLine(BdPoint3 *first, BdPoint3 *second);
  void MarkViewActive(i32 view);
  void AdvanceDirty();
  void ClearCurrentDirty();
  virtual void InvalidateArea(const BdRect *rectangle);
  virtual void ValidateArea(const BdRect *rectangle);
  void ResetDirty();
  // Native MSVC rectangle return ABI is verified; the private source spelling of its hidden output is undocumented.
  BdRect GetDirtyArea(i32 view);
  void DrawViewHandle();
  void UpdateHandleBounds();
  u8 GetPlaneMask(u32 cell_index, const BdPoint3 *tile);
  void DrawLeftSlice();
  void DrawRightSlice();
  void DrawBottomSlice();
  i32 ProjectTileF(const BdPoint3 *tile, BdPointF *screen);
  // Plane projections retain observed results; their private nominal return declarations remain unresolved.
  i32 ProjectLeftTile(const BdPoint3 *tile, BdPoint *screen);
  i32 ProjectLeftCell(const BdPoint3 *tile, BdPoint *screen);
  i32 ProjectRightTile(const BdPoint3 *tile, BdPoint *screen);
  i32 ProjectRightCell(const BdPoint3 *tile, BdPoint *screen);
  BdEditor *ProjectBottomTile(const BdPoint3 *tile, BdPoint *screen);
  BdEditor *ProjectBottomCell(const BdPoint3 *tile, BdPoint *screen);

  BdVolume selection;
  BdPoint left_anchor, right_anchor, bottom_anchor;
  i32 zoom_shift;
  BdPoint3 cursor;
  BdCell cursor_cell, field_734;
  u32 field_730;
  BdPoint3 field_754;
  GxBuffer *handle_images[4];
  BdRect handle_bounds[4];
  BdPoint projection_anchor, view_anchor, tool_anchor;
  BdGrid *grid;
  BdRect surface_bounds;
  BdCatalog *catalog;
  BdRenderer *renderer;
  WnWindow *tool_panel = nullptr;
  i32 tool, tool_mode, field_81c;
  u32 placement_tint, brush_asset_id;
  BdPoint3 brush_origin;
  BdHit previous_hit, selected_hit, hover_hit;
  u16 hover_kind;
  GxBuffer *drawing_buffer, *view_handles[6];
  u8 view_handle_enabled, field_780, left_drag_active, right_drag_active, handle_hover;
  i32 field_764, field_774, field_778, field_77c;
  BdPoint3 drag_direction;
  BdPoint drag_screen_scaled;
  u32 frame_time, last_frame_time;
  BdRect dirty_area, view_bounds[10];
  BdRect dirty_active[10][4], dirty_pending[10][4];
  u32 dirty_masks[3], active_masks[3];
  f32 zoom_scale;
  u32 brush_kind, brush_rotation, paint_asset_id, saved_tool_modes[4];
  u32 mark_side, mark_height, primary_asset_id;
  u32 field_834, field_838, background_pixel, field_844, field_848;
  u8 field_83c, field_83d, field_83e;
  u8 **left_marks, **right_marks, **bottom_marks;
  u32 shadow_columns[40][40];
  i32 grid_counts[8];
  BdPoint grid_steps[8], grid_extents[4];
  u32 grid_colors[2], grid_edge_color;
};

#endif
