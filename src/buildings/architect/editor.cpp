#define WIN32_LEAN_AND_MEAN
#include <math.h>
#include <SDL3/SDL_timer.h>
#include <stdlib.h>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/bd_editor.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/manager.h"
#include "buildings/architect/renderer.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "window/api.h"
#include "resources/resource.h"

// The editor and grid-view ID counters start in the same reserved window-ID range.
enum BdEditorGridIdentity : u32 {
  kBdEditorGridWindowIdBase = 0x6104489a,
};

// The editor assigns this identity to its own root window before binding its catalog and accelerators.
enum BdEditorWindowIdentity : u32 {
  kBdEditorWindowId = 0xb1d0b1d0,
};

// Face-paint propagation stops after the original processed-face threshold.
enum BdPaintLimit : i32 {
  kBdPaintMaximumProcessedFaces = 0x186a0,
};

// Editor preview buffers use a magenta transparency key and a distinct erase-tool tint.
enum BdEditorColor : u32 {
  kBdEditorTransparencyColor = 0xffff00ff,
  kBdErasePlacementColor = 0xfff7574d,
};

static constexpr i32 kBdUnsetProjectionAnchor = INT32_MAX;

enum BdEditorRenderConstant : u32 {
  kBdFootprintBlueHighlight = 0xffa4b7c1,
  kBdFootprintBlueShadow = 0xff546a84,
  kBdFootprintBlueMidtone = 0xff78899e,
  kBdFootprintPinkHighlight = 0xffd2939d,
  kBdFootprintPinkMidtone = 0xffa36b72,
  kBdFootprintPinkShadow = 0xff8b454a,
  kBdFootprintGreenHighlight = 0xffbdd3c5,
  kBdFootprintGreenShadow = 0xff498f63,
  kBdFootprintGreenMidtone = 0xff6fa784,
  kBdInvalidPlacementTint = 0xff734040,
  kBdGridLineDefaultColor = 0xff505750,
};

// Walk the clipped cell column in height order, preserving brush/object tint selection and selected-face overlays.
// Projection and material rendering use the actual editor, grid and renderer fields.
void BdEditor::DrawColumn(i32 x, i32 y, const u32 *tint) {
  u32 index = grid->render_columns[x][y];
  if (!index)
    return;
  u32 color = *tint;
  BdPoint3 tile = {
      x, y, static_cast<i32>((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask)};
  BdPoint3 base = {x, y, 0}, selected_origin = {};
  BdPoint base_screen;
  ProjectTile(&base, &base_screen);
  BdPoint screen = base_screen;
  BdObjectInfo info;
  bool selected_object = selected_hit.kind == 128 &&
      (grid->cells[selected_hit.cell].link_height_kind & kBdCellKindMask) == kBdCellObjectKind;
  if (tool == 3 && selected_hit.kind == 128) {
    BdPoint3 selected_tile = selected_hit.tile;
    BdGetObjectInfo(brush_asset_id, &info);
    grid->GetObjectOrigin(&selected_tile, &selected_origin, &grid->cells[selected_hit.cell]);
    if (tool_mode == kBdToolErase)
      color = BdErasableHighlight;
  }
  bool selected_column = selected_hit.kind == 128 && x == selected_hit.tile.x && y == selected_hit.tile.y;
  u16 limit = static_cast<u16>(grid->bounds.size.z);
  bool detailed = selected_column;
  if (tool == 3 && tool_mode == kBdToolProp) {
    u32 dx = static_cast<u32>(abs(tile.x - selected_hit.tile.x));
    u32 dy = static_cast<u32>(abs(tile.y - selected_hit.tile.y));
    if (dx <= static_cast<u32>(info.size.x / 2) && dy <= static_cast<u32>(info.size.y / 2) && selected_hit.kind == 128)
      detailed = true;
  }
  do {
    const BdCell *cell = &grid->cells[index];
    u32 packed = cell->link_height_kind, height = (packed >> kBdCellHeightShift) & kBdCellHeightValueMask;
    if (height >= limit)
      break;
    tile.z = static_cast<i32>(height);
    screen.y = static_cast<i32>(static_cast<u32>(base_screen.y) - (height << (zoom_shift & 31)));
    u8 use_color = 0;
    if (tool == 3) {
      if (detailed) {
        if (selected_object && (packed & kBdCellKindMask) == kBdCellObjectKind) {
          if (BdEqualViewBytes(cell, &grid->cells[selected_hit.cell])) {
            BdPoint3 origin = {};
            grid->GetObjectOrigin(&tile, &origin, cell);
            if (origin.x == selected_origin.x && origin.y == selected_origin.y && origin.z == selected_origin.z)
              use_color = 1;
          }
        } else if (height - static_cast<u16>(selected_hit.tile.z) < static_cast<u32>(info.size.z) &&
            (packed & kBdCellKindMask) != kBdCellObjectKind)
          use_color = tool_mode == kBdToolProp;
      } else if ((packed & kBdCellKindMask) == kBdCellObjectKind && selected_object &&
          BdEqualViewBytes(cell, &grid->cells[selected_hit.cell])) {
        BdPoint3 origin = {}, other_origin = {}, selected_tile = selected_hit.tile;
        grid->GetObjectOrigin(&tile, &origin, cell);
        grid->GetObjectOrigin(&selected_tile, &other_origin, &grid->cells[selected_hit.cell]);
        if (origin.x == other_origin.x && origin.y == other_origin.y)
          use_color = origin.z == other_origin.z;
      }
    }
    if (height)
      renderer->DrawCell(&screen, cell, use_color, color);
    else
      renderer->DrawTopCell(&screen, cell, use_color, color);
    if (detailed && selected_column) {
      packed = grid->cells[index].link_height_kind;
      if (static_cast<u32>(selected_hit.tile.z) == ((packed >> kBdCellHeightShift) & kBdCellHeightValueMask)) {
        u32 selected = grid->cells[selected_hit.cell].link_height_kind;
        const BdFace *face = &BdCellFaces[(selected >> kBdCellKindShift) & kBdCellKindValueMask][selected_hit.face];
        u8 rotation = static_cast<u8>(selected >> kBdCellRotationShift);
        bool object = (packed & kBdCellKindMask) == kBdCellObjectKind;
        if ((tool == 3 && tool_mode != kBdToolProp) || object) {
          if ((renderer->draw_empty_objects || !object) && (tool != 3 || tool_mode == kBdToolProp))
            renderer->DrawHighlight(&screen, &grid->cells[index], BdConstructRegBlock);
        } else {
          u32 face_tint = placement_tint;
          if (tool == 3) {
            BdPoint3 placement = selected_hit.tile;
            f32 normal_x, normal_y, normal_z;
            u8 direction = static_cast<u8>((face->flags & 0xF8) | ((face->flags + 2 * rotation) & 7));
            BdFaceNormal(direction, &normal_x, &normal_y, &normal_z);
            if ((grid->cells[selected_hit.cell].link_height_kind & kBdCellKindMask) != kBdCellGroundKind) {
              if (normal_z >= 0.9)
                ++placement.z;
              else if (normal_x > 0.9)
                ++placement.x;
              else if (normal_x < -0.9)
                --placement.x;
              else if (normal_y > 0.9)
                ++placement.y;
              else if (normal_y < -0.9)
                --placement.y;
              else
                face_tint = kBdInvalidPlacementTint;
            }
            if (placement.z >= grid->height || placement.x >= grid->side || placement.y >= grid->side ||
                placement.z < 0 || placement.x < 0 || placement.y < 0)
              face_tint = kBdInvalidPlacementTint;
          }
          if ((grid->cells[index].link_height_kind & kBdCellHeightMask) || selected_hit.face == 0) {
            renderer->ProjectCell(&screen, rotation);
            renderer->DrawFace(face, face_tint, rotation);
            renderer->DrawEdges(face, 0, rotation);
          }
        }
      }
    }
    index = grid->cells[index].link_height_kind & kBdCellNextMask;
  } while (index);
}

// Visit the selected grid volume in the original x-major column order, sharing one captured highlight color.
// Cache the traversal limits before drawing, as the original scene pass does.
void BdEditor::DrawColumns() {
  u32 tint = BdSelectedHighlight;
  i32 first_x = grid->bounds.origin.x, first_y = grid->bounds.origin.y;
  i32 end_x = first_x + grid->bounds.size.x, end_y = first_y + grid->bounds.size.y;
  for (i32 x = first_x; x < end_x; ++x)
    for (i32 y = first_y; y < end_y; ++y)
      DrawColumn(x, y, &tint);
}

// Preserve the source's wrapped subtraction and integer absolute value for a projected pan distance.
static i32 BdPanDistance(i32 first, i32 second, f64 fraction) {
  i32 length = static_cast<i32>(static_cast<u32>(first) - static_cast<u32>(second));
  i32 value = static_cast<i32>(static_cast<i64>(static_cast<f64>(length) * fraction));
  return value < 0 ? static_cast<i32>(0u - static_cast<u32>(value)) : value;
}

static void BdPostEditorCommand(u32 type) {
  ScMessage message(type);
  BdGetMessageServer()->MessagePost(message, false);
}

// Dispatch editor navigation, view, edit, file, and display commands to their actual game methods.
// Preserve the original handled result and invalidate only after the corresponding successful operation.
bool BdEditor::OnEditorCommand(u32 command) {
  if (command == kBdEditorCommandZoomIn || command == kBdEditorCommandZoomOut ||
      command == kBdEditorCommandZoomInAlternate || command == kBdEditorCommandZoomOutAlternate) {
    if (Zoom(command == kBdEditorCommandZoomIn || command == kBdEditorCommandZoomInAlternate ? 1 : -1))
      InvalidateView(0);
    return true;
  }
  if (command == kBdEditorCommandRotatePositive || command == kBdEditorCommandRotateNegative) {
    RotateView(command == kBdEditorCommandRotatePositive ? 128 : -128, 1);
    InvalidateView(0);
    return true;
  }
  if (command == kBdEditorCommandLowerSelection || command == kBdEditorCommandRaiseSelection ||
      command == kBdEditorCommandLowerSelectionEight || command == kBdEditorCommandRaiseSelectionEight ||
      (command >= kBdEditorCommandNudgeNegativeX && command <= kBdEditorCommandNudgeNegativeZ)) {
    bool vertical = command == kBdEditorCommandLowerSelection || command == kBdEditorCommandRaiseSelection ||
        command == kBdEditorCommandLowerSelectionEight || command == kBdEditorCommandRaiseSelectionEight;
    if (vertical && (tool || selected_hit.kind != 64))
      return true;
    BdPoint3 delta = {};
    switch (command) {
    case kBdEditorCommandLowerSelection:
      delta.z = -1;
      break;
    case kBdEditorCommandRaiseSelection:
      delta.z = 1;
      break;
    case kBdEditorCommandLowerSelectionEight:
      delta.z = -8;
      break;
    case kBdEditorCommandRaiseSelectionEight:
      delta.z = 8;
      break;
    case kBdEditorCommandNudgeNegativeX:
      delta.x = -1;
      break;
    case kBdEditorCommandNudgeNegativeXNegativeY:
      delta.x = -1;
      delta.y = -1;
      break;
    case kBdEditorCommandNudgeNegativeY:
      delta.y = -1;
      break;
    case kBdEditorCommandNudgeNegativeXPositiveY:
      delta.x = -1;
      delta.y = 1;
      break;
    case kBdEditorCommandNudgePositiveXNegativeY:
      delta.x = 1;
      delta.y = -1;
      break;
    case kBdEditorCommandNudgePositiveY:
      delta.y = 1;
      break;
    case kBdEditorCommandNudgePositiveXPositiveY:
      delta.x = 1;
      delta.y = 1;
      break;
    case kBdEditorCommandNudgePositiveX:
      delta.x = 1;
      break;
    case kBdEditorCommandNudgePositiveZ:
      delta.z = 1;
      break;
    case kBdEditorCommandNudgeNegativeZ:
      delta.z = -1;
      break;
    }
    BdPoint3 next = {static_cast<i32>(static_cast<u32>(cursor.x) + static_cast<u32>(delta.x)),
        static_cast<i32>(static_cast<u32>(cursor.y) + static_cast<u32>(delta.y)),
        static_cast<i32>(static_cast<u32>(cursor.z) + static_cast<u32>(delta.z))};
    if (MoveSelection(&delta)) {
      if (!tool || (command == kBdEditorCommandNudgePositiveXNegativeY && tool == 3))
        SetCursor(&next);
      InvalidateView(1);
      if (vertical)
        InvalidateView(4);
    }
    return true;
  }
  switch (command) {
  case kBdEditorCommandCycleToolMode:
    if (!tool)
      SetToolMode(field_81c == kBdToolBlock ? kBdToolLine : field_81c == kBdToolLine ? kBdToolPlane : kBdToolBlock);
    else if (tool == 1)
      SetToolMode(field_81c == kBdToolPaintbrush ? kBdToolFill : kBdToolPaintbrush);
    else if (tool == 2)
      SetToolMode(kBdToolComposition);
    else if (tool == 3)
      SetToolMode(kBdToolProp);
    else if (tool == 4)
      SetToolMode(kBdToolNone);
    return true;
  case kBdEditorCommandEraseTool:
    SetToolMode(kBdToolErase);
    return true;
  case kBdEditorCommandSelectTool:
    SetToolMode(kBdToolSelect);
    return true;
  case kBdEditorCommandUndo:
  case kBdEditorCommandUndoAlternate:
    if (grid->Undo())
      InvalidateView(0);
    return true;
  case kBdEditorCommandRedo:
  case kBdEditorCommandRedoAlternate:
    if (grid->Redo())
      InvalidateView(0);
    return true;
  case kBdEditorCommandCopySelection:
    if (!tool)
      grid->CopySlice(&selection);
    return true;
  case kBdEditorCommandCutSelection:
    if (!tool) {
      grid->CutSlice(&selection);
      InvalidateView(0);
    }
    return true;
  case kBdEditorCommandPasteSelection:
    if (!tool) {
      grid->PasteSlice(&selection);
      InvalidateView(0);
    }
    return true;
  case kBdEditorCommandSave:
  case kBdEditorCommandSaveAlternate: {
    ScString path;
    if (BdGetManager()->GetPath(&path)) {
      const char *path_data = path.begin;
      BdManager *grid_manager = BdGetManager(), *save_manager = BdGetManager();
      BdGrid *value = grid_manager->GetGrid();
      if (!save_manager->SaveGrid(value, path_data))
        BdPostEditorCommand(kWnMsg_ArchitectSaveBuilding);
    } else {
      BdPostEditorCommand(kWnMsg_ArchitectSaveBuilding);
    }
    return true;
  }
  case kBdEditorCommandSaveAs:
  case kBdEditorCommandSaveAsAlternate:
    BdPostEditorCommand(kWnMsg_ArchitectSaveBuilding);
    return true;
  case kBdEditorCommandOpen:
  case kBdEditorCommandOpenAlternate:
    BdPostEditorCommand(kWnMsg_ArchitectOpenBuilding);
    return true;
  case kBdEditorCommandExit:
  case kBdEditorCommandExitAlternate:
    BdPostEditorCommand(kWnMsg_ArchitectExit);
    return true;
  case 76:
  case 91:
  case 103:
  case 104:
  case 106:
    return true;
  case kBdEditorCommandResetView:
  case kBdEditorCommandResetViewAlternate:
    ResetView();
    InvalidateView(0);
    return true;
  case kBdEditorCommandToggleEdges:
    if (!renderer)
      return true;
    renderer->draw_edges = !renderer->draw_edges;
    InvalidateView(0);
    return true;
  case kBdEditorCommandToggleViewHandles:
    view_handle_enabled = !view_handle_enabled;
    InvalidateView(0);
    return true;
  case kBdEditorCommandToggleEmptyObjects:
    if (!renderer)
      return true;
    renderer->draw_empty_objects = !renderer->draw_empty_objects;
    InvalidateView(0);
    return true;
  default:
    return false;
  }
}

// Route editor notifications to tools, view controls, commands, and inherited window message handling.
// Pan messages update and clamp the projection anchor before notifying the other editor controls.
bool BdEditor::DoMessage(ScMessage *message) {
  u32 first = static_cast<u32>(message->data1), second = static_cast<u32>(message->data2);
  switch (message->type) {
  case kWnMsg_AcceleratorCommand:
    return OnEditorCommand(first);
  case kWnMsg_ArchitectZoomIn:
  case kWnMsg_ArchitectZoomOut:
    Zoom(message->type == kWnMsg_ArchitectZoomIn ? kBdForwardViewStep : -1);
    InvalidateView(0);
    return false;
  case kWnMsg_ArchitectRotateViewNegative:
  case kWnMsg_ArchitectRotateViewPositive:
    RotateView(message->type == kWnMsg_ArchitectRotateViewNegative ? -1 : kBdForwardViewStep, 1);
    InvalidateView(0);
    return false;
  case kWnMsg_ArchitectSetTool:
    SetToolMode(first);
    return false;
  case kWnMsg_ArchitectSetAsset:
    if (tool == 1)
      SetPrimaryAsset(first);
    else if (tool == 2)
      SetPaintAsset(first);
    else if (tool == 3) {
      BdPoint3 origin = {};
      SetObjectBrush(first, &origin);
    }
    return false;
  case kWnMsg_ArchitectSetBrush:
    SetBrush(first, second);
    return false;
  case kWnMsg_ArchitectPanView:
    if (tool >= 1 && tool <= 4) {
      f64 x_fraction = static_cast<f64>(static_cast<i32>(first - kBdPanMessageCoordinateBias)) * 0.00001f;
      f32 y_fraction =
          static_cast<f32>(static_cast<f64>(static_cast<i32>(second - kBdPanMessageCoordinateBias)) * 0.00001f);
      i32 x_sign = x_fraction <= 0 ? -1 : 1, y_sign = y_fraction <= 0 ? -1 : 1;
      i32 x_distance = BdPanDistance(surface_bounds.right, surface_bounds.left, x_fraction);
      i32 y_distance = BdPanDistance(surface_bounds.bottom, surface_bounds.top, y_fraction);
      i8 orientation = static_cast<i8>(grid->view_mode);
      u8 shift = orientation == 1 ? static_cast<u8>(static_cast<u32>(field_778) - 5)
          : orientation == 2 ? static_cast<u8>(static_cast<u32>(field_778) - 4)
          : orientation > 2 && orientation <= 5 ? static_cast<u8>(static_cast<u32>(field_778) - 3)
          : static_cast<u8>(static_cast<u32>(y_distance) >> 24);
      if (shift) {
        x_distance = static_cast<i32>(static_cast<u32>(x_distance) << (shift & 31));
        y_distance = static_cast<i32>(static_cast<u32>(y_distance) << (shift & 31));
      }
      projection_anchor.x = static_cast<i32>(static_cast<u32>(GetW() / 2) -
          static_cast<u32>(x_sign) * static_cast<u32>(x_distance));
      projection_anchor.y = static_cast<i32>(static_cast<u32>(GetH() / 2) -
          static_cast<u32>(y_sign) * static_cast<u32>(y_distance));
      if (projection_anchor.x < 0)
        projection_anchor.x = 0;
      if (projection_anchor.x > GetW())
        projection_anchor.x = GetW();
      if (projection_anchor.y < surface_bounds.top)
        projection_anchor.y = surface_bounds.top;
      if (projection_anchor.y > surface_bounds.bottom)
        projection_anchor.y = surface_bounds.bottom;
      UpdateHandleBounds();
      ScMessage update(kWnMsg_ArchitectProjectionBoundsChanged, reinterpret_cast<uptr>(&projection_anchor),
          reinterpret_cast<uptr>(&view_bounds[0]), 0);
      BdGetMessageServer()->MessagePost(update, false);
      InvalidateView(0);
    }
    return false;
  case kWnMsg_ArchitectRotateSelectionNegative:
  case kWnMsg_ArchitectRotateSelectionPositive:
    RotateView(message->type == kWnMsg_ArchitectRotateSelectionNegative ? -1 : kBdForwardViewStep, 0);
    InvalidateView(0);
    return false;
  case kWnMsg_ArchitectForwardInput:
    if (first == 3)
      return OnCommand(second, static_cast<u32>(message->data3));
    if (first == 5)
      return OnKeyDown(second, static_cast<u32>(message->data3));
    if (first == 6)
      return OnKeyUp(second, static_cast<u32>(message->data3));
    return false;
  default:
    return WnArchitectRootWidget::DoMessage(message);
  }
}

// Initialize the editor's owned objects, selection, tool defaults, image references and dirty rectangles.
// The zero dirty area prevents the original constructor's read of an uninitialized rectangle.
BdEditor::BdEditor() {
  grid = nullptr;
  drawing_buffer = nullptr;
  catalog = nullptr;
  renderer = nullptr;
  tool_panel = nullptr;
  tool = 4;
  view_anchor = {kBdUnsetProjectionAnchor, kBdUnsetProjectionAnchor};
  selection = {};
  cursor = {};
  placement_tint = BdConstructSelectedBlock;
  field_730 = 0;
  field_754 = {};
  drag_direction = {};
  field_774 = 0;
  field_778 = 0;
  left_drag_active = 0;
  right_drag_active = 0;
  previous_hit.tile = {};
  selected_hit.tile = {};
  hover_hit.tile = {};
  hover_kind = 0;
  frame_time = last_frame_time = 0;
  mark_side = 0;
  mark_height = 0;
  left_marks = nullptr;
  right_marks = nullptr;
  bottom_marks = nullptr;
  brush_kind = 0;
  brush_rotation = 0;
  paint_asset_id = 0;
  primary_asset_id = 0;
  brush_asset_id = 0;
  brush_origin = {};
  saved_tool_modes[0] = kBdToolBlock;
  saved_tool_modes[1] = kBdToolFill;
  saved_tool_modes[2] = kBdToolComposition;
  saved_tool_modes[3] = kBdToolProp;
  field_834 = 1;
  field_838 = 0;
  field_83c = 1;
  field_83d = 1;
  field_83e = 0;
  field_844 = 0;
  field_848 = 0;
  view_handle_enabled = 1;
  for (u32 i = 0; i < 4; ++i)
    handle_images[i] = nullptr;
  handle_hover = 0;
  grid_colors[0] = 0;
  grid_colors[1] = 0;
  grid_edge_color = 0;
  zoom_shift = field_778;
  tool_anchor = {kBdUnsetProjectionAnchor, kBdUnsetProjectionAnchor};
  view_anchor = {kBdUnsetProjectionAnchor, kBdUnsetProjectionAnchor};
  selected_hit.kind = 0;
  previous_hit.kind = 0;
  dirty_area = {};
  ResetDirty();
  for (u32 i = 0; i < 6; ++i)
    view_handles[i] = nullptr;
  surface_bounds = {-1, -1, -1, -1};
  for (u32 i = 0; i < 4; ++i)
    handle_bounds[i] = {-1, -1, -1, -1};
  for (u32 view = 0; view < 10; ++view) {
    view_bounds[view] = {-1, -1, -1, -1};
    for (u32 slot = 0; slot < 4; ++slot) {
      dirty_active[view][slot] = {-1, -1, -1, -1};
      dirty_pending[view][slot] = {-1, -1, -1, -1};
    }
  }
  grid_colors[0] = BdConstructIsoGrid;
  grid_colors[1] = kBdGridLineDefaultColor;
  grid_edge_color = BdConstructIsoVolumeEdge;
  selected_hit = {};
}

BdEditor::~BdEditor() {
  BdEditor::Shutdown();
}

// Clip two rectangles into the receiver, clearing all four output coordinates when their intervals do not overlap.
bool BdRect::Intersect(const BdRect *first, const BdRect *second) {
  if (first->right <= second->left || first->left >= second->right || first->bottom <= second->top ||
      first->top >= second->bottom) {
    bottom = 0;
    top = 0;
    right = 0;
    left = 0;
    return false;
  }
  left = first->left > second->left ? first->left : second->left;
  top = first->top > second->top ? first->top : second->top;
  right = first->right < second->right ? first->right : second->right;
  bottom = first->bottom < second->bottom ? first->bottom : second->bottom;
  return true;
}

// Accumulate the selected view's paint bounds into its first pending rectangle and publish its mask bit.
// Only nonnegative left/top bounds dispatch the original live invalidation virtual.
void BdEditor::InvalidateView(i32 view) {
  dirty_masks[0] |= 1u << (static_cast<u32>(view) & 31);
  BdRect *pending = &dirty_pending[view][0];
  if (pending->left == -1)
    *pending = view_bounds[view];
  else {
    BdRect previous = *pending;
    pending->Unite(&previous, &view_bounds[view]);
  }
  if (pending->left >= 0 && pending->top >= 0)
    InvalidateArea(pending);
}

// Clip an overlapping invalidation into the window, accumulate its dirty area, and notify children in reverse list order.
// Capture the child count before callbacks, and preserve child notification even when an empty input does not mark the window.
void BdEditor::InvalidateArea(const BdRect *rectangle) {
  BdRect window_area = {absolute_area.left, absolute_area.top, absolute_area.right, absolute_area.bottom};
  BdRect clipped = *rectangle;
  if (clipped.right <= window_area.left || clipped.left >= window_area.right || clipped.bottom <= window_area.top ||
      clipped.top >= window_area.bottom)
    return;
  clipped.Intersect(rectangle, &window_area);
  if (dirty_area.right > dirty_area.left && dirty_area.bottom > dirty_area.top) {
    BdRect previous = dirty_area;
    dirty = true;
    dirty_area.Unite(&clipped, &previous);
  } else if (rectangle->right > rectangle->left && rectangle->bottom > rectangle->top) {
    dirty_area = clipped;
    dirty = true;
  }
  u32 count = 0;
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    ++count;
  WnChild *node = children.head;
  while (count) {
    node = node->previous;
    node->window->InvalidateSelf();
    --count;
  }
}

// Clear the complete window dirty area when a positive clipped rectangle lies strictly inside the right/bottom edges.
// Preserve the source's child-list traversal without adding notifications or clearing the separate dirty byte.
void BdEditor::ValidateArea(const BdRect *rectangle) {
  if (rectangle->right <= rectangle->left || rectangle->bottom <= rectangle->top)
    return;
  BdRect window_area = {absolute_area.left, absolute_area.top, absolute_area.right, absolute_area.bottom};
  BdRect clipped = *rectangle;
  if (clipped.right <= window_area.left || clipped.left >= window_area.right || clipped.bottom <= window_area.top ||
      clipped.top >= window_area.bottom)
    return;
  clipped.Intersect(rectangle, &window_area);
  if (clipped.right > clipped.left && clipped.bottom > clipped.top && clipped.left >= window_area.left &&
      clipped.top >= window_area.top && clipped.left < window_area.right && clipped.top < window_area.bottom &&
      clipped.right >= window_area.left && clipped.bottom >= window_area.top && clipped.right < window_area.right &&
      clipped.bottom < window_area.bottom) {
    dirty_area.bottom = 0;
    dirty_area.right = 0;
    dirty_area.top = 0;
    dirty_area.left = 0;
  }
  u32 count = 0;
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    ++count;
  WnChild *node = children.head;
  while (count) {
    node = node->previous;
    --count;
  }
}

void BdEditor::ResetDirty() {
  for (u32 slot = 0; slot < 3; ++slot) {
    dirty_masks[slot] = 0;
    for (u32 view = 0; view < 10; ++view) {
      BdRect *rectangle = &dirty_pending[view][slot];
      rectangle->left = -1;
      rectangle->top = -1;
      rectangle->right = -1;
    }
  }
  BdRect rectangle = dirty_area;
  ValidateArea(&rectangle);
}

// Combine the first three active rectangles whose left coordinates are not the unset sentinel, then clip to the window.
// With no active rectangle, every defined source execution returns zero after horizontal rejection; undefined local reads are omitted.
BdRect BdEditor::GetDirtyArea(i32 view) {
  BdRect window_area = {absolute_area.left, absolute_area.top, absolute_area.right, absolute_area.bottom};
  const BdRect *areas = dirty_active[view];
  u32 mask = (areas[0].left != -1 ? 1u : 0u) | (areas[1].left != -1 ? 2u : 0u) | (areas[2].left != -1 ? 4u : 0u);
  BdRect merged, clipped;
  switch (mask) {
  case 0:
    return {0, 0, 0, 0};
  case 1:
    merged = areas[0];
    break;
  case 2:
    merged = areas[1];
    break;
  case 4:
    merged = areas[2];
    break;
  case 3:
    merged.Unite(areas, areas + 1);
    break;
  case 5:
    merged.Unite(areas, areas + 2);
    break;
  case 6:
    merged.Unite(areas + 1, areas + 2);
    break;
  default: {
    BdRect first, second;
    first.Unite(areas, areas + 1);
    second.Unite(areas + 1, areas + 2);
    merged.Unite(&first, &second);
    break;
  }
  }
  clipped.Intersect(&merged, &window_area);
  return clipped;
}

// Accumulate the selected view into the first active rectangle and mark its current presentation mask.
// Publish nonnegative bounds through the same invalidation virtual used by pending scene changes.
void BdEditor::MarkViewActive(i32 view) {
  active_masks[0] |= 1u << (static_cast<u32>(view) & 31);
  BdRect *active = &dirty_active[view][0];
  if (active->left == -1)
    *active = view_bounds[view];
  else {
    BdRect previous = *active;
    active->Unite(&previous, &view_bounds[view]);
  }
  if (active->left >= 0 && active->top >= 0)
    InvalidateArea(active);
}

// Shift both mask histories, then validate and advance the pending rectangle histories before the active histories.
void BdEditor::AdvanceDirty() {
  dirty_masks[2] = dirty_masks[1];
  dirty_masks[1] = dirty_masks[0];
  active_masks[2] = active_masks[1];
  active_masks[1] = active_masks[0];
  for (u32 view = 0; view < 10; ++view) {
    BdRect *areas = dirty_pending[view];
    ValidateArea(areas + 2);
    areas[2] = areas[1];
    areas[1] = areas[0];
  }
  for (u32 view = 0; view < 10; ++view) {
    BdRect *areas = dirty_active[view];
    ValidateArea(areas + 2);
    areas[2] = areas[1];
    areas[1] = areas[0];
  }
}

void BdEditor::ClearCurrentDirty() {
  active_masks[0] = 0;
  for (u32 view = 0; view < 10; ++view) {
    BdRect *rectangle = &dirty_active[view][0];
    rectangle->left = -1;
    rectangle->top = -1;
    rectangle->right = -1;
  }
  dirty_masks[0] = 0;
  for (u32 view = 0; view < 10; ++view) {
    BdRect *rectangle = &dirty_pending[view][0];
    rectangle->left = -1;
    rectangle->top = -1;
    rectangle->right = -1;
  }
}

// Invalidate the individual editor views selected by the low nine mask bits, in the original callback order.
// The top two bits both select view 1 and therefore can trigger two separate invalidations.
void BdEditor::InvalidateViews(u16 mask) {
  if (mask & 0x001)
    InvalidateView(6);
  if (mask & 0x002)
    InvalidateView(7);
  if (mask & 0x004)
    InvalidateView(8);
  if (mask & 0x008)
    InvalidateView(5);
  if (mask & 0x010)
    InvalidateView(2);
  if (mask & 0x020)
    InvalidateView(3);
  if (mask & 0x040)
    InvalidateView(4);
  if (mask & 0x080)
    InvalidateView(1);
  if (mask & 0x100)
    InvalidateView(1);
}

// Clear both vertical mark grids only when both exist, then clear the independent horizontal grid.
// Rows use the live grid's side and height, preserving its original paired-pointer behavior.
void BdEditor::ClearMarks() {
  u32 side = static_cast<u32>(grid->side), height = static_cast<u32>(grid->height);
  if (left_marks && right_marks)
    for (u32 x = 0; x < side; ++x)
      for (u32 z = 0; z < height; ++z) {
        left_marks[x][z] = 0;
        right_marks[x][z] = 0;
      }
  if (bottom_marks)
    for (u32 x = 0; x < side; ++x)
      for (u32 y = 0; y < side; ++y)
        bottom_marks[x][y] = 0;
}

// Mark the three projected views of the inclusive box formed by two tile coordinates.
// Capture the box before constraining the mutable endpoint to the currently selected edit plane.
u8 BdEditor::MarkBox(const BdPoint3 *first, BdPoint3 *second) {
  BdPoint3 lower = {first->x < second->x ? first->x : second->x,
      first->y < second->y ? first->y : second->y, first->z < second->z ? first->z : second->z};
  BdPoint3 upper = {first->x > second->x ? first->x : second->x,
      first->y > second->y ? first->y : second->y, first->z > second->z ? first->z : second->z};
  if (previous_hit.kind == 32)
    second->x = first->x;
  if (previous_hit.kind == 16)
    second->y = first->y;
  if (previous_hit.kind == 64)
    second->z = first->z;
  if (grid) {
    i32 *low[3] = {&lower.x, &lower.y, &lower.z}, *high[3] = {&upper.x, &upper.y, &upper.z};
    const i32 *origin[3] = {&grid->bounds.origin.x, &grid->bounds.origin.y, &grid->bounds.origin.z};
    const i32 *size[3] = {&grid->bounds.size.x, &grid->bounds.size.y, &grid->bounds.size.z};
    for (u32 axis = 0; axis < 3; ++axis) {
      i32 limit = static_cast<i32>(static_cast<u32>(*origin[axis]) + static_cast<u32>(*size[axis]));
      if (*low[axis] < *origin[axis])
        *low[axis] = *origin[axis];
      if (*high[axis] < *origin[axis])
        *high[axis] = *origin[axis];
      if (*low[axis] >= limit)
        *low[axis] = static_cast<i32>(static_cast<u32>(limit) - 1);
      if (*high[axis] >= limit)
        *high[axis] = static_cast<i32>(static_cast<u32>(limit) - 1);
    }
  }
  for (i32 x = lower.x; x <= upper.x; ++x)
    for (i32 z = lower.z; z <= upper.z; ++z)
      left_marks[x][z] = 1;
  for (i32 y = lower.y; y <= upper.y; ++y)
    for (i32 z = lower.z; z <= upper.z; ++z)
      right_marks[y][z] = 1;
  for (i32 x = lower.x; x <= upper.x; ++x)
    for (i32 y = lower.y; y <= upper.y; ++y)
      bottom_marks[x][y] = 1;
  return 1;
}

// Clamp both mutable endpoints, then mark a projected line in the selected editor plane.
// The raster excludes its final endpoint, and an unsupported plane returns false after endpoint clamping.
bool BdEditor::MarkLine(BdPoint3 *first, BdPoint3 *second) {
  if (grid) {
    i32 *a[3] = {&first->x, &first->y, &first->z}, *b[3] = {&second->x, &second->y, &second->z};
    const i32 *origin[3] = {&grid->bounds.origin.x, &grid->bounds.origin.y, &grid->bounds.origin.z};
    const i32 *size[3] = {&grid->bounds.size.x, &grid->bounds.size.y, &grid->bounds.size.z};
    for (u32 axis = 0; axis < 3; ++axis)
      if (*a[axis] < *origin[axis])
        *a[axis] = *origin[axis];
    for (u32 axis = 0; axis < 3; ++axis)
      if (*b[axis] < *origin[axis])
        *b[axis] = *origin[axis];
    for (u32 axis = 0; axis < 3; ++axis) {
      i32 limit = static_cast<i32>(static_cast<u32>(*origin[axis]) + static_cast<u32>(*size[axis]));
      if (*a[axis] >= limit)
        *a[axis] = static_cast<i32>(static_cast<u32>(limit) - 1);
    }
    for (u32 axis = 0; axis < 3; ++axis) {
      i32 limit = static_cast<i32>(static_cast<u32>(*origin[axis]) + static_cast<u32>(*size[axis]));
      if (*b[axis] >= limit)
        *b[axis] = static_cast<i32>(static_cast<u32>(limit) - 1);
    }
  }
  i32 first_axis, second_axis, end_first, end_second, fixed;
  if (previous_hit.kind == 32) {
    first_axis = first->y;
    second_axis = first->z;
    end_first = second->y;
    end_second = second->z;
    fixed = first->x;
  } else if (previous_hit.kind == 16) {
    first_axis = first->x;
    second_axis = first->z;
    end_first = second->x;
    end_second = second->z;
    fixed = first->y;
  } else if (previous_hit.kind == 64) {
    first_axis = first->x;
    second_axis = first->y;
    end_first = second->x;
    end_second = second->y;
    fixed = first->z;
  } else
    return false;
  u32 first_delta = static_cast<u32>(end_first) - static_cast<u32>(first_axis);
  u32 second_delta = static_cast<u32>(end_second) - static_cast<u32>(second_axis);
  i32 first_signed = static_cast<i32>(first_delta), second_signed = static_cast<i32>(second_delta);
  i32 first_step = (first_signed > 0) - (first_signed < 0);
  i32 second_step = (second_signed > 0) - (second_signed < 0);
  i32 first_length = static_cast<i32>(first_signed < 0 ? 0u - first_delta : first_delta);
  i32 second_length = static_cast<i32>(second_signed < 0 ? 0u - second_delta : second_delta);
  bool swapped = second_length > first_length;
  i32 major = swapped ? second_length : first_length, minor = swapped ? first_length : second_length;
  i32 error = static_cast<i32>(2u * static_cast<u32>(minor) - static_cast<u32>(major));
  for (i32 count = 0; count < major; ++count) {
    if (previous_hit.kind == 32) {
      left_marks[fixed][second_axis] = 1;
      right_marks[first_axis][second_axis] = 1;
      bottom_marks[fixed][first_axis] = 1;
    } else if (previous_hit.kind == 16) {
      left_marks[first_axis][second_axis] = 1;
      right_marks[fixed][second_axis] = 1;
      bottom_marks[first_axis][fixed] = 1;
    } else {
      left_marks[first_axis][fixed] = 1;
      right_marks[second_axis][fixed] = 1;
      bottom_marks[first_axis][second_axis] = 1;
    }
    while (error > 0) {
      if (swapped)
        first_axis = static_cast<i32>(static_cast<u32>(first_axis) + static_cast<u32>(first_step));
      else
        second_axis = static_cast<i32>(static_cast<u32>(second_axis) + static_cast<u32>(second_step));
      error = static_cast<i32>(static_cast<u32>(error) - 2u * static_cast<u32>(major));
    }
    if (swapped)
      second_axis = static_cast<i32>(static_cast<u32>(second_axis) + static_cast<u32>(second_step));
    else
      first_axis = static_cast<i32>(static_cast<u32>(first_axis) + static_cast<u32>(first_step));
    error = static_cast<i32>(static_cast<u32>(error) + 2u * static_cast<u32>(minor));
  }
  return true;
}

// Draw the selection footprint in back-to-front column, plane, preview and front-column passes.
// The vertical preview uses the source end-height packing; the horizontal preview updates the persistent cursor cell.
void BdEditor::DrawFootprint() {
  bool selected = false;
  if (field_81c != kBdToolLine && field_81c != kBdToolPlane) {
    BdPoint3 selected_tile = selected_hit.tile;
    selected = BdContains(&selected_tile, &selection);
  }
  i32 end_x = static_cast<i32>(static_cast<u32>(selection.origin.x) + selection.size.x);
  i32 end_y = static_cast<i32>(static_cast<u32>(selection.origin.y) + selection.size.y);
  i32 end_z = static_cast<i32>(static_cast<u32>(selection.origin.z) + selection.size.z);
  bool left = selected_hit.kind == 16 || previous_hit.kind == 16 || previous_hit.kind == 2 || selected_hit.kind == 2;
  bool bottom = !left && (selected_hit.kind == 64 || previous_hit.kind == 64 || previous_hit.kind == 4 || selected_hit.kind == 4);
  bool right = !left && !bottom &&
      (selected_hit.kind == 32 || previous_hit.kind == 32 || previous_hit.kind == 1 || selected_hit.kind == 1);
  if (!left && !right && !bottom) {
    for (i32 x = selection.origin.x; x < end_x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            if (selected && height == static_cast<u16>(cursor.z) && x == selected_hit.tile.x &&
                y == selected_hit.tile.y) {
              BdCell preview;
              preview.link_height_kind = (preview.link_height_kind & kBdCellNextMask) |
                  ((height & kBdCellHeightValueMask) << kBdCellHeightShift) |
                  ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
              renderer->DrawHighlight(&screen, &preview, placement_tint);
            } else {
              u8 planes = GetPlaneMask(index, &column_tile);
              renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            }
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
    return;
  }
  BdPoint3 tile;
  BdPoint origin, a, b, c, d, first, second;
  if (left) {
    for (i32 y = selection.origin.y; y < cursor.y; ++y) {
      for (i32 x = selection.origin.x; x < end_x; ++x) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
    tile = {selection.origin.x, cursor.y, selection.origin.z};
    ProjectTile(&tile, &origin);
    renderer->DrawPlane(&origin, static_cast<i32>((0u - static_cast<u32>(selection.size.x)) << (zoom_shift & 31)),
        static_cast<i32>(static_cast<u32>(selection.size.y) << ((static_cast<u32>(zoom_shift) - 1) & 31)), 0,
        static_cast<i32>((0u - static_cast<u32>(selection.size.z)) << (zoom_shift & 31)), 1, BdConstructLeftPlane);
    tile = {end_x, cursor.y, selection.origin.z};
    ProjectTile(&tile, &a);
    tile = {selection.origin.x, cursor.y, selection.origin.z};
    ProjectTile(&tile, &b);
    tile = {end_x, cursor.y, end_z};
    ProjectTile(&tile, &c);
    tile = {selection.origin.x, cursor.y, end_z};
    ProjectTile(&tile, &d);
    first = a;
    second = b;
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueShadow);
    first = b;
    second = d;
    renderer->DrawSegment(&first, &second, kBdFootprintBlueHighlight);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueShadow);
    for (i32 x = selection.origin.x; x < end_x; ++x) {
      i32 y = cursor.y;
      u32 index = grid->render_columns[x][y];
      if (index) {
        BdPoint3 column_tile = {x, y, 0};
        BdPoint base, screen;
        ProjectTile(&column_tile, &base);
        screen.x = base.x;
        while (index) {
          u32 header = grid->cells[index].link_height_kind,
              height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
          if (height >= static_cast<u32>(end_z))
            break;
          column_tile.z = static_cast<i32>(height);
          screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
          if (x == selected_hit.tile.x && height == static_cast<u16>(selected_hit.tile.z)) {
            BdCell preview;
            preview.link_height_kind = (preview.link_height_kind & kBdCellNextMask) |
                ((static_cast<u32>(end_z) & kBdCellHeightValueMask) << kBdCellHeightShift) |
                ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
            renderer->DrawHighlight(&screen, &preview, placement_tint);
          } else {
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
          }
          index = grid->cells[index].link_height_kind & kBdCellNextMask;
        }
      }
    }
    tile = {end_x, cursor.y, selection.origin.z};
    ProjectTile(&tile, &a);
    tile = {selection.origin.x, cursor.y, selection.origin.z};
    ProjectTile(&tile, &b);
    tile = {end_x, cursor.y, end_z};
    ProjectTile(&tile, &c);
    tile = {selection.origin.x, cursor.y, end_z};
    ProjectTile(&tile, &d);
    first = c;
    second = d;
    first.x = static_cast<i32>(static_cast<u32>(first.x) - 1);
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueShadow);
    first = a;
    second = c;
    first.x = static_cast<i32>(static_cast<u32>(first.x) - 2);
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - 2);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueMidtone);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueMidtone);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintBlueShadow);
    for (i32 y = static_cast<i32>(static_cast<u32>(cursor.y) + 1); y < end_y; ++y) {
      for (i32 x = selection.origin.x; x < end_x; ++x) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
  } else if (right) {
    for (i32 x = selection.origin.x; x < cursor.x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
    tile = {cursor.x, selection.origin.y, selection.origin.z};
    ProjectTile(&tile, &origin);
    renderer->DrawPlane(&origin, 0, static_cast<i32>((0u - static_cast<u32>(selection.size.z)) << (zoom_shift & 31)),
        static_cast<i32>(static_cast<u32>(selection.size.x) << (zoom_shift & 31)),
        static_cast<i32>(static_cast<u32>(selection.size.y) << ((static_cast<u32>(zoom_shift) - 1) & 31)), 1,
        BdConstructRightPlane);
    tile = {cursor.x, selection.origin.y, selection.origin.z};
    ProjectTile(&tile, &a);
    tile = {cursor.x, end_y, selection.origin.z};
    ProjectTile(&tile, &b);
    tile = {cursor.x, selection.origin.y, end_z};
    ProjectTile(&tile, &c);
    tile = {cursor.x, end_y, end_z};
    ProjectTile(&tile, &d);
    first = a;
    second = b;
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkMidtone);
    first = a;
    second = c;
    renderer->DrawSegment(&first, &second, kBdFootprintPinkShadow);
    first.x = static_cast<i32>(static_cast<u32>(first.x) - 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkMidtone);
    for (i32 y = selection.origin.y; y < end_y; ++y) {
      i32 x = cursor.x;
      u32 index = grid->render_columns[x][y];
      if (index) {
        BdPoint3 column_tile = {x, y, 0};
        BdPoint base, screen;
        ProjectTile(&column_tile, &base);
        screen.x = base.x;
        while (index) {
          u32 header = grid->cells[index].link_height_kind,
              height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
          if (height >= static_cast<u32>(end_z))
            break;
          column_tile.z = static_cast<i32>(height);
          screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
          if (y == selected_hit.tile.y && height == static_cast<u16>(selected_hit.tile.z)) {
            BdCell preview;
            preview.link_height_kind = (preview.link_height_kind & kBdCellNextMask) |
                ((static_cast<u32>(end_z) & kBdCellHeightValueMask) << kBdCellHeightShift) |
                ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
            renderer->DrawHighlight(&screen, &preview, placement_tint);
          } else {
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
          }
          index = grid->cells[index].link_height_kind & kBdCellNextMask;
        }
      }
    }
    tile = {cursor.x, selection.origin.y, selection.origin.z};
    ProjectTile(&tile, &a);
    tile = {cursor.x, end_y, selection.origin.z};
    ProjectTile(&tile, &b);
    tile = {cursor.x, selection.origin.y, end_z};
    ProjectTile(&tile, &c);
    tile = {cursor.x, end_y, end_z};
    ProjectTile(&tile, &d);
    first = c;
    second = d;
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkHighlight);
    first.x = static_cast<i32>(static_cast<u32>(first.x) - 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkMidtone);
    first = d;
    second = b;
    first.x = static_cast<i32>(static_cast<u32>(first.x) - 2);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - 2);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkMidtone);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkShadow);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintPinkShadow);
    for (i32 x = static_cast<i32>(static_cast<u32>(cursor.x) + 1); x < end_x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
  } else {
    for (i32 x = selection.origin.x; x < end_x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(cursor.z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
    tile = {selection.origin.x, selection.origin.y, cursor.z};
    ProjectTile(&tile, &origin);
    i32 vertical = static_cast<i32>(static_cast<u32>(selection.size.y) << ((static_cast<u32>(zoom_shift) - 1) & 31));
    renderer->DrawPlane(&origin, static_cast<i32>(static_cast<u32>(selection.size.x) << (zoom_shift & 31)), vertical,
        static_cast<i32>((0u - static_cast<u32>(selection.size.x)) << (zoom_shift & 31)), vertical, 1,
        BdConstructFlatPlane);
    tile = {end_x, selection.origin.y, cursor.z};
    ProjectTile(&tile, &a);
    tile = {selection.origin.x, selection.origin.y, cursor.z};
    ProjectTile(&tile, &b);
    tile = {selection.origin.x, end_y, cursor.z};
    ProjectTile(&tile, &c);
    tile = {end_x, end_y, cursor.z};
    ProjectTile(&tile, &d);
    first = a;
    second = b;
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenShadow);
    first = b;
    second = c;
    first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenMidtone);
    cursor_cell.link_height_kind = (cursor_cell.link_height_kind & kBdCellWithoutHeight) |
        ((static_cast<u32>(cursor.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
    for (i32 x = selection.origin.x; x < end_x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        while (index &&
            ((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) <
                static_cast<u16>(cursor.z))
          index = grid->cells[index].link_height_kind & kBdCellNextMask;
        if (index) {
          tile = {x, y, cursor.z};
          ProjectTile(&tile, &origin);
          u32 height = (grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask;
          if (height == static_cast<u32>(cursor.z)) {
            if (x == selected_hit.tile.x && y == selected_hit.tile.y)
              renderer->DrawHighlight(&origin, &cursor_cell, placement_tint);
            else {
              u8 planes = GetPlaneMask(index, &tile);
              renderer->DrawMaskedCell(&origin, &grid->cells[index], planes);
            }
          }
        }
      }
    }
    tile = {end_x, selection.origin.y, cursor.z};
    ProjectTile(&tile, &a);
    tile = {selection.origin.x, selection.origin.y, cursor.z};
    ProjectTile(&tile, &b);
    tile = {selection.origin.x, end_y, cursor.z};
    ProjectTile(&tile, &c);
    tile = {end_x, end_y, cursor.z};
    ProjectTile(&tile, &d);
    first = a;
    second = d;
    renderer->DrawSegment(&first, &second, kBdFootprintGreenHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenMidtone);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenMidtone);
    first = d;
    second = c;
    renderer->DrawSegment(&first, &second, kBdFootprintGreenHighlight);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenShadow);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + 1);
    renderer->DrawSegment(&first, &second, kBdFootprintGreenShadow);
    for (i32 x = selection.origin.x; x < end_x; ++x) {
      for (i32 y = selection.origin.y; y < end_y; ++y) {
        u32 index = grid->render_columns[x][y];
        while (index &&
            ((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) <
                static_cast<u16>(cursor.z))
          index = grid->cells[index].link_height_kind & kBdCellNextMask;
        if (index) {
          BdPoint3 column_tile = {x, y, 0};
          BdPoint base, screen;
          ProjectTile(&column_tile, &base);
          screen.x = base.x;
          while (index) {
            u32 header = grid->cells[index].link_height_kind,
                height = (header >> kBdCellHeightShift) & kBdCellHeightValueMask;
            if (height >= static_cast<u32>(end_z))
              break;
            column_tile.z = static_cast<i32>(height);
            screen.y = static_cast<i32>(static_cast<u32>(base.y) - (height << (zoom_shift & 31)));
            u8 planes = GetPlaneMask(index, &column_tile);
            renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
            index = grid->cells[index].link_height_kind & kBdCellNextMask;
          }
        }
      }
    }
  }
}

u16 BdTextureWidth, BdTextureHeight;

// Return the editor's borrowed grid pointer without changing its reference count.
BdGrid *BdEditor::GetGrid() {
  return grid;
}

// Test the three half-open coordinate intervals of an editor volume. Empty or reversed intervals contain no tile.
bool BdContains(const BdPoint3 *point, const BdVolume *volume) {
  return point->x >= volume->origin.x && point->x < volume->origin.x + volume->size.x && point->y >= volume->origin.y &&
      point->y < volume->origin.y + volume->size.y && point->z >= volume->origin.z &&
      point->z < volume->origin.z + volume->size.z;
}

// Invert the selection's left, right or bottom projection while preserving the cursor's locked axis.
// Automatic picking publishes a tile only on success; an explicit face publishes it even outside the selection.
bool BdEditor::PickSelectionFace(const BdPoint *screen, BdPoint3 *tile, u16 *face) {
  BdPoint3 picked = cursor;
  i32 x = screen->x, y = screen->y;
  u16 selected = *face;
  bool automatic = selected == 0;
  if (automatic) {
    if (x < left_anchor.x)
      selected = 16;
    else if (x > right_anchor.x)
      selected = 32;
    else if (y > bottom_anchor.y)
      selected = 64;
    else
      return false;
    *face = selected;
  }
  switch (selected) {
  case 16: {
    i32 distance = left_anchor.x - x;
    picked.x = selection.origin.x + (distance >> zoom_shift);
    picked.z = selection.origin.z + ((distance - 2 * (y - left_anchor.y)) >> (zoom_shift + 1));
    break;
  }
  case 32: {
    i32 distance = x - right_anchor.x;
    picked.y = selection.origin.y + (distance >> zoom_shift);
    picked.z = selection.origin.z + ((distance - 2 * (y - right_anchor.y)) >> (zoom_shift + 1));
    break;
  }
  case 64: {
    i32 horizontal = x - bottom_anchor.x, vertical = 2 * (y - bottom_anchor.y);
    picked.x = selection.origin.x + ((vertical - horizontal) >> (zoom_shift + 1));
    picked.y = selection.origin.y + ((horizontal + vertical) >> (zoom_shift + 1));
    break;
  }
  default:
    return false;
  }
  if (automatic) {
    if (!BdContains(&picked, &selection)) {
      *face = 0;
      return false;
    }
    *tile = picked;
    return true;
  }
  *tile = picked;
  return BdContains(&picked, &selection);
}

// Hit test enabled resize handles with inclusive rectangle edges.
bool BdEditor::HitTestHandle(const BdPoint *screen, u16 *handle) {
  i32 x = screen->x, y = screen->y;
  if ((handle_images[2] && x >= handle_bounds[2].left && y >= handle_bounds[2].top && x <= handle_bounds[2].right &&
          y <= handle_bounds[2].bottom) ||
      (handle_images[3] && x >= handle_bounds[3].left && y >= handle_bounds[3].top && x <= handle_bounds[3].right &&
          y <= handle_bounds[3].bottom)) {
    *handle = 4;
    return true;
  }
  if (handle_images[0] && x >= handle_bounds[0].left && y >= handle_bounds[0].top && x <= handle_bounds[0].right &&
      y <= handle_bounds[0].bottom) {
    *handle = 2;
    return true;
  }
  if (handle_images[1] && x >= handle_bounds[1].left && y >= handle_bounds[1].top && x <= handle_bounds[1].right &&
      y <= handle_bounds[1].bottom) {
    *handle = 1;
    return true;
  }
  return false;
}

// Enclose both rectangles by taking the outermost edges. Report whether both resulting spans are nonzero.
bool BdRect::Unite(const BdRect *first, const BdRect *second) {
  left = first->left < second->left ? first->left : second->left;
  top = first->top < second->top ? first->top : second->top;
  right = first->right > second->right ? first->right : second->right;
  bottom = first->bottom > second->bottom ? first->bottom : second->bottom;
  return left != right && top != bottom;
}

// Project a tile relative to the existing selection origin onto the editor's isometric screen anchor.
// Unsigned arithmetic preserves x86 wrapping and masked shift counts without signed-shift undefined behavior.
i32 BdEditor::ProjectTile(const BdPoint3 *tile, BdPoint *screen) {
  u32 dx = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  u32 dy = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 dz = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  screen->x = static_cast<i32>(static_cast<u32>(projection_anchor.x) + ((dy - dx) << (zoom_shift & 31)));
  u32 vertical = (dx + dy - 2 * dz) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(projection_anchor.y) + vertical);
  return screen->y;
}

// Decode the face direction into its tilt and horizontal octant, preserving stores through aliased outputs.
// The two upright categories and unknown upper categories differ in their final output-pointer result.
f32 *BD_STDCALL BdFaceNormal(u8 direction, f32 *x, f32 *y, f32 *z) {
  u8 group = direction >> 3;
  if (group == 2 || group == 7) {
    *z = 1;
    *x = 0;
    *y = 0;
    return y;
  }
  if (group >= 8) {
    *z = 1;
    *y = 0;
    *x = 0;
    return x;
  }
  static const f32 vertical[8] = {0, 0.7071067690849304f, 1, 0.5f, 0.8660253882408142f, -1, 0, 1};
  static const f32 horizontal[8] = {1, 0.7071067690849304f, 0, 0.8660253882408142f, 0.5f, 0, 1, 0};
  static const f32 octants[8][2] = {{0.7071067690849304f, 0.7071067690849304f}, {0, 1},
      {-0.7071067690849304f, 0.7071067690849304f}, {-1, 0}, {-0.7071067690849304f, -0.7071067690849304f}, {0, -1},
      {0.7071067690849304f, -0.7071067690849304f}, {1, 0}};
  *y = 0;
  *x = 0;
  *z = 0;
  *x = octants[direction & 7][0];
  *y = octants[direction & 7][1];
  *z = vertical[group];
  *x *= horizontal[group];
  *y *= horizontal[group];
  return y;
}

// Clamp each coordinate against the volume's lower bound and its nonempty half-open upper bound.
// Retain lower-only clamping for empty/reversed extents and read each axis after the preceding stores.
BdPoint3 *BdClampPoint(BdPoint3 *point, const BdVolume *volume) {
  if (point->x < volume->origin.x)
    point->x = volume->origin.x;
  i32 size = volume->size.x, end = size + volume->origin.x;
  if (point->x >= end && size > 0)
    point->x = end - 1;
  if (point->y < volume->origin.y)
    point->y = volume->origin.y;
  size = volume->size.y;
  end = size + volume->origin.y;
  if (point->y >= end && size > 0)
    point->y = end - 1;
  if (point->z < volume->origin.z)
    point->z = volume->origin.z;
  size = volume->size.z;
  end = size + volume->origin.z;
  if (point->z >= end && size > 0)
    point->z = end - 1;
  return point;
}

// Translate both rectangle corners by the same point, retaining the source's observed vertical-coordinate result.
// Use unsigned additions to preserve the original wrapping without signed-overflow behavior.
i32 BdRect::Offset(const BdPoint *point) {
  i32 y = point->y, x = point->x;
  left = static_cast<i32>(static_cast<u32>(left) + static_cast<u32>(point->x));
  right = static_cast<i32>(static_cast<u32>(right) + static_cast<u32>(x));
  top = static_cast<i32>(static_cast<u32>(top) + static_cast<u32>(y));
  bottom = static_cast<i32>(static_cast<u32>(bottom) + static_cast<u32>(y));
  return y;
}

// Add the selection-volume edge planes and the planes adjoining the selected tile at the current cursor height.
// Keep the unused original cell-index argument and swap horizontal bits only for grid rotation three.
u8 BdEditor::GetPlaneMask(u32, const BdPoint3 *tile) {
  u8 planes = 0, horizontal = grid->rotation == 3 ? 1 : 2;
  i32 x = tile->x, y = tile->y, z = tile->z, cursor_z = static_cast<u16>(cursor.z);
  if (x == selection.origin.x + selection.size.x - 1 ||
      (x == selected_hit.tile.x - 1 && y == selected_hit.tile.y && z == cursor_z))
    planes = grid->rotation == 3 ? 2 : 1;
  if (y == selection.origin.y + selection.size.y - 1 ||
      (x == selected_hit.tile.x && y == selected_hit.tile.y - 1 && z == cursor_z))
    planes |= horizontal;
  if (z == selection.origin.z + selection.size.z - 1 ||
      (x == selected_hit.tile.x && y == selected_hit.tile.y && z == cursor_z - 1))
    planes |= 4;
  return planes;
}

// Compute the packed texture tile offsets relative to the cursor and current grid rotation.
// The source uses signed low-word arithmetic for axis-aligned faces and float-spilled projected normals elsewhere.
u8 BdEditor::TextureOffset(u8 direction, const BdPoint3 *tile) {
  i32 dx = static_cast<i32>(static_cast<u32>(tile->x) - static_cast<u32>(cursor.x));
  i32 dy = static_cast<i32>(static_cast<u32>(tile->y) - static_cast<u32>(cursor.y));
  i32 dz = static_cast<i32>(static_cast<u32>(tile->z) - static_cast<u32>(cursor.z));
  u8 horizontal = 0, vertical = 0, group = direction & 0xF8;
  i16 short_x = static_cast<i16>(dx), short_y = static_cast<i16>(dy);
  if (group == 16 || group == 56) {
    i16 along;
    i32 across;
    switch (grid->rotation) {
    case 0:
      along = short_x;
      across = static_cast<i16>(static_cast<u16>(-static_cast<u16>(dy) - 1));
      break;
    case 1:
      along = static_cast<i16>(static_cast<u16>(static_cast<u16>(dy) + 1));
      across = static_cast<i16>(static_cast<u16>(-static_cast<u16>(dx) + 1));
      break;
    case 2:
      along = static_cast<i16>(static_cast<u16>(-static_cast<u16>(dx) + 1));
      across = static_cast<i16>(static_cast<u16>(-static_cast<u16>(dy)));
      break;
    case 3:
      along = static_cast<i16>(static_cast<u16>(-static_cast<u16>(dy)));
      across = short_x;
      break;
    default:
      return 0;
    }
    if (BdTextureWidth) {
      i32 amount = grid->rotation == 0 ? BdTextureWidth + 16 * across : BdTextureWidth - 16 * across;
      horizontal = static_cast<u8>(amount % BdTextureWidth);
    }
    if (BdTextureHeight)
      vertical = static_cast<u8>((BdTextureHeight - 16 * along) % BdTextureHeight);
  } else if (direction == 1 || direction == 7) {
    i32 closest = short_x;
    i32 abs_x = short_x < 0 ? -static_cast<i32>(short_x) : short_x;
    i32 abs_y = short_y < 0 ? -static_cast<i32>(short_y) : short_y;
    if (abs_x <= abs_y)
      closest = short_y;
    if (BdTextureWidth) {
      i32 amount = direction == 1 ? -16 * closest : BdTextureWidth + 16 * closest;
      horizontal = static_cast<u8>(amount % BdTextureWidth);
    }
    if (BdTextureHeight) {
      u32 amount = 2u * (BdTextureHeight + 8u * (static_cast<u32>(cursor.z) - static_cast<u32>(tile->z))) -
          2u * BdTextureHeight / 16u;
      vertical = static_cast<u8>(static_cast<i32>(amount) % BdTextureHeight);
    }
  } else {
    u32 length = static_cast<u32>(dx) * static_cast<u32>(dx) + static_cast<u32>(dy) * static_cast<u32>(dy) +
        static_cast<u32>(dz) * static_cast<u32>(dz);
    if (length) {
      f32 normal_x, normal_y, normal_z;
      BdFaceNormal(direction, &normal_x, &normal_y, &normal_z);
      f64 horizontal_length = static_cast<f64>(normal_x) * normal_x + static_cast<f64>(normal_y) * normal_y;
      if (horizontal_length > 0.01) {
        f64 inverse = 1.0 / sqrt(horizontal_length), raw_x = inverse * normal_x;
        f32 axis_y = static_cast<f32>(inverse * normal_y), axis_x = static_cast<f32>(raw_x);
        f64 cross = static_cast<f64>(axis_y) * normal_y - raw_x * normal_x;
        f64 length_squared = (raw_x * raw_x * normal_z + static_cast<f64>(axis_y) * axis_y * normal_z) * normal_z +
            cross * cross;
        if (length_squared > 0.01) {
          f64 normal_inverse = 1.0 / sqrt(length_squared);
          f32 vertical_x = static_cast<f32>(axis_x * normal_inverse * normal_z);
          f32 vertical_y = static_cast<f32>(axis_y * normal_inverse * normal_z);
          f64 vertical_z = normal_inverse * (static_cast<f64>(axis_y) * normal_y - static_cast<f64>(axis_x) * normal_x);
          f32 point_x = static_cast<f32>(dx), point_y = static_cast<f32>(dy), point_z = static_cast<f32>(dz);
          if (BdTextureWidth) {
            i32 amount = static_cast<i8>(static_cast<u8>(static_cast<i64>(
                (static_cast<f64>(axis_x) * point_y + static_cast<f64>(axis_y) * point_x) * 16.0 + 0.5)));
            horizontal = static_cast<u8>(amount % BdTextureWidth);
          }
          if (BdTextureHeight) {
            i32 amount = static_cast<i8>(static_cast<u8>(static_cast<i64>(
                (vertical_z * point_z + static_cast<f64>(vertical_y) * point_y +
                    static_cast<f64>(vertical_x) * point_x) * 16.0 + 0.5)));
            vertical = static_cast<u8>(amount % BdTextureHeight);
          }
        }
      }
    }
  }
  return static_cast<u8>((horizontal & 0xF0) | (vertical >> 4));
}

// Update one existing cell's second material and its computed texture offset, then reinsert the copied cell.
// An empty column or missing height leaves the grid untouched.
bool BdEditor::PaintSecondTile(const BdPoint3 *tile, u8 part, u8 direction, BdAsset *value) {
  i32 index = grid->FindCell(tile, 0, 0);
  if (!index)
    return false;
  BdCell cell = grid->cells[index];
  i8 offset = static_cast<i8>(TextureOffset(direction, tile));
  grid->SetSecondMaterialOffset(&cell, part, value, offset);
  return grid->InsertCell(tile, &cell);
}

// Paint the selected face with a catalog asset, spreading larger images over adjacent tiles.
// Image dimensions select the original horizontal or vertical traversal and shared texture-offset state.
bool BdEditor::PaintSecond(const BdHit *hit, u32 id) {
  BdPoint3 tile = hit->tile;
  SetCursor(&tile);
  if (!id) {
    BdCell cell = grid->cells[hit->cell];
    grid->SetSecondMaterial(&cell, hit->face, nullptr);
    return grid->InsertCell(&cursor, &cell);
  }
  BdAsset *asset = nullptr;
  if (!catalog->GetAsset(id, &asset))
    return false;
  i32 width = asset->icon_bounds.right - asset->icon_bounds.left;
  i32 height = asset->icon_bounds.bottom - asset->icon_bounds.top;
  BdTextureWidth = static_cast<u16>(width);
  BdTextureHeight = static_cast<u16>(height);
  if (width == 16 && height == 16) {
    BdCell cell = grid->cells[hit->cell];
    grid->SetSecondMaterial(&cell, hit->face, asset);
    grid->InsertCell(&cursor, &cell);
    return true;
  }
  u32 packed = grid->cells[hit->cell].link_height_kind;
  u8 face_flags = static_cast<u8>(BdCellFaces[(packed >> kBdCellKindShift) & kBdCellKindValueMask][hit->face].flags);
  u8 direction = static_cast<u8>((face_flags & 0xF8) | ((face_flags + 2 * (packed >> kBdCellRotationShift)) & 7));
  f32 normal_x, normal_y, normal_z;
  BdFaceNormal(direction, &normal_x, &normal_y, &normal_z);
  i32 rows = height >> 4, columns = width >> 4;
  if (normal_z <= 0.01) {
    if (normal_x > 0.01) {
      for (i32 x = 0; x < columns; ++x)
        for (i32 z = 0; z < rows; ++z) {
          BdPoint3 point = {tile.x, static_cast<i32>(static_cast<u32>(tile.y) + x),
              static_cast<i32>(static_cast<u32>(tile.z) + rows - 1u - z)};
          if (BdContains(&point, &grid->bounds))
            PaintSecondTile(&point, hit->face, direction, asset);
        }
    } else if (normal_y > 0.01) {
      for (i32 x = 0; x < columns; ++x)
        for (i32 z = 0; z < rows; ++z) {
          BdPoint3 point = {static_cast<i32>(static_cast<u32>(tile.x) - x), tile.y,
              static_cast<i32>(static_cast<u32>(tile.z) + rows - 1u - z)};
          if (BdContains(&point, &grid->bounds))
            PaintSecondTile(&point, hit->face, direction, asset);
        }
    }
  } else {
    i32 start_x = static_cast<i16>(tile.x), start_y = static_cast<i16>(tile.y);
    bool rotated = grid->rotation == 1 || grid->rotation == 3;
    i32 end_x = static_cast<i16>(static_cast<u16>(static_cast<u32>(tile.x) - (rotated ? columns : rows)));
    i32 end_y = static_cast<i16>(static_cast<u16>(static_cast<u32>(tile.y) + (rotated ? rows : columns)));
    for (i32 x = start_x; x > end_x; --x)
      for (i32 y = start_y; y < end_y; ++y) {
        BdPoint3 point = {x, y, tile.z};
        if (BdContains(&point, &grid->bounds))
          PaintSecondTile(&point, hit->face, direction, asset);
      }
  }
  return true;
}

// Find the object at or adjacent to the hit face, remove it at its actual origin, and return its definition ID.
// Resolve the original projected asset before the descriptor releases its retained definitions.
u32 BdEditor::EraseObject(const BdHit *hit) {
  BdPoint3 tile = hit->tile;
  const BdCell *cell = &grid->cells[hit->cell];
  if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind) {
    u32 packed = cell->link_height_kind;
    u8 face_flags = static_cast<u8>(BdCellFaces[(packed >> kBdCellKindShift) & kBdCellKindValueMask][hit->face].flags);
    u8 direction = static_cast<u8>((face_flags & 0xf8) | ((face_flags + 2 * (packed >> kBdCellRotationShift)) & 7));
    f32 x, y, z;
    BdFaceNormal(direction, &x, &y, &z);
    if ((packed & kBdCellKindMask) != kBdCellGroundKind) {
      if (z >= 0.9)
        tile.z = static_cast<i32>(static_cast<u32>(tile.z) + 1);
      else if (x > 0.9)
        tile.x = static_cast<i32>(static_cast<u32>(tile.x) + 1);
      else if (x < -0.9)
        tile.x = static_cast<i32>(static_cast<u32>(tile.x) - 1);
      else if (y > 0.9)
        tile.y = static_cast<i32>(static_cast<u32>(tile.y) + 1);
      else if (y < -0.9)
        tile.y = static_cast<i32>(static_cast<u32>(tile.y) - 1);
    }
    if (tile.z < 0 || tile.x < 0 || tile.y < 0 || tile.z >= grid->height || tile.x >= grid->side ||
        tile.y >= grid->side)
      return 0;
    i32 index = grid->FindCell(&tile, 1, 0);
    if (!index)
      return 0;
    cell = &grid->cells[index];
    if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind) {
      tile.z = static_cast<i32>(static_cast<u32>(tile.z) + 1);
      index = grid->FindCell(&tile, 1, 0);
      if (!index)
        return 0;
      cell = &grid->cells[index];
    }
  }
  if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind)
    return 0;
  BdObjectInfo info;
  grid->GetObjectInfo(cell, &info);
  grid->GetObjectOrigin(&tile, &tile, cell);
  BdCell replacement;
  replacement.Reset();
  replacement.link_height_kind = (replacement.link_height_kind & kBdCellWithoutHeight) |
      ((static_cast<u32>(tile.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
  grid->InsertCell(&tile, &replacement);
  grid->DeleteCell(&tile);
  BdAsset *asset = nullptr;
  info.GetAsset(0, &asset);
  return asset ? asset->definition_id : 0;
}

// Place or remove an object at the hit tile using its actual catalog dimensions and projected assets.
// Preserve adjacent-face selection, optional height adjustment and the grid's original insertion/undo ordering.
void BdEditor::PlaceObject(const BdHit *hit, u32 id) {
  BdPoint3 tile = hit->tile;
  u32 index = hit->cell, rotation = grid->rotation;
  const BdCell *cell = &grid->cells[index];
  bool inserted = false, deleting = false;
  if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind) {
    u32 packed = cell->link_height_kind;
    u8 face_flags = static_cast<u8>(BdCellFaces[(packed >> kBdCellKindShift) & kBdCellKindValueMask][hit->face].flags);
    u8 direction = static_cast<u8>((face_flags & 0xf8) | ((face_flags + 2 * (packed >> kBdCellRotationShift)) & 7));
    f32 x, y, z;
    BdFaceNormal(direction, &x, &y, &z);
    if ((packed & kBdCellKindMask) != kBdCellGroundKind) {
      if (z >= 0.9)
        tile.z = static_cast<i32>(static_cast<u32>(tile.z) + 1);
      else if (x > 0.9)
        tile.x = static_cast<i32>(static_cast<u32>(tile.x) + 1);
      else if (x < -0.9)
        tile.x = static_cast<i32>(static_cast<u32>(tile.x) - 1);
      else if (y > 0.9)
        tile.y = static_cast<i32>(static_cast<u32>(tile.y) + 1);
      else if (y < -0.9)
        tile.y = static_cast<i32>(static_cast<u32>(tile.y) - 1);
      else
        return;
    }
    if (tile.z < 0 || tile.x < 0 || tile.y < 0 || tile.z >= grid->height || tile.x >= grid->side ||
        tile.y >= grid->side)
      return;
    BdCell empty;
    empty.Reset();
    empty.link_height_kind = (empty.link_height_kind & kBdCellWithoutHeightOrKind) | kBdCellObjectKind |
        ((static_cast<u32>(tile.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
    inserted = grid->InsertCell(&tile, &empty);
    index = static_cast<u32>(grid->FindCell(&tile, 1, 0));
    if (!index)
      return;
    cell = &grid->cells[index];
    if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind) {
      tile.z = static_cast<i32>(static_cast<u32>(tile.z) + 1);
      empty.link_height_kind = (empty.link_height_kind & kBdCellWithoutHeight) |
          ((static_cast<u32>(tile.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
      inserted = grid->InsertCell(&tile, &empty);
      index = static_cast<u32>(grid->FindCell(&tile, 1, 0));
      if (!index)
        return;
      cell = &grid->cells[index];
    }
  }
  if ((cell->link_height_kind & kBdCellKindMask) != kBdCellObjectKind)
    return;
  SetCursor(&tile);
  BdObjectInfo info;
  if (id) {
    if (!BdGetObjectInfo(id, &info))
      return;
    i32 adjustment = info.size.z ? static_cast<i32>(static_cast<u32>(info.size.z) - 1) / 2 : 0;
    i32 shifted = static_cast<i32>(static_cast<u32>(tile.z) + static_cast<u32>(adjustment));
    if (shifted >= grid->height)
      adjustment = static_cast<i32>(static_cast<u32>(grid->height) - static_cast<u32>(tile.z));
    else if (shifted < 0)
      adjustment = static_cast<i32>(0u - static_cast<u32>(tile.z));
    if (adjustment) {
      if (inserted)
        grid->Undo();
      BdCell empty;
      empty.Reset();
      tile.z = static_cast<i32>(static_cast<u32>(tile.z) + static_cast<u32>(adjustment));
      empty.link_height_kind = (empty.link_height_kind & kBdCellWithoutHeightOrKind) | kBdCellObjectKind |
          ((static_cast<u32>(tile.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
      grid->InsertCell(&tile, &empty);
      index = static_cast<u32>(grid->FindCell(&tile, 1, 0));
      if (!index || (grid->cells[index].link_height_kind & kBdCellKindMask) != kBdCellObjectKind)
        return;
    }
  } else {
    if (BdZeroViewBytes(cell) || !BdIsObjectOrigin(cell))
      return;
    deleting = true;
    if (!grid->GetObjectInfo(cell, &info))
      return;
  }
  i32 half_x = info.size.x >> 1, half_y = info.size.y >> 1, half_z = info.size.z >> 1;
  BdPoint3 second = {static_cast<i32>(static_cast<u32>(tile.x) + half_x),
      static_cast<i32>(static_cast<u32>(tile.y) + half_y), static_cast<i32>(static_cast<u32>(tile.z) + half_z)};
  if (info.size.z && !(info.size.z & 1))
    --half_z;
  BdPoint3 first = {static_cast<i32>(static_cast<u32>(tile.x) - half_x),
      static_cast<i32>(static_cast<u32>(tile.y) - half_y), static_cast<i32>(static_cast<u32>(tile.z) - half_z)};
  BdCell value = grid->cells[index];
  if (deleting) {
    grid->DeleteObjectBox(&first, &second, &tile, &value);
  } else {
    for (u32 view = 0; view < 4; ++view) {
      BdAsset *asset = nullptr;
      info.GetAsset(static_cast<i32>(view), &asset);
      if (asset) {
        u32 part = ((rotation + view) & 3) + 1;
        grid->SetFirstMaterial(&value, static_cast<u8>(part), asset);
        value.parts[part].offset_x = 0;
        value.parts[part].offset_y = 0;
      }
    }
    grid->InsertCell(&tile, &value);
    grid->InsertObjectBox(&first, &second, &tile, &value);
  }
}

// Shift a face's vertex mask into a neighboring tile using the source's x, y and z bit arrangements.
// Offsets with squared distance greater than three have no shared vertices.
u32 BdShiftVertexMask(const u32 *mask, const BdPoint3 *offset) {
  u32 x = static_cast<u32>(offset->x), y = static_cast<u32>(offset->y), z = static_cast<u32>(offset->z);
  u32 length = x * x + y * y + z * z, value = *mask;
  if (!length)
    return value;
  if (length > 3)
    return 0;
  if (offset->x == -1)
    value = 2 * (value & 0x444) | ((value >> 1) & 0x111);
  else if (offset->x == 1)
    value = ((value >> 1) & 0x444) | 2 * (value & 0x111);
  if (offset->y == 1)
    value = 2 * ((value & 0x222) | (4 * (value & 0x111)));
  else if (offset->y == -1)
    value = ((value & 0x444) | ((value >> 2) & 0x222)) >> 1;
  if (offset->z == 1)
    return static_cast<u8>(value) >> 4;
  if (offset->z == -1)
    return 16 * (value & 0xF);
  return value;
}

BdPaintEntry BdPaintEntries[128];
i32 BdPaintQueued, BdPaintProcessed;

// A full queue retains all records and returns the observed pending distance.
i32 BdQueuePaint(u32 cell, u8 face, i32 x, i32 y, i32 z) {
  i32 distance = static_cast<i32>(static_cast<u32>(BdPaintQueued) - static_cast<u32>(BdPaintProcessed));
  if (distance >= 127)
    return distance;
  u32 index = static_cast<u32>(BdPaintQueued) & 127;
  BdPaintQueued = static_cast<i32>(static_cast<u32>(BdPaintQueued) + 1);
  BdPaintEntries[index].cell = cell;
  BdPaintEntries[index].face = face;
  BdPaintEntries[index].tile = {x, y, z};
  return static_cast<i32>(index * 20);
}

// Rotate the four source-face vertices into a bit mask for the cell's current orientation.
// Triangle sentinels select the known preceding initialized byte without an out-of-bounds C++ read.
static u32 *BdFaceVertices(u32 *out, const BdFace *face, u8 rotation) {
  *out = 0;
  for (u32 i = 0; i < 4; ++i) {
    i8 vertex = face->vertex[i];
    u8 rotated = vertex < 0 ? (rotation ? BdCellRotations[rotation - 1][11] : 0) :
        BdCellRotations[rotation][vertex];
    *out |= 1u << (rotated & 31);
  }
  return out;
}

// Find the matching visible face in a neighboring cell and replace it only when its previous material matches.
// Queue each changed face after the grid records its actual replacement and texture offset.
void BdEditor::PaintPrimaryNeighbor(BdPoint3 tile, u32 direction, u32 vertices, u8 previous, BdAsset *asset) {
  i32 index = grid->FindCell(&tile, 0, 0);
  if (!index)
    return;
  BdCell *current = &grid->cells[index];
  u32 packed = current->link_height_kind, rotation = packed >> kBdCellRotationShift,
      kind = (packed >> kBdCellKindShift) & kBdCellKindValueMask;
  for (u32 part = 0; part < 8; ++part) {
    const BdFace *face = &BdCellFaces[kind][part];
    if (face->flags == -1)
      return;
    u32 mask;
    BdFaceVertices(&mask, face, static_cast<u8>(rotation));
    u8 face_flags = static_cast<u8>(face->flags);
    u32 face_direction = (face_flags & 0xf8) | ((face_flags + 2 * rotation) & 7);
    if (face_direction == direction && (mask & vertices)) {
      if ((current->face_mask & (1u << part)) && current->parts[part].first == previous) {
        BdCell replacement = *current;
        i8 offset = static_cast<i8>(TextureOffset(static_cast<u8>(direction), &tile));
        grid->SetFirstMaterialOffset(&replacement, static_cast<u8>(part), asset, offset);
        grid->InsertCell(&tile, &replacement);
        BdQueuePaint(static_cast<u32>(index), static_cast<u8>(part), tile.x, tile.y, tile.z);
      }
      return;
    }
  }
}

// Visit the adjacent tiles whose shifted face vertices meet the source face's plane or diagonal-edge rule.
// The unused cell argument and the source's global paint-processing limit remain part of the original call.
void BdEditor::PaintPrimaryNearby(u32, i32 x, i32 y, i32 z, u32 direction, u8 previous,
    f32 normal_x, f32 normal_y, f32 normal_z, u32 vertices, BdAsset *asset) {
  u8 group = static_cast<u8>(direction) >> 3;
  for (i32 dx = x == 0 ? 0 : -1; dx <= 1; ++dx) {
    for (i32 dy = y == 0 ? 0 : -1; dy <= 1; ++dy) {
      for (i32 dz = z == 0 ? 0 : -1; dz <= 1; ++dz) {
        if (!dx && !dy && !dz)
          continue;
        BdPoint3 delta = {dx, dy, dz};
        f32 distance = static_cast<f32>(static_cast<f64>(dz) * normal_z +
            static_cast<f64>(dy) * normal_y + static_cast<f64>(dx) * normal_x);
        u32 shifted = BdShiftVertexMask(&vertices, &delta);
        if (fabs(distance) < static_cast<f64>(0.01f) || ((group == 3 || group == 4) && shifted)) {
          BdPoint3 tile = {static_cast<i32>(static_cast<u32>(x) + dx),
              static_cast<i32>(static_cast<u32>(y) + dy), static_cast<i32>(static_cast<u32>(z) + dz)};
          PaintPrimaryNeighbor(tile, direction, shifted, previous, asset);
        }
        if (BdPaintProcessed > kBdPaintMaximumProcessedFaces)
          return;
      }
      if (BdPaintProcessed > kBdPaintMaximumProcessedFaces)
        return;
    }
    if (BdPaintProcessed > kBdPaintMaximumProcessedFaces)
      return;
  }
}

// Drain the original circular face queue, computing each changed face's rotated vertex mask before propagation.
// Stop after the original processing limit even when more faces remain queued.
i32 BdEditor::ProcessPrimaryPaint(u32 direction, u8 previous, f32 normal_x, f32 normal_y, f32 normal_z,
    u32, BdAsset *asset) {
  BdPaintProcessed = 0;
  while (BdPaintProcessed < BdPaintQueued) {
    BdPaintEntry entry = BdPaintEntries[static_cast<u32>(BdPaintProcessed) & 127];
    u32 packed = grid->cells[entry.cell].link_height_kind, vertices;
    BdFaceVertices(&vertices, &BdCellFaces[(packed >> kBdCellKindShift) & kBdCellKindValueMask][entry.face],
        static_cast<u8>(packed >> kBdCellRotationShift));
    PaintPrimaryNearby(entry.cell, entry.tile.x, entry.tile.y, entry.tile.z, direction, previous,
        normal_x, normal_y, normal_z, vertices, asset);
    ++BdPaintProcessed;
    if (BdPaintProcessed > kBdPaintMaximumProcessedFaces)
      break;
  }
  return BdPaintProcessed;
}

// Paint the selected non-object face with the requested catalog asset and propagate across matching connected faces.
// Texture dimensions, grid change ordering, and the bounded original paint queue remain shared with the editor.
void BdEditor::PaintPrimary(const BdHit *hit, u32 id) {
  if (!id)
    return;
  BdPoint3 tile = hit->tile;
  SetCursor(&tile);
  BdCell *current = &grid->cells[hit->cell];
  if ((current->link_height_kind & kBdCellKindMask) == kBdCellObjectKind)
    return;
  BdAsset *asset = nullptr;
  if (!catalog->GetAsset(id, &asset))
    return;
  BdTextureWidth = static_cast<u16>(static_cast<u32>(asset->icon_bounds.right) - asset->icon_bounds.left);
  BdTextureHeight = static_cast<u16>(static_cast<u32>(asset->icon_bounds.bottom) - asset->icon_bounds.top);
  u8 previous = current->parts[hit->face].first;
  BdMaterials *materials = nullptr;
  BdAsset *old_asset = nullptr;
  grid->GetMaterials(&materials);
  materials->Get(previous, &old_asset);
  materials->Release();
  if (old_asset == asset)
    return;
  u32 packed = current->link_height_kind, vertices;
  const BdFace *face = &BdCellFaces[(packed >> kBdCellKindShift) & kBdCellKindValueMask][hit->face];
  BdFaceVertices(&vertices, face, static_cast<u8>(packed >> kBdCellRotationShift));
  u8 face_flags = static_cast<u8>(face->flags);
  u8 direction = static_cast<u8>((face_flags & 0xf8) | ((face_flags + 2 * (packed >> kBdCellRotationShift)) & 7));
  f32 normal_x, normal_y, normal_z;
  BdFaceNormal(direction, &normal_x, &normal_y, &normal_z);
  BdPaintQueued = 0;
  PaintPrimaryNearby(hit->cell, tile.x, tile.y, tile.z, direction, previous,
      normal_x, normal_y, normal_z, vertices, asset);
  BdCell replacement = grid->cells[hit->cell];
  i8 offset = static_cast<i8>(TextureOffset(direction, &tile));
  grid->SetFirstMaterialOffset(&replacement, hit->face, asset, offset);
  grid->InsertCell(&cursor, &replacement);
  ProcessPrimaryPaint(direction, previous, normal_x, normal_y, normal_z, vertices, asset);
}

// Prepare eight grid counts and step vectors, four enclosing extents, and the three orthographic paint rectangles.
// Keep the original asymmetric size assignments and signed half-span division after wrapped products and additions.
i32 BdEditor::UpdateGridSteps() {
  i32 step = static_cast<i32>(1u << (zoom_shift & 31)), half = step / 2;
  grid_counts[0] = grid_counts[1] = selection.size.z;
  grid_counts[2] = grid_counts[3] = selection.size.y;
  grid_counts[4] = grid_counts[6] = grid_counts[7] = selection.size.x;
  grid_counts[5] = selection.size.y;
  for (u32 i = 0; i < 4; ++i)
    grid_steps[i] = {step, half};
  grid_steps[4] = grid_steps[5] = {0, step};
  grid_steps[6] = grid_steps[7] = {step, half};
  for (u32 i = 0; i < 4; ++i) {
    u32 first = static_cast<u32>(grid_counts[i]), second = static_cast<u32>(grid_counts[i + 4]);
    grid_extents[i].x = static_cast<i32>(
        first * static_cast<u32>(grid_steps[i + 4].x) + second * static_cast<u32>(grid_steps[i].x) + 12);
    grid_extents[i].y = static_cast<i32>(
        second * static_cast<u32>(grid_steps[i].y) + first * static_cast<u32>(grid_steps[i + 4].y) + 14);
  }
  for (u32 i = 0; i < 3; ++i)
    grid_extents[i].y = static_cast<i32>(static_cast<u32>(grid_extents[i].y) + step);
  for (u32 i = 0; i < 2; ++i)
    grid_extents[i].x = static_cast<i32>(static_cast<u32>(grid_extents[i].x) + 2u * step);
  u32 lx = static_cast<u32>(left_anchor.x), ly = static_cast<u32>(left_anchor.y);
  u32 rx = static_cast<u32>(right_anchor.x), ry = static_cast<u32>(right_anchor.y);
  u32 bx = static_cast<u32>(bottom_anchor.x), by = static_cast<u32>(bottom_anchor.y);
  view_bounds[2] = {static_cast<i32>(lx - grid_extents[0].x), static_cast<i32>(ly - grid_extents[0].y),
      static_cast<i32>(lx + 2u * step), static_cast<i32>(ly + grid_extents[0].x / 2)};
  view_bounds[3] = {static_cast<i32>(rx - 2u * step), static_cast<i32>(ry - grid_extents[1].y),
      static_cast<i32>(rx + grid_extents[1].x), static_cast<i32>(ry + grid_extents[1].x / 2)};
  view_bounds[4] = {static_cast<i32>(bx - grid_extents[2].x / 2), static_cast<i32>(by - step),
      static_cast<i32>(bx + grid_extents[2].x / 2), static_cast<i32>(by + grid_extents[2].y)};
  return grid_extents[2].y;
}

// Draw both isometric grid directions, selecting the major color every eighth original world coordinate.
// The first direction intentionally uses the y count with the x-origin color phase; do-while loops also draw zero spans.
void BdEditor::DrawIsoGrid() {
  BdPoint first = {static_cast<i32>(static_cast<u32>(projection_anchor.x) + 1), projection_anchor.y};
  BdPoint second = {static_cast<i32>(static_cast<u32>(first.x) + static_cast<u32>(grid_counts[3]) * grid_steps[7].x),
      static_cast<i32>(static_cast<u32>(first.y) + static_cast<u32>(grid_counts[3]) * grid_steps[7].y)};
  u32 i = 0;
  do {
    u32 color = grid_colors[((i + static_cast<u32>(selection.origin.x)) & 7) == 0];
    renderer->DrawSegment(&first, &second, color);
    first.x = static_cast<i32>(static_cast<u32>(first.x) - grid_steps[7].x);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[7].y);
    second.x = static_cast<i32>(static_cast<u32>(second.x) - grid_steps[7].x);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[7].y);
    ++i;
  } while (i <= static_cast<u32>(grid_counts[3]));
  first = projection_anchor;
  second = {static_cast<i32>(static_cast<u32>(first.x) - static_cast<u32>(grid_counts[7]) * grid_steps[3].x),
      static_cast<i32>(static_cast<u32>(first.y) + static_cast<u32>(grid_counts[7]) * grid_steps[3].y)};
  i = 0;
  do {
    u32 color = grid_colors[((i + static_cast<u32>(selection.origin.y)) & 7) == 0];
    renderer->DrawSegment(&first, &second, color);
    first.x = static_cast<i32>(static_cast<u32>(first.x) + grid_steps[3].x);
    first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[3].y);
    second.x = static_cast<i32>(static_cast<u32>(second.x) + grid_steps[3].x);
    second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[3].y);
    ++i;
  } while (i <= static_cast<u32>(grid_counts[7]));
}

// Draw the left slice's rear grid and beveled frame before cells, or its two front frame edges afterward.
// Height clipping is signed and advances the horizontal grid only for drawn lines; zero counts retain the source do-while pass.
void BdEditor::DrawLeftGrid(u8 behind) {
  u32 x = static_cast<u32>(left_anchor.x), y = static_cast<u32>(left_anchor.y);
  u32 height = static_cast<u32>(grid_counts[0]) * grid_steps[4].y;
  u32 dx = static_cast<u32>(grid_counts[4]) * grid_steps[0].x;
  u32 dy = static_cast<u32>(grid_counts[4]) * grid_steps[0].y;
  BdPoint first, second, third, fourth;
  if (behind) {
    first = {static_cast<i32>(x + 6), static_cast<i32>(y - height - 12)};
    second = {static_cast<i32>(x + 6), static_cast<i32>(y + 6)};
    third = {static_cast<i32>(x), static_cast<i32>(y)};
    fourth = {static_cast<i32>(x), static_cast<i32>(y - height)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueShadow);
    first = {static_cast<i32>(x + 2), static_cast<i32>(y - height - 4)};
    second = {static_cast<i32>(x + 2), static_cast<i32>(y + 2)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueMidtone);
    first = {static_cast<i32>(x - dx), static_cast<i32>(y + dy)};
    second = {static_cast<i32>(x), static_cast<i32>(y)};
    third = {static_cast<i32>(x + 6), static_cast<i32>(y + 6)};
    fourth = {static_cast<i32>(x - dx - 6), static_cast<i32>(y + dy + 12)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueHighlight);
    first = {static_cast<i32>(x - dx - 2), static_cast<i32>(y + dy + 4)};
    second = {static_cast<i32>(x + 2), static_cast<i32>(y + 2)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueShadow);
    first = left_anchor;
    second = {static_cast<i32>(x - dx), static_cast<i32>(y + dy)};
    u32 i = 0;
    do {
      i32 z = static_cast<i32>(static_cast<u32>(selection.origin.z) + i);
      u32 color = grid_colors[(static_cast<u32>(z) & 7) == 0];
      i32 end_z = static_cast<i32>(static_cast<u32>(grid->bounds.origin.z) + grid->bounds.size.z);
      if (z < end_z) {
        renderer->DrawSegment(&second, &first, color);
        second.y = static_cast<i32>(static_cast<u32>(second.y) - grid_steps[4].y);
        first.y = static_cast<i32>(static_cast<u32>(first.y) - grid_steps[4].y);
      }
      ++i;
    } while (i <= static_cast<u32>(grid_counts[0]));
    first = left_anchor;
    second = {left_anchor.x, static_cast<i32>(y - height)};
    i = 0;
    do {
      u32 color = grid_colors[((static_cast<u32>(selection.origin.x) + i) & 7) == 0];
      renderer->DrawSegment(&first, &second, color);
      first.x = static_cast<i32>(static_cast<u32>(first.x) - grid_steps[0].x);
      first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[0].y);
      second.x = static_cast<i32>(static_cast<u32>(second.x) - grid_steps[0].x);
      second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[0].y);
      ++i;
    } while (i <= static_cast<u32>(grid_counts[4]));
  } else {
    first = {static_cast<i32>(x - dx - 6), static_cast<i32>(y + dy - height - 6)};
    second = {static_cast<i32>(x + 6), static_cast<i32>(y - height - 12)};
    third = {static_cast<i32>(x), static_cast<i32>(y - height)};
    fourth = {static_cast<i32>(x - dx), static_cast<i32>(y + dy - height)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueHighlight);
    first = {static_cast<i32>(x - dx - 4), static_cast<i32>(y + dy - height - 4)};
    second = {static_cast<i32>(x + 4), static_cast<i32>(y - height - 8)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueShadow);
    first = {static_cast<i32>(x - dx - 6), static_cast<i32>(y + dy - height - 6)};
    second = {static_cast<i32>(x - dx), static_cast<i32>(y + dy - height)};
    third = {static_cast<i32>(x - dx), static_cast<i32>(y + dy)};
    fourth = {static_cast<i32>(x - dx - 6), static_cast<i32>(y + dy + 12)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueMidtone);
    first = {static_cast<i32>(x - dx - 4), static_cast<i32>(y + dy - height - 4)};
    fourth = {static_cast<i32>(x - dx - 4), static_cast<i32>(y + dy + 8)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintBlueShadow);
  }
}

// Draw the right slice's rear bevels and clipped grid, or the two front bevels after the slice cells.
// Preserve its independent corner offsets, colors, y-origin phase and positive horizontal stepping.
void BdEditor::DrawRightGrid(u8 behind) {
  u32 x = static_cast<u32>(right_anchor.x), y = static_cast<u32>(right_anchor.y);
  u32 height = static_cast<u32>(grid_counts[1]) * grid_steps[5].y;
  u32 dx = static_cast<u32>(grid_counts[5]) * grid_steps[1].x;
  u32 dy = static_cast<u32>(grid_counts[5]) * grid_steps[1].y;
  BdPoint first, second, third, fourth;
  if (behind) {
    first = {static_cast<i32>(x - 6), static_cast<i32>(y - height - 12)};
    second = {static_cast<i32>(x), static_cast<i32>(y - height)};
    third = {static_cast<i32>(x), static_cast<i32>(y)};
    fourth = {static_cast<i32>(x - 6), static_cast<i32>(y + 6)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkMidtone);
    first = {static_cast<i32>(x - 2), static_cast<i32>(y - height - 2)};
    fourth = {static_cast<i32>(x - 2), static_cast<i32>(y + 4)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkShadow);
    first = {static_cast<i32>(x), static_cast<i32>(y)};
    second = {static_cast<i32>(x + dx), static_cast<i32>(y + dy)};
    third = {static_cast<i32>(x + dx + 6), static_cast<i32>(y + dy + 12)};
    fourth = {static_cast<i32>(x - 6), static_cast<i32>(y + 6)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkHighlight);
    first = {static_cast<i32>(x - 2), static_cast<i32>(y + 2)};
    second = {static_cast<i32>(x + dx + 2), static_cast<i32>(y + dy + 4)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkMidtone);
    first = right_anchor;
    second = {static_cast<i32>(x + dx), static_cast<i32>(y + dy)};
    u32 i = 0;
    do {
      i32 z = static_cast<i32>(static_cast<u32>(selection.origin.z) + i);
      u32 color = grid_colors[(static_cast<u32>(z) & 7) == 0];
      i32 end_z = static_cast<i32>(static_cast<u32>(grid->bounds.origin.z) + grid->bounds.size.z);
      if (z < end_z) {
        renderer->DrawSegment(&second, &first, color);
        second.y = static_cast<i32>(static_cast<u32>(second.y) - grid_steps[5].y);
        first.y = static_cast<i32>(static_cast<u32>(first.y) - grid_steps[5].y);
      }
      ++i;
    } while (i <= static_cast<u32>(grid_counts[1]));
    first = right_anchor;
    second = {right_anchor.x, static_cast<i32>(y - height)};
    i = 0;
    do {
      u32 color = grid_colors[((static_cast<u32>(selection.origin.y) + i) & 7) == 0];
      renderer->DrawSegment(&first, &second, color);
      first.x = static_cast<i32>(static_cast<u32>(first.x) + grid_steps[1].x);
      first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[1].y);
      second.x = static_cast<i32>(static_cast<u32>(second.x) + grid_steps[1].x);
      second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[1].y);
      ++i;
    } while (i <= static_cast<u32>(grid_counts[5]));
  } else {
    first = {static_cast<i32>(x + dx + 6), static_cast<i32>(y + dy - height - 6)};
    second = {static_cast<i32>(x + dx + 6), static_cast<i32>(y + dy + 12)};
    third = {static_cast<i32>(x + dx), static_cast<i32>(y + dy)};
    fourth = {static_cast<i32>(x + dx), static_cast<i32>(y + dy - height)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkShadow);
    first = {static_cast<i32>(x + dx + 4), static_cast<i32>(y + dy - height - 4)};
    second = {static_cast<i32>(x + dx + 4), static_cast<i32>(y + dy + 8)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkMidtone);
    first = {static_cast<i32>(x - 6), static_cast<i32>(y - height - 12)};
    second = {static_cast<i32>(x + dx + 6), static_cast<i32>(y + dy - height - 6)};
    third = {static_cast<i32>(x + dx), static_cast<i32>(y + dy - height)};
    fourth = {static_cast<i32>(x), static_cast<i32>(y - height)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkHighlight);
    first = {static_cast<i32>(x - 4), static_cast<i32>(y - height - 8)};
    second = {static_cast<i32>(x + dx + 4), static_cast<i32>(y + dy - height - 4)};
    renderer->DrawScreenQuad(&first, &second, &third, &fourth, kBdFootprintPinkMidtone);
  }
}

// Draw the horizontal slice's rear twelve-line bevel strips and grid, or its front strips after cell drawing.
// Keep the source's mixed count/step reads in the front strips, including their non-square-selection behavior.
void BdEditor::DrawBottomGrid(u8 behind) {
  u32 x = static_cast<u32>(bottom_anchor.x), y = static_cast<u32>(bottom_anchor.y);
  BdPoint first, second;
  if (behind) {
    u32 dx = static_cast<u32>(grid_counts[6]) * grid_steps[2].x;
    u32 dy = static_cast<u32>(grid_counts[6]) * grid_steps[2].y;
    first = {bottom_anchor.x, static_cast<i32>(y - 6)};
    second = {static_cast<i32>(x - dx - 12), static_cast<i32>(y + dy)};
    for (u32 i = 0; i < 12; ++i) {
      if (i) {
        second.x = static_cast<i32>(static_cast<u32>(second.x) + 1);
        if (!(i & 1))
          first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
      }
      renderer->DrawSegment(&first, &second, i < 8 ? kBdFootprintGreenHighlight : kBdFootprintGreenShadow);
    }
    dx = static_cast<u32>(grid_counts[2]) * grid_steps[6].x;
    dy = static_cast<u32>(grid_counts[2]) * grid_steps[6].y;
    first = {static_cast<i32>(x + 1), static_cast<i32>(y - 6)};
    second = {static_cast<i32>(x + dx + 13), static_cast<i32>(y + dy)};
    for (u32 i = 0; i < 12; ++i) {
      if (i) {
        second.x = static_cast<i32>(static_cast<u32>(second.x) - 1);
        if (!(i & 1))
          first.y = static_cast<i32>(static_cast<u32>(first.y) + 1);
      }
      renderer->DrawSegment(&first, &second, i < 8 ? kBdFootprintGreenHighlight : kBdFootprintGreenMidtone);
    }
    first = {static_cast<i32>(x + 1), bottom_anchor.y};
    second = {static_cast<i32>(x + dx + 1), static_cast<i32>(y + dy)};
    u32 i = 0;
    do {
      u32 color = grid_colors[((static_cast<u32>(selection.origin.x) + i) & 7) == 0];
      renderer->DrawSegment(&first, &second, color);
      first.x = static_cast<i32>(static_cast<u32>(first.x) - grid_steps[6].x);
      first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[6].y);
      second.x = static_cast<i32>(static_cast<u32>(second.x) - grid_steps[6].x);
      second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[6].y);
      ++i;
    } while (i <= static_cast<u32>(grid_counts[2]));
    dx = static_cast<u32>(grid_counts[6]) * grid_steps[2].x;
    dy = static_cast<u32>(grid_counts[6]) * grid_steps[2].y;
    first = bottom_anchor;
    second = {static_cast<i32>(x - dx), static_cast<i32>(y + dy)};
    i = 0;
    do {
      u32 color = grid_colors[((static_cast<u32>(selection.origin.y) + i) & 7) == 0];
      renderer->DrawSegment(&first, &second, color);
      first.x = static_cast<i32>(static_cast<u32>(first.x) + grid_steps[2].x);
      first.y = static_cast<i32>(static_cast<u32>(first.y) + grid_steps[2].y);
      second.x = static_cast<i32>(static_cast<u32>(second.x) + grid_steps[2].x);
      second.y = static_cast<i32>(static_cast<u32>(second.y) + grid_steps[2].y);
      ++i;
    } while (i <= static_cast<u32>(grid_counts[6]));
  } else {
    u32 dx = static_cast<u32>(grid_counts[6]) * grid_steps[2].x;
    u32 dy = static_cast<u32>(grid_counts[6]) * grid_steps[6].y;
    first = {static_cast<i32>(x - dx - 12), static_cast<i32>(y + dy)};
    second = {bottom_anchor.x, static_cast<i32>(y + dx + 6)};
    for (u32 i = 0; i < 12; ++i) {
      if (i) {
        first.x = static_cast<i32>(static_cast<u32>(first.x) + 1);
        if (!(i & 1))
          second.y = static_cast<i32>(static_cast<u32>(second.y) - 1);
      }
      renderer->DrawSegment(&first, &second, i < 6 ? kBdFootprintGreenMidtone : kBdFootprintGreenHighlight);
    }
    first = {static_cast<i32>(x + 1), static_cast<i32>(y + dx + 6)};
    second = {static_cast<i32>(x + dx + 13), static_cast<i32>(y + dy)};
    for (u32 i = 0; i < 12; ++i) {
      if (i) {
        second.x = static_cast<i32>(static_cast<u32>(second.x) - 1);
        if (!(i & 1))
          first.y = static_cast<i32>(static_cast<u32>(first.y) - 1);
      }
      renderer->DrawSegment(&first, &second, i < 6 ? kBdFootprintGreenShadow : kBdFootprintGreenHighlight);
    }
  }
}

// Draw the current view's handle only while the original selection, zoom and tool conditions permit it.
// Preserve lock/blit/unlock order and the four separate blit arguments, including the null clip pointer.
void BdEditor::DrawViewHandle() {
  if (!view_handle_enabled)
    return;
  u8 view = grid->view_mode;
  if (selection.origin.z || field_778 != field_77c || !view_handles[view] || field_764 || (view == 1 && !tool))
    return;
  if (!drawing_buffer->Lock(GxLockFlags::Blt))
    return;
  ScRect source = *view_handles[view]->GetBufferArea();
  BdPoint center;
  if (!tool)
    ProjectTile(&selection.origin, &center);
  else {
    center.x = projection_anchor.x;
    center.y = static_cast<i32>(static_cast<u32>(projection_anchor.y) + (UINT32_MAX << (zoom_shift & 31)));
  }
  static const BdPoint offsets[4] = {{0, 0}, {7, -22}, {3, -59}, {3, -3}};
  center.x += offsets[view].x;
  center.y += offsets[view].y;
  i32 width = source.right - source.left, height = source.bottom - source.top;
  ScRect destination = {center.x - width / 2, center.y - height / 2,
      center.x + static_cast<i32>(static_cast<f64>(width) * 0.5 + 0.5),
      center.y + static_cast<i32>(static_cast<f64>(height) * 0.5 + 0.5)};
  drawing_buffer->Blt(view_handles[view], source, destination, nullptr);
  drawing_buffer->Unlock(GxLockFlags::Blt);
}

// Recompute the renderer's clipped drawing bounds and all enabled resize-handle rectangles in source order.
// Keep previous bounds for missing images and include the two depth-handle rectangles in the final drawing union.
void BdEditor::UpdateHandleBounds() {
  BdPoint projected;
  ProjectTile(&selection.origin, &projected);
  u32 span = static_cast<u32>(selection.size.y) << (zoom_shift & 31);
  if (renderer && renderer->buffer) {
    ScRect image_area = *renderer->buffer->GetBufferArea();
    i32 left = static_cast<i32>(static_cast<u32>(projected.x) - span - 1);
    i32 right = static_cast<i32>(span + static_cast<u32>(projected.x) + 1), width = image_area.right - image_area.left;
    i32 bottom = static_cast<i32>(static_cast<u32>(projected.y) + span + 2),
        height = image_area.bottom - image_area.top;
    view_bounds[1].left = left < 0 ? 0 : left;
    view_bounds[1].top = 0;
    view_bounds[1].right = right <= width ? right : width;
    view_bounds[1].bottom = bottom <= height ? bottom : height;
    view_bounds[1].bottom += handle_bounds[0].bottom - handle_bounds[0].top;
  } else
    view_bounds[1] = {absolute_area.left, absolute_area.top, absolute_area.right, absolute_area.bottom};
  if (tool)
    return;
  for (u32 n = 0; n < 4; ++n) {
    u32 handle = (n + 2) & 3;
    if (!handle_images[handle])
      continue;
    BdPoint3 tile = selection.origin;
    if (handle == 0 || handle == 2)
      tile.x += selection.size.x;
    if (handle == 1 || handle == 3)
      tile.y += selection.size.y;
    if (handle >= 2)
      tile.z = cursor.z;
    else if (handle == 0)
      tile.y = cursor.y;
    else
      tile.x = cursor.x;
    ProjectTile(&tile, &projected);
    ScRect image_area = *handle_images[handle]->GetBufferArea();
    BdRect *bounds = handle_bounds + handle;
    *bounds = {image_area.left, image_area.top, image_area.right, image_area.bottom};
    bounds->Offset(&projected);
    if (handle == 0 || handle == 2) {
      i32 delta = image_area.left - image_area.right;
      bounds->left += delta;
      bounds->right += delta;
    }
    if (handle >= 2) {
      i32 delta = (image_area.bottom - image_area.top) / -2;
      bounds->top += delta;
      bounds->bottom += delta;
    }
  }
  if (drawing_buffer) {
    BdRect first, second;
    second.Unite(handle_bounds + 2, &view_bounds[1]);
    first.Unite(handle_bounds + 3, &second);
    view_bounds[1] = first;
  }
}

// Draw all four resize handles onto the editor buffer in depth-first source order under one write lock.
// Each image supplies its current source area and the corresponding previously computed destination rectangle.
void BdEditor::DrawResizeHandles() {
  if (!drawing_buffer->Lock(GxLockFlags::Blt))
    return;
  for (u32 n = 0; n < 4; ++n) {
    u32 handle = (n + 2) & 3;
    ScRect source = *handle_images[handle]->GetBufferArea();
    const BdRect *bounds = handle_bounds + handle;
    ScRect destination = {bounds->left, bounds->top, bounds->right, bounds->bottom};
    drawing_buffer->Blt(handle_images[handle], source, destination, nullptr);
  }
  drawing_buffer->Unlock(GxLockFlags::Blt);
}

static BdPoint3 BdTileStart;
static BdPoint BdTileStartScreen, BdTileStartProjection;

static i32 BdScreenAdd(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) + static_cast<u32>(second));
}

static i32 BdScreenSubtract(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) - static_cast<u32>(second));
}

static bool BdSameTile(const BdPoint3 *first, const BdPoint3 *second) {
  return first->x == second->x && first->y == second->y && first->z == second->z;
}

// Calculate the original fixed-point interpolation count for a mouse drag.
// A negative wrapped square would make the source's floating-to-integer conversion undefined.
static i32 BdDragCount(const BdPoint *distance, i32 zoom) {
  u32 x = static_cast<u32>(distance->x), y = static_cast<u32>(distance->y);
  i32 square = static_cast<i32>(x * x + y * y);
  i32 count = static_cast<i32>(sqrt(static_cast<f64>(square < 0 ? 0 : square))) >> ((static_cast<u32>(zoom) + 6) & 31);
  return count ? count : 1;
}

// Interpolate a painting drag through screen-space samples and apply each newly encountered tile.
// The first paint tool includes the final sample; the second stops before it.
static void BdPaintDrag(BdEditor *editor, BdPoint *position, i32 distance_x, bool include_final, i32 *last_x) {
  editor->drag_screen_scaled.x = BdScreenSubtract(editor->drag_screen_scaled.x, distance_x);
  editor->drag_screen_scaled.y = BdScreenSubtract(editor->drag_screen_scaled.y, position->y);
  i32 count = BdDragCount(&editor->drag_screen_scaled, editor->zoom_shift);
  BdPoint step = {editor->drag_screen_scaled.x / -count, editor->drag_screen_scaled.y / -count};
  position->x = BdScreenSubtract(position->x, static_cast<i32>(static_cast<u32>(step.x) * static_cast<u32>(count)));
  position->y = BdScreenSubtract(position->y, static_cast<i32>(static_cast<u32>(step.y) * static_cast<u32>(count)));
  BdPoint3 last = {0, 0, -1};
  i32 remaining = count + (include_final ? 1 : 0);
  while (remaining-- > 0) {
    *last_x = position->x;
    u8 old_face = editor->selected_hit.face;
    BdPoint screen = {position->x >> 8, position->y >> 8};
    editor->UpdateHit(&screen, &editor->selected_hit);
    if (editor->selected_hit.kind == 128 && editor->selected_hit.cell &&
        (!BdSameTile(&last, &editor->selected_hit.tile) || old_face != editor->previous_hit.face)) {
      editor->ApplyTileOp(&editor->selected_hit, 1);
      editor->InvalidateView(1);
    }
    position->x = BdScreenAdd(position->x, step.x);
    position->y = BdScreenAdd(position->y, step.y);
    last = editor->selected_hit.tile;
  }
}
