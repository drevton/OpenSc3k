#include "ui/text_controls.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio/api.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/utf8.h"
#include "fonts/sc3_bitmap_font.h"
#include "resources/resource.h"
#include "ui/replacement_manager.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/double_click.h"
#include "window/input.h"

// Insertion-at-end and complete line recalculation use separate request sentinels.
enum UiEditorPositionRequest : u32 {
  kUiEditorRecalculateAllLines = 0xffffffff,
  kUiEditorInsertionAtEnd = 0xffffffff,
};

// Editor secondary text colors may request the corresponding foreground color instead of a separate pixel value.
enum UiEditorColorSentinel : u32 {
  kUiEditorUseForegroundColor = 0x40000000,
};

// The two-column occupant display uses stable rows for name, architect and lot dimensions.
enum UiAttributeRowId : u32 {
  kUiAttributeNameRowId = 0x3581da07,
  kUiAttributeArchitectRowId = 0x3581da08,
  kUiAttributeLotSizeRowId = 0x3581da09,
};

// Add the three empty field rows, then fill their localized names while carrying each setter's byte reply forward.
// Missing resources leave that reply unchanged; retire the manager before releasing the original queried input view.
u8 UiAttributeText::InitFieldNames(ScUnknown *display) {
  if (!display)
    return 0;
  UiColumnsControl *columns = nullptr;
  if (!display->QueryInterface(IID_UiColumnsControl, reinterpret_cast<void **>(&columns)))
    return 0;
  u8 result = columns->AddRow(kUiAttributeNameRowId, nullptr, nullptr);
  if (result) {
    result = columns->AddRow(kUiAttributeArchitectRowId, nullptr, nullptr);
    if (result) {
      result = columns->AddRow(kUiAttributeLotSizeRowId, nullptr, nullptr);
      if (result) {
        ScStringView *text = nullptr;
        RsKey key(kRsTextResourceType, kRsSupplementalGameTextGroup, 0);
        UiResourceManagerRef resources;
        result = resources.manager != nullptr;
        if (resources.manager) {
          key.instance = 313;
          if (UiReplacementControls::GetStringResource(resources.manager, key, &text)) {
            result = columns->SetRowText(kUiAttributeNameRowId, 0, text);
            text->Release();
            text = nullptr;
          }
          if (result) {
            key.instance = 306;
            if (UiReplacementControls::GetStringResource(resources.manager, key, &text)) {
              result = columns->SetRowText(kUiAttributeArchitectRowId, 0, text);
              text->Release();
              text = nullptr;
            }
            if (result) {
              key.instance = 307;
              if (UiReplacementControls::GetStringResource(resources.manager, key, &text)) {
                result = columns->SetRowText(kUiAttributeLotSizeRowId, 0, text);
                text->Release();
                text = nullptr;
              }
            }
          }
        }
      }
    }
  }
  display->Release();
  return result;
}

// Write the occupant's name, architect and square lot dimensions through the queried display's second column.
// Each temporary string owns its source reference only until the corresponding row setter returns.
u8 UiAttributeText::Transfer(GeAttrib *attributes, ScUnknown *display) {
  if (!display)
    return 0;
  UiColumnsControl *columns = nullptr;
  if (!display->QueryInterface(IID_UiColumnsControl, reinterpret_cast<void **>(&columns)))
    return 0;
  if (attributes) {
    ScString *text = new ScString;
    text->AddRef();
    attributes->GetName(text);
    columns->SetRowText(kUiAttributeNameRowId, 1, text);
    text->Release();
    text = ScAddRefNotNull(new ScString);
    attributes->GetArchitectName(text);
    columns->SetRowText(kUiAttributeArchitectRowId, 1, text);
    text->Release();
    text = ScAddRefNotNull(new ScString);
    u32 size = attributes->SizeInCellsX();
    char dimensions[32];
    snprintf(dimensions, sizeof(dimensions), "%dx%d", (i32)size, (i32)size);
    text->FromChar(dimensions);
    columns->SetRowText(kUiAttributeLotSizeRowId, 1, text);
    text->Release();
  } else {
    columns->SetRowText(kUiAttributeNameRowId, 1, nullptr);
    columns->SetRowText(kUiAttributeArchitectRowId, 1, nullptr);
    columns->SetRowText(kUiAttributeLotSizeRowId, 1, nullptr);
  }
  columns->Release();
  return 0;
}

// Write an override's name, architect and byte-sized square lot dimensions, or clear all three value rows.
// Preserve separate acquisitions and retire each temporary before querying the next attribute value.
u8 UiAttributeText::Transfer(OcOverride *attributes, ScUnknown *display) {
  if (!display)
    return 0;
  UiColumnsControl *columns = nullptr;
  if (!display->QueryInterface(IID_UiColumnsControl, reinterpret_cast<void **>(&columns)))
    return 0;
  if (attributes) {
    ScString *text = new ScString;
    text->AddRef();
    attributes->GetName(text);
    columns->SetRowText(kUiAttributeNameRowId, 1, text);
    text->Release();
    text = ScAddRefNotNull(new ScString);
    attributes->GetArchitectName(text);
    columns->SetRowText(kUiAttributeArchitectRowId, 1, text);
    text->Release();
    text = ScAddRefNotNull(new ScString);
    u8 size = attributes->GetLotSize();
    char dimensions[32];
    snprintf(dimensions, sizeof(dimensions), "%dx%d", size, size);
    text->FromChar(dimensions);
    columns->SetRowText(kUiAttributeLotSizeRowId, 1, text);
    text->Release();
  } else {
    columns->SetRowText(kUiAttributeNameRowId, 1, nullptr);
    columns->SetRowText(kUiAttributeArchitectRowId, 1, nullptr);
    columns->SetRowText(kUiAttributeLotSizeRowId, 1, nullptr);
  }
  columns->Release();
  return 0;
}

UiColumns::UiColumns()
    : border_style(0), outline_colors{}, insets{}, first_width(0), minimum_widths{1, 0}, gap(2), font_height(0),
      rows(new UiColumnRow), fonts{}, text_colors{}, ellipsis(nullptr) {
  rows->next = rows;
  rows->previous = rows;
}

UiColumns::~UiColumns() {
  Shutdown();
  delete rows;
}

// Publish the secondary column interface before retaining its shared window owner; delegate other identities to the base.
bool UiColumns::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiColumnsControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiColumnsControl *>(this);
  AddRef();
  return true;
}

u32 UiColumns::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiColumns::Release() {
  return UiBaseWindow::Release();
}

// Return the primary window identity without taking another reference.
WnWindow *UiColumns::GetWindow() {
  return this;
}

bool UiColumns::Init() {
  if (!ellipsis) {
    UiWindowResourcesRef resources;
    ellipsis = resources.provider->GetText(kWnEllipsisTextId);
    if (ellipsis)
      const_cast<ScStringView *>(ellipsis)->AddRef();
  }
  SetFlag(kWnFlagPrivateBuffer, true);
  return UiBaseWindow::Init();
}

// Release row strings while the complete row list is still published, then clear rows, fonts and ellipsis before base close.
bool UiColumns::Shutdown() {
  for (UiColumnRow *row = rows->next; row != rows; row = row->next) {
    if (row->text[0])
      row->text[0]->Release();
    if (row->text[1])
      row->text[1]->Release();
  }
  UiColumnRow *row = rows->next;
  while (row != rows) {
    UiColumnRow *next = row->next;
    delete row;
    row = next;
  }
  rows->next = rows;
  rows->previous = rows;
  for (u32 column = 0; column < 2; ++column) {
    if (fonts[column])
      fonts[column]->Release();
    fonts[column] = nullptr;
  }
  if (ellipsis)
    const_cast<ScStringView *>(ellipsis)->Release();
  ellipsis = nullptr;
  return UiBaseWindow::Shutdown();
}

// Clamp dimensions to the actual border/gap minimums and invalidate only when the final extent changes.
void UiColumns::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - left), height = (i32)((u32)bottom - top);
  i32 minimum_width = (i32)((u32)insets[0] + insets[2] + gap + 2);
  i32 minimum_height = (i32)((u32)insets[1] + insets[3]);
  if (width < minimum_width)
    width = minimum_width;
  if (height < minimum_height)
    height = minimum_height;
  bool changed = width != GetW() || height != GetH();
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + width), (i32)((u32)top + height));
  if (changed)
    InvalidateSelfAndParents();
}

// Preserve the raw border selector and reset all four border insets before invalidating, even when unchanged.
u8 UiColumns::SetBorderStyle(u32 style) {
  border_style = style;
  i32 inset = style == 1 ? 2 : style == 2 ? 3 : 0;
  for (u32 side = 0; side < 4; ++side)
    insets[side] = inset;
  InvalidateSelfAndParents();
  return 1;
}

u8 UiColumns::GetBorderStyle(u32 *style) {
  *style = border_style;
  return 1;
}

u8 UiColumns::SetOutlineColor(u32 pixel) {
  return SetOutlineColors(pixel, pixel, pixel, pixel);
}

// Store the four source outline arguments in their nonsequential physical order, then invalidate the owner.
u8 UiColumns::SetOutlineColors(u32 first, u32 second, u32 third, u32 fourth) {
  outline_colors[1] = first;
  outline_colors[3] = second;
  outline_colors[0] = third;
  outline_colors[2] = fourth;
  InvalidateSelfAndParents();
  return 1;
}

// Return each outline argument through its separate original output, retaining source write order for aliased outputs.
u8 UiColumns::GetOutlineColors(u32 *first, u32 *second, u32 *third, u32 *fourth) {
  *first = outline_colors[1];
  *second = outline_colors[3];
  *third = outline_colors[0];
  *fourth = outline_colors[2];
  return 1;
}

// Retain before replacing the old font, then recompute the maximum height with the original repeated live getter calls.
u8 UiColumns::SetFont(i32 column, FtFont *font) {
  if (column >= 2)
    return 0;
  assert(column >= 0);
  if (font)
    font->AddRef();
  if (fonts[column])
    fonts[column]->Release();
  fonts[column] = font;
  font_height = 0;
  for (u32 index = 0; index < 2; ++index)
    if (fonts[index] && font_height < (u32)fonts[index]->Height(0))
      font_height = fonts[index]->Height(0);
  InvalidateSelfAndParents();
  return 1;
}

u8 UiColumns::SetFontColor(i32 column, u32 pixel) {
  if (column >= 2)
    return 0;
  assert(column >= 0);
  text_colors[column] = pixel;
  InvalidateSelfAndParents();
  return 1;
}

// Publish the appended row before retaining its first and second strings, then invalidate the shared window.
u8 UiColumns::AddRow(u32 row_id, ScStringView *first, ScStringView *second) {
  UiColumnRow *row = new UiColumnRow{rows, rows->previous, row_id, {first, second}};
  rows->previous->next = row;
  rows->previous = row;
  if (first)
    first->AddRef();
  if (second)
    second->AddRef();
  InvalidateSelfAndParents();
  return 1;
}

// Find the first matching row and publish its retained string, including a defined null value; misses leave output untouched.
u8 UiColumns::GetRowText(u32 row_id, u32 column, ScStringView **out) {
  if (out && column < 2)
    for (UiColumnRow *row = rows->next; row != rows; row = row->next)
      if (row->identifier == row_id) {
        *out = row->text[column];
        if (*out)
          (*out)->AddRef();
        return 1;
      }
  return 0;
}

// Replace only the first matching row's chosen string, retaining the incoming pointer before releasing its predecessor.
u8 UiColumns::SetRowText(u32 row_id, u32 column, ScStringView *text) {
  if (column < 2)
    for (UiColumnRow *row = rows->next; row != rows; row = row->next)
      if (row->identifier == row_id) {
        if (text)
          text->AddRef();
        if (row->text[column])
          row->text[column]->Release();
        row->text[column] = text;
        InvalidateSelfAndParents();
        return 1;
      }
  return 0;
}

i32 UiColumns::GetColumnWidth(i32 column) {
  if (column == 0)
    return first_width;
  if (column == 1)
    return (i32)((u32)GetW() - gap - first_width - insets[2] - insets[0]);
  return 0;
}

// Convert a requested second width to the stored first width; invalid columns leave the receiver unchanged.
u8 UiColumns::SetColumnWidth(i32 column, i32 width) {
  if (column == 1)
    first_width = (i32)((u32)GetW() - gap - insets[2] - insets[0] - width);
  else if (column == 0)
    first_width = width;
  else
    return 0;
  InvalidateSelfAndParents();
  return 1;
}

// Write the genuine minimum width/height and unbounded signed maxima through the caller's rectangle.
u8 UiColumns::GetMinMaxInfo(ScRect *limits) {
  limits->left = (i32)((u32)insets[0] + insets[2] + minimum_widths[0] + minimum_widths[1] + gap);
  i32 height = (i32)((u32)insets[1] + insets[3]);
  limits->right = INT32_MAX;
  limits->bottom = INT32_MAX;
  limits->top = height;
  return 1;
}

u8 UiColumns::Fit() {
  u32 count = 0;
  for (UiColumnRow *row = rows->next; row != rows; row = row->next)
    ++count;
  SetH((i32)((u32)insets[1] + insets[3] + font_height * count));
  return 1;
}

class UiEllipsisRef {
public:
  UiEllipsisRef(const ScStringView *value);
  ~UiEllipsisRef();
  u32 FitEffects(FtFont *font, ScStringView *source, u32 width, ScString *out);

private:
  const ScStringView *text;
};

// Retain the supplied ellipsis, or look it up and release the scoped provider before taking the string reference.
UiEllipsisRef::UiEllipsisRef(const ScStringView *value) : text(value) {
  if (!text) {
    UiWindowResourcesRef resources;
    text = resources.provider->GetText(kWnEllipsisTextId);
  }
  if (text)
    const_cast<ScStringView *>(text)->AddRef();
}

// Retire the scoped ellipsis before clearing its live reference field.
UiEllipsisRef::~UiEllipsisRef() {
  if (text)
    const_cast<ScStringView *>(text)->Release();
  text = nullptr;
}

// A complete fit copies the source; truncation reserves ellipsis width and appends its NUL-terminated bytes.
u32 UiEllipsisRef::FitEffects(FtFont *font, ScStringView *source, u32 width, ScString *out) {
  if (!text || !source)
    return 0;
  u32 length = source->Strlen();
  const char *bytes = source->Data();
  u32 fitted = font->Fit(bytes, length, width);
  if (!fitted)
    return 0;
  if (fitted == length) {
    out->Copy(*source);
    return length;
  }
  u32 suffix_length = text->Strlen();
  const char *suffix = text->Data();
  u32 suffix_width = font->Measure(suffix, suffix_length);
  if (suffix_width > width)
    return 0;
  bytes = source->Data();
  fitted = font->Fit(bytes, length, width - suffix_width);
  bytes = source->Data();
  out->FromChar(bytes, fitted);
  out->Append(text->ToChar());
  return out->Strlen();
}

// Fill a one-pixel vertical or horizontal span against the live drawing-area clip; diagonal spans return zero.
u8 UiColumns::DrawSegment(GxBuffer *target, u32 pixel, i32 x1, i32 y1, i32 x2, i32 y2) {
  if (x1 == x2) {
    ScRect rectangle = {x1, y1, (i32)((u32)x1 + 1), y2};
    return target->Fill(pixel, rectangle, &draw_area);
  }
  if (y1 == y2) {
    ScRect rectangle = {x1, y1, x2, (i32)((u32)y1 + 1)};
    return target->Fill(pixel, rectangle, &draw_area);
  }
  return 0;
}

// Draw outer and optional inner bevel edges under one lock, then optionally fill the remaining rectangle before unlocking.
u8 UiColumns::PaintBorder(GxBuffer *target, const ScRect *rectangle, u8 filled) {
  u8 locked = target->Lock(GxLockFlags::Fill);
  if (!locked)
    return locked;
  ScRect edges = *rectangle;
  edges.right = (i32)((u32)edges.right - 1);
  DrawSegment(target, outline_colors[3], edges.right, edges.top, edges.right, edges.bottom);
  edges.bottom = (i32)((u32)edges.bottom - 1);
  DrawSegment(target, outline_colors[3], edges.left, edges.bottom, edges.right, edges.bottom);
  DrawSegment(target, outline_colors[1], edges.left, edges.top, edges.right, edges.top);
  DrawSegment(target, outline_colors[1], edges.left, edges.top, edges.left, edges.bottom);
  if (border_style == 2) {
    edges.top = (i32)((u32)edges.top + 1);
    edges.right = (i32)((u32)edges.right - 1);
    DrawSegment(target, outline_colors[2], edges.right, edges.top, edges.right, edges.bottom);
    edges.left = (i32)((u32)edges.left + 1);
    edges.bottom = (i32)((u32)edges.bottom - 1);
    DrawSegment(target, outline_colors[2], edges.left, edges.bottom, edges.right, edges.bottom);
    DrawSegment(target, outline_colors[0], edges.left, edges.top, edges.right, edges.top);
    DrawSegment(target, outline_colors[0], edges.left, edges.top, edges.left, edges.bottom);
  }
  if (filled) {
    edges.left = (i32)((u32)edges.left + 1);
    edges.top = (i32)((u32)edges.top + 1);
    target->Fill(GetFillColor(), edges, nullptr);
  }
  target->Unlock(GxLockFlags::Fill);
  return 1;
}

// Fit each row's two strings independently and draw through the real clipped-font overload, restoring temporary colors.
bool UiColumns::GZPaint() {
  if (border_style)
    PaintBorder(draw_buffer, &draw_area, 1);
  UiEllipsisRef suffix(ellipsis);
  u32 available = (u32)draw_area.right - draw_area.left - insets[0] - insets[2], width = (u32)first_width;
  if (available < width)
    width = available;
  i32 left = (i32)((u32)draw_area.left + insets[0]);
  i32 top = (i32)((u32)draw_area.top + insets[1]);
  i32 first_right = (i32)((u32)left + width), second_left = (i32)((u32)first_right + gap);
  i32 right = (i32)((u32)draw_area.right - insets[2]), bottom = (i32)((u32)draw_area.bottom - insets[3]);
  u32 second_width = (u32)right - second_left;
  ScString output;
  for (UiColumnRow *row = rows->next; row != rows; row = row->next) {
    ScRect rectangle = {left, top, first_right, bottom};
    u32 pixel = text_colors[0];
    FtFont *font = fonts[0];
    ScStringView *source = row->text[0];
    if (source && font && suffix.FitEffects(font, source, width, &output)) {
      u32 previous = font->Color();
      if (previous != pixel)
        font->SetColor(pixel);
      font->DrawTextA(draw_buffer, (u32)rectangle.left, (u32)rectangle.top, output.begin,
          (u32)(output.end - output.begin), &rectangle);
      if (previous != pixel)
        font->SetColor(previous);
    }
    rectangle = {second_left, top, right, bottom};
    pixel = text_colors[1];
    font = fonts[1];
    source = row->text[1];
    if (source && font && second_width && suffix.FitEffects(font, source, second_width, &output)) {
      u32 previous = font->Color();
      if (previous != pixel)
        font->SetColor(pixel);
      font->DrawTextA(draw_buffer, (u32)rectangle.left, (u32)rectangle.top, output.begin,
          (u32)(output.end - output.begin), &rectangle);
      if (previous != pixel)
        font->SetColor(previous);
    }
    top = (i32)((u32)top + font_height);
  }
  return true;
}

// Store root geometry first, then refresh text width, full layout and scrollbar placement before final invalidation.
// Repeated width and synchronization calls are retained because the source observes the live virtual window state.
void UiEditor::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiBaseWindow::SetArea(left, top, right, bottom);
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
}

// Publish the stored text bounds after capturing every source field, preserving defined output aliases.
void UiEditor::GetTextArea(ScRect *output) {
  u32 left = gutter_x, top = gutter_y, width = text_width, height = text_height;
  output->left = (i32)left;
  output->top = (i32)top;
  output->right = (i32)(left + width);
  output->bottom = (i32)(top + height);
}

// Retain the incoming face before releasing the live old field, including same-font replacement.
// Publish it before checking initialized state; only initialized editors relayout and invalidate here.
void UiEditor::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  if (GetInitFlag()) {
    RecalculateAll();
    InvalidateSelfAndParents();
  }
}

bool UiEditor::SetFontColor(WnFontColor index, u32 color) {
  font_colors[(i32)index] = color;
  return true;
}

// Store the root fill color first, then invalidate only when the editor's own opaque-text flag is set.
void UiEditor::SetFillColor(u32 color) {
  UiBaseWindow::SetFillColor(color);
  if (editor_flags & 0x20)
    InvalidateSelfAndParents();
}

// Move upward only from a positive signed first-line value, then synchronize the live bar before invalidating parents.
bool UiEditor::ScrollLineUp() {
  if ((i32)first_visible <= 0)
    return false;
  --first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Move downward while the signed remaining-row calculation is positive.
bool UiEditor::ScrollLineDown() {
  if ((i32)(UiEditor::GetLineCount() - visible_rows - first_visible) <= 0)
    return false;
  ++first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Reset a nonzero first line, including a negative raw word, and synchronize the bar before parent invalidation.
bool UiEditor::ScrollToStart() {
  if (!first_visible)
    return false;
  first_visible = 0;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Jump to the last complete viewport only while the source signed remainder is positive; do not add clamping otherwise.
bool UiEditor::ScrollToEnd() {
  if ((i32)(UiEditor::GetLineCount() - visible_rows - first_visible) <= 0)
    return false;
  first_visible = UiEditor::GetLineCount() - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Subtract a page from a nonzero first line, choosing zero when the signed first-line value is below the page size.
bool UiEditor::ScrollPageUp() {
  if (!first_visible)
    return false;
  first_visible = (i32)first_visible < (i32)visible_rows ? 0 : first_visible - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

bool UiEditor::ScrollPageDown() {
  if ((i32)(UiEditor::GetLineCount() - visible_rows - first_visible) <= 0)
    return false;
  first_visible = first_visible >= UiEditor::GetLineCount() - visible_rows ? UiEditor::GetLineCount() - 1
                                                                           : first_visible + visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Parent invalidation follows the source's initialized-bit check after all layout callbacks.
void UiEditor::SetTextGutterX(u32 value) {
  if (gutter_x == value)
    return;
  gutter_x = value;
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  if (GetInitFlag())
    InvalidateSelfAndParents();
}

// Store a changed vertical gutter without recomputing layout; initialized editors are invalidated afterward.
void UiEditor::SetTextGutterY(u32 value) {
  if (gutter_y == value)
    return;
  gutter_y = value;
  if (GetInitFlag())
    InvalidateSelfAndParents();
}

// Round the current inner height down to full stored line heights, preserving unsigned gutter arithmetic.
bool UiEditor::ResizeWindowForExactLineHeights() {
  u32 gutters = 2 * gutter_y, height = (u32)GetH() - gutters;
  u32 fitted = line_height * (height / line_height);
  if (fitted != height)
    SetH((i32)(fitted + gutters));
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
  return true;
}

// Read the incoming interface's length before its Data pointer and forward the installed two-argument byte setter.
bool UiEditor::SetText(const ScStringView &value) {
  u32 length = value.Strlen();
  const char *bytes = value.Data();
  return SetText(bytes, length);
}

// Clamp the requested byte index to the current text, derive its line and column, then always invalidate the parent chain.
bool UiEditor::SetInsertionIndex(u32 index) {
  u32 length = text.ScString::Strlen();
  insertion_index = ScUtf8Floor(text.begin, length, index);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Clamp a line/column against stored offsets and that line's full byte extent, then derive the insertion index.
// Empty line storage resets the three insertion fields; neither path adds an invalidation absent from the source.
bool UiEditor::SetInsertionLineAndColumn(u32 line, u32 column) {
  if (!UiEditor::GetLineCount()) {
    insertion_index = 0;
    insertion_column = 0;
    insertion_line = 0;
    return true;
  }
  u32 selected = line < UiEditor::GetLineCount() ? line : UiEditor::GetLineCount() - 1,
      length = GetCharacterCountOfLine(selected);
  insertion_line = selected;
  insertion_column = column < length ? column : length;
  GetIndexFromLineAndColumn(insertion_line, insertion_column, &insertion_index);
  insertion_column = insertion_index - lines.begin[selected];
  return true;
}

// Store the source's250..10000 millisecond period and restart caret blinking only on an initialized window.
// Restart uses the actual clear/add flag virtuals, preserving their timer and layout side effects.
bool UiEditor::SetCaretPeriod(u32 milliseconds) {
  caret_period = milliseconds;
  if (milliseconds < 250)
    caret_period = 250;
  if (caret_period > 10000)
    caret_period = 10000;
  if (GetInitFlag()) {
    ClearFlag(0x40);
    SetFlag(0x40);
  }
  return true;
}

// Add flags only when none of the requested bits are currently present; retain the original partial-overlap no-op.
// The original signed flag argument and live SetFlags dispatch preserve all32 flag bits.
void UiEditor::SetFlag(i32 requested) {
  i32 current = editor_flags;
  if (!(current & requested))
    SetFlags(current | requested);
}

void UiEditor::ClearFlag(i32 mask) {
  if (editor_flags & mask)
    SetFlags(editor_flags & ~mask);
}

// Retain the incoming vertical atlas before releasing the live old image, then configure an existing scrollbar.
// The original ignores its rectangle argument and uses the stored rectangle or the incoming buffer's area.
bool UiEditor::SetScrollBarImageVertical(GxBuffer *incoming, const ScRect &) {
  if (incoming)
    incoming->AddRef();
  if (vertical_image)
    vertical_image->Release();
  vertical_image = incoming;
  if (incoming && vertical_scrollbar) {
    if (vertical_image_area.right > vertical_image_area.left && vertical_image_area.bottom > vertical_image_area.top)
      vertical_scrollbar->ImageSetup(vertical_image_area, incoming, 0);
    else {
      const ScRect *bounds = incoming->GetBufferArea();
      vertical_scrollbar->ImageSetup(*bounds, incoming, 0);
    }
  }
  return true;
}

// Replace the retained horizontal atlas before applying it through the live scrollbar, ignoring its setup reply.
// Preserve the unused rectangle input and reload the scrollbar after the fallback buffer-area call.
bool UiEditor::SetScrollBarImageHorizontal(GxBuffer *incoming, const ScRect &) {
  if (incoming)
    incoming->AddRef();
  if (horizontal_image)
    horizontal_image->Release();
  horizontal_image = incoming;
  if (incoming && horizontal_scrollbar) {
    if (horizontal_image_area.right > horizontal_image_area.left &&
        horizontal_image_area.bottom > horizontal_image_area.top)
      horizontal_scrollbar->ImageSetup(horizontal_image_area, incoming, 0);
    else {
      const ScRect *bounds = incoming->GetBufferArea();
      horizontal_scrollbar->ImageSetup(*bounds, incoming, 0);
    }
  }
  return true;
}

// Store any of the four editor sound identifiers. Negative indices lie outside the original array's defined contract.
bool UiEditor::SetSound(i32 index, u32 incoming) {
  if (index >= 4)
    return false;
  sounds[index] = incoming;
  return true;
}

// Return the borrowed primary window from the editor interface without retaining it.
WnWindow *UiEditor::AsIGZWin() {
  return this;
}

bool UiEditor::SetFontColor(u32 color) {
  font_colors[0] = color;
  return true;
}

bool UiEditor::GetFontColor(WnFontColor index, u32 *color) const {
  *color = font_colors[(i32)index];
  return true;
}

// Restore the selected automatic-color sentinel; unsupported roles leave all colors unchanged.
bool UiEditor::ClearFontColor(WnFontColor index) {
  switch (index) {
  case WnFontColor::Count:
    break;
  case WnFontColor::Foreground:
  case WnFontColor::DisabledForeground:
    font_colors[(i32)index] = 0;
    break;
  case WnFontColor::Background:
  case WnFontColor::DisabledBackground:
  case WnFontColor::HilitedBackground:
    font_colors[(i32)index] = kUiEditorUseForegroundColor;
    break;
  case WnFontColor::HilitedForeground:
    font_colors[(i32)index] = UINT32_MAX;
    break;
  }
  return true;
}

void UiEditor::SetCaretColor(u32 color) {
  caret_color = color;
}

u32 UiEditor::GetHighlightFillColor() {
  return highlight_color;
}

void UiEditor::SetHighlightFillColor(u32 color) {
  highlight_color = color;
}

u32 UiEditor::GetMaximumTextToAllow() {
  return maximum_text;
}

void UiEditor::SetMaximumTextToAllow(u32 value) {
  maximum_text = value;
}

u32 UiEditor::GetFirstVisibleLine() {
  return first_visible;
}

u32 UiEditor::GetLineCount() {
  return lines.begin ? (u32)(lines.end - lines.begin) : 0;
}

u32 UiEditor::GetLineHeight() {
  return line_height;
}

// Change the outline flag and store its color in the original order.
void UiEditor::SetOutlineUse(bool enabled, u32 color) {
  if (enabled)
    editor_flags |= 0x10;
  else
    editor_flags &= ~0x10;
  outline_color = color;
}

u32 UiEditor::GetTextGutterX() {
  return gutter_x;
}

u32 UiEditor::GetTextGutterY() {
  return gutter_y;
}

// Copy a complete UTF-8 prefix within the caller's byte capacity, without adding a terminator.
void UiEditor::GetText(char *output, u32 *length) {
  *length = ScUtf8Prefix(text.begin, text.ScString::Strlen(), *length);
  if (*length)
    memcpy(output, text.begin, *length);
}

void UiEditor::GetText(ScStringView *output) {
  output->FromChar(text.begin, text.ScString::Strlen());
}

// Return the borrowed text buffer, including its original unallocated null state.
const char *UiEditor::GetText() {
  return text.begin;
}

u32 UiEditor::GetTextLength() {
  return text.ScString::Strlen();
}

bool UiEditor::SetInsertMode(bool enabled) {
  insert_mode = enabled;
  return true;
}

bool UiEditor::IsFlagSet(i32 mask) {
  return (mask & editor_flags) != 0;
}

i32 UiEditor::GetFlags() {
  return editor_flags;
}

void UiEditor::SetMaxUndoLevelCount(u32 count) {
  undo_limit = count;
}

// Retire a history record's text allocation through its actual destructor without retaining spare string capacity.
// The now-unused array element remains a valid empty object until its typed allocation is deleted or reused.
static void UiRetireEditText(ScString *text) {
  ScString retired;
  retired.begin = text->begin;
  retired.end = text->end;
  retired.storage_end = text->storage_end;
  text->begin = text->end = text->storage_end = nullptr;
}

u32 UiEditHistory::Size() const {
  return begin ? (u32)(end - begin) : 0;
}

// Append one copied action, growing only this editor-specific typed record allocation when necessary.
// Old active strings retire in forward order before the new storage pointers are published.
void UiEditHistory::Append(const UiEditAction &value) {
  u32 length = Size();
  if (end == capacity) {
    u64 grown = length ? 2ull * length : 1;
    if (grown > UINT32_MAX)
      grown = UINT32_MAX;
    if (grown > SIZE_MAX / sizeof(UiEditAction))
      grown = SIZE_MAX / sizeof(UiEditAction);
    if (grown <= length)
      ScFatal(SC_ERROR_NOMEM, "SIMUI editor history is too large");
    UiEditAction *replacement = new UiEditAction[(usize)grown];
    for (u32 i = 0; i < length; ++i)
      replacement[i] = begin[i];
    for (UiEditAction *action = begin; action != end; ++action)
      UiRetireEditText(&action->text);
    delete[] begin;
    begin = replacement;
    end = replacement + length;
    capacity = replacement + (usize)grown;
  }
  *end = value;
  ++end;
}

// Shift the remaining records forward, then retire discarded tail strings before reducing the active end.
void UiEditHistory::EraseFirst(u32 count) {
  u32 length = Size(), remaining = length - count;
  for (u32 i = 0; i < remaining; ++i)
    begin[i] = begin[i + count];
  for (u32 i = remaining; i < length; ++i)
    UiRetireEditText(&begin[i].text);
  end = begin ? begin + remaining : nullptr;
}

void UiEditHistory::Pop() {
  --end;
  UiRetireEditText(&end->text);
}

// Retire active strings in their original forward order and retain the history's record allocation for reuse.
void UiEditHistory::Clear() {
  for (UiEditAction *action = begin; action != end; ++action)
    UiRetireEditText(&action->text);
  end = begin;
}

// Record the inverse erase plus any replaced text, trimming a quarter of the oldest undo entries at the source limit.
// Preserve the unused inserted-byte argument and clear redo only after the complete action has been copied into undo.
bool UiEditor::AddUndoRecordForTextInsertion(
    const UiEditPosition &position, u32 index, const char *, u32 count, const char *removed, u32 removed_count) {
  if (!(editor_flags & 0x100))
    return false;
  if (undo.Size() >= undo_limit)
    undo.EraseFirst(undo.Size() >> 2);
  UiEditAction action{};
  action.erase = 1;
  action.erase_index = index;
  action.erase_count = count;
  if (removed && removed_count) {
    action.insert = 1;
    action.insert_index = index;
    action.text.ScString::FromChar(removed, removed_count);
  } else {
    action.insert = 0;
    action.insert_index = 0;
  }
  action.position = position;
  undo.Append(action);
  redo.Clear();
  return true;
}

// Record the removed bytes and saved caret as a future insertion, without applying insertion-history trimming.
// Clear the redo range after appending the copied action; disabled history leaves both arrays unchanged.
bool UiEditor::AddUndoRecordForTextTextDeletion(
    const UiEditPosition &position, u32 index, const char *removed, u32 count) {
  if (!(editor_flags & 0x100))
    return false;
  UiEditAction action{};
  action.erase = 0;
  action.erase_index = 0;
  action.erase_count = 0;
  action.insert = 1;
  action.insert_index = index;
  action.text.ScString::FromChar(removed, count);
  action.position = position;
  undo.Append(action);
  redo.Clear();
  return true;
}

// Capture the opposite action before changing text, then restore the saved five-word caret state and retire the source action.
// Layout follows the history pop; inactive inverse fields are initialized rather than copying undefined stack bytes.
bool UiEditor::Undo() {
  if (!(editor_flags & 0x100) || !undo.Size())
    return false;
  UiEditAction *action = undo.end - 1;
  UiEditAction inverse{};
  if (action->insert) {
    inverse.erase = 1;
    inverse.erase_index = action->insert_index;
    inverse.erase_count = action->text.ScString::Strlen();
  }
  if (action->erase) {
    inverse.insert = 1;
    inverse.insert_index = action->erase_index;
    inverse.text.ScString::FromChar(text.begin + action->erase_index, action->erase_count);
  }
  inverse.position = {insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
  redo.Append(inverse);
  if (action->erase)
    UiEraseEditorText(&text, action->erase_index, action->erase_count);
  if (action->insert)
    text.Replace(action->insert_index, 0, action->text.begin, action->text.ScString::Strlen());
  insertion_line = action->position.line;
  insertion_column = action->position.column;
  insertion_index = action->position.index;
  selection_start = action->position.selection_start;
  selection_finish = action->position.selection_finish;
  undo.Pop();
  RecalculateAll();
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}

// Capture the opposite action before changing text, then restore the saved five-word caret state and retire the source action.
// Layout follows the history pop; inactive inverse fields are initialized rather than copying undefined stack bytes.
bool UiEditor::Redo() {
  if (!(editor_flags & 0x100) || !redo.Size())
    return false;
  UiEditAction *action = redo.end - 1;
  UiEditAction inverse{};
  if (action->insert) {
    inverse.erase = 1;
    inverse.erase_index = action->insert_index;
    inverse.erase_count = action->text.ScString::Strlen();
  }
  if (action->erase) {
    inverse.insert = 1;
    inverse.insert_index = action->erase_index;
    inverse.text.ScString::FromChar(text.begin + action->erase_index, action->erase_count);
  }
  inverse.position = {insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
  undo.Append(inverse);
  if (action->erase)
    UiEraseEditorText(&text, action->erase_index, action->erase_count);
  if (action->insert)
    text.Replace(action->insert_index, 0, action->text.begin, action->text.ScString::Strlen());
  insertion_line = action->position.line;
  insertion_column = action->position.column;
  insertion_index = action->position.index;
  selection_start = action->position.selection_start;
  selection_finish = action->position.selection_finish;
  redo.Pop();
  RecalculateAll();
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}

// While editable and captured, start drag scrolling, select the measured point and publish the adjusted finish index.
// Editable movement without capture stops drag scrolling; every path returns the source's constant success.
bool UiEditor::OnMouseMove(i32 x, i32 y, u32) {
  if (editor_flags & 1) {
    if (owner->GetCapture() == this) {
      StartScrollTimer();
      u32 line, column;
      GetLineAndColumnFromPosition(&line, &column, x, y);
      SetInsertionLineAndColumn(line, column);
      MoveInsertionPositionToBeforeReturnIfNecessary();
      if (selection_finish != insertion_index) {
        selection_finish = insertion_index;
        InvalidateSelfAndParents();
      }
    } else
      StopScrollTimer();
  }
  return true;
}

// Expand the existing selection to adjacent space/newline word boundaries, updating insertion at the finish.
// Clicking the separator after a word first backs up one byte; consecutive separators return without expansion.
bool UiEditor::SelectCurrentWord() {
  if (text.begin == text.end)
    return false;
  const char *begin = text.begin;
  u32 index = insertion_index;
  char code = begin[index];
  if (code == ' ' || code == '\n') {
    if (!index)
      return true;
    code = begin[index - 1];
    if (code == ' ' || code == '\n')
      return true;
    insertion_index = index - 1;
    selection_finish = index - 1;
    selection_start = index - 1;
  }
  while (selection_start) {
    code = begin[selection_start - 1];
    if (code == ' ' || code == '\n')
      break;
    --selection_start;
  }
  while (selection_finish < text.ScString::Strlen()) {
    code = begin[selection_finish];
    if (code == ' ' || code == '\n')
      break;
    ++selection_finish;
    insertion_index = selection_finish;
  }
  FixInsertionPositionIfBroken();
  return true;
}

// Use the elapsed click clock before capture and point insertion, preserving shift selection and word expansion.
// Every editable press invalidates and resets the saved horizontal column, even if capture is refused.
bool UiEditor::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  if (!(editor_flags & 1))
    return true;
  bool second_click = false;
  if (!elapsed.IsTimerRunning() || elapsed.GetElapsedTime() >= double_click_ms)
    elapsed.Restart();
  else {
    second_click = true;
    elapsed.Reset();
  }
  owner->SetCapture(this);
  u32 line, column;
  GetLineAndColumnFromPosition(&line, &column, x, y);
  SetInsertionLineAndColumn(line, column);
  MoveInsertionPositionToBeforeReturnIfNecessary();
  selection_finish = insertion_index;
  if (!(modifiers & kWnModifierShift))
    selection_start = insertion_index;
  if (second_click)
    SelectCurrentWord();
  InvalidateSelfAndParents();
  preferred_x = -1;
  return true;
}

// Release this editable window's capture before stopping an active drag-scroll timer; coordinates are ignored.
bool UiEditor::OnMouseUpL(i32, i32, u32) {
  if ((editor_flags & 1) && owner->GetCapture() == this) {
    owner->ReleaseCapture(this);
    StopScrollTimer();
  }
  return true;
}

// Convert each complete signed32 wheel hundred into a line scroll while this window does not own capture.
// Scroll results are ignored, so all requested iterations run even after the viewport reaches its limit.
bool UiEditor::GZOnMouseWheel(i32, i32, u32, i32 wheel) {
  if (owner->GetCapture() != this) {
    i32 count = wheel > 0 ? wheel / 100 : wheel / -100;
    for (i32 i = 0; i < count; ++i)
      if (wheel > 0)
        ScrollLineUp();
      else
        ScrollLineDown();
  }
  return true;
}

// Forward Escape release to the notification target with the original key/modifier payload; other releases are handled.
bool UiEditor::OnKeyUp(u32 key, u32 modifiers) {
  if (key == kWnKeyEscape)
    return SendMsg(notification_target, kWnMsg_KeyUp, kWnKeyEscape, modifiers, 0);
  return true;
}

// Invalidate editable state on focus loss before preserving the root's unhandled result.
bool UiEditor::GZOnKillFocus(WnWindow *window) {
  string236.Erase(0, kScStringToEnd);
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return UiBaseWindow::GZOnKillFocus(window);
}

// Invalidate editable state before the root's hidden-window focus redirection and unhandled result.
bool UiEditor::GZOnSetFocus(WnWindow *window) {
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return UiBaseWindow::GZOnSetFocus(window);
}

// Scroll a captured pointer beyond the original three-pixel vertical thresholds, then reenter the real drag method.
void UiEditor::DoCursorScroll() {
  if (owner->GetCapture() != this)
    return;
  GetW();
  i32 height = GetH(), x, y;
  owner->GetCursorRelativePosition(this, &x, &y);
  if (y < -3) {
    if (ScrollLineUp())
      OnMouseMove(x, (i32)(line_height >> 1), 0);
  } else if (y >= (i32)((u32)height + 3))
    if (ScrollLineDown())
      OnMouseMove(x, (i32)((u32)height - (line_height >> 1)), 0);
}

// Keep the insertion line within the vertical viewport, synchronizing only when its first row changes.
void UiEditor::PutInsertionPositionInView() {
  u32 first = insertion_line;
  if (first >= first_visible) {
    if (first < visible_rows + first_visible)
      return;
    first = first - visible_rows + 1;
  }
  first_visible = first;
  SetScrollBarValues(false);
}

// Dispatch source navigation, deletion, insert-mode and history commands through the real editor methods.
// Preserve separate vertical saved-column behavior, boundary sounds, and Escape's live target reply.
bool UiEditor::OnKeyDown(u32 key, u32 modifiers) {
  string236.Erase(0, kScStringToEnd);
  bool shift = (modifiers & kWnModifierShift) != 0;
  switch (key) {
  case kWnKeyEscape: {
    if (!(editor_flags & 1))
      goto UndoCommand;
    if (selection_start != selection_finish) {
      u32 index = insertion_index;
      selection_finish = index;
      selection_start = index;
      InvalidateSelfAndParents();
    }
    return SendMsg(notification_target, kWnMsg_KeyDown, kWnKeyEscape, modifiers, 0);
  }
  case kWnKeyY:
    if ((modifiers & kWnModifierControl) && !(modifiers & kWnModifierAlt)) {
      if (redo.Size())
        Redo();
      else
        PlaySound(3);
    }
    return true;
  case kWnKeyZ:
    goto UndoCommand;
  case kWnKeyInsert:
    if ((editor_flags & 1) && (editor_flags & 0x80))
      insert_mode = !insert_mode;
    return true;
  case kWnKeyBackspace:
  case kWnKeyDelete: {
    if (!(editor_flags & 1))
      return true;
    if (selection_start != selection_finish) {
      u32 first = selection_finish >= selection_start ? selection_start : selection_finish;
      u32 last = selection_start >= selection_finish ? selection_start : selection_finish;
      u32 count = last - first;
      ScString removed;
      UiEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      u32 erased = RemoveText(first, count, &removed);
      if (erased)
        AddUndoRecordForTextTextDeletion(position, first, removed.begin, erased);
      else
        PlaySound(1);
      SetInsertionIndex(first);
    } else if (key == kWnKeyBackspace ? insertion_index != 0 : insertion_index < text.ScString::Strlen()) {
      ScString removed;
      UiEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      u32 index = insertion_index;
      if (key == kWnKeyBackspace)
        index = insertion_index - PreviousCharSizeFromText(text.begin + index, index);
      if (RemoveChar(index, key == kWnKeyBackspace, &removed))
        AddUndoRecordForTextTextDeletion(position, index, removed.begin, removed.ScString::Strlen());
      else
        PlaySound(1);
    } else
      PlaySound(1);
    u32 index = insertion_index;
    selection_finish = index;
    selection_start = index;
    goto ResetColumn;
  }
  case kWnKeyLeft:
  case kWnKeyRight: {
    if (!(editor_flags & 1))
      return true;
    bool move = key == kWnKeyLeft ? insertion_index != 0 : insertion_index < text.ScString::Strlen();
    if (move) {
      i32 delta = key == kWnKeyLeft ? -1 : 1;
      if (modifiers & kWnModifierControl)
        IncrementInsertionPositionByWord(delta);
      else
        IncrementInsertionPosition(delta);
    } else if (selection_start == selection_finish) {
      PlaySound(2);
      goto ResetColumn;
    }
    goto SelectionChanged;
  }
  case kWnKeyHome: {
    if (!(editor_flags & 1))
      return true;
    u32 column = insertion_column;
    if (column || selection_start != selection_finish) {
      if (modifiers & kWnModifierControl) {
        insertion_index = 0;
        GetLineAndColumnFromIndex(&insertion_line, &insertion_column, 0);
      } else if (column) {
        insertion_column = 0;
        GetIndexFromLineAndColumn(insertion_line, 0, &insertion_index);
      }
      goto SelectionChanged;
    }
    goto ResetColumn;
  }
  case kWnKeyEnd: {
    if (!(editor_flags & 1))
      return true;
    u32 length = GetCharacterCountOfLine(insertion_line), column = insertion_column;
    if (column != length || selection_start != selection_finish) {
      if (modifiers & kWnModifierControl) {
        u32 index = text.ScString::Strlen();
        insertion_index = index;
        GetLineAndColumnFromIndex(&insertion_line, &insertion_column, index);
      } else if (column != length) {
        insertion_column = length;
        GetIndexFromLineAndColumn(insertion_line, length, &insertion_index);
        MoveInsertionPositionToBeforeReturnIfNecessary();
      }
      goto SelectionChanged;
    }
    goto ResetColumn;
  }
  case kWnKeyPageUp:
  case kWnKeyPageDown:
  case kWnKeyUp:
  case kWnKeyDown: {
    if (!(editor_flags & 1))
      return true;
    u32 line = insertion_line;
    bool boundary = key == kWnKeyPageUp || key == kWnKeyUp ? line == 0 : line >= UiEditor::GetLineCount() - 1;
    if (boundary) {
      if (selection_start != selection_finish)
        goto VerticalSelectionChanged;
      PlaySound(2);
      return true;
    }
    i32 x, y;
    GetPositionFromLineAndColumn(line, insertion_column, &x, &y);
    if (key == kWnKeyUp)
      --insertion_line;
    else if (key == kWnKeyDown)
      ++insertion_line;
    else if (key == kWnKeyPageUp) {
      u32 current = insertion_line, rows = visible_rows;
      insertion_line = current < rows ? 0 : current - rows;
    } else {
      u32 rows = visible_rows, current = insertion_line;
      insertion_line = current >= UiEditor::GetLineCount() - rows ? UiEditor::GetLineCount() - 1 : current + rows;
    }
    if (preferred_x == -1)
      preferred_x = x;
    GetColumnFromPosition(
        insertion_line, &insertion_column, key == kWnKeyPageUp || key == kWnKeyPageDown ? x : preferred_x);
    GetIndexFromLineAndColumn(insertion_line, insertion_column, &insertion_index);
    MoveInsertionPositionToBeforeReturnIfNecessary();
    goto VerticalSelectionChanged;
  }
  default:
    return true;
  }
UndoCommand:
  if ((modifiers & kWnModifierControl) && !(modifiers & kWnModifierAlt)) {
    if (undo.Size())
      Undo();
    else
      PlaySound(3);
  }
  return true;
SelectionChanged: {
  u32 index = insertion_index;
  selection_finish = index;
  if (!shift)
    selection_start = index;
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
}
ResetColumn:
  preferred_x = -1;
  return true;
VerticalSelectionChanged: {
  u32 index = insertion_index;
  selection_finish = index;
  if (!shift)
    selection_start = index;
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}
}

// Assemble complete UTF-8 through the original char callback before changing selection, text or history.
// Snapshot completed input across editing callbacks; incomplete input stays in the existing pending string.
bool UiEditor::GZOnCharacter(char incoming) {
  u8 character = static_cast<u8>(incoming);
  bool control = keyboard->IsKeyDown(kWnKeyControl), alt = keyboard->IsKeyDown(kWnKeyAlt);
  if (ignore_next_character) {
    ignore_next_character = false;
    string236.Erase(0, kScStringToEnd);
    return true;
  }
  if (character == '\r')
    character = '\n';
  u32 scalar;
  if (!ScAppendUtf8Byte(&string236, character, &scalar))
    return true;
  ScString input(string236);
  string236.Erase(0, kScStringToEnd);
  if ((editor_flags & 1) && (scalar >= 0x20 || scalar == 10) && !control && !alt &&
      owner->GetCapture() != static_cast<WnWindow *>(this)) {
    if ((editor_flags & 0x200) && *input.begin == 10) {
      u32 index = insertion_index;
      selection_finish = index;
      selection_start = index;
      InvalidateSelfAndParents();
    } else {
      ScString removed;
      u32 start = selection_start, index = insertion_index;
      UiEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      if (start != selection_finish) {
        u32 first = selection_finish >= start ? start : selection_finish;
        u32 last = selection_start < selection_finish ? selection_finish : selection_start;
        index = first;
        RemoveText(first, last - first, &removed);
        SetInsertionIndex(index);
        u32 current = insertion_index;
        selection_finish = current;
        selection_start = current;
      }
      if (!insert_mode && !removed.ScString::Strlen() && insertion_index < text.ScString::Strlen())
        RemoveChar(insertion_index, false, &removed);
      if (InsertText(input.begin, input.ScString::Strlen(), insertion_index))
        AddUndoRecordForTextInsertion(
            position, index, input.begin, input.ScString::Strlen(), removed.begin, removed.ScString::Strlen());
      else
        PlaySound(1);
      u32 current = insertion_index;
      preferred_x = -1;
      selection_finish = current;
      selection_start = current;
      PutInsertionPositionInView();
    }
  }
  return false;
}

// Resolve borrowed sound and resource services only for a configured sound, then forward the device's raw byte result.
// Sound fields remain unwritten until configured, as in the original constructor.
u8 UiEditor::PlaySound(u32 index) {
  u32 *sound = sounds + index;
  if (!*sound)
    return 0;
  AuSoundDevice *device = UiGetSoundDevice();
  if (!device)
    return 0;
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  i32 volume = resources->GetVolume();
  return device->SndPlay((i32)*sound, volume, 0);
}

UiEditor::UiEditor(i32 initial_flags) : elapsed(ScStopwatch::Milliseconds, 0) {
  editor_flags = 0;
  InitializeMemberVariables(initial_flags);
}

// Resolve the default font and reserve line offsets before selecting the cursor, caret timer and language mode.
// The inherited initialization follows every editor-specific effect.
bool UiEditor::Init() {
  if (GetInitFlag())
    return true;
  if (!font)
    SetFont(UiGetWindowResources()->GetFont(kWnFontDefault));
  u32 capacity = lines.begin ? (u32)(lines.capacity - lines.begin) : 0;
  if (capacity < 32) {
    u32 count = UiEditor::GetLineCount();
    u32 *replacement = new u32[32];
    for (u32 i = 0; i < count; ++i)
      replacement[i] = lines.begin[i];
    delete[] lines.begin;
    lines.begin = replacement;
    lines.end = replacement + count;
    lines.capacity = replacement + 32;
  }
  text_width = CalculateVisibleWidth();
  if (editor_flags & 1) {
    WnCursorManager *manager = owner->GetCursorManager();
    SetCursor(manager->GetIGZCursor(2), false);
  }
  if ((editor_flags & 0x40) && (editor_flags & 1) && !caret_timer.IsRunning())
    caret_timer.StartPeriodic(caret_period >> 1, nullptr);
  ScLanguageManager *manager = UiGetLanguageManager();
  language = manager ? (i32)manager->GetCurrentLanguage() : 1;
  double_byte = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
  return UiBaseWindow::Init();
}

// Initialize editor state and color roles in the original order, then register its two DWORD timer callbacks.
// Finish through the live flag setter so its timer, layout and cursor effects remain observable.
void UiEditor::InitializeMemberVariables(i32 requested) {
  UiGetGraphics();
  insert_mode = true;
  language = 0;
  double_byte = false;
  ignore_next_character = 0;
  {
    ScString mask("-", 1);
    mask_text = mask;
  }
  maximum_text = UINT32_MAX;
  preferred_x = -1;
  gutter_x = 5;
  gutter_y = 0;
  line_height = 0;
  text_width = text_height = visible_rows = first_visible = 0;
  undo_limit = 0x4000;
  caret_color = 0;
  caret_visible = false;
  outline_color = 0;
  font = nullptr;
  vertical_scrollbar = horizontal_scrollbar = nullptr;
  vertical_image = horizontal_image = nullptr;
  vertical_image_area = {};
  horizontal_image_area = {};
  selection_finish = selection_start = insertion_index = insertion_column = insertion_line = 0;
  highlight_color = 0;
  UiBaseWindow::SetFillColor(255, 255, 255);
  for (i32 i = 0; i < 6; ++i)
    ClearFontColor((WnFontColor)i);
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  caret_period = 1000;
  double_click_ms = 500;
  uptr argument = reinterpret_cast<uptr>(this);
  caret_timer.SetCallbackFunction(CaretTimer, argument);
  scroll_timer.SetCallbackFunction(ScrollTimer, argument);
  SetFlags(requested);
}

void SC3_CDECL UiEditor::CaretTimer(uptr argument) {
  WnWindow *window = reinterpret_cast<WnWindow *>(argument);
  UiGetWindowManager()->PostMessageToTarget(window, kWnMsg_EditorCaret, 0, 0, 0);
}

void SC3_CDECL UiEditor::ScrollTimer(uptr argument) {
  WnWindow *window = reinterpret_cast<WnWindow *>(argument);
  UiGetWindowManager()->PostMessageToTarget(window, kWnMsg_EditorScroll, 0, 0, 0);
}

UiEditLines::~UiEditLines() {
  delete[] begin;
}

// Resize the editor's line-offset storage while preserving its live prefix and initializing only appended words.
// This replaces the original vector storage operation without introducing a general container abstraction.
void UiEditLines::Resize(u32 count, u32 fill) {
  u32 length = begin ? (u32)(end - begin) : 0;
  u32 allocated = begin ? (u32)(capacity - begin) : 0;
  if ((usize)count > SIZE_MAX / sizeof(u32))
    ScFatal(SC_ERROR_NOMEM, "SIMUI editor line storage is too large");
  if (count > allocated) {
    u32 *replacement = new u32[count];
    for (u32 i = 0; i < length; ++i)
      replacement[i] = begin[i];
    delete[] begin;
    begin = replacement;
    capacity = replacement + count;
  }
  for (u32 i = length; i < count; ++i)
    begin[i] = fill;
  end = begin ? begin + count : nullptr;
}

UiEditHistory::~UiEditHistory() {
  Clear();
  delete[] begin;
}
