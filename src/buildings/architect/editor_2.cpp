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

// Start a left-button edit, capturing the mouse and recording the selected hit and drag position.
// A simultaneous right-button drag prevents the new edit.
bool BdEditor::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  field_764 = 0;
  if (right_drag_active)
    return false;
  left_drag_active = 1;
  owner->SetCapture(this);
  grid->CloseChangeGroup();
  BdPoint screen = {x, y};
  UpdateHit(&screen, &selected_hit);
  previous_hit = selected_hit;
  previous_hit.screen = screen;
  UpdateDragTool(static_cast<u8>(modifiers));
  ApplyTileOp(&previous_hit, 0);
  drag_screen_scaled = {static_cast<i32>(static_cast<u32>(screen.x) << 8),
      static_cast<i32>(static_cast<u32>(screen.y) << 8)};
  return true;
}

// Start a right-button edit with the same captured hit and drag origin as a left-button edit.
// A simultaneous left-button drag prevents the new edit.
bool BdEditor::OnMouseDownR(i32 x, i32 y, u32 modifiers) {
  field_764 = 0;
  if (left_drag_active)
    return false;
  right_drag_active = 1;
  owner->SetCapture(this);
  grid->CloseChangeGroup();
  BdPoint screen = {x, y};
  UpdateHit(&screen, &selected_hit);
  previous_hit = selected_hit;
  previous_hit.screen = screen;
  UpdateDragTool(static_cast<u8>(modifiers));
  ApplyTileOp(&previous_hit, 0);
  drag_screen_scaled = {static_cast<i32>(static_cast<u32>(screen.x) << 8),
      static_cast<i32>(static_cast<u32>(screen.y) << 8)};
  return true;
}

// Finish a left-button edit at the updated hit and invalidate the affected view.
// Clear the drag state and release the captured mouse after the tile operation.
bool BdEditor::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  BdPoint screen = {x, y};
  UpdateHit(&screen, &selected_hit);
  selected_hit.screen = screen;
  u16 kind = selected_hit.kind;
  if (!tool) {
    if (kind == 16 || kind == 32 || kind == 64) {
      if (tool_mode == kBdToolPlane || tool_mode == kBdToolLine)
        ApplyTileOp(&selected_hit, (modifiers & kWnModifierShift) ? 3 : 2);
      InvalidateView(0);
    }
  } else if (kind == 128 || kind == 16 || kind == 32 || kind == 64) {
    ApplyTileOp(&selected_hit, 2);
  } else if (kind == 1 || kind == 2 || kind == 4) {
    ApplyTileOp(&selected_hit, 2);
    InvalidateView(1);
  }
  selected_hit.kind = 0;
  field_774 = static_cast<i32>(static_cast<u32>(field_774) & ~1u);
  left_drag_active = 0;
  owner->ReleaseCapture(this);
  return true;
}

// Finish a right-button edit and restore the saved paint tool when a temporary erase mode was active.
// Release the captured mouse and clear the selected hit after the edit.
bool BdEditor::OnMouseUpR(i32, i32, u32 modifiers) {
  if (tool) {
    ApplyTileOp(&selected_hit, 2);
  } else {
    u16 kind = selected_hit.kind;
    if (kind == 16 || kind == 32 || kind == 64) {
      if (tool_mode == kBdToolPlane || tool_mode == kBdToolLine)
        ApplyTileOp(&selected_hit, (modifiers & kWnModifierShift) ? 3 : 2);
      InvalidateView(0);
    }
    if (tool_mode != field_81c && (tool_mode == kBdToolErase || tool_mode == kBdToolSelect)) {
      SetToolMode(static_cast<u32>(field_81c));
      placement_tint = BdConstructSelectedBlock;
    }
  }
  field_774 = static_cast<i32>(static_cast<u32>(field_774) & ~2u);
  right_drag_active = 0;
  owner->ReleaseCapture(this);
  selected_hit.kind = 0;
  return true;
}

// Update the hovered hit and interpolate active paint and selection drags across the mouse path.
// Refresh affected views and handles, then give the editor window keyboard focus.
bool BdEditor::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  static BdPoint3 axis_origin = {};
  static i32 axis = -1;
  selected_hit.screen = {x, y};
  BdPoint screen = {x, y};
  UpdateHit(&screen, &selected_hit);
  BdPoint position = {static_cast<i32>(static_cast<u32>(screen.x) << 8),
      static_cast<i32>(static_cast<u32>(screen.y) << 8)};
  if ((modifiers & kWnModifierShift) && (modifiers & kWnModifierMouseButtonsMask)) {
    if (axis == -1) {
      axis_origin = previous_hit.tile;
      axis = 0;
    }
    modifiers &= ~1u;
  } else {
    axis = -1;
  }
  UpdateDragTool(static_cast<u8>(modifiers));
  i32 previous_x = position.x;
  if (tool == 1 && (left_drag_active || right_drag_active) && field_81c != kBdToolFill)
    BdPaintDrag(this, &position, position.x, true, &previous_x);
  if (tool == 2 && (left_drag_active || right_drag_active)) {
    BdPaintDrag(this, &position, previous_x, false, &previous_x);
    previous_x = position.x;
  }
  bool dragging = left_drag_active || right_drag_active;
  if (!tool && dragging) {
    BdPoint3 point = cursor, corner;
    BdPoint projected;
    if (previous_hit.kind == 1) {
      corner = {BdScreenAdd(selection.origin.x, selection.size.x),
          BdScreenAdd(selection.origin.y, selection.size.y), selection.origin.z};
      drag_direction.x = 0;
      ProjectTile(&corner, &projected);
      i32 distance = BdScreenSubtract(x, projected.x) / 2 - BdScreenSubtract(y, projected.y);
      if (distance < 0) {
        distance = 0;
        drag_direction.x = 1;
      }
      i32 offset = distance >> (static_cast<u32>(zoom_shift) & 31);
      if (offset >= selection.size.x) {
        drag_direction.x = -1;
        offset = selection.size.x - 1;
      }
      point.x = BdScreenSubtract(BdScreenAdd(selection.origin.x, selection.size.x - 1), offset);
      SetCursor(&point);
      InvalidateView(1);
    } else if (previous_hit.kind == 2) {
      corner = {BdScreenAdd(selection.origin.x, selection.size.x), selection.origin.y, selection.origin.z};
      drag_direction.y = 0;
      ProjectTile(&corner, &projected);
      i32 distance = BdScreenAdd(BdScreenSubtract(y, projected.y), BdScreenSubtract(x, projected.x) / 2);
      if (distance < 0) {
        distance = 0;
        drag_direction.y = -1;
      }
      i32 offset = distance >> (static_cast<u32>(zoom_shift) & 31);
      if (offset >= selection.size.y) {
        drag_direction.y = 1;
        offset = selection.size.y - 1;
      }
      point.y = BdScreenAdd(selection.origin.y, offset);
      SetCursor(&point);
      InvalidateView(1);
    } else if (previous_hit.kind == 4) {
      corner = selection.origin;
      drag_direction.z = 0;
      ProjectTile(&corner, &projected);
      if (selected_hit.screen.x >= projected.x)
        corner.y = BdScreenAdd(selection.origin.y, selection.size.x);
      else
        corner.x = BdScreenAdd(selection.origin.x, selection.size.x);
      ProjectTile(&corner, &projected);
      i32 distance = BdScreenSubtract(x, projected.x) / 2 - BdScreenSubtract(y, projected.y);
      if (distance < 0) {
        distance = 0;
        drag_direction.z = -1;
      }
      i32 offset = distance >> (static_cast<u32>(zoom_shift) & 31);
      if (offset >= selection.size.z) {
        drag_direction.z = 1;
        offset = selection.size.z - 1;
      }
      point.z = BdScreenAdd(selection.origin.z, offset);
      if (point.z > BdScreenAdd(selection.origin.z, selection.size.z))
        selection.origin.z = BdScreenSubtract(point.z, selection.size.z);
      else if (point.z < selection.origin.z)
        selection.origin.z = point.z;
      if (selection.origin.z < 0)
        selection.origin.z = 0;
      SetCursor(&point);
      InvalidateView(1);
    } else if (previous_hit.kind == 16 || previous_hit.kind == 32 || previous_hit.kind == 64) {
      drag_screen_scaled.x = BdScreenSubtract(drag_screen_scaled.x, previous_x);
      drag_screen_scaled.y = BdScreenSubtract(drag_screen_scaled.y, position.y);
      i32 count = BdDragCount(&drag_screen_scaled, zoom_shift);
      BdPoint step = {drag_screen_scaled.x / -count, drag_screen_scaled.y / -count};
      position.x = BdScreenSubtract(position.x, static_cast<i32>(static_cast<u32>(step.x) * static_cast<u32>(count)));
      position.y = BdScreenSubtract(position.y, static_cast<i32>(static_cast<u32>(step.y) * static_cast<u32>(count)));
      BdPoint3 last = {0, 0, -1};
      if (count > 0) {
        if (previous_hit.kind == 16) {
          InvalidateView(2);
          InvalidateView(1);
        }
        if (previous_hit.kind == 32) {
          InvalidateView(3);
          InvalidateView(1);
        }
        if (previous_hit.kind == 64) {
          InvalidateView(4);
          InvalidateView(1);
        }
      }
      while (count-- > 0) {
        screen = {position.x >> 8, position.y >> 8};
        PickSelectionFace(&screen, &selected_hit.tile, &previous_hit.kind);
        if (!axis && !BdSameTile(&axis_origin, &selected_hit.tile)) {
          if (axis_origin.x != selected_hit.tile.x)
            axis = 1;
          if (axis_origin.y != selected_hit.tile.y)
            axis = 2;
          if (axis_origin.z != selected_hit.tile.z)
            axis = 3;
        }
        if (axis != -1) {
          if (axis != 1)
            selected_hit.tile.x = axis_origin.x;
          if (axis != 2)
            selected_hit.tile.y = axis_origin.y;
          if (axis != 3)
            selected_hit.tile.z = axis_origin.z;
        }
        if (!BdSameTile(&last, &selected_hit.tile))
          ApplyTileOp(&selected_hit, 1);
        position.x = BdScreenAdd(position.x, step.x);
        position.y = BdScreenAdd(position.y, step.y);
        last = selected_hit.tile;
      }
    }
  }
  if ((field_81c == kBdToolErase || field_81c == kBdToolSelect) && selected_hit.kind == 128 &&
      !BdSameTile(&selected_hit.tile, &hover_hit.tile))
    InvalidateView(1);
  if ((tool == 1 || tool == 2) &&
      (field_81c == kBdToolErase || field_81c == kBdToolSelect || field_81c == kBdToolPaintbrush ||
          field_81c == kBdToolFill || field_81c == kBdToolComposition) &&
      !BdSameTile(&selected_hit.tile, &hover_hit.tile))
    InvalidateView(1);
  i32 view = tool == 0 ? 0 : (tool == 1 || tool == 2 || tool == 4) ? 1 : tool == 3 ? 2 : -1;
  if (view == 1 || view == 2) {
    if (selected_hit.kind != hover_kind) {
      hover_kind = selected_hit.kind;
      if (tool != 4)
        InvalidateView(1);
    }
    if (!BdSameTile(&selected_hit.tile, &hover_hit.tile)) {
      if (tool != 4)
        InvalidateView(1);
      hover_hit = selected_hit;
    }
  } else if (!view) {
    if (selected_hit.kind != hover_kind) {
      hover_kind = selected_hit.kind;
      InvalidateView(0);
    } else if (!BdSameTile(&selected_hit.tile, &hover_hit.tile)) {
      dragging = left_drag_active || right_drag_active;
      if (dragging || !previous_hit.kind) {
        if ((dragging ? previous_hit.kind : selected_hit.kind) == 16) {
          InvalidateView(2);
          InvalidateView(1);
        }
        if ((dragging ? previous_hit.kind : selected_hit.kind) == 32) {
          InvalidateView(3);
          InvalidateView(1);
        }
        if ((dragging ? previous_hit.kind : selected_hit.kind) == 64) {
          InvalidateView(4);
          InvalidateView(1);
        }
      }
      hover_hit = selected_hit;
    }
  }
  drag_screen_scaled = position;
  if (grid->FindCell(&cursor, tool == 3 && tool_mode != kBdToolProp, 0) != kBdCellHeightValueMask)
    cursor_cell.parts[0].first = 0;
  UpdateHandleBounds();
  owner->SetFocus(this);
  return true;
}

// Route the current tool's tile action through the grid and catalog, retaining the original drag-start state.
// A completion phase posts the corresponding redraw and selection messages in the original order.
void BdEditor::ApplyTileOp(const BdHit *hit, u32 phase) {
  ScMessageServer *server = BdGetMessageServer();
  BdPoint3 tile;
  if (!phase) {
    BdTileStart = previous_hit.tile;
    BdTileStartScreen = previous_hit.screen;
    BdTileStartProjection = projection_anchor;
  }
  switch (tool_mode) {
  case 3:
    if (hit->cell) {
      if ((!tool && (hit->kind == 16 || hit->kind == 32 || hit->kind == 64)) ||
          ((tool == 1 || tool == 2 || tool == 3) && hit->kind == 128)) {
        tile = hit->tile;
        SetCursor(&tile);
      }
      if (!tool) {
        InvalidateViews(hit->kind);
        grid->DeleteCell(&cursor);
      }
      if (tool == 1) {
        BdCell cell = grid->cells[hit->cell];
        cell.link_height_kind = (cell.link_height_kind & kBdCellWithoutHeight) |
            ((static_cast<u32>(cursor.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
        grid->SetFirstMaterial(&cell, hit->face, nullptr);
        grid->SetSecondMaterial(&cell, hit->face, nullptr);
        grid->InsertCell(&cursor, &cell);
      }
      if (tool == 2)
        PaintSecond(hit, 0);
      if (tool == 3 && hit->kind == 128) {
        tile = hit->tile;
        SetCursor(&tile);
        BdCell *cell = &grid->cells[hit->cell];
        if ((cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind) {
          BdPoint3 origin = {};
          tile = hit->tile;
          grid->GetObjectOrigin(&tile, &origin, cell);
          BdCell replacement;
          replacement.link_height_kind = (replacement.link_height_kind & kBdCellWithoutHeightOrKind) |
              ((static_cast<u32>(origin.z) & kBdCellHeightValueMask) << kBdCellHeightShift);
          grid->InsertCell(&origin, &replacement);
          grid->DeleteCell(&origin);
        }
      }
    }
    if (phase == 2) {
      InvalidateView(1);
      InvalidateViews(hit->kind);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  case 4:
    if (phase == 2) {
      BdMaterials *materials = nullptr;
      grid->GetMaterials(&materials);
      if (hit->kind == 128) {
        tile = hit->tile;
        SetCursor(&tile);
      }
      if (!tool) {
        InvalidateViews(hit->kind);
        u32 packed = grid->cells[hit->cell].link_height_kind;
        SetBrush((packed >> kBdCellKindShift) & kBdCellKindValueMask, packed >> kBdCellRotationShift);
        packed = grid->cells[hit->cell].link_height_kind;
        ScMessage message(kWnMsg_ArchitectBrushPicked, 0, (packed >> kBdCellKindShift) & kBdCellKindValueMask,
            packed >> kBdCellRotationShift);
        BdGetMessageServer()->MessagePost(message, false);
      }
      if (tool == 3 && hit->kind == 128) {
        tile = hit->tile;
        SetCursor(&tile);
        BdCell *cell = &grid->cells[hit->cell];
        if ((cell->link_height_kind & kBdCellKindMask) == kBdCellObjectKind) {
          BdObjectInfo info;
          grid->GetObjectInfo(cell, &info);
          BdAsset *asset = nullptr;
          info.GetAsset(0, &asset);
          if (asset) {
            brush_asset_id = asset->definition_id;
            if (brush_asset_id) {
              ScMessage selected(kWnMsg_ArchitectBrushPicked, 3, static_cast<u8>(asset->kind), brush_asset_id);
              BdGetMessageServer()->MessagePost(selected, false);
              InvalidateViews(hit->kind);
              tile = hit->tile;
              SetObjectBrush(brush_asset_id, &tile);
              u32 packed = grid->cells[hit->cell].link_height_kind;
              ScMessage shape(kWnMsg_ArchitectBrushPicked, 3, (packed >> kBdCellKindShift) & kBdCellKindValueMask,
                  packed >> kBdCellRotationShift);
              BdGetMessageServer()->MessagePost(shape, false);
            }
          }
        }
      }
      if (tool == 2 && grid->cells[hit->cell].parts[hit->face].second) {
        BdAsset *asset = nullptr;
        if (materials->Get(grid->cells[hit->cell].parts[hit->face].second, &asset)) {
          paint_asset_id = asset->definition_id;
          if (paint_asset_id) {
            ScMessage message(kWnMsg_ArchitectBrushPicked, 2, static_cast<u8>(asset->kind), paint_asset_id);
            BdGetMessageServer()->MessagePost(message, false);
          }
        }
      }
      if (tool == 1 && grid->cells[hit->cell].parts[hit->face].first) {
        BdAsset *asset = nullptr;
        if (materials->Get(grid->cells[hit->cell].parts[hit->face].first, &asset)) {
          primary_asset_id = asset->definition_id;
          if (primary_asset_id) {
            ScMessage message(kWnMsg_ArchitectBrushPicked, 1, static_cast<u8>(asset->kind), primary_asset_id);
            BdGetMessageServer()->MessagePost(message, false);
          }
        }
      }
      materials->Release();
    }
    break;
  case 5:
    if (hit->kind == 16 || hit->kind == 32 || hit->kind == 64) {
      tile = hit->tile;
      SetCursor(&tile);
      InvalidateViews(hit->kind);
      if (left_drag_active) {
        BdCell cell;
        cell.link_height_kind = (cell.link_height_kind & kBdCellNextMask) |
            (((static_cast<u32>(cursor.z) & kBdCellHeightValueMask) |
                 (((16 * brush_rotation) | (brush_kind & kBdCellKindValueMask)) << 9))
                << kBdCellHeightShift);
        grid->InsertCell(&cursor, &cell);
      } else if (right_drag_active)
        grid->DeleteCell(&cursor);
    }
    if (phase == 2) {
      InvalidateView(1);
      InvalidateViews(hit->kind);
      grid->MarkDirtyColumns(&selection);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  case 6:
  case 7: {
    if (hit->kind == 16 || hit->kind == 32 || hit->kind == 64) {
      tile = hit->tile;
      SetCursor(&tile);
      if (tool_mode == kBdToolLine) {
        grid->MarkDirtyColumns(&selection);
        InvalidateViews(hit->kind);
      } else {
        InvalidateViews(hit->kind);
        grid->MarkDirtyColumns(&selection);
      }
    }
    BdCell cell;
    cell.link_height_kind = (cell.link_height_kind & kBdCellNextMask) |
        (((static_cast<u32>(cursor.z) & kBdCellHeightValueMask) |
             (((16 * brush_rotation) | (brush_kind & kBdCellKindValueMask)) << 9))
            << kBdCellHeightShift);
    u16 plane = previous_hit.kind;
    if ((plane == 16 || plane == 32 || plane == 64) && phase <= 1) {
      ClearMarks();
      BdPoint3 first = BdTileStart, second = hit->tile;
      drag_direction = {0, 0, 0};
      if (plane == 16 || plane == 64) {
        i32 end = static_cast<i32>(static_cast<u32>(selection.origin.x) + selection.size.x);
        drag_direction.x = second.x < selection.origin.x ? -1 : second.x > end ? 1 : 0;
      }
      if (plane == 32 || plane == 64) {
        i32 end = static_cast<i32>(static_cast<u32>(selection.origin.y) + selection.size.y);
        drag_direction.y = second.y < selection.origin.y ? -1 : second.y > end ? 1 : 0;
      }
      if (plane == 16 || plane == 32) {
        i32 end = static_cast<i32>(static_cast<u32>(selection.origin.z) + selection.size.z);
        drag_direction.z = second.z < selection.origin.z ? -1 : second.z > end ? 1 : 0;
      }
      if (tool_mode == kBdToolLine)
        MarkLine(&first, &second);
      else
        MarkBox(&first, &second);
    }
    if (phase == 2) {
      ClearMarks();
      BdPoint3 first = BdTileStart, second = cursor;
      if (right_drag_active) {
        if (tool_mode == kBdToolLine)
          grid->DeleteLine(&first, &second);
        else
          grid->DeleteBox(&first, &second);
      } else if (left_drag_active) {
        if (tool_mode == kBdToolLine)
          grid->InsertLine(&first, &second, &cell);
        else
          grid->InsertBox(&first, &second, &cell);
      }
      if (tool_mode == kBdToolLine)
        grid->MarkDirtyColumns(&grid->bounds);
      InvalidateView(1);
      InvalidateViews(hit->kind);
      if (tool_mode == kBdToolPlane)
        grid->MarkDirtyColumns(&grid->bounds);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  }
  case 8:
    if (hit->cell && hit->kind == 128) {
      tile = hit->tile;
      SetCursor(&tile);
      grid->MarkDirtyColumns(&selection);
      BdCell cell = grid->cells[hit->cell];
      if (left_drag_active) {
        BdAsset *asset = nullptr;
        if (catalog->GetAsset(primary_asset_id, &asset))
          grid->SetFirstMaterial(&cell, hit->face, asset);
      } else if (right_drag_active) {
        grid->SetFirstMaterial(&cell, hit->face, nullptr);
        grid->SetSecondMaterial(&cell, hit->face, nullptr);
      }
      grid->InsertCell(&cursor, &cell);
    }
    if (phase == 2) {
      InvalidateView(1);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  case 9:
    if (hit->kind == 128 && !phase) {
      tile = hit->tile;
      SetCursor(&tile);
      PaintPrimary(hit, primary_asset_id);
      InvalidateView(1);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  case 10:
    if (hit->cell && hit->kind == 128) {
      tile = hit->tile;
      SetCursor(&tile);
      if (left_drag_active)
        PaintSecond(hit, paint_asset_id);
      else if (right_drag_active)
        PaintSecond(hit, 0);
    }
    if (phase == 2) {
      InvalidateView(1);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  case 11:
    if (hit->cell && hit->kind == 128) {
      tile = hit->tile;
      SetCursor(&tile);
      if (left_drag_active)
        PlaceObject(hit, brush_asset_id);
      else if (right_drag_active) {
        u32 id = EraseObject(hit);
        if (id) {
          brush_asset_id = id;
          ScMessage selected(kWnMsg_ArchitectBrushPicked, 3, 0, id);
          BdGetMessageServer()->MessagePost(selected, false);
          tile = hit->tile;
          SetObjectBrush(id, &tile);
          u32 packed = grid->cells[hit->cell].link_height_kind;
          ScMessage shape(kWnMsg_ArchitectBrushPicked, 3, 15, packed >> kBdCellRotationShift);
          BdGetMessageServer()->MessagePost(shape, false);
        }
      }
    }
    if (phase == 2) {
      InvalidateView(1);
      ScMessage message(kWnMsg_ArchitectInvalidateGrid);
      server->MessagePost(message, false);
    }
    break;
  default:
    break;
  }
  if ((previous_hit.kind == 1 || previous_hit.kind == 2 || previous_hit.kind == 4) && phase == 2) {
    ScMessage message(kWnMsg_ArchitectSelectionBoundsChanged, reinterpret_cast<uptr>(&selection), 0, 0);
    server->MessagePost(message, false);
  }
}

// Load the editor's resize and view handles from the installed image resources.
// Retain each available buffer and set its magenta color key before the scoped resource releases it.
bool BdEditor::LoadImages() {
  static const u32 handle_ids[4] = {197, 196, 198, 199};
  static const u32 view_ids[5] = {193, 194, 195, 195, 195};
  for (u32 i = 0; i < 9; ++i) {
    GxBuffer **image = i < 4 ? &handle_images[i] : &view_handles[i - 4];
    if (!*image) {
      const GxColorType format(GxColorType::Color::RGB16);
      BdImageResource resource(
          i < 4 ? handle_ids[i] : view_ids[i - 4], kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
      *image = resource.buffer;
      if (*image) {
        (*image)->AddRef();
        u16 key = (*image)->ConvertRGBValueToNative16Bit(255, 0, 255);
        (*image)->SetTransparency(key);
      }
    }
  }
  return true;
}

// Initialize the editor's catalog, handle images, dirty state, and keyboard accelerator resources.
// Finish through the inherited root initialization after releasing the temporary resource interfaces.
bool BdEditor::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  SetID(kBdEditorWindowId);
  catalog = BdGetCatalog();
  LoadImages();
  SetFlag(kWnFlagReorderable, false);
  ResetDirty();
  RsKey key = {kRsAcceleratorResourceType, kRsArchitectAcceleratorGroup, 177};
  WnAcceleratorResources *resources = nullptr;
  BdGetResourceManager()->GetResource(key, IID_WnAcceleratorResources, reinterpret_cast<void **>(&resources));
  if (resources) {
    WnAccelerator *keyboard_accelerator = nullptr;
    if (BdGetCom()->GetClassObject(
            kWnKeyAcceleratorClassId, IID_WnAccelerator, reinterpret_cast<void **>(&keyboard_accelerator))) {
      if (keyboard_accelerator->Init(resources))
        SetKeyboardAccelerator(keyboard_accelerator);
      keyboard_accelerator->Release();
    }
    resources->Release();
  }
  return WnArchitectRootWidget::Init();
}

// Retire root state and the editor's owned renderer, panel, grid, mark planes and images in source order.
// Remove the seventeen editor notifications after releasing those resources.
bool BdEditor::Shutdown() {
  if (GetFlag(kWnFlagInitialized)) {
    SetFlag(kWnFlagInitialized, false);
    WnArchitectRootWidget::Shutdown();
  }
  if (renderer) {
    renderer->SetBuffer(nullptr);
    delete renderer;
    renderer = nullptr;
  }
  ScRelease(&tool_panel);
  ScRelease(&drawing_buffer);
  if (grid) {
    u32 side = static_cast<u32>(grid->side);
    grid->Release();
    grid = nullptr;
    if (left_marks) {
      for (u32 i = 0; i < side; ++i)
        delete[] left_marks[i];
      delete[] left_marks;
      left_marks = nullptr;
    }
    if (right_marks) {
      for (u32 i = 0; i < side; ++i)
        delete[] right_marks[i];
      delete[] right_marks;
      right_marks = nullptr;
    }
    if (bottom_marks) {
      for (u32 i = 0; i < side; ++i)
        delete[] bottom_marks[i];
      delete[] bottom_marks;
      bottom_marks = nullptr;
    }
  }
  for (u32 i = 0; i < 4; ++i) {
    if (handle_images[i]) {
      handle_images[i]->Release();
      handle_images[i] = nullptr;
    }
  }
  for (u32 i = 0; i < 6; ++i) {
    if (view_handles[i]) {
      view_handles[i]->Release();
      view_handles[i] = nullptr;
    }
  }
  static const u32 notifications[17] = {kWnMsg_ArchitectForwardInput, kWnMsg_ArchitectGridChanged,
      kWnMsg_ArchitectInvalidateGrid, kWnMsg_ArchitectSelectionBoundsChanged, kWnMsg_ArchitectRotateSelectionNegative,
      kWnMsg_ArchitectRotateSelectionPositive, kWnMsg_Unresolved30, kWnMsg_Unresolved31, kWnMsg_ArchitectZoomIn,
      kWnMsg_ArchitectZoomOut, kWnMsg_ArchitectRotateViewNegative, kWnMsg_ArchitectRotateViewPositive,
      kWnMsg_ArchitectSetBrush, kWnMsg_ArchitectSetAsset, kWnMsg_ArchitectPanView, kWnMsg_ArchitectSetTool,
      kWnMsg_AcceleratorCommand};
  ScMessageServer *server = BdGetMessageServer();
  for (u32 id : notifications)
    server->RemoveNotification(this, id);
  return true;
}

// Draw the left cursor slice from the actual grid table, including transient selected or marked placement cells.
// Original left-slice iteration and cursor coordinates narrow to sixteen bits independently of the cell height bits.
void BdEditor::DrawLeftSlice() {
  if (previous_hit.kind != 16 && selected_hit.kind != 16 && previous_hit.kind != 2)
    return;
  u16 y = static_cast<u16>(cursor.y);
  for (u16 x = static_cast<u16>(selection.origin.x); x < selection.origin.x + selection.size.x; ++x) {
    for (u16 z = static_cast<u16>(selection.origin.z); z < selection.origin.z + selection.size.z; ++z) {
      bool selected = (tool_mode == kBdToolBlock || tool_mode == kBdToolPlane || tool_mode == kBdToolLine ||
                          tool_mode == kBdToolErase) &&
          selected_hit.kind == 16 && selected_hit.tile.x == x && selected_hit.tile.z == z;
      BdPoint3 tile = {x, y, z};
      BdPoint screen;
      if (selected || ((tool_mode == kBdToolPlane || tool_mode == kBdToolLine) && left_marks[x][z])) {
        ProjectLeftTile(&tile, &screen);
        BdCell cell;
        cell.link_height_kind = (cell.link_height_kind & kBdCellNextMask) |
            ((static_cast<u32>(z) & kBdCellHeightValueMask) << kBdCellHeightShift) |
            ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
        renderer->DrawHighlight(&screen, &cell, placement_tint);
      } else {
        u32 index = grid->slice_y[x][z];
        if (index && ((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == z) {
          ProjectLeftTile(&tile, &screen);
          u8 planes = grid->rotation == 3 ? 1 : 2;
          planes |= GetPlaneMask(index, &tile);
          renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
        }
      }
    }
  }
}

// Draw the right cursor slice with its independent mark table and opposite horizontal plane bit.
// Preserve the source's sixteen-bit loop/cursor narrowing and direct highlight or masked-cell dispatch.
void BdEditor::DrawRightSlice() {
  if (previous_hit.kind != 32 && selected_hit.kind != 32 && previous_hit.kind != 1)
    return;
  u16 x = static_cast<u16>(cursor.x);
  for (u16 y = static_cast<u16>(selection.origin.y); y < selection.origin.y + selection.size.y; ++y) {
    for (u16 z = static_cast<u16>(selection.origin.z); z < selection.origin.z + selection.size.z; ++z) {
      bool selected = (tool_mode == kBdToolBlock || tool_mode == kBdToolPlane || tool_mode == kBdToolLine ||
                          tool_mode == kBdToolErase) &&
          selected_hit.kind == 32 && selected_hit.tile.y == y && selected_hit.tile.z == z;
      BdPoint3 tile = {x, y, z};
      BdPoint screen;
      if (selected || ((tool_mode == kBdToolPlane || tool_mode == kBdToolLine) && right_marks[y][z])) {
        ProjectRightTile(&tile, &screen);
        BdCell cell;
        cell.link_height_kind = (cell.link_height_kind & kBdCellNextMask) |
            ((static_cast<u32>(z) & kBdCellHeightValueMask) << kBdCellHeightShift) |
            ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
        renderer->DrawHighlight(&screen, &cell, placement_tint);
      } else {
        u32 index = grid->slice_x[y][z];
        if (index && ((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == z) {
          ProjectRightTile(&tile, &screen);
          u8 planes = grid->rotation == 3 ? 2 : 1;
          planes |= GetPlaneMask(index, &tile);
          renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
        }
      }
    }
  }
}

// Draw the horizontal cursor slice using full-width x/y iteration and the original narrowed cursor height.
// Marked or selected cells use a temporary cell; stored slice entries require an exact height match.
void BdEditor::DrawBottomSlice() {
  u16 z = static_cast<u16>(cursor.z);
  if (previous_hit.kind != 64 && selected_hit.kind != 64 && previous_hit.kind != 4)
    return;
  for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
    for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
      bool selected = (tool_mode == kBdToolBlock || tool_mode == kBdToolPlane || tool_mode == kBdToolLine ||
                          tool_mode == kBdToolErase) &&
          selected_hit.kind == 64 && selected_hit.tile.x == x && selected_hit.tile.y == y;
      BdPoint3 tile = {x, y, z};
      BdPoint screen;
      if (selected || ((tool_mode == kBdToolPlane || tool_mode == kBdToolLine) && bottom_marks[x][y])) {
        ProjectBottomTile(&tile, &screen);
        BdCell cell;
        cell.link_height_kind = (cell.link_height_kind & kBdCellNextMask) |
            ((static_cast<u32>(z) & kBdCellHeightValueMask) << kBdCellHeightShift) |
            ((brush_kind & kBdCellKindValueMask) << kBdCellKindShift) | (brush_rotation << kBdCellRotationShift);
        renderer->DrawHighlight(&screen, &cell, placement_tint);
      } else {
        u32 index = grid->slice_z[x][y];
        if (index && ((grid->cells[index].link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask) == z) {
          ProjectBottomTile(&tile, &screen);
          u8 planes = GetPlaneMask(index, &tile) | 4;
          renderer->DrawMaskedCell(&screen, &grid->cells[index], planes);
        }
      }
    }
  }
}

// Project the left selection tile coordinate relative to the current volume origin and plane anchor.
// Preserve wrapping integer arithmetic and x86 shift counts while returning the observed screen-y word.
i32 BdEditor::ProjectLeftTile(const BdPoint3 *tile, BdPoint *screen) {
  u32 delta = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  u32 height = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  screen->x = static_cast<i32>(static_cast<u32>(left_anchor.x) - (delta << (zoom_shift & 31)));
  u32 vertical = (delta - 2 * height) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(left_anchor.y) + vertical);
  return screen->y;
}

// Project the left selection cell edge relative to the current volume origin and plane anchor.
// Preserve wrapping integer arithmetic and x86 shift counts while returning the observed screen-y word.
i32 BdEditor::ProjectLeftCell(const BdPoint3 *tile, BdPoint *screen) {
  u32 delta = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  u32 height = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  screen->x = static_cast<i32>(static_cast<u32>(left_anchor.x) - ((delta + 1) << (zoom_shift & 31)));
  u32 vertical = (delta - 2 * height - 1) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(left_anchor.y) + vertical);
  return screen->y;
}

// Project the right selection tile coordinate relative to the current volume origin and plane anchor.
// Preserve wrapping integer arithmetic and x86 shift counts while returning the observed screen-y word.
i32 BdEditor::ProjectRightTile(const BdPoint3 *tile, BdPoint *screen) {
  u32 delta = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 height = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  screen->x = static_cast<i32>(static_cast<u32>(right_anchor.x) + (delta << (zoom_shift & 31)));
  u32 vertical = (delta - 2 * height) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(right_anchor.y) + vertical);
  return screen->y;
}

// Project the right selection cell edge relative to the current volume origin and plane anchor.
// Preserve wrapping integer arithmetic and x86 shift counts while returning the observed screen-y word.
i32 BdEditor::ProjectRightCell(const BdPoint3 *tile, BdPoint *screen) {
  u32 delta = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 height = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  screen->x = static_cast<i32>(static_cast<u32>(right_anchor.x) + ((delta + 1) << (zoom_shift & 31)));
  u32 vertical = (delta - 2 * height - 1) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(right_anchor.y) + vertical);
  return screen->y;
}

// Project the bottom selection tile coordinate from its two horizontal axes and the bottom anchor.
// The observed return is the editor receiver; the private pointer/reference/void spelling remains unresolved.
BdEditor *BdEditor::ProjectBottomTile(const BdPoint3 *tile, BdPoint *screen) {
  u32 dy = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 dx = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  screen->x = static_cast<i32>(static_cast<u32>(bottom_anchor.x) + ((dy - dx) << (zoom_shift & 31)));
  u32 vertical = (dx + dy) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(bottom_anchor.y) + vertical);
  return this;
}

// Project the bottom selection cell edge from its two horizontal axes and the bottom anchor.
// Keep the original one-pixel horizontal adjustment separate from the zoomed vertical cell offset.
BdEditor *BdEditor::ProjectBottomCell(const BdPoint3 *tile, BdPoint *screen) {
  u32 dy = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 dx = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  screen->x = static_cast<i32>(static_cast<u32>(bottom_anchor.x) + ((dy - dx) << (zoom_shift & 31)) + 1);
  u32 vertical = (dx + dy + 2) << ((static_cast<u32>(zoom_shift) - 1) & 31);
  screen->y = static_cast<i32>(static_cast<u32>(bottom_anchor.y) + vertical);
  return this;
}

// Project tile coordinates using the editor's continuous float scale instead of its discrete zoom shift.
// Preserve the wrapped integer deltas and their observed vertical return before converting each output to float.
i32 BdEditor::ProjectTileF(const BdPoint3 *tile, BdPointF *screen) {
  u32 dx = static_cast<u32>(tile->x) - static_cast<u32>(selection.origin.x);
  u32 dy = static_cast<u32>(tile->y) - static_cast<u32>(selection.origin.y);
  u32 dz = static_cast<u32>(tile->z) - static_cast<u32>(selection.origin.z);
  i32 horizontal = static_cast<i32>(dy - dx), vertical = static_cast<i32>(dx + dy - 2 * dz);
  screen->x = static_cast<f32>(static_cast<f64>(horizontal) * zoom_scale + projection_anchor.x);
  screen->y = static_cast<f32>(static_cast<f64>(vertical) * zoom_scale * 0.5 + projection_anchor.y);
  return vertical;
}

#undef RGB

static u32 BdDragFrameTime;

static ScRect BdBufferRect(const BdRect *value) {
  return {value->left, value->top, value->right, value->bottom};
}

static void BdCopyDirtyView(BdEditor *editor, GxBuffer *destination, i32 view) {
  BdRect dirty = editor->GetDirtyArea(view);
  ScRect source = {0, 0, dirty.right - dirty.left, dirty.bottom - dirty.top};
  if (source.right > 0 && source.bottom > 0) {
    ScRect target = view < 2 ? BdBufferRect(&dirty) : source;
    destination->Blt(editor->drawing_buffer, source, target, nullptr);
  }
}

// Render dirty architect views, copy their updated rectangles to the window, and advance drag timing.
// Retain the source's independent lock operations, drawing order, and dirty-history updates.
void BdEditor::PaintScene() {
  if (!drawing_buffer)
    return;
  frame_time = static_cast<u32>(SDL_GetTicks());
  i32 elapsed = static_cast<i32>(frame_time - last_frame_time);
  if (elapsed > 1000 || elapsed < 0)
    elapsed = 1000;
  i32 mode = 0;
  if (field_764) {
    mode = 4;
    grid->MarkDirtyColumns(&selection);
    InvalidateView(1);
  } else if (!tool) {
    mode = 1;
  } else if (tool == 1 || tool == 2) {
    mode = 2;
  } else if (tool == 3) {
    mode = 3;
  } else if (tool == 4) {
    mode = 5;
  }
  if (draw_buffer->Lock(GxLockFlags::Fill)) {
    if (active_masks[2] & 1) {
      draw_buffer->Fill(background_pixel, draw_area, nullptr);
    } else {
      bool proceed = true;
      if (active_masks[2] & 2) {
        ScRect rectangle = BdBufferRect(&dirty_active[1][1]);
        proceed = draw_buffer->Fill(background_pixel, rectangle, nullptr) != 0;
      }
      if (proceed && !tool) {
        if (active_masks[2] & 4) {
          ScRect rectangle = BdBufferRect(&dirty_active[2][1]);
          proceed = draw_buffer->Fill(background_pixel, rectangle, nullptr) != 0;
        }
        if (proceed && (active_masks[2] & 8)) {
          ScRect rectangle = BdBufferRect(&dirty_active[3][1]);
          proceed = draw_buffer->Fill(background_pixel, rectangle, nullptr) != 0;
        }
        if (proceed && (active_masks[2] & 16)) {
          ScRect rectangle = BdBufferRect(&dirty_active[4][1]);
          draw_buffer->Fill(background_pixel, rectangle, nullptr);
        }
      }
    }
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (drawing_buffer->Lock(GxLockFlags::Fill)) {
    if (dirty_masks[0] & 1) {
      BdRect *pending = &dirty_pending[0][0];
      ScRect rectangle = {0, 0, pending->right - pending->left, pending->bottom - pending->top};
      drawing_buffer->Fill(background_pixel, rectangle, nullptr);
    } else {
      if (dirty_masks[0] & 2) {
        ScRect rectangle = BdBufferRect(&dirty_pending[1][0]);
        drawing_buffer->Fill(background_pixel, rectangle, nullptr);
      }
      if (!tool) {
        for (i32 view = 2; view <= 4; ++view) {
          if (dirty_masks[0] & (1u << view)) {
            ScRect rectangle = BdBufferRect(&dirty_pending[view][0]);
            drawing_buffer->Fill(background_pixel, rectangle, nullptr);
          }
        }
      }
    }
    drawing_buffer->Unlock(GxLockFlags::Fill);
  }
  if (dirty_masks[0] & 3)
    DrawViewHandle();
  grid->RefreshRenderSlices(&selection, &cursor);
  renderer->SetScale(zoom_scale);
  if (dirty_masks[0]) {
    renderer->SetLighting(tool, static_cast<u32>(grid->rotation) << 7);
    renderer->BeginScene();
  }
  if (mode == 1) {
    if (dirty_masks[0] & 5) {
      DrawLeftShadow();
      DrawLeftGrid(1);
      DrawLeftSlice();
      DrawLeftGrid(0);
    }
    if (dirty_masks[0] & 9) {
      DrawRightShadow();
      DrawRightGrid(1);
      DrawRightSlice();
      DrawRightGrid(0);
    }
    if (dirty_masks[0] & 17) {
      DrawBottomShadow();
      DrawBottomGrid(1);
      DrawBottomSlice();
      DrawBottomGrid(0);
    }
    if (dirty_masks[0] & 3) {
      DrawVolumeEdges(1);
      DrawIsoGrid();
      DrawFootprint();
      DrawVolumeEdges(0);
      DrawResizeHandles();
    }
  } else if ((mode == 2 || mode == 3 || mode == 5) && (dirty_masks[0] & 3)) {
    DrawColumns();
  }
  if (dirty_masks[0])
    renderer->EndScene();
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    if ((active_masks[1] | active_masks[2]) & 1) {
      BdCopyDirtyView(this, draw_buffer, 0);
    } else {
      if ((active_masks[1] | active_masks[2]) & 2)
        BdCopyDirtyView(this, draw_buffer, 1);
      if (!tool) {
        for (i32 view = 2; view <= 4; ++view)
          if ((active_masks[1] | active_masks[2]) & (1u << view))
            BdCopyDirtyView(this, draw_buffer, view);
      }
    }
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  AdvanceDirty();
  if (!field_764)
    ClearCurrentDirty();
  if (field_764 && !left_drag_active) {
    i32 amount = elapsed / 7;
    field_764 = field_764 <= 0 ? static_cast<i32>(static_cast<u32>(field_764) - amount)
                               : static_cast<i32>(static_cast<u32>(field_764) + amount);
  }
  BdDragFrameTime += static_cast<u32>(elapsed);
  if (left_drag_active || right_drag_active) {
    if (static_cast<i32>(BdDragFrameTime) > 135) {
      ApplyTileOp(&selected_hit, 1);
      if (drag_direction.x || drag_direction.y || drag_direction.z) {
        MoveSelection(&drag_direction);
        InvalidateView(1);
        BdDragFrameTime = 0;
      }
    }
  } else {
    drag_direction = {};
  }
  last_frame_time = frame_time;
}

// Mark intersecting editor views, render their current scene, and draw the window's two-color border.
// The actual drawing buffer's lock and line operations provide the border pixels.
bool BdEditor::GZPaint() {
  for (i32 view = 0; view < 10; ++view) {
    const BdRect *bounds = &view_bounds[view];
    if (dirty_area.right > bounds->left && dirty_area.left < bounds->right && dirty_area.bottom > bounds->top &&
        dirty_area.top < bounds->bottom)
      MarkViewActive(view);
  }
  MarkViewActive(0);
  PaintScene();
  if (!field_844)
    field_844 = draw_buffer->ConvertRGBValueToNative16Bit(111, 114, 163);
  if (!field_848)
    field_848 = draw_buffer->ConvertRGBValueToNative16Bit(49, 54, 124);
  if (draw_buffer->Lock(GxLockFlags::Line)) {
    i32 left = GetL(), top = GetT(), right = GetR(), bottom = GetB() - 1;
    draw_buffer->DrawLine(left, top, right, top, static_cast<i32>(static_cast<u16>(field_844)), 1);
    draw_buffer->DrawLine(left + 1, top + 1, right, top + 1, static_cast<i32>(static_cast<u16>(field_848)), 1);
    draw_buffer->DrawLine(left, top, left, bottom, static_cast<i32>(static_cast<u16>(field_844)), 1);
    draw_buffer->DrawLine(left + 1, top + 1, left + 1, bottom - 1, static_cast<i32>(static_cast<u16>(field_848)), 1);
    draw_buffer->DrawLine(right - 1, top + 2, right - 1, bottom - 1, static_cast<i32>(static_cast<u16>(field_844)), 1);
    draw_buffer->DrawLine(right, top, right, bottom, static_cast<i32>(static_cast<u16>(field_848)), 1);
    draw_buffer->DrawLine(
        left + 1, bottom - 1, right - 2, bottom - 1, static_cast<i32>(static_cast<u16>(field_844)), 1);
    draw_buffer->DrawLine(left, bottom, right, bottom, static_cast<i32>(static_cast<u16>(field_848)), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

static u32 BdNextEditorGridId = kBdEditorGridWindowIdBase;

// Replace the retained grid and mark planes, create the editor's drawing buffer, and attach its renderer.
// Initialize the view and register the original editor notifications before posting the grid change.
u8 BdEditor::SetGrid(BdGrid *value) {
  if (value)
    value->AddRef();
  if (grid) {
    u32 old_side = static_cast<u32>(grid->side), old_height = static_cast<u32>(grid->height);
    u32 next_side = value ? static_cast<u32>(value->side) : 0;
    u32 next_height = value ? static_cast<u32>(value->height) : 0;
    grid->Release();
    grid = nullptr;
    if (old_side != next_side || old_height != next_height) {
      for (u32 i = 0; i < old_side; ++i)
        delete[] left_marks[i];
      delete[] left_marks;
      left_marks = nullptr;
      for (u32 i = 0; i < old_side; ++i)
        delete[] right_marks[i];
      delete[] right_marks;
      right_marks = nullptr;
      for (u32 i = 0; i < old_side; ++i)
        delete[] bottom_marks[i];
      delete[] bottom_marks;
      bottom_marks = nullptr;
    }
  }
  grid = value;
  if (value) {
    mark_side = static_cast<u32>(value->side);
    mark_height = static_cast<u32>(value->height);
    if (!left_marks) {
      left_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        left_marks[i] = new u8[mark_height];
    }
    if (!right_marks) {
      right_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        right_marks[i] = new u8[mark_height];
    }
    if (!bottom_marks) {
      bottom_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        bottom_marks[i] = new u8[mark_side];
    }
  } else {
    mark_side = 0;
    mark_height = 0;
  }
  SetID(BdNextEditorGridId++);
  SetFlag(kWnFlagReorderable, false);
  ShowWindow();
  if (grid) {
    grid->BuildCoverage();
    ClearMarks();
  }
  if (!renderer)
    renderer = new BdRenderer;
  if (!catalog)
    catalog = BdGetCatalog();
  i32 width = draw_area.right - draw_area.left;
  i32 height = draw_area.bottom - draw_area.top;
  surface_bounds = {0, 0, width, height};
  view_bounds[0] = {draw_area.left, draw_area.top, draw_area.left + width, draw_area.top + height};
  if (!drawing_buffer) {
    BdGetGraphics()->CreateBuffer(&drawing_buffer);
    GxColorType current = BdGetGraphics()->GetScreenColorType();
    bool initialized = drawing_buffer->Init(static_cast<u32>(width), static_cast<u32>(height), current, nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
    drawing_buffer->RemoveTransparency();
    background_pixel = BdBackgroundPixel;
    if (drawing_buffer->Lock(GxLockFlags::Fill)) {
      drawing_buffer->Fill(background_pixel, nullptr);
      drawing_buffer->Unlock(GxLockFlags::Fill);
    }
    if (!initialized)
      return 0;
  }
  BdPoint anchor = {static_cast<i32>(static_cast<f64>(drawing_buffer->Width()) * 0.5),
      static_cast<i32>(static_cast<f64>(drawing_buffer->Height()) * 0.5)};
  renderer->SetOffset(&anchor);
  renderer->SetBuffer(drawing_buffer);
  renderer->SetCatalog(catalog);
  BdMaterials *materials = nullptr;
  value->GetMaterials(&materials);
  renderer->SetMaterials(materials);
  materials->Release();
  ResetView();
  selected_hit.kind = 0;
  field_764 = 0;
  static const u32 notifications[17] = {kWnMsg_ArchitectForwardInput, kWnMsg_ArchitectGridChanged,
      kWnMsg_ArchitectInvalidateGrid, kWnMsg_ArchitectSelectionBoundsChanged, kWnMsg_ArchitectRotateSelectionNegative,
      kWnMsg_ArchitectRotateSelectionPositive, kWnMsg_Unresolved30, kWnMsg_Unresolved31, kWnMsg_ArchitectZoomIn,
      kWnMsg_ArchitectZoomOut, kWnMsg_ArchitectRotateViewNegative, kWnMsg_ArchitectRotateViewPositive,
      kWnMsg_ArchitectSetBrush, kWnMsg_ArchitectSetAsset, kWnMsg_ArchitectPanView, kWnMsg_ArchitectSetTool,
      kWnMsg_AcceleratorCommand};
  ScMessageServer *server = BdGetMessageServer();
  for (u32 id : notifications)
    server->AddNotification(this, id);
  SetToolView(tool);
  InvalidateView(0);
  ScMessage message(kWnMsg_ArchitectGridChanged);
  BdGetMessageServer()->MessagePost(message, false);
  return 1;
}

// Accumulate bottom shadow coverage through each linked column below the narrowed selection end height.
// Only the first ground or object record is skipped; complementary triangle pairs collapse to the full raw FF mask.
void BdEditor::DrawBottomShadow() {
  u16 end_z = static_cast<u16>(static_cast<u32>(selection.origin.z) + selection.size.z);
  for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
    for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
      u32 index = grid->render_columns[x][y];
      if (!index)
        continue;
      u32 header = grid->cells[index].link_height_kind, kind = (header >> kBdCellKindShift) & kBdCellKindValueMask;
      if (kind == 8 || kind == 15)
        index = header & kBdCellNextMask;
      u8 mask = 0;
      while (index) {
        header = grid->cells[index].link_height_kind;
        if (((header >> kBdCellHeightShift) & kBdCellHeightValueMask) >= end_z)
          break;
        u8 coverage =
            grid->bottom_coverage[(header >> kBdCellKindShift) & kBdCellKindValueMask][header >> kBdCellRotationShift];
        mask |= ((coverage & 0x70) == 0x70 ? 0x20 : 0) | ((coverage & 0xB0) == 0xB0 ? 0x10 : 0) |
            ((coverage & 0xE0) == 0xE0 ? 0x40 : 0) | ((coverage & 0xD0) == 0xD0 ? 0x80 : 0);
        if ((mask & 0x50) == 0x50 || (mask & 0xA0) == 0xA0) {
          mask = 255;
          break;
        }
        index = header & kBdCellNextMask;
      }
      if (mask) {
        BdPoint3 tile = {x, y, 0};
        BdPoint screen;
        ProjectBottomCell(&tile, &screen);
        renderer->DrawBottomShadow(&screen, mask, BdConstructShadow);
      }
    }
  }
}

// Advance a fixed copy of the visible column heads one height at a time while accumulating left-plane coverage.
// Ground and object records contribute no shadow, and only a head at the current height advances after drawing.
void BdEditor::DrawLeftShadow() {
  for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x)
    for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y)
      shadow_columns[x][y] = grid->render_columns[x][y];
  for (u16 z = static_cast<u16>(selection.origin.z); z < selection.origin.z + selection.size.z; ++z) {
    for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
      u8 mask = 0;
      for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
        u32 index = shadow_columns[x][y];
        if (!index)
          continue;
        u32 header = grid->cells[index].link_height_kind, kind = (header >> kBdCellKindShift) & kBdCellKindValueMask;
        if (((header >> kBdCellHeightShift) & kBdCellHeightValueMask) > z || kind == 8 || kind == 15)
          continue;
        u8 coverage = grid->left_coverage[kind][header >> kBdCellRotationShift];
        mask |= ((coverage & 0x13) == 0x13 ? 1 : 0) | ((coverage & 0x23) == 0x23 ? 2 : 0) |
            ((coverage & 0x31) == 0x31 ? 0x10 : 0) | ((coverage & 0x32) == 0x32 ? 0x20 : 0);
        if ((mask & 0x21) == 0x21 || (mask & 0x12) == 0x12) {
          mask = 255;
          break;
        }
      }
      if (mask) {
        BdPoint3 tile = {x, 0, z};
        BdPoint screen;
        ProjectLeftCell(&tile, &screen);
        renderer->DrawLeftShadow(&screen, mask, BdConstructShadow);
      }
    }
    for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
      for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
        u32 index = shadow_columns[x][y];
        if (index) {
          u32 header = grid->cells[index].link_height_kind;
          if (((header >> kBdCellHeightShift) & kBdCellHeightValueMask) == z)
            shadow_columns[x][y] = header & kBdCellNextMask;
        }
      }
    }
  }
}

// Accumulate right-plane shadows with y outside x, retaining each original copied head until its exact height.
// The source's fourth coverage test compares masked92 against98 and can never set bit80; preserve that defined behavior.
void BdEditor::DrawRightShadow() {
  for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x)
    for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y)
      shadow_columns[x][y] = grid->render_columns[x][y];
  for (u16 z = static_cast<u16>(selection.origin.z); z < selection.origin.z + selection.size.z; ++z) {
    for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
      u8 mask = 0;
      for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
        u32 index = shadow_columns[x][y];
        if (!index)
          continue;
        u32 header = grid->cells[index].link_height_kind, kind = (header >> kBdCellKindShift) & kBdCellKindValueMask;
        if (((header >> kBdCellHeightShift) & kBdCellHeightValueMask) > z || kind == 8 || kind == 15)
          continue;
        u8 coverage = grid->right_coverage[kind][header >> kBdCellRotationShift];
        mask |= ((coverage & 0x19) == 0x19 ? 1 : 0) | ((coverage & 0x89) == 0x89 ? 8 : 0) |
            ((coverage & 0x91) == 0x91 ? 0x10 : 0);
        if ((mask & 0x81) == 0x81 || (mask & 0x18) == 0x18) {
          mask = 255;
          break;
        }
      }
      if (mask) {
        BdPoint3 tile = {0, y, z};
        BdPoint screen;
        ProjectRightCell(&tile, &screen);
        renderer->DrawRightShadow(&screen, mask, BdConstructShadow);
      }
    }
    for (i32 x = selection.origin.x; x < selection.origin.x + selection.size.x; ++x) {
      for (i32 y = selection.origin.y; y < selection.origin.y + selection.size.y; ++y) {
        u32 index = shadow_columns[x][y];
        if (index) {
          u32 header = grid->cells[index].link_height_kind;
          if (((header >> kBdCellHeightShift) & kBdCellHeightValueMask) == z)
            shadow_columns[x][y] = header & kBdCellNextMask;
        }
      }
    }
  }
}

// Draw the five rear edges of the isometric selection before its contents are painted.
// A zero enable byte draws nothing; both horizontal spans intentionally use the selection's y size.
void BdEditor::DrawVolumeEdges(u8 enabled) {
  if (!enabled)
    return;
  BdPoint bottom, top, right_bottom, right_top, left_bottom, left_top;
  ProjectTile(&selection.origin, &bottom);
  u32 height = static_cast<u32>(selection.size.z) << (zoom_shift & 31);
  i32 span = static_cast<i32>(static_cast<u32>(selection.size.y) << (zoom_shift & 31));
  u32 x = static_cast<u32>(bottom.x), y = static_cast<u32>(bottom.y), half = static_cast<u32>(span / 2);
  top = {bottom.x, static_cast<i32>(y - height)};
  right_bottom = {static_cast<i32>(x + span), static_cast<i32>(y + half)};
  right_top = {right_bottom.x, static_cast<i32>(y + half - height)};
  left_bottom = {static_cast<i32>(x - span), right_bottom.y};
  left_top = {left_bottom.x, right_top.y};
  renderer->DrawSegment(&bottom, &top, BdConstructIsoVolumeEdge);
  renderer->DrawSegment(&right_top, &top, BdConstructIsoVolumeEdge);
  renderer->DrawSegment(&left_top, &top, BdConstructIsoVolumeEdge);
  renderer->DrawSegment(&right_top, &right_bottom, BdConstructIsoVolumeEdge);
  renderer->DrawSegment(&left_top, &left_bottom, BdConstructIsoVolumeEdge);
}

// Render the current building into its retained thumbnail, then restore the editor's live renderer and view.
// Scale and convert the cropped render through the original image operations.
void BdEditor::RenderThumbnail() {
  static const i32 dimensions[6][3] = {
      {-1, -1, -1}, {392, 1568, 4}, {392, 1568, 3}, {784, 1960, 3}, {784, 1960, 2}, {784, 1568, 3}};
  u32 saved_background = background_pixel;
  GxBuffer *saved_buffer = renderer->GetBuffer();
  BdPoint saved_offset = renderer->offset;
  saved_buffer->AddRef();
  GxBuffer *preview = grid->buffer, *target = nullptr;
  u32 mode = grid->view_mode;
  i32 width = dimensions[mode][0], height = dimensions[mode][1];
  BdGetGraphics()->CreateBuffer(&target);
  if (!target || !preview)
    return;
  ScRect preview_area = *preview->GetBufferArea();
  target->Init(static_cast<u32>(width), static_cast<u32>(height), GxColorType(static_cast<GxColorType::Color>(14)),
      nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false);
  u16 clear = preview->ConvertRGBValueToNative16Bit(255, 0, 255);
  if (preview->Lock(GxLockFlags::Fill)) {
    preview->Fill(clear, nullptr);
    preview->Unlock(GxLockFlags::Fill);
  }
  preview->SetTransparency(clear);
  i32 saved_zoom = field_778;
  field_778 = dimensions[mode][2];
  BdPoint anchor = {static_cast<i32>(static_cast<f64>(width) * 0.5), static_cast<i32>(static_cast<f64>(height) * 0.75)};
  if (renderer) {
    renderer->SetOffset(&anchor);
    renderer->SetBuffer(target);
    renderer->DrawBackground(kBdEditorTransparencyColor);
    BdMaterials *materials = nullptr;
    grid->GetMaterials(&materials);
    renderer->SetMaterials(materials);
    materials->Release();
    renderer->SetFiltering(1);
  }
  UpdateGeometry();
  SetProjectionAnchor(anchor.x, height);
  InvalidateView(0);
  u8 rotation = grid->rotation;
  if (rotation == 1)
    RotateView(-1, 1);
  else if (rotation == 3)
    RotateView(1, 1);
  grid->BuildRenderSlices(&selection, &cursor);
  renderer->SetScale(zoom_scale);
  renderer->SetLighting(4, static_cast<u32>(grid->rotation) << 7);
  renderer->BeginScene();
  DrawColumns();
  renderer->EndScene();
  target->SetTransparency(kBdEditorTransparencyColor);
  BdRect bounds;
  BdFindImageBounds(target, &bounds);
  GxBuffer *cropped = nullptr;
  BdCropImage(target, &bounds, &cropped);
  f64 height_scale =
      static_cast<f64>(static_cast<u32>(preview_area.bottom - preview_area.top - 4)) / (bounds.bottom - bounds.top);
  f32 width_scale = static_cast<f32>(static_cast<f64>(static_cast<u32>(preview_area.right - preview_area.left - 4)) /
      static_cast<u32>(bounds.right - bounds.left));
  f32 scale = static_cast<f32>(height_scale >= width_scale ? width_scale : height_scale);
  GxBuffer *scaled = BdScaleRgba(scale, cropped, nullptr, nullptr, nullptr);
  cropped->Release();
  target->Release();
  GxColorType current = preview->GetColorType();
  GxBuffer *converted = nullptr;
  if (static_cast<i32>(current.color) == 5)
    BdConvertToRgb555(scaled, &converted);
  else
    BdConvertToRgb565(scaled, &converted);
  scaled->Release();
  ScRect converted_area = *converted->GetBufferArea();
  preview->Height();
  preview->Width();
  u32 left = (preview->Width() - static_cast<u32>(converted_area.right - converted_area.left)) >> 1;
  u32 top = (preview->Height() - static_cast<u32>(converted_area.bottom - converted_area.top)) >> 1;
  ScRect destination = {static_cast<i32>(left), static_cast<i32>(top),
      static_cast<i32>(left + static_cast<u32>(converted_area.right - converted_area.left)),
      static_cast<i32>(top + static_cast<u32>(converted_area.bottom - converted_area.top))};
  if (preview->Lock(GxLockFlags::Blt)) {
    preview->Blt(converted, converted_area, destination, nullptr);
    preview->Unlock(GxLockFlags::Blt);
  }
  if (rotation == 1)
    RotateView(1, 1);
  else if (rotation == 3)
    RotateView(-1, 1);
  background_pixel = saved_background;
  field_778 = saved_zoom;
  renderer->SetOffset(&saved_offset);
  if (saved_buffer) {
    renderer->SetBuffer(saved_buffer);
    BdMaterials *materials = nullptr;
    grid->GetMaterials(&materials);
    renderer->SetMaterials(materials);
    materials->Release();
    renderer->SetFiltering(0);
  }
  grid->BuildRenderSlices(&selection, &cursor);
  u32 half_height = saved_buffer->Height() >> 1;
  u32 half_width = saved_buffer->Width() >> 1;
  SetProjectionAnchor(static_cast<i32>(half_width), static_cast<i32>(half_height));
  UpdateGeometry();
  InvalidateView(0);
  renderer->BeginScene();
  DrawColumns();
  renderer->EndScene();
  if (saved_buffer)
    saved_buffer->Release();
}

// Render one building orientation and produce the four original scaled export images and their bounds.
// Image conversion follows the display format, with the source's per-scale tint and release order.
bool BdEditor::RenderExportView(i32 orientation, GxBuffer *const *source, BdThumbnailViews *out) {
  i32 code = static_cast<i32>(BdGetGraphics()->GetScreenColorType().color);
  bool rgb565;
  if (code == 4 || code == 7)
    rgb565 = true;
  else if (code == 5 || code == 10)
    rgb565 = false;
  else
    return false;
  renderer->RotateLights(0);
  if (!orientation) {
    renderer->SetLighting(4, 0);
  } else if (orientation >= 1 && orientation <= 3) {
    grid->Rotate(128);
    renderer->SetLighting(4, orientation * 128);
  }
  ScRect source_area = *(*source)->GetBufferArea();
  BdPoint anchor = {static_cast<i32>(static_cast<f64>(source_area.right - source_area.left) * 0.5),
      source_area.bottom - source_area.top};
  SetProjectionAnchor(anchor.x, anchor.y);
  field_778 = 3;
  UpdateGeometry();
  grid->BuildRenderSlices(&selection, &cursor);
  renderer->SetScale(zoom_scale);
  renderer->SetOffset(&anchor);
  renderer->SetFiltering(1);
  renderer->draw_edges = 1;
  renderer->DrawBackground(kBdEditorTransparencyColor);
  renderer->BeginScene();
  DrawColumns();
  renderer->EndScene();
  (*source)->SetTransparency(kBdEditorTransparencyColor);
  BdRect bounds;
  BdFindImageBounds(*source, &bounds);
  GxBuffer *cropped = nullptr;
  BdCropImage(*source, &bounds, &cropped);
  GxBuffer *scaled[4] = {BdScaleRgba(0.5f, cropped, nullptr, nullptr, nullptr),
      BdScaleRgba(0.25f, cropped, nullptr, nullptr, nullptr), BdScaleRgba(0.125f, cropped, nullptr, nullptr, nullptr),
      BdScaleRgba(0.0625f, cropped, nullptr, nullptr, nullptr)};
  cropped->Release();
  static const f32 scales[4] = {0.5f, 0.25f, 0.125f, 0.0625f};
  static const f32 tints[3] = {0.9f, 0.8f, 0.7f};
  static const u8 channels[3][3] = {{16, 17, 18}, {33, 34, 35}, {49, 51, 53}};
  for (u32 i = 0; i < 4; ++i) {
    u32 view = 4 - i;
    GxBuffer *image = scaled[i];
    if (i)
      BdShadeThumbnail(image, tints[i - 1], channels[i - 1][0], channels[i - 1][1], channels[i - 1][2]);
    if (rgb565)
      BdConvertToRgb565(image, &out->images[view]);
    else
      BdConvertToRgb555(image, &out->images[view]);
    BdRect *result = &out->bounds[view];
    result->left = 0;
    result->right = image->Width();
    result->top = static_cast<i32>(floor(static_cast<f64>(anchor.y - bounds.top) * scales[i]));
    result->bottom = image->Height() - result->top;
    image->Release();
  }
  return true;
}

static i32 BdCoordAdd(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) + static_cast<u32>(second));
}

static i32 BdCoordSubtract(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) - static_cast<u32>(second));
}

// Resize the inherited window, recenter the projection and rebuild the editor's view geometry.
// Width and height are obtained through their live window virtuals after the base resize.
void BdEditor::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnArchitectRootWidget::SetArea(left, top, right, bottom);
  i32 width = GetW(), height = GetH();
  SetProjectionAnchor(width / 2, height / 2);
  UpdateGeometry();
}

void BdEditor::SetDrawEdges(u8 enabled) {
  if (renderer)
    renderer->draw_edges = enabled;
}

// Reset the editor selection and projection to its grid and drawing buffer, then rebuild the view geometry.
// Grid modes one through five choose their original initial zoom levels; other modes retain the existing levels.
void BdEditor::ResetView() {
  if (!grid)
    return;
  u8 mode = grid->view_mode;
  if (mode >= 1 && mode <= 5) {
    i32 zoom = mode == 1 ? 4 : mode == 2 ? 3 : 2;
    field_778 = zoom;
    field_77c = zoom;
  }
  selection = grid->bounds;
  cursor = {0, 0, 0};
  ClearCurrentDirty();
  const ScRect *render_area = renderer->buffer->GetBufferArea();
  SetProjectionAnchor(BdCoordSubtract(render_area->right, render_area->left) / 2,
      BdCoordSubtract(render_area->bottom, render_area->top) / 2);
  UpdateGeometry();
}

// Fit the visible volume to its window, clamp its origin and cursor, and rebuild grid slices and paint bounds.
// Publish the anchor/area message before the zoom message and retain the final message-post result.
bool BdEditor::UpdateGeometry() {
  zoom_shift = field_778;
  zoom_scale = static_cast<f32>(static_cast<i32>(1u << (zoom_shift & 31)));
  if (grid) {
    if (tool)
      selection.size = grid->bounds.size;
    else {
      i32 height = BdCoordSubtract(GetH(), 9), x = projection_anchor.x;
      u32 shift = static_cast<u32>(zoom_shift) > 2 ? 2 : static_cast<u32>(zoom_shift);
      selection.size.x = BdCoordSubtract(x, left_anchor.x) >> shift;
      selection.size.y = BdCoordSubtract(right_anchor.x, x) >> shift;
      while (BdCoordAdd(bottom_anchor.y, static_cast<i32>(static_cast<u32>(selection.size.y) << (zoom_shift & 31))) <
          height) {
        i32 left = BdCoordSubtract(left_anchor.x, 2);
        if (left <= 0)
          break;
        left_anchor.x = left;
        right_anchor.x = BdCoordAdd(right_anchor.x, 2);
        selection.size.x = BdCoordSubtract(x, left) >> (zoom_shift & 31);
        x = projection_anchor.x;
        selection.size.y = BdCoordSubtract(right_anchor.x, x) >> (zoom_shift & 31);
      }
      while (BdCoordAdd(bottom_anchor.y, static_cast<i32>(static_cast<u32>(selection.size.y) << (zoom_shift & 31))) >
          height) {
        i32 right = BdCoordSubtract(right_anchor.x, 2);
        if (right <= 0)
          break;
        left_anchor.x = BdCoordAdd(left_anchor.x, 2);
        right_anchor.x = right;
        selection.size.x = BdCoordSubtract(x, left_anchor.x) >> (zoom_shift & 31);
        x = projection_anchor.x;
        selection.size.y = BdCoordSubtract(right, x) >> (zoom_shift & 31);
      }
      while (BdCoordSubtract(left_anchor.x, static_cast<i32>(static_cast<u32>(selection.size.x) << (zoom_shift & 31))) >
          11) {
        u32 current_shift = static_cast<u32>(zoom_shift) & 31;
        i32 right = right_anchor.x, width = GetW();
        if (BdCoordAdd(right, static_cast<i32>(static_cast<u32>(selection.size.y) << current_shift)) >=
            BdCoordSubtract(width, 9))
          break;
        left_anchor.x = BdCoordSubtract(left_anchor.x, 2);
        right_anchor.x = BdCoordAdd(right, 2);
      }
      if (selection.size.x > 32)
        selection.size.x = 32;
      if (selection.size.y > 32)
        selection.size.y = 32;
      u32 side = static_cast<u32>(selection.size.x < selection.size.y ? selection.size.x : selection.size.y);
      if (static_cast<u32>(grid->side) < side)
        side = static_cast<u32>(grid->side);
      selection.size.x = selection.size.y = static_cast<i32>(side);
      i32 top = left_anchor.y < right_anchor.y ? left_anchor.y : right_anchor.y;
      selection.size.z = static_cast<i32>((static_cast<u32>(BdCoordSubtract(top, 12)) >> (zoom_shift & 31)) - 1);
      if (selection.size.z > grid->bounds.size.z)
        selection.size.z = grid->bounds.size.z;
    }
    if (!tool && !selection.origin.z)
      selection.origin.z = 1;
    if (selection.origin.x < 0)
      selection.origin.x = 0;
    i32 end_x = BdCoordAdd(grid->bounds.origin.x, grid->bounds.size.x);
    if (BdCoordAdd(selection.origin.x, selection.size.x) > end_x)
      selection.origin.x = BdCoordSubtract(end_x, selection.size.x);
    if (selection.origin.y < 0)
      selection.origin.y = 0;
    i32 end_y = BdCoordAdd(grid->bounds.origin.y, grid->bounds.size.y);
    if (BdCoordAdd(selection.origin.y, selection.size.y) > end_y)
      selection.origin.y = BdCoordSubtract(end_y, selection.size.y);
    if (selection.origin.z < 0)
      selection.origin.z = 0;
    i32 end_z = BdCoordAdd(grid->bounds.origin.z, grid->bounds.size.z);
    if (BdCoordAdd(selection.origin.z, selection.size.z) > end_z)
      selection.origin.z = BdCoordSubtract(end_z, selection.size.z);
    BdClampPoint(&cursor, &selection);
    grid->BuildRenderSlices(&selection, &cursor);
    if (!tool)
      UpdateGridSteps();
  }
  field_780 = 0;
  field_764 = 0;
  UpdateHandleBounds();
  ScMessage message(kWnMsg_ArchitectProjectionBoundsChanged, reinterpret_cast<uptr>(&projection_anchor),
      reinterpret_cast<uptr>(&view_bounds[0]), 0);
  BdGetMessageServer()->MessagePost(message, false);
  message = ScMessage(kWnMsg_ArchitectViewModeChanged, static_cast<uptr>(field_778), 0, 0);
  return BdGetMessageServer()->MessagePost(message, false);
}

static bool BdMoveFits(i32 origin, i32 size, i32 amount, i32 low, i32 extent) {
  i32 shifted = BdCoordAdd(origin, amount);
  return shifted >= low && BdCoordAdd(shifted, size) <= BdCoordAdd(low, extent);
}

// Shift one selection axis when its full interval fits, then independently shift the cursor within the grid.
// Keep the source's repeated bounds checks and report whether either coordinate was assigned.
static bool BdMoveAxis(i32 *origin, i32 size, i32 *cursor, i32 amount, i32 low, i32 extent) {
  bool changed = false;
  if (BdMoveFits(*origin, size, amount, low, extent)) {
    *origin = BdCoordAdd(*origin, amount);
    changed = true;
  }
  i32 shifted = BdCoordAdd(*cursor, amount);
  if (shifted >= low && shifted < BdCoordAdd(low, extent)) {
    *cursor = shifted;
    changed = true;
  }
  return changed;
}

// Move the normal selection and cursor within the grid, or pan the projection for tools 1 through 4.
// Publish changed bounds before rebuilding slices; a resulting Z origin of zero skips both vertical assignments.
bool BdEditor::MoveSelection(const BdPoint3 *delta) {
  ScMessageServer *server = BdGetMessageServer();
  if (!tool) {
    if (!delta->x && !delta->y && !delta->z)
      return false;
    BdPoint3 move = *delta;
    if (move.x && !BdMoveFits(selection.origin.x, selection.size.x, move.x, grid->bounds.origin.x, grid->bounds.size.x))
      return false;
    if (move.y && !BdMoveFits(selection.origin.y, selection.size.y, move.y, grid->bounds.origin.y, grid->bounds.size.y))
      return false;
    if (move.z && !BdMoveFits(selection.origin.z, selection.size.z, move.z, grid->bounds.origin.z, grid->bounds.size.z))
      return false;
    bool changed = false;
    if (move.x)
      changed = BdMoveAxis(
          &selection.origin.x, selection.size.x, &cursor.x, move.x, grid->bounds.origin.x, grid->bounds.size.x);
    if (move.y)
      changed = BdMoveAxis(&selection.origin.y, selection.size.y, &cursor.y, move.y, grid->bounds.origin.y,
                    grid->bounds.size.y) ||
          changed;
    if (move.z && BdCoordAdd(selection.origin.z, move.z))
      changed = BdMoveAxis(&selection.origin.z, selection.size.z, &cursor.z, move.z, grid->bounds.origin.z,
                    grid->bounds.size.z) ||
          changed;
    if (changed) {
      InvalidateView(0);
      ScMessage message(kWnMsg_ArchitectSelectionBoundsChanged, reinterpret_cast<uptr>(&selection), 0, 0);
      server->MessagePost(message, false);
      grid->BuildRenderSlices(&grid->bounds, &cursor);
    }
  }
  if (tool == 1 || tool == 2 || tool == 3 || tool == 4) {
    BdPoint3 move = *delta;
    u32 x = static_cast<u32>(move.x) * 8, y = static_cast<u32>(move.y) * 8, z = static_cast<u32>(move.z) * 8;
    projection_anchor.x = BdCoordSubtract(projection_anchor.x, static_cast<i32>((y - x) << (zoom_shift & 31)));
    projection_anchor.y = BdCoordSubtract(
        projection_anchor.y, static_cast<i32>((x + y + 2 * z) << (BdCoordSubtract(zoom_shift, 1) & 31)));
    InvalidateView(0);
    UpdateHandleBounds();
    ScMessage message(kWnMsg_ArchitectProjectionBoundsChanged, reinterpret_cast<uptr>(&projection_anchor),
        reinterpret_cast<uptr>(&view_bounds[0]), 0);
    server->MessagePost(message, false);
  }
  return true;
}

// Accept a cursor tile only inside the grid volume, then clamp it to the editor's current selection.
// Rebuild the real render slices before posting the original cursor-change message with the actual cursor address.
bool BdEditor::SetCursor(const BdPoint3 *tile) {
  if (!BdContains(tile, &grid->bounds))
    return false;
  cursor = *tile;
  BdClampPoint(&cursor, &selection);
  grid->BuildRenderSlices(&selection, &cursor);
  ScMessage message = {kWnMsg_ArchitectSelectedCellChanged, reinterpret_cast<uptr>(&cursor), 0, 0};
  return BdGetMessageServer()->MessagePost(message, false);
}

void BdEditor::SetBrush(u32 kind, u32 rotation) {
  brush_kind = kind;
  brush_rotation = rotation;
}

// Set the current projection center and initialize each unset normal-view or tool-mode center independently.
// Display mode, grid view byte and the existing zoom index select the original side/bottom anchor spacing.
i32 BdEditor::SetProjectionAnchor(i32 x, i32 y) {
  static const i32 spacing800[6][6] = {{-1, -1, -1, -1, -1, -1}, {-1, 20, 20, 20, 20, 20}, {-1, 20, 20, 20, 20, 20},
      {-1, 20, 20, 20, 20, 20}, {-1, 20, 20, 20, 20, 20}, {-1, 20, 20, 20, 20, 20}};
  static const i32 spacing1024[6][6] = {{-1, -1, -1, -1, -1, -1}, {-1, 20, 28, 28, 28, 28}, {-1, 20, 28, 28, 28, 28},
      {-1, 20, 28, 28, 28, 28}, {-1, 20, 28, 28, 28, 28}, {-1, 20, 28, 28, 28, 28}};
  u8 view = grid ? grid->view_mode : 2;
  GxMode mode = BdGetGraphics()->GetGameMetrics();
  i32 spacing = 20;
  if (mode.width == 800 && mode.height == 600)
    spacing = spacing800[view][field_778];
  else if (mode.width == 1024 && mode.height == 768)
    spacing = spacing1024[view][field_778];
  i32 result = x;
  projection_anchor = {x, y};
  if (tool_anchor.x == kBdUnsetProjectionAnchor)
    tool_anchor = {x, y};
  if (view_anchor.x == kBdUnsetProjectionAnchor)
    view_anchor = {x, y};
  u32 extent = static_cast<u32>(spacing) + 4, base_x = static_cast<u32>(x), base_y = static_cast<u32>(y);
  bottom_anchor = {x, static_cast<i32>(base_y + 8 * static_cast<u32>(spacing))};
  left_anchor = {static_cast<i32>(base_x - 8 * extent), static_cast<i32>(base_y - 4 * extent)};
  right_anchor = {static_cast<i32>(base_x + 8 * extent), left_anchor.y};
  if (view_anchor.x == kBdUnsetProjectionAnchor) {
    view_anchor = projection_anchor;
    result = projection_anchor.y;
  }
  return result;
}

// Select an object material and recover the matching cell's origin when its active view uses that material.
// A missing, non-object or differently textured cell leaves the previously stored origin untouched.
void BdEditor::SetObjectBrush(u32 material, const BdPoint3 *tile) {
  brush_asset_id = material;
  i32 index = grid->FindCell(tile, 1, 0);
  if (index) {
    const BdCell *cell = &grid->cells[index];
    u32 packed = cell->link_height_kind;
    if ((packed & kBdCellKindMask) == kBdCellObjectKind &&
        cell->parts[(packed >> kBdCellRotationShift) + 1].first == material)
      grid->GetObjectOrigin(tile, &brush_origin, cell);
  }
}

void BdEditor::SetPaintAsset(u32 id) {
  paint_asset_id = id;
}

// Select an editor tool or execute its undo/redo commands through the real grid and message interfaces.
// Resolve the actual window-manager cursor, preserve per-view tool choices, and update only a visible window.
void BdEditor::SetToolMode(u32 mode) {
  if (mode == kBdToolRestorePrevious) {
    i32 category = tool;
    if (category >= 0 && category < 4) {
      mode = saved_tool_modes[category];
      if (!mode) {
        static const u32 defaults[4] = {kBdToolBlock, kBdToolFill, kBdToolComposition, kBdToolProp};
        mode = defaults[category];
      }
    }
  }
  if (mode == kBdToolUndo || mode == kBdToolRedo) {
    if (grid) {
      if (mode == kBdToolUndo)
        grid->Undo();
      else
        grid->Redo();
      InvalidateView(0);
      ScMessage message = {kWnMsg_ArchitectInvalidateGrid, 0, 0, 0};
      BdGetMessageServer()->MessagePost(message, false);
    }
    return;
  }
  field_81c = static_cast<i32>(mode);
  tool_mode = static_cast<i32>(mode);
  u32 cursor_id;
  switch (mode) {
  case kBdToolErase:
    cursor_id = kBdEraseCursorId;
    break;
  case kBdToolSelect:
    cursor_id = kBdSelectCursorId;
    break;
  case kBdToolBlock:
    cursor_id = kBdBlockCursorId;
    break;
  case kBdToolLine:
    cursor_id = kBdLineCursorId;
    break;
  case kBdToolPlane:
    cursor_id = kBdPlaneCursorId;
    break;
  case kBdToolPaintbrush:
    cursor_id = kBdPaintbrushCursorId;
    break;
  case kBdToolFill:
    cursor_id = kBdFillCursorId;
    break;
  case kBdToolComposition:
    cursor_id = kBdCompositionCursorId;
    break;
  case kBdToolProp:
    cursor_id = kBdPropCursorId;
    break;
  case 19:
    cursor_id = kBdHandleCursorId;
    break;
  default:
    cursor_id = 0;
    break;
  }
  WnCursor *cursor_icon = BdGetWindowManager()->GetCursorManager()->GetIGZCursor(cursor_id);
  if (mode == kBdToolErase)
    placement_tint = kBdErasePlacementColor;
  else if (mode == kBdToolSelect || mode == kBdToolFill)
    placement_tint = BdConstructSelectedBlock;
  else if (mode == kBdToolBlock || mode == kBdToolLine || mode == kBdToolPlane || mode == kBdToolPaintbrush ||
      mode == kBdToolComposition || mode == kBdToolProp)
    placement_tint = right_drag_active ? kBdErasePlacementColor : BdConstructSelectedBlock;
  if (handle_hover)
    cursor_icon = BdGetWindowManager()->GetCursorManager()->GetIGZCursor(kBdHandleCursorId);
  if (tool == 0 && (field_81c == kBdToolBlock || field_81c == kBdToolLine || field_81c == kBdToolPlane))
    saved_tool_modes[0] = static_cast<u32>(field_81c);
  if (tool == 1 && (field_81c == kBdToolFill || field_81c == kBdToolPaintbrush))
    saved_tool_modes[1] = static_cast<u32>(field_81c);
  if (tool == 2 && field_81c == kBdToolComposition)
    saved_tool_modes[2] = kBdToolComposition;
  if (tool == 3 && field_81c == kBdToolProp)
    saved_tool_modes[3] = kBdToolProp;
  if (cursor_icon && IsVisible())
    SetCursor(cursor_icon, true);
}

u8 BdDragToolChanged, BdDragToolLatched;

// Toggle the right-drag temporary tool and cursor, restoring the saved tool on the next update.
// Preserve the source's two module-wide drag flags and the distinct cursor activation behavior.
void BdEditor::UpdateDragTool(u8 modifiers) {
  u32 mode = static_cast<u32>(field_81c);
  tool_mode = field_81c;
  if (!right_drag_active || BdDragToolLatched) {
    SetToolMode(mode);
    BdDragToolChanged = 0;
    BdDragToolLatched = 0;
    return;
  }
  WnCursor *cursor_icon = nullptr;
  if (mode >= kBdToolBlock && mode <= kBdToolProp) {
    placement_tint = kBdErasePlacementColor;
    cursor_icon = BdGetWindowManager()->GetCursorManager()->GetIGZCursor(kBdEraseCursorId);
    if (modifiers & kWnModifierControl) {
      tool_mode = kBdToolSelect;
      placement_tint = BdConstructSelectedBlock;
      cursor_icon = BdGetWindowManager()->GetCursorManager()->GetIGZCursor(kBdSelectCursorId);
    }
  } else {
    placement_tint = BdConstructSelectedBlock;
  }
  if (cursor_icon)
    SetCursor(cursor_icon, false);
  BdDragToolChanged = 1;
  BdDragToolLatched = 1;
}

// Save the old view center, switch the editor view and restore its corresponding projection center.
// Refresh actual lighting, tool mode and geometry before posting the view-specific selection or anchor message.
void BdEditor::SetToolView(i32 view) {
  if (tool)
    tool_anchor = projection_anchor;
  else
    view_anchor = projection_anchor;
  tool = view;
  if (view)
    projection_anchor = tool_anchor;
  else
    SetProjectionAnchor(view_anchor.x, view_anchor.y);
  renderer->SetLighting(tool, 0);
  u32 mode = 0;
  if (tool >= 0 && tool < 4)
    mode = saved_tool_modes[tool];
  SetToolMode(mode);
  InvalidateView(0);
  UpdateGeometry();
  ScMessageServer *server = BdGetMessageServer();
  if (!tool) {
    ScMessage message = {kWnMsg_ArchitectSelectionBoundsChanged, reinterpret_cast<uptr>(&selection), 0, 0};
    server->MessagePost(message, false);
  } else if (tool >= 1 && tool <= 4) {
    ScMessage message = {kWnMsg_ArchitectProjectionBoundsChanged, reinterpret_cast<uptr>(&projection_anchor),
        reinterpret_cast<uptr>(&view_bounds[0]), 0};
    server->MessagePost(message, false);
  }
}

// Trace the screen point through the visible isometric cells in depth order.
// A visible face replaces the hit record; an empty ray leaves it untouched.
bool BdEditor::PickCellFace(const BdPoint *screen, BdHit *hit) {
  i32 shift = zoom_shift, half_shift = shift - 1, width = selection.size.x;
  i32 left = projection_anchor.x - (width << shift);
  if (screen->x < left || screen->x >= projection_anchor.x + (selection.size.y << shift) ||
      screen->y < projection_anchor.y - (selection.size.z << shift) ||
      screen->y >= projection_anchor.y + ((width + selection.size.y) << half_shift))
    return false;
  i32 dx = screen->x - left;
  i32 dy = ((3 * width) << half_shift) + projection_anchor.y - screen->y;
  i32 layer = (2 * dy - dx + 1) >> (shift + 1);
  i32 column = dx >> shift;
  i32 within_x = dx - (column << shift);
  i32 within_y = dy - static_cast<i32>(static_cast<u32>(column + 2 * layer) << half_shift);
  bool vertical_first = within_x + 2 * within_y + 2 - (2 << shift) < 0;
  BdPoint3 tile = {selection.origin.x + 2 * width - column - 1,
      selection.origin.y + selection.size.y - 1, selection.origin.z + layer};
  i32 phase = 0;
  while (!BdContains(&tile, &selection)) {
    if (phase == 0) {
      if (vertical_first)
        --tile.z;
      else
        --tile.x;
    } else if (phase == 1) {
      if (vertical_first)
        --tile.x;
      else
        --tile.z;
    } else if (--tile.y < selection.origin.y)
      return false;
    phase = (phase + 1) % 3;
  }
  f32 sample_x = static_cast<f32>(static_cast<f64>(screen->x) + 0.5);
  f32 sample_y = static_cast<f32>(static_cast<f64>(screen->y) - 0.5);
  for (;;) {
    i32 index = grid->FindCell(&tile, tool == 3 && tool_mode != kBdToolProp, 0);
    if (index) {
      BdPointF projected;
      ProjectTileF(&tile, &projected);
      projected.x -= sample_x;
      projected.y -= sample_y;
      i8 face = renderer->PickCellFace(&projected, &grid->cells[index]);
      if (face >= 0 && (grid->cells[index].face_mask & (1u << face))) {
        BdClampPoint(&tile, &selection);
        hit->kind = 128;
        hit->tile = tile;
        hit->cell = static_cast<u32>(index);
        hit->face = static_cast<u8>(face);
        return true;
      }
    }
    if (phase == 0) {
      if (vertical_first) {
        if (--tile.z < selection.origin.z)
          return false;
      } else if (--tile.x < selection.origin.x)
        return false;
    } else if (phase == 1) {
      if (vertical_first) {
        if (--tile.x < selection.origin.x)
          return false;
      } else if (--tile.z < selection.origin.z)
        return false;
    } else if (--tile.y < selection.origin.y)
      return false;
    phase = (phase + 1) % 3;
  }
}

// Recompute a screen hit from the selection planes, handles and visible cell faces in original order.
// A screen x of -1 reuses the last point; selection and hover state retain the editor's real hit records.
u8 BdEditor::UpdateHit(BdPoint *screen, BdHit *hit) {
  static BdPoint last_screen = {};
  if (screen->x == -1)
    *screen = last_screen;
  UpdateHandleBounds();
  hit->kind = 0;
  hit->cell = 0;
  BdPoint3 tile;
  u16 kind = 0;
  if (PickSelectionFace(screen, &tile, &kind)) {
    hit->kind = kind;
    hit->tile = tile;
    hit->cell = static_cast<u32>(grid->FindCell(&tile, tool == 3 && tool_mode != kBdToolProp, 0));
  } else
    hit->kind = kind;
  if (!tool) {
    kind = hit->kind;
    HitTestHandle(screen, &kind);
    hit->kind = kind;
  }
  PickCellFace(screen, hit);
  last_screen = *screen;
  if (tool)
    return static_cast<u8>(screen->y);
  u8 face = tool_mode == kBdToolSelect ? 0 : static_cast<u8>(((selected_hit.kind | previous_hit.kind) >> 7) & 1);
  bool handle = ((selected_hit.kind | previous_hit.kind) & 7) != 0;
  if (face)
    tool_mode = kBdToolNone;
  if (handle) {
    tool_mode = 19;
    handle_hover = 1;
  } else
    handle_hover = 0;
  return face;
}

// Move the active selection plane or cursor in response to one wheel event.
// The largest observed wheel increment becomes the shared step; refresh the hit after any movement.
bool BdEditor::GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) {
  static i32 wheel_step = 300;
  static const BdPoint3 offsets[11] = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, -1, 0}, {0, 0, 0},
      {-1, 1, 0}, {0, -1, 0}, {-1, -1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
  BdPoint screen = {x, y};
  UpdateHit(&screen, &selected_hit);
  i32 delta = wheel;
  i32 direction = delta >= 0 ? 1 : -1, index;
  switch (selected_hit.kind) {
  case 16:
    index = 2 * direction + 4;
    break;
  case 32:
    index = 4 * direction + 4;
    break;
  case 64:
    index = direction <= 0 ? 10 : 9;
    break;
  default:
    return false;
  }
  if (!tool) {
    if (delta < 0)
      delta = static_cast<i32>(0u - static_cast<u32>(delta));
    if (wheel_step < delta)
      wheel_step = delta;
    while (delta > 0) {
      const BdPoint3 *offset = offsets + index;
      if (modifiers == 2) {
        MoveSelection(offset);
        grid->MarkDirtyColumns(&selection);
      }
      if (modifiers == 0 || modifiers == 16 || modifiers == 32) {
        BdPoint3 point = {BdCoordAdd(cursor.x, offset->x), BdCoordAdd(cursor.y, offset->y),
            BdCoordAdd(cursor.z, offset->z)};
        if (BdContains(&point, &selection) || MoveSelection(offset))
          SetCursor(&point);
      }
      delta -= wheel_step;
    }
  }
  UpdateHit(&screen, &selected_hit);
  return true;
}

// Change the zoom level, refit the selection and rebuild the visible grid through the real editor methods.
// Refresh the current hit before publishing the view-specific and zoom messages.
bool BdEditor::Zoom(i32 delta) {
  i32 level = field_778;
  if ((level == field_77c && delta < 0) || BdCoordAdd(level, delta) > 5)
    return false;
  if (tool) {
    field_778 = BdCoordAdd(level, delta <= 0 ? -1 : 1);
    selection = grid->bounds;
  } else {
    if (delta <= 0) {
      selection.origin.x = BdCoordSubtract(selection.origin.x, selection.size.x);
      selection.origin.y = BdCoordSubtract(selection.origin.y, selection.size.y);
      selection.size.x = BdCoordAdd(selection.size.x, selection.size.x);
      selection.size.y = BdCoordAdd(selection.size.y, selection.size.y);
      selection.size.z = BdCoordAdd(selection.size.z, selection.size.z);
      field_778 = BdCoordSubtract(level, 1);
    } else {
      selection.origin.x = BdCoordAdd(selection.origin.x, selection.size.x / 4);
      selection.origin.y = BdCoordAdd(selection.origin.y, selection.size.y / 4);
      selection.size.x /= 2;
      selection.size.y /= 2;
      selection.size.z /= 2;
      field_778 = BdCoordAdd(level, 1);
    }
    field_780 = 1;
  }
  if (selection.origin.x < 0)
    selection.origin.x = 0;
  i32 end = BdCoordAdd(grid->bounds.origin.x, grid->bounds.size.x);
  if (BdCoordAdd(selection.origin.x, selection.size.x) > end)
    selection.origin.x = BdCoordSubtract(end, selection.size.x);
  if (selection.origin.y < 0)
    selection.origin.y = 0;
  end = BdCoordAdd(grid->bounds.origin.y, grid->bounds.size.y);
  if (BdCoordAdd(selection.origin.y, selection.size.y) > end)
    selection.origin.y = BdCoordSubtract(end, selection.size.y);
  if (selection.origin.z < 0)
    selection.origin.z = 0;
  end = BdCoordAdd(grid->bounds.origin.z, grid->bounds.size.z);
  if (BdCoordAdd(selection.origin.z, selection.size.z) > end)
    selection.origin.z = BdCoordSubtract(end, selection.size.z);
  BdClampPoint(&cursor, &selection);
  UpdateGeometry();
  BdPoint screen = selected_hit.screen;
  UpdateHit(&screen, &selected_hit);
  selected_hit.screen = screen;
  grid->BuildRenderSlices(&selection, &cursor);
  InvalidateView(0);
  ScMessageServer *server = BdGetMessageServer();
  if (!tool) {
    ScMessage selection_message(kWnMsg_ArchitectSelectionBoundsChanged, reinterpret_cast<uptr>(&selection), 0, 0);
    server->MessagePost(selection_message, false);
  } else if (tool >= 1 && tool <= 4) {
    ScMessage view_message(kWnMsg_ArchitectProjectionBoundsChanged, reinterpret_cast<uptr>(&projection_anchor),
        reinterpret_cast<uptr>(&view_bounds[0]), 0);
    server->MessagePost(view_message, false);
  }
  ScMessage zoom_message(kWnMsg_ArchitectViewModeChanged, static_cast<uptr>(field_778), 0, 0);
  server->MessagePost(zoom_message, false);
  return true;
}

// Rotate the selection around the grid's boundary rings and optionally rotate the underlying grid first.
// Clamp the transformed origin, rebuild the cursor slices and post the original view-change message.
void BdEditor::RotateView(i32 direction, u8 rotate_grid) {
  if (rotate_grid)
    grid->Rotate(direction);
  i32 n = grid->bounds.size.x, width = selection.size.x, height = selection.size.y;
  i32 x = selection.origin.x, y = selection.origin.y, right = BdCoordAdd(x, width), bottom = BdCoordAdd(y, height);
  BdPoint original[4] = {{x, y}, {x, bottom}, {right, y}, {right, bottom}};
  BdPoint rotated[4] = {{x, y}, {x, bottom}, {right, y}, {right, bottom}};
  u8 found[4] = {};
  i32 half = n / 2;
  for (i32 layer = 0; layer < half; ++layer) {
    i32 far_layer = n - layer;
    for (i32 offset = 0; offset < half; ++offset) {
      i32 far_offset = n - offset;
      for (i32 i = 0; i < 4; ++i) {
        if (found[i])
          continue;
        BdPoint point = original[i];
        if (direction > 0) {
          if (point.x == offset && point.y == far_layer) {
            rotated[i] = {layer, offset};
            found[i] = 1;
          }
          if (point.x == far_layer && point.y == far_offset) {
            rotated[i] = {offset, far_layer};
            found[i] = 1;
          }
          if (point.x == far_offset && point.y == layer) {
            rotated[i] = {far_layer, far_offset};
            found[i] = 1;
          }
          if (point.x == layer && point.y == offset) {
            rotated[i] = {far_offset, layer};
            found[i] = 1;
          }
        } else {
          if (point.x == far_layer && point.y == far_offset) {
            rotated[i] = {far_offset, layer};
            found[i] = 1;
          }
          if (point.x == offset && point.y == far_layer) {
            rotated[i] = {far_layer, far_offset};
            found[i] = 1;
          }
          if (point.x == layer && point.y == offset) {
            rotated[i] = {offset, far_offset};
            found[i] = 1;
          }
          if (point.x == far_offset && point.y == layer) {
            rotated[i] = {layer, offset};
            found[i] = 1;
          }
        }
      }
    }
  }
  i32 last_x = BdCoordSubtract(n, width), last_y = BdCoordSubtract(n, height);
  selection.origin.x = last_x;
  selection.origin.y = last_y;
  for (i32 i = 0; i < 4; ++i) {
    if (found[i]) {
      if (rotated[i].x < selection.origin.x)
        selection.origin.x = rotated[i].x;
      if (rotated[i].y < selection.origin.y)
        selection.origin.y = rotated[i].y;
    }
  }
  if (selection.origin.x < 0)
    selection.origin.x = 0;
  if (selection.origin.y < 0)
    selection.origin.y = 0;
  if (BdCoordAdd(selection.origin.x, width) > n)
    selection.origin.x = last_x;
  if (BdCoordAdd(selection.origin.y, height) > n)
    selection.origin.y = last_y;
  SetCursor(&cursor);
  ScMessage message(kWnMsg_ArchitectInvalidateGrid, 0, 0, 0);
  BdGetMessageServer()->MessagePost(message, false);
}

// Release the previous tool panel before retaining and storing the replacement window.
// A null replacement leaves the editor without a panel.
bool BdEditor::SetToolPanel(WnWindow *window) {
  ScRelease(&tool_panel);
  if (window) {
    window->AddRef();
    tool_panel = window;
  }
  return true;
}

// Apply tool and asset commands received from the editor's controls.
// Asset commands select the setter for the current tool, with object placement starting at the origin.
bool BdEditor::OnCommand(u32 kind, u32 command) {
  switch (kind) {
  case kBdCommandSelectionMode:
    SetToolView(static_cast<i32>(command));
    break;
  case kBdCommandToolMode:
    SetToolMode(command);
    break;
  case kBdCommandAsset:
    switch (tool) {
    case 1:
      SetPrimaryAsset(command);
      break;
    case 2:
      SetPaintAsset(command);
      break;
    case 3: {
      BdPoint3 origin = {0, 0, 0};
      SetObjectBrush(command, &origin);
      break;
    }
    }
    break;
  }
  return true;
}

// Update the hit with an off-screen point and end the current grid change group on a key press.
// Control-K clears grid materials and refreshes the editor view.
bool BdEditor::OnKeyDown(u32 key, u32 modifiers) {
  BdPoint screen = {-1, -1};
  UpdateHit(&screen, &selected_hit);
  modifiers &= kWnKeyWithoutRepeatMask;
  if (grid)
    grid->CloseChangeGroup();
  if (key != kWnKeyK)
    return WnArchitectRootWidget::OnKeyDown(key, modifiers);
  if (modifiers == 2) {
    if (grid)
      grid->ClearMaterials();
    InvalidateView(0);
  }
  return true;
}

// Attach the actual grid and supplied drawing buffer, allocating the three mark planes for its dimensions.
// Preserve the original ownership changes, renderer setup, notification, and view update order.
u8 BdEditor::SetGridAndBuffer(BdGrid *value, GxBuffer *buffer) {
  if (value)
    value->AddRef();
  if (grid) {
    u32 next_side = value ? static_cast<u32>(value->side) : 0;
    u32 next_height = value ? static_cast<u32>(value->height) : 0;
    grid->Release();
    grid = nullptr;
    if (mark_side != next_side || mark_height != next_height) {
      for (u32 i = 0; i < mark_side; ++i)
        delete[] left_marks[i];
      delete[] left_marks;
      left_marks = nullptr;
      for (u32 i = 0; i < mark_side; ++i)
        delete[] right_marks[i];
      delete[] right_marks;
      right_marks = nullptr;
      for (u32 i = 0; i < mark_side; ++i)
        delete[] bottom_marks[i];
      delete[] bottom_marks;
      bottom_marks = nullptr;
    }
  }
  grid = value;
  if (value) {
    mark_side = static_cast<u32>(value->side);
    mark_height = static_cast<u32>(value->height);
    if (!left_marks) {
      left_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        left_marks[i] = new u8[mark_height];
    }
    if (!right_marks) {
      right_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        right_marks[i] = new u8[mark_height];
    }
    if (!bottom_marks) {
      bottom_marks = new u8 *[mark_side];
      for (u32 i = 0; i < mark_side; ++i)
        bottom_marks[i] = new u8[mark_side];
    }
  } else {
    mark_side = 0;
    mark_height = 0;
  }
  static u32 next_id = kBdEditorGridWindowIdBase;
  SetID(next_id++);
  SetFlag(kWnFlagReorderable, false);
  if (grid)
    grid->BuildCoverage();
  SetBrush(0, 0);
  SetToolMode(kBdToolBlock);
  SetPrimaryAsset(0);
  SetPaintAsset(0);
  BdPoint3 origin = {0, 0, 0};
  SetObjectBrush(0, &origin);
  if (grid)
    ClearMarks();
  if (!renderer)
    renderer = new BdRenderer;
  i32 width = buffer->Width(), height = buffer->Height();
  view_bounds[0] = {0, 0, width, height};
  BdPoint anchor = {static_cast<i32>(static_cast<f64>(width) * 0.5),
      static_cast<i32>(static_cast<f64>(height) * 0.75)};
  renderer->SetOffset(&anchor);
  renderer->SetBuffer(buffer);
  renderer->SetCatalog(catalog);
  BdMaterials *materials = nullptr;
  value->GetMaterials(&materials);
  renderer->SetMaterials(materials);
  materials->Release();
  selected_hit.kind = 0;
  field_764 = 0;
  BdGetMessageServer()->AddNotification(this, kWnMsg_ArchitectForwardInput);
  SetProjectionAnchor(anchor.x, anchor.y);
  UpdateGeometry();
  InvalidateView(0);
  ScMessage message(kWnMsg_ArchitectGridChanged);
  BdGetMessageServer()->MessagePost(message, false);
  return 1;
}

u32 BdEditor::SetPrimaryAsset(u32 id) {
  primary_asset_id = id;
  return id;
}
