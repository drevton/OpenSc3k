#ifndef SC3_UI_UI_MENU_LABEL_H
#define SC3_UI_UI_MENU_LABEL_H

#include "fonts/sc3_bitmap_font.h"
#include "ui/ui_window.h"

// The label owns one font reference and six target-format colors; Style supplies the otherwise unwritten colors.
class UiMenuLabel : public UiWindow {
public:
  UiMenuLabel();
  ~UiMenuLabel() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool GZPaint() override;
  void SetCaption(const ScStringView &text) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  virtual bool Style(FtFont *font, const u32 *colors);
  virtual bool Fit();
  bool SetLabelFlags(u32 mask, u8 enabled);
  bool GetLabelFlags(u32 mask) const;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;

protected:
  bool IsPointInWindow(i32 x, i32 y) const override;
  FtFont *font;
  u32 colors[6], label_flags;
};

#endif
