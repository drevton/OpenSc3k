#include "buildings/architect/document.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/bd_director.h"
#include "buildings/architect/files.h"
#include "common/framework.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/text_edit.h"
#include "window/window.h"

// Subtract the shifted amount from the three packed channel fields and combine them with the original alpha.
// Preserve the original unsaturated borrow/OR behavior using defined unsigned word arithmetic.
BdColor *BdColor::Darken(i32 amount) {
  u32 change = static_cast<u32>(amount);
  value = (value & kGxArgbAlphaMask) | ((value & 255) - change) | ((value & 0xFF00) - (change << 8)) |
      ((value & kGxArgbRedMask) - (change << 16));
  return this;
}

// Scale each channel through a stored float and the current rounding mode, then retain its low byte and original alpha.
// The source uses FISTP rather than truncation; normal game factors are finite and within the integer conversion range.
BdColor *BdColor::Scale(f32 factor) {
  u32 red, green, blue;
  f32 scaled = static_cast<f32>(static_cast<f64>((value >> 16) & 255) * factor);
  red = static_cast<u32>(lrintf(scaled)) & 255;
  scaled = static_cast<f32>(static_cast<f64>((value >> 8) & 255) * factor);
  green = static_cast<u32>(lrintf(scaled)) & 255;
  scaled = static_cast<f32>(static_cast<f64>(value & 255) * factor);
  blue = static_cast<u32>(lrintf(scaled)) & 255;
  value = (value & kGxArgbAlphaMask) | (red << 16) | (green << 8) | blue;
  return this;
}

static const u8 BdBuiltinColors[][3] = {
    {232, 220, 244},
    {162, 161, 201},
    {96, 150, 92},
    {210, 225, 192},
    {225, 168, 192},
    {141, 153, 143},
    {246, 247, 248},
    {236, 236, 166},
    {244, 242, 254},
    {214, 253, 120},
    {156, 162, 194},
    {112, 114, 150},
    {65, 111, 58},
    {156, 173, 136},
    {191, 135, 164},
    {104, 117, 103},
    {173, 173, 183},
    {155, 157, 104},
    {152, 160, 178},
    {148, 175, 70},
    {87, 110, 148},
    {67, 70, 103},
    {28, 66, 19},
    {98, 119, 76},
    {110, 95, 128},
    {75, 88, 71},
    {81, 82, 102},
    {72, 75, 39},
    {76, 91, 114},
    {93, 107, 26},
    {252, 223, 217},
    {243, 244, 248},
    {253, 186, 197},
    {133, 186, 226},
    {148, 180, 107},
    {248, 204, 56},
    {148, 176, 249},
    {245, 188, 127},
    {243, 222, 204},
    {216, 161, 141},
    {210, 157, 147},
    {176, 177, 178},
    {183, 139, 163},
    {97, 144, 185},
    {96, 133, 74},
    {191, 125, 38},
    {103, 122, 181},
    {214, 162, 129},
    {141, 131, 132},
    {157, 121, 107},
    {167, 91, 75},
    {100, 100, 98},
    {114, 93, 130},
    {61, 103, 147},
    {38, 81, 38},
    {135, 47, 21},
    {61, 69, 117},
    {179, 135, 131},
    {40, 41, 60},
    {96, 82, 73},
    {0, 0, 0},
    {153, 101, 101},
    {204, 135, 135},
    {255, 168, 168},
    {153, 50, 50},
    {204, 67, 67},
    {255, 84, 84},
    {153, 0, 0},
    {204, 0, 0},
    {255, 0, 0},
    {17, 17, 17},
    {153, 120, 101},
    {204, 161, 135},
    {255, 201, 168},
    {153, 89, 50},
    {204, 119, 67},
    {255, 151, 80},
    {153, 57, 0},
    {204, 76, 0},
    {255, 96, 0},
    {34, 34, 34},
    {153, 140, 101},
    {204, 187, 135},
    {255, 233, 168},
    {153, 127, 50},
    {204, 170, 67},
    {255, 212, 84},
    {153, 115, 0},
    {204, 153, 0},
    {255, 191, 0},
    {51, 51, 51},
    {146, 153, 101},
    {195, 204, 135},
    {244, 255, 168},
    {140, 153, 50},
    {187, 204, 67},
    {234, 255, 84},
    {134, 153, 0},
    {178, 204, 0},
    {223, 255, 0},
    {68, 68, 68},
    {127, 153, 101},
    {169, 204, 135},
    {212, 255, 168},
    {102, 153, 50},
    {136, 204, 67},
    {170, 255, 84},
    {76, 153, 0},
    {102, 204, 0},
    {128, 255, 0},
    {85, 85, 85},
    {107, 153, 101},
    {143, 204, 135},
    {179, 255, 168},
    {63, 153, 50},
    {84, 204, 67},
    {106, 255, 84},
    {19, 153, 0},
    {26, 204, 0},
    {32, 255, 0},
    {102, 102, 102},
    {101, 153, 114},
    {135, 204, 152},
    {168, 255, 190},
    {50, 153, 76},
    {67, 204, 101},
    {84, 255, 127},
    {0, 153, 38},
    {0, 204, 51},
    {0, 255, 64},
    {119, 119, 119},
    {101, 153, 133},
    {135, 204, 178},
    {168, 255, 222},
    {50, 153, 115},
    {67, 204, 153},
    {84, 255, 191},
    {0, 153, 96},
    {0, 204, 128},
    {0, 255, 159},
    {136, 136, 136},
    {101, 153, 153},
    {135, 204, 204},
    {168, 255, 255},
    {50, 153, 153},
    {67, 204, 204},
    {84, 255, 255},
    {0, 153, 153},
    {0, 204, 204},
    {0, 255, 255},
    {153, 153, 153},
    {101, 133, 153},
    {135, 178, 204},
    {168, 222, 255},
    {50, 115, 153},
    {67, 153, 204},
    {84, 191, 255},
    {0, 96, 153},
    {0, 128, 204},
    {0, 159, 255},
    {170, 170, 170},
    {101, 114, 153},
    {135, 152, 204},
    {168, 190, 255},
    {50, 76, 153},
    {67, 101, 204},
    {84, 127, 255},
    {0, 38, 153},
    {0, 51, 204},
    {0, 64, 255},
    {187, 187, 187},
    {107, 101, 153},
    {143, 135, 204},
    {179, 168, 255},
    {63, 50, 153},
    {84, 67, 204},
    {106, 84, 255},
    {19, 0, 153},
    {26, 0, 204},
    {32, 0, 255},
    {204, 204, 204},
    {127, 101, 153},
    {169, 135, 204},
    {212, 168, 255},
    {102, 50, 153},
    {136, 67, 204},
    {170, 84, 255},
    {76, 0, 153},
    {102, 0, 204},
    {128, 0, 255},
    {221, 221, 221},
    {146, 101, 153},
    {195, 135, 204},
    {244, 168, 255},
    {140, 50, 153},
    {187, 67, 204},
    {234, 84, 255},
    {134, 0, 153},
    {178, 0, 204},
    {223, 0, 255},
    {238, 238, 238},
    {153, 101, 140},
    {204, 135, 187},
    {255, 168, 233},
    {153, 50, 127},
    {204, 67, 170},
    {255, 84, 212},
    {153, 0, 115},
    {204, 0, 153},
    {255, 0, 191},
    {255, 255, 255},
    {153, 101, 120},
    {204, 135, 161},
    {255, 168, 201},
    {153, 50, 89},
    {204, 67, 119},
    {255, 84, 148},
    {153, 0, 57},
    {204, 0, 76},
    {255, 0, 96},
};

// Create the original 220 literal 16-by-16 BGRA color images and register their named kind 3 assets with fourteen-pixel
// views.
void BdCreateColors(BdCatalog *catalog) {
  BdRect bounds = {0, 0, 14, 14};
  for (u32 i = 0; i < sizeof(BdBuiltinColors) / sizeof(BdBuiltinColors[0]); ++i) {
    const u8 *color = BdBuiltinColors[i];
    GxBuffer *buffer = nullptr;
    BdGetGraphics()->CreateBuffer(&buffer);
    buffer->Init(16, 16, GxColorType(static_cast<GxColorType::Color>(14)), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, true);
    if (buffer->Lock(GxLockFlags::Bits)) {
      u8 *pixels = static_cast<u8 *>(buffer->GetColorSurfaceBits());
      for (u32 pixel = 0; pixel < 256; ++pixel) {
        pixels[0] = color[2];
        pixels[1] = color[1];
        pixels[2] = color[0];
        pixels[3] = 255;
        pixels += 4;
      }
      buffer->Unlock(GxLockFlags::Bits);
    }
    char text[64];
    snprintf(text, sizeof(text), "color %d,%d,%d", color[0], color[1], color[2]);
    ScString name(text, static_cast<u32>(strlen(text))), path;
    catalog->CreateAsset(&name, 3, &path, 5, buffer, &bounds, &bounds);
    buffer->Release();
  }
}

BdPart::BdPart() {
}

BdPart::BdPart(const BdPart &source) : x(source.x), y(source.y), name(source.name) {
}

BdPart::~BdPart() {
}

BdComposition::BdComposition() {
}

BdComposition::BdComposition(const BdComposition &source) : name(source.name), height(source.height), width(source.width) {
  for (u32 i = 0; i < source.parts.size; ++i)
    *parts.Append() = new BdPart(*source.parts.data[i]);
}

BdComposition::~BdComposition() {
  for (u32 i = 0; i < parts.size; ++i)
    delete parts.data[i];
}

BdGroup::BdGroup() {
}

BdGroup::BdGroup(const BdGroup &source) : name(source.name) {
  for (u32 i = 0; i < source.compositions.size; ++i)
    *compositions.Append() = new BdComposition(*source.compositions.data[i]);
}

BdGroup::~BdGroup() {
  for (u32 i = 0; i < compositions.size; ++i)
    delete compositions.data[i];
}

// Construct the name and variant strings before clearing the definition's IDs, dimensions, and reference count.
BdDefinition::BdDefinition() {
  references = 0;
  id = 0;
  size = {0, 0, 0};
  variant_ids[0] = 0;
  variant_ids[1] = 0;
  variant_ids[2] = 0;
}

BdDefinition::~BdDefinition() {
}

// Release a catalog definition and destroy its owned strings before freeing its final reference.
u32 BdDefinition::Release() {
  u32 remaining = --references;
  if (!remaining)
    delete this;
  return remaining;
}

void BdTrimTrailingSpaces(ScString *text) {
  u32 length = text->Strlen(), end = length;
  while (end && text->Data()[end - 1] == ' ')
    --end;
  if (end != length)
    text->Erase(end, length - end);
}

// Fit a string to a font width, adding the resource-defined suffix when part of the text fits.
void BdTruncateEncodedText(ScString *text, i32 width, FtFont *font) {
  if (!font || !text->Strlen())
    return;
  u32 length = text->Strlen();
  u32 count = font->Fit(text->Data(), length, static_cast<u32>(width));
  if (!count) {
    text->Erase(0, length);
    return;
  }
  if (count == length)
    return;
  WnResources *resources = BdGetWindowResources();
  if (!resources)
    return;
  const ScStringView *value = resources->GetText(kWnEllipsisTextId);
  ScString suffix;
  if (value) {
    u32 value_length = value->Strlen();
    const char *bytes = value->Data();
    suffix.FromChar(bytes, value_length);
  }
  ScRect area{};
  if (suffix.Strlen())
    font->TextArea(suffix.Data(), suffix.Strlen(), &area);
  if (area.right - area.left < width) {
    count = font->Fit(text->Data(), text->Strlen(), static_cast<u32>(width + area.left - area.right));
    text->Erase(count, text->Strlen() - count);
    BdTrimTrailingSpaces(text);
    text->Append(suffix.Data(), suffix.Strlen());
  } else {
    text->Erase(0, text->Strlen());
  }
}

u8 BdFitDialogText(ScString *text, i32 width, FtFont *font) {
  BdTruncateEncodedText(text, width, font);
  return 1;
}

u32 BD_STDCALL BdSetDialogChildEnabled(WnWindow *parent, u32 identifier, u8 enabled) {
  WnWindow *child = parent->GetChildWindowFromID(identifier);
  return child ? child->SetFlag(kWnFlagEnabled, enabled != 0) : 0;
}

// Create the original two nested beveled rectangles and attach them to the dialog.
// A failed child attachment shuts down that rectangle before releasing its control.
u8 BD_STDCALL BdCreateDialogFrame(WnWindow *parent, i32 x, i32 y, i32 width, i32 height) {
  i32 right = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(width));
  i32 bottom = static_cast<i32>(static_cast<u32>(y) + static_cast<u32>(height));
  for (u32 layer = 0; layer < 2; ++layer) {
    WnFlatControl *panel = nullptr;
    if (!BdGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
      return 0;
    WnWindow *window = panel->GetWindow();
    if (layer)
      window->SetArea(static_cast<i32>(static_cast<u32>(x) + 1), static_cast<i32>(static_cast<u32>(y) + 1),
          static_cast<i32>(static_cast<u32>(right) - 1), static_cast<i32>(static_cast<u32>(bottom) - 1));
    else
      window->SetArea(x, y, right, bottom);
    panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    panel->SetOutlineFlags(15);
    WnResources *first = BdGetWindowResources(), *second = BdGetWindowResources();
    WnResources *third = BdGetWindowResources(), *fourth = BdGetWindowResources();
    u32 low = layer ? kWnThemeOutlineDarkColorId : kWnThemeOutlineMediumColorId,
        high = layer ? kWnThemeSecondaryMutedColorId : kWnThemeOutlineLightColorId;
    u32 bottom_color = first->GetColor(high), right_color = second->GetColor(high);
    u32 top_color = third->GetColor(low), left_color = fourth->GetColor(low);
    panel->SetOutlineColors(left_color, top_color, right_color, bottom_color);
    u8 added = parent->ChildAdd(panel->GetWindow());
    if (!added)
      panel->GetWindow()->Shutdown();
    panel->Release();
    if (!added)
      return 0;
  }
  return 1;
}

// Create a dialog button from its image resource, position its window and attach it to the parent.
// Preserve the resource release before the borrowed image is passed to the window provider.
u8 BD_STDCALL BdCreateDialogButton(WnWindow *parent, u32 identifier, i32 x, i32 y, u32 image_instance, i8 key) {
  const GxColorType format(GxColorType::Color::RGB16);
  GxBuffer *image;
  {
    BdImageResource resource(image_instance, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = resource.buffer;
  }
  if (!image)
    return 0;
  WnButton *button;
  {
    ScString caption;
    WnResources *resources = BdGetWindowResources();
    const ScRect *area = image->GetBufferArea();
    button = resources->CreateFixedButton(identifier, caption, image, area, static_cast<u32>(static_cast<i32>(key)));
  }
  if (!button)
    return 0;
  WnWindow *window = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  window->GZWinMoveTo(x, y);
  u8 added = parent->ChildAdd(window);
  if (!added)
    window->Shutdown();
  button->Release();
  return added;
}

// Create a right-aligned dialog label, configure its font, color and alignment, and attach its window.
// A rejected child is shut down before the retained label interface is released.
u8 BD_STDCALL BdCreateDialogLabel(
    WnWindow *parent, u32 identifier, i32 x, i32 y, const ScStringView *caption, u32 alignment) {
  WnResources *resources = BdGetWindowResources();
  if (!resources)
    return 0;
  WnLabelControl *label = resources->CreateLabel(identifier, *caption);
  if (!label)
    return 0;
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  label->SetLabelFlag(kWnLabelUseFontColors, false);
  label->SetTextColor(resources->GetColor(kWnThemeMutedTextColorId));
  label->Fit();
  label->SetAlignment(alignment);
  WnWindow *measure = label->GetWindow(), *target = label->GetWindow();
  i32 left = static_cast<i32>(static_cast<u32>(x) - static_cast<u32>(measure->GetW()));
  target->GZWinMoveTo(left, y);
  u8 added = parent->ChildAdd(label->GetWindow());
  if (!added)
    label->GetWindow()->Shutdown();
  label->Release();
  return added;
}

// The frame is attempted first; the result reports the input window's attachment.
u8 BD_STDCALL BdCreateDialogInput(
    WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height, ScStringView *text, u8 editable) {
  BdCreateDialogFrame(parent, x, y, width, height);
  WnTextControl *input = nullptr;
  if (!BdGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, reinterpret_cast<void **>(&input)))
    return 0;
  input->AsIGZWin()->SetID(identifier);
  i32 left = static_cast<i32>(static_cast<u32>(x) + 2), top = static_cast<i32>(static_cast<u32>(y) + 2);
  i32 right = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(width) - 2);
  i32 bottom = static_cast<i32>(static_cast<u32>(y) + static_cast<u32>(height) - 2);
  input->AsIGZWin()->SetArea(left, top, right, bottom);
  input->SetFont(BdGetWindowResources()->GetFont(kWnFontUiSmall));
  input->SetText(text);
  input->SetOutline(0);
  input->SetCharacterLimit(50);
  input->SetEditable(editable);
  u8 added = parent->ChildAdd(input->AsIGZWin());
  if (editable)
    input->AsIGZWin()->SetFillColor(255, 255, 255);
  else {
    WnResources *resources = BdGetWindowResources();
    WnWindow *window = input->AsIGZWin();
    window->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
  }
  if (!added)
    input->AsIGZWin()->Shutdown();
  input->Release();
  return added;
}

// Create the dialog's framed multiline editor with its initial text and editable background.
// Attachment failure shuts down the editor window before its interface is released.
u8 BD_STDCALL BdCreateDialogEditor(
    WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height, ScStringView *text, u8 editable) {
  BdCreateDialogFrame(parent, x, y, width, height);
  WnEditorControl *editor = nullptr;
  if (!BdGetCom()->GetClassObject(kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor)))
    return 0;
  editor->AsIGZWin()->SetID(identifier);
  editor->AsIGZWin()->SetFlag(kWnFlagPrivateBuffer, true);
  editor->AsIGZWin()->SetFillColor(255, 255, 255);
  editor->SetFlag(8);
  editor->SetFont(BdGetWindowResources()->GetFont(kWnFontUiSmall));
  i32 left = static_cast<i32>(static_cast<u32>(x) + 2), top = static_cast<i32>(static_cast<u32>(y) + 2);
  i32 right = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(width) - 2);
  i32 bottom = static_cast<i32>(static_cast<u32>(y) + static_cast<u32>(height) - 2);
  editor->SetArea(left, top, right, bottom);
  editor->SetText(*text);
  if (editable)
    editor->SetFlag(1);
  else
    editor->ClearFlag(1);
  u8 added = parent->ChildAdd(editor->AsIGZWin());
  if (editable)
    editor->AsIGZWin()->SetFillColor(255, 255, 255);
  else {
    WnResources *resources = BdGetWindowResources();
    WnWindow *window = editor->AsIGZWin();
    window->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
  }
  if (!added)
    editor->AsIGZWin()->Shutdown();
  editor->Release();
  return added;
}

// Create and attach a styled dialog label, selecting either the theme color or the original two converted colors.
// Nonzero dimensions are applied only after successful attachment, with the width enabling label flag two.
u8 BD_STDCALL BdCreateStyledLabel(WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height,
    const ScStringView *caption, u32 alignment, u32 font, u8 theme_color) {
  WnResources *resources = BdGetWindowResources();
  if (!resources)
    return 0;
  WnLabelControl *label = resources->CreateLabel(identifier, *caption);
  if (!label)
    return 0;
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(font));
  label->SetLabelFlag(kWnLabelUseFontColors, false);
  if (theme_color) {
    label->SetTextColor(resources->GetColor(kWnThemeMutedTextColorId));
  } else {
    u16 first = BdGetGraphics()->ConvertRGBValueToNative16Bit(255, 215, 127);
    label->SetTextColor(0, first);
    u16 second = BdGetGraphics()->ConvertRGBValueToNative16Bit(180, 180, 210);
    label->SetTextColor(1, second);
  }
  label->SetAlignment(alignment);
  label->GetWindow()->GZWinMoveTo(x, y);
  u8 added = parent->ChildAdd(label->GetWindow());
  if (added) {
    if (width) {
      label->GetWindow()->SetW(width);
      label->SetLabelFlag(kWnLabelWrapText, true);
    }
    if (height)
      label->GetWindow()->SetH(height);
  } else {
    label->GetWindow()->Shutdown();
  }
  label->Release();
  return added;
}

BdDocument::BdDocument()
    : byte_004(0), word_044(0), bytes_048{2, 0, 28, 0}, word_04c(0), references(0), word_054(0), words_06c{} {
}

BdDocument::~BdDocument() {
}

// Retain before publishing either accepted identity. An absent output or unrecognized IID clears the output.
bool BdDocument::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_BdDocument))
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  *out = this;
  return true;
}

u32 BdDocument::AddRef() {
  return ++references;
}

// Decrement with unsigned wrapping and destroy only on the original1-to0 transition.
u32 BdDocument::Release() {
  u32 remaining = --references;
  if (!remaining)
    delete this;
  return remaining;
}

u32 BdDocument::GetByte04() {
  return byte_004;
}

// Allocate an independent first-text string and publish its single retained interface through a required output.
bool BdDocument::GetText08(ScStringView **out) {
  if (!out)
    return false;
  ScString *copy = new ScString(text_008);
  copy->AddRef();
  *out = copy;
  return true;
}

// Allocate an independent second-text string, preserving the document's own storage and reference count.
bool BdDocument::GetText1C(ScStringView **out) {
  if (!out)
    return false;
  ScString *copy = new ScString(text_01c);
  copy->AddRef();
  *out = copy;
  return true;
}

// Allocate an independent third-text string and return its retained string interface.
bool BdDocument::GetText30(ScStringView **out) {
  if (!out)
    return false;
  ScString *copy = new ScString(text_030);
  copy->AddRef();
  *out = copy;
  return true;
}

u32 BdDocument::GetWord44() {
  return word_044;
}

bool BdDocument::GetWord54(u32 *out) {
  *out = word_054;
  return true;
}

// Clear the output before constructing the fourth owned string copy, then publish its retained interface.
bool BdDocument::GetText58(ScStringView **out) {
  if (!out)
    return false;
  *out = nullptr;
  ScString *copy = new ScString(text_058);
  copy->AddRef();
  *out = copy;
  return true;
}

u8 BdDocument::SetByte04(u8 value) {
  byte_004 = value;
  return value;
}

// Read the input's length before its ToChar operation, then replace the first text through FromChar.
bool BdDocument::SetText08(const ScStringView *value) {
  if (!value)
    return false;
  u32 length = value->Strlen();
  text_008.FromChar(value->ToChar(), length);
  return true;
}

// Replace the second text using the original length/read/write callback order; null input reports false.
bool BdDocument::SetText1C(const ScStringView *value) {
  if (!value)
    return false;
  u32 length = value->Strlen();
  text_01c.FromChar(value->ToChar(), length);
  return true;
}

// Replace the third text after querying its input length and pointer, preserving the genuine Boolean result.
bool BdDocument::SetText30(const ScStringView *value) {
  if (!value)
    return false;
  u32 length = value->Strlen();
  text_030.FromChar(value->ToChar(), length);
  return true;
}

u32 BdDocument::SetWord44(u32 value) {
  word_044 = value;
  return value;
}

u32 BdDocument::SetWord54(const u32 *value) {
  word_054 = *value;
  return word_054;
}

bool BdDocument::GetWord4C(u32 *out) {
  *out = word_04c;
  return true;
}

u32 BdDocument::SetWord4C(u32 value) {
  word_04c = value;
  return value;
}

// Publish all four version-like metadata bytes through the original four separate outputs in forward order.
bool BdDocument::GetBytes48(u8 *first, u8 *second, u8 *third, u8 *fourth) {
  *first = bytes_048[0];
  *second = bytes_048[1];
  *third = bytes_048[2];
  *fourth = bytes_048[3];
  return true;
}

u8 BdDocument::SetBytes48(u8 first, u8 second, u8 third, u8 fourth) {
  bytes_048[0] = first;
  bytes_048[1] = second;
  bytes_048[2] = third;
  bytes_048[3] = fourth;
  return fourth;
}

// Ignore null input, otherwise replace the fourth text after the input length and pointer callbacks.
void BdDocument::SetText58(const ScStringView *value) {
  if (value) {
    u32 length = value->Strlen();
    text_058.FromChar(value->ToChar(), length);
  }
}

// Construct the actual zero-reference document registered by the architect root.
ScUnknown *BdCreateClass455969DF() {
  return new BdDocument;
}

BdMaterialSlot::BdMaterialSlot() : uses(0), value(nullptr) {
}

BdMaterialSlot::~BdMaterialSlot() {
  if (value)
    value = nullptr;
}

BdMaterials::BdMaterials() : references(0) {
}

BdMaterials::~BdMaterials() {
}

bool BD_FASTCALL BdMaterials::Get(u32 index, BdAsset **value) {
  *value = nullptr;
  if (!slots[index].value)
    return false;
  *value = slots[index].value;
  return true;
}

BdAsset *BD_FASTCALL BdMaterials::Peek(u32 index) {
  return slots[index].value;
}

u32 BdMaterials::GetUse(u32 index, u32 *uses) {
  *uses = slots[index].uses;
  return *uses;
}

void BdMaterials::Set(u32 index, BdAsset *value) {
  BdMaterialSlot *slot = &slots[index];
  if (slot->value) {
    slot->value = nullptr;
    slot->uses = 0;
  }
  if (value)
    slot->value = value;
}

bool BdMaterials::Find(BdAsset *value, u32 *index) {
  for (u32 i = 0; i < 256; ++i) {
    if (slots[i].value == value) {
      *index = i;
      return true;
    }
  }
  return Insert(value, index);
}

bool BdMaterials::Insert(BdAsset *value, u32 *index) {
  u32 selected = 0;
  if (!FindReplacement(&selected))
    return false;
  Set(selected, value);
  *index = selected;
  return true;
}

bool BdMaterials::FindReplacement(u32 *index) {
  u32 selected = 0, minimum = UINT32_MAX;
  for (u32 i = 1; i < 256; ++i) {
    if (slots[i].uses < minimum) {
      minimum = slots[i].uses;
      selected = i;
    }
  }
  if (selected)
    *index = selected;
  return selected != 0;
}

// Nonzero counts retain the existing value; the original private shim has no observable effects.
void BdMaterials::SetUse(u32 index, u32 uses) {
  if (!uses && slots[index].value)
    slots[index].value = nullptr;
  slots[index].uses = uses;
}

u32 BD_FASTCALL BdMaterials::IncrementUse(u32 index) {
  return ++slots[index].uses;
}

// Remove one recorded use and clear the stored value when that use was the last one.
u32 BD_FASTCALL BdMaterials::DecrementUse(u32 index) {
  u32 remaining = --slots[index].uses;
  if (!remaining && slots[index].value)
    slots[index].value = nullptr;
  return remaining;
}

u32 BdMaterials::AddRef() {
  return ++references;
}

u32 BdMaterials::Release() {
  u32 remaining = --references;
  if (!remaining)
    delete this;
  return remaining;
}

// Start a one-cell object descriptor and clear each projected asset/definition pair in forward order.
BdObjectInfo::BdObjectInfo() {
  size = {1, 1, 1};
  for (u32 i = 0; i < 4; ++i) {
    assets[i] = nullptr;
    definitions[i] = nullptr;
  }
}

// Release each retained definition in projection order.
BdObjectInfo::~BdObjectInfo() {
  for (u32 i = 0; i < 4; ++i)
    if (definitions[i])
      definitions[i]->Release();
}

// Publish a borrowed projected asset for a valid view and output pointer; an empty valid view publishes null.
// Invalid views leave the output untouched, and the original pure asset-retain shim changes no ownership state.
void BdObjectInfo::GetAsset(i32 view, BdAsset **asset) {
  if (asset && view >= 0 && view < 4) {
    *asset = nullptr;
    if (assets[view])
      *asset = assets[view];
  }
}

// Resolve an asset's retained definition and available projected variants, then derive its odd grid footprint and height.
// Keep existing descriptor entries for missing variants and preserve the separate local/descriptor reference ownership.
bool BD_STDCALL BdFillObjectInfo(BdAsset *asset, BdObjectInfo *info) {
  if (!asset)
    return false;
  BdCatalog *catalog = BdGetCatalog();
  BdDefinition *definition = nullptr;
  if (!catalog->GetDefinition(asset->definition_id, &definition))
    return false;
  info->assets[0] = asset;
  if (definition) {
    ++definition->references;
    info->definitions[0] = definition;
  }
  BdAsset *variant = nullptr;
  BdDefinition *variant_definition = nullptr;
  for (u32 i = 1; i < 4; ++i) {
    if (catalog->GetAsset(definition->variant_ids[i - 1], &variant)) {
      if (variant)
        info->assets[i] = variant;
      if (catalog->GetDefinition(variant->definition_id, &variant_definition)) {
        if (variant_definition) {
          ++variant_definition->references;
          info->definitions[i] = variant_definition;
        }
        variant_definition->Release();
        variant_definition = nullptr;
      }
      variant = nullptr;
    }
  }
  i32 widths[4], heights[4];
  for (i32 i = 0; i < 4; ++i) {
    BdAsset *view = nullptr;
    info->GetAsset(i, &view);
    if (view) {
      widths[i] = view->opaque_bounds.right - view->opaque_bounds.left;
      heights[i] = view->opaque_bounds.bottom - view->opaque_bounds.top;
    } else {
      widths[i] = 0;
      heights[i] = 0;
    }
  }
  i32 x = definition->size.x, y = definition->size.y, z = definition->size.z;
  if (!y || !x) {
    i32 first = widths[0] >= widths[2] ? widths[0] : widths[2];
    i32 second = widths[1] >= widths[3] ? widths[1] : widths[3];
    x = (first / 2 + (second + 1) / 2 + 15) / 16;
    y = (second / 2 + (first + 1) / 2 + 15) / 16;
  }
  x |= 1;
  y |= 1;
  if (!z) {
    i32 maximum = 0;
    for (u32 i = 0; i < 4; ++i)
      if (maximum < heights[i])
        maximum = heights[i];
    z = (maximum + 15) / 16;
  }
  info->size = {x, y, z};
  if (definition)
    definition->Release();
  return x != 0 && y != 0 && z != 0;
}

// Resolve the selected catalog asset before filling the caller's actual object descriptor.
// A missing catalog or asset leaves existing descriptor entries untouched through the null-asset fill path.
bool BD_STDCALL BdGetObjectInfo(u32 id, BdObjectInfo *info) {
  BdCatalog *catalog = BdGetCatalog();
  BdAsset *asset = nullptr;
  if (catalog && catalog->GetAsset(id, &asset))
    return BdFillObjectInfo(asset, info);
  BdFillObjectInfo(nullptr, info);
  return false;
}
