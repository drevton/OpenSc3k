#ifndef SC3_FONTS_SC3_BITMAP_FONT_H
#define SC3_FONTS_SC3_BITMAP_FONT_H

#ifdef __cplusplus
#include "graphics/image.h"
#endif

#include "common/sc3_string.h"
#include "graphics/sc3_graphics.h"

struct FtOutlineState;

struct FtGlyph {
  ScRect rectangle;
  u32 advance;
};
#ifdef __cplusplus

enum class FtType : i32 { VariableSerif, VariableSans, FixedSerif, FixedSans, Dingbat, Bitmap, Count };
enum class FtStyle : i32 { Normal = 0, Bold = 1, Italic = 2, Underline = 4, Strike = 8, Shadow = 16 };
enum class FtSource : i32 { Outline = 1, Bitmapped = 2 };
// Original cIGZFontSys::tOutputStyleType values from retail CodeView1855/1856.
enum class FtOutputStyle : i32 { Left, Center, Right };

// Complete retail cIGZFont order through180, CodeView1871/1872. MSVC reverses each original overload group;
// keep DrawTextA and DrawTextPara separate so wrapped colors remain152 and rectangle-only drawing remains176.
class FtFont : public ScUnknown {
public:
  virtual u8 Initialize() = 0;
  virtual u8 Close() = 0;
  virtual u8 BeginDraw(GxBuffer *target) = 0;
  virtual u8 EndDraw(GxBuffer *target) = 0;
  virtual u8 Compare(const FtFont &other) = 0;
  virtual u8 SetPalette(GxPaletteView *value) = 0;
  virtual FtType GetFontType() = 0;
  virtual u32 GetFontSize() = 0;
  virtual u32 Color() = 0;
  virtual u32 EndColor() = 0;
  virtual u8 SetColor(u32 color) = 0;
  virtual u8 SetColors(u32 first, u32 second) = 0;
  virtual u32 GetFontFlags() = 0;
  virtual u8 GetFontFlag(FtStyle value) = 0;
  virtual FtSource GetSrcType() = 0;
  virtual u8 IsCharBreakingChar(u32 code) = 0;
  virtual u8 IsCharReturnChar(u32 code) = 0;
  virtual u8 IsCharWhitespace(u32 code) = 0;
  virtual u8 Contains(u32 code) = 0;
  virtual void TextArea(const char *bytes, u32 length, ScRect *rectangle) = 0;
  virtual i32 Width(u32 code) = 0;
  virtual i32 Height(u32 unused) = 0;
  virtual i32 Advance(u32 code) = 0;
  virtual u32 WrapFit(const char *bytes, u32 length, u32 width, u8 force) = 0;
  virtual u32 Fit(const char *bytes, u32 length, u32 width) = 0;
  virtual u32 WrapLines(const char *bytes, u32 length, u32 width) = 0;
  virtual u32 WrapWidth(const char *bytes, u32 length, u32 height) = 0;
  virtual i32 Measure(const char *bytes, u32 length) = 0;
  virtual i32 GetStringVisibleWidth(const char *bytes, u32 length, u32 *visible_length) = 0;
  virtual u8 DrawChar(GxBuffer *target, i32 x, i32 y, u32 code) = 0;
  virtual u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) = 0;
  virtual u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) = 0;
  virtual u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 color) = 0;
  virtual u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 first, u32 second) = 0;
  virtual u8 DrawCommandText(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) = 0;
  virtual u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length) = 0;
  virtual u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, const ScRect *clip) = 0;
  virtual u8 DrawTextPara(
      GxBuffer *target, const ScRect &box, const char *bytes, u32 length, FtOutputStyle alignment) = 0;
  virtual u8 DrawTextPara(
      GxBuffer *target, u32 *x, u32 *y, const ScRect &box, ScRect *out, const char *bytes, u32 length) = 0;
  virtual u8 DrawTextPara(
      GxBuffer *target, const ScRect &box, u32 indent, const char *bytes, u32 length, const ScRect *clip) = 0;
  virtual u8 DrawTextPara(
      GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 color, FtOutputStyle alignment) = 0;
  virtual u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 first, u32 second,
      FtOutputStyle alignment) = 0;
  virtual u8 GetFontFilePath(ScStringView *out) = 0;
};

// The actual bitmap-font object owns its glyph records, sheet, palette and optional filename.
struct FtBitmapFont : public FtFont {
  u32 type, kind, size, flags;
  FtGlyph *glyphs;
  u32 tracking, line_height;
  GxPaletteView *palette;
  u32 references;
  bool initialized, key_enabled, drawing;
  u32 first, count;
  GxBuffer *sheet;
  ScString *path;
  FtOutlineState *outline = nullptr;
  FtBitmapFont(u32 type, u32 height, u32 flags, u32 fourth);
  FtBitmapFont(const ScStringView *filename, u8 key_enabled);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Initialize() override;
  u8 InitializeOutline(bool enabled);
  u8 InitializeFile(u8 enabled);
  u8 LoadFbf(const ScStringView *filename);
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
  i32 Height(u32 unused) override;
  i32 Advance(u32 code) override;
  u32 WrapFit(const char *bytes, u32 length, u32 width, u8 force) override;
  u32 Fit(const char *bytes, u32 length, u32 width) override;
  u32 WrapLines(const char *bytes, u32 length, u32 width) override;
  u32 WrapWidth(const char *bytes, u32 length, u32 height) override;
  i32 Measure(const char *bytes, u32 length) override;
  i32 GetStringVisibleWidth(const char *bytes, u32 length, u32 *visible_length) override;
  u8 DrawChar(GxBuffer *target, i32 x, i32 y, u32 code) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, const ScRect *clip) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 color) override;
  u8 DrawTextA(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length, u32 first, u32 second) override;
  u8 DrawCommandText(GxBuffer *target, i32 x, i32 y, const char *bytes, u32 length) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, const ScRect *clip) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, FtOutputStyle alignment) override;
  u8 DrawTextPara(
      GxBuffer *target, u32 *x, u32 *y, const ScRect &box, ScRect *out, const char *bytes, u32 length) override;
  u8 DrawTextPara(
      GxBuffer *target, const ScRect &box, u32 indent, const char *bytes, u32 length, const ScRect *clip) override;
  u8 DrawTextPara(
      GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 color, FtOutputStyle alignment) override;
  u8 DrawTextPara(GxBuffer *target, const ScRect &box, const char *bytes, u32 length, u32 first, u32 second,
      FtOutputStyle alignment) override;
  u8 GetFontFilePath(ScStringView *out) override;
  virtual ~FtBitmapFont();
};
#endif

#endif
