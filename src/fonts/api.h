#ifndef SC3_FONTS_API_H
#define SC3_FONTS_API_H

#include "fonts/sc3_bitmap_font.h"

class GxGraphics;
class ScLanguageManager;
class FtFontSystem;

GxGraphics *FtGetGraphics();
ScLanguageManager *FtGetLanguageManager();
FtFontSystem *FtGetFontSystem();

// Retail cIGZFontSys type 5C4B and field list 86F0 establish these original service operations.
// MSVC reverses the acquisition overloads in the table: filename at slot20, attributes at slot24.
class FtFontSystem : public ScUnknown {
public:
  // The installed primary entries and secondary system-service thunks share these Boolean methods.
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual FtFont *FontAcquire(FtType type, u32 size, u32 style, u32 source) = 0;
  virtual FtFont *FontAcquire(const ScStringView &filename) = 0;
  virtual u32 FontRelease(FtFont *font) = 0;
  virtual u8 FontSetName(FtType type, const ScStringView &name) = 0;
  virtual u8 FontGetName(FtType type, ScStringView *name) = 0;
};

#endif
