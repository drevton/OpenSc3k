#include <cassert>
#include <cstdio>
#include <cstring>

#include "common/locale.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/utf8.h"
#include "fonts/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/sc3_graphics.h"

// Outline font readers identify collection headers and metrics tables by their standard four-character tags.
enum FtSfntTag : u32 {
  kFtTrueTypeCollectionTag = 0x74746366,
  kFtFontHeaderTableTag = 0x68656164,
  kFtVerticalDeviceMetricsTableTag = 0x56444d58,
  kFtOs2MetricsTableTag = 0x4f532f32,
  kFtPostScriptTableTag = 0x706f7374,
};

// Resolve an outline glyph and its owning atlas page, growing the cache only for a real face glyph.
static const FtGlyph *FtOutlineGlyphFor(FtBitmapFont *font, u32 scalar, GxBuffer **sheet);
static bool FtOutlineContains(const FtBitmapFont *font, u32 scalar);
static i32 FtOutlineDrawOffset(const FtBitmapFont *font, u32 scalar);
static void FtOutlineClose(FtBitmapFont *font);

// Construct a parameter font with no glyph or surface allocation. Reference ownership starts at zero.
FtBitmapFont::FtBitmapFont(u32 value, u32 height, u32 options, u32 fourth)
    : type(value), kind(1), size(height), flags(options), glyphs(nullptr), tracking(fourth), line_height(height),
      palette(nullptr), references(0), initialized(false), key_enabled(true), drawing(false), first(0), count(256),
      sheet(nullptr), path(nullptr) {
}

// Construct a file font and own its copied filename. Pixel and glyph loading occurs at Initialize.
FtBitmapFont::FtBitmapFont(const ScStringView *filename, u8 key)
    : type(5), kind(2), size(1), flags(0), glyphs(nullptr), tracking(0), line_height(1), palette(nullptr),
      references(0), initialized(false), key_enabled(key != 0), drawing(false), first(0), count(256), sheet(nullptr),
      path(new ScString(*filename)) {
}

u8 FtBitmapFont::BeginDraw(GxBuffer *target) {
  return target->Lock(GxLockFlags::Blt);
}

u8 FtBitmapFont::EndDraw(GxBuffer *target) {
  return target->Unlock(GxLockFlags::Blt);
}

// Publish and retain the original font/base interface identities. Unsupported queries clear the output.
bool FtBitmapFont::QueryInterface(u32 iid, void **out) {
  if (iid != IID_FtFont && iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<FtFont *>(this);
  AddRef();
  return 1;
}

u32 FtBitmapFont::AddRef() {
  return ++references;
}

// Release one ownership reference and delete the actual object when the old count was one.
u32 FtBitmapFont::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Select the original outline or file initialization path from the constructor kind.
u8 FtBitmapFont::Initialize() {
  if (kind == 1)
    return InitializeOutline(true);
  if (kind == 2)
    return InitializeFile(1);
  return 0;
}

// Close the constructor-specific resources, then independently dispose any remaining owned glyphs, sheet and palette.
// The parameter-font path releases its sheet and glyph array only when its initialization byte is set.
u8 FtBitmapFont::Close() {
  u8 result = 0;
  if (kind == 1) {
    FtOutlineClose(this);
    if (initialized) {
      sheet->Release();
      sheet = nullptr;
      delete[] glyphs;
      glyphs = nullptr;
      initialized = false;
    }
    result = 1;
  } else if (kind == 2)
    result = InitializeFile(0);
  if (glyphs) {
    delete[] glyphs;
    glyphs = nullptr;
  }
  ScRelease(&sheet);
  ScRelease(&palette);
  return result;
}

// Release resources through the actual close method and destroy the owned filename.
FtBitmapFont::~FtBitmapFont() {
  Close();
  if (path)
    delete path;
}

// Compare the stored constructor properties with the other font's live virtual properties, in the original order.
u8 FtBitmapFont::Compare(const FtFont &other) {
  FtFont *font = const_cast<FtFont *>(&other);
  FtType other_type = font->GetFontType();
  if (type != (u32)other_type)
    return false;
  u32 other_size = font->GetFontSize();
  if (size != other_size)
    return false;
  u32 other_flags = font->GetFontFlags();
  if (flags != other_flags)
    return false;
  u32 other_color = font->Color();
  return tracking == other_color;
}

u8 FtBitmapFont::SetPalette(GxPaletteView *value) {
  sheet->ReplacePalette(value ? value : palette);
  return 1;
}

FtType FtBitmapFont::GetFontType() {
  return (FtType)type;
}

u32 FtBitmapFont::GetFontSize() {
  return size;
}

u32 FtBitmapFont::GetFontFlags() {
  return flags;
}

u8 FtBitmapFont::GetFontFlag(FtStyle value) {
  return (flags & (u32)value) != 0;
}

FtSource FtBitmapFont::GetSrcType() {
  return (FtSource)kind;
}

u8 FtBitmapFont::IsCharBreakingChar(u32 code) {
  return code == 32 || code == 45;
}

u8 FtBitmapFont::IsCharReturnChar(u32 code) {
  return code == 10;
}

u8 FtBitmapFont::IsCharWhitespace(u32 code) {
  return code == 32;
}

// Translate a Unicode scalar only at the installed single-byte bitmap-glyph boundary.
static bool FtBitmapGlyphIndex(u32 scalar, u8 *index) {
  if (scalar < 128) {
    *index = static_cast<u8>(scalar);
    return true;
  }
  ScLanguageManager *manager = FtGetLanguageManager();
  u32 page = ScLanguageCrtCodePage(manager ? manager->GetCurrentLanguage() : 1);
  char encoded[4];
  if (ScEncodeLegacyCharacter(page, scalar, encoded) != 1)
    return false;
  *index = static_cast<u8>(encoded[0]);
  return true;
}

static bool FtIsSignedGlyphByte(u32 code) {
  return code >= 0xffffff80u;
}

// Test a Unicode scalar against the selected outline face or the installed bitmap's legacy glyph range.
// The resource conversion rejects characters that have no exact bitmap index.
u8 FtBitmapFont::Contains(u32 code) {
  if (FtIsSignedGlyphByte(code)) {
    if (kind == 1 && outline) {
      GxBuffer *surface;
      return FtOutlineGlyphFor(this, code, &surface) != nullptr;
    }
    u8 character = static_cast<u8>(code);
    return character > first && character < first + count;
  }
  if (kind == 1 && outline)
    return code == 10 || (code > first && FtOutlineContains(this, code));
  u8 character;
  if (!FtBitmapGlyphIndex(code, &character) || character <= first)
    return 0;
  return character < first + count;
}

// Return the selected glyph rectangle width using DWORD subtraction.
i32 FtBitmapFont::Width(u32 code) {
  if (kind == 1 && outline) {
    GxBuffer *surface;
    const FtGlyph *glyph = FtOutlineGlyphFor(this, code, &surface);
    return glyph ? glyph->rectangle.right - glyph->rectangle.left : 0;
  }
  u8 character;
  if (FtIsSignedGlyphByte(code))
    character = static_cast<u8>(code);
  else if (!FtBitmapGlyphIndex(code, &character))
    return 0;
  return (i32)((u32)glyphs[character].rectangle.right - (u32)glyphs[character].rectangle.left);
}

i32 FtBitmapFont::Height(u32) {
  return (i32)line_height;
}

// Return the selected glyph's stored horizontal advance.
i32 FtBitmapFont::Advance(u32 code) {
  if (kind == 1 && outline) {
    GxBuffer *surface;
    const FtGlyph *glyph = FtOutlineGlyphFor(this, code, &surface);
    return glyph ? static_cast<i32>(glyph->advance) : 0;
  }
  u8 character;
  if (FtIsSignedGlyphByte(code))
    character = static_cast<u8>(code);
  else if (!FtBitmapGlyphIndex(code, &character))
    return 0;
  return static_cast<i32>(glyphs[character].advance);
}

static bool FtBitmapContainsText(FtBitmapFont *font, const char *bytes, u32 length) {
  u32 scalar;
  return ScUtf8Decode(bytes, length, &scalar) && font->FtBitmapFont::Contains(scalar);
}

// Measure the valid prefix through virtual glyph advances, including the final valid code.
i32 FtBitmapFont::Measure(const char *bytes, u32 length) {
  u32 position = 0, sum = 0;
  while (position < length) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size || !FtBitmapFont::Contains(scalar))
      break;
    sum += Advance(scalar);
    position += char_size;
  }
  return static_cast<i32>(sum);
}

// Trim trailing spaces and codes outside the live glyph range, then measure the surviving prefix virtually.
// An empty input returns zero without touching the genuine visible-length output.
i32 FtBitmapFont::GetStringVisibleWidth(const char *bytes, u32 length, u32 *visible_length) {
  if (!length)
    return 0;
  *visible_length = ScUtf8Prefix(bytes, length, length);
  while (*visible_length) {
    u32 char_size = ScUtf8PreviousSize(bytes, *visible_length), scalar;
    if (!char_size || ScUtf8Decode(bytes + *visible_length - char_size, char_size, &scalar) != char_size)
      break;
    if (scalar != 32 && FtBitmapFont::Contains(scalar))
      break;
    *visible_length -= char_size;
  }
  return Measure(bytes, *visible_length);
}

// Test increasing prefixes through virtual Measure, dropping the last prefix if it exceeds the width.
u32 FtBitmapFont::Fit(const char *bytes, u32 length, u32 width) {
  u32 position = 0, measured = 0;
  while (position < length && measured < width) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size || !FtBitmapFont::Contains(scalar))
      break;
    u32 next = position + char_size;
    measured = Measure(bytes, next);
    if (measured > width)
      break;
    position = next;
  }
  return position;
}

// Fit a source word/line using glyph bounds and live virtual advances, including LF and forced-word behavior.
u32 FtBitmapFont::WrapFit(const char *bytes, u32 length, u32 width, u8 force) {
  if (!length)
    return 0;
  u32 scalar, char_size = ScUtf8Decode(bytes, length, &scalar);
  if (!char_size || !FtBitmapFont::Contains(scalar))
    return 0;
  u32 position = 0, tested = 0, split = 0, pixels = static_cast<u32>(Width(scalar));
  if (pixels <= width) {
    for (;;) {
      tested = position + char_size;
      if (tested >= length)
        return tested;
      if (scalar == 10) {
        split = tested;
        break;
      }
      if ((scalar == 32 || scalar == 45) && pixels <= width)
        split = tested;
      u32 next, next_size = ScUtf8Decode(bytes + tested, length - tested, &next);
      if (!next_size || !FtBitmapFont::Contains(next))
        return split ? split : tested;
      pixels = pixels - static_cast<u32>(Width(scalar)) + static_cast<u32>(Advance(scalar)) +
          static_cast<u32>(Width(next));
      if (pixels > width)
        break;
      position = tested;
      scalar = next;
      char_size = next_size;
    }
  }
  if (!split) {
    if (!force)
      return 0;
    if (position)
      split = position;
    else {
      while (split < length) {
        u32 code, byte_count = ScUtf8Decode(bytes + split, length - split, &code);
        if (!byte_count || !code || code == 10 || code == 32 || !FtBitmapFont::Contains(code))
          break;
        split += byte_count;
      }
    }
  }
  if (split && ScUtf8PreviousSize(bytes, split) < split && bytes[split - 1] != 32 && split < length &&
      bytes[split] == 32)
    ++split;
  return split;
}

// Count forced wrapped prefixes without adding a synthetic progress policy.
u32 FtBitmapFont::WrapLines(const char *bytes, u32 length, u32 width) {
  u32 lines = 0, consumed = 0;
  while (consumed < length) {
    u32 next = WrapFit(bytes + consumed, length - consumed, width, 1);
    if (!next)
      break;
    consumed += next;
    ++lines;
  }
  return lines;
}

// Find the first width in the source 20..3990 probe sequence that fits the requested height.
u32 FtBitmapFont::WrapWidth(const char *bytes, u32 length, u32 height) {
  u32 rows = height / line_height;
  for (u32 width = 20; width < 4000; width += 10)
    if (WrapLines(bytes, length, width) <= rows)
      return width;
  return 1;
}

// Write the original text extent through the caller's rectangle, retaining live per-character virtual advance calls.
// Authentic cIGZFont CodeView establishes a void operation at virtual88.
void FtBitmapFont::TextArea(const char *bytes, u32 length, ScRect *out) {
  out->left = out->top = out->right = 0;
  u32 bottom = static_cast<u32>(glyphs[0].rectangle.bottom) - static_cast<u32>(glyphs[0].rectangle.top);
  memcpy(&out->bottom, &bottom, sizeof(bottom));
  u32 position = 0, previous = 0, total = 0;
  bool have_previous = false;
  while (position < length) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size)
      break;
    if (have_previous)
      total += static_cast<u32>(Advance(previous));
    previous = scalar;
    have_previous = true;
    position += char_size;
  }
  if (have_previous)
    total += static_cast<u32>(Width(previous));
  memcpy(&out->right, &total, sizeof(total));
}

// Draw a Unicode scalar from the selected outline atlas or an exact installed bitmap glyph index.
// Return the underlying blitter's byte result and report unavailable glyphs as a failed draw.
u8 FtBitmapFont::DrawChar(GxBuffer *target, i32 x, i32 y, u32 code) {
  GxBuffer *source_sheet = sheet;
  const FtGlyph *glyph = nullptr;
  i32 bearing = 0;
  if (kind == 1 && outline) {
    glyph = FtOutlineGlyphFor(this, code, &source_sheet);
    if (!glyph)
      return 0;
    bearing = FtOutlineDrawOffset(this, code);
  }
  if (!glyph) {
    u8 character;
    if (FtIsSignedGlyphByte(code))
      character = static_cast<u8>(code);
    else if (!FtBitmapGlyphIndex(code, &character))
      return 0;
    glyph = glyphs + character;
  }
  const ScRect *source = &glyph->rectangle;
  u32 left = static_cast<u32>(x) + static_cast<u32>(bearing);
  u32 words[4] = {left, (u32)y, left + (u32)source->right - (u32)source->left,
      (u32)y + (u32)source->bottom - (u32)source->top};
  ScRect destination;
  memcpy(&destination, words, sizeof(destination));
  return target->Blt(source_sheet, *source, destination, nullptr);
}

// Draw complete UTF-8 characters until the length or NUL, skipping LF and preserving nested target-lock ownership.
u8 FtBitmapFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) {
  bool acquired = false, success = true;
  if (!drawing) {
    if (!target->Lock(GxLockFlags::Blt))
      return 0;
    drawing = true;
    acquired = true;
  }
  for (u32 position = 0; position < length;) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size) {
      success = false;
      break;
    }
    if (!scalar)
      break;
    if (scalar != 10) {
      if (!FtBitmapFont::DrawChar(target, x, y, scalar))
        success = false;
      x = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(Advance(scalar)));
    }
    position += char_size;
  }
  if (acquired) {
    target->Unlock(GxLockFlags::Blt);
    drawing = false;
  }
  return success;
}

// Draw only glyphs whose advance and line height fit wholly inside the supplied signed bounds.
// Advance precedes the live bounds/text reads; LF is skipped and nested target-lock ownership is preserved.
u8 FtBitmapFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) {
  bool acquired = false, success = true;
  if (!drawing) {
    if (!target->Lock(GxLockFlags::Blt))
      return false;
    drawing = true;
    acquired = true;
  }
  for (u32 position = 0; position < length;) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size) {
      success = false;
      break;
    }
    if (!scalar)
      break;
    if (scalar != 10) {
      i32 next_x = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(Advance(scalar)));
      if (next_x <= clip->right && static_cast<i32>(y + line_height) <= clip->bottom && x >= clip->left &&
          y >= clip->top && !FtBitmapFont::DrawChar(target, x, y, scalar))
        success = false;
      x = next_x;
    }
    position += char_size;
  }
  if (acquired) {
    target->Unlock(GxLockFlags::Blt);
    drawing = false;
  }
  return success;
}

// Wrap successive text prefixes into the rectangle through the original virtual fit and ordinary draw methods.
// Capture width, left and line height once, reload the bottom each row and release only a lock acquired here.
u8 FtBitmapFont::DrawTextPara(GxBuffer *target, const ScRect &area, const char *bytes, u32 length) {
  const ScRect *box = &area;
  u32 consumed = 0, x = (u32)box->left, y = (u32)box->top;
  u32 width = (u32)box->right - x, height = line_height;
  bool acquired = false;
  if (length && FtBitmapContainsText(this, bytes, length)) {
    if (!drawing) {
      if (!target->Lock(GxLockFlags::Blt))
        return false;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box->bottom) {
      if (consumed >= length || !FtBitmapContainsText(this, bytes + consumed, length - consumed))
        break;
      u32 line = WrapFit(bytes + consumed, length - consumed, width, 1);
      DrawTextA(target, x, y, bytes + consumed, line);
      consumed += line;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Blt);
      drawing = false;
    }
  }
  return true;
}

// Use the rectangle's captured wrapping geometry while forwarding the explicit clip to each ordinary draw call.
// Nested drawing shares the existing lock; the original ignores individual row drawing results.
u8 FtBitmapFont::DrawTextPara(GxBuffer *target, const ScRect &area, const char *bytes, u32 length, const ScRect *clip) {
  const ScRect *box = &area;
  u32 consumed = 0, x = (u32)box->left, y = (u32)box->top;
  u32 width = (u32)box->right - x, height = line_height;
  bool acquired = false;
  if (length && FtBitmapContainsText(this, bytes, length)) {
    if (!drawing) {
      if (!target->Lock(GxLockFlags::Blt))
        return false;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box->bottom) {
      if (consumed >= length || !FtBitmapContainsText(this, bytes + consumed, length - consumed))
        break;
      u32 line = WrapFit(bytes + consumed, length - consumed, width, 1);
      DrawTextA(target, x, y, bytes + consumed, line, clip);
      consumed += line;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Blt);
      drawing = false;
    }
  }
  return true;
}

static u32 FtSignedHalf(u32 value) {
  return value & kScIntegerSignBit ? 0u - ((0u - value) / 2) : value / 2;
}

// Draw successive wrapped lines against a live rectangle, retaining the captured width and line height.
u8 FtBitmapFont::DrawTextPara(
    GxBuffer *target, const ScRect &area, const char *bytes, u32 length, FtOutputStyle alignment) {
  const ScRect *box = &area;
  const ScRect *rectangle = box;
  u32 y = rectangle->top, x = rectangle->left, height = line_height, width = rectangle->right - x, consumed = 0;
  bool acquired = false;
  if (length && FtBitmapContainsText(this, bytes, length)) {
    if (!drawing) {
      if (!target->Lock(GxLockFlags::Blt))
        return 0;
      drawing = true;
      acquired = true;
    }
    u32 bottom = rectangle->bottom;
    while (y < bottom) {
      if (consumed >= length || !FtBitmapContainsText(this, bytes + consumed, length - consumed))
        break;
      u32 line = WrapFit(bytes + consumed, length - consumed, width, 1);
      if (alignment == FtOutputStyle::Left)
        x = rectangle->left;
      else if (alignment == FtOutputStyle::Center || alignment == FtOutputStyle::Right) {
        u32 measured = Measure(bytes + consumed, line);
        if (alignment == FtOutputStyle::Center)
          x = rectangle->left + (width >> 1) - FtSignedHalf(measured);
        else
          x = rectangle->right - measured;
      }
      DrawTextA(target, x, y, bytes + consumed, line);
      consumed += line;
      y += height;
      bottom = rectangle->bottom;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Blt);
      drawing = false;
    }
  }
  return 1;
}

// Read palette endpoint one only for a type-five font; an absent palette yields zero.
u32 FtBitmapFont::Color() {
  if (!palette || type != 5)
    return 0;
  u32 pixel;
  palette->GetColor(1, &pixel);
  return pixel;
}

// Read palette endpoint sixteen only for a type-five font; an absent palette yields zero.
u32 FtBitmapFont::EndColor() {
  if (!palette || type != 5)
    return 0;
  u32 pixel;
  palette->GetColor(16, &pixel);
  return pixel;
}

// Convert and publish the source sixteen-step gradient, reloading the actual font palette after each graphics call.
static void FtGradient(FtBitmapFont *font, GxGraphics *graphics, u32 first, u32 second) {
  u8 from[3], to[3];
  graphics->Convert16BitNativeValueToRGB((u16)first, &from[0], &from[1], &from[2]);
  graphics->Convert16BitNativeValueToRGB((u16)second, &to[0], &to[1], &to[2]);
  for (u32 index = 0; index < 16; ++index) {
    u8 rgb[3];
    for (u32 channel = 0; channel < 3; ++channel)
      rgb[channel] = index == 15 ? to[channel] : (u8)(from[channel] + (i32)index * (to[channel] - from[channel]) / 16);
    u16 pixel;
    graphics->ConvertRGBValueToNative16Bit(&pixel, rgb[0], rgb[1], rgb[2]);
    font->palette->SetColor((i32)index + 1, pixel);
  }
}

// Set endpoint one and interpolate toward captured endpoint sixteen. A missing palette succeeds without graphics work.
u8 FtBitmapFont::SetColor(u32 color) {
  if (!palette)
    return 1;
  u32 endpoint;
  palette->GetColor(16, &endpoint);
  GxGraphics *graphics = FtGetGraphics();
  assert(graphics);
  FtGradient(this, graphics, color, endpoint);
  return 1;
}

// Interpolate the supplied endpoints through the actual graphics object and current owned palette.
u8 FtBitmapFont::SetColors(u32 first_color, u32 second_color) {
  if (!palette)
    return 1;
  GxGraphics *graphics = FtGetGraphics();
  assert(graphics);
  FtGradient(this, graphics, first_color, second_color);
  return 1;
}

// Capture, replace and restore both palette endpoints around the virtual text draw.
u8 FtBitmapFont::DrawTextA(
    GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 first_color, u32 second_color) {
  u32 old_first = Color(), old_second = EndColor();
  if (!SetColors(first_color, second_color))
    return 0;
  u8 result = DrawTextA(target, x, y, bytes, length);
  SetColors(old_first, old_second);
  return result;
}

// Capture, replace and restore endpoint one around the virtual text draw.
u8 FtBitmapFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 color) {
  u32 old = Color();
  if (!SetColor(color))
    return 0;
  u8 result = DrawTextA(target, x, y, bytes, length);
  SetColor(old);
  return result;
}

// Preserve the current first palette endpoint around a wrapped draw with one temporary color.
// A failed color change returns immediately; restoration uses the live virtual setter after drawing.
u8 FtBitmapFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 color, FtOutputStyle alignment) {
  u32 old = Color();
  if (!SetColor(color))
    return 0;
  u8 result = DrawTextPara(target, box, bytes, length, alignment);
  SetColor(old);
  return result;
}

// Preserve both endpoint colors around a wrapped draw and return the original draw's low byte.
u8 FtBitmapFont::DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 first_color,
    u32 second_color, FtOutputStyle alignment) {
  u32 old_first = Color(), old_second = EndColor();
  if (!SetColors(first_color, second_color))
    return 0;
  u8 result = DrawTextPara(target, box, bytes, length, alignment);
  SetColors(old_first, old_second);
  return result;
}

// Copy the owned filename to the genuine output string, or assign empty text for a parameter font.
u8 FtBitmapFont::GetFontFilePath(ScStringView *out) {
  static const char empty = 0;
  const char *bytes = path ? path->ToChar() : &empty;
  u32 length = path ? path->Strlen() : 0;
  out->FromChar(bytes, length);
  return path != nullptr;
}

// Draw command-text ampersands, underlining the following ordinary character and collapsing a doubled ampersand.
// Return the bitwise union of the actual glyph blit bytes without acquiring an extra surface lock.
u8 FtBitmapFont::DrawCommandText(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) {
  u8 result = 0;
  bool underline = false;
  for (u32 position = 0; position < length;) {
    u32 scalar, char_size = ScUtf8Decode(bytes + position, length - position, &scalar);
    if (!char_size || !scalar)
      break;
    position += char_size;
    if (scalar == '&') {
      if (position < length && bytes[position] == '&') {
        result |= FtBitmapFont::DrawChar(target, x, y, '&');
        x = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(Advance('&')));
        ++position;
      } else
        underline = true;
    } else {
      if (underline)
        result |= FtBitmapFont::DrawChar(target, x, y, '_');
      result |= FtBitmapFont::DrawChar(target, x, y, scalar);
      x = static_cast<i32>(static_cast<u32>(x) + static_cast<u32>(Advance(scalar)));
      underline = false;
    }
  }
  return result;
}

// Wrap a paragraph with a first-line indent, optionally clipping each virtual line draw. Preserve the source's
// cached width/line height, live bottom edge and ownership of a blit lock acquired only by this operation.
u8 FtBitmapFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, u32 indent, const char *bytes, u32 length, const ScRect *clip) {
  u32 consumed = 0, y = (u32)box.top, width = (u32)box.right - (u32)box.left, height = line_height;
  i32 left = box.left;
  bool acquired = false;
  if (length && FtBitmapContainsText(this, bytes, length - consumed)) {
    if (!drawing) {
      if (!target->Lock(GxLockFlags::Blt))
        return 0;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box.bottom && consumed < length && FtBitmapContainsText(this, bytes, length - consumed)) {
      u32 offset = consumed ? 0 : indent;
      u32 line_length = WrapFit(bytes, length - consumed, width - offset, 1);
      i32 x = (i32)((u32)left + offset);
      if (clip)
        DrawTextA(target, x, (i32)y, bytes, line_length, clip);
      else
        DrawTextA(target, x, (i32)y, bytes, line_length);
      consumed += line_length;
      bytes += line_length;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Blt);
      drawing = false;
    }
  }
  return 1;
}

// Continue a paragraph from explicit mutable X/Y outputs and report its occupied rectangle. Keep the original
// retry at the left edge when the next word does not fit, and retain a last line's horizontal cursor.
u8 FtBitmapFont::DrawTextPara(
    GxBuffer *target, u32 *x, u32 *y, const ScRect &box, ScRect *out, const char *bytes, u32 length) {
  u32 height = line_height, consumed = 0, width = (u32)box.right - (u32)box.left;
  *out = box;
  if (!length || !FtBitmapContainsText(this, bytes, length - consumed))
    return 1;
  bool acquired = false;
  if (!drawing) {
    if (!target->Lock(GxLockFlags::Blt))
      return 0;
    drawing = true;
    acquired = true;
  }
  u32 lines = 0;
  while (*y < (u32)box.bottom && consumed < length && FtBitmapContainsText(this, bytes, length - consumed)) {
    i32 available = (i32)(width + (u32)box.left - *x);
    u32 line_length = WrapFit(bytes, length - consumed, (u32)available, 1);
    if (Measure(bytes, line_length) <= available || *x <= (u32)box.left) {
      const ScRect *clip = target->GetBufferArea();
      DrawTextA(target, (i32)*x, (i32)*y, bytes, line_length, clip);
      i32 advance = Measure(bytes, line_length);
      ++lines;
      if (consumed + line_length >= length) {
        *x += (u32)advance;
        consumed += line_length;
        bytes += line_length;
        continue;
      }
      *x = (u32)box.left;
    } else {
      line_length = 0;
      ++lines;
      *x = (u32)box.left;
    }
    *y += height;
    consumed += line_length;
    bytes += line_length;
  }
  out->right = lines > 1 ? box.right : (i32)*x;
  out->bottom = (i32)(height + *y);
  if (acquired) {
    target->Unlock(GxLockFlags::Blt);
    drawing = false;
  }
  return 1;
}

// Release allocations reached by an unsuccessful file load, keeping actual ownership fields synchronized.
static void FtLoadCleanup(FtBitmapFont *font) {
  if (font->sheet) {
    font->sheet->Release();
    font->sheet = nullptr;
  }
  if (font->palette) {
    font->palette->Release();
    font->palette = nullptr;
  }
}

static bool FtReadFile(FILE *file, void *out, usize bytes) {
  return fread(out, 1, bytes, file) == bytes;
}

// Load the real FBF header, pixels, palette and glyph records through the actual graphics/buffer interfaces.
u8 FtBitmapFont::LoadFbf(const ScStringView *filename) {
  FtBitmapFont *font = this;
  GxGraphics *graphics = FtGetGraphics();
  if (!graphics)
    return 0;
  FILE *file = nullptr;
  file = ScOpenNativeFile(filename->ToChar(), "rb");
  if (!file)
    return 0;
  u32 header[4];
  if (!FtReadFile(file, header, sizeof(header))) {
    fclose(file);
    return 0;
  }
  u32 width = header[1], height = header[2], storage = header[3];
  u32 row_bytes = width * (storage == 1 ? 1u : 2u);
  graphics->CreateBuffer(&font->sheet);
  font->sheet->Init(width, height, GxColorType(static_cast<GxColorType::Color>(storage == 1 ? 3 : 4)), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (!font->sheet->Lock(GxLockFlags::Bits)) {
    FtLoadCleanup(font);
    fclose(file);
    return 0;
  }
  u32 pitch = font->sheet->GetColorSurfaceStride();
  u8 *pixels = static_cast<u8 *>(font->sheet->GetColorSurfaceBits());
  if (pitch != row_bytes) {
    i32 delta = static_cast<i32>(pitch - row_bytes);
    uptr cursor = reinterpret_cast<uptr>(pixels);
    for (i32 row = 0; row < static_cast<i32>(height); ++row) {
      FtReadFile(file, reinterpret_cast<void *>(cursor), row_bytes);
      cursor += static_cast<iptr>(delta);
    }
  } else
    FtReadFile(file, pixels, row_bytes * height);
  font->sheet->Unlock(GxLockFlags::Bits);
  if (storage == 1) {
    font->palette = new GxPalette;
    font->palette->AddRef();
    font->palette->Init();
    font->palette->MakeGenericPalette(256, 16);
    u16 *entries = (u16 *)font->palette->GetPaletteArray();
    if (!FtReadFile(file, entries, 512)) {
      FtLoadCleanup(font);
      fclose(file);
      return 0;
    }
    for (u32 index = 0; index < 256; ++index)
      graphics->Convert16BitValueFromFormatToFormat(
          entries + index, GxColorType::Color::RGB565, GxColorType::Color::Native);
    font->sheet->ReplacePalette(font->palette);
  } else
    font->sheet->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>(7), static_cast<GxColorType::Color>(1));
  font->glyphs = new FtGlyph[256];
  if (!FtReadFile(file, font->glyphs, 5120)) {
    FtLoadCleanup(font);
    delete[] font->glyphs;
    font->glyphs = nullptr;
    fclose(file);
    return 0;
  }
  fclose(file);
  return 1;
}

// Initialize or close a file font, including the first-pixel color key and glyph-zero line height.
u8 FtBitmapFont::InitializeFile(u8 enabled) {
  FtBitmapFont *font = this;
  if (enabled) {
    if (!LoadFbf(path))
      return 0;
    if (font->key_enabled && font->sheet->Lock(GxLockFlags::Bits)) {
      u32 key = font->sheet->GetPixel(0, 0);
      font->sheet->Unlock(GxLockFlags::Bits);
      font->sheet->SetTransparency(key);
    }
    font->initialized = true;
    font->line_height = (u32)font->glyphs[0].rectangle.bottom - (u32)font->glyphs[0].rectangle.top;
  } else {
    font->initialized = false;
    if (font->sheet) {
      font->sheet->Release();
      font->sheet = nullptr;
    }
    if (font->glyphs) {
      delete[] font->glyphs;
      font->glyphs = nullptr;
    }
    if (font->palette) {
      font->palette->Release();
      font->palette = nullptr;
    }
  }
  return 1;
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#endif
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "common/api.h"
#include "common/locale.h"

// SDL outline faces and dynamically allocated atlas rows stay owned by their font instance.
struct FtOutlinePage {
  GxBuffer *surface;
  u32 width, used;
  FtOutlinePage *next;
};

struct FtOutlineGlyph {
  u32 scalar;
  FtGlyph glyph;
  FtOutlinePage *page;
  FtOutlineGlyph *next;
};

struct FtOutlineState {
  TTF_Font *font;
  FtOutlinePage *pages;
  FtOutlineGlyph *glyphs;
  i32 font_height, baseline_offset, strike_row, strike_rows, underline_row, underline_rows;
  bool shadow, symbol, strike, underline;
};

// A non-Windows host uses the selected game's language page; the original machine's ANSI page is unavailable there.
static bool FtOutlineGraphScalar(const FtBitmapFont *font, u32 code, u32 *scalar) {
  if (!FtIsSignedGlyphByte(code)) {
    *scalar = code;
    return true;
  }
  u8 byte = static_cast<u8>(code);
  if (font->outline->symbol) {
    *scalar = byte;
    return true;
  }
#ifdef _WIN32
  u32 page = GetACP();
#else
  ScLanguageManager *manager = FtGetLanguageManager();
  u32 page = ScLanguageCrtCodePage(manager ? manager->GetCurrentLanguage() : 1);
#endif
  return ScDecodeLegacyCharacter(page, reinterpret_cast<const char *>(&byte), 1, scalar) == 1;
}

// Report whether the selected face contains this scalar without substituting a different face.
static bool FtOutlineContains(const FtBitmapFont *font, u32 scalar) {
  if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
    return false;
  if (font->outline->symbol && scalar < 256)
    scalar += 0xf000;
  return TTF_FontHasGlyph(font->outline->font, scalar);
}

// Position a zero-advance mark by its font bearing relative to the current text origin.
static i32 FtOutlineDrawOffset(const FtBitmapFont *font, u32 scalar) {
  if (!FtOutlineGraphScalar(font, scalar, &scalar))
    return 0;
  if (font->outline->symbol && scalar < 256)
    scalar += 0xf000;
  int left = 0, advance = 0;
  if (!TTF_GetGlyphMetrics(font->outline->font, scalar, &left, nullptr, nullptr, nullptr, &advance))
    return 0;
  return advance ? 0 : left;
}

// Release every lazily allocated atlas row and glyph record before closing the retained SDL_ttf face.
static void FtOutlineClose(FtBitmapFont *font) {
  FtOutlineState *state = font->outline;
  if (!state)
    return;
  while (state->glyphs) {
    FtOutlineGlyph *next = state->glyphs->next;
    delete state->glyphs;
    state->glyphs = next;
  }
  while (state->pages) {
    FtOutlinePage *next = state->pages->next;
    state->pages->surface->Release();
    delete state->pages;
    state->pages = next;
  }
  TTF_CloseFont(state->font);
  TTF_Quit();
  delete state;
  font->outline = nullptr;
}

// Locate the glyph's original atlas slot or append a real SDL_ttf glyph to a cached atlas row.
// Glyphs absent from the requested face remain absent; no other face or replacement glyph is selected.
static const FtGlyph *FtOutlineGlyphFor(FtBitmapFont *font, u32 scalar, GxBuffer **sheet) {
  FtOutlineState *state = font->outline;
  if (!FtOutlineGraphScalar(font, scalar, &scalar) || !FtOutlineContains(font, scalar))
    return nullptr;
  if (scalar < 256) {
    *sheet = font->sheet;
    return font->glyphs + scalar;
  }
  for (FtOutlineGlyph *entry = state->glyphs; entry; entry = entry->next)
    if (entry->scalar == scalar) {
      *sheet = entry->page->surface;
      return &entry->glyph;
    }
  int advance = 0, left = 0, right = 0, top = 0;
  if (!TTF_GetGlyphMetrics(state->font, scalar, &left, &right, nullptr, &top, &advance) || advance < 0)
    return nullptr;
  u32 ink_width = !advance && right > left ? static_cast<u32>(right - left) : static_cast<u32>(advance);
  u32 width = ink_width + state->shadow;
  u32 height = static_cast<u32>(state->font_height) + 1;
  FtOutlinePage *page = state->pages;
  while (page && (width > page->width || page->used > page->width - width))
    page = page->next;
  if (!page) {
    u32 page_width = width > 512 ? width : 512;
    GxBuffer *surface = nullptr;
    FtGetGraphics()->CreateBuffer(&surface);
    if (!surface || !surface->Init(page_width, height, GxColorType(GxColorType::Color::RGB16), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      if (surface)
        surface->Release();
      return nullptr;
    }
    u8 red = font->tracking == surface->ConvertRGBValueToNative16Bit(255, 0, 255) ? 1 : 255;
    u16 background = surface->ConvertRGBValueToNative16Bit(red, 0, 255);
    if (!surface->Lock(GxLockFlags::Fill)) {
      surface->Release();
      return nullptr;
    }
    surface->Fill(background, nullptr);
    surface->Unlock(GxLockFlags::Fill);
    surface->SetTransparency(background);
    page = new FtOutlinePage{surface, page_width, 0, state->pages};
    state->pages = page;
  }
  SDL_Color foreground = {255, 255, 255, 255};
  SDL_Surface *bitmap = TTF_RenderGlyph_Solid(state->font, scalar, foreground);
  if (!bitmap && width > static_cast<u32>(state->shadow))
    return nullptr;
  if (bitmap && SDL_MUSTLOCK(bitmap) && !SDL_LockSurface(bitmap)) {
    SDL_DestroySurface(bitmap);
    return nullptr;
  }
  if (!page->surface->Lock(GxLockFlags::GetSetPixel)) {
    if (bitmap) {
      if (SDL_MUSTLOCK(bitmap))
        SDL_UnlockSurface(bitmap);
      SDL_DestroySurface(bitmap);
    }
    return nullptr;
  }
  i32 x_offset = advance && left < 0 ? left : 0;
  i32 extra_top = top > TTF_GetFontAscent(state->font) ? top - TTF_GetFontAscent(state->font) : 0;
  for (i32 row = 0; row < state->font_height; ++row) {
    i32 y = row - state->baseline_offset + extra_top;
    const u8 *pixels = bitmap && y >= 0 && y < bitmap->h
        ? static_cast<const u8 *>(bitmap->pixels) + static_cast<usize>(y) * bitmap->pitch : nullptr;
    for (u32 column = 0; column < width - static_cast<u32>(state->shadow); ++column) {
      i32 x = static_cast<i32>(column) - x_offset;
      bool ink = pixels && x >= 0 && x < bitmap->w && pixels[x];
      if (ink || (state->strike && row >= state->strike_row && row - state->strike_row < state->strike_rows) ||
          (state->underline && row >= state->underline_row && row - state->underline_row < state->underline_rows)) {
        page->surface->SetPixel(page->used + column, static_cast<u32>(row), font->tracking);
        if (state->shadow)
          page->surface->SetPixel(page->used + column + 1, static_cast<u32>(row) + 1, 0);
      }
    }
  }
  page->surface->Unlock(GxLockFlags::GetSetPixel);
  if (bitmap) {
    if (SDL_MUSTLOCK(bitmap))
      SDL_UnlockSurface(bitmap);
    SDL_DestroySurface(bitmap);
  }
  FtOutlineGlyph *entry = new FtOutlineGlyph{scalar,
      {{static_cast<i32>(page->used), 0, static_cast<i32>(page->used + width), static_cast<i32>(height)},
          advance && (font->type == 2 || font->type == 3) ? width : static_cast<u32>(advance)}, page, state->glyphs};
  state->glyphs = entry;
  page->used += width;
  *sheet = page->surface;
  return &entry->glyph;
}

struct FtFontSearch {
  const char *family;
  f32 size;
  TTF_FontStyleFlags style;
  TTF_Font *font;
  int rank, depth;
  ScString path;
  int face;
};

// Open an individual face in a font collection without substituting a different family.
static TTF_Font *FtOpenFace(const char *path, f32 size, int face) {
  SDL_PropertiesID properties = SDL_CreateProperties();
  if (!properties)
    return nullptr;
  bool set = SDL_SetStringProperty(properties, TTF_PROP_FONT_CREATE_FILENAME_STRING, path) &&
      SDL_SetFloatProperty(properties, TTF_PROP_FONT_CREATE_SIZE_FLOAT, size) &&
      SDL_SetNumberProperty(properties, TTF_PROP_FONT_CREATE_FACE_NUMBER, face);
  TTF_Font *font = set ? TTF_OpenFontWithProperties(properties) : nullptr;
  SDL_DestroyProperties(properties);
  return font;
}

// Inspect actual font files and collections, preferring the closest intrinsic bold and italic face.
static SDL_EnumerationResult SDLCALL FtFindFontEntry(void *data, const char *directory, const char *name) {
  FtFontSearch *search = static_cast<FtFontSearch *>(data);
  if (name[0] == '.')
    return SDL_ENUM_CONTINUE;
  ScString path;
  path.Sprintf("%s%s", directory, name);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path.ToChar(), &info))
    return SDL_ENUM_CONTINUE;
  if (info.type == SDL_PATHTYPE_DIRECTORY) {
    if (search->depth < 8) {
      ++search->depth;
      SDL_EnumerateDirectory(path.ToChar(), FtFindFontEntry, search);
      --search->depth;
    }
    return search->rank == 0 ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
  }
  const char *extension = SDL_strrchr(name, '.');
  if (info.type != SDL_PATHTYPE_FILE || !extension ||
      (SDL_strcasecmp(extension, ".ttf") && SDL_strcasecmp(extension, ".otf") &&
       SDL_strcasecmp(extension, ".ttc") && SDL_strcasecmp(extension, ".otc") &&
       SDL_strcasecmp(extension, ".dfont")))
    return SDL_ENUM_CONTINUE;
  TTF_Font *first = FtOpenFace(path.ToChar(), search->size, 0);
  if (!first)
    return SDL_ENUM_CONTINUE;
  int count = TTF_GetNumFontFaces(first);
  if (count <= 0) {
    TTF_CloseFont(first);
    return SDL_ENUM_CONTINUE;
  }
  for (int index = 0; index < count; ++index) {
    TTF_Font *font = index ? FtOpenFace(path.ToChar(), search->size, index) : first;
    if (!font)
      continue;
    const char *family = TTF_GetFontFamilyName(font);
    TTF_FontStyleFlags actual = TTF_GetFontStyle(font) & (TTF_STYLE_BOLD | TTF_STYLE_ITALIC);
    TTF_FontStyleFlags wanted = search->style & (TTF_STYLE_BOLD | TTF_STYLE_ITALIC);
    if (family && !SDL_strcasecmp(family, search->family) && !(actual & ~wanted)) {
      int rank = ((wanted & TTF_STYLE_BOLD) && !(actual & TTF_STYLE_BOLD)) +
          ((wanted & TTF_STYLE_ITALIC) && !(actual & TTF_STYLE_ITALIC));
      if (rank < search->rank) {
        if (search->font)
          TTF_CloseFont(search->font);
        search->font = font;
        search->rank = rank;
        search->path = path;
        search->face = index;
        font = nullptr;
      }
    }
    if (font)
      TTF_CloseFont(font);
    if (search->rank == 0)
      break;
  }
  return search->rank == 0 ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}

// Search installed resource and platform font directories for the configured family, with no fallback face.
static TTF_Font *FtFindFont(const char *family, f32 size, TTF_FontStyleFlags style, ScString *font_path, int *face) {
  FtFontSearch search = {family, size, style, nullptr, 3, 0, ScString(), 0};
  const char *game = SDL_getenv("SC3_GAME_DIR");
#ifdef _WIN32
  const char *windows = SDL_getenv("SystemRoot"), *local = SDL_getenv("LOCALAPPDATA");
  if (!windows)
    windows = SDL_getenv("windir");
  ScString windows_fonts, local_fonts;
  if (windows)
    windows_fonts.Sprintf("%s/Fonts", windows);
  if (local)
    local_fonts.Sprintf("%s/Microsoft/Windows/Fonts", local);
#endif
  const char *paths[] = {game, SDL_GetBasePath(),
#ifdef _WIN32
      windows_fonts.ToChar(), local_fonts.ToChar()
#elif defined(__APPLE__)
      "/System/Library/Fonts", "/Library/Fonts"
#else
      "/usr/share/fonts", "/usr/local/share/fonts"
#endif
  };
  for (u32 index = 0; index < sizeof(paths) / sizeof(paths[0]) && search.rank != 0; ++index) {
    if (!paths[index] || !*paths[index])
      continue;
    SDL_EnumerateDirectory(paths[index], FtFindFontEntry, &search);
  }
#ifndef _WIN32
  const char *home = SDL_getenv("HOME");
  if (home && search.rank != 0) {
    ScString path;
#ifdef __APPLE__
    path.Sprintf("%s/Library/Fonts", home);
#else
    path.Sprintf("%s/.local/share/fonts", home);
#endif
    SDL_EnumerateDirectory(path.ToChar(), FtFindFontEntry, &search);
  }
#endif
  if (search.font) {
    *font_path = search.path;
    *face = search.face;
    TTF_SetFontStyle(search.font, style);
    TTF_SetFontHinting(search.font, TTF_HINTING_MONO);
  }
  return search.font;
}

// Read big-endian font table integers only when the complete field is present in the loaded file.
static bool FtTable16(const u8 *data, usize length, usize offset, u16 *out) {
  if (offset > length || length - offset < 2)
    return false;
  *out = static_cast<u16>((data[offset] << 8) | data[offset + 1]);
  return true;
}

// Read a big-endian table offset without consuming bytes beyond the font file.
static bool FtTable32(const u8 *data, usize length, usize offset, u32 *out) {
  u16 high, low;
  if (!FtTable16(data, length, offset, &high) || !FtTable16(data, length, offset + 2, &low))
    return false;
  *out = (static_cast<u32>(high) << 16) | low;
  return true;
}

// Locate one SFNT table in either a standalone font or a selected collection face.
static bool FtSfntTable(const u8 *data, usize length, int face, u32 wanted, u32 *out) {
  u32 signature = 0, directory = 0, count32 = 0;
  u16 count = 0;
  if (!FtTable32(data, length, 0, &signature))
    return false;
  if (signature == kFtTrueTypeCollectionTag &&
      (face < 0 || !FtTable32(data, length, 8, &count32) || static_cast<u32>(face) >= count32 ||
          !FtTable32(data, length, 12 + static_cast<usize>(face) * 4, &directory)))
    return false;
  if (!FtTable16(data, length, static_cast<usize>(directory) + 4, &count))
    return false;
  for (u32 index = 0; index < count; ++index) {
    usize entry = static_cast<usize>(directory) + 12 + static_cast<usize>(index) * 16;
    u32 tag, offset;
    if (!FtTable32(data, length, entry, &tag) || !FtTable32(data, length, entry + 8, &offset))
      return false;
    if (tag == wanted) {
      *out = offset;
      return true;
    }
  }
  return false;
}

// Read the selected face's design-space decoration position and thickness without substituting another font.
static bool FtFontDecorationMetrics(const ScString &path, int face, u32 tag, u32 position_offset, u32 thickness_offset,
    u16 *units, i16 *position, i16 *thickness) {
  usize length = 0;
  u8 *data = static_cast<u8 *>(SDL_LoadFile(path.ToChar(), &length));
  if (!data)
    return false;
  u32 head, table;
  u16 raw_position = 0, raw_thickness = 0;
  bool found = FtSfntTable(data, length, face, kFtFontHeaderTableTag, &head) &&
      FtSfntTable(data, length, face, tag, &table) && FtTable16(data, length, static_cast<usize>(head) + 18, units) &&
      *units && FtTable16(data, length, static_cast<usize>(table) + thickness_offset, &raw_thickness) &&
      FtTable16(data, length, static_cast<usize>(table) + position_offset, &raw_position);
  if (found) {
    *position = static_cast<i16>(raw_position);
    *thickness = static_cast<i16>(raw_thickness);
  }
  SDL_free(data);
  return found;
}

// Select square-pixel cell metrics from VDMX.
static bool FtFontCellMetrics(
    const ScString &path, int face, TTF_Font *font, u32 requested, u32 *em, i32 *top, i32 *bottom) {
  usize length = 0;
  u8 *data = static_cast<u8 *>(SDL_LoadFile(path.ToChar(), &length));
  if (!data)
    return false;
  u32 table = 0, os2 = 0;
  u16 ratios = 0;
  bool valid = FtSfntTable(data, length, face, kFtVerticalDeviceMetricsTableTag, &table) && table &&
      FtTable16(data, length, static_cast<usize>(table) + 4, &ratios);
  FtSfntTable(data, length, face, kFtOs2MetricsTableTag, &os2);
  u16 group = 0;
  for (u32 index = 0; valid && index < ratios; ++index) {
    usize entry = static_cast<usize>(table) + 6 + static_cast<usize>(index) * 4;
    if (entry > length || length - entry < 4) {
      valid = false;
      break;
    }
    u8 charset = data[entry], x = data[entry + 1], low = data[entry + 2], high = data[entry + 3];
    if (charset && ((!x && !low && !high) || (x == 1 && low <= 1 && high >= 1))) {
      valid = FtTable16(data, length, static_cast<usize>(table) + 6 + ratios * 4 + index * 2, &group);
      break;
    }
  }
  usize group_offset = static_cast<usize>(table) + group;
  u16 records = 0;
  valid = valid && group && FtTable16(data, length, group_offset, &records);
  bool found = false;
  u16 first_pixels = 0;
  for (u32 index = 0; valid && index < records; ++index) {
    usize entry = group_offset + 4 + static_cast<usize>(index) * 6;
    u16 pixels, max, min;
    if (!FtTable16(data, length, entry, &pixels) || !FtTable16(data, length, entry + 2, &max) ||
        !FtTable16(data, length, entry + 4, &min)) {
      found = false;
      break;
    }
    i32 ascent = static_cast<i16>(max), descent = static_cast<i16>(min);
    if (!index)
      first_pixels = pixels;
    if (ascent - descent > static_cast<i32>(requested))
      break;
    *em = pixels;
    *top = ascent;
    *bottom = descent;
    found = true;
    if (ascent - descent == static_cast<i32>(requested))
      break;
  }
  if (!found && valid && first_pixels > 1 && requested && os2) {
    u16 ascent = 0, descent = 0;
    if (FtTable16(data, length, static_cast<usize>(os2) + 74, &ascent) &&
        FtTable16(data, length, static_cast<usize>(os2) + 76, &descent) && ascent + descent) {
      f32 previous_size = TTF_GetFontSize(font);
      for (u32 pixels = 1; pixels < first_pixels; ++pixels) {
        if (!TTF_SetFontSize(font, static_cast<f32>(pixels)))
          break;
        int height = TTF_GetFontHeight(font);
        if (height > 0 && static_cast<u32>(height) <= requested) {
          u32 units = static_cast<u32>(ascent) + descent;
          u32 lower = static_cast<u32>((static_cast<u64>(requested) * descent + units / 2) / units);
          *em = pixels;
          *top = static_cast<i32>(requested - lower);
          *bottom = -static_cast<i32>(lower);
          found = true;
        }
      }
      if (!found)
        TTF_SetFontSize(font, previous_size);
    }
  }
  // Lucida Console has no VDMX table in the measured installed face. Its unchanged SDL em size and rounded OS/2
  // baseline match the sampled GDI fixed-pitch widths and cell metrics; other no-VDMX families remain provisional.
  const char *family = TTF_GetFontFamilyName(font);
  if (!found && !table && os2 && requested && family && !SDL_strcasecmp(family, "Lucida Console") &&
      TTF_GetFontHeight(font) == static_cast<i32>(requested)) {
    u16 ascent = 0, descent = 0;
    if (FtTable16(data, length, static_cast<usize>(os2) + 74, &ascent) &&
        FtTable16(data, length, static_cast<usize>(os2) + 76, &descent) && ascent + descent) {
      u32 units = static_cast<u32>(ascent) + descent;
      u32 lower = static_cast<u32>((static_cast<u64>(requested) * descent + units / 2) / units);
      *em = static_cast<u32>(TTF_GetFontSize(font));
      *top = static_cast<i32>(requested - lower);
      *bottom = -static_cast<i32>(lower);
      found = *em != 0;
    }
  }
  SDL_free(data);
  return found;
}



// Build the original outline-font sheet or release its initialized resources. The SDL rasterizer uses FreeType
// metrics; their equivalence to GDI metrics remains unverified.
// The reference failure cleanup subsequently reads cleared glyph storage; assert instead of inventing fatal errors.
u8 FtBitmapFont::InitializeOutline(bool enabled) {
  if (!enabled) {
    FtOutlineClose(this);
    if (initialized) {
      sheet->Release();
      sheet = nullptr;
      delete[] glyphs;
      glyphs = nullptr;
      initialized = false;
    }
    return 1;
  }
  if (initialized)
    return 1;
  ScString face;
  if (!FtGetFontSystem()->FontGetName(static_cast<FtType>(type), &face) || !TTF_Init())
    return 0;
  TTF_FontStyleFlags style = TTF_STYLE_NORMAL;
  if (flags & 1)
    style |= TTF_STYLE_BOLD;
  if (flags & 2)
    style |= TTF_STYLE_ITALIC;
  if (flags & 4)
    style |= TTF_STYLE_UNDERLINE;
  if (flags & 8)
    style |= TTF_STYLE_STRIKETHROUGH;
  ScString font_path;
  int font_face = 0;
  TTF_Font *font = FtFindFont(face.ToChar(), static_cast<f32>(size), style, &font_path, &font_face);
  if (!font) {
    TTF_Quit();
    return 0;
  }
  u32 em = 0;
  i32 cell_top = 0, cell_bottom = 0;
  bool cell_metrics =
      size <= INT32_MAX && FtFontCellMetrics(font_path, font_face, font, size, &em, &cell_top, &cell_bottom);
  if (cell_metrics && !TTF_SetFontSize(font, static_cast<f32>(em))) {
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
  }
  u16 strike_units = 0, underline_units = 0;
  i16 strike_position = 0, strike_thickness = 0, underline_position = 0, underline_thickness = 0;
  // The selected face's OS/2 strike and post underline metrics match observed GDI rows when the cell baseline is known.
  // Without that baseline SDL_ttf's placement remains provisional.
  bool own_strike = cell_metrics && (flags & 8) &&
      FtFontDecorationMetrics(
          font_path, font_face, kFtOs2MetricsTableTag, 28, 26, &strike_units, &strike_position, &strike_thickness) &&
      strike_position >= 0 && strike_thickness > 0;
  bool own_underline = cell_metrics && (flags & 4) &&
      FtFontDecorationMetrics(font_path, font_face, kFtPostScriptTableTag, 8, 10, &underline_units, &underline_position,
          &underline_thickness) &&
      underline_position <= 0 && underline_thickness > 0;
  i32 strike_row = 0, strike_rows = 0, underline_row = 0, underline_rows = 0;
  TTF_FontStyleFlags render_style = style;
  if (own_strike) {
    f32 font_size = TTF_GetFontSize(font);
    strike_row = cell_top - static_cast<i32>(SDL_roundf(font_size * strike_position / strike_units));
    strike_rows = static_cast<i32>(SDL_roundf(font_size * strike_thickness / strike_units));
    if (strike_rows < 1)
      strike_rows = 1;
    render_style &= ~TTF_STYLE_STRIKETHROUGH;
  }
  if (own_underline) {
    f32 font_size = TTF_GetFontSize(font);
    underline_row = cell_top - static_cast<i32>(SDL_roundf(font_size * underline_position / underline_units));
    underline_rows = static_cast<i32>(SDL_roundf(font_size * underline_thickness / underline_units));
    if (underline_rows < 1)
      underline_rows = 1;
    render_style &= ~TTF_STYLE_UNDERLINE;
  }
  if (own_strike || own_underline)
    TTF_SetFontStyle(font, render_style);
  u32 characters[256], widths[256], advances[256], total = 0, row_width = 0, rows = 1;
  bool shadow = GetFontFlag(FtStyle::Shadow) != 0;
  // GDI selects SYMBOL_CHARSET for the installed Wingdings face; its symbol cmap maps bytes into U+F000..U+F0FF.
  bool symbol = !SDL_strcasecmp(face.ToChar(), "Wingdings") && !TTF_FontHasGlyph(font, 'A') &&
      TTF_FontHasGlyph(font, 0xf041);
  for (u32 index = 0; index < 256; ++index) {
    characters[index] = symbol ? 0xf000 + index : index;
    int advance = 0;
    if (!TTF_GetGlyphMetrics(font, characters[index], nullptr, nullptr, nullptr, nullptr, &advance) || advance < 0) {
      TTF_CloseFont(font);
      TTF_Quit();
      return 0;
    }
    widths[index] = static_cast<u32>(advance) + shadow;
    advances[index] = type == 2 || type == 3 ? widths[index] : static_cast<u32>(advance);
    row_width += widths[index];
    if (row_width >= 512) {
      ++rows;
      row_width = widths[index];
    }
    total += widths[index];
  }
  int font_height = cell_metrics ? cell_top - cell_bottom : TTF_GetFontHeight(font);
  int baseline_offset = cell_metrics ? cell_top - TTF_GetFontAscent(font) : 0;
  if (font_height <= 0) {
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
  }
  u32 sheet_width = total > 508 ? 512 : total + 4;
  u32 glyph_height = static_cast<u32>(font_height) + 1;
  u32 sheet_height = glyph_height * (total > 508 ? rows : 1);
  if (sheet)
    sheet->Release();
  sheet = nullptr;
  FtGetGraphics()->CreateBuffer(&sheet);
  if (!sheet || !sheet->Init(sheet_width, sheet_height, GxColorType(GxColorType::Color::RGB16), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
    if (sheet)
      sheet->Release();
    sheet = nullptr;
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
  }
  u8 red = tracking == sheet->ConvertRGBValueToNative16Bit(255, 0, 255) ? 1 : 255;
  u16 background = sheet->ConvertRGBValueToNative16Bit(red, 0, 255);
  if (!sheet->Lock(GxLockFlags::Fill)) {
    sheet->Release();
    sheet = nullptr;
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
  }
  sheet->Fill(background, nullptr);
  sheet->Unlock(GxLockFlags::Fill);
  sheet->SetTransparency(background);
  glyphs = new FtGlyph[256];
  if (!sheet->Lock(GxLockFlags::GetSetPixel)) {
    delete[] glyphs;
    glyphs = nullptr;
    sheet->Release();
    sheet = nullptr;
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
  }
  bool success = true;
  u32 x_position = 0, y_position = 0;
  SDL_Color foreground = {255, 255, 255, 255};
  // Render a readable binary mask. Original GDI quality-1 smoothing depends on the display environment and remains
  // unresolved; a 16-bit non-antialiased GDI comparison provides a separate observable reference.
  TTF_SetFontHinting(font, TTF_HINTING_MONO);
  for (u32 index = 0; index < 256; ++index) {
    u32 width = widths[index];
    if (x_position + width >= sheet_width) {
      x_position = 0;
      y_position += glyph_height;
    }
    FtGlyph *glyph = glyphs + index;
    glyph->rectangle.left = static_cast<i32>(x_position);
    glyph->rectangle.top = static_cast<i32>(y_position);
    glyph->rectangle.right = static_cast<i32>(x_position + width);
    glyph->rectangle.bottom = static_cast<i32>(y_position + glyph_height);
    glyph->advance = advances[index];
    x_position += width;
    SDL_Surface *bitmap = TTF_RenderGlyph_Solid(font, characters[index], foreground);
    if (!bitmap) {
      if (width > static_cast<u32>(shadow))
        success = false;
      if (!success)
        break;
      continue;
    }
    if (SDL_MUSTLOCK(bitmap) && !SDL_LockSurface(bitmap)) {
      SDL_DestroySurface(bitmap);
      success = false;
      break;
    }
    u32 pixel_width = width - static_cast<u32>(shadow);
    // SDL shifts negative ink bounds into its temporary surface; GDI clips them at the text origin.
    int left = 0, top = 0;
    if (!TTF_GetGlyphMetrics(font, characters[index], &left, nullptr, nullptr, &top, nullptr)) {
      if (SDL_MUSTLOCK(bitmap))
        SDL_UnlockSurface(bitmap);
      SDL_DestroySurface(bitmap);
      success = false;
      break;
    }
    i32 x_offset = left < 0 ? left : 0;
    i32 extra_top = top > TTF_GetFontAscent(font) ? top - TTF_GetFontAscent(font) : 0;
    for (i32 row = 0; row < font_height; ++row) {
      i32 y = row - baseline_offset + extra_top;
      const u8 *pixels = y >= 0 && y < bitmap->h
          ? static_cast<const u8 *>(bitmap->pixels) + static_cast<usize>(y) * bitmap->pitch : nullptr;
      for (u32 column = 0; column < pixel_width; ++column) {
        i32 x = static_cast<i32>(column) - x_offset;
        bool ink = pixels && x >= 0 && x < bitmap->w && pixels[x];
        if (ink || (own_strike && row >= strike_row && row - strike_row < strike_rows) ||
            (own_underline && row >= underline_row && row - underline_row < underline_rows)) {
          u32 target_x = static_cast<u32>(glyph->rectangle.left) + column;
          u32 target_y = static_cast<u32>(glyph->rectangle.top) + static_cast<u32>(row);
          sheet->SetPixel(target_x, target_y, tracking);
          if (shadow)
            sheet->SetPixel(target_x + 1, target_y + 1, 0);
        }
      }
    }
    if (SDL_MUSTLOCK(bitmap))
      SDL_UnlockSurface(bitmap);
    SDL_DestroySurface(bitmap);
    if (!success)
      break;
  }
  sheet->Unlock(GxLockFlags::GetSetPixel);
  if (!success) {
    TTF_CloseFont(font);
    TTF_Quit();
    delete[] glyphs;
    glyphs = nullptr;
    sheet->Release();
    sheet = nullptr;
    return 0;
  }
  outline = new FtOutlineState{font, nullptr, nullptr, font_height, baseline_offset, strike_row, strike_rows,
      underline_row, underline_rows, shadow, symbol, own_strike, own_underline};
  initialized = true;
  line_height = glyph_height;
  return 1;
}
