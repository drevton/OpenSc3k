#define _CRT_SECURE_NO_WARNINGS
#include "fonts/multibyte_font.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/locale.h"
#include "common/path.h"
#include "common/utf8.h"

// Installed 256-byte glyph-width table at GZService for code page 874.
static const u8 kFtThaiWidth[256] = {
  12, 12, 4, 5, 5, 5, 12, 12, 12, 12, 12, 5, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0, 1,
  5, 2, 3, 5, 5, 5, 5, 1, 2, 2, 5, 5, 2, 5, 2, 5,
  5, 3, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 4, 5, 4, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 3, 5, 5,
  3, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 2, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 1, 3, 5, 4,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10,
  0, 8, 8, 8, 7, 7, 9, 6, 7, 9, 8, 9, 10, 10, 8, 8,
  7, 10, 11, 12, 7, 7, 8, 8, 7, 9, 8, 8, 7, 7, 9, 9,
  8, 7, 6, 7, 8, 6, 8, 7, 8, 10, 6, 8, 11, 6, 7, 6,
  6, 6, 6, 11, 7, 6, 7, 6, 3, 5, 2, 0, 0, 0, 0, 8,
  3, 7, 6, 6, 7, 6, 6, 6, 1, 5, 6, 3, 5, 3, 3, 8,
  7, 7, 9, 8, 10, 10, 7, 9, 8, 8, 11, 11, 0, 0, 9, 0,
};

// Installed Thai baseline and character-class tables and.
static const u8 kFtThaiBase[256] = {
  12, 12, 4, 5, 5, 5, 12, 12, 12, 12, 12, 5, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0, 1,
  5, 2, 5, 5, 5, 5, 5, 2, 2, 2, 5, 5, 2, 5, 2, 5,
  5, 3, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 4, 5, 4, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 3, 5, 5,
  3, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 2, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 1, 3, 5, 4,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9,
  0, 7, 7, 7, 6, 6, 8, 5, 6, 7, 6, 7, 9, 9, 7, 7,
  5, 9, 9, 9, 6, 6, 7, 7, 5, 7, 7, 4, 6, 3, 8, 5,
  7, 6, 5, 5, 8, 5, 8, 6, 6, 7, 5, 7, 8, 5, 5, 6,
  6, 3, 6, 1, 6, 5, 6, 5, 2, 4, 1, 0, 0, 0, 0, 8,
  3, 7, 6, 6, 7, 6, 6, 6, 0, 3, 3, 1, 2, 1, 1, 8,
  7, 7, 9, 8, 10, 10, 7, 9, 8, 8, 11, 11, 0, 0, 9, 0,
};
static const u8 kFtThaiClass[256] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 4, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 1,
  4, 6, 4, 10, 6, 6, 6, 6, 5, 5, 9, 1, 1, 1, 1, 1,
  3, 3, 3, 3, 3, 4, 1, 8, 7, 7, 7, 7, 11, 8, 8, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// Convert a double-byte character to the source font's code-page index. Unknown and invalid pairs select index zero.
static u32 FtDbcsIndex(u32 code, i32 code_page) {
  u32 high = code >> 8, low = code & 255;
  if (code_page == 874)
    return code;
  if (code_page == 932) {
    if (!((high >= 0x81 && high <= 0x9F) || (high >= 0xE0 && high <= 0xFC)) ||
        !((low >= 0x40 && low <= 0x7E) || (low >= 0x80 && low <= 0xFC)))
      return 0;
    u32 column = low < 0x80 ? low - 64 : low - 65;
    return column + 188 * (high < 0xE0 ? high - 129 : high - 193);
  }
  if (code_page == 936 && high >= 0xA1 && high <= 0xFC && low >= 0xA1 && low != 255)
    return 94 * high + low - 15295;
  if (code_page == 949 && high >= 0xB0 && high <= 0xC8 && low >= 0xA1 && low != 255)
    return 94 * high + low - 16705;
  if (code_page == 950 && high >= 0xA1 && high <= 0xF9 &&
      ((low >= 0x40 && low <= 0x7E) || (low >= 0xA1 && low != 255))) {
    u32 column = low < 0x80 ? low - 64 : low - 98;
    return 157 * high + column - 25277;
  }
  return 0;
}

struct FtThaiCell {
  u32 length, width;
  u8 code[3], offset[3], mode[3];
};

// Add one combining character and update the original aggregate height and baseline offset.
static i32 FtThaiAppend(FtThaiCell *cell, u8 code, i32 baseline) {
  u32 index = cell->length;
  i32 next_base = kFtThaiBase[code];
  cell->code[index] = code;
  i32 height = baseline + kFtThaiWidth[code] - next_base;
  if (height > (i32)cell->width)
    cell->width = (u32)height;
  if (next_base > baseline) {
    cell->width += (u32)(next_base - baseline);
    baseline = next_base;
  }
  cell->offset[index] = (u8)(baseline - next_base);
  cell->mode[index] = 1;
  ++cell->length;
  return baseline;
}

// Parse one source Thai cell with at most three combined bytes, preserving its glyph modes and base adjustment.
static void FtThaiParse(FtThaiCell *cell, const u8 *bytes, u32 length) {
  cell->length = 0;
  if (!length)
    return;
  u8 first = bytes[0], first_class = kFtThaiClass[first];
  cell->code[0] = first;
  cell->offset[0] = 0;
  cell->mode[0] = 1;
  cell->width = kFtThaiWidth[first];
  cell->length = 1;
  if (length == 1 || first_class != 2)
    return;
  u8 second = bytes[1], second_class = kFtThaiClass[second];
  if (second_class != 5 && second_class != 6 && second_class != 7 && second_class != 8 &&
      second_class != 9 && second_class != 10 && second_class != 11)
    return;
  if ((second_class == 5 || second_class == 9) && cell->code[0] == 0xAD)
    cell->code[0] = 0x9F;
  i32 baseline = FtThaiAppend(cell, second, kFtThaiBase[first]);
  if (second_class == 7 || second_class == 11)
    cell->mode[1] = 4;
  if (second_class == 8 || second_class == 10 || length == 2)
    return;
  u8 third = bytes[2], third_class = kFtThaiClass[third];
  if (second_class == 6 && third_class != 7 && third_class != 11)
    return;
  if ((second_class == 7 || second_class == 11) && third_class != 10)
    return;
  if ((second_class == 5 || second_class == 9) && third_class != 7 && third_class != 8 && third_class != 11)
    return;
  if ((second_class == 7 || second_class == 11) && third_class == 10)
    cell->mode[1] = 1;
  FtThaiAppend(cell, third, baseline);
  if ((second_class == 5 || second_class == 9) && (third_class == 7 || third_class == 11))
    cell->mode[2] = 4;
}

// Construct the parameter form with empty strings and no loaded data or active drawing state.
FtMultiFont::FtMultiFont(FtType value, u32 height, u32 options, u32 initial_color) {
  type = (u32)value;
  size = height;
  flags = options;
  color = initial_color;
  references = 0;
  kind = 1;
  line_height = 0;
  drawing = initialized = false;
  code_page = unknown = 0;
  file_data = nullptr;
  memset(metrics, 0, sizeof(metrics));
}

// Construct the file form and copy the supplied String through its original length and data interface calls.
FtMultiFont::FtMultiFont(const ScStringView &filename, bool) {
  references = 0;
  type = 5;
  size = 12;
  flags = color = 0;
  kind = 1;
  line_height = 0;
  drawing = initialized = false;
  {
    ScString copy(filename);
    path = copy;
  }
  code_page = unknown = 0;
  file_data = nullptr;
  memset(metrics, 0, sizeof(metrics));
}

FtMultiFont::~FtMultiFont() {
  FtMultiFont::Close();
}

// Publish the font or basic identity and retain the object; unsupported identities clear the output.
bool FtMultiFont::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_FtFont, FtFont);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, FtFont);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 FtMultiFont::AddRef() {
  return ++references;
}

// Release one reference and destroy the complete object when the previous count was one.
u32 FtMultiFont::Release() {
  u32 count = --references;
  if (!count)
    delete this;
  return count;
}

u8 FtMultiFont::Initialize() {
  if (!initialized && LoadDBCFontFile(path)) {
    u32 height = (u32)Height(65);
    size = height;
    line_height = height;
    initialized = true;
  }
  return initialized;
}

// Copy the path and read its font data; successful reads publish the final 14-word metadata record.
u8 FtMultiFont::LoadDBCFontFile(const ScString &filename) {
  path = filename;
  FILE *stream = ScOpenNativeFile(path.begin, "rb");
  if (!stream)
    return 0;
  if (fseek(stream, 0, SEEK_END)) {
    fclose(stream);
    return 0;
  }
  u32 length = (u32)ftell(stream);
  file_data = malloc(length);
  if (!file_data) {
    fclose(stream);
    return 0;
  }
  fseek(stream, 0, SEEK_SET);
  usize read = fread(file_data, 1, length, stream);
  if (read != length) {
    free(file_data);
    file_data = nullptr;
    fclose(stream);
    return 0;
  }
  u32 trailer[14];
  assert(length >= sizeof(trailer));
  memcpy(trailer, static_cast<u8 *>(file_data) + length - sizeof(trailer), sizeof(trailer));
  code_page = trailer[1];
  unknown = trailer[2];
  memcpy(metrics, trailer + 3, sizeof(metrics));
  fclose(stream);
  return 1;
}

u8 FtMultiFont::Close() {
  if (initialized) {
    if (file_data) {
      free(file_data);
      file_data = nullptr;
    }
    initialized = false;
  }
  return 1;
}

u8 FtMultiFont::BeginDraw(GxBuffer *target) {
  return target->Lock(GxLockFlags::Bits);
}

u8 FtMultiFont::EndDraw(GxBuffer *target) {
  return target->Unlock(GxLockFlags::Bits);
}

// Compare type, size, flags and color through the other font's live virtual methods in that order.
u8 FtMultiFont::Compare(const FtFont &other) {
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
  return color == other_color;
}

u8 FtMultiFont::SetPalette(GxPaletteView *) {
  return 1;
}

FtType FtMultiFont::GetFontType() {
  return (FtType)type;
}

u32 FtMultiFont::GetFontSize() {
  return size;
}

u32 FtMultiFont::Color() {
  return color;
}

u32 FtMultiFont::EndColor() {
  return color;
}

u8 FtMultiFont::SetColor(u32 value) {
  color = value;
  return 1;
}

u8 FtMultiFont::SetColors(u32 first, u32) {
  color = first;
  return 1;
}

u32 FtMultiFont::GetFontFlags() {
  return flags;
}

u8 FtMultiFont::GetFontFlag(FtStyle value) {
  return (flags & (u32)value) != 0;
}

FtSource FtMultiFont::GetSrcType() {
  return (FtSource)kind;
}

u8 FtMultiFont::IsCharBreakingChar(u32 code) {
  return code == 32 || code == 45;
}

u8 FtMultiFont::IsCharReturnChar(u32 code) {
  return code == 10;
}

u8 FtMultiFont::IsCharWhitespace(u32 code) {
  return code == 32;
}

// Check whether a page-encoded character has an installed glyph-table index.
static bool FtMultiResourceCode(u32 page, const char *bytes, u32 count, u32 *code);

// Accept line feed and printable scalars only when the installed legacy font page represents them.
u8 FtMultiFont::Contains(u32 code) {
  if (code != 10 && code < 32)
    return 0;
  char encoded[4];
  u32 count = ScEncodeLegacyCharacter(code_page, code, encoded), resource_code;
  return count && FtMultiResourceCode(code_page, encoded, count, &resource_code);
}

// Classify the leading byte using installed entry.
bool FtMultiFont::DBCIsLeadByte(u32 code) {
  u32 lead = code > 255 ? code >> 8 : code;
  switch (code_page) {
  case 932:
    return (lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC);
  case 936:
    return lead >= 0xA1 && lead <= 0xFC;
  case 949:
    return lead >= 0xB0 && lead <= 0xC8;
  case 950:
    return lead >= 0x81 && lead <= 0xFE;
  default:
    return false;
  }
}

// Return the borrowed glyph-data address selected by the code page and source metadata, or null when no font data exists.
u8 *FtMultiFont::GetCharPixelStart(u32 code, bool double_byte) {
  if (!unknown)
    return nullptr;
  u32 offset;
  if (double_byte) {
    i32 page;
    memcpy(&page, &code_page, sizeof(page));
    u32 index = FtDbcsIndex(code, page);
    if (index >= (u32)metrics[8])
      index = 0;
    offset = ((u32)metrics[6] << 8) + index * (u32)metrics[7];
  } else
    offset = code * (u32)metrics[6];
  return reinterpret_cast<u8 *>(reinterpret_cast<uptr>(file_data) + offset);
}

// Convert the source's wrapping arithmetic word to its signed result without signed overflow.
static i32 FtSignedWord(u32 value) {
  i32 result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

// Accept only encoded single- or double-byte indices represented by the installed glyph table layout.
static bool FtMultiResourceCode(u32 page, const char *bytes, u32 count, u32 *code) {
  u32 high = static_cast<u8>(bytes[0]);
  if (count == 1) {
    bool lead = (page == 932 && ((high >= 0x81 && high <= 0x9f) || (high >= 0xe0 && high <= 0xfc))) ||
        (page == 936 && high >= 0xa1 && high <= 0xfc) || (page == 949 && high >= 0xb0 && high <= 0xc8) ||
        (page == 950 && high >= 0x81 && high <= 0xfe);
    if (lead)
      return false;
    *code = high;
    return true;
  }
  if (count != 2)
    return false;
  u32 low = static_cast<u8>(bytes[1]);
  bool valid = (page == 932 && ((high >= 0x81 && high <= 0x9f) || (high >= 0xe0 && high <= 0xfc)) &&
                   ((low >= 0x40 && low <= 0x7e) || (low >= 0x80 && low <= 0xfc))) ||
      (page == 936 && high >= 0xa1 && high <= 0xfc && low >= 0xa1 && low < 0xff) ||
      (page == 949 && high >= 0xb0 && high <= 0xc8 && low >= 0xa1 && low < 0xff) ||
      (page == 950 && high >= 0xa1 && high <= 0xf9 &&
          ((low >= 0x40 && low <= 0x7e) || (low >= 0xa1 && low < 0xff)));
  if (!valid)
    return false;
  *code = (high << 8) | low;
  return true;
}

// Translate a Unicode scalar into an exact installed multibyte bitmap glyph index.
static bool FtMultiGlyphCode(u32 page, u32 scalar, u32 *code) {
  char encoded[4];
  u32 count = ScEncodeLegacyCharacter(page, scalar, encoded);
  return count && FtMultiResourceCode(page, encoded, count, code);
}

// Convert a UTF-8 span at the installed glyph-resource boundary and retain byte-offset correspondence.
// Unrepresentable or malformed characters end the valid prefix rather than being replaced.
struct FtMultiSpan {
  char *bytes;
  u32 *offsets;
  u32 length, input_length;

  // Encode the representable UTF-8 prefix and map each resulting byte back to the input boundary.
  FtMultiSpan(u32 page, const char *input, u32 input_size) :
      bytes(new char[static_cast<usize>(input_size) + 4]()),
      offsets(new u32[static_cast<usize>(input_size) + 4]()), length(0), input_length(0) {
    while (input_length < input_size) {
      u32 scalar, size = ScUtf8Decode(input + input_length, input_size - input_length, &scalar);
      char encoded[4];
      u32 count = size ? ScEncodeLegacyCharacter(page, scalar, encoded) : 0, resource_code;
      if (!count || !FtMultiResourceCode(page, encoded, count, &resource_code) || length + count > input_size)
        break;
      for (u32 i = 0; i < count; ++i) {
        bytes[length++] = encoded[i];
        offsets[length] = input_length;
      }
      input_length += size;
      offsets[length] = input_length;
    }
  }
  // Release both temporary buffers after the resource operation finishes.
  ~FtMultiSpan() {
    delete[] offsets;
    delete[] bytes;
  }
};

// Look up the resource glyph's horizontal extent for one Unicode scalar.
i32 FtMultiFont::Width(u32 code) {
  if (code >= 0xffffff80u)
    return WidthLegacy(code);
  u32 encoded;
  return FtMultiGlyphCode(code_page, code, &encoded) ? WidthLegacy(encoded) : 0;
}

// Look up the resource glyph's height for one Unicode scalar.
i32 FtMultiFont::Height(u32 code) {
  if (code >= 0xffffff80u)
    return HeightLegacy(code);
  u32 encoded;
  return FtMultiGlyphCode(code_page, code, &encoded) ? HeightLegacy(encoded) : 0;
}

// Return the installed glyph's advance for one representable Unicode scalar.
i32 FtMultiFont::Advance(u32 code) {
  if (code >= 0xffffff80u)
    return AdvanceLegacy(code);
  u32 encoded;
  return FtMultiGlyphCode(code_page, code, &encoded) ? AdvanceLegacy(encoded) : 0;
}

void FtMultiFont::TextArea(const char *bytes, u32 length, ScRect *out) {
  FtMultiSpan span(code_page, bytes, length);
  TextAreaLegacy(span.bytes, span.length, out);
}

i32 FtMultiFont::Measure(const char *bytes, u32 length) {
  FtMultiSpan span(code_page, bytes, length);
  return MeasureLegacy(span.bytes, span.length);
}

// Map the installed renderer's visible prefix back to a complete UTF-8 byte offset.
i32 FtMultiFont::GetStringVisibleWidth(const char *bytes, u32 length, u32 *visible_length) {
  FtMultiSpan span(code_page, bytes, length);
  u32 used = 0;
  i32 result = VisibleWidthLegacy(span.bytes, span.length, &used);
  *visible_length = span.offsets[used <= span.length ? used : span.length];
  return result;
}

// Fit an installed-font prefix and return the corresponding UTF-8 byte length.
u32 FtMultiFont::Fit(const char *bytes, u32 length, u32 width) {
  FtMultiSpan span(code_page, bytes, length);
  u32 used = FitLegacy(span.bytes, span.length, width);
  return span.offsets[used <= span.length ? used : span.length];
}

// Wrap with the installed punctuation and Thai cell rules while returning UTF-8 byte offsets.
u32 FtMultiFont::WrapFit(const char *bytes, u32 length, u32 width, u8 force) {
  FtMultiSpan span(code_page, bytes, length);
  u32 used = WrapFitLegacy(span.bytes, span.length, width, force);
  return span.offsets[used <= span.length ? used : span.length];
}

// Draw a Unicode scalar only if the installed bitmap resource can represent it.
u8 FtMultiFont::DrawChar(GxBuffer *target, i32 x, i32 y, u32 code) {
  // Non-Thai original glyph reads use the full widened value and leave the loaded font allocation.
  if (code >= 0xffffff80u)
    return code_page == 874 ? DrawCharLegacy(target, x, y, code) : 0;
  u32 encoded;
  return FtMultiGlyphCode(code_page, code, &encoded) ? DrawCharLegacy(target, x, y, encoded) : 0;
}

u8 FtMultiFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) {
  FtMultiSpan span(code_page, bytes, length);
  return span.input_length == length ? DrawTextLegacy(target, x, y, span.bytes, span.length, clip) : 0;
}

u8 FtMultiFont::DrawCommandText(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) {
  FtMultiSpan span(code_page, bytes, length);
  return span.input_length == length ? DrawCommandLegacy(target, x, y, span.bytes, span.length) : 0;
}

// Read Thai glyph widths from the original table, or combine the selected single- or double-byte metadata words.
i32 FtMultiFont::WidthLegacy(u32 code) {
  if (code_page == 874)
    return code < 256 ? kFtThaiWidth[code] + 1 : 0;
  u32 height = DBCIsLeadByte(code) ? (u32)metrics[3] + (u32)metrics[5] : (u32)metrics[0] + (u32)metrics[2];
  return FtSignedWord(height);
}

// Combine the character-class height and vertical margins, adding one pixel for the shadow style.
i32 FtMultiFont::HeightLegacy(u32 code) {
  u32 width = DBCIsLeadByte(code) ? (u32)metrics[4] : (u32)metrics[1];
  width += (u32)metrics[9] + (u32)metrics[10];
  if (flags & 16)
    ++width;
  return FtSignedWord(width);
}

// Compute the direct glyph-width advance, including the shadow pixel independently of virtual Height overrides.
i32 FtMultiFont::AdvanceLegacy(u32 code) {
  u32 height;
  if (code_page == 874)
    height = code < 256 ? kFtThaiWidth[code] + 1 : 0;
  else
    height = DBCIsLeadByte(code) ? (u32)metrics[3] + (u32)metrics[5] : (u32)metrics[0] + (u32)metrics[2];
  if (flags & 16)
    ++height;
  return FtSignedWord(height);
}

// Compute the source text bounds using Thai combined cells or complete non-Thai multibyte characters.
void FtMultiFont::TextAreaLegacy(const char *bytes, u32 length, ScRect *out) {
  i32 height = HeightLegacy(0);
  out->left = 0;
  out->top = 0;
  out->right = 0;
  out->bottom = height;
  u32 width = 0;
  if (code_page == 874) {
    u32 previous = 0;
    while (length) {
      FtThaiCell cell;
      FtThaiParse(&cell, reinterpret_cast<const u8 *>(bytes), length);
      if (!cell.length)
        break;
      bytes += cell.length;
      length -= cell.length;
      previous = !IsCharWhitespace((u32)(i32)(i8)cell.code[0]);
      width += previous + cell.width;
    }
    width -= previous;
  } else {
    u32 last_code = 0, last_advance = 0;
    while (length) {
      u8 first = (u8)*bytes;
      u32 count = !first ? 0 : (DBCIsLeadByte(first) ? 2 : 1);
      if (!count || count > length)
        break;
      u32 code = first;
      if (count == 2)
        code = (code << 8) | (u8)bytes[1];
      bytes += count;
      length -= count;
      last_code = code;
      last_advance = (u32)FtMultiFont::AdvanceLegacy(code);
      width += last_advance;
    }
    if (width)
      width += (u32)WidthLegacy(last_code) - last_advance;
  }
  out->right = FtSignedWord(width);
}

// Measure Thai combined cells or the live per-character advances of complete non-Thai byte sequences.
i32 FtMultiFont::MeasureLegacy(const char *bytes, u32 length) {
  u32 measured = 0;
  if (code_page == 874) {
    u32 previous = 0;
    for (;;) {
      FtThaiCell cell;
      FtThaiParse(&cell, reinterpret_cast<const u8 *>(bytes), length);
      if (!cell.length)
        break;
      bytes += cell.length;
      length -= cell.length;
      previous = !IsCharWhitespace((u32)(i32)(i8)cell.code[0]);
      if (cell.length == 1 && cell.code[0] == 95)
        previous = 0;
      else
        measured += previous + cell.width;
    }
    measured -= previous;
  } else {
    while (length) {
      u8 first = (u8)*bytes;
      u32 count = !first ? 0 : (DBCIsLeadByte(first) ? 2 : 1);
      if (!count || count > length)
        break;
      measured += (u32)AdvanceLegacy(first);
      bytes += count;
      length -= count;
    }
  }
  return FtSignedWord(measured);
}

// Count successive forced wrapped prefixes through the font's live WrapFit interface.
u32 FtMultiFont::WrapLines(const char *bytes, u32 length, u32 width) {
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

// Probe widths in ten-pixel steps until the live wrapped-line count fits the requested height.
u32 FtMultiFont::WrapWidth(const char *bytes, u32 length, u32 height) {
  u32 rows = height / (u32)Height(0);
  for (u32 width = 20; width < 4000; width += 10)
    if (WrapLines(bytes, length, width) <= rows)
      return width;
  return 1;
}

// Trim trailing Thai whitespace, or scan complete multibyte characters, then measure the retained prefix.
i32 FtMultiFont::VisibleWidthLegacy(const char *bytes, u32 length, u32 *visible_length) {
  u32 used = 0;
  if (code_page == 874) {
    i32 remaining = FtSignedWord(length);
    *visible_length = 0;
    while (remaining > 0 && IsCharWhitespace((u32)(i32)(i8)bytes[remaining - 1]))
      --remaining;
    used = (u32)remaining;
    *visible_length = used;
    if (!used)
      return 0;
  } else {
    while (used < length) {
      u8 first = (u8)bytes[used];
      if (!first)
        break;
      u32 count = DBCIsLeadByte(first) ? 2 : 1;
      if (count > length - used)
        break;
      used += count;
      if (count == 1)
        IsCharWhitespace((u32)(i32)(i8)bytes[used - 1]);
    }
    *visible_length = used;
  }
  return MeasureLegacy(bytes, used);
}

u8 FtMultiFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) {
  ScRect clip{-INT32_MAX, -INT32_MAX, INT32_MAX, INT32_MAX};
  return DrawTextA(target, x, y, bytes, length, &clip);
}

// Reject coordinates outside the selected glyph, then read the corresponding high-to-low packed bitmap bit.
bool FtMultiFont::GetCharBit(u8 *pixels, bool double_byte, i32 x, i32 y) {
  if (!unknown || y < metrics[9] || x < 0)
    return false;
  i32 row = FtSignedWord((u32)y - (u32)metrics[9]);
  i32 width = metrics[double_byte ? 3 : 0], height = metrics[double_byte ? 4 : 1];
  if (x >= width || row < 0 || row >= height)
    return false;
  assert(pixels);
  usize stride = (usize(u32(width)) + 7) / 8;
  u8 pixel = pixels[usize(row) * stride + u32(x) / 8];
  return (pixel & (1u << (7 - u32(x) % 8))) != 0;
}

// Draw an individual byte or character, retaining the original module-wide pending lead byte and Thai baseline.
// The packed glyph data must be readable for every pixel selected by the font metadata.
u8 FtMultiFont::DrawCharLegacy(GxBuffer *target, i32 x, i32 y, u32 code) {
  static u32 pending_lead, thai_width;
  if (code_page != 874) {
    if (pending_lead) {
      code = (pending_lead << 8) | (u8)code;
      pending_lead = 0;
    } else if (code <= 255 && DBCIsLeadByte(code)) {
      pending_lead = code;
      return 1;
    }
    bool double_byte = DBCIsLeadByte(code);
    u8 *pixels = GetCharPixelStart(code, double_byte);
    u8 shadow = GetFontFlag(FtStyle::Shadow), underline = GetFontFlag(FtStyle::Underline);
    u32 buffer_width = target->Width(), buffer_height = target->Height();
    u32 glyph_width = (u32)WidthLegacy(code), glyph_height = (u32)HeightLegacy(code);
    for (u32 row = 0; row < glyph_height; ++row) {
      u32 target_y = (u32)y + row;
      if (target_y >= buffer_height)
        break;
      for (u32 column = 0, target_x = (u32)x; column < glyph_width; ++column, ++target_x) {
        if (target_x >= buffer_width)
          break;
        if (GetCharBit(pixels, double_byte, FtSignedWord(column), FtSignedWord(row))) {
          target->SetPixel(target_x, target_y, color);
          if (shadow && target_y + 1 < buffer_height) {
            target->SetPixel(target_x, target_y + 1, 0);
            if (target_x + 1 < buffer_width)
              target->SetPixel(target_x + 1, target_y + 1, 0);
          }
        }
      }
    }
    if (underline) {
      u32 target_y = GetFontSize() + (u32)y + 1;
      if (target_y < buffer_height) {
        for (u32 column = 0, target_x = (u32)x; column < glyph_width; ++column, ++target_x)
          if (target_x < buffer_width)
            target->SetPixel(target_x, target_y, color);
      }
    }
    return 1;
  }
  i32 buffer_width = FtSignedWord(target->Width()), buffer_height = FtSignedWord(target->Height());
  i32 glyph_width = WidthLegacy(code), glyph_height = HeightLegacy(code);
  u8 character_class = kFtThaiClass[(u8)code];
  bool combining = character_class >= 5 && (character_class <= 9 || character_class == 11);
  if (combining)
    x = FtSignedWord((u32)x - (((u32)glyph_width + thai_width) >> 1));
  else
    thai_width = (u32)glyph_width;
  u8 *pixels = GetCharPixelStart(code, false);
  for (i32 row = 0; row < glyph_height; ++row) {
    i32 target_y = FtSignedWord((u32)y + (u32)row);
    if (target_y >= buffer_height)
      break;
    for (i32 column = 0; column < glyph_width; ++column) {
      i32 target_x = FtSignedWord((u32)x + (u32)column);
      if (target_x >= buffer_width)
        break;
      if (target_x >= 0 && GetCharBit(pixels, false, column, row)) {
        target->SetPixel((u32)target_x, (u32)target_y, color);
        if (GetFontFlag(FtStyle::Shadow) && FtSignedWord((u32)target_y + 1) < buffer_height) {
          target->SetPixel((u32)target_x, (u32)target_y + 1, 0);
          if (FtSignedWord((u32)target_x + 1) < buffer_width)
            target->SetPixel((u32)target_x + 1, (u32)target_y + 1, 0);
        }
      }
    }
  }
  if (!combining && GetFontFlag(FtStyle::Underline)) {
    i32 target_y = FtSignedWord(GetFontSize() + (u32)y + 1);
    if (target_y < buffer_height) {
      for (i32 column = 0; column < glyph_width; ++column) {
        i32 target_x = FtSignedWord((u32)x + (u32)column);
        if (target_x < buffer_width)
          target->SetPixel((u32)target_x, (u32)target_y, color);
      }
    }
  }
  return 1;
}

// Rasterize an encoded glyph with the original live style and buffer callbacks.
u32 FtMultiFont::DrawChar2(GxBuffer *target, i32 x, i32 y, u32 code) {
  if (code_page == 874) {
    static u32 previous_width;
    i32 buffer_width = FtSignedWord(target->Width()), buffer_height = FtSignedWord(target->Height());
    i32 glyph_width = WidthLegacy(code), glyph_height = HeightLegacy(code);
    u8 character_class = kFtThaiClass[(u8)code];
    bool combining = character_class >= 5 && (character_class <= 9 || character_class == 11);
    if (combining)
      x = FtSignedWord((u32)x - (((u32)glyph_width + previous_width) >> 1));
    else
      previous_width = (u32)glyph_width;
    u8 *pixels = GetCharPixelStart(code, false);
    for (i32 row = 0; row < glyph_height; ++row) {
      i32 target_y = FtSignedWord((u32)y + (u32)row);
      if (target_y >= buffer_height)
        break;
      for (i32 column = 0; column < glyph_width; ++column) {
        i32 target_x = FtSignedWord((u32)x + (u32)column);
        if (target_x < 0)
          continue;
        if (target_x >= buffer_width)
          break;
        if (GetCharBit(pixels, false, column, row)) {
          target->SetPixel((u32)target_x, (u32)target_y, color);
          if (GetFontFlag(FtStyle::Shadow) && FtSignedWord((u32)target_y + 1) < buffer_height) {
            target->SetPixel((u32)target_x, (u32)target_y + 1, 0);
            if (FtSignedWord((u32)target_x + 1) < buffer_width)
              target->SetPixel((u32)target_x + 1, (u32)target_y + 1, 0);
          }
        }
      }
    }
    if (combining)
      return 0;
    if (GetFontFlag(FtStyle::Underline)) {
      i32 target_y = FtSignedWord(GetFontSize() + (u32)y + 1);
      if (target_y < buffer_height) {
        for (i32 column = 0; column < glyph_width; ++column) {
          i32 target_x = FtSignedWord((u32)x + (u32)column);
          if (target_x >= buffer_width)
            break;
          target->SetPixel((u32)target_x, (u32)target_y, color);
        }
      }
    }
    return (u32)glyph_width;
  }
  bool double_byte = DBCIsLeadByte(code);
  u8 *pixels = GetCharPixelStart(code, double_byte);
  u8 shadow = GetFontFlag(FtStyle::Shadow), underline = GetFontFlag(FtStyle::Underline);
  u32 buffer_width = target->Width(), buffer_height = target->Height();
  u32 glyph_width = (u32)WidthLegacy(code), glyph_height = (u32)HeightLegacy(code);
  for (u32 row = 0; row < glyph_height; ++row) {
    u32 target_y = (u32)y + row;
    if (target_y >= buffer_height)
      break;
    for (u32 column = 0, target_x = (u32)x; column < glyph_width; ++column, ++target_x) {
      if (target_x >= buffer_width)
        break;
      if (GetCharBit(pixels, double_byte, FtSignedWord(column), FtSignedWord(row))) {
        target->SetPixel(target_x, target_y, color);
        if (shadow && target_y + 1 < buffer_height) {
          target->SetPixel(target_x, target_y + 1, 0);
          if (target_x + 1 < buffer_width)
            target->SetPixel(target_x + 1, target_y + 1, 0);
        }
      }
    }
  }
  if (underline) {
    u32 target_y = GetFontSize() + (u32)y + 1;
    if (target_y < buffer_height) {
      for (u32 column = 0, target_x = (u32)x; column < glyph_width; ++column, ++target_x) {
        if (target_x >= buffer_width)
          break;
        target->SetPixel(target_x, target_y, color);
      }
    }
  }
  return glyph_width;
}

// Draw each glyph in a Thai cell, adjusting the source's special combining marks and optional underline.
// The packed glyph data must be readable for every pixel selected by the font metadata.
u32 FtMultiFont::DrawCharThai(GxBuffer *target, i32 x, i32 y, FtThaiCell *cell) {
  if (cell->code[0] == 95)
    return 0;
  i32 width = FtSignedWord(target->Width()), height = FtSignedWord(target->Height());
  i32 underline_width = FtSignedWord(target->Width()), underline_height = FtSignedWord(target->Height());
  if (cell->length >= 2 && (cell->code[0] == 0xBB || cell->code[0] == 0xBD || cell->code[0] == 0xBF) &&
      (cell->code[1] == 0xD8 || cell->code[1] == 0xD9 || cell->code[1] == 0xDA))
    cell->offset[1] += 3;
  for (u32 index = 0; index < cell->length; ++index) {
    u32 code = cell->code[index];
    i32 glyph_width = WidthLegacy(code), glyph_height = HeightLegacy(code);
    u8 *pixels = GetCharPixelStart(code, false);
    for (i32 column = 0; column < glyph_width; ++column) {
      i32 target_x = FtSignedWord((u32)x + (u32)column + (u32)(i32)(i8)cell->offset[index]);
      if (target_x < 0 || target_x >= width)
        continue;
      for (i32 row = 0; row < glyph_height; ++row) {
        i32 target_y = FtSignedWord((u32)y + (u32)row + (u32)(i32)(i8)cell->mode[index]);
        if (target_y >= 0 && target_y < height && GetCharBit(pixels, false, column, row))
          target->SetPixel((u32)target_x, (u32)target_y, color);
      }
    }
  }
  if (GetFontFlag(FtStyle::Underline)) {
    i32 target_y = FtSignedWord((u32)HeightLegacy(0) + (u32)y - 3);
    i32 limit = FtSignedWord(cell->width + 1);
    if (target_y >= 0 && target_y < underline_height) {
      for (i32 column = 0; column < limit; ++column) {
        i32 target_x = FtSignedWord((u32)x + (u32)column);
        if (target_x >= 0 && target_x < underline_width)
          target->SetPixel((u32)target_x, (u32)target_y, color);
      }
    }
  }
  u8 whitespace = IsCharWhitespace((u32)(i32)(i8)cell->code[0]);
  return cell->width + !whitespace;
}

// Lock the target, parse encoded text and apply the original whole-glyph clip checks before drawing.
// The caller provides readable text and the selected font supplies readable glyph data.
u8 FtMultiFont::DrawTextLegacy(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) {
  bool acquired = false;
  if (!drawing) {
    if (!target->Lock(GxLockFlags::Bits))
      return 0;
    drawing = true;
    acquired = true;
  }
  u32 advance = 0;
  if (code_page == 874) {
    while (length) {
      FtThaiCell cell;
      FtThaiParse(&cell, reinterpret_cast<const u8 *>(bytes), length);
      if (!cell.length)
        break;
      bytes += cell.length;
      length -= cell.length;
      i32 left = FtSignedWord((u32)x + advance);
      if (cell.code[0] != 10 && FtSignedWord((u32)left + cell.width) <= clip->right) {
        i32 bottom = FtSignedWord((u32)y + (u32)HeightLegacy(cell.code[0]));
        if (bottom <= clip->bottom && left >= clip->left && y >= clip->top)
          advance += DrawCharThai(target, left, y, &cell);
      }
    }
  } else {
    u32 lead = 0;
    for (u32 i = 0; i < length; ++i) {
      u32 code = (u8)bytes[i];
      if (!code)
        break;
      if (lead) {
        code |= lead << 8;
        i32 left = FtSignedWord((u32)x + advance);
        if (clip->left == -INT32_MAX)
          DrawChar2(target, left, y, code);
        else {
          i32 right = FtSignedWord((u32)left + (u32)WidthLegacy(code));
          if (right <= clip->right) {
            i32 bottom = FtSignedWord((u32)y + (u32)HeightLegacy(code));
            if (bottom <= clip->bottom && left >= clip->left && y >= clip->top)
              DrawChar2(target, left, y, code);
          }
        }
        advance += (u32)WidthLegacy(code);
        lead = 0;
      } else if (DBCIsLeadByte(code))
        lead = code;
      else if (code != 10) {
        i32 left = FtSignedWord((u32)x + advance);
        i32 right = FtSignedWord((u32)left + (u32)WidthLegacy(code));
        if (right <= clip->right) {
          i32 bottom = FtSignedWord((u32)y + (u32)HeightLegacy(code));
          if (bottom <= clip->bottom && left >= clip->left && y >= clip->top)
            DrawChar2(target, left, y, code);
        }
        advance += (u32)WidthLegacy(code);
      }
    }
  }
  if (acquired) {
    target->Unlock(GxLockFlags::Bits);
    drawing = false;
  }
  return code_page == 874 ? FtSignedWord(advance) > 0 : 1;
}

// Save the current color, draw through the live ordinary text overload, then restore the color.
u8 FtMultiFont::DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 value) {
  u32 old = Color();
  if (!SetColor(value))
    return 0;
  u8 result = DrawTextA(target, x, y, bytes, length);
  SetColor(old);
  return result;
}

// Save both live colors around the ordinary text overload, restoring them after a successful color change.
u8 FtMultiFont::DrawTextA(
    GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 first, u32 second) {
  u32 old_first = Color(), old_second = EndColor();
  if (!SetColors(first, second))
    return 0;
  u8 result = DrawTextA(target, x, y, bytes, length);
  SetColors(old_first, old_second);
  return result;
}

// Draw command text with the original ampersand underline markers and encoded-character advances.
// Marker lookahead reads the caller's next accessible byte, even when the supplied text length ends there.
u8 FtMultiFont::DrawCommandLegacy(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) {
  u32 advance = 0;
  if (code_page == 874) {
    while (length) {
      FtThaiCell cell;
      FtThaiParse(&cell, reinterpret_cast<const u8 *>(bytes), length);
      if (!cell.length)
        break;
      bytes += cell.length;
      length -= cell.length;
      if (cell.code[0] == 38) {
        if ((u8)*bytes != 38)
          DrawCharThai(target, FtSignedWord((u32)x + advance), y, &cell);
      } else if (cell.code[0] != 10)
        advance += DrawCharThai(target, FtSignedWord((u32)x + advance), y, &cell);
    }
  } else {
    u32 lead = 0;
    for (u32 i = 0; i < length; ++i) {
      u32 code = (u8)bytes[i];
      if (!code)
        break;
      if (lead) {
        code |= lead << 8;
        DrawChar2(target, FtSignedWord((u32)x + advance), y, code);
        advance += (u32)WidthLegacy(code);
        lead = 0;
      } else if (DBCIsLeadByte(code))
        lead = code;
      else if (code == 38) {
        if ((u8)bytes[i + 1] != 38)
          DrawChar2(target, FtSignedWord((u32)x + advance), y, 95);
      } else if (code != 10) {
        DrawChar2(target, FtSignedWord((u32)x + advance), y, code);
        advance += (u32)WidthLegacy(code);
      }
    }
  }
  return 1;
}

u8 FtMultiFont::DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length) {
  ScRect clip{-INT32_MAX, -INT32_MAX, INT32_MAX, INT32_MAX};
  return DrawTextPara(target, box, bytes, length, &clip);
}

// Wrap and draw each line inside the box, using the explicit clip unless it is the broad sentinel rectangle.
u8 FtMultiFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, const char *bytes, u32 length, const ScRect *clip) {
  u32 x = (u32)box.left, y = (u32)box.top, height = (u32)Height(0), width = (u32)box.right - x, consumed = 0;
  bool acquired = false;
  if (length) {
    if (!drawing) {
      u8 locked = target->Lock(GxLockFlags::Bits);
      if (!locked)
        return locked;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box.bottom && consumed < length) {
      u32 line = WrapFit(bytes + consumed, length - consumed, width, 1);
      if (clip->left == -INT32_MAX)
        DrawTextA(target, FtSignedWord(x), FtSignedWord(y), bytes + consumed, line);
      else
        DrawTextA(target, FtSignedWord(x), FtSignedWord(y), bytes + consumed, line, clip);
      consumed += line;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Bits);
      drawing = false;
    }
  }
  return 1;
}

// Apply the indent to the first wrapped line only and forward the optional clip to each live draw.
u8 FtMultiFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, u32 indent, const char *bytes, u32 length, const ScRect *clip) {
  u32 x = (u32)box.left, y = (u32)box.top, height = (u32)Height(0), width = (u32)box.right - x, consumed = 0;
  bool acquired = false;
  if (length) {
    if (!drawing) {
      u8 locked = target->Lock(GxLockFlags::Bits);
      if (!locked)
        return locked;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box.bottom && consumed < length) {
      u32 first_indent = consumed ? 0 : indent;
      u32 line = WrapFit(bytes + consumed, length - consumed, width - first_indent, 1);
      if (clip)
        DrawTextA(target, FtSignedWord(x + first_indent), FtSignedWord(y), bytes + consumed, line, clip);
      else
        DrawTextA(target, FtSignedWord(x + first_indent), FtSignedWord(y), bytes + consumed, line);
      consumed += line;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Bits);
      drawing = false;
    }
  }
  return 1;
}

// Align each wrapped row using the captured box width and the live measured line width.
u8 FtMultiFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, const char *bytes, u32 length, FtOutputStyle alignment) {
  u32 x = (u32)box.left, y = (u32)box.top, height = (u32)Height(0), width = (u32)box.right - x, consumed = 0;
  bool acquired = false;
  if (length) {
    if (!drawing) {
      u8 locked = target->Lock(GxLockFlags::Bits);
      if (!locked)
        return locked;
      drawing = true;
      acquired = true;
    }
    while (y < (u32)box.bottom && consumed < length) {
      u32 line = WrapFit(bytes + consumed, length - consumed, width, 1);
      if (alignment == FtOutputStyle::Left)
        x = (u32)box.left;
      else if (alignment == FtOutputStyle::Center) {
        i32 measured = Measure(bytes + consumed, line);
        x = (u32)box.left + (width >> 1) - (u32)(measured / 2);
      } else if (alignment == FtOutputStyle::Right)
        x = (u32)box.right - (u32)Measure(bytes + consumed, line);
      DrawTextA(target, FtSignedWord(x), FtSignedWord(y), bytes + consumed, line);
      consumed += line;
      y += height;
    }
    if (acquired) {
      target->Unlock(GxLockFlags::Bits);
      drawing = false;
    }
  }
  return 1;
}

// Continue wrapped drawing from mutable coordinates and update the occupied rectangle after the final row.
u8 FtMultiFont::DrawTextPara(
    GxBuffer *target, u32 *x, u32 *y, const ScRect &box, ScRect *out, const char *bytes, u32 length) {
  u32 height = (u32)Height(0), consumed = 0, width = (u32)box.right - (u32)box.left;
  if (!length)
    return 1;
  *out = box;
  bool acquired = false;
  if (!drawing) {
    u8 locked = target->Lock(GxLockFlags::Bits);
    if (!locked)
      return locked;
    drawing = true;
    acquired = true;
  }
  u32 lines = 0;
  while (*y < (u32)box.bottom && consumed < length) {
    i32 available = FtSignedWord(width + (u32)box.left - *x);
    u32 line = WrapFit(bytes, length - consumed, (u32)available, 1);
    if (Measure(bytes, line) <= available || FtSignedWord(*x) <= box.left) {
      const ScRect *clip = target->GetBufferArea();
      DrawTextA(target, FtSignedWord(*x), FtSignedWord(*y), bytes, line, clip);
      i32 advance = Measure(bytes, line);
      ++lines;
      if (consumed + line >= length) {
        *x += (u32)advance;
        consumed += line;
        bytes += line;
        continue;
      }
      *x = (u32)box.left;
    } else {
      line = 0;
      ++lines;
      *x = (u32)box.left;
    }
    *y += height;
    consumed += line;
    bytes += line;
  }
  out->right = lines > 1 ? box.right : FtSignedWord(*x);
  out->bottom = FtSignedWord(height + *y);
  if (acquired) {
    target->Unlock(GxLockFlags::Bits);
    drawing = false;
  }
  return 1;
}

// Preserve the live color around the aligned paragraph draw and restore it afterward.
u8 FtMultiFont::DrawTextPara(
    GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 value, FtOutputStyle alignment) {
  u32 old = Color();
  if (!SetColor(value))
    return 0;
  u8 result = DrawTextPara(target, box, bytes, length, alignment);
  SetColor(old);
  return result;
}

// Preserve both color endpoints around the aligned paragraph draw and return its raw byte result.
u8 FtMultiFont::DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 first,
    u32 second, FtOutputStyle alignment) {
  u32 old_first = Color(), old_second = EndColor();
  if (!SetColors(first, second))
    return 0;
  u8 result = DrawTextPara(target, box, bytes, length, alignment);
  SetColors(old_first, old_second);
  return result;
}

u8 FtMultiFont::GetFontFilePath(ScStringView *out) {
  out->FromChar(path.begin, (u32)(path.end - path.begin));
  return path.end != path.begin;
}

// Original code-page punctuation tables..10018700; the first selected page remains cached per process.
static const u16 kFtEmptyPunctuation[] = {0};
static const u16 kFtMaekin932[] = {
  0x8191, 0x818B, 0x8166, 0x8168, 0x81F1, 0x818C, 0x818D, 0x818E, 0x8141, 0x8142, 0x8158, 0x8172,
  0x8174, 0x8176, 0x8178, 0x817A, 0x816C, 0x829F, 0x82A1, 0x82A3, 0x82A5, 0x82A7, 0x82C1, 0x82E1,
  0x82E3, 0x82E5, 0x82EC, 0x814A, 0x814B, 0x8154, 0x8155, 0x8340, 0x8342, 0x8344, 0x8346, 0x8348,
  0x8362, 0x8383, 0x8385, 0x8387, 0x838E, 0x8395, 0x8396, 0x8145, 0x815B, 0x8152, 0x8153, 0x8149,
  0x8193, 0x816A, 0x8143, 0x8144, 0x8146, 0x8147, 0x8148, 0x816E, 0x8170, 0,
};
static const u16 kFtMaekin936[] = {
  0xA1A2, 0xA1A3, 0xA1A4, 0xA1A5, 0xA1A6, 0xA1A7, 0xA1A8, 0xA1A9, 0xA1AC, 0xA1AD, 0xA1AF, 0xA1B1,
  0xA1B3, 0xA1B5, 0xA1B7, 0xA1B9, 0xA1BB, 0xA1BD, 0xA1BF, 0xA1C3, 0xA3A1, 0xA3A2, 0xA3A7, 0xA3A9,
  0xA3AC, 0xA3AE, 0xA3BA, 0xA3BB, 0xA3BF, 0xA3DD, 0xA3E0, 0xA3FC, 0xA844, 0,
};
static const u16 kFtMaekin949[] = {
  0xA1A2, 0xA1A3, 0xA1A4, 0xA1A5, 0xA1A6, 0xA1A7, 0xA1A8, 0xA1AF, 0xA1B1, 0xA1B3, 0xA1B5, 0xA1B7,
  0xA1B9, 0xA1BB, 0xA1BD, 0xA3A9, 0xA3DD, 0xA3FD, 0xBAA3, 0xBDBA, 0xC0CC, 0xC6AE, 0xC6F9, 0,
};
static const u16 kFtMaekin950[] = {
  0xA1E3, 0xA1CA, 0xA15B, 0xA24E, 0xA2B1, 0xA1CC, 0xA2B2, 0xA243, 0xA24C, 0xA1EC, 0xA24D, 0xA2B4,
  0xA173, 0xA16F, 0xA34E, 0xF9DE, 0xA273, 0xF9F0, 0xA2B5, 0xA1CD, 0xA2B6, 0xA1B0, 0xA1B8, 0xA2B7,
  0xA15D, 0xA15F, 0xA1B3, 0xA1B4, 0xA2B8, 0xA15E, 0xA160, 0xA2AF, 0xA1C5, 0xA1DE, 0xA1F0, 0xA1F1,
  0xA1F2, 0xA1D3, 0xA271, 0xA2A6, 0xF9E1, 0xF9EA, 0xF9F3, 0xA157, 0xA159, 0xA278, 0xF9F8, 0xA279,
  0xA149, 0xA249, 0xA1AD, 0xA243, 0xA248, 0xA1F4, 0xA1AE, 0xA1AF, 0xA17D, 0xA17E, 0xA1C4, 0xA1CF,
  0xA1FC, 0xA161, 0xA163, 0xA1A1, 0xA162, 0xA164, 0xA1A2, 0xA1A7, 0xA1A8, 0xA1A9, 0xA1B2, 0xA146,
  0xA151, 0xA3B5, 0xA141, 0xA14D, 0xA142, 0xA14E, 0xA3AE, 0xA1D5, 0xA1E0, 0xA171, 0xA1D8, 0xA1F6,
  0xA1E7, 0xA16D, 0xA16F, 0xA173, 0xA1F8, 0xA2A9, 0xA2AB, 0xA347, 0xA27A, 0xA27E, 0xF9DD, 0xF9E6,
  0xF9EF, 0xF9FA, 0xA1E8, 0xA1FA, 0xA27C, 0xA2A2, 0xF9E3, 0xF9EC, 0xF9F5, 0xF9FC, 0xA144, 0xA143,
  0xA14F, 0xA145, 0xA150, 0xA3B2, 0xA1D2, 0xA1DC, 0xA1EE, 0xA1EF, 0xA1F3, 0xA14B, 0xA14C, 0xA1D6,
  0xA1E1, 0xA172, 0xA1D9, 0xA1F7, 0xA16E, 0xA170, 0xA27D, 0xA2A3, 0xF9E5, 0xF9EE, 0xF9F7, 0xA1FB,
  0xA1F9, 0xA174, 0xA1E9, 0xA274, 0xA2A7, 0xF9E2, 0xF9EB, 0xF9F4, 0xA2A8, 0xA2AA, 0xA148, 0xA153, 0,
};
static const u16 kFtAtokin932[] = {
  0x8192, 0x8165, 0x8167, 0x8171, 0x8173, 0x8175, 0x8177, 0x8179, 0x816B, 0x8190, 0x8169, 0x816D,
  0x816F, 0x8192, 0x818F, 0,
};
static const u16 kFtAtokin936[] = {
  0xA1AE, 0xA1B0, 0xA1B2, 0xA1B4, 0xA1B6, 0xA1B8, 0xA1BA, 0xA1BC, 0xA1BE, 0xA3A8, 0xA3DA, 0,
};
static const u16 kFtAtokin949[] = {
  0xA1AE, 0xA1B0, 0xA1B2, 0xA1B4, 0xA1B6, 0xA1B8, 0xA1BA, 0xA1BC, 0xA3A8, 0xA3DB, 0xA3FB, 0,
};
static const u16 kFtAtokin950[] = {0xA1A1, 0xA1A3, 0xA1A5, 0xA1A7, 0xA1A9, 0xA1AB, 0};

// Select a source table only when a recognized code page is first encountered.
static const u16 *FtPunctuationTable(u32 page, const u16 *japanese, const u16 *chinese, const u16 *korean,
    const u16 *traditional) {
  switch (page) {
  case 874:
    return kFtEmptyPunctuation;
  case 932:
    return japanese;
  case 936:
    return chinese;
  case 949:
    return korean;
  case 950:
    return traditional;
  default:
    return nullptr;
  }
}

// Match an encoded pair, or the exact installed ASCII set including its terminating zero entry.
static bool FtPunctuationContains(const u16 *table, u32 code, const u8 *single, u32 single_count) {
  if (code < 256) {
    for (u32 index = 0; index < single_count; ++index)
      if (code == single[index])
        return true;
    return false;
  }
  assert(table);
  u16 pair = (u16)code;
  for (const u16 *item = table; *item; ++item)
    if (*item == pair)
      return true;
  return false;
}

bool FtMultiFont::DBCIsMaekin(u32 code) {
  static const u8 single[] = {',', '.', ':', ';', '?', '!', ')', ']', '}', 0};
  const u16 *table = FtPunctuationTable(code_page, kFtMaekin932, kFtMaekin936, kFtMaekin949, kFtMaekin950);
  return FtPunctuationContains(table, code, single, (u32)sizeof(single));
}

bool FtMultiFont::DBCIsAtokin(u32 code) {
  static const u8 single[] = {'(', '$', 0};
  const u16 *table = FtPunctuationTable(code_page, kFtAtokin932, kFtAtokin936, kFtAtokin949, kFtAtokin950);
  return FtPunctuationContains(table, code, single, (u32)sizeof(single));
}

// Scan the original word boundary, preserving lookahead beyond the logical length and callback ordering.
i32 FtMultiFont::GetWord(char *bytes, i32 length) {
  if (length <= 0)
    return 0;
  u32 limit = (u32)length, consumed = 0;
  if (code_page == 874) {
    for (;;) {
      u8 current = (u8)bytes[consumed++];
      if (current < 32)
        return (i32)consumed;
      if ((u8)bytes[consumed] < 32 || IsCharBreakingChar(current))
        return (i32)consumed;
      if (consumed >= limit)
        return (i32)consumed;
    }
  }
  u8 first = (u8)bytes[0];
  if (!first)
    return 0;
  u32 char_size = DBCIsLeadByte(first) ? 2 : 1;
  u32 previous = first;
  if (char_size == 2)
    previous = (previous << 8) | (u8)bytes[1];
  DBCIsMaekin(previous);
  bool previous_atokin = DBCIsAtokin(previous), previous_double = char_size > 1, previous_control = previous < 32;
  consumed = char_size;
  for (;;) {
    u8 next = (u8)bytes[consumed];
    if (!next || consumed >= limit)
      break;
    u32 next_size = DBCIsLeadByte(next) ? 2 : 1;
    u32 current = next;
    if (next_size == 2)
      current = (current << 8) | (u8)bytes[consumed + 1];
    bool current_maekin = DBCIsMaekin(current), current_atokin = DBCIsAtokin(current);
    if (previous_control || current < 32 ||
        (!current_maekin && !previous_atokin &&
            (previous_double || next_size > 1 ||
                (IsCharBreakingChar((u32)(i32)(i8)previous) &&
                    !IsCharBreakingChar((u32)(i32)(i8)current)))))
      break;
    consumed += next_size;
    previous_control = false;
    previous_atokin = current_atokin;
    previous = (u8)current;
    previous_double = next_size > 1;
  }
  return (i32)(consumed > limit ? limit : consumed);
}

// Return the next encoded character's byte length, using the original Thai word rule when applicable.
u32 FtMultiFont::GetChar(const char *bytes) {
  if (code_page == 874)
    return (u32)GetWord(const_cast<char *>(bytes), 3);
  u8 first = (u8)*bytes;
  return first ? 1 + DBCIsLeadByte(first) : 0;
}

// Fit successive whole encoded characters using the live measured prefix width.
u32 FtMultiFont::FitLegacy(const char *bytes, u32 length, u32 width) {
  u32 consumed = 0, measured = 0, last = 0;
  while (consumed < length && measured < width) {
    u8 first = (u8)bytes[consumed];
    if (!first)
      last = 1;
    else
      last = GetChar(bytes + consumed);
    if (last + consumed > length)
      break;
    consumed += last;
    measured = (u32)MeasureLegacy(bytes, consumed);
  }
  if (measured > width)
    consumed -= last;
  return consumed;
}

// Fit whole words and optionally force the first one, preserving live return, width and whitespace callbacks.
u32 FtMultiFont::WrapFitLegacy(const char *bytes, u32 length, u32 width, u8 force) {
  u32 consumed = 0, measured = 0;
  const char *next = bytes;
  if (width) {
    while (consumed < length) {
      if (!*next)
        break;
      if (IsCharReturnChar((u32)(i32)(i8)*next)) {
        ++consumed;
        break;
      }
      u32 remaining = length - consumed;
      i32 logical_length;
      memcpy(&logical_length, &remaining, sizeof(logical_length));
      u32 word = (u32)GetWord(const_cast<char *>(next), logical_length);
      u32 end = consumed + word;
      if (end > length)
        break;
      u32 visible_length;
      i32 visible_width = VisibleWidthLegacy(next, word, &visible_length);
      if (measured + (u32)visible_width > width)
        break;
      measured += (u32)MeasureLegacy(next, word);
      next += word;
      consumed = end;
      if (measured >= width)
        break;
    }
  }
  if (!consumed && force) {
    i32 logical_length;
    memcpy(&logical_length, &length, sizeof(logical_length));
    consumed = (u32)GetWord(const_cast<char *>(bytes), logical_length);
  }
  if (consumed > 1 && consumed < kScIntegerSignBit && !IsCharWhitespace((u32)(i32)(i8)bytes[consumed - 1]) &&
      consumed < length && IsCharWhitespace((u32)(i32)(i8)bytes[consumed]))
    consumed += GetChar(bytes + consumed);
  return consumed;
}
