#ifndef SC3_UI_LOCAL_ALBUM_H
#define SC3_UI_LOCAL_ALBUM_H

#include "city/city.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "resources/file.h"
#include "ui/commands.h"
#include "ui/controls.h"

class GeAttrib;
class OcOverride;

// Reached5590459E view of cSC3WinSlides; original GetSelection is const and returns a signed ordinal at slot28.
class UiSlidesControl : public ScUnknown {
public:
  virtual u8 AddOriginal(GeAttrib *original, OcOverride *replacement) = 0;
  virtual u8 RemoveAllSlides() = 0;
  virtual u8 ClearAllOverRides() = 0;
  virtual u8 SetOverRide(OcOverride *replacement) = 0;
  virtual i32 GetSelection() const = 0;
  virtual u8 SetSelection(i32 selection) = 0;
  virtual u8 SetPicSize(u32 size) = 0;
  virtual u8 GetMinMaxInfo(ScRect *limits) const = 0;
  virtual u8 SetScrollIndent(u32 indent) = 0;
};

// Original tSlideInfo retains its two attributes independently from the optional lazily created preview child.
struct UiSlideInfo {
  u32 identifier;
  GeAttrib *original;
  OcOverride *replacement;
  bool created;
};

// Actual cSC3WinSlidesOnly is the208-byte inner window that scrolls and lazily creates the visible previews.
class UiSlideList : public UiBaseWindow {
public:
  UiSlideList();
  ~UiSlideList() override;
  bool Init() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 AddOriginal(GeAttrib *original, OcOverride *replacement);
  u8 RemoveAllSlides();
  u8 ClearAllOverRides();
  u8 SetOverRide(u32 index, OcOverride *replacement);
  u8 GetMinMaxInfo(ScRect *limits) const;
  u32 GetPicSize() const;
  u32 GetScrollIndent() const;
  i32 GetSelection() const;
  u8 SetPicSize(u32 size);
  u8 SetScrollIndent(u32 indent);
  u8 SetSelection(i32 selection);
  void Tick();

protected:
  u32 picture_size, gap, scroll_x, word176, total_width, word184;
  i32 selected;
  ScArray<UiSlideInfo> slides;
  u32 scroll_indent;
  u8 CreateSlide(UiSlideInfo *slide, i32 x, i32 y);
  u8 RemoveSlide(UiSlideInfo *slide);
  u8 SetScrollX(u32 position);
};

// Actual244-byte cSC3WinSlides owns its primary slide interface and a separate window subobject at4.
// The held inner list also belongs to the window's child inventory; those two references retire independently.
class UiSlides : public UiSlidesControl, public UiBaseWindow {
public:
  UiSlides();
  ~UiSlides() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 AddOriginal(GeAttrib *original, OcOverride *replacement) override;
  u8 RemoveAllSlides() override;
  u8 ClearAllOverRides() override;
  u8 SetOverRide(OcOverride *replacement) override;
  i32 GetSelection() const override;
  u8 SetSelection(i32 selection) override;
  u8 SetPicSize(u32 size) override;
  u8 GetMinMaxInfo(ScRect *limits) const override;
  u8 SetScrollIndent(u32 indent) override;

protected:
  UiSlideList *list;
  i32 inset_left, inset_top, inset_right, inset_bottom, button_width, button_height;
  u32 pending_command, colors[10], last_scroll;
  u8 CreateSlidesOnly();
  u8 AddButton(u32 button_id, GxBuffer *image, const ScRect &rectangle);
  u8 CalcButtonArea();
  u8 OnScroll(u32 command);
  u8 DrawLine(GxBuffer *target, u32 pixel, i32 x1, i32 y1, i32 x2, i32 y2) const;
  u8 DrawOutline(GxBuffer *target, const ScRect &rectangle, bool raised, bool filled, u32 *palette) const;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiSlideInfo) == 16);
static_assert(sizeof(uptr) != 4 || sizeof(UiSlideList) == 208);
static_assert(sizeof(uptr) != 4 || sizeof(UiSlides) == 244);

class GxBuffer;
class CtCity;
class ScGraphInfoView;

// The bulk command's primary identity exposes only the original Unknown prefix.
class UiBulkSnapshotIdentity : public ScUnknown {};

// Original843171C3 has primary Unknown, command4 and lifetime8 views, plus a separate reference word176.
class UiBulkSnapshotCommand : public UiBulkSnapshotIdentity, public UiCommand {
public:
  UiBulkSnapshotCommand();
  ~UiBulkSnapshotCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;

protected:
  u32 snapshot_references;
};

// OriginalE404E938 has Unknown0, city-enumerator4, command8 and lifetime12 views, and owns a real closed file.
// Original word184 is the capture buffer; the constructor leaves it untouched until capture begins.
class UiSnapshotCommand : public ScModelReceiver, public CtCityEnumerator, public UiCommand {
public:
  UiSnapshotCommand();
  ~UiSnapshotCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool Execute() override;
  u8 DumpGraphData(ScGraphInfoView *model) override;

protected:
  static u8 SC3_STDCALL GetSnapshotDestPath(CtCity *city, ScStringView *out);
  static u8 SC3_STDCALL IsBufferRowBlack(GxBuffer *buffer, i32 row);
  static u8 SC3_STDCALL IsBufferColumnBlack(GxBuffer *buffer, i32 column);
  u8 GetCityThumbnail(GxBuffer **out, u32 width, u32 height, u16 color);
  u8 DoSnapshot(CtCity *city, ScStringView *root, u32 height, u32 width, u16 color);
  u32 snapshot_references;
  GxBuffer *capture_buffer;
  RsFile file;
};

#endif
