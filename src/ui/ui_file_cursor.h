#ifndef SC3_UI_UI_FILE_CURSOR_H
#define SC3_UI_UI_FILE_CURSOR_H

#include "common/sc3_string.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"

// The application's file cursor owns its filename and image; the captured graphics interface remains borrowed.
class UiFileCursor : public WnCursor {
public:
  explicit UiFileCursor(const ScString &name);
  virtual ~UiFileCursor();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool LoadCursor(ScStringView *name) override;
  void SetCursor() override;
  void GetHotSpot(i32 *x, i32 *y) override;

protected:
  bool hardware;
  GxGraphics *graphics;
  ScString filename;
  GxBuffer *image;

  struct Tail {
    i16 x, y;
    u32 references;
  } tail;

  void *cursor;
};

GxGraphics *ApGetGraphics();

#endif
