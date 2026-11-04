#ifndef SC3_UI_STATUS_WINDOWS_H
#define SC3_UI_STATUS_WINDOWS_H

#include "city/sc_stopwatch.h"
#include "common/sc3_string.h"
#include "ui/controls.h"

class FtFont;
class CtCity;

// The original title-bar layout consists of a resource group, five rectangles and four control positions.
struct UiDateCashLayout {
  u32 image_group;
  ScRect title, population, cash, date, center;
  ScPoint play, pause, button, speed;
};

// The date/cash/title window exposes its primary window through this original secondary identity.
class UiDateCashWindowInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
};

// The original title bar owns six buffer slots, two sets of rectangles and a cached city-title string.
// Its private display-update methods retain explicit return-type uncertainty pending source contract evidence.
class UiDateCashWindow : public UiBaseWindow, public UiDateCashWindowInterface {
public:
  UiDateCashWindow();
  ~UiDateCashWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Shutdown() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  bool DoMessage(ScMessage *message) override;

protected:
  GxBuffer *images[6];
  ScRect image_areas[6], display_areas[6];
  u32 words384[6], words408[6];
  FtFont *font;
  u32 font_color;
  ScString title;
  u32 year, day_of_year;
  bool OnCommand(u32 kind, u32 identifier) override;
  // These nonvirtual methods expose the observed return bytes while their source-level nominal types remain unresolved.
  u8 UpdateTextEffects(i32 index, FtFont *text_font, const ScStringView *text, u32 color);
  u8 UpdateTitleEffects(CtCity *city);
  u8 UpdateCashEffects(CtCity *city);
  u8 UpdateDateEffects();
  u8 UpdatePopulationEffects(CtCity *city);
  // The city/month combination helpers return normalized full EAX; the original nominal types remain unresolved.
  i32 OnCityChangedEffects();
  i32 OnMonthChangedEffects();
  u8 GetLayoutInfoEffects(UiDateCashLayout *layout);
  static bool RestoreField(void *owner, void *field);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiDateCashWindow) == 468);

// The demand meter publishes only the inherited unknown operations and its borrowed window view.
class UiRciWindowInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
};

// Private calculation effects only; the original method's nominal return has not been recovered.
void UiRciUpdateRectangleEffects(ScRect *bar, f32 value, f32 scale, i32 lower, i32 upper);

// SIMUI's demand meter owns one atlas, three bar rectangles and their converted fill colors.
// Initialization, painting and message routing are candidates awaiting connected verification.
class UiRciWindow : public UiBaseWindow, public UiRciWindowInterface {
public:
  UiRciWindow();
  ~UiRciWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Shutdown() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  bool DoMessage(ScMessage *message) override;

protected:
  GxBuffer *atlas;
  ScRect bars[3];
  u32 colors[3];
  f32 scale;
  i32 lower_origin, upper_origin;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiRciWindow) == 244);

// The title-bar pause controls share three images, a hover token and the original blink stopwatch.
class UiPauseToggle : public UiBaseWindow {
public:
  UiPauseToggle();
  ~UiPauseToggle() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;

protected:
  GxBuffer *images[3];
  u32 state, hint_token;
  ScStopwatch stopwatch;
  u32 blink_time;
};

// Pause and play use distinct resource sets and notifications.
class UiPauseTogglePause : public UiPauseToggle {
public:
  bool Init() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;

private:
  bool modal_disabled;
};

class UiPauseTogglePlay : public UiPauseToggle {
public:
  bool Init() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
};

// The speed selector keeps four hit rectangles, six images and four owned tooltip strings.
// Its resource initialization and paint paths use the same graphics resources as the title bar.
class UiSpeedWindow : public UiBaseWindow {
public:
  UiSpeedWindow();
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;

private:
  u8 paused, disabled;
  i8 selection, hovered;
  ScRect hit_areas[4];
  GxBuffer *images[6];
  u32 hint_token;
  ScStringView *hints[4];
  bool modal_disabled;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiPauseToggle) == 248);
static_assert(sizeof(uptr) != 4 || sizeof(UiPauseTogglePause) == 256);
static_assert(sizeof(uptr) != 4 || sizeof(UiSpeedWindow) == 280);

#endif
