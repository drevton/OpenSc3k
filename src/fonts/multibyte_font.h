#ifndef SC3_FONTS_MULTIBYTE_FONT_H
#define SC3_FONTS_MULTIBYTE_FONT_H

#include "fonts/sc3_bitmap_font.h"

struct FtThaiCell;

// Alternate installed font class for multibyte and Thai bitmap glyphs.
class FtMultiFont : public FtFont {
public:
  FtMultiFont(FtType type, u32 height, u32 flags, u32 color);
  FtMultiFont(const ScStringView &filename, bool key_enabled);
  virtual ~FtMultiFont();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Initialize() override;
  u8 Close() override;
  u8 BeginDraw(GxBuffer *target) override;
  u8 EndDraw(GxBuffer *target) override;
  u8 Compare(const FtFont &other) override;
  u8 SetPalette(GxPaletteView *value) override;
  FtType GetFontType() override;
  u32 GetFontSize() override;
  u32 Color() override;
  u32 EndColor() override;
  u8 SetColor(u32 color) override;
  u8 SetColors(u32 first, u32 second) override;
  u32 GetFontFlags() override;
  u8 GetFontFlag(FtStyle value) override;
  FtSource GetSrcType() override;
  u8 IsCharBreakingChar(u32 code) override;
  u8 IsCharReturnChar(u32 code) override;
  u8 IsCharWhitespace(u32 code) override;
  u8 Contains(u32 code) override;
  void TextArea(const char *bytes, u32 length, ScRect *rectangle) override;
  i32 Width(u32 code) override;
  i32 Height(u32 code) override;
  i32 Advance(u32 code) override;
  u32 Fit(const char *bytes, u32 length, u32 width) override;
  u32 WrapFit(const char *bytes, u32 length, u32 width, u8 force) override;
  u32 WrapLines(const char *bytes, u32 length, u32 width) override;
  u32 WrapWidth(const char *bytes, u32 length, u32 height) override;
  i32 Measure(const char *bytes, u32 length) override;
  i32 GetStringVisibleWidth(const char *bytes, u32 length, u32 *visible_length) override;
  u8 DrawChar(GxBuffer *target, i32 x, i32 y, u32 code) override;
  using FtFont::DrawTextA;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 color) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 first, u32 second) override;
  u8 DrawCommandText(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) override;
  using FtFont::DrawTextPara;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, const ScRect *clip) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, u32 indent, const char *bytes, u32 length,
      const ScRect *clip) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length,
      FtOutputStyle alignment) override;
  u8 DrawTextPara(GxBuffer *target, u32 *x, u32 *y, const ScRect &box, ScRect *out,
      const char *bytes, u32 length) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 color,
      FtOutputStyle alignment) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 first, u32 second,
      FtOutputStyle alignment) override;
  u8 GetFontFilePath(ScStringView *out) override;

protected:
  i32 WidthLegacy(u32 code);
  i32 HeightLegacy(u32 code);
  i32 AdvanceLegacy(u32 code);
  void TextAreaLegacy(const char *bytes, u32 length, ScRect *rectangle);
  i32 MeasureLegacy(const char *bytes, u32 length);
  i32 VisibleWidthLegacy(const char *bytes, u32 length, u32 *visible_length);
  u32 FitLegacy(const char *bytes, u32 length, u32 width);
  u32 WrapFitLegacy(const char *bytes, u32 length, u32 width, u8 force);
  u8 DrawCharLegacy(GxBuffer *target, i32 x, i32 y, u32 code);
  u8 DrawTextLegacy(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip);
  u8 DrawCommandLegacy(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length);
  u8 *GetCharPixelStart(u32 code, bool double_byte);
  bool DBCIsLeadByte(u32 code);
  bool DBCIsMaekin(u32 code);
  bool DBCIsAtokin(u32 code);
  i32 GetWord(char *bytes, i32 length);
  u32 GetChar(const char *bytes);
  bool GetCharBit(u8 *pixels, bool double_byte, i32 x, i32 y);
  u32 DrawChar2(GxBuffer *target, i32 x, i32 y, u32 code);
  u32 DrawCharThai(GxBuffer *target, i32 x, i32 y, FtThaiCell *cell);
  u8 LoadDBCFontFile(const ScString &filename);
  u32 references, type, size, flags, color, kind, line_height;
  bool drawing, initialized;
  ScString path, name;
  u32 code_page, unknown;
  void *file_data;
  i32 metrics[11];
};

#endif
